# AYTHER performance baseline

Issue #8 has two reproducible performance signals. `bench_audio_probe.c`
measures three transport operations in nanoseconds per event:

- a volatile control loop;
- synchronous callback delivery;
- ring emission followed by polling.

Run it with:

```sh
make -C bench run
```

The JSON result is written to `bench/artifacts/audio_probe_benchmark.json`.

## Sprite capture deduplication

`bench_sprite_capture.c` protects issue #10's observed VDP hot path. It feeds
80 stable sprite identities through 64 scanlines and compares the previous
linear scan against the production fixed hash/generation implementation:

- 101 measured samples after 11 warmups;
- nanoseconds per record at p50/p95/p99;
- exact legacy comparisons versus hash probes and maximum probe depth;
- deterministic failure if the hash path is not faster or performs more work.

The result is written to `bench/artifacts/sprite_capture_benchmark.json` by the
same `make -C bench run` command. The Windows x64 development capture reduced
207,280 comparisons to 6,144 probes (97.0%) and p50 from 17.56 ns to 6.95 ns
per record (60.4%). Hosted-runner timing remains diagnostic; the structural
comparison/probe reduction is deterministic.

The full-core replay benchmark is produced by the deterministic fixture:

```sh
make -f Makefile.libretro platform=unix SOUND_PROBE=1 -j2
make -C tests check-full-core CORE=../genesis_plus_gx_libretro.so
```

`tests/artifacts/full_core_benchmark.json` contains frame CPU p50/p95/p99,
core binary size, serialized-state and working-buffer sizes, events/frame,
fallback/false-clean counts, replay divergences, sprite pressure and audio
activity. The workload is a generated, redistributable ROM; no commercial ROM
or external fixture is required.

## Statistical method

Each mode performs 11 unrecorded warmups and then 101 samples of 4096 events.
The report includes minimum, p50, p95, p99 and maximum using nearest-rank
samples after sorting. CI records and publishes the result but does not reject a
change on absolute hosted-runner timing: cross-machine absolute thresholds are
too noisy to be meaningful.

The versioned Windows snapshots in `baselines/` are informational anchors tied
to their recorded target and compiler. Hosted-runner results are artifacts,
not pass/fail gates. Once a stable runner has at least five captures, an
interleaved before/after comparison may warn at a p95 regression above 5% and
fail above 10%; the median of the captures is compared and all correctness
hashes must already match. A single sample, cross-machine result or p99 spike
must never fail CI.

## Extensions-off versus compiled-idle

Issue #9 adds a paired gate for the hot-path cost of merely compiling AYTHER.
Build one core with `AYTHER_EXTENSIONS=0 SOUND_PROBE=0` and another with
`AYTHER_EXTENSIONS=1 AYTHER_LEGACY_PROFILE=0`; then run:

```sh
make -C tests check-profile-comparison \
  PROFILE_OFF_CORE=/path/to/extensions-off-core \
  PROFILE_IDLE_CORE=/path/to/compiled-idle-core
```

The generated-ROM harness alternates binary order across seven rounds, compares
video/audio correctness, reports p50/p95 and binary size, and calculates the
median of paired overhead samples. Unlike cross-machine absolute timing, this
same-process relative signal is enforced: compiled-idle overhead must remain
strictly below 1%. Active telemetry timing remains diagnostic.
