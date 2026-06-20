# `audio_probe` — Sound-trigger exposure surface

## Purpose

`audio_probe` is a **read-only observation + output-control surface** that lets an
external tool (an "audio lab" / authoring tool and its runtime) do the following
against a running Genesis Plus GX core:

1. **Capture** every meaningful audio event (FM, PSG, DAC, CD-PCM) in order, with a
   deterministic timestamp.
2. **Identify** the *start* of a sound effect and the **components** that make it up,
   independently of which hardware channel the driver happened to use.
3. **Sequence** the groups of events that follow a start and keep an assigned audio
   playing *for as long as the sequence is respected*.
4. **Substitute** audio by muting/attenuating the specific emulated channels that an
   external HQ track replaces.

**Scope boundary:** GPGX exposes *facts* (events + resolved state + a monotonic
timeline) and *levers* (per-channel gain). It does **not** match, sequence, decide,
or store definitions. All of that intelligence lives in the external tool. The
authoring workflow itself is explicitly out of scope for the core.

Everything here is gated behind the `SOUND_PROBE` build flag and is **zero-cost when
disabled** (same pattern as `HOOK_CPU`).

---

## Why "resolved state", not raw writes

Mega Drive sound drivers **cache instruments**: the patch is written once when the
song loads, and every subsequent note is just *frequency + key-on*. A fingerprint
built from the register writes *near* the key-on therefore captures the patch the
first time and misses it afterwards, producing false negatives.

The YM2612 emulation already keeps the fully **decoded** per-channel / per-operator
state in `ym2612.CH[6]` (`core/sound/ym2612.c`). The probe snapshots that resolved
state at each anchor (key-on), so a "component" is *the effective voice at the moment
it is triggered* — independent of when the patch was last written.

This single decision (snapshot-on-anchor) is what makes the rest robust.

---

## Design patterns

| Pattern | Role |
|---|---|
| **Facade** (`audio_probe.{c,h}`) | One module centralizes hooks, schema, gating. Chips only *report*; the core never knows the tool. |
| **Snapshot / Memento on anchor** | Resolved voice state captured at key-on, not reconstructed from deltas. Survives cached patches. |
| **Canonical Value Object** (voice descriptor + hash) | Makes a trigger **channel- and time-invariant**. Searching for "a" becomes a hash lookup, not a register address match. |
| **Coincidence-window grouping** | Simultaneous writes (same cycle ± window) get a shared `group` id → one logical trigger spanning multiple channels. Enables "a/b/c mark a start regardless of channel". |
| **Anchor taxonomy** (typed events) | The tool searches over typed anchors (NOTE_ON, DAC_START…), not a raw byte torrent. |
| **Differential emission / dirty-flag** | Emit only on meaningful state change → less noise, fewer false triggers. |
| **Monotonic timeline** (clock-domain normalization) | Global monotonic timestamp → seek, measure Δt, normalize PAL/NTSC and lag frames. |
| **Producer/Consumer ring buffer** | Decouples emulator thread from tool thread; non-blocking emission, plus an optional direct callback. |
| **Schema versioning** | Every event carries `schema` → core and tool evolve without breaking. |

The decisive pair is **Snapshot-on-anchor + Canonical hash**: together they turn
"what sound started and what is it made of?" into a deterministic, channel-independent
query.

---

## Trigger flow

```
68000/Z80 writes  ->  YM2612_Write (sound.c, has 'cycles')
   |  audio_probe_set_time(cycles)            // memento of current time
   v
YM2612Write -> case 0x28 (ym2612.c)           // channel 'c' already decoded
   |  audio_probe_note_on(c, keymask)
   |     |- snapshot YM2612_GetVoice(c)        // resolved state
   |     |- voice_hash = canon(voice)          // channel-independent
   |     '- coincidence_group(t)               // group with same-instant anchors
   v
ap_event_t  ->  ring buffer / callback  ->  external tool
```

---

## API contract

### `core/debug/audio_probe.h`

