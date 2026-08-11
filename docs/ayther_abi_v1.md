# AYTHER ABI v1

`core/ayther/ayther_api.h` is the public, versioned contract between this core
and AYTHER Engine. The only discovery symbol is:

```c
const ayther_interface_v1 *ayther_get_interface(uint32_t requested_version);
```

This interface replaces implicit agreement about private libretro memory IDs
and C structure layouts. The legacy IDs remain available during the v1
transition, but new integrations should not take mutable pointers from them.

## Safe discovery and version negotiation

1. Resolve `ayther_get_interface` dynamically.
2. If the symbol is absent, classify the library as stock/pre-ABI AYTHER and
   expose zero AYTHER capabilities. Do not call a null function pointer.
3. Call it with `AYTHER_ABI_VERSION_1_0` when v1 is required, or `0` to discover
   the latest interface supported by the loaded core.
4. A requested unsupported version returns `NULL`.
5. Validate `abi_version`, `struct_size`, the required capability bits and the
   relevant data sizes before using any field or function.

ABI versions use `0xMMMMmmmm` (`major`, `minor`). A future descriptor may append
fields; consumers must only read fields covered by `struct_size` and ignore
unknown capability bits.

## Descriptor and capabilities

The immutable descriptor is owned by the DLL/so and remains valid until the
library is unloaded. It reports:

- ABI version and descriptor size;
- build identifier and explicit string length;
- host endianness and pointer size;
- sizes of `ayther_region_info_v1`, `ayther_frame_snapshot_v1`,
  `ayther_sprite_v1`, `ayther_audio_write_v1`, `ayther_audio_event_v1` and
  `ayther_audio_transport_stats_v1`, plus the subscription state layout;
- additive capability bits;
- functions for region discovery, copy-based reads, controlled writes,
  snapshots, recomposition and the optional audio event transport.

The v1 layout is frozen at 10 bytes for `ayther_sprite_v1`, 8 bytes for
`ayther_audio_write_v1`, 40 bytes for `ayther_audio_voice_v1`, 88 bytes for
`ayther_audio_event_v1`, and 32 bytes for
`ayther_audio_transport_stats_v1` and `ayther_subscription_state_v1`.
Compile-time assertions verify the public layouts and their internal aliases.

## Endianness, ownership and lifetime

This is an in-process ABI, not a serialized wire format. Fixed-width scalar
fields use the host endianness reported by `host_endianness`. CRAM, VSRAM,
sprites, audio writes, counters and raster reasons are marked
`AYTHER_REGION_NATIVE_ENDIAN`; VRAM and VDP register regions retain the core's
raw byte representation. In particular, existing word-swapped VRAM/CRAM
semantics are unchanged.

`read_region` copies bytes into frontend-owned memory. It never exposes a new
mutable core pointer. `build_id` and the interface descriptor are core-owned and
valid until unload. The private libretro IDs still return direct core-owned
pointers for compatibility; those pointers bypass generation checks and
controlled validation, and are deprecated.

Region, control, snapshot and recomposition calls must be made on the emulation
thread at a frame boundary (for example from or after the video callback).
Calls made while the core is executing a frame return `AYTHER_STATUS_BUSY`.
The two `AYTHER_CAP_AUDIO_PROBE_V1` functions are the exception: one tool thread
may poll events and read transport statistics concurrently with the sole
emulator/audio producer.

## Runtime subscriptions

`AYTHER_CAP_SUBSCRIPTIONS_V1` separates compile-time availability from runtime
work. Standard builds begin with `active_mask = requested_mask = 0`; legacy
profile builds begin with every compiled subsystem active. A mask requested by
`set_subscriptions` between frames becomes active at the beginning of the next
`retro_run`. Unknown or unavailable bits are rejected rather than ignored.

Observed region reads and operations return
`AYTHER_STATUS_NOT_SUBSCRIBED` when their subsystem is compiled but idle.
`AYTHER_STATUS_UNSUPPORTED` means the relevant feature is not present in the
binary. Control state may be prepared while idle, but render/audio effects are
gated by `AYTHER_SUB_RENDER_CONTROLS`.

The masks, bit-to-subsystem mapping, build profiles and activation protocol are
specified in [`ayther_subscriptions.md`](ayther_subscriptions.md).

## Regions

`query_region` returns an explicit layout version, element size, capacity, byte
size, access flags and the transition-era legacy ID.

