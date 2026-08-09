/* Deterministic, ROM-free golden trace for the AYTHER audio_probe contract. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared.h"

ROMINFO rominfo;
uint8 rom_region;
uint8 vdp_pal;
uint8 system_hw;
uint32 system_clock;
uint16 lines_per_frame;

void psg_refresh_gain(void) { }

#define TRACE_CAPACITY 256
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

static unsigned long long hash_byte(unsigned long long hash, unsigned int value)
{
  hash ^= (unsigned char)value;
  return hash * FNV_PRIME;
}

static unsigned long long hash_u32(unsigned long long hash, unsigned int value)
{
  int i;
  for (i = 0; i < 4; ++i)
    hash = hash_byte(hash, value >> (i * 8));
  return hash;
}

static unsigned long long hash_u64(unsigned long long hash,
                                   unsigned long long value)
{
  int i;
  for (i = 0; i < 8; ++i)
    hash = hash_byte(hash, (unsigned int)(value >> (i * 8)));
  return hash;
}

static unsigned long long hash_bytes(unsigned long long hash,
                                     const unsigned char *values,
                                     int count)
{
  int i;
  for (i = 0; i < count; ++i)
    hash = hash_byte(hash, values[i]);
  return hash;
}

static unsigned long long hash_event(unsigned long long hash,
                                     const ap_event_t *event)
{
  const ap_voice_t *voice = &event->voice;

  hash = hash_u64(hash, event->t_global);
  hash = hash_u32(hash, event->t_frame);
  hash = hash_u32(hash, event->t_cycles);
  hash = hash_byte(hash, event->source);
  hash = hash_byte(hash, event->type);
  hash = hash_byte(hash, event->channel);
  hash = hash_byte(hash, event->schema);
  hash = hash_u32(hash, event->group);
  hash = hash_u32(hash, event->reg);
  hash = hash_u32(hash, event->data);
  hash = hash_bytes(hash, voice->op_tl, 4);
  hash = hash_bytes(hash, voice->op_ar, 4);
  hash = hash_bytes(hash, voice->op_dr, 4);
  hash = hash_bytes(hash, voice->op_sr, 4);
  hash = hash_bytes(hash, voice->op_rr, 4);
  hash = hash_bytes(hash, voice->op_mul, 4);
  hash = hash_bytes(hash, voice->op_dt, 4);
  hash = hash_byte(hash, voice->algorithm);
  hash = hash_byte(hash, voice->feedback);
  hash = hash_byte(hash, voice->ams);
  hash = hash_byte(hash, voice->fms);
  hash = hash_byte(hash, voice->pan);
  hash = hash_u32(hash, voice->block_fnum);
  hash = hash_u64(hash, event->voice_hash);
  hash = hash_u64(hash, event->timbre_hash);
  return hash;
}

static void set_time_and_write(unsigned int *cycles,
                               unsigned int reg,
                               unsigned int data)
{
  audio_probe_set_time(*cycles);
  audio_probe_fm_raw(reg, data);
  *cycles += 17;
}

static void program_voice(int channel, unsigned int *cycles)
{
  int bank = (channel >= 3) ? 0x100 : 0;
  int channel_in_bank = channel % 3;
  int op;

  for (op = 0; op < 4; ++op)
  {
    int offset = (op << 2) + channel_in_bank;
    set_time_and_write(cycles, bank + 0x30 + offset, 0x35);
    set_time_and_write(cycles, bank + 0x40 + offset, 0x12 + op);
    set_time_and_write(cycles, bank + 0x50 + offset, 0x1f);
    set_time_and_write(cycles, bank + 0x60 + offset, 0x0a);
    set_time_and_write(cycles, bank + 0x70 + offset, 0x07);
    set_time_and_write(cycles, bank + 0x80 + offset, 0xf9);
  }

  set_time_and_write(cycles, bank + 0xb0 + channel_in_bank, 0x3a);
  set_time_and_write(cycles, bank + 0xb4 + channel_in_bank, 0xc5);
  set_time_and_write(cycles, bank + 0xa4 + channel_in_bank, 0x1a);
  set_time_and_write(cycles, bank + 0xa0 + channel_in_bank, 0x34);
}

static int emit_trace(ap_event_t *events, int capacity)
{
  unsigned int cycles = 100;

  audio_probe_reset();
  program_voice(0, &cycles);

  audio_probe_set_time(cycles);
  audio_probe_fm_key(0, 0xf0);
  cycles += 23;

  audio_probe_set_time(cycles);
  audio_probe_fm_dac(1);
  cycles += 29;

  audio_probe_psg_raw(cycles, 0x9f);
  cycles += 31;

  audio_probe_set_time(cycles);
  audio_probe_signal(AP_EV_STATE_LOAD);
  audio_probe_frame(3420);

  cycles = 44;
  audio_probe_psg_raw(cycles, 0x80);
  audio_probe_set_time(cycles + 7);
  audio_probe_fm_key(0, 0x00);
  audio_probe_set_time(cycles + 19);
  audio_probe_fm_dac(0);
  audio_probe_frame(3420);

  return audio_probe_poll(events, capacity);
}

static int read_golden(const char *path,
                       unsigned int *schema,
                       unsigned int *event_count,
                       unsigned long long *trace_hash)
{
  FILE *file;
  char buffer[256];
  char hash_text[17];

  file = fopen(path, "rb");
  if (!file)
  {
    fprintf(stderr, "cannot open golden trace: %s\n", path);
    return 0;
  }

  if (!fgets(buffer, sizeof(buffer), file))
  {
    fclose(file);
    fprintf(stderr, "cannot read golden trace: %s\n", path);
    return 0;
  }
  fclose(file);

  if (sscanf(buffer,
             "{\"schema\":%u,\"event_count\":%u,\"trace_hash\":\"%16[0-9a-fA-F]\"}",
             schema, event_count, hash_text) != 3)
  {
    fprintf(stderr, "invalid golden trace format: %s\n", path);
    return 0;
  }

  *trace_hash = strtoull(hash_text, NULL, 16);
  return 1;
}

static int write_actual(const char *path,
                        unsigned int event_count,
                        unsigned long long trace_hash)
{
  FILE *file = stdout;
  int close_file = 0;

  if (path)
  {
    file = fopen(path, "wb");
    if (!file)
    {
      fprintf(stderr, "cannot write actual trace: %s\n", path);
      return 0;
    }
    close_file = 1;
  }

  fprintf(file,
          "{\"schema\":%u,\"event_count\":%u,\"trace_hash\":\"%016llx\"}\n",
          AUDIO_PROBE_SCHEMA, event_count, trace_hash);

  if (close_file)
    fclose(file);
  return 1;
}

int main(int argc, char **argv)
{
  ap_event_t events[TRACE_CAPACITY];
  unsigned int expected_schema;
  unsigned int expected_count;
  unsigned long long expected_hash;
  unsigned long long actual_hash = FNV_OFFSET;
  int event_count;
  int i;

  if (argc < 2 || argc > 3)
  {
    fprintf(stderr, "usage: %s GOLDEN_JSON [ACTUAL_JSON]\n", argv[0]);
    return 2;
  }

  event_count = emit_trace(events, TRACE_CAPACITY);
  if (event_count <= 0 || event_count == TRACE_CAPACITY)
  {
    fprintf(stderr, "invalid deterministic trace size: %d\n", event_count);
    return 2;
  }

  for (i = 0; i < event_count; ++i)
    actual_hash = hash_event(actual_hash, &events[i]);

  if (!write_actual(argc == 3 ? argv[2] : NULL,
                    (unsigned int)event_count,
                    actual_hash))
    return 2;

  printf("audio_probe deterministic trace: events=%d hash=%016llx\n",
         event_count, actual_hash);

  if (!read_golden(argv[1], &expected_schema, &expected_count, &expected_hash))
    return 2;

  if (expected_schema != AUDIO_PROBE_SCHEMA ||
      expected_count != (unsigned int)event_count ||
      expected_hash != actual_hash)
  {
    fprintf(stderr,
            "golden mismatch: expected schema=%u events=%u hash=%016llx\n",
            expected_schema, expected_count, expected_hash);
    return 1;
  }

  return 0;
}
