#!/usr/bin/env python3
"""Guard the startup cost of `lust` relative to plain luajit.

usage: test_startup.py <lust> <luajit> <number of embedded libraries>

Every start of `lust` pays for whatever it links in, used or not. With many
libraries that must stay near zero, so:

1. `lust` must not depend on more shared libraries than the plain luajit
   built from the same sources. A library's dynamic dependency would be
   mapped by the loader on every start (see AGENTS.md: use an `ffi`
   library, which dlopens lazily, or lib_embed = false).
2. Where `perf` works: the user space instructions of an empty run may
   exceed plain luajit's by a small budget only. Instructions don't suffer
   from timing noise, unlike wall time.
"""

import os
import re
import statistics
import subprocess
import sys

SKIP = 77  # meson: skipped

# Fixed overhead of lust (searcher, package.path ...) and a generous
# allowance per embedded library. The measured overhead is far below this;
# a mapped shared library (e.g. libz) costs ~60000.
BASE_BUDGET = 15000
PER_LIB_BUDGET = 500


def needed(exe):
    r = subprocess.run(["readelf", "-d", exe], capture_output=True, text=True)
    if r.returncode:
        return None
    return sorted(re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", r.stdout))


def instructions(exe):
    got = []
    for _ in range(9):
        r = subprocess.run(
            ["perf", "stat", "-x,", "-e", "instructions:u", "--", exe, "-e", ""],
            capture_output=True, text=True,
            env={k: v for k, v in os.environ.items() if k not in ("LUA_PATH", "LUA_CPATH", "LUA_INIT")},
        )
        for line in r.stderr.splitlines():
            f = line.split(",")
            if len(f) > 2 and "instructions" in f[2] and f[0].strip().isdigit():
                got.append(int(f[0]))
    return int(statistics.median(got)) if len(got) >= 5 else None


def main():
    lust, luajit, nlibs = sys.argv[1], sys.argv[2], int(sys.argv[3])
    ok = True

    a, b = needed(lust), needed(luajit)
    if a is not None and b is not None:
        if a != b:
            print(f"FAIL lust links other shared libraries than luajit:\n  lust:   {a}\n  luajit: {b}")
            ok = False
        else:
            print(f"ok   same shared libraries as luajit: {a}")
    else:
        print("skip readelf not available")

    os.sched_setaffinity(0, {0})  # steady counts
    try:
        lu, lj = instructions(lust), instructions(luajit)
    except FileNotFoundError:
        lu = lj = None
    if lu is None or lj is None:
        print("skip perf not usable, instruction budget not checked")
    else:
        budget = BASE_BUDGET + PER_LIB_BUDGET * nlibs
        delta = lu - lj
        print(f"{'ok  ' if delta <= budget else 'FAIL'} startup overhead {delta:+,} instructions (budget {budget:,})")
        ok = ok and delta <= budget

    if not ok:
        sys.exit(1)
    if a is None and lu is None:
        sys.exit(SKIP)


if __name__ == "__main__":
    main()
