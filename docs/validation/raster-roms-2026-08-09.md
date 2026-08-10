# Raster fallback validation with local ROMs — 2026-08-09

This report validates issue #5's safety contract against a local Mega Drive
collection. No ROM content or generated frame is stored in the repository.

## Contract and method

For every emulated frame, `tests/ayther/raster_rom_probe.c`:

1. captures the RGB565 frame emitted by the libretro video callback;
2. reads the reason bitmask from private memory id `0x10E`;
3. negotiates ABI v1 and calls its `recompose_frame` function pointer against
   the same final VDP state;
4. compares dimensions and every output pixel;
5. fails if a mismatch has mask zero, or if recomposition is unavailable
   without `UNSUPPORTED_MODE`;
6. validates `frame_generation`, snapshot fallback parity and capture bounds
   on every frame.

The run used the Windows x64 llvm-mingw core with `SOUND_PROBE=0`, default core
options, 1,800 frames per ROM and deterministic two-frame START/A pulses to
advance title screens. Fourteen `.md` files were read directly from the local
collection: 25,200 frames in total.

## Findings and corrections

The first full run found 56 mismatches with mask zero. The ROM evidence exposed
two missing cases in the initial implementation:

- active-display writes to ordinary VRAM (patterns, name tables and SAT) are
  temporal; final-state recomposition can apply new data to earlier lines;
- while display is disabled, register 7 and its selected CRAM entry still
  affect the visible backdrop.

`AYTHER_RASTER_REASON_VRAM` was added as bit 6 without renumbering the existing
six reasons. The guard also now derives the effective scanline from CPU cycles
instead of relying only on the coarse `v_counter` value.

After the VRAM reason, false negatives dropped from 56 to 2. Both remaining
frames were isolated in the Battle Mania titles and visually confirmed as
mid-frame backdrop colour changes while display was disabled. The precise
backdrop exception eliminated both.

## Final result

| Classification | Frames |
|---|---:|
| Exact recomposition, mask zero | 22,915 |
| Exact recomposition, conservative non-zero mask | 1,760 |
| Different recomposition, protected by non-zero mask | 525 |
| Different recomposition, mask zero | **0** |
| Recomposition unavailable without reason | **0** |
| Missing video callback | **0** |
| Total | **25,200** |

Reason occurrence counts overlap because a frame can expose multiple bits:

| Reason | Frames |
|---|---:|
| `REG` | 323 |
| `VRAM` | 1,637 |
| `CRAM` | 9 |
| `VSRAM` | 421 |
| `HSCROLL` | 31 |
| `DMA` | 251 |
| `UNSUPPORTED_MODE` | 14 |

Per-ROM comparison results:

| ROM | Clean exact | Guarded exact | Guarded different | False negatives |
|---|---:|---:|---:|---:|
| Aladdin (USA) | 1,442 | 354 | 4 | 0 |
| Battle Mania (Japan) | 1,769 | 24 | 7 | 0 |
| Battle Mania Daiginjou (Japan, Korea) | 1,758 | 36 | 6 | 0 |
| Ecco the Dolphin (USA, Europe) | 1,723 | 77 | 0 | 0 |
| Golden Axe (World) (Rev A) | 1,782 | 13 | 5 | 0 |
| Golden Axe II (World) | 1,779 | 20 | 1 | 0 |
| Musha Aleste - Full Metal Fighter Ellinor (Japan) | 1,575 | 224 | 1 | 0 |
| Sonic & Knuckles + Sonic The Hedgehog 3 (Europe) | 1,611 | 188 | 1 | 0 |
| Sonic The Hedgehog (USA, Europe) | 1,530 | 269 | 1 | 0 |
| Sonic The Hedgehog 2 (World) (Rev A) | 1,587 | 212 | 1 | 0 |
| Streets of Rage 2 (USA) | 1,740 | 51 | 9 | 0 |
| Teenage Mutant Ninja Turtles - Return of the Shredder (Japan) | 1,641 | 129 | 30 | 0 |
| Toy Story (Europe) | 1,597 | 146 | 57 | 0 |
| Vectorman (USA, Europe) | 1,381 | 17 | 402 | 0 |

The local corpus satisfies the fallback safety invariant. This is strong
regression evidence, not a proof for every ROM, core option or save-state/input
branch; the probe is kept in-tree so broader corpora can be added without
versioning copyrighted fixtures.

The same 25,200-frame corpus was rerun after issue #6 moved recomposition behind
the single versioned entry point and made capture resets core-owned. All ABI
snapshot checks passed and every classification/count above remained identical.