```c
#define AUDIO_PROBE_SCHEMA 1

typedef enum { AP_SRC_FM=0, AP_SRC_PSG=1, AP_SRC_DAC=2, AP_SRC_PCM=3 } ap_source_t;

typedef enum {
  AP_EV_RAW_WRITE=0,  /* raw register write (reg,data)        */
  AP_EV_NOTE_ON,      /* key-on  (voice + voice_hash valid)   */
  AP_EV_NOTE_OFF,     /* key-off                              */
  AP_EV_DAC_START,    /* DAC enabled / first sample           */
  AP_EV_DAC_STOP,
  AP_EV_PATCH,        /* instrument changed                   */
  AP_EV_PITCH,        /* frequency changed                    */
  AP_EV_VOLUME,       /* level / attenuation changed          */
  AP_EV_RESET,        /* chip reset    -> resync              */
  AP_EV_STATE_LOAD,   /* savestate loaded -> resync           */
  AP_EV_FRAME         /* end-of-frame marker (timeline)       */
} ap_event_type_t;

/* Resolved voice state (channel-independent). FM uses 4 ops; PSG uses [0]. */
typedef struct {
  unsigned char op_tl[4], op_ar[4], op_dr[4], op_sr[4], op_rr[4], op_mul[4], op_dt[4];
  unsigned char algorithm, feedback, ams, fms, pan;
  unsigned int  block_fnum;   /* pitch (FM) / period (PSG) */
} ap_voice_t;

typedef struct {
  unsigned long long t_global;   /* monotonic master cycles        */
  unsigned int  t_frame;         /* frame index                    */
  unsigned int  t_cycles;        /* cycles within frame            */
  unsigned char source;          /* ap_source_t                    */
  unsigned char type;            /* ap_event_type_t                */
  unsigned char channel;         /* physical channel (0-5 FM,0-3 PSG) */
  unsigned char schema;          /* AUDIO_PROBE_SCHEMA             */
  unsigned int  group;           /* coincidence id (trigger)       */
  unsigned int  reg, data;       /* raw (AP_EV_RAW_WRITE)          */
  ap_voice_t    voice;           /* valid for NOTE_ON/PATCH/PITCH  */
  unsigned long long voice_hash; /* canonical, channel-stripped    */
  unsigned long long timbre_hash;/* patch only (no pitch)          */
} ap_event_t;

/* Consumer registration (Observer) or pull (ring buffer). */
typedef void (*ap_callback_t)(const ap_event_t *ev, void *user);
void audio_probe_set_callback(ap_callback_t cb, void *user);
int  audio_probe_poll(ap_event_t *out, int max);   /* drains up to N, returns count */

/* Context / identity / time base. */
typedef struct {
  unsigned int  rom_crc;
  unsigned char region;          /* 0=NTSC 1=PAL */
  unsigned int  system_hw;
  unsigned int  master_clock;    /* Hz   */
  unsigned int  cycles_per_frame;
} ap_context_t;
void audio_probe_get_context(ap_context_t *out);

/* Substitution lever: per-channel gain/mute (0..100, 0 = mute). */
void audio_probe_set_channel_gain(ap_source_t src, int ch, int gain_percent);
```

### Resolved-state accessors added to the chips

```c
/* ym2612.h */  void YM2612_GetVoice(int ch, ap_voice_t *out);
                int  YM2612_GetKeyState(int ch);   /* bitmask of keyed slots */
/* psg.h   */  void PSG_GetVoice(int ch, ap_voice_t *out);
```

### Canonical fingerprint (in `audio_probe.c`)

- `timbre_hash` — operator params + algorithm + feedback only (no channel, no pitch).
- `voice_hash` — `timbre_hash` combined with the tone class (`block_fnum` quantized
  to a semitone).

Two hashes on purpose: the tool decides whether two notes of the same instrument at
different pitches are "the same trigger" (use `timbre_hash`) or not (use `voice_hash`).

---

## Implementation plan

### New files
- `core/debug/audio_probe.h` — the contract above.
- `core/debug/audio_probe.c` — callback/ring buffer, classification, hashing,
  coincidence-window grouping, per-channel gain table.

### Edits (all under `#ifdef SOUND_PROBE`, guarded by `if (UNLIKELY(ap_cb))`)

**`core/sound/sound.c`**
- `YM2612_Write` (~`:114`): `audio_probe_set_time(cycles)` + emit `AP_EV_RAW_WRITE`.
- `sound_update` (~`:445`): emit `AP_EV_FRAME`, advance the global monotonic clock.
- `*_Reset`: emit `AP_EV_RESET`.
- Apply per-channel gain before mixing (via FM `pan[]` and PSG `chanAmp[][]`, which
  already exist).

