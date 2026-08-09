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

Expected output:

```
audio_probe unit tests: 49 passed, 0 failed
```

`make check` exits non-zero if any check fails (CI-friendly).

The command also runs `ayther/audio_probe_trace.c`. That harness emits a fixed
sequence of FM, PSG, DAC, frame and state events, serializes every logical field
in an explicit little-endian format, and compares the resulting FNV-1a digest
against `ayther/golden/audio_probe_trace.json`. Struct padding and host byte
order are never hashed.

On a mismatch, the actual trace summary remains in
`tests/artifacts/audio_probe_trace.actual.json` so CI can publish it as a
diagnostic artifact.

### What is covered

| Area | Checks |
|------|--------|
| Lifecycle | reset clears the buffer |
| Raw events | source/type/reg/data/schema/timestamp round-trip |
| Timeline | frame markers advance `t_frame` and accumulate `t_global` |
| Voice decode | TL/AR/DR/SR/RR/MUL/DT, algorithm, feedback, pan, fms, block_fnum from the register shadow |
| Fingerprint | `voice_hash` / `timbre_hash` are **channel-independent** (same voice on ch0 vs ch3) |
| Notes | NOTE_ON on key-on, NOTE_OFF on key-off |
| DAC | start/stop on channel 6 |
| PSG | raw write source/data/time |
| Gain | set/get, negative clamps to 0, out-of-range ignored |
| Grouping | coincidence window groups near events, splits far ones |
| Ring buffer | overflow drops cleanly, keeps capacity-1 |
| Callback | bypasses the ring buffer |
| Context | reflects ROM checksum / region / clock / cycles-per-frame |
| Determinism | canonical event trace matches the versioned golden digest |

### Layout

```
tests/
├── Makefile            # portable `make check` (Linux and Windows)
├── README.md
├── ayther/
│   ├── audio_probe_trace.c
│   └── golden/audio_probe_trace.json
├── stub/
│   └── shared.h        # minimal emulator stand-in for isolated builds
└── test_audio_probe.c  # the test runner (self-contained assert framework)
```

Generated binaries and mismatch reports live under `.build/` and `artifacts/`;
both directories are ignored by Git.
