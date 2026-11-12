#!/usr/bin/env python3
"""Thin shim around meson for building, testing and installing lust.

Targets (for `build` and `install`):
  all      everything (the default)
  lust     the statically linked `lust` interpreter
  libs     all libraries as loadable .so modules
  <name>   a single library, e.g. `hash.xxh` or `sys.env` (see libs/)

Examples:
  ./dev.py build                      # everything
  ./dev.py build hash.xxh             # only libs/hash/xxh as lust/hash/xxh.so
  ./dev.py build lust -D libs=hash.xxh,pcre2 # configure which libs get built
  ./dev.py test
  ./dev.py test --suite pcre2 -v      # options go to `meson test`
  ./dev.py install lust --prefix /usr # needs root, e.g. via sudo/pkexec
  ./dev.py install libs --destdir pkg # staging for distro packaging
  ./dev.py build --musl-static        # static `lust` with musl -> build-musl/src/lust
  ./dev.py test --musl-static         # its tests

--musl-static compiles with musl-gcc and links `lust` fully statically
(meson option static=true) in its own build directory (default:
build-musl). Only `lust` is built: no .so modules (a static binary can't
load them, so libraries with lib_embed = false are left out), and the
LuaJIT FFI can't resolve C symbols (ffi.C, ffi.load).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def run(*cmd, env=None):
    prefix = "".join(f"{k}={v} " for k, v in (env or {}).items())
    print("+", prefix + " ".join(map(str, cmd)), file=sys.stderr, flush=True)
    r = subprocess.run(cmd, cwd=ROOT, env={**os.environ, **env} if env else None)
    if r.returncode:
        sys.exit(r.returncode)


def library_names():
    """Libraries are the libs/<name>/ dirs with a meson.build, libs/<ns>/<name>/
    is <ns>.<name> (libs/lust/ only holds targets)."""
    libs = ROOT / "libs"
    return sorted(
        ".".join(p.parent.relative_to(libs).parts)
        for p in libs.glob("**/meson.build")
        if p.parent != libs and p.relative_to(libs).parts[0] != "lust"
    )


def configured_static(builddir):
    """The `static` option of an existing build directory."""
    info = ROOT / builddir / "meson-info" / "intro-buildoptions.json"
    opts = json.loads(info.read_text()) if info.exists() else []
    return any(o["name"] == "static" and o["value"] is True for o in opts)


def configure(args):
    """Create or update the build directory so it matches the options."""
    opts = [f"-D{d}" for d in args.define]
    if args.prefix:
        opts.append(f"--prefix={args.prefix}")
    if not (ROOT / args.builddir / "build.ninja").exists():
        env, setup = None, []
        if args.musl_static:
            if not shutil.which("musl-gcc"):
                sys.exit("--musl-static needs musl-gcc (Arch: musl, Ubuntu: musl-tools)")
            env, setup = {"CC": "musl-gcc"}, ["-Dstatic=true"]
        run("meson", "setup", args.builddir, *setup, *opts, env=env)
    else:
        # The compiler is fixed when the build directory is created: a
        # glibc one can't be switched over. (Without --musl-static, -B
        # build-musl just goes on as it was set up.)
        if args.musl_static and not configured_static(args.builddir):
            sys.exit(f"{args.builddir} is not a --musl-static build directory, "
                     "pick another one with -B (or remove it)")
        if opts:
            run("meson", "configure", args.builddir, *opts)


def check_targets(targets, builddir):
    known = {"all", "lust", "libs", *library_names()}
    if configured_static(builddir):
        mods = [t for t in targets if t not in ("all", "lust") and t in known]
        if mods:
            sys.exit(f"{', '.join(mods)}: {builddir} is a --musl-static build, it has no .so modules "
                     "(the libraries are inside `lust`: target all or lust)")
    unknown = [t for t in targets if t not in known]
    if unknown:
        sys.exit(f"unknown target(s): {', '.join(unknown)} (known: {', '.join(sorted(known))})")
    return targets


def compile_(args):
    configure(args)
    targets = check_targets(args.targets, args.builddir)
    if "all" in targets:
        run("meson", "compile", "-C", args.builddir)
    else:
        # Libraries are exposed by meson as the `lib-<name>` aliases.
        names = [f"lib-{t}" if t in library_names() else t for t in targets]
        run("meson", "compile", "-C", args.builddir, *names)


def install(args):
    compile_(args)
    cmd = ["meson", "install", "-C", args.builddir, "--no-rebuild"]
    if args.destdir:
        cmd += ["--destdir", str(Path(args.destdir).resolve())]
    if "all" not in args.targets:
        # Every installed file carries an install tag: `lust` or `lib-<name>`.
        tags = []
        for t in args.targets:
            tags += (
                [f"lib-{n}" for n in library_names()] if t == "libs"
                else [t if t == "lust" else f"lib-{t}"]
            )
        cmd += ["--tags", ",".join(tags)]
    run(*cmd)


def test(args):
    configure(args)
    run("meson", "test", "-C", args.builddir, *args.targets)


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("-B", "--builddir", help="build directory (default: build, build-musl with --musl-static)")
    common.add_argument("--musl-static", action="store_true",
                        help="build with musl-gcc, `lust` linked fully statically (no .so modules, no FFI C symbols)")
    common.add_argument("-D", "--define", action="append", default=[], metavar="OPT=VAL",
                        help="meson option, repeatable (see meson_options.txt)")
    common.add_argument("--prefix", help="install prefix (default: meson's, /usr/local)")

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("build", parents=[common], help="build lust and/or libraries")
    p.add_argument("targets", nargs="*", default=["all"])
    p.set_defaults(fn=compile_)

    p = sub.add_parser("install", parents=[common], help="install lust and/or libraries (like make install)")
    p.add_argument("targets", nargs="*", default=["all"])
    p.add_argument("--destdir", help="stage into this directory instead of the real root")
    p.set_defaults(fn=install)

    # No positional arguments: everything dev.py doesn't know is collected
    # in order (see below) and goes to `meson test`, e.g. --suite hash.xxh.
    p = sub.add_parser("test", parents=[common], help="run the tests (extra args go to meson test)")
    p.set_defaults(fn=test)

    # `test` forwards what it doesn't know to meson test (e.g. --suite hash.xxh
    # -v, test names); for the other commands that is an error.
    args, extra = ap.parse_known_args()
    if args.builddir is None:
        args.builddir = "build-musl" if args.musl_static else "build"
    if args.cmd == "test":
        args.targets = extra
    elif extra:
        ap.error(f"unrecognized arguments: {' '.join(extra)}")
    args.fn(args)


if __name__ == "__main__":
    main()
