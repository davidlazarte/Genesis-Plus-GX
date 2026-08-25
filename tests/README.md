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

Expected unit output includes:

```
audio_probe unit tests: 64 passed, 0 failed
audio_probe concurrency tests: 10 passed, 0 failed; events=3000000
sprite capture unit tests: 13 passed, 0 failed
```

`make check` exits non-zero if any check fails (CI-friendly).

`test_sprite_capture.c` separately validates issue #10's generation-tagged
hash: exact duplicate removal, complete identity fields, deterministic order,
mid-frame rewrites of one SAT slot, 10-bit interlace Y coordinates, overflow
and frame isolation. Production builds compile out its metrics counters.

## AYTHER ABI v1

`test_ayther_api.c` freezes the public fixed-width layouts and constants from
`core/ayther/ayther_api.h`. CI additionally compiles
`ci/verify_ayther_api.c`, loads the produced DLL/so, negotiates v1 and validates
capabilities, all region sizes, generation handling, controlled writes and the
complete legacy adapter. Without `--require`, that verifier also exercises the
safe stock/pre-ABI path: a missing symbol is reported as zero capabilities
instead of being called. CI proves that branch with `ci/mock_stock_core.c`.

The command also runs `ayther/audio_probe_trace.c`. That harness emits a fixed
sequence of FM, PSG, DAC, frame and state events, serializes every logical field
in an explicit little-endian format, and compares the resulting FNV-1a digest
against `ayther/golden/audio_probe_trace.json`. Struct padding and host byte
order are never hashed.

On a mismatch, the actual trace summary remains in
`tests/artifacts/audio_probe_trace.actual.json` so CI can publish it as a
diagnostic artifact.

## Full-core deterministic replay

`ayther/full_core_replay.c` loads the real libretro DLL/so and runs a generated
64 KiB Mega Drive ROM. The generator emits only repository-owned bytes and
exercises CRAM raster writes, per-line plane scroll, a 24-sprite pressure band
with a scanline-time SAT attribute rewrite, and recurring PSG/YM2612 traffic.
The frontend supplies a fixed input/config
stream, captures an initial savestate, and executes one reference plus two
independent restore/replay passes.

Every frame hashes visible RGB565 pixels, stereo samples, zero-initialized
serialized state, canonical audio telemetry, input and configuration. It also
compares ABI v1 recomposition with the emitted frame and records fallback
reasons, parsed sprites and audio writes. Run it after building a probe-enabled
core:

```sh
make -f Makefile.libretro platform=unix SOUND_PROBE=1 -j2
make -C tests check-full-core CORE=../genesis_plus_gx_libretro.so
```

The golden summary is a single file: `ayther/golden/full_core_replay-x64.json`.
Linux and Windows produce identical `video_hash`, `audio_hash`, `state_hash` and
`replay_hash` from it.

It was two files, one per platform, from the point where the hashes were
measured to differ (video `4d39e98f` on Linux against `dd112c2b` on Windows) up
to #45. The leading hypothesis was the worst one — a real emulation divergence
between compilers. It was neither emulation nor the compiler. Two causes, both
outside the emulated system:

1. **`rand()` in `gen_reset`.** On a *button* reset the 68k starts at a random
   point in the VDP frame (Bonkers, Eternal Champions, X-Men 2 depend on it).
   That point came from the C library's `rand()`, and glibc and MSVCRT agree on
   neither the generator nor `RAND_MAX` (2147483647 against 32767). Windows
   started the CPU somewhere else and stayed exactly one emulated frame ahead
   for the rest of the run. `core/genesis.c` now uses its own xorshift, reseeded
   on power-on: same variety across resets, same numbers on every platform.
2. **The layout tag in the state hash.** `retro_serialize` writes an AYTHER tag
   in the last 16 bytes encoding the sizes of the serialized structs, so a
   savestate from another ABI is rejected instead of silently corrupting. It
   differs across platforms *on purpose*. Hashing it mixed "what the emulated
   system did" with "how this build stores it", and made a shared golden
   impossible even with byte-identical emulation. The harness now hashes the
   state below the tag.

That the compiler was *not* the variable came from a cheap check worth repeating
in similar hunts: CI builds Linux with clang and this repo's local runs use gcc,
and both matched the same golden. Whatever differed had to be the platform.

The actual summary,
per-frame JSONL trace, first-frame savestate diagnostic and p50/p95/p99 frame
benchmark remain in `tests/artifacts/`; CI uploads them even when the golden or
replay differs.

