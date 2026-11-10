#!/usr/bin/env python3
"""The usage text of `lust`: `-h` lists the options, `--help` also every
library's documentation.

usage: test_usage.py LUST LUAJIT NAME=doc-shortform.txt...

`lust` prints the usage (to stderr): the options of the stock luajit
(LUAJIT, the vendored one, whose option lines must all be there) plus
`-h|--help`. `-h` and `--help` exit successfully, `--help` adds a
LIBRARIES section with each library's doc-shortform.txt, indented below
its name. Invalid options print the usage without LIBRARIES and fail.
"""

import subprocess
import sys

lust, luajit = sys.argv[1], sys.argv[2]
docs = dict(a.split("=", 1) for a in sys.argv[3:])


def run(*args, exe=lust):
    return subprocess.run([exe, *args], capture_output=True, text=True)


# The option lines of the stock luajit (it prints its usage for any
# invalid option): lust must not lose one when LuaJIT gains an option.
stock = [l for l in run("-h", exe=luajit).stderr.splitlines() if l.startswith("  -")]
assert stock, "no options in the usage text of luajit"


def check_options(flag, out):
    assert out.startswith("usage: "), out
    assert "\nOPTIONS\n" in out, f"{flag}: no OPTIONS section in:\n{out}"
    lines = out.splitlines()
    for line in stock + ["  -h|--help Show this help text"]:
        assert line in lines, f"{flag}: missing option line {line!r}"


for flag, ok in (("-h", True), ("--no-such-option", False)):
    r = run(flag)
    assert (r.returncode == 0) == ok, f"{flag}: exit status {r.returncode}"
    check_options(flag, r.stderr)
    assert "LIBRARIES" not in r.stderr, f"{flag} shows the libraries"

r = run("--help")
out = r.stderr
assert r.returncode == 0, "--help must succeed"
check_options("--help", out)

head, sep, libs = out.partition("\nLIBRARIES\n")
assert sep, f"no LIBRARIES section in:\n{out}"
for name, path in sorted(docs.items()):
    with open(path, encoding="utf-8") as f:
        for line in f.read().splitlines():
            line = line.rstrip()
            want = "    " + line if line else ""
            assert want in libs.splitlines(), f"{name}: missing {line!r}"
    assert f"\n  {name}\n" in "\n" + libs, f"library {name} not listed"
print("ok  --help lists", ", ".join(sorted(docs)))
