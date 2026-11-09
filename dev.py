#!/usr/bin/env python3
"""Thin shim around meson for building, testing and installing lust.

Targets (for `build` and `install`):
  all      everything (the default)
  lust     the statically linked `lust` interpreter
  libs     all libraries as loadable .so modules
  <name>   a single library, e.g. `xxh` (see libs/)

Examples:
  ./dev.py build                      # everything
  ./dev.py build xxh                  # only libs/xxh as lust/xxh.so
  ./dev.py build lust -D libs=xxh,pcre2 # configure which libs get built
  ./dev.py test
  ./dev.py test --suite pcre2 -v      # options go to `meson test`
  ./dev.py install lust --prefix /usr # needs root, e.g. via sudo/pkexec
  ./dev.py install libs --destdir pkg # staging for distro packaging
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def run(*cmd):
    print("+", " ".join(map(str, cmd)), file=sys.stderr, flush=True)
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode:
        sys.exit(r.returncode)


def library_names():
    """Libraries are the libs/<name>/ dirs (libs/lust/ only holds targets)."""
    return sorted(
        p.name
        for p in (ROOT / "libs").iterdir()
        if p.name != "lust" and (p / "meson.build").exists()
    )


def configure(args):
    """Create or update the build directory so it matches the options."""
    opts = [f"-D{d}" for d in args.define]
    if args.prefix:
        opts.append(f"--prefix={args.prefix}")
    if not (ROOT / args.builddir / "build.ninja").exists():
        run("meson", "setup", args.builddir, *opts)
    elif opts:
        run("meson", "configure", args.builddir, *opts)


def check_targets(targets):
    known = {"all", "lust", "libs", *library_names()}
    unknown = [t for t in targets if t not in known]
    if unknown:
        sys.exit(f"unknown target(s): {', '.join(unknown)} (known: {', '.join(sorted(known))})")
    return targets


def compile_(args):
    configure(args)
    targets = check_targets(args.targets)
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
    common.add_argument("-B", "--builddir", default="build", help="build directory (default: build)")
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
    # in order (see below) and goes to `meson test`, e.g. --suite xxh.
    p = sub.add_parser("test", parents=[common], help="run the tests (extra args go to meson test)")
    p.set_defaults(fn=test)

    # `test` forwards what it doesn't know to meson test (e.g. --suite xxh
    # -v, test names); for the other commands that is an error.
    args, extra = ap.parse_known_args()
    if args.cmd == "test":
        args.targets = extra
    elif extra:
        ap.error(f"unrecognized arguments: {' '.join(extra)}")
    args.fn(args)


if __name__ == "__main__":
    main()
