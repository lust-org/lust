#!/usr/bin/env python3
"""Compare library flavours (Lua C API vs static FFI bindings) on startup
cost and steady-state speed, pinned to one CPU.

By default it compares lust.xxh (C API) with lust.xxhffi (FFI), each both as
a shared module loaded by a plain luajit and as part of the static `lust`:

  luajit + .so    (build/vendor/luajit, the same VM build `lust` uses, so
                   the only difference is how the library gets in)
  lust            (library linked in, registered in package.preload)

Startup: `<interp> -e 'require "lust.<lib>"'` is spawned many times,
interleaved across all variants (so drift hits all of them alike), wall
time measured around posix_spawn+wait. The cost of the require is the
difference to the same interpreter running an empty chunk. Where `perf`
works, user space instructions are reported too: they are free of timing
noise. Startup runs on --cpu only.

Steady state: tools/bench/throughput.lua, several processes per variant.
A round is one process of every variant (shuffled) on one cpu. Rounds
run in parallel, each on a physical core of its own of the same type as
--cpu (P/E cores on hybrid CPUs; never two SMT siblings, they share the
core): the cases are CPU bound and fit in L2, so separate cores barely
influence each other, and as every variant gets one process per core,
cores of different speed (favoured cores turbo higher) hit all of them
alike. `--jobs 1` runs everything on --cpu alone.

Run through `./dev.py bench` (it builds first) or directly after a build.
"""

import argparse
import os
import queue
import random
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HERE = Path(__file__).resolve().parent


def pin(cpu):
    """Pin the calling thread (Linux affinity is per thread); every child
    it spawns inherits the affinity."""
    os.sched_setaffinity(0, {cpu})


def parse_cpus(text):
    """A sysfs cpu list ("0-3,8") as a set."""
    cpus = set()
    for part in text.strip().split(","):
        if part:
            lo, _, hi = part.partition("-")
            cpus.update(range(int(lo), int(hi or lo) + 1))
    return cpus


def cores_like(cpu):
    """`cpu` first, then one cpu of every other physical core of the same
    type (on hybrid CPUs) that this process may run on."""
    allowed = os.sched_getaffinity(0)
    kind = allowed
    # Hybrid CPUs have a PMU per core type listing its cpus (cpu_core,
    # cpu_atom); others have none.
    for f in Path("/sys/devices").glob("cpu_*/cpus"):
        cpus = parse_cpus(f.read_text())
        if cpu in cpus:
            kind = cpus & allowed
    chosen, taken = [], set()
    for c in [cpu, *sorted(kind - {cpu})]:
        sib = Path(f"/sys/devices/system/cpu/cpu{c}/topology/thread_siblings_list")
        core = parse_cpus(sib.read_text()) if sib.exists() else {c}
        if not core & taken:  # no SMT sibling of a chosen cpu
            chosen.append(c)
            taken |= core
    return chosen


def warn_unstable(cpu):
    gov = Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor")
    if gov.exists() and gov.read_text().strip() != "performance":
        print(
            f"warning: cpu{cpu} governor is '{gov.read_text().strip()}', "
            "timings drift with frequency scaling. For stable numbers:\n"
            "  sudo cpupower frequency-set -g performance   "
            "(and optionally disable turbo/boost)\n",
            file=sys.stderr,
        )


class Variant:
    def __init__(self, interp_name, interp, lib, cpath):
        self.interp_name, self.interp, self.lib = interp_name, interp, lib
        self.env = dict(os.environ, LUA_CPATH=cpath)
        # Don't let a developer's LUA_PATH leak into the measurement.
        self.env.pop("LUA_PATH", None)
        self.env.pop("LUA_INIT", None)

    @property
    def label(self):
        return f"{self.interp_name}:{self.lib or '(bare)'}"

    def startup_argv(self):
        chunk = f'require "lust.{self.lib}"' if self.lib else ""
        return [str(self.interp), "-e", chunk]


def spawn_ns(argv, env):
    """Wall time of one complete run of argv."""
    null = os.devnull
    actions = [
        (os.POSIX_SPAWN_OPEN, 0, null, os.O_RDONLY, 0),
        (os.POSIX_SPAWN_OPEN, 1, null, os.O_WRONLY, 0),
        (os.POSIX_SPAWN_OPEN, 2, null, os.O_WRONLY, 0),
    ]
    t0 = time.perf_counter_ns()
    pid = os.posix_spawn(argv[0], argv, env, file_actions=actions)
    _, status = os.waitpid(pid, 0)
    dt = time.perf_counter_ns() - t0
    if status != 0:
        sys.exit(f"command failed ({status}): {' '.join(argv)}")
    return dt


def perf_instructions(argv, env):
    """User space instructions of one run, or None if perf is unusable."""
    r = subprocess.run(
        ["perf", "stat", "-x,", "-e", "instructions:u", "--", *argv],
        env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True,
    )
    for line in r.stderr.splitlines():
        f = line.split(",")
        if len(f) > 2 and "instructions" in f[2] and f[0].strip().isdigit():
            return int(f[0])
    return None


