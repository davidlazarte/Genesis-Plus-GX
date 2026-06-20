/* Unit tests for core/debug/audio_probe.c
 *
 * Built standalone against tests/stub/shared.h (see tests/Makefile), so it
 * exercises the probe logic without compiling the whole emulator.
 *
 * Covers: reset/empty, raw-write events, timeline/frame advance, FM voice
 * decoding from the register shadow, channel-independent fingerprints,
 * note on/off, DAC start/stop, PSG raw, per-channel gain (set/get/clamp),
 * coincidence-window grouping, ring-buffer overflow, the callback path,
 * and the context accessor.
 */

#include <stdio.h>
#include "shared.h"   /* stub: types, ROMINFO, externs, MCYCLES_PER_LINE, audio_probe.h */

/* ---- definitions for the globals the stub declares ---- */
ROMINFO        rominfo;
uint8          vdp_pal;
uint8          system_hw;
uint32         system_clock;
uint16         lines_per_frame;
void psg_refresh_gain(void) { /* no-op in tests */ }

/* ---- tiny assert framework ---- */
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", (msg), __LINE__); } \
  } while (0)

/* ---- helpers ---- */

/* drain every buffered event into a scratch array */
static int drain(ap_event_t *out, int max)
{
  return audio_probe_poll(out, max);
}

/* find the most recent NOTE_ON among buffered events */
static int find_note_on(ap_event_t *out)
{
  static ap_event_t buf[AUDIO_PROBE_RING_SIZE];
  int n = audio_probe_poll(buf, AUDIO_PROBE_RING_SIZE);
  int i;
  for (i = n - 1; i >= 0; i--)
  {
    if (buf[i].type == AP_EV_NOTE_ON) { *out = buf[i]; return 1; }
  }
  return 0;
}

/* program a fixed, non-trivial FM voice on a given channel (0-5) via raw
   register writes, mirroring how a driver caches a patch before key-on */
static void program_voice(int ch)
{
  int base = (ch >= 3) ? 0x100 : 0;
  int cn   = ch % 3;
  int op;
  for (op = 0; op < 4; op++)
  {
    int off = (op << 2) + cn;
    audio_probe_fm_raw(base + 0x30 + off, 0x35);        /* DT=3, MUL=5     */
    audio_probe_fm_raw(base + 0x40 + off, 0x12 + op);   /* TL varies/op    */
    audio_probe_fm_raw(base + 0x50 + off, 0x1f);        /* AR=0x1f         */
    audio_probe_fm_raw(base + 0x60 + off, 0x0a);        /* DR=0x0a         */
    audio_probe_fm_raw(base + 0x70 + off, 0x07);        /* SR=0x07         */
    audio_probe_fm_raw(base + 0x80 + off, 0xf9);        /* SL=0xf, RR=9    */
  }
  audio_probe_fm_raw(base + 0xb0 + cn, 0x3a);           /* ALGO=2, FB=7    */
  audio_probe_fm_raw(base + 0xb4 + cn, 0xc5);           /* pan=3,ams=0,fms=5 */
  audio_probe_fm_raw(base + 0xa4 + cn, 0x1a);           /* fnum hi / block */
  audio_probe_fm_raw(base + 0xa0 + cn, 0x34);           /* fnum lo         */
}

/* ---- callback-path capture ---- */
static ap_event_t g_cb_last;
static int        g_cb_count;
static void cb_capture(const ap_event_t *ev, void *user)
{
  (void)user;
  g_cb_last = *ev;
  g_cb_count++;
}