The same harness begins with every subscription idle, verifies that sprite,
raster and audio capture remain empty, activates the supported mask at the next
frame boundary, then replays both modes from one checkpoint. Video, audio and
serialized state must stay byte-identical. `--compare-profiles` alternates an
extensions-off and a standard-idle binary for seven rounds and fails if their
paired median overhead reaches 1%; see
[`docs/ayther_subscriptions.md`](../docs/ayther_subscriptions.md).

## Raster fallback guard

`test_raster_guard.c` validates issue #5's stable reason bits and centralized
classification rules: effective-change gating, active-display gating, aligned
hscroll detection, reason accumulation, DMA origin and legacy boolean
compatibility. The production 68K, Z80 and DMA paths all feed these same rules.

### What is covered

| Area | Checks |
|------|--------|
| Lifecycle | core reset preserves already-published SPSC events |
| Raw events | source/type/reg/data/schema/timestamp round-trip |
| Timeline | frame markers advance `t_frame` and accumulate `t_global` |
| Voice decode | TL/AR/DR/SR/RR/MUL/DT, algorithm, feedback, pan, fms, block_fnum from the register shadow |
| Fingerprint | `voice_hash` / `timbre_hash` are **channel-independent** (same voice on ch0 vs ch3) |
| Notes | NOTE_ON on key-on, NOTE_OFF on key-off |
| DAC | start/stop on channel 6 |
| PSG | raw write source/data/time |
| Gain | atomic set/get, negative clamps to 0, out-of-range ignored |
| Grouping | only NOTE_ON/DAC_START anchor a fixed first-anchor window; RAW stays at group 0 |
| Ring buffer | acquire/release publication, wrap-around, exact overflow, capacity/pending/high-water stats |
| Concurrency | three million events across one producer/consumer plus callback reconfiguration |
| Callback | coherent callback/user pair, explicit active-mode statistic |
| Context | reflects ROM checksum / region / clock / cycles-per-frame |
| Determinism | canonical event trace matches the versioned golden digest |
| Raster guard | REG/VRAM/CRAM/VSRAM/HSCROLL/DMA/mode reason classification |

### Layout

```
tests/
├── Makefile            # portable `make check` (Linux and Windows)
├── README.md
├── ayther/
│   ├── audio_probe_trace.c
│   ├── generated_rom.c/.h
│   ├── full_core_replay.c
│   ├── raster_rom_probe.c
│   └── golden/{audio_probe_trace,full_core_replay-{linux,windows}-x64}.json
├── ci/verify_ayther_api.c # dynamic ABI/symbol verifier
├── stub/
│   └── shared.h        # minimal emulator stand-in for isolated builds
├── test_audio_probe.c  # audio probe unit runner
├── test_audio_probe_concurrency.c # SPSC/callback stress runner
├── test_ayther_api.c    # public ABI layout/constant runner
├── test_sprite_capture.c # fixed-hash identity/overflow runner
└── test_raster_guard.c # raster fallback reason unit runner
```

Generated binaries and mismatch reports live under `.build/` and `artifacts/`;
both directories are ignored by Git.

## Running the core under sanitizers (#38)

`tests/ci/run_sanitizers.sh <core> [golden]` runs the **full-core replay**
instrumented, not just the stub-built unit tests. Until it existed, ASan/UBSan
covered everything except `vdp_render.c`, `vdp_ctrl.c`, `ym2612.c` and
`libretro.c` — which is where the fork lives.

```sh
# Linux
make -B -f Makefile.libretro platform=unix SOUND_PROBE=1 \
  CC="clang -fsanitize=undefined -static-libsan -fno-omit-frame-pointer -g" -j2
make -C tests full-core-replay CC=clang
bash tests/ci/run_sanitizers.sh "$(pwd)/genesis_plus_gx_libretro.so"

# Windows llvm-mingw (the toolchain ships no ASan runtime)
make -f Makefile.libretro platform=win64 SOUND_PROBE=1 \
  CC="cc -fsanitize=undefined -fno-omit-frame-pointer" -j8
bash tests/ci/run_sanitizers.sh genesis_plus_gx_libretro.dll
```

**UBSan and not ASan**, on both platforms. The core is built as a shared library
with `-Wl,--no-undefined`, and ASan's runtime does not link into one: it is
expected in the executable that loads it, so you get undefined references to
`__asan_report_*`. Making it work would mean building the harness with ASan too
and preloading the runtime. `-static-libsan` puts UBSan's runtime inside the
`.so`, and UBSan is what found everything this section documents. ASan over the
full core is tracked in #45; over the stub-built tests it already runs.

Do **not** build with `-fno-sanitize-recover=all`: it traps on the first report,
which defeats `halt_on_error=0` and gives one finding per run instead of the
full inventory. The verdict is the script's job, not the compiler's.

