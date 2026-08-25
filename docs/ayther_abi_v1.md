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

### The version check a consumer must actually perform

The rule is **same major, minor at least what you need** — never
`abi_version == mine`. The core returns a single descriptor and answers every
request whose major matches and whose minor it can satisfy, so a consumer built
against 1.0 keeps working against a 1.1 core: the 1.0 fields are at the same
offsets, and `struct_size` says where its knowledge ends. A request for a minor
the core does not implement returns `NULL`, because serving it would mean
promising fields that are not there.

Guard every optional field with the header's helper, which asks `struct_size`
rather than the version:

```c
if (AYTHER_IFACE_HAS(api, get_recompose_stats))
  api->get_recompose_stats(&stats, sizeof(stats));
```

### Version history

| Version | Change |
|---|---|
| 1.0 | Initial contract: regions, snapshots, subscriptions, recomposition, audio transport, frame delta. |
| 1.1 | **Additive.** Appends `recompose_stats_size` + `get_recompose_stats` and the `AYTHER_CAP_RECOMPOSE_STATS_V1` capability bit (#26). No existing field moved or changed meaning. |
| 1.3 | **Additive.** Adds the `ATTRIBUTION` region, the `AYTHER_SUB_ATTRIBUTION` subscription and the `AYTHER_CAP_ATTRIBUTION_V1` capability (#41). |
| 1.4 | **Additive.** Appends `frame_delta_since` to the descriptor, the
`AYTHER_CAP_FRAME_DELTA_SINCE_V1` capability and the
`AYTHER_STATUS_DELTA_HISTORY_LOST` status (#30). `poll_frame_delta` stops
consuming the bitmap: it now returns the frozen delta of the last completed
frame, so two consumers of the same frame read the same thing. Before, the
second one read zero. |
| 1.2 | **Additive.** Appends `recompose_multilayer` to the descriptor and adds the `AYTHER_REGION_WORD_SWAPPED_LE` region flag (#32). The standalone `ayther_recompose_multilayer` export is now **deprecated** and is emitted only in the legacy profile. |
| 1.5 | **Additive.** Adds the `SYSTEM` region and `AYTHER_CAP_SYSTEM_V1` (#39.B), plus the `AYTHER_STATUS_UNSUPPORTED_MODE` status and `AYTHER_CAP_MODE4_CONTROLS` (#40). No existing field moved. Controls that only exist in Mode 5 now say so instead of accepting the write and doing nothing. |
| 1.6 | **Additive.** Adds the `LINE_REGS`, `LINE_CRAM` and `LINE_CELLS` regions, the `AYTHER_SUB_LINE_STATE` / `AYTHER_SUB_LINE_CRAM` / `AYTHER_SUB_LINE_CELLS` subscriptions and `AYTHER_CAP_LINE_STATE_V1` (#42). `AYTHER_SUB_ALL` widens from `0xFF` to `0x7FF`; the eight existing bits keep their positions. |
| 1.7 | **Additive.** Adds the `RASTER_JOURNAL`, `FRAME_HASH` and `PALETTE` regions, the `AYTHER_SUB_FRAME_HASH` subscription and `AYTHER_CAP_OBSERVABILITY_V1` (#39 A/D/E). `AYTHER_SUB_ALL` widens to `0xFFF`. No existing structure changes size or order. |
| 1.8 | **Additive.** Adds the `SPRITE_OUTCOME` region and `AYTHER_CAP_SPRITE_OUTCOME_V1` (#39.C). Deliberately a parallel region rather than new fields on `ayther_sprite_v1`: that struct travels with its size announced in the descriptor, and a 1.0 consumer that transcribed it with its own types would read the array shifted from the second element on. |

Two earlier changes were shipped inside 1.0 without a bump, which is what this
table exists to prevent from recurring:

- `AYTHER_LAYOUT_AUDIO_EVENT_V1` went from `1` to `2` when PCM events moved off
  the `voice` arm. Every event carries the layout in its `schema` byte, so a
  consumer can tell the two apart at runtime — but it should have been a minor
  bump, not a silent redefinition.
- `AUDIO_MUTE` widened from `uint16_t` to `uint32_t` (bit 18 onwards reserved)
  when the Mega CD PCM channels were added.

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
`ayther_audio_write_v1`, 40 bytes for `ayther_audio_voice_v1`, 80 bytes for
`ayther_audio_event_v1`, and 32 bytes for
`ayther_audio_transport_stats_v1` and `ayther_subscription_state_v1`.
Compile-time assertions verify the public layouts and their internal aliases.

## Endianness, ownership and lifetime

This is an in-process ABI, not a serialized wire format. Fixed-width scalar
fields use the host endianness reported by `host_endianness`. CRAM, VSRAM,
sprites, audio writes, counters and raster reasons are marked
`AYTHER_REGION_NATIVE_ENDIAN`; VRAM and VDP register regions retain the core's
raw byte representation.

**VRAM is word-swapped on little-endian hosts**: the logical byte at offset
`off` lives at `off ^ 1`, the emulator's internal layout. Since 1.2 the region
declares this with `AYTHER_REGION_WORD_SWAPPED_LE` in `access_flags`, so a
consumer can discover it from the descriptor instead of from prose — reading it
straight gives an image with the bytes of every tile crossed, which looks like a
frontend bug. This is a different axis from `AYTHER_REGION_NATIVE_ENDIAN`: that
one is about the byte order of multi-byte *fields*, this one about the order of
*bytes within the emulated memory*.

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
| `VRAM` | raw bytes, **word-swapped on LE** | 65,536 / 65,536 | read | standard `0x003` |
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

The `AUDIO_WRITES` log is produced by the core (`sound.c`, `psg.c`) under
`AYTHER_SUB_AUDIO_WRITES`, **not** by the audio probe: the capability belongs to
the ABI, so it must produce with `SOUND_PROBE=0` too (#29). `addr` carries the
decoded FM register `(part << 8) | reg`, not the raw bus port; PSG writes are
single-byte commands and report `addr = 0`.
| `AUDIO_WRITE_COUNT` | `uint32_t` | 1 / 4 | frame read | `0x10A` |
| `PARSED_SPRITES` | `ayther_sprite_v1` | 128 / 1,280 | frame read | `0x10B` |
| `PARSED_SPRITE_COUNT` | `uint8_t` | 1 / 1 | frame read | `0x10C` |
| `AUDIO_MUTE` | `uint32_t` | 1 / 4 | read/control | `0x10D` |
| `RASTER_FALLBACK_REASONS` | `uint32_t` | 1 / 4 | frame read | `0x10E` |
| `RASTER_JOURNAL` | `ayther_journal_v1` | 1 / 2,064 | frame read | — |
| `FRAME_HASH` | `ayther_frame_hash_v1` | 1 / 56 | frame read | — |
| `PALETTE` | build pixel type | 256 / 512 (16bpp) | read | — |
| `SPRITE_OUTCOME` | `uint8_t` bitfield | 80 / 80 | frame read | — |

The public ABI treats emulated memory and frame counters as read-only. The
legacy pointers preserve their historical mutability for the transition.

## Frame Delta: double buffer and history (#30)

`poll_frame_delta` used to be **consume-on-poll**: reading the dirty-pattern
bitmap cleared it. That made it valid for exactly one consumer per frame, which
is not how it is used -- the debug Lab and the HD engine read the same frame, and
whichever asked second got an empty bitmap and never invalidated its assets.

What the bitmap is accumulated into during a frame is **frozen** when the frame
ends, and the read returns the frozen copy without touching it. Reading it N
times returns the same thing N times.

`frame_delta_since(generation_from, out, out_size)` ORs a ring of
`AYTHER_FRAME_DELTA_HISTORY` (8) frames, for a consumer that does not read every
frame. A generation that already fell out of the ring returns
`AYTHER_STATUS_DELTA_HISTORY_LOST` with **everything** marked dirty: returning
only the subset still in the ring would be worse than useless, because the
consumer would believe it saw every change.

One property the old design bought and this one keeps: `retro_unserialize` can
replace the whole VRAM without a single frame running, and `vdp_ctrl.c` marks
everything dirty on context load. The frozen copy is refreshed there too, so the
consumer does not have to wait for the next frame end to find out.

**Not yet gated.** The issue also asks for `NOT_SUBSCRIBED` without an explicit
subscription. That bit does not exist yet -- bits 0-7 are taken and
`AYTHER_SUB_ALL` is `0xFF` -- and adding it would be a **breaking** change:
consumers that poll the delta today do so without subscribing, and would start
getting `NOT_SUBSCRIBED`. It is tracked with the strict-idle work, where the
accumulation cost lives.

## Recomposition and Delta Stream

Recomposition capabilities (`ayther_recompose_frame` and `ayther_core_recompose_multilayer`) are exposed via function pointers in the interface. They allow rendering specific layers or frames with custom parameters (like ignoring sprite limits or layer masks) without mutating the core's deterministic state. This effectively acts as an oracle for the Delta Stream implementation, which uses these capabilities to isolate and stream partial frame updates (such as only sprites or a single plane) to the frontend, significantly accelerating network replication and state sync.

### Recomposition cache key (#26)

Both recompositors cache their last result. The key is:

```
(core frame generation, flags, effective layer mask, controls fingerprint,
 output width, output height)
```

`controls fingerprint` is an FNV-1a hash over the **contents** of every control
region — layer mask, layer dim, sprite suppression, cell suppression, plane-tile
suppression and its active flag — plus whether `AYTHER_SUB_RENDER_CONTROLS` is
active, since the controls only take effect while subscribed.

It hashes contents rather than counting writes because the legacy ids
`0x102`–`0x108` still hand out mutable pointers through
`retro_get_memory_data`: a legacy consumer writes a mask without ever calling
`write_control`, so there is no hook where a counter could be bumped, and
`snapshot_generation` does not move. Content is the only thing both paths share.

The practical guarantee for a frontend: **the pixels you get always match the
control state you set**, no matter the order or number of calls on a given
frame. Before this key existed, writing a mask between two recompositions of the
same frame returned the previous image — silently, which in the Lab reads as an
unresponsive UI rather than as an error.

Hashing ~3.6 KB costs less than the ~150 KB copy a cache hit performs, so the
correctness fix does not change the case for keeping the cache.

`get_recompose_stats` (ABI 1.1) reports `single_calls`/`single_hits`,
`multilayer_calls`/`multilayer_hits` and the current `controls_fingerprint`. Use
it to answer "why did my recomposition not hit the cache?": if the fingerprint
moved and you did not call `write_control`, something wrote through a legacy
mutable pointer.


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
- audio mute: 18 valid channel bits (FM 0-5, PSG 6-9, Mega CD PCM 10-17);
  anything at bit 18 or above is rejected. It widened from `uint16_t`/`0x03FF`
  when the PCM chip was added, so a legacy reader of `0x10D` that still assumes
  two bytes reads half the mask — and on a big-endian host, the wrong half;
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


## Per-pixel attribution (#41)

`AYTHER_REGION_ATTRIBUTION` is one byte per pixel of the **emitted** frame, in
framebuffer order, saying who painted it:

```
bits 7-6  layer: 0 backdrop, 1 Plane B, 2 Plane A, 3 Window
bit  5    priority of the winning background cell
bits 4-3  palette line (0-3)
bits 2-1  shadow/highlight: 0 normal, 1 shadow, 2 highlight
bit  0    the visible pixel was put there by a sprite
```

It exists because the frontend was answering this question by recomposing the
frame once per layer and diffing — N render passes for something the VDP already
knows while it draws. With this, "which asset replaces this pixel" is one byte.

### Why the layer is decided by rule, not by comparison

The obvious implementation — render the layers, then compare each output pixel
against each source — is wrong in exactly the pixels that matter. Two layers can
produce the same byte, and there the comparison answers arbitrarily. The core
instead replicates the priority rule of the merge LUT: A wins if it has priority
and is opaque; otherwise B wins if it has priority and is opaque; otherwise A
wins if it is opaque. No ambiguity.

This is not hypothetical. On the synthetic fixture both planes draw the same
content, so **2,560 of 2,560** non-backdrop background pixels are ambiguous to
the comparison method. The core has no such trouble.

Plane A and Window share a line buffer but occupy disjoint x ranges (`clip[0]`
and `clip[1]`), so telling them apart is exact and free.

### Cost and gating

Captured inside `merge()`, the one point every Mode 5 renderer passes through —
putting stores in each renderer's inner loop would have meant touching the
hottest code in the emulator five times, and the information needed (which layer
won) only exists at the merge. Nothing is captured without
`AYTHER_SUB_ATTRIBUTION`, which is a bit of its own rather than part of
`RENDER_CONTROLS`: a byte per pixel per frame is a different order of cost, and
a consumer that only hides layers should not pay it.

### The sprite bit (#31/#37/#41)

Bit 0 used to come from comparing the line buffer before and after
`render_obj`. That gave two wrong answers, and both mattered: a sprite pixel
whose byte happened to equal the background byte was **not** marked — a hole
inside the sprite — and a pixel that only changed because of a shadow/highlight
operator **was** marked, though an operator places no colour at all.

It is now written where the priority is decided, and that is not one place:
`render_obj_m5` / `_im2` draw straight into `linebuf[0]` and the LUT resolves
sprite-against-background pixel by pixel, so the store lives in the loop;
`render_obj_m5_ste` / `_im2_ste` draw into `linebuf[1]` and merge afterwards, so
the store lives in the merge. Answering the question where it is *not* decided
is exactly the mistake the diff made.

The rule is the one `make_lut_bgobj` applies, written as a predicate in
`core/ayther/ayther_sprite_px.h`: the sprite has colour, and either it has
priority, or the background does not, or the background is transparent.
`test_sprite_px` checks that predicate against a copy of `make_lut_bgobj` over
every unambiguous combination — replicating a rule without proving the replica
agrees is how a copy drifts from the original.

Neither store costs anything with no subscribers: the in-loop one lives under a
clone parameter the compiler folds away, the merge one under a flag that is only
raised when `layer_dim` or attribution is active.

Mode 4 and TMS reach neither path and keep the old diff, with its known flaw,
until #40 phase 2 brings those modes into scope.

## System descriptor (#39.B)

`AYTHER_REGION_SYSTEM` returns an `ayther_system_v1`: which hardware is running,
which video mode the VDP is in, the emitted viewport, the clocks, which FM core
is selected, and the identity of the loaded ROM.

All of it was already derivable — by decoding `VDP_REGS` on the consumer side.
H40 is bit 0 of register 12, interlace is bits 1-2, the active line count is
bit 3 of register 1, and so on. Decoding it outside means reimplementing the
core's rules in another repository, and when the core corrects them — which has
happened: #28 fixed exactly those masks — nothing tells the copy it went stale.
The core knows the answer; giving it costs a struct.

The region is **read-only and needs no subscription**. It is filled *when read*,
not once per frame: it is about forty bytes that nobody looks at in most frames,
and filling them unconditionally is precisely the kind of idle work #36 removed
from six other places. Between frames — which is when a frontend can read — the
VDP state is the state of the frame that just finished, which is the right
answer.

`vdp_mode` is `0` on a Mega Drive cartridge until the program writes register 1:
before that the VDP has not chosen, and answering "Mode 4" would be inventing.

## Control × video mode (#40)

Not every control means something in every video mode. Until #40 the ones that
do not simply returned `AYTHER_STATUS_OK` and did nothing, which is the worst
contract available: a frontend believes it hid a sprite and draws its HD
replacement on top of the original.

| Control | Mode 5 (MD) | Mode 4 (SMS/GG/PBC) |
|---|---|---|
| `LAYER_MASK` sprite bit | yes | yes — resolved in `render_line`, shared by both |
| `LAYER_MASK` A/B/W bits | yes | **`UNSUPPORTED_MODE`** — one background plane, the bits have no referent |
| `SPRITE_SUPPRESS` (0x103) | yes | **`UNSUPPORTED_MODE`** — `parse_satb_m4` has no suppression |
| `TILE_SUPPRESS` / peel (0x104) | yes | **`UNSUPPORTED_MODE`** — `render_bg_m4` has no hooks and does not merge |
| `PLANE_TILE_SUPPRESS` (0x105/0x106) | yes | **`UNSUPPORTED_MODE`** — same reason |
| `LAYER_DIM` (0x108) | yes | yes — `remap_line` is shared |
| Audio mute and gain (0x10D) | yes | yes |
| `ATTRIBUTION` | yes | background layer only |
| Recomposition | yes | `AYTHER_STATUS_RC_NOT_MODE5` |

A rejected write also raises `AYTHER_RASTER_REASON_UNSUPPORTED_CONTROLS` in
`0x10E`, so a frontend polling the fallback reasons sees it without having to
check every return value.

`AYTHER_CAP_MODE4_CONTROLS` announces that the rows above have become "yes".
While the bit is absent, the table is the contract.

## Per-scanline render state (#42)

`LINE_REGS` and `LINE_CRAM` give the state the VDP actually used **on each
scanline**: the resolved horizontal and vertical scroll, the resolved table
bases, the registers that matter for reconstruction, the window clip, and the
palette in force on that line.

It is captured on the **read** side — where the renderer consumes it — not
reconstructed from the write side. Reconstructing it from writes is what the
raster journal does, and that is approximate by construction: you have to guess
which cycle of which line each write landed on and what it affected. Capturing
it where the renderer uses it is exact — it is literally the value it used — and
cheaper, because there is nothing to rebuild.

What it unlocks: the screen cell a frontend can hide today (`0x104`) is
frame-space, and with per-line scroll it never lines up with a plane tile. With
`xscroll_a` for the line, "cell (x,y) of the screen" maps to "this tile of plane
A" — which is what an HD substitution pipeline needs in order to key an asset.

### Layout

Both regions are a header followed by entries, in one contiguous buffer. The
header is *inside* because a consumer needs the line count and the frame
generation in the **same** read: two separate reads can land on either side of a
frame boundary.

`LINE_CRAM` costs 128 bytes per line, so it does not pay it when nobody changed
the palette: a frame with no mid-frame CRAM writes returns **one** entry and the
`AYTHER_LINES_CRAM_UNIFORM` flag — 128 bytes instead of 30 KB.

### Memory

The recomposition caches (1.15 MB in total) used to be static, and therefore
resident in every build with extensions whether anyone looked or not. A core
nobody observes carried them in memory and, worse, in cache: 1.15 MB that is
never touched still evicts lines that are. They are now requested on the first
recomposition and released in `retro_deinit`, so a frontend that only reads VRAM
pays nothing for them (#36).

### Cost

The capture lives in the observed clone of `render_bg_m5*` and runs only under
subscription: with no subscribers, nothing executes. The `-DAYTHER_METRICS`
counters confirm it, and the CRAM copy is skipped entirely until the palette
actually changes.

### Per-cell provenance (`LINE_CELLS`)

`LINE_CELLS` says, for each 16-pixel column of each line, **which name-table
entry** the VDP read and **which row of the tile**. That is the last piece the
cell→tile mapping needs: `LINE_REGS` gives the scroll, `LINE_CELLS` gives the
entry the scroll landed on, and neither requires the consumer to reimplement the
core's address arithmetic.

The VDP reads the name table two cells at a time — one 32-bit access — and that
is what is stored: `name_pair`, the raw value the renderer consumed. Storing
pairs rather than individual cells is what makes the capture free: the value was
already in a register.

Row and fine shift are **per line and per plane**, not per column: the renderer
computes them once. Storing them per column would repeat the same byte 21 times.

The region declares `AYTHER_REGION_WORD_SWAPPED_LE`, because "raw" includes byte
order: on a little-endian host the core keeps VRAM byte-swapped. Reading the
region without honouring that flag yields reversed cells with no symptom to give
it away — which is exactly what the flag exists to prevent.

### The predicate that decides the clone

`AYTHER_OBSERVED_ACTIVE` must list **every** subscription whose data is captured
inside the observed clone. Forgetting one does not raise an error: it produces a
region that answers `OK` and comes back empty. That is how #41 was found, and it
happened again while adding these regions. Any future region filled in there
gets its bit added to that list.
## Observability: journal, hashes and palette (#39 A/D/E)

Three read-only regions, all for the same reason: the core already knows the
answer, and the frontend was re-deriving it with its own copy of the rules.

### `RASTER_JOURNAL`

The journal already existed — it is what the raster replay consumes. The only
thing exposed was its **count**. Knowing a frame had 17 events and not *which*
is enough to decide "don't recompose this one" and nothing else; a frontend that
wants to understand the split had to re-read VDP memory frame by frame and guess
when it changed.

`dropped` is a count and not a bit, matching the core: a frontend sizing its own
buffer needs to tell "missed by one" from "this frame is a festival of splits".

It is gated on `AYTHER_SUB_RASTER_TRACKING` — the subscription that makes the
journal exist at all. Without it the region answers `NOT_SUBSCRIBED` instead of
handing back the fossil journal of the last observed frame.

### `FRAME_HASH`

Answers "are we still in sync?" without serializing. Until now the only way was
to ask for the whole savestate — ~1 MB — and hash it outside, once per frame, to
compare one number.

There is deliberately **no `state_hash`**: computing it requires serializing,
which is exactly what this region exists to avoid. The four VDP memories plus
the emitted video and audio cover the desync a frontend can actually see; for
the rest of the state there is `retro_serialize`, which is where that question
belongs.

The algorithm is 64-bit FNV-1a over the bytes in the core's own order — the same
one `full_core_replay` uses — so a frontend can recompute and compare rather
than trust. The video hash walks the frame **row by row**, `width` pixels at a
time, skipping by pitch: the pitch covers 720 px and the frame occupies less, so
hashing in one shot would fold in padding nobody sees. `tests/ayther/observability.c`
computes the same number from the `video_refresh` callback and requires the two
to match.

This is the only part of 1.7 that costs anything — about 100 KB walked per frame
— and therefore the only one with its own subscription.

### `PALETTE`

The core builds this table anyway, in `color_update_m5`: indexed by the merged
line-buffer byte, with shadow and highlight already applied. A frontend that
wanted the colour of an index had to redo the 9-bit → pixel conversion and the
S/H rules itself.

The whole 256-entry table is exposed, not the 192 "useful" ones: the index *is*
the line-buffer byte, and trimming it would force the consumer to know the trim
in order to map back. Entry width comes from the region's `element_size`,
because it depends on the pixel format the core was built with — a consumer that
hardcodes 2 bytes misreads a 32bpp build with no symptom.

**It is the current table, not one per line.** A game that writes CRAM mid-frame
— a palette raster — produces an image with colours from several tables, and
this region answers the last one. That is not a hidden limitation: `LINE_CRAM`
(#42) covers the per-line case, and `RASTER_JOURNAL` says whether the frame had
CRAM writes at all. With a journal free of CRAM events this table explains every
colour in the frame; with them, it cannot. The test measures the property on the
S/H fixture, which has no H-int and therefore a palette that does not move.
