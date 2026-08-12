# Tests

Standalone unit and deterministic contract tests for Genesis Plus GX modules
that can be exercised without distributing a ROM.

## audio_probe

`test_audio_probe.c` unit-tests `core/debug/audio_probe.c` (the sound-trigger
exposure surface — see [`docs/audio_probe.md`](../docs/audio_probe.md)).

It builds the module against `tests/stub/shared.h`, a minimal stand-in that
declares only the few emulator globals `audio_probe_get_context()` references,
so **no full emulator build is needed**.

```sh
cd tests
make check
```

Expected unit output includes:

```
audio_probe unit tests: 64 passed, 0 failed
audio_probe concurrency tests: 10 passed, 0 failed; events=3000000
sprite capture unit tests: 13 passed, 0 failed
```

`make check` exits non-zero if any check fails (CI-friendly).

`test_sprite_capture.c` separately validates issue #10's generation-tagged
hash: exact duplicate removal, complete identity fields, deterministic order,
mid-frame rewrites of one SAT slot, 10-bit interlace Y coordinates, overflow
and frame isolation. Production builds compile out its metrics counters.

## AYTHER ABI v1

`test_ayther_api.c` freezes the public fixed-width layouts and constants from
`core/ayther/ayther_api.h`. CI additionally compiles
`ci/verify_ayther_api.c`, loads the produced DLL/so, negotiates v1 and validates
capabilities, all region sizes, generation handling, controlled writes and the
complete legacy adapter. Without `--require`, that verifier also exercises the
safe stock/pre-ABI path: a missing symbol is reported as zero capabilities
instead of being called. CI proves that branch with `ci/mock_stock_core.c`.

The command also runs `ayther/audio_probe_trace.c`. That harness emits a fixed
sequence of FM, PSG, DAC, frame and state events, serializes every logical field
in an explicit little-endian format, and compares the resulting FNV-1a digest
against `ayther/golden/audio_probe_trace.json`. Struct padding and host byte
order are never hashed.

On a mismatch, the actual trace summary remains in
`tests/artifacts/audio_probe_trace.actual.json` so CI can publish it as a
diagnostic artifact.

## Full-core deterministic replay

`ayther/full_core_replay.c` loads the real libretro DLL/so and runs a generated
64 KiB Mega Drive ROM. The generator emits only repository-owned bytes and
exercises CRAM raster writes, per-line plane scroll, a 24-sprite pressure band
with a scanline-time SAT attribute rewrite, and recurring PSG/YM2612 traffic.
The frontend supplies a fixed input/config
stream, captures an initial savestate, and executes one reference plus two
independent restore/replay passes.

Every frame hashes visible RGB565 pixels, stereo samples, zero-initialized
serialized state, canonical audio telemetry, input and configuration. It also
compares ABI v1 recomposition with the emitted frame and records fallback
reasons, parsed sprites and audio writes. Run it after building a probe-enabled
core:

```sh
make -f Makefile.libretro platform=unix SOUND_PROBE=1 -j2
make -C tests check-full-core CORE=../genesis_plus_gx_libretro.so
```

The x64 golden summary is `ayther/golden/full_core_replay-x64.json`. Video,
audio, serialized state, telemetry, input, configuration and replay hashes are
byte-identical on Linux x64 and Windows x64 MSVCRT. The actual summary,
per-frame JSONL trace, first-frame savestate diagnostic and p50/p95/p99 frame
benchmark remain in `tests/artifacts/`; CI uploads them even when the golden or
replay differs.

The same harness begins with every subscription idle, verifies that sprite,
raster and audio capture remain empty, activates the supported mask at the next
frame boundary, then replays both modes from one checkpoint. Video, audio and
serialized state must stay byte-identical. `--compare-profiles` alternates an
extensions-off and a standard-idle binary for seven rounds and fails if their
paired median overhead reaches 1%; see
[`docs/ayther_subscriptions.md`](../docs/ayther_subscriptions.md).

## Raster fallback guard

`test_raster_guard.c` validates issue #5's stable reason bits and centralized
classification rules: effective-change gating, active-display gating, aligned
hscroll detection, reason accumulation, DMA origin and legacy boolean
compatibility. The production 68K, Z80 and DMA paths all feed these same rules.

### What is covered

| Area | Checks |
|------|--------|
| Lifecycle | core reset preserves already-published SPSC events |
| Raw events | source/type/reg/data/schema/timestamp round-trip |
| Timeline | frame markers advance `t_frame` and accumulate `t_global` |
| Voice decode | TL/AR/DR/SR/RR/MUL/DT, algorithm, feedback, pan, fms, block_fnum from the register shadow |
| Fingerprint | `voice_hash` / `timbre_hash` are **channel-independent** (same voice on ch0 vs ch3) |
| Notes | NOTE_ON on key-on, NOTE_OFF on key-off |
| DAC | start/stop on channel 6 |
| PSG | raw write source/data/time |
| Gain | atomic set/get, negative clamps to 0, out-of-range ignored |
| Grouping | only NOTE_ON/DAC_START anchor a fixed first-anchor window; RAW stays at group 0 |
| Ring buffer | acquire/release publication, wrap-around, exact overflow, capacity/pending/high-water stats |
| Concurrency | three million events across one producer/consumer plus callback reconfiguration |
| Callback | coherent callback/user pair, explicit active-mode statistic |
| Context | reflects ROM checksum / region / clock / cycles-per-frame |
| Determinism | canonical event trace matches the versioned golden digest |
| Raster guard | REG/VRAM/CRAM/VSRAM/HSCROLL/DMA/mode reason classification |

### Layout

```
tests/
├── Makefile            # portable `make check` (Linux and Windows)
├── README.md
├── ayther/
│   ├── audio_probe_trace.c
│   ├── generated_rom.c/.h
│   ├── full_core_replay.c
│   ├── raster_rom_probe.c
│   └── golden/{audio_probe_trace,full_core_replay-x64}.json
├── ci/verify_ayther_api.c # dynamic ABI/symbol verifier
├── stub/
│   └── shared.h        # minimal emulator stand-in for isolated builds
├── test_audio_probe.c  # audio probe unit runner
├── test_audio_probe_concurrency.c # SPSC/callback stress runner
├── test_ayther_api.c    # public ABI layout/constant runner
├── test_sprite_capture.c # fixed-hash identity/overflow runner
└── test_raster_guard.c # raster fallback reason unit runner
```

Generated binaries and mismatch reports live under `.build/` and `artifacts/`;
both directories are ignored by Git.
