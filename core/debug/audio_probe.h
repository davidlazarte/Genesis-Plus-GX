/***************************************************************************************
 *  Genesis Plus GX
 *  audio_probe - sound-trigger exposure surface
 *
 *  Read-only audio event stream + resolved per-channel voice snapshots +
 *  monotonic timeline/context + per-channel gain, for an external tool that
 *  identifies sound-effect starts and their components (channel-independently),
 *  sequences the groups that follow, and substitutes audio.
 *
 *  SOUND_PROBE must be defined in a makefile/project to enable this. When it is
 *  not defined this module compiles to nothing and has zero cost (same pattern
 *  as HOOK_CPU). See docs/audio_probe.md for the full design.
 *
 *  Copyright (C) 2026  Genesis Plus GX contributors
 *
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************/

#ifndef _AUDIO_PROBE_H_
#define _AUDIO_PROBE_H_

#ifdef SOUND_PROBE

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PROBE_SCHEMA 1

/* internal event ring buffer size (must be a power of two) */
#ifndef AUDIO_PROBE_RING_SIZE
#define AUDIO_PROBE_RING_SIZE 8192
#endif

/* events whose global timestamps differ by less than this many master cycles
   are considered part of the same logical trigger (coincidence window) */
#ifndef AUDIO_PROBE_COINCIDENCE_WINDOW
#define AUDIO_PROBE_COINCIDENCE_WINDOW 1024
#endif

typedef enum {
  AP_SRC_FM  = 0,   /* YM2612 (MAME OPN2 path)        */
  AP_SRC_PSG = 1,   /* SN76489                        */
  AP_SRC_DAC = 2,   /* YM2612 channel 6 DAC           */
  AP_SRC_PCM = 3    /* Sega CD RF5C164 PCM (phase 3)  */
} ap_source_t;

typedef enum {
  AP_EV_RAW_WRITE = 0, /* raw register write (reg,data)        */
  AP_EV_NOTE_ON,       /* key-on  (voice + hashes valid)       */
  AP_EV_NOTE_OFF,      /* key-off                              */
  AP_EV_DAC_START,     /* DAC enabled                          */
  AP_EV_DAC_STOP,      /* DAC disabled                         */
  AP_EV_PATCH,         /* instrument changed       (phase 3)   */
  AP_EV_PITCH,         /* frequency changed        (phase 3)   */
  AP_EV_VOLUME,        /* level/attenuation change (phase 3)   */
  AP_EV_RESET,         /* chip reset       -> resync           */
  AP_EV_STATE_LOAD,    /* savestate loaded -> resync           */
  AP_EV_FRAME          /* end-of-frame marker (timeline)       */
} ap_event_type_t;

/* Resolved voice state, channel-independent. FM uses all 4 operators (in
   hardware register order); PSG uses index [0]. */
typedef struct {
  unsigned char op_tl[4], op_ar[4], op_dr[4], op_sr[4], op_rr[4], op_mul[4], op_dt[4];
  unsigned char algorithm, feedback, ams, fms, pan;
  unsigned int  block_fnum;   /* FM blk/fnum, or PSG tone period */
} ap_voice_t;

typedef struct {
  unsigned long long t_global;    /* monotonic master-cycle timestamp     */
  unsigned int  t_frame;          /* frame index                          */
  unsigned int  t_cycles;         /* cycles within the frame              */
  unsigned char source;           /* ap_source_t                          */
  unsigned char type;             /* ap_event_type_t                      */
  unsigned char channel;          /* physical channel (0xff = n/a)        */
  unsigned char schema;           /* AUDIO_PROBE_SCHEMA                    */
  unsigned int  group;            /* coincidence id (logical trigger)     */
  unsigned int  reg;              /* raw register/addr (AP_EV_RAW_WRITE)  */
  unsigned int  data;             /* raw data / key-on slot mask          */
  ap_voice_t    voice;            /* valid for AP_EV_NOTE_ON               */
  unsigned long long voice_hash;  /* canonical fingerprint (timbre+tone)  */
  unsigned long long timbre_hash; /* canonical fingerprint (timbre only)  */
} ap_event_t;

typedef struct {
  unsigned int  rom_crc;          /* ROM checksum (identity)              */
  unsigned char region;           /* 0 = NTSC, 1 = PAL                    */
  unsigned int  system_hw;        /* SYSTEM_* hardware id                 */
  unsigned int  master_clock;     /* master clock in Hz                   */
  unsigned int  cycles_per_frame; /* master cycles per video frame        */
} ap_context_t;

/* ---------------- consumer-facing API (external tool) ---------------- */

/* Register a callback invoked inline for every event. When no callback is set,
   events are buffered and must be drained with audio_probe_poll(). */
typedef void (*ap_callback_t)(const ap_event_t *ev, void *user);
void audio_probe_set_callback(ap_callback_t cb, void *user);

/* Drain up to 'max' buffered events into 'out'; returns the number copied. */
int  audio_probe_poll(ap_event_t *out, int max);

/* Fill the current emulation context/identity. */
void audio_probe_get_context(ap_context_t *out);

/* Per-channel output gain in percent (0 = mute, 100 = unchanged), applied to
   the audio mixer. FM channels 0-5 use AP_SRC_FM; while DAC mode is active,
   channel 6 uses AP_SRC_DAC. PSG channels 0-3 use AP_SRC_PSG. */
void audio_probe_set_channel_gain(ap_source_t src, int ch, int gain_percent);
int  audio_probe_get_channel_gain(ap_source_t src, int ch);

/* ---------------- core-internal emit API (chips/system) ---------------- */

void audio_probe_reset(void);
void audio_probe_set_time(unsigned int cycles);
void audio_probe_frame(unsigned int frame_cycles);
void audio_probe_signal(ap_event_type_t type);
void audio_probe_fm_raw(unsigned int reg, unsigned int data);
void audio_probe_fm_key(int ch, unsigned int slot_mask);
void audio_probe_fm_dac(int enabled);
void audio_probe_psg_raw(unsigned int clocks, unsigned int data);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_PROBE */
#endif /* _AUDIO_PROBE_H_ */