int main(void)
{
  ap_event_t ev, evb;
  ap_event_t scratch[8];
  ap_context_t ctx;
  int n;

  /* 1. reset clears the buffer */
  audio_probe_reset();
  CHECK(drain(scratch, 8) == 0, "buffer empty after reset");

  /* 2. raw FM write round-trips with the right fields */
  audio_probe_set_time(1234);
  audio_probe_fm_raw(0x40, 0x7f);
  n = drain(scratch, 8);
  CHECK(n == 1, "one event after one raw write");
  CHECK(scratch[0].source == AP_SRC_FM, "raw source is FM");
  CHECK(scratch[0].type == AP_EV_RAW_WRITE, "raw type");
  CHECK(scratch[0].reg == 0x40 && scratch[0].data == 0x7f, "raw reg/data");
  CHECK(scratch[0].schema == AUDIO_PROBE_SCHEMA, "schema stamped");
  CHECK(scratch[0].t_cycles == 1234, "timestamp from set_time");

  /* 3. frame marker advances the monotonic timeline */
  audio_probe_reset();
  audio_probe_set_time(10);
  audio_probe_fm_raw(0x21, 0);          /* frame 0 */
  audio_probe_frame(3420);              /* end of frame 0 */
  audio_probe_set_time(20);
  audio_probe_fm_raw(0x21, 0);          /* frame 1 */
  n = drain(scratch, 8);
  CHECK(n == 3, "raw + frame + raw");
  CHECK(scratch[0].t_frame == 0, "first write in frame 0");
  CHECK(scratch[1].type == AP_EV_FRAME, "frame marker emitted");
  CHECK(scratch[2].t_frame == 1, "second write in frame 1");
  CHECK(scratch[2].t_global == 3420 + 20, "global time accumulates across frames");

  /* 4. FM voice decodes from the register shadow on key-on */
  audio_probe_reset();
  program_voice(0);
  audio_probe_fm_key(0, 0xf0);          /* all slots on */
  CHECK(find_note_on(&ev) == 1, "NOTE_ON emitted on key-on");
  CHECK(ev.channel == 0, "note channel");
  CHECK(ev.voice.op_mul[0] == 5 && ev.voice.op_dt[0] == 3, "decoded MUL/DT");
  CHECK(ev.voice.op_tl[0] == 0x12 && ev.voice.op_tl[1] == 0x13, "decoded TL per op");
  CHECK(ev.voice.op_ar[0] == 0x1f && ev.voice.op_rr[0] == 9, "decoded AR/RR");
  CHECK(ev.voice.algorithm == 2 && ev.voice.feedback == 7, "decoded ALGO/FB");
  CHECK(ev.voice.pan == 3 && ev.voice.fms == 5, "decoded pan/fms");
  CHECK(ev.voice.block_fnum == 0x1a34, "decoded block_fnum");
  CHECK(ev.voice_hash != 0 && ev.timbre_hash != 0, "hashes populated");

  /* 5. fingerprint is channel-independent (same voice, different channel) */
  audio_probe_reset();
  program_voice(0);
  audio_probe_fm_key(0, 0xf0);
  CHECK(find_note_on(&ev) == 1, "note on ch0");
  audio_probe_reset();
  program_voice(3);                     /* bank 1, same cn */
  audio_probe_fm_key(3, 0xf0);
  CHECK(find_note_on(&evb) == 1, "note on ch3");
  CHECK(ev.voice_hash == evb.voice_hash, "voice_hash channel-independent");
  CHECK(ev.timbre_hash == evb.timbre_hash, "timbre_hash channel-independent");

  /* 6. key-off (no slots set) -> NOTE_OFF */
  audio_probe_reset();
  audio_probe_fm_key(2, 0x02);          /* channel field only, no slot bits */
  n = drain(scratch, 8);
  CHECK(n == 1 && scratch[0].type == AP_EV_NOTE_OFF, "NOTE_OFF on key-off");
  CHECK(scratch[0].channel == 2, "note-off channel");

  /* 7. DAC start/stop */
  audio_probe_reset();
  audio_probe_fm_dac(1);
  audio_probe_fm_dac(0);
  n = drain(scratch, 8);
  CHECK(n == 2, "two DAC events");
  CHECK(scratch[0].type == AP_EV_DAC_START && scratch[0].source == AP_SRC_DAC, "DAC start");
  CHECK(scratch[1].type == AP_EV_DAC_STOP, "DAC stop");
  CHECK(scratch[0].channel == 5, "DAC is channel 6 (index 5)");

  /* 8. PSG raw write */
  audio_probe_reset();
  audio_probe_psg_raw(555, 0x9f);
  n = drain(scratch, 8);
  CHECK(n == 1 && scratch[0].source == AP_SRC_PSG, "PSG raw source");
  CHECK(scratch[0].data == 0x9f && scratch[0].t_cycles == 555, "PSG raw data/time");

  /* 9. per-channel gain set/get + clamping + range guard */
  audio_probe_reset();
  CHECK(audio_probe_get_channel_gain(AP_SRC_FM, 0) == 100, "gain defaults to 100");
  audio_probe_set_channel_gain(AP_SRC_FM, 0, 50);
  CHECK(audio_probe_get_channel_gain(AP_SRC_FM, 0) == 50, "gain set");
  audio_probe_set_channel_gain(AP_SRC_FM, 0, -10);
  CHECK(audio_probe_get_channel_gain(AP_SRC_FM, 0) == 0, "negative gain clamped to 0");
  audio_probe_set_channel_gain(AP_SRC_FM, 99, 30);   /* out of range: ignored */
  CHECK(audio_probe_get_channel_gain(AP_SRC_FM, 99) == 100, "out-of-range gain ignored");

  /* 10. coincidence-window grouping */
  audio_probe_reset();
  audio_probe_set_time(1000);
  audio_probe_fm_raw(0x40, 1);
  audio_probe_set_time(1000 + 10);                   /* within window */
  audio_probe_fm_raw(0x40, 2);
  audio_probe_set_time(1000 + 10 + AUDIO_PROBE_COINCIDENCE_WINDOW + 1); /* beyond */
  audio_probe_fm_raw(0x40, 3);
  n = drain(scratch, 8);
  CHECK(n == 3, "three grouped events");
  CHECK(scratch[0].group == scratch[1].group, "near events share a group");
  CHECK(scratch[2].group != scratch[1].group, "far event starts a new group");

  /* 11. ring-buffer overflow drops cleanly (keeps capacity-1) */
  audio_probe_reset();
  {
    int i;
    for (i = 0; i < AUDIO_PROBE_RING_SIZE + 5000; i++)
      audio_probe_fm_raw(0x40, i & 0xff);
    {
      static ap_event_t big[AUDIO_PROBE_RING_SIZE];
      int got = audio_probe_poll(big, AUDIO_PROBE_RING_SIZE);
      CHECK(got == AUDIO_PROBE_RING_SIZE - 1, "ring holds capacity-1 on overflow");
    }
  }

  /* 12. callback path bypasses the ring buffer */
  audio_probe_reset();
  g_cb_count = 0;
  audio_probe_set_callback(cb_capture, NULL);
  audio_probe_fm_raw(0x28, 0xaa);
  CHECK(g_cb_count == 1, "callback invoked");
  CHECK(g_cb_last.data == 0xaa, "callback event payload");
  CHECK(drain(scratch, 8) == 0, "nothing buffered while callback set");
  audio_probe_set_callback(NULL, NULL);

  /* 13. context accessor reflects emulator globals */
  rominfo.realchecksum = 0xBEEF;
  vdp_pal = 1;
  system_hw = 0x20;
  system_clock = 53693175u;
  lines_per_frame = 313;
  audio_probe_get_context(&ctx);
  CHECK(ctx.rom_crc == 0xBEEF, "context rom_crc");
  CHECK(ctx.region == 1, "context region (PAL)");
  CHECK(ctx.system_hw == 0x20, "context system_hw");
  CHECK(ctx.master_clock == 53693175u, "context master_clock");
  CHECK(ctx.cycles_per_frame == (unsigned)(MCYCLES_PER_LINE * 313), "context cycles_per_frame");

  /* ---- summary ---- */
  printf("\naudio_probe unit tests: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
