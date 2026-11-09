# Benchmark regressions

Every library has benchmarks in `libs/<name>/bench/*.lua` (`_*.lua` are
helpers): a fixed amount of work each (a few ms), asserting its result.
`./dev.py test` runs each one in both flavours (vendored luajit + `.so`,
and `lust`) and fails if its user space instructions (`perf stat -e
instructions:u`, median of 3) differ more than 3% from
`libs/<name>/bench/baseline.txt`. Instructions are free of timing noise
(repeated runs differ well below 1%, whatever the CPU or its core type),
so this works on a loaded machine, and all runs go in parallel, each
process pinned to a CPU of its own. Without a usable `perf` only the
results are checked; non release or sanitizer builds don't compare.

The counts depend on the toolchain and system libraries. After an
intended change (faster counts too, so the baseline stays tight), for a
new library, or on another system, record them anew and commit the diff
(counts within 1% of the recorded ones are kept, so noise doesn't churn
the baselines):

```sh
./dev.py test --suite bench --test-args=--update
```
