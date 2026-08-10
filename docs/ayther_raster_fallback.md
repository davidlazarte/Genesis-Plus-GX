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
- DMA from all 68K source areas through `vdp_bus_w`;
- DMA copy to VRAM;
- DMA fill to VRAM, CRAM and VSRAM.

## Unsupported output modes

The current recompositor reports `UNSUPPORTED_MODE` for non-Mega-Drive video,
Mode 4, interlace mode 2, doubled interlaced output, NTSC-filter output and
builds without 16-bit rendering. This mirrors the guards in
`ayther_recompose_frame()`.

The versioned public ABI and capability discovery remain tracked by issue #6.

## Validation

The reusable libretro probe and the 14-ROM, 25,200-frame result are documented
in [`validation/raster-roms-2026-08-09.md`](validation/raster-roms-2026-08-09.md).
The final run produced zero recomposition mismatches with mask zero.
