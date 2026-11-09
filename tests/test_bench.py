#!/usr/bin/env python3
"""Benchmark regression test of one library.

usage: test_bench.py [--update] [--skip REASON] <bench dir>
                     --flavour <name> <interpreter> <LUA_CPATH> ...

Every libs/<name>/bench/*.lua (files starting with `_` are helpers, not
benchmarks) is a fixed amount of work that asserts its own result. Each
one is run with every flavour (`lust`, `luajit` + .so), and its user
space instructions (`perf stat -e instructions:u`, median of RUNS) must
stay within TOLERANCE of the count recorded in <bench dir>/baseline.txt.
Instructions don't suffer from timing noise (frequency scaling, other
load, sharing a cpu, even the core type of hybrid CPUs): repeated runs
differ by well under 1%, so a real slowdown (or speedup, which should be
recorded) of a few percent stands out. That also lets all runs go in
parallel, each process pinned to one cpu of its own.

The counts depend on the toolchain and the system libraries (e.g. libz
for lust.crc), so after an intended change, or on another system, record
them anew (only counts that are new or moved by more than NOISE are
rewritten, so this doesn't churn the other baselines):

  ./dev.py test --suite bench --test-args=--update

Without a usable `perf` the benchmarks still run (they check their
results), but the counts are not compared and the test is skipped.
"""

import argparse
import os
import queue
import statistics
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SKIP = 77  # meson: skipped
RUNS = 3
TOLERANCE = 0.03
# --update keeps recorded counts that are within this of the measured one:
# re-recording noise would only churn the baselines.
NOISE = 0.01
BASELINE = "baseline.txt"
HEADER = """\
# User space instructions per run of each benchmark, checked by
# tests/test_bench.py (see there). Regenerate after an intended change:
#   ./dev.py test --suite bench --test-args=--update
# <flavour> <benchmark> <instructions>
"""


class Failed(Exception):
    pass


def read_baseline(path):
    """{(flavour, benchmark): instructions}"""
    counts = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                flavour, bench, n = line.split()
                counts[flavour, bench] = int(n)
    return counts


def write_baseline(path, counts):
    lines = [f"{f} {b} {n}\n" for (f, b), n in sorted(counts.items())]
    path.write_text(HEADER + "".join(lines))


def run(argv, env):
    """Run argv; raise Failed (with its output) unless it succeeds."""
    r = subprocess.run(argv, env=env, capture_output=True, text=True)
    if r.returncode:
        raise Failed(f"exit status {r.returncode}\n{r.stdout}{r.stderr}".rstrip())


def instructions(argv, env):
    """User space instructions of one run of argv, None if perf can't
    count them. On hybrid CPUs perf prints a line per core type, the one
    not used being "<not counted>": the numeric ones are summed (a
    process moving between core types would be split over both)."""
    with tempfile.NamedTemporaryFile("r") as out:
        try:
            run(["perf", "stat", "-x,", "-e", "instructions:u", "-o", out.name, "--", *argv], env)
        except FileNotFoundError:
            return None
        counts = [int(f[0]) for f in (line.split(",") for line in out.read().splitlines())
                  if len(f) > 2 and "instructions" in f[2] and f[0].strip().isdigit()]
    return sum(counts) if counts else None


def pinned_pool():
    """Thread pool with one worker per usable cpu, each pinned to its own
    cpu. Linux affinity is per thread and a child inherits the one of the
    thread spawning it, so every process a worker runs stays on its cpu
    (os.sched_setaffinity(0) sets the calling thread's)."""
    cpus = queue.SimpleQueue()
    usable = sorted(os.sched_getaffinity(0))
    for cpu in usable:
        cpus.put(cpu)

    def pin():
        cpu = cpus.get()
        try:
            os.sched_setaffinity(0, {cpu})
        except OSError as e:
            print(f"note: cannot pin to cpu {cpu} ({e})")

    return ThreadPoolExecutor(max_workers=len(usable), initializer=pin)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bench_dir", type=Path)
    ap.add_argument("--flavour", nargs=3, action="append", required=True,
                    metavar=("NAME", "INTERPRETER", "LUA_CPATH"))
    ap.add_argument("--update", action="store_true", help=f"record the counts in <bench dir>/{BASELINE}")
    ap.add_argument("--skip", metavar="REASON", help="only check that the benchmarks succeed")
    args = ap.parse_args()

    benches = sorted(p for p in args.bench_dir.glob("*.lua") if not p.name.startswith("_"))
    if not benches:
        sys.exit(f"no benchmarks in {args.bench_dir}")

    # A developer's LUA_PATH/LUA_INIT must not leak into the measurement.
    base_env = {k: v for k, v in os.environ.items() if k not in ("LUA_PATH", "LUA_INIT")}

    baseline_path = args.bench_dir / BASELINE
    baseline = read_baseline(baseline_path)
    measured = {}
    ok = True
    # No perf (or it can't count): the benchmarks still run once each, to
    # make sure they work.
    perf_ok = args.skip is None and instructions(["true"], base_env) is not None

    # Start every run at once (they don't influence each other's counts),
    # then go through the results in order.
    jobs = []
    with pinned_pool() as pool:
        for name, interp, cpath in args.flavour:
            env = dict(base_env, LUA_CPATH=cpath)
            for bench in benches:
                argv = [interp, str(bench)]
                runs = [pool.submit(instructions, argv, env) for _ in range(RUNS)] if perf_ok \
                    else [pool.submit(run, argv, env)]
                jobs.append((name, bench, runs))

    for name, bench, runs in jobs:
        label = f"{name:<7} {bench.name:<16}"
        try:
            counts = [r.result() for r in runs]
        except Failed as e:
            print(f"FAIL {label} {e}")
            ok = False
            continue
        if not perf_ok:
            print(f"ok   {label} (not measured)")
            continue
        if None in counts:
            # perf counted `true`, but not this: don't pass silently.
            print(f"FAIL {label} perf counted no instructions")
            ok = False
            continue

        got = int(statistics.median(counts))
        want = baseline.get((name, bench.name))
        if want is None:
            ok = ok and args.update
            measured[name, bench.name] = got
            print(f"{'new ' if args.update else 'FAIL'} {label} {got:>14,}"
                  + ("" if args.update else "  no baseline, record it with --update"))
            continue
        delta = got / want - 1
        if args.update:
            moved = abs(delta) > NOISE
            measured[name, bench.name] = got if moved else want
            print(f"{'new ' if moved else 'keep'} {label} {got:>14,}  baseline {want:>14,}  {delta:+.2%}")
            continue
        good = abs(delta) <= TOLERANCE
        ok = ok and good
        print(f"{'ok  ' if good else 'FAIL'} {label} {got:>14,}  baseline {want:>14,}  {delta:+.2%}"
              + ("" if good else f" (allowed: ±{TOLERANCE:.0%}; if intended, record it with --update)"))

    if args.update and perf_ok and ok:
        # Keep the counts of flavours not measured now (e.g. built without
        # `lust`), drop those of removed benchmarks.
        names = {f[0] for f in args.flavour}
        kept = {k: v for k, v in baseline.items() if k[0] not in names}
        write_baseline(baseline_path, kept | measured)
        print(f"wrote {baseline_path}")

    if not ok:
        sys.exit(1)
    if args.update and not perf_ok:
        sys.exit(f"nothing recorded: {args.skip or 'perf not usable'}")
    if not perf_ok:
        print(f"skip {args.skip or 'perf not usable'}: instructions not checked")
        sys.exit(SKIP)


if __name__ == "__main__":
    main()
