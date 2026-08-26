# AYTHER raster fallback contract

The private libretro memory id `0x10E` exposes one transient `u32` per frame.
It is a bitmask of reasons why final-state VDP recomposition cannot safely
replace the original frame.

| Bit | Constant | Meaning |
|---:|---|---|
| 0 | `AYTHER_RASTER_REASON_REG` | visual VDP register changed during active display |
| 1 | `AYTHER_RASTER_REASON_CRAM` | effective CRAM change during active display |
| 2 | `AYTHER_RASTER_REASON_VSRAM` | effective VSRAM change during active display |
| 3 | `AYTHER_RASTER_REASON_HSCROLL` | effective write inside the active 1 KiB hscroll block |
| 4 | `AYTHER_RASTER_REASON_DMA` | the associated memory reason originated in DMA |
| 5 | `AYTHER_RASTER_REASON_UNSUPPORTED_MODE` | current output mode is outside recompositor support |
| 6 | `AYTHER_RASTER_REASON_VRAM` | effective non-hscroll VRAM change during active display |
| 7 | `AYTHER_RASTER_REASON_JOURNAL_OVERFLOW` | the frame produced more replayable events than the journal holds (#27) |

`DMA` is an origin bit, not a standalone destination. For example, a CRAM DMA
produces `CRAM | DMA`; DMA copy/fill into the hscroll block produces
`HSCROLL | DMA`.

## Compatibility and lifecycle

- Existing consumers may continue to treat `value > 0` as the fallback flag.
- New consumers can decode individual bits from `core/ayther_raster.h`.
- The core resets the mask at the beginning of every Genesis, Sega CD or
  8-bit-system frame. A supported frame starts at zero; an unsupported mode
  starts with `UNSUPPORTED_MODE`.
- Writing zero through private memory id `0x10E` remains supported during the
  transition, but is no longer required for normal frame lifecycle.
- The mask is transient and is not part of `vdp_context_save()` or
  `vdp_context_load()`, so save/load cannot persist a stale fallback reason.

## Marking rules

A memory reason is recorded only when all three conditions hold:

1. the target value actually changes;
2. the VDP is on a visible line and the target can affect its output (normally
   display enabled, with a backdrop exception described below);
3. the memory affects temporal reconstruction (VRAM, CRAM or VSRAM).

VRAM writes inside the aligned hscroll block receive the more specific
`HSCROLL` reason; other active-display VRAM writes receive `VRAM`. This includes
pattern, name-table and SAT updates: a final-state renderer would otherwise use
new data for lines that the original renderer drew before the write. Writes
during vertical blanking remain clean. With display disabled, VRAM/VSRAM and
non-backdrop CRAM writes remain clean, but changes to register 7 or its selected
CRAM entry are still marked because the backdrop remains visible. A
display-enable register transition is also a `REG` reason because the
transition itself changes visible output.

The audited production paths are:

- 68K data-port writes through `vdp_bus_w`;
- Z80 Mode 5 byte writes to VRAM, CRAM and VSRAM;
- Z80 Mode 4 byte writes to VRAM and CRAM — Master System, Game Gear and the
  Mega Drive in Power Base Converter mode (#40). Register writes on those
  systems already went through `vdp_reg_w`, so `REG` was reported; `CRAM` and
  `VRAM` were not, and a mid-frame palette split on a Master System left the
  mask at zero;
- DMA from all 68K source areas through `vdp_bus_w`;
- DMA copy to VRAM;
- DMA fill to VRAM, CRAM and VSRAM.

## Unsupported output modes

The current recompositor reports `UNSUPPORTED_MODE` for TMS video modes
(SG-1000 / ColecoVision), interlace mode 2, doubled interlaced output,
NTSC-filter output and builds without 16-bit rendering. This mirrors the guards
in `ayther_recompose_frame()`; the predicate is `ayther_recompose_mode_supported()`.

Mode 4 (Master System, Game Gear, Power Base Converter) is supported since #40:
a frame starts at zero and `ayther_recompose_frame()` reproduces it from the
final state. There is **no replay path in Mode 4** — `recompose_multilayer` is
Mode 5 only — so every replayable reason is, in Mode 4, a plain fallback: the
final-state picture is right up to the line of the first event and wrong after
it (or before it, when the event restores an earlier value during the frame),
and the mask is the only thing that tells a frontend which frames those are.
`tests/ayther/mode4_raster.c` pins both halves: a scroll-lock-only scene
recomposes pixel-perfect with the mask at zero, and a mid-frame palette split
reports `CRAM`, journals the event on its line, and confines the recomposition
mismatch to one side of that line.

The versioned public ABI and capability discovery remain tracked by issue #6.

## Validation

The reusable libretro probe and the 14-ROM, 25,200-frame result are documented
in [`validation/raster-roms-2026-08-09.md`](validation/raster-roms-2026-08-09.md).
The final run produced zero recomposition mismatches with mask zero.

## The raster journal and what replay reproduces (#27)

The bitmask above says *whether* the frame has mid-screen changes. The **journal**
(`ayther_raster_journal`, 256 entries) records the individual events so
`ayther_recompose_multilayer` can replay them line by line instead of rendering
from the VDP's end-of-frame state alone.

### Only replayable reasons are journaled

`AYTHER_RASTER_REASON_REPLAYABLE` = `REG | CRAM | VSRAM | HSCROLL`.

A mid-screen write to pattern VRAM is a legitimate fallback reason, but the
replay does not apply it, so spending a journal slot on it only displaces the
CRAM or hscroll event that *would* have been reproduced. Measured on the
synthetic fixture before this filter existed: the journal saturated at 256 in
all 120 frames and dropped useful events to store ones nothing reads. After the
filter: 113 of 256 slots used, nothing dropped. The bitmask still records every
reason, replayable or not.

### Register events

A visual register change is now an event, not just a bit. Before, only the `REG`
bit was set and nothing was recorded, so the replay's register branch was dead
code: no mid-frame change of plane base, scroll size, window clipping or
backdrop colour was ever reproduced, and the frame stayed in fallback for a
reason the recompositor already knew how to handle.

Replaying a register means recomputing the **derived** state the renderer
actually reads (`ntab`, `ntbb`, `ntwb`, `satb`, `hscb`, `hscroll_mask`, the
playfield masks, the window clip, the backdrop colour) — assigning `reg[r]`
alone would change the register and draw exactly as before. The replay uses the
same layout tables as `vdp_reg_w` rather than a private copy.

Registers 10, 14, 15 and 19–23 do not affect rendering and are excluded by
design. A mid-frame change of H40/H32 (`reg 12` bit 0) is **not** replayed: it
moves the viewport width, which a fixed-size output buffer cannot represent, so
it is reported as `UNSUPPORTED_MODE`.

### Overflow

When more replayable events occur than the journal holds, the surplus is counted
(`raster_events_dropped`, also exposed in `ayther_frame_delta_v1`) and
`JOURNAL_OVERFLOW` is set. `ayther_recompose_multilayer` then returns
`AYTHER_STATUS_RC_JOURNAL_OVERFLOW` instead of replaying a prefix: a partial
replay yields a plausible, wrong image, which is precisely what a frontend
cannot detect on its own.

### Replay leaves no trace

The replay mutates real VDP state and restores it: CRAM, VSRAM, registers and
derived state are snapshotted, and VRAM writes are undone through an exact
per-write undo log rather than by restoring a fixed 1 KiB window — a `reg 13`
event can move the hscroll base mid-frame, in which case the saved window and
the written window are not the same memory.

It also no longer writes to `ayther_raster_dirty`. Merging reasons from inside
the replay mutated the frame's public fallback mask from a call the ABI defines
as read-only.