**`core/sound/ym2612.c` (+ `.h`)**
- `case 0x28` (~`:1555`): after `c` is resolved, call `audio_probe_note_on/off`.
- DAC `case 0x2a/0x2b` (~`:1979`): emit `AP_EV_DAC_START/STOP`.
- Add `YM2612_GetVoice` / `YM2612_GetKeyState` (expose today's `static` struct).

**`core/sound/psg.c` (+ `.h`)**
- `psg_write` (`psg.h:56`): emit event; classify volume->0 as NOTE_OFF.
- Add `PSG_GetVoice`; gain via existing `chanAmp[4][2]`.

**`core/system.c` / `system.h`**
- Monotonic frame counter + `audio_probe_get_context` (uses `system_clock`,
  `mcycles_vdp`, ROM CRC, region).
- Emit `AP_EV_STATE_LOAD` on the savestate load path.

**`core/cd_hw/pcm.c`** — *Phase 3*: Sega CD PCM events.

### Build / gating
- `Makefile.libretro`: add `SOUND_PROBE = 0` (next to `HOOK_CPU`).
- `libretro/Makefile.common`: `ifeq ($(SOUND_PROBE),1) FLAGS += -DSOUND_PROBE`.
- `sdl/Makefile.sdl1|2`: document the flag.
- *Phase 3*: libretro core-option + `environment` callback to register the consumer.

### Phases
1. **Phase 1 (core) — done:** facade module + FM/PSG tap (RAW + NOTE_ON/OFF) +
   voice accessors + timeline + context + reset/state-load signals + build flag.
   Enough to capture and identify triggers.
2. **Phase 2 (substitution) — done:** per-channel gain wired into mixing.
   - FM (and DAC on channel 6) gain is applied per sample in `YM2612Update`,
     alongside the existing `md_ch_volumes` scaling.
   - PSG gain is folded into `chanAmp` in `psg_config`; `audio_probe_set_channel_gain`
     calls `psg_refresh_gain()` so a change is heard immediately (propagated
     through the same delta path).
3. **Phase 3 (extension) — partial:**
   - **Nuked OPN2 (YM3438) parity — done:** the alternative high-accuracy FM
     core funnels through `YM3438_Write`, which does not expose the internal
     address latch, so `sound.c` mirrors the bus protocol (address ports 0/2,
     data ports 1/3) to recover the register address and emits RAW / NOTE_ON /
     NOTE_OFF / DAC just like the MAME path. The FM register shadow and voice
     decoder now live in `audio_probe.c`, so both cores share one fingerprint
     path.
   - **libretro exposure — done:** the `audio_probe_*` symbols are added to the
     version script (`libretro/link.T`), so a frontend / harness that `dlopen`s
     the core can resolve the consumer API (`audio_probe_set_callback`,
     `audio_probe_poll`, `audio_probe_get_context`, `audio_probe_set_channel_gain`).
     They are only present when `SOUND_PROBE` is built in.
   - **Deferred:** Sega CD PCM (RF5C164) events — the PCM chip runs on the
     Sub-CPU clock domain, so its events need a dedicated timestamp/correlation
     design rather than the main FM/PSG timeline; PSG note-on/off classification
     from attenuation transitions; hash tuning; a dedicated libretro
     `GET_PROC_ADDRESS` interface (the version-script export covers `dlopen`
     consumers today).

### Non-regression guarantee
With `SOUND_PROBE` disabled (default), there is **no behavioral change and zero
cost** (same pattern as `HOOK_CPU`). All matching/sequencing lives outside the core;
GPGX only exposes facts and levers.

---

## Consumer integration

### Building

The probe is opt-in. Build the libretro core with the flag enabled:

```sh
make -f Makefile.libretro platform=unix SOUND_PROBE=1
```

When `SOUND_PROBE=1`, the `audio_probe_*` symbols are exported from the shared
library (via `libretro/link.T`); when it is off, none are present and the code
compiles to nothing.

### Attaching a consumer

There are two ways to receive events. Pick one:

- **Callback (push):** register a function called inline for every event.
- **Poll (pull):** leave the callback unset and drain the ring buffer
  periodically (e.g. once per frame). This decouples the emulator thread from
  the tool thread.

A frontend or harness that `dlopen`s the core resolves the consumer API by name:

```c
#include "audio_probe.h"   /* from core/debug */

/* resolved via dlsym() on the loaded core */
void (*set_cb)(ap_callback_t, void *);
int  (*poll)(ap_event_t *, int);
void (*get_ctx)(ap_context_t *);
void (*set_gain)(ap_source_t, int, int);

set_cb  = dlsym(core, "audio_probe_set_callback");
poll    = dlsym(core, "audio_probe_poll");
get_ctx = dlsym(core, "audio_probe_get_context");
set_gain= dlsym(core, "audio_probe_set_channel_gain");
```

### Minimal poll loop

```c
ap_event_t evs[512];
for (;;) {
  /* ... run one frame of the core ... */
  int n = poll(evs, 512);
  for (int i = 0; i < n; i++) {
    switch (evs[i].type) {
      case AP_EV_NOTE_ON:
        /* evs[i].voice_hash identifies the sound, regardless of channel;
           evs[i].group ties simultaneous cross-channel anchors together */
        on_trigger(evs[i].voice_hash, evs[i].group, evs[i].t_global);
        break;
      case AP_EV_NOTE_OFF:
      case AP_EV_DAC_STOP:
        on_release(evs[i].channel);
        break;
      case AP_EV_RESET:
      case AP_EV_STATE_LOAD:
        resync();            /* discard in-flight hypotheses */
        break;
      default: break;
    }
  }
}
```

### Substituting audio

To replace a sound with an HQ track, attenuate the channels it occupies and
play your own stream:

```c
set_gain(AP_SRC_FM, 5, 0);    /* mute FM channel 6 */
set_gain(AP_SRC_DAC, 5, 0);   /* mute the DAC voice on channel 6 */
/* ... restore with gain 100 when the substituted sound ends ... */
```

FM/DAC gain takes effect on the next sample; PSG gain is applied immediately
through `psg_refresh_gain()`.

---

## Testing

Unit tests for the probe logic live in [`tests/`](../tests/) and build the
module in isolation (no full emulator build):

```sh
cd tests
make check
```

They cover event round-trips, the monotonic timeline, FM voice decoding,
**channel-independent fingerprints**, note/DAC/PSG events, per-channel gain,
coincidence grouping, ring-buffer overflow, the callback path, and the context
accessor. See [`tests/README.md`](../tests/README.md).