def startup(variants, runs, perf_runs):
    for v in variants:  # warm the page cache / dynamic loader
        for _ in range(20):
            spawn_ns(v.startup_argv(), v.env)

    times = {v.label: [] for v in variants}
    for _ in range(runs):
        order = variants[:]
        random.shuffle(order)  # interleaved, in changing order
        for v in order:
            times[v.label].append(spawn_ns(v.startup_argv(), v.env))

    insns = {}
    if perf_runs:
        for v in variants:
            got = [perf_instructions(v.startup_argv(), v.env) for _ in range(perf_runs)]
            got = [g for g in got if g]
            insns[v.label] = int(statistics.median(got)) if got else None

    def med(v):
        return statistics.median(times[v.label]) / 1e3  # us

    print(f"== startup, {runs} runs each (microseconds / user instructions)")
    print(f"{'variant':<24}{'median':>10}{'min':>10}{'+require':>10}{'instr':>14}{'+require':>12}")
    bare = {v.interp_name: v for v in variants if v.lib is None}
    for v in variants:
        b = bare[v.interp_name]
        d_t = "" if v is b else f"{med(v) - med(b):+.0f}"
        i, bi = insns.get(v.label), insns.get(b.label)
        d_i = "" if v is b or not (i and bi) else f"{i - bi:+,}"
        print(
            f"{v.label:<24}{med(v):>10.0f}{min(times[v.label]) / 1e3:>10.0f}{d_t:>10}"
            f"{(f'{i:,}' if i else '-'):>14}{d_i:>12}"
        )
    print()


def throughput(variants, procs, rounds, cpus):
    free = queue.SimpleQueue()  # cpus not running a round right now
    for cpu in cpus:
        free.put(cpu)

    def one_round():
        """One process of every variant, in random order, on a free cpu:
        [(variant, output)]."""
        cpu = free.get()
        try:
            pin(cpu)
            order = variants[:]
            random.shuffle(order)
            outs = []
            for v in order:
                out = subprocess.run(
                    [str(v.interp), str(HERE / "throughput.lua"), v.lib, str(rounds)],
                    env=v.env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    text=True, check=True,
                ).stdout
                outs.append((v, out, cpu))
            return outs
        finally:
            free.put(cpu)

    with ThreadPoolExecutor(max_workers=len(cpus)) as pool:
        done = [f.result() for f in [pool.submit(one_round) for _ in range(procs)]]

    # case -> variant label -> list of per-process medians (ns/call)
    res = {}
    cases = []
    for v, out, cpu in (r for outs in done for r in outs):
        for line in out.splitlines():
            f = line.split("\t")
            if f[0] == "# cpus":
                if f[1] != str(cpu):
                    sys.exit(f"{v.label} ran on cpus {f[1]}, not pinned to {cpu}")
                continue
            if f[0] not in cases:
                cases.append(f[0])
            res.setdefault(f[0], {}).setdefault(v.label, []).append(float(f[2]))

    labels = [v.label for v in variants]
    used = sorted({cpu for outs in done for _, _, cpu in outs})
    print(f"== steady state, ns per call (median of {procs} processes x {rounds} rounds,"
          f" on cpus {','.join(map(str, used))})")
    print(f"{'case':<12}" + "".join(f"{l:>20}" for l in labels))
    for c in cases:
        print(f"{c:<12}" + "".join(f"{statistics.median(res[c][l]):>20.1f}" for l in labels))
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-B", "--builddir", default="build", type=Path)
    ap.add_argument("--libs", default="xxh,xxhffi", help="libraries to compare (default: %(default)s)")
    ap.add_argument("--cpu", type=int, default=0, help="CPU to pin to (default: %(default)s)")
    ap.add_argument("--jobs", type=int,
                    help="physical cores (like --cpu) for steady state rounds in parallel (default: all)")
    ap.add_argument("--only", choices=["startup", "throughput"], help="run just one of the two")
    ap.add_argument("--luajit", type=Path, help="interpreter for the .so modules (default: the vendored build)")
    ap.add_argument("--startup-runs", type=int, default=300)
    ap.add_argument("--perf-runs", type=int, default=10, help="runs for the instruction counts, 0 disables")
    ap.add_argument("--procs", type=int,
                    help="processes per variant for steady state (default: one per core, at least 5)")
    ap.add_argument("--rounds", type=int, default=15, help="timed rounds per case and process")
    args = ap.parse_args()

    bd = (ROOT / args.builddir).resolve()
    luajit = args.luajit or bd / "vendor" / "luajit"
    lust = bd / "src" / "lust"
    for p in (luajit, lust):
        if not p.exists():
            sys.exit(f"{p} not found, build first (./dev.py build)")

    # Before pinning: the cores this process may use at all.
    cpus = cores_like(args.cpu)[: args.jobs]
    procs = args.procs or max(5, len(cpus))
    pin(args.cpu)
    warn_unstable(args.cpu)
    print(f"pinned to cpu {args.cpu}; shared: {luajit}; static: {lust}\n")

    libs = args.libs.split(",")
    shared_cpath = str(bd / "libs" / "?.so")
    nowhere = "/nonexistent/?.so"  # force the preloaded copies in lust
    variants = []
    for name, interp, cpath in (("luajit", luajit, shared_cpath), ("lust", lust, nowhere)):
        for lib in [None, *libs]:
            variants.append(Variant(name, interp, lib, cpath))

    if args.only != "throughput":
        startup(variants, args.startup_runs, args.perf_runs)
    if args.only != "startup":
        throughput([v for v in variants if v.lib], procs, args.rounds, cpus)


if __name__ == "__main__":
    main()
