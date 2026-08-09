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

## Next fixture families

The full core replay harness remains part of issue #8. Its fixtures will add a
generated/open ROM payload, initial savestate, input/config stream, and hashes
for video, audio and serialized state. Raster, sprite pressure, plane scroll
and repeated save/load scenarios cannot be represented honestly by this
isolated module test and are therefore not marked as covered yet.