| v1 region | Layout | Capacity / bytes | Public access | Legacy ID |
|---|---:|---:|---|---:|
| `VRAM` | raw bytes | 65,536 / 65,536 | read | standard `0x003` |
| `CRAM` | `uint16_t` | 64 / 128 | read | `0x100` |
| `VDP_REGS` | `uint8_t` | 32 / 32 | read | `0x101` |
| `VSRAM` | `uint16_t` | 64 / 128 | read | `0x107` |
| `LAYER_MASK` | `uint8_t` | 1 / 1 | read/control | `0x102` |
| `SPRITE_SUPPRESS` | bitmap | 16 / 16 | read/control | `0x103` |
| `TILE_SUPPRESS` | bitmap | 512 / 512 | read/control | `0x104` |
| `PLANE_TILE_SUPPRESS` | bitmap | 3,072 / 3,072 | read/control | `0x105` |
| `PLANE_SUPPRESS_ACTIVE` | `uint8_t` | 1 / 1 | read (derived) | `0x106` |
| `LAYER_DIM` | `uint8_t` | 1 / 1 | read/control | `0x108` |
| `AUDIO_WRITES` | `ayther_audio_write_v1` | 8,192 / 65,536 | frame read | `0x109` |
| `AUDIO_WRITE_COUNT` | `uint32_t` | 1 / 4 | frame read | `0x10A` |
| `PARSED_SPRITES` | `ayther_sprite_v1` | 128 / 1,280 | frame read | `0x10B` |
| `PARSED_SPRITE_COUNT` | `uint8_t` | 1 / 1 | frame read | `0x10C` |
| `AUDIO_MUTE` | `uint16_t` | 1 / 2 | read/control | `0x10D` |
| `RASTER_FALLBACK_REASONS` | `uint32_t` | 1 / 4 | frame read | `0x10E` |

The public ABI treats emulated memory and frame counters as read-only. The
legacy pointers preserve their historical mutability for the transition.

## Consistent frame snapshots

The core owns per-frame reset of parsed-sprite/audio-write counters and their
overflow flags. A frontend no longer needs to clear `0x10A` or `0x10C` before
`retro_run`; doing so remains harmless for old integrations.

After a completed frame:

```c
ayther_frame_snapshot_v1 snap;
ayther_region_info_v1 sprites;

if (api->capture_snapshot(&snap, sizeof(snap)) == AYTHER_STATUS_OK &&
    api->query_region(AYTHER_REGION_PARSED_SPRITES,
                      &sprites, sizeof(sprites)) == AYTHER_STATUS_OK) {
  size_t bytes = snap.parsed_sprite_count * sprites.element_size;
  int32_t status = api->read_region(
      AYTHER_REGION_PARSED_SPRITES, 0, destination, (uint32_t)bytes,
      snap.snapshot_generation, NULL);
  if (status == AYTHER_STATUS_STALE_GENERATION) {
    /* State changed: capture a new snapshot and retry. */
  }
}
```

`frame_generation` advances after each `retro_run`. `snapshot_generation` also
invalidates reads after a state load/reset or a successful controlled write.
Snapshots include valid sprite/audio counts, raster fallback reasons and
explicit overflow bits. Neither generation is serialized into savestates.

## Thread-safe audio event transport

`AYTHER_CAP_AUDIO_PROBE_V1` is present only when the core was built with
`SOUND_PROBE=1`. The descriptor fields are still appended in feature-off builds
so their offsets remain stable, but calls return `AYTHER_STATUS_UNSUPPORTED`.

`poll_audio_events` drains a bounded acquire/release SPSC queue into
frontend-owned `ayther_audio_event_v1` records. Exactly one emulator/audio
producer and one tool consumer are supported. Release publication of `head`
occurs only after a complete event copy, so the consumer cannot observe a
partially written record. Core resets do not discard published records or
write the consumer-owned `tail`.

`get_audio_transport_stats` reports effective capacity, an approximate pending
count, high-water mark, a saturating dropped-event counter, and whether legacy
inline callback mode is active.

Only `NOTE_ON` and `DAC_START` are logical grouping anchors. Anchors inside a
fixed coincidence window measured from the first anchor share a non-zero group.
RAW_WRITE, FRAME, NOTE_OFF, DAC_STOP and synchronization events use `group=0`
and never slide or extend that window.

## Controlled writes

`write_control` accepts only regions with
`AYTHER_REGION_ACCESS_CONTROL_WRITE`, validates bounds and the expected
snapshot generation, and advances the generation on success.

- layer mask: only A/B/Window/Sprite bits (`0x0F`);
- layer dim: boolean `0` or `1`;
- audio mute: only FM/PSG channel bits `0x03FF`;
- suppression bitmaps: bounded full or partial writes;
- plane suppression activity: derived automatically from the plane bitmap.

Writes to CRAM, VSRAM, VDP registers, VRAM, capture arrays, counters and raster
reasons return `AYTHER_STATUS_READ_ONLY`.

## Export and verification

Linux's version script exports `ayther_get_interface` explicitly. Windows uses
an explicit DLL export annotation, preserving the undecorated x64 name. The
recomposition implementation is reachable only through the negotiated function
pointer, so there is one AYTHER entry point to version. CI loads extensions-off,
standard-idle and legacy profiles with both relevant `SOUND_PROBE=0/1`
combinations and runs
`tests/ci/verify_ayther_api.c`, which verifies:

- missing-symbol-safe discovery logic and version errors;
- descriptor/capability/layout negotiation;
- optional audio transport discovery, layout sizes and feature-off behavior;
- all region sizes and legacy mappings;
- generation-safe reads and validated controls;
- supported, requested and frame-boundary-active subscription states;
- the legacy `0x100`–`0x10E` adapter;
- exact dynamic symbol resolution on DLL and so builds.

The legacy adapter is retained for at least the complete ABI v1 migration
window. Its eventual removal requires a new major ABI decision and a separate
migration notice.
