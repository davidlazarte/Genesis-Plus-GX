/***************************************************************************************
 *  Genesis Plus GX
 *  audio_probe - sound-trigger exposure surface (implementation)
 *
 *  See docs/audio_probe.md and audio_probe.h. Enabled by SOUND_PROBE; compiles
 *  to nothing otherwise.
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

#ifdef SOUND_PROBE

#include "shared.h"
#include "audio_probe.h"

/* registered consumer (Observer); when NULL events go to the ring buffer */
static ap_callback_t s_cb   = NULL;
static void         *s_user = NULL;

/* monotonic timeline */
static unsigned long long s_global_base = 0; /* global master cycles at frame start */
static unsigned int       s_frame       = 0; /* monotonic frame index               */
static unsigned int       s_cycles      = 0; /* current frame-relative timestamp     */

/* coincidence-window grouping */
static unsigned long long s_last_anchor_t = 0;
static int                s_have_anchor   = 0;
static unsigned int       s_group_seq     = 0;

/* per-channel gain (stored now; applied to the mixer in phase 2) */
static int s_gain[4][8];
static int s_gain_init = 0;

/* single-producer / single-consumer event ring buffer */
static ap_event_t   s_ring[AUDIO_PROBE_RING_SIZE];
static unsigned int s_head = 0; /* producer (emulator) */
static unsigned int s_tail = 0; /* consumer (tool)     */

static void ap_init_gain(void)
{
  int s, c;
  for (s = 0; s < 4; s++)
    for (c = 0; c < 8; c++)
      s_gain[s][c] = 100;
  s_gain_init = 1;
}

static unsigned long long ap_now(void)
{
  return s_global_base + (unsigned long long)s_cycles;
}

static unsigned int ap_group(unsigned long long t)
{
  if (!s_have_anchor || (t - s_last_anchor_t) > AUDIO_PROBE_COINCIDENCE_WINDOW)
    s_group_seq++;
  s_last_anchor_t = t;
  s_have_anchor   = 1;
  return s_group_seq;
}

/* FNV-1a 64-bit */
static unsigned long long ap_fnv(unsigned long long h, const void *p, int n)
{
  const unsigned char *b = (const unsigned char *)p;
  int i;
  for (i = 0; i < n; i++)
  {
    h ^= b[i];
    h *= 1099511628211ULL;
  }
  return h;
}

/* canonical, channel-independent fingerprints */
static void ap_hash_voice(const ap_voice_t *v,
                          unsigned long long *timbre,
                          unsigned long long *voice)
{
  unsigned long long h = 14695981039346656037ULL; /* FNV offset basis */
  unsigned int tone;

  h = ap_fnv(h, v->op_tl,  4);
  h = ap_fnv(h, v->op_ar,  4);
  h = ap_fnv(h, v->op_dr,  4);
  h = ap_fnv(h, v->op_sr,  4);
  h = ap_fnv(h, v->op_rr,  4);
  h = ap_fnv(h, v->op_mul, 4);
  h = ap_fnv(h, v->op_dt,  4);
  h = ap_fnv(h, &v->algorithm, 1);
  h = ap_fnv(h, &v->feedback,  1);
  h = ap_fnv(h, &v->ams, 1);
  h = ap_fnv(h, &v->fms, 1);
  *timbre = h;

  /* fold in a coarse tone class (block + top fnum bits), still channel-free */
  tone = (v->block_fnum >> 7) & 0x0f;
  h = ap_fnv(h, &tone, sizeof(tone));
  *voice = h;
}

static void ap_emit(ap_event_t *ev)
{
  unsigned int next;

  ev->schema   = AUDIO_PROBE_SCHEMA;
  ev->t_global = ap_now();
  ev->t_frame  = s_frame;
  ev->t_cycles = s_cycles;
  ev->group    = ap_group(ev->t_global);

  if (s_cb)
  {
    s_cb(ev, s_user);
    return;
  }

  /* push into ring buffer; drop when full (consumer fell behind) */
  next = (s_head + 1) & (AUDIO_PROBE_RING_SIZE - 1);
  if (next != s_tail)
  {
    s_ring[s_head] = *ev;
    s_head = next;
  }
}