Reports are grouped **per site** (file:line:col, with addresses normalised) and
checked against `tests/ci/known_ub.txt`. Anything not listed there fails the run.
The list currently holds eight misaligned 32-bit stores in `vdp_render.c`:
upstream writes the line buffer four pixels at a time, and fine horizontal
scroll makes the destination start at an odd offset. Harmless on x86, relevant
once macOS arm64 joins the matrix (#35). Entries are removed by fixing the
cause, not by widening the pattern.

## Comparing two platforms' replays (#38)

The Linux and Windows goldens still differ. `tests/ci/first_divergence.sh
<a.frames.jsonl> <b.frames.jsonl> [name-a] [name-b]` reports the **first frame
and subsystem** where two runs part ways, so the diagnosis starts from a
location instead of from an aggregate hash. Download the other platform's
`*.frames.jsonl` artifact and point the script at both.

## Coverage added for the previously untested surfaces (#33)

Three areas had no direct test and were only covered incidentally, if at all.

**Legacy memory ids** (`check_memory_regions` in `full_core_replay.c`). Starting
with `RETRO_MEMORY_VIDEO_RAM` — the delta this branch is named after — every id
is checked for size, non-NULL, and **pointer stability across `retro_reset` and
`retro_unserialize`**. A frontend caches those pointers once at load; if a reset
reallocated the buffers it would keep reading freed memory, and the symptom
would be intermittent graphical corruption far from the cause. The legacy VRAM
pointer is also compared against the ABI region, since they are two windows onto
the same memory.

**Savestate round trip** (`check_savestate_roundtrip`). `save → load → save` must
be byte-identical for the bytes the core writes; truncated buffers and corrupted
headers must be rejected rather than silently accepted. Note the measurement it
prints: **the core writes ~144 KB of the ~1,036 KB that `retro_serialize_size`
declares (14%)**. That is libretro semantics — the size is an upper bound — but
it means the tail of a saved file is whatever the frontend's buffer happened to
contain, so two savestates of the same state are not byte-equal on disk.

**A real ABI 1.0 client** (`tests/ci/abi_compat_1_0.c`). `test_ayther_api.c`
freezes offsets against the *current* header, which catches a reordering but
cannot answer whether an already-compiled frontend still works. This one does
not include `ayther_api.h` at all: it transcribes the 1.0 prefix with its own
types and uses it, the way a consumer from that era would.

`HOOK_CPU=1` also joined the Linux matrix. It had not been built in CI, and it
had been broken for a while: `cpuhook.h` *defined* `cpu_hook` instead of
declaring it, which only linked under `-fcommon` (the default up to gcc 9 /
clang 10).

## Video mode scenes (#35)

`make -C tests check-scenes CORE=...` runs eight ROMs, one per VDP
configuration, and compares a per-scene video and audio hash against
`ayther/golden/scenes.txt`.

The deterministic fixture covers Mode 5, H40, progressive, NTSC, no window and
no DMA — and nothing else. That is the gap these scenes close: #28 fixed the
render masks for interlace 2 and enhanced vscroll *without a fixture that
exercised either*, so "they work now" was a claim nobody could reproduce.

Each scene is a complete, static ROM rather than a segment inside one. One ROM
per scene avoids emitting a dispatcher in hand-written big-endian 68000, and it
gives the thing the scenes exist for: a hash **per mode**. An aggregate golden
says "something broke"; a per-scene golden says "interlace 2 broke".

Regenerate with `make -C tests regen-scenes CORE=...`. A golden that can only be
updated by editing it ends up edited without looking, which is how a golden
stops meaning anything.

The run ends with a distinctness count. A scene whose video hash equals
another's does not discriminate: the mode it claims to cover could break and the
golden would not notice. `interlace1` is currently such a scene — with
`config.render` off, interlace 1 emits the same frame as progressive — and the
warning keeps that visible instead of letting a useless scene sit there looking
like coverage.

The golden is **shared by Linux and Windows**, and all eight scenes match on
both. That is an independent confirmation of the #45 fix above: eight different
video modes, byte-identical across platforms.
## Plane tile suppression (#37.4)

`make -C tests check-plane-suppress CORE=...` is the first test to check that
region `0x105` actually *hides* something. The two that touched it before write
it to prove that writing it does not break determinism — a different claim.

The S/H fixture is the oracle, and it needs no reimplementation of the renderer:
planes A and B hold the same content and A covers B. So hiding the tile in A
must leave the **image identical** and move the attribution to plane B; hiding
it in B as well must leave the backdrop; clearing the mask must restore the
original frame exactly.

That middle state is also the case the per-plane gate introduced. Before #37.4,
`ayther_plane_suppress_active` was one flag for all three planes: hiding a tile
in A made B and Window lose the `DRAW_COLUMN` fast path and consult an entirely
empty mask, once per column and per line. Now each plane decides on its own,
and this test is what keeps a plane from being marked empty when it is not.

## The five recomposition layers (#37.6)

`make -C tests check-multilayer CORE=...` is the first test to look at what
`recompose_multilayer` **returns**. The function has existed since #12C; the
only test that called it asks for two layers and verifies something else — that
recomposing does not perturb emulation.

Two fixtures, because one is not enough. The S/H ROM has A and B with identical
content, so it exercises the shadow/highlight operator but cannot tell one plane
from the other; the `window` scene from #35 gives five distinct hashes, so
confusing two layers shows up.

Two of the assertions need no golden at all:

- Asking for one layer alone must return exactly what asking for all five
  returns. A frontend that draws its inspector layer by layer and one that asks
  for them at once cannot see different images.
- Where the attribution says "plane X won" and no sprite covers the pixel, layer
  X and the composite must agree. The one measured exception is the 64 pixels of
  the **brightness operator**: it carries no sprite bit — it is not a visible
  sprite, and counting it as one was the #31 bug — but it changes the intensity
  of what is underneath, and in the layer-alone render that sprite does not
  exist. In the scene without operators the difference is 0.

Regenerate with `make -C tests regen-multilayer CORE=...`.

The golden is what made #37.6 safe to attempt: plane B used to be drawn on
**every** pass and erased right before the merge, and three of the five passes
hide it. Removing that work had to leave all ten hashes untouched, and it did —
with the `bg_b_skipped` counter reading exactly `3 x 224` lines per multilayer
call, so the saving is counted rather than asserted.

## macOS in CI (#35.2)

The `macos-core` job is the only place the goldens run outside x86-64: GitHub's
macOS runners are arm64. Until it exists, "the golden is architecture
independent" is a supposition — the two goldens unified in #45.B were both
verified on x86-64 hosts.

It deliberately does not run the export closure: `check_exports.sh` reads
symbols the ELF/PE way, and Mach-O prefixes them with an underscore. A green
check that verifies nothing is worse than no check.
## Journal, frame hashes and palette (#39 A/D/E)

`make -C tests check-observability CORE=...` covers the three read-only regions
of ABI 1.7. Every assertion has an oracle that does not reimplement the core:

- **Journal.** The event *count* was already exposed through another path
  (`raster_event_count` in the frame delta). Both numbers come from the same
  source, so if they disagree one of the two is reading it wrong. And a journal
  is a diary: its lines cannot run backwards.
- **Frame hash.** The issue's own acceptance criterion — the region's
  `video_hash` must equal the hash computed *outside* over the frame that
  arrives through `video_refresh`. Same algorithm, same seed, two
  implementations.
- **Palette.** The table must **explain the frame**: every colour in the emitted
  image must appear in it. A stale, truncated or wrong-format table breaks that
  property, and asserting it needs none of the 9-bit conversion the region
  exists to avoid.

All three must answer `NOT_SUBSCRIBED` without a subscription.

The test uses two fixtures, and the reason is something it found. The palette is
the **current** table, not one per line: in the standard ROM the H-int handler
writes CRAM mid-frame, so the image carries colours from several tables and the
final one cannot explain them all — 8 of 11 distinct colours had no entry. That
is not a defect (the per-line case is `LINE_CRAM`), but it is a property that
has to be measured where it holds, so the palette check runs on the S/H fixture,
which has no H-int. The contract is now written down in `ayther_api.h` and in
`docs/ayther_abi_v1.md` instead of being folklore.
## LTO: measured, not adopted (#43.5)

The issue asked for a CI job that builds with `-flto` and compares, adopting it
if it improves by >= 3% without breaking goldens. Measured on Linux/gcc:

| | `.so` total | `.text` |
|---|---:|---:|
| without LTO | 12,917,992 | 1,211,647 |
| with LTO | 11,224,584 | 1,267,191 |

The total drops 13%, but that is almost entirely symbols and debug info, which
are never loaded. What actually runs -- `.text` -- **grows 4.6%**: LTO inlines
across translation units and i-cache pressure goes up. On speed there is no
honest verdict available from this harness: the bench carries +-3% noise and the
issue's threshold is exactly 3%, so any answer would be noise shaped like a
conclusion.

So it is **not adopted by default**. It stays as `AYTHER_LTO=1`, and the
`lto-build` job keeps it working -- an LTO build is where cross-unit UB shows up
-- and asserts the 14 fast/observed clones are still separate functions. If LTO
merged them, the pattern the whole "zero cost when idle" claim rests on would
quietly collapse.
