# AYTHER feature gates and runtime subscriptions

AYTHER extensions have three distinct states: absent from the binary, compiled
but idle, and actively observed. This keeps a stock-compatible core small while
allowing one extension-enabled DLL to turn individual integrations on at a
deterministic frame boundary.

## Build profiles

| Profile | Build variables | Initial state | Intended use |
|---|---|---|---|
| Extensions off | `AYTHER_EXTENSIONS=0 SOUND_PROBE=0` | ABI and private AYTHER regions are absent | stock-compatible distribution and baseline |
| Standard | `AYTHER_EXTENSIONS=1 AYTHER_LEGACY_PROFILE=0` | supported mask is reported; active/requested masks are zero | new AYTHER integrations |
| Legacy | `AYTHER_EXTENSIONS=1 AYTHER_LEGACY_PROFILE=1` | every compiled subsystem starts active | transition for consumers that do not subscribe |

`SOUND_PROBE=1` additionally compiles the audio event transport and requires
`AYTHER_EXTENSIONS=1`. `SOUND_PROBE=0` removes that transport and its large ring
buffer; `AYTHER_SUB_AUDIO_EVENTS` is then absent from `supported_mask`.

Examples:

```sh
# No AYTHER ABI, hooks or extension-owned buffers.
make -f Makefile.libretro platform=unix \
  AYTHER_EXTENSIONS=0 SOUND_PROBE=0 -j2

# ABI present, zero subscriptions until the host opts in.
make -f Makefile.libretro platform=unix \
  AYTHER_EXTENSIONS=1 AYTHER_LEGACY_PROFILE=0 SOUND_PROBE=1 -j2

# Compatibility behavior for a legacy host.
make -f Makefile.libretro platform=unix \
  AYTHER_EXTENSIONS=1 AYTHER_LEGACY_PROFILE=1 SOUND_PROBE=1 -j2
```

The standard libretro `RETRO_MEMORY_VIDEO_RAM` adapter remains available in an
extensions-off build. AYTHER's discovery symbol, private `0x100`–`0x10E`
regions, controls and extension-owned buffers do not.

## Subscription mask

The immutable ABI descriptor advertises `AYTHER_CAP_SUBSCRIPTIONS_V1` and
appends `get_subscriptions` / `set_subscriptions`. The returned
`ayther_subscription_state_v1` is 32 bytes and contains:

- `supported_mask`: subsystems actually compiled into this binary;
- `active_mask`: work enabled for the current frame;
- `requested_mask`: state requested for the next frame boundary;
- `activation_frame`: current frame when there is no pending change, otherwise
  the next frame generation.

| Bit | Enables | Runtime effect while inactive |
|---|---|---|
| `AYTHER_SUB_VDP_MEMORY` | ABI reads of VRAM, CRAM, VSRAM and VDP registers | reads return `AYTHER_STATUS_NOT_SUBSCRIBED` |
| `AYTHER_SUB_SPRITE_CAPTURE` | parsed-sprite capture with fixed O(1)-amortized identity hash | parser uses its fast path and captures nothing |
| `AYTHER_SUB_RENDER_CONTROLS` | layer/sprite/tile suppression, dimming and audio mute/gain | render/audio use unmodified fast paths |
| `AYTHER_SUB_RASTER_TRACKING` | per-frame raster fallback classification | VDP write hooks do not classify or accumulate reasons |
| `AYTHER_SUB_AUDIO_WRITES` | bounded PSG/YM2612 write capture | writes are not copied into the frame buffer |
| `AYTHER_SUB_RECOMPOSITION` | ABI frame recomposition | call returns `AYTHER_STATUS_NOT_SUBSCRIBED` |
| `AYTHER_SUB_AUDIO_EVENTS` | optional `audio_probe` SPSC transport | producers emit no events |

Capabilities answer whether an operation exists in the binary. Subscriptions
answer whether its observation/control work is active now. An unavailable
compile-time feature returns `AYTHER_STATUS_UNSUPPORTED`; a supported but idle
feature returns `AYTHER_STATUS_NOT_SUBSCRIBED`.

## Activation protocol

Subscription changes are all-or-nothing masks. A host should:

1. negotiate ABI v1 and verify `AYTHER_CAP_SUBSCRIPTIONS_V1`;
2. call `get_subscriptions` and retain only bits from `supported_mask`;
3. call `set_subscriptions(mask)` between frames;
4. call `retro_run`; the requested mask becomes active at the beginning of
   that frame;
5. query the state again if it needs to confirm activation.

Unknown or unsupported bits are rejected with
`AYTHER_STATUS_INVALID_ARGUMENT`. A change attempted while `retro_run` is
executing returns `AYTHER_STATUS_BUSY`. Reset, content load and unload restore
the build profile's initial mask. Subscription state is a host/runtime contract
and is intentionally not serialized into emulator savestates.

Control regions remain readable and writable while the corresponding runtime
work is idle, allowing a host to prepare state before activation. Their effects
are applied only while `AYTHER_SUB_RENDER_CONTROLS` is active. Plane-suppression
activity remains derived from its bitmap; there is no second flag for a host to
keep synchronized.

## Performance and correctness guard

Hot render and audio loops dispatch to physically separate fast and observed
variants. Audio control values are latched once per frame instead of loaded
atomically per sample. Extension-owned capture arrays are compile-gated, and
the standard profile clears no sprite, raster or audio buffers unless their
subsystem is active.

Observed Mode 5 parsing latches capture and suppression state once per
scanline. Sprite identities include raw Y/X/attribute, dimensions, SAT slot and
chain position. A slot rewritten during active display therefore produces a
second ordered identity, while the same identity repeated across scanlines is
deduplicated through a fixed 256-entry generation-tagged table. There are no
allocations or per-frame table clears in the hot path; the public 128-entry
capacity and overflow signal remain unchanged.

The generated-ROM replay verifies that an inactive extensions build and an
extensions-off build emit identical video and audio, and that toggling
subscriptions does not alter video, audio or serialized state. Its interleaved
seven-round profile comparison uses the median paired frame time and fails when
compiled-idle overhead reaches 1%:

```sh
make -C tests check-profile-comparison \
  PROFILE_OFF_CORE=/path/to/extensions-off-core \
  PROFILE_IDLE_CORE=/path/to/standard-idle-core
```

Absolute hosted-runner timings remain diagnostic; the `<1%` gate is a paired
comparison of binaries built from the same source and exercised by the same
process and generated fixture.
