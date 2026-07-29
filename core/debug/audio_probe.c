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

/* raw FM register shadow: bank 0 = CH1-3, bank 1 = CH4-6. Updated on every FM
   write (from either the MAME or Nuked core), so a note's resolved voice can
   be decoded regardless of when the driver programmed the patch. */
static unsigned char s_fm_regs[2][256];

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

/* Decode the resolved voice of FM channel 'ch' (0-5) from the register shadow.
   Operators are reported in hardware register order; values match what the
   driver programmed (TL/AR/DR/SR/RR/MUL/DT, algorithm, feedback, LFO
   sensitivity, blk/fnum), so a fingerprint is independent of channel. */
static void ap_decode_fm_voice(int ch, ap_voice_t *out)
{
  int bank, cn, op;
  unsigned char fb_algo, lr;

  memset(out, 0, sizeof(*out));
  if (ch < 0 || ch > 5) return;

  bank = (ch >= 3) ? 1 : 0;
  cn   = ch % 3;

  for (op = 0; op < 4; op++)
  {
    int off = (op << 2) + cn; /* register operator slots: S1,S3,S2,S4 */
    out->op_mul[op] =  s_fm_regs[bank][0x30 + off] & 0x0f;
    out->op_dt[op]  = (s_fm_regs[bank][0x30 + off] >> 4) & 0x07;
    out->op_tl[op]  =  s_fm_regs[bank][0x40 + off] & 0x7f;
    out->op_ar[op]  =  s_fm_regs[bank][0x50 + off] & 0x1f;
    out->op_dr[op]  =  s_fm_regs[bank][0x60 + off] & 0x1f;
    out->op_sr[op]  =  s_fm_regs[bank][0x70 + off] & 0x1f;
    out->op_rr[op]  =  s_fm_regs[bank][0x80 + off] & 0x0f;
  }

  fb_algo = s_fm_regs[bank][0xb0 + cn];
  out->algorithm = fb_algo & 0x07;
  out->feedback  = (fb_algo >> 3) & 0x07;

  lr = s_fm_regs[bank][0xb4 + cn];
  out->pan = (lr >> 6) & 0x03;
  out->ams = (lr >> 4) & 0x03;
  out->fms =  lr & 0x07;

  out->block_fnum = ((s_fm_regs[bank][0xa4 + cn] & 0x3f) << 8)
                  |   s_fm_regs[bank][0xa0 + cn];
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

  /* PSG amplitude is baked into chanAmp; re-apply so the change is heard now.
     FM/DAC gain is read per-sample, so no refresh is needed there. */
  if (src == AP_SRC_PSG)
    psg_refresh_gain();
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
  memset(s_fm_regs, 0, sizeof(s_fm_regs));
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

  /* keep the register shadow up to date for voice decoding */
  s_fm_regs[(reg >> 8) & 1][reg & 0xff] = (unsigned char)data;

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
    ap_decode_fm_voice(ch, &ev.voice);
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
