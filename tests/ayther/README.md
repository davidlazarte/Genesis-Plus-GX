# AYTHER deterministic fixtures

This directory is the ROM-free starting point for issue #8's deterministic
harness. The current fixture protects the public behavior of `audio_probe`:

- fixed FM voice programming and key on/off;
- PSG and DAC events;
- frame boundaries and a state-load signal;
- canonical serialization of every logical event and voice field;
- one versioned event count and digest in `golden/`.

The hash is intentionally independent of C struct layout, padding, pointer size
and host byte order. Integers are serialized least-significant byte first and
the fields are hashed in the order documented by the harness source.

Issue #7 intentionally changed the v1 digest to `212433c5d4128d7f`: semantic
anchors retain non-zero coincidence groups while RAW_WRITE, FRAME and release
events now serialize `group=0`. The event count remains 37.

Run it through the top-level test target:

```sh
make -C tests check-determinism
```

If the contract changes intentionally, inspect
`tests/artifacts/audio_probe_trace.actual.json`, explain the schema or semantic
change in the pull request, and only then update the golden file. Never update a
golden merely to make CI green.

The concurrent transport fixture is separate from the deterministic trace:
`test_audio_probe_concurrency.c` forces a 64-slot ring through wrap-around and
overflow while transferring three million events. CI runs it on Windows and
Linux and repeats it under ThreadSanitizer.

## Local ROM raster validation

`raster_rom_probe.c` is an end-to-end safety probe for issue #5. It loads a
test core dynamically, captures the RGB565 frame emitted by libretro and
compares it with ABI v1 `recompose_frame` from the same final VDP state. A
different frame is safe only when private memory id `0x10E` exposes a non-zero
fallback reason.

The core used for this test must export the single discovery symbol
`ayther_get_interface`. The probe also checks `frame_generation`, snapshot
fallback reasons and capture bounds on every frame. Build and run it without
copying ROMs into the repository:

```sh
make -C tests raster-rom-probe
tests/.build/raster_rom_probe \
  --frames 600 --output tests/artifacts/raster-roms.jsonl \
  ./genesis_plus_gx_libretro.dll /path/to/roms/*.md
```

The JSON-lines report contains ROM filenames, sizes and aggregate frame
statistics, never ROM contents. The process exits with status 2 when it finds a
recomposition mismatch with mask zero or an unavailable recomposition without
`UNSUPPORTED_MODE`.

The first recorded local-corpus execution is summarized in
`docs/validation/raster-roms-2026-08-09.md` (14 ROMs, 25,200 frames). The ROMs
and generated JSON/images remain outside version control.

## Full-core generated fixture

`generated_rom.c` emits a 64 KiB redistributable Mega Drive image directly in
big-endian 68000 form. It covers:

- CRAM changes from horizontal interrupts (raster fallback);
- different per-line Plane A/B scroll values;
- 24 linked sprites on one visible band (hardware-limit pressure);
- repeated scanline-time attribute rewrites of one SAT slot;
- recurring PSG and YM2612 writes.

`full_core_replay.c` loads that image through the real libretro core, captures
an initial savestate and runs one reference plus two restore/replay passes over
the same 120-frame input/config stream. The versioned golden includes video,
audio, serialized-state, telemetry, input, configuration and aggregate replay
hashes. Process-local pointers in the Z80 and YM2612 contexts are canonicalized before
hashing. Even so, the video, audio, state and replay digests **differ between
Linux x64 (gcc/clang) and Windows x64 (llvm-mingw MSVCRT)**, so there is one
golden per platform; input, configuration and telemetry hashes do match. See
`tests/README.md` for the measurement and #38 for the diagnosis. (This file used
to claim the hashes were identical everywhere, which stopped being true when the
per-platform goldens were introduced.) The per-frame JSONL artifact additionally
records fallback reasons, recomposition differences, sprites, audio writes and
event counts.

```sh
make -f Makefile.libretro platform=unix SOUND_PROBE=1 -j2
make -C tests check-full-core CORE=../genesis_plus_gx_libretro.so
```

On the Windows x64 MSVCRT baseline the fixture produces 27 sprite identities
(20 slots plus seven observed rewrites), eight captured audio writes and nine
v1 events per frame, with
`false_clean_frames = 0` and no replay divergences.

For issue #9 the fixture also exercises the subscription lifecycle. It proves
zero capture while the standard profile is idle, next-frame activation, and
bit-identical video/audio/state when observation is disabled. A separate
seven-round, alternating profile mode compares an extensions-off DLL with a
compiled-idle DLL and enforces median overhead below 1%.