/* ===================== consumer-facing API ===================== */

void audio_probe_set_callback(ap_callback_t cb, void *user)
{
  s_cb   = cb;
  s_user = user;
}

int audio_probe_poll(ap_event_t *out, int max)
{
  int n = 0;
  if (!out) return 0;
  while (n < max && s_tail != s_head)
  {
    out[n++] = s_ring[s_tail];
    s_tail = (s_tail + 1) & (AUDIO_PROBE_RING_SIZE - 1);
  }
  return n;
}

void audio_probe_get_context(ap_context_t *out)
{
  if (!out) return;
  out->rom_crc          = (unsigned int)rominfo.realchecksum;
  out->region           = vdp_pal ? 1 : 0;
  out->system_hw        = (unsigned int)system_hw;
  out->master_clock     = (unsigned int)system_clock;
  out->cycles_per_frame = (unsigned int)(MCYCLES_PER_LINE * lines_per_frame);
}

void audio_probe_set_channel_gain(ap_source_t src, int ch, int gain_percent)
{
  if (!s_gain_init) ap_init_gain();
  if ((int)src < 0 || (int)src > 3 || ch < 0 || ch > 7) return;
  if (gain_percent < 0) gain_percent = 0;
  s_gain[src][ch] = gain_percent;
}

int audio_probe_get_channel_gain(ap_source_t src, int ch)
{
  if (!s_gain_init) ap_init_gain();
  if ((int)src < 0 || (int)src > 3 || ch < 0 || ch > 7) return 100;
  return s_gain[src][ch];
}

/* ===================== core-internal emit API ===================== */

void audio_probe_reset(void)
{
  s_global_base   = 0;
  s_frame         = 0;
  s_cycles        = 0;
  s_last_anchor_t = 0;
  s_have_anchor   = 0;
  s_group_seq     = 0;
  s_head          = 0;
  s_tail          = 0;
  if (!s_gain_init) ap_init_gain();
}

void audio_probe_set_time(unsigned int cycles)
{
  s_cycles = cycles;
}

void audio_probe_frame(unsigned int frame_cycles)
{
  ap_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_FM;
  ev.type    = AP_EV_FRAME;
  ev.channel = 0xff;
  s_cycles   = frame_cycles;
  ap_emit(&ev);

  /* roll the monotonic timeline forward to the next frame */
  s_global_base += (unsigned long long)frame_cycles;
  s_frame++;
  s_cycles = 0;
}

void audio_probe_signal(ap_event_type_t type)
{
  ap_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_FM;
  ev.type    = (unsigned char)type;
  ev.channel = 0xff;
  ap_emit(&ev);
}

void audio_probe_fm_raw(unsigned int reg, unsigned int data)
{
  ap_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_FM;
  ev.type    = AP_EV_RAW_WRITE;
  ev.channel = 0xff;
  ev.reg     = reg;
  ev.data    = data;
  ap_emit(&ev);
}

void audio_probe_fm_key(int ch, unsigned int slot_mask)
{
  ap_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_FM;
  ev.channel = (unsigned char)ch;
  ev.data    = slot_mask;

  if (slot_mask & 0xf0)
  {
    ev.type = AP_EV_NOTE_ON;
    YM2612_GetVoice(ch, &ev.voice);
    ap_hash_voice(&ev.voice, &ev.timbre_hash, &ev.voice_hash);
  }
  else
  {
    ev.type = AP_EV_NOTE_OFF;
  }
  ap_emit(&ev);
}

void audio_probe_fm_dac(int enabled)
{
  ap_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_DAC;
  ev.channel = 5; /* YM2612 channel 6 */
  ev.type    = enabled ? AP_EV_DAC_START : AP_EV_DAC_STOP;
  ap_emit(&ev);
}

void audio_probe_psg_raw(unsigned int clocks, unsigned int data)
{
  ap_event_t ev;
  s_cycles = clocks;
  memset(&ev, 0, sizeof(ev));
  ev.source  = AP_SRC_PSG;
  ev.type    = AP_EV_RAW_WRITE;
  ev.channel = 0xff;
  ev.data    = data;
  ap_emit(&ev);
}

#endif /* SOUND_PROBE */
