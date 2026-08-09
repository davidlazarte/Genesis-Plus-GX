/* Statistical microbenchmark for the current AYTHER audio_probe transport. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "shared.h"

ROMINFO rominfo;
uint8 vdp_pal;
uint8 system_hw;
uint32 system_clock;
uint16 lines_per_frame;

void psg_refresh_gain(void) { }

#define SAMPLE_COUNT 101
#define WARMUP_COUNT 11
#define EVENTS_PER_SAMPLE 4096

typedef struct
{
  double p50;
  double p95;
  double p99;
  double minimum;
  double maximum;
} stats_t;

static volatile unsigned long long s_sink;
static ap_event_t s_events[EVENTS_PER_SAMPLE];

static double monotonic_ns(void)
{
#if defined(_WIN32)
  static LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  if (!frequency.QuadPart)
    QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return ((double)counter.QuadPart * 1000000000.0) /
         (double)frequency.QuadPart;
#else
  struct timespec value;
  clock_gettime(CLOCK_MONOTONIC, &value);
  return ((double)value.tv_sec * 1000000000.0) + (double)value.tv_nsec;
#endif
}

static void capture_event(const ap_event_t *event, void *user)
{
  (void)user;
  s_sink += event->data + event->group;
}

static double sample_loop(void)
{
  double start;
  double end;
  int i;

  start = monotonic_ns();
  for (i = 0; i < EVENTS_PER_SAMPLE; ++i)
    s_sink += (unsigned int)i & 0xffu;
  end = monotonic_ns();
  return (end - start) / EVENTS_PER_SAMPLE;
}

static double sample_callback(void)
{
  double start;
  double end;
  int i;

  audio_probe_reset();
  audio_probe_set_callback(capture_event, NULL);
  start = monotonic_ns();
  for (i = 0; i < EVENTS_PER_SAMPLE; ++i)
    audio_probe_fm_raw(0x40u, (unsigned int)i & 0xffu);
  end = monotonic_ns();
  audio_probe_set_callback(NULL, NULL);
  return (end - start) / EVENTS_PER_SAMPLE;
}

static double sample_ring(void)
{
  double start;
  double end;
  int count;
  int i;

  audio_probe_reset();
  start = monotonic_ns();
  for (i = 0; i < EVENTS_PER_SAMPLE; ++i)
    audio_probe_fm_raw(0x40u, (unsigned int)i & 0xffu);
  count = audio_probe_poll(s_events, EVENTS_PER_SAMPLE);
  end = monotonic_ns();

  if (count > 0)
    s_sink += s_events[count - 1].data;
  return (end - start) / EVENTS_PER_SAMPLE;
}

static int compare_double(const void *left, const void *right)
{
  double a = *(const double *)left;
  double b = *(const double *)right;
  return (a > b) - (a < b);
}

static stats_t summarize(double *samples)
{
  stats_t stats;
  qsort(samples, SAMPLE_COUNT, sizeof(samples[0]), compare_double);
  stats.minimum = samples[0];
  stats.p50 = samples[50];
  stats.p95 = samples[95];
  stats.p99 = samples[99];
  stats.maximum = samples[SAMPLE_COUNT - 1];
  return stats;
}

static stats_t measure(double (*sample)(void))
{
  double samples[SAMPLE_COUNT];
  int i;

  for (i = 0; i < WARMUP_COUNT; ++i)
    (void)sample();
  for (i = 0; i < SAMPLE_COUNT; ++i)
    samples[i] = sample();
  return summarize(samples);
}

static const char *target_name(void)
{
#if defined(_WIN32) && defined(_WIN64)
  return "windows-x64-msvcrt";
#elif defined(_WIN32)
  return "windows-x86-msvcrt";
#elif defined(__linux__) && defined(__x86_64__)
  return "linux-x64";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

static const char *compiler_name(void)
{
#if defined(__clang__)
  return "clang-" __clang_version__;
#elif defined(__GNUC__)
  return "gcc-" __VERSION__;
#else
  return "unknown";
#endif
}

static void write_stats(FILE *file, const char *name, const stats_t *stats)
{
  fprintf(file,
          "\"%s\":{\"min\":%.6f,\"p50\":%.6f,\"p95\":%.6f,"
          "\"p99\":%.6f,\"max\":%.6f}",
          name, stats->minimum, stats->p50, stats->p95, stats->p99,
          stats->maximum);
}

static void write_result(FILE *file,
                         const stats_t *loop,
                         const stats_t *callback,
                         const stats_t *ring)
{
  fprintf(file,
          "{\"schema\":1,\"target\":\"%s\",\"compiler\":\"%s\","
          "\"unit\":\"ns/event\",\"samples\":%d,"
          "\"warmups\":%d,\"events_per_sample\":%d,\"results\":{",
          target_name(), compiler_name(), SAMPLE_COUNT, WARMUP_COUNT,
          EVENTS_PER_SAMPLE);
  write_stats(file, "control_loop", loop);
  fputc(',', file);
  write_stats(file, "inline_callback", callback);
  fputc(',', file);
  write_stats(file, "ring_emit_and_poll", ring);
  fprintf(file, "},\"sink\":%llu}\n", s_sink);
}

int main(int argc, char **argv)
{
  stats_t loop;
  stats_t callback;
  stats_t ring;
  FILE *artifact = NULL;

  if (argc > 2)
  {
    fprintf(stderr, "usage: %s [RESULT_JSON]\n", argv[0]);
    return 2;
  }

  loop = measure(sample_loop);
  callback = measure(sample_callback);
  ring = measure(sample_ring);

  write_result(stdout, &loop, &callback, &ring);
  if (argc == 2)
  {
    artifact = fopen(argv[1], "wb");
    if (!artifact)
    {
      fprintf(stderr, "cannot write benchmark result: %s\n", argv[1]);
      return 2;
    }
    write_result(artifact, &loop, &callback, &ring);
    fclose(artifact);
  }

  return 0;
}
