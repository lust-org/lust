#!/usr/bin/env python3
"""The usage text of `lust`: `--help` lists every library's documentation.

usage: test_usage.py LUST NAME=doc-shortform.txt...

`lust` prints the usage (to stderr) for invalid options. `-h` is one of
them: it prints the options of the stock luajit and points to `--help`,
which adds a LIBRARIES section with each library's doc-shortform.txt,
indented below its name, and exits successfully.
"""

import subprocess
import sys

lust, docs = sys.argv[1], dict(a.split("=", 1) for a in sys.argv[2:])


def run(*args):
    return subprocess.run([lust, *args], capture_output=True, text=True)


for flag in ("-h", "--no-such-option"):
    r = run(flag)
    assert r.returncode != 0, f"{flag} must fail"
    assert r.stderr.startswith("usage: "), r.stderr
    assert "Available options are:" in r.stderr and "  -e chunk" in r.stderr, r.stderr
    assert "--help" in r.stderr, f"{flag} does not mention --help"
    assert "LIBRARIES" not in r.stderr, f"{flag} shows the libraries"

r = run("--help")
out = r.stderr
assert r.returncode == 0, "--help must succeed"
assert out.startswith("usage: "), out
assert "Available options are:" in out and "  -e chunk" in out, out

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
