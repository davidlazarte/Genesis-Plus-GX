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

Run it through the top-level test target:

```sh
make -C tests check-determinism
```

If the contract changes intentionally, inspect
`tests/artifacts/audio_probe_trace.actual.json`, explain the schema or semantic
change in the pull request, and only then update the golden file. Never update a
golden merely to make CI green.

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

## Next fixture families

The full deterministic replay harness remains part of issue #8. Its fixtures will add a
generated/open ROM payload, initial savestate, input/config stream, and hashes
for video, audio and serialized state. Raster, sprite pressure, plane scroll
and repeated save/load scenarios cannot be represented honestly by this
isolated module test and are therefore not marked as covered yet.
