# AYTHER performance baseline

`bench_audio_probe.c` establishes the first reproducible performance signal for
issue #8. It measures three operations in nanoseconds per event:

- a volatile control loop;
- synchronous callback delivery;
- ring emission followed by polling.

Run it with:

```sh
make -C bench run
```

The JSON result is written to `bench/artifacts/audio_probe_benchmark.json`.

## Statistical method

Each mode performs 11 unrecorded warmups and then 101 samples of 4096 events.
The report includes minimum, p50, p95, p99 and maximum using nearest-rank
samples after sorting. CI records and publishes the result but does not reject a
change on absolute hosted-runner timing: cross-machine absolute thresholds are
too noisy to be meaningful.

The versioned Windows snapshot in `baselines/windows-x64-msvcrt.json` is an
informational anchor tied to its recorded compiler and host. A blocking
regression threshold will only be introduced after at least five captures on a
stable runner; comparisons must use the same target/toolchain and will gate on
p95 medians from interleaved before/after runs.

This is a transport microbenchmark, not yet the end-to-end frame benchmark.
The full replay harness in issue #8 must add frame CPU p50/p95/p99, AYTHER
idle/active deltas, memory, binary size, fallbacks and false-clean metrics.
