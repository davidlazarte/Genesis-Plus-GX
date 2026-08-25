/* Before/after microbenchmark for issue #10 sprite deduplication. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "ayther/ayther_sprite_capture.h"

#define SAMPLE_COUNT 101
#define WARMUP_COUNT 11
#define SPRITES_PER_LINE 80
#define VISIBLE_LINES 64
#define CALLS_PER_SAMPLE (SPRITES_PER_LINE * VISIBLE_LINES)

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

typedef struct
{
  double p50;
  double p95;
  double p99;
  double minimum;
  double maximum;
} stats_t;

static volatile uint64_t sink;
static ayther_sprite_v1 legacy_sprites[AYTHER_SPRITE_CAPTURE_CAPACITY];
static uint32_t legacy_count;
static uint64_t legacy_comparisons;

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

static void sprite_values(unsigned int index, uint16_t *yr, uint16_t *xr,
                          uint16_t *attr, uint8_t *w, uint8_t *h,
                          uint8_t *sat_idx, uint8_t *chain_pos)
{
  *yr = (uint16_t)(0x90u + (index & 7u));
  *xr = (uint16_t)(0x80u + ((index * 37u) % 320u));
  *attr = (uint16_t)(0x2000u + index);
  *w = (uint8_t)(1u + (index & 3u));
  *h = (uint8_t)(1u + ((index >> 2) & 3u));
  *sat_idx = (uint8_t)index;
  *chain_pos = (uint8_t)index;
}

static NOINLINE void legacy_record(uint16_t yr, uint16_t xr, uint16_t attr,
                                   uint8_t w, uint8_t h, uint8_t sat_idx,
                                   uint8_t chain_pos)
{
  uint32_t index;
  for (index = 0; index < legacy_count; ++index)
  {
    ++legacy_comparisons;
    if (legacy_sprites[index].yr == yr &&
        legacy_sprites[index].xr == xr &&
        legacy_sprites[index].attr == attr)
      return;
  }
  if (legacy_count < AYTHER_SPRITE_CAPTURE_CAPACITY)
  {
    ayther_sprite_v1 *sprite = &legacy_sprites[legacy_count++];
    sprite->yr = yr;
    sprite->xr = xr;
    sprite->attr = attr;
    sprite->w = w;
    sprite->h = h;
    sprite->sat_idx = sat_idx;
    sprite->chain_pos = chain_pos;
  }
}

static double sample_legacy(void)
{
  double start;
  double end;
  unsigned int line;
  unsigned int index;
  legacy_count = 0;
  legacy_comparisons = 0;
  start = monotonic_ns();
  for (line = 0; line < VISIBLE_LINES; ++line)
    for (index = 0; index < SPRITES_PER_LINE; ++index)
    {
      uint16_t yr, xr, attr;
      uint8_t w, h, sat_idx, chain_pos;
      sprite_values(index, &yr, &xr, &attr, &w, &h, &sat_idx, &chain_pos);
      legacy_record(yr, xr, attr, w, h, sat_idx, chain_pos);
    }
  end = monotonic_ns();
  sink += legacy_count + legacy_sprites[legacy_count - 1u].attr;
  return (end - start) / CALLS_PER_SAMPLE;
}

static double sample_hash(void)
{
  double start;
  double end;
  unsigned int line;
  unsigned int index;
  ayther_sprite_capture_begin_frame();
  start = monotonic_ns();
  for (line = 0; line < VISIBLE_LINES; ++line)
    for (index = 0; index < SPRITES_PER_LINE; ++index)
    {
      uint16_t yr, xr, attr;
      uint8_t w, h, sat_idx, chain_pos;
      sprite_values(index, &yr, &xr, &attr, &w, &h, &sat_idx, &chain_pos);
      ayther_sprite_capture_record(yr, xr, attr, w, h, sat_idx, chain_pos);
    }
  end = monotonic_ns();
  sink += ayther_sprite_n + ayther_sprites[ayther_sprite_n - 1u].attr;
  return (end - start) / CALLS_PER_SAMPLE;
}

static int compare_double(const void *left, const void *right)
{
  double a = *(const double *)left;
  double b = *(const double *)right;
  return (a > b) - (a < b);
}

static stats_t measure(double (*sample)(void))
{
  double samples[SAMPLE_COUNT];
  stats_t result;
  int index;
  for (index = 0; index < WARMUP_COUNT; ++index)
    (void)sample();
  for (index = 0; index < SAMPLE_COUNT; ++index)
    samples[index] = sample();
  qsort(samples, SAMPLE_COUNT, sizeof(samples[0]), compare_double);
  result.minimum = samples[0];
  result.p50 = samples[50];
  result.p95 = samples[95];
  result.p99 = samples[99];
  result.maximum = samples[SAMPLE_COUNT - 1];
  return result;
}

int main(int argc, char **argv)
{
  stats_t legacy;
  stats_t hashed;
  uint64_t comparison_count;
  uint32_t probe_count;
  FILE *output;
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s OUTPUT_JSON\n", argv[0]);
    return 2;
  }

  legacy = measure(sample_legacy);
  (void)sample_legacy();
  comparison_count = legacy_comparisons;
  hashed = measure(sample_hash);
  (void)sample_hash();
  probe_count = ayther_sprite_metrics.hash_probes;

  output = fopen(argv[1], "wb");
  if (!output)
  {
    fprintf(stderr, "cannot create sprite benchmark %s\n", argv[1]);
    return 1;
  }
  fprintf(output,
      "{\"schema\":1,\"fixture\":\"sprite-heavy-v1\","
      "\"samples\":%u,\"warmups\":%u,\"sprites_per_line\":%u,"
      "\"visible_lines\":%u,\"calls_per_sample\":%u,"
      "\"unit\":\"ns/record\","
      "\"legacy_linear\":{\"min\":%.3f,\"p50\":%.3f,"
      "\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f,"
      "\"comparisons\":%" PRIu64 "},"
      "\"generation_hash\":{\"min\":%.3f,\"p50\":%.3f,"
      "\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f,"
      "\"probes\":%u,\"max_probe\":%u},"
      "\"comparison_reduction_percent\":%.3f,"
      "\"cpu_reduction_percent_p50\":%.3f}\n",
      SAMPLE_COUNT, WARMUP_COUNT, SPRITES_PER_LINE, VISIBLE_LINES,
      CALLS_PER_SAMPLE,
      legacy.minimum, legacy.p50, legacy.p95, legacy.p99, legacy.maximum,
      comparison_count,
      hashed.minimum, hashed.p50, hashed.p95, hashed.p99, hashed.maximum,
      probe_count, ayther_sprite_metrics.max_probe,
      comparison_count ?
        (1.0 - ((double)probe_count / (double)comparison_count)) * 100.0 : 0.0,
      legacy.p50 ? (1.0 - (hashed.p50 / legacy.p50)) * 100.0 : 0.0);
  fclose(output);

  printf("sprite capture p50: linear %.3f ns, hash %.3f ns (%.1f%% faster); "
         "comparisons %" PRIu64 " -> probes %u\n",
         legacy.p50, hashed.p50,
         legacy.p50 ? (1.0 - (hashed.p50 / legacy.p50)) * 100.0 : 0.0,
         comparison_count, probe_count);

  /* #35 punto 3: que falla y que solo se informa.

     Antes este bench fallaba si CUALQUIERA de las dos cosas no se cumplia, y
     una de ellas es una comparacion de reloj entre dos implementaciones que se
     parecen. En un runner compartido eso se invierte solo, y el efecto de un
     gate asi no es cuidar el codigo: es ensenar a ignorar el rojo.

     Lo que queda bloqueante es la afirmacion ESTRUCTURAL, que no depende de en
     que maquina toco correr: el camino con hash hace menos sondeos que
     comparaciones hace el barrido lineal. Si eso se invierte, la estructura de
     datos dejo de servir para lo que existe, y da igual el reloj.

     El tiempo se sigue midiendo y publicando -- la serie temporal es util-,
     pero si se invierte se avisa y no se rompe la corrida. */
  if (probe_count >= comparison_count)
  {
    fprintf(stderr,
            "REGRESION ESTRUCTURAL: el hash hace %u sondeos y el barrido lineal\n"
            "%" PRIu64 " comparaciones. La estructura dejo de ahorrar trabajo.\n",
            probe_count, comparison_count);
    return 1;
  }
  if (!(hashed.p50 < legacy.p50))
    fprintf(stderr,
            "AVISO: el p50 con hash (%.3f ns) no quedo debajo del lineal (%.3f ns).\n"
            "No falla la corrida: dos implementaciones parecidas medidas en un\n"
            "runner compartido se invierten por ruido. El dato queda en el JSON.\n",
            hashed.p50, legacy.p50);
  return 0;
}
