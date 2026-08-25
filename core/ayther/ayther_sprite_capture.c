/* O(1)-amortized sprite deduplication with no hot-path allocations. */

#include "ayther_sprite_capture.h"

#include <string.h>

typedef struct ayther_sprite_hash_entry
{
  uint32_t generation;
  uint8_t sprite_index;
} ayther_sprite_hash_entry;

AytherSpr ayther_sprites[AYTHER_SPRITE_CAPTURE_CAPACITY];
uint8_t ayther_sprite_n;
uint32_t ayther_sprite_overflow;

static ayther_sprite_hash_entry
    ayther_sprite_hash[AYTHER_SPRITE_CAPTURE_HASH_SIZE];
static uint32_t ayther_sprite_generation = 1;

/* #37 punto 2: un sprite de 32 px de alto se registra 32 veces por frame, una
   por scanline en la que es visible. Las 31 repeticiones terminan en el mismo
   lugar -- el dedup del hash-- pero recien despues de hashear diez bytes y
   sondear la tabla.

   El filtro de aca adelante recuerda, POR SLOT DE LA SAT, la ultima identidad
   registrada. Si la de esta linea es la misma, no hay nada que hacer y se sale
   con una comparacion. Si cambio, sigue el camino de siempre.

   Recordar la ULTIMA y no un "ya lo vi este frame" es lo que conserva el caso
   que la captura existe para cubrir: el SAT reescrito a mitad de frame -- el
   genio del logo Sega-- donde un mismo slot tiene dos identidades distintas en
   el mismo frame. Un bitmap de "visto" se comeria la segunda. */
#define AYTHER_SPRITE_SLOTS 128u

typedef struct ayther_sprite_last_seen
{
  uint32_t generation;
  uint16_t yr, xr, attr;
  uint8_t w, h, chain_pos;
} ayther_sprite_last_seen;

static ayther_sprite_last_seen ayther_sprite_last[AYTHER_SPRITE_SLOTS];

#ifdef AYTHER_SPRITE_CAPTURE_METRICS
ayther_sprite_capture_metrics ayther_sprite_metrics;
#define AYTHER_METRIC_ADD(field, value) \
  (ayther_sprite_metrics.field += (uint32_t)(value))
#define AYTHER_METRIC_MAX(field, value) \
  do { \
    uint32_t ayther_metric_value = (uint32_t)(value); \
    if (ayther_sprite_metrics.field < ayther_metric_value) \
      ayther_sprite_metrics.field = ayther_metric_value; \
  } while (0)
#else
#define AYTHER_METRIC_ADD(field, value) ((void)0)
#define AYTHER_METRIC_MAX(field, value) ((void)0)
#endif

static uint32_t ayther_sprite_hash_key(uint16_t yr, uint16_t xr,
                                      uint16_t attr, uint8_t w, uint8_t h,
                                      uint8_t sat_idx, uint8_t chain_pos)
{
  uint32_t hash = UINT32_C(2166136261);
#define AYTHER_HASH_BYTE(value) \
  do { hash = (hash ^ (uint8_t)(value)) * UINT32_C(16777619); } while (0)
  AYTHER_HASH_BYTE(yr);
  AYTHER_HASH_BYTE(yr >> 8);
  AYTHER_HASH_BYTE(xr);
  AYTHER_HASH_BYTE(xr >> 8);
  AYTHER_HASH_BYTE(attr);
  AYTHER_HASH_BYTE(attr >> 8);
  AYTHER_HASH_BYTE(w);
  AYTHER_HASH_BYTE(h);
  AYTHER_HASH_BYTE(sat_idx);
  AYTHER_HASH_BYTE(chain_pos);
#undef AYTHER_HASH_BYTE
  hash ^= hash >> 16;
  return hash;
}

static int ayther_sprite_equal(const AytherSpr *sprite,
                               uint16_t yr, uint16_t xr, uint16_t attr,
                               uint8_t w, uint8_t h, uint8_t sat_idx,
                               uint8_t chain_pos)
{
  return sprite->yr == yr && sprite->xr == xr && sprite->attr == attr &&
         sprite->w == w && sprite->h == h &&
         sprite->sat_idx == sat_idx && sprite->chain_pos == chain_pos;
}

void ayther_sprite_capture_begin_frame(void)
{
  ayther_sprite_n = 0;
  ayther_sprite_overflow = 0;
  ++ayther_sprite_generation;
  if (!ayther_sprite_generation)
  {
    memset(ayther_sprite_hash, 0, sizeof(ayther_sprite_hash));
    memset(ayther_sprite_last, 0, sizeof(ayther_sprite_last));
    ayther_sprite_generation = 1;
  }
#ifdef AYTHER_SPRITE_CAPTURE_METRICS
  memset(&ayther_sprite_metrics, 0, sizeof(ayther_sprite_metrics));
#endif
}

void ayther_sprite_capture_record(uint16_t yr, uint16_t xr, uint16_t attr,
                                  uint8_t w, uint8_t h, uint8_t sat_idx,
                                  uint8_t chain_pos)
{
  uint32_t slot;
  uint32_t probe;
  ayther_sprite_last_seen *last = &ayther_sprite_last[sat_idx & (AYTHER_SPRITE_SLOTS - 1u)];

  AYTHER_METRIC_ADD(record_calls, 1);

  /* #37.2: la misma identidad en el mismo slot y el mismo frame ya se
     registro. Salir aca ahorra el hash de diez bytes y el sondeo. */
  if (last->generation == ayther_sprite_generation &&
      last->yr == yr && last->xr == xr && last->attr == attr &&
      last->w == w && last->h == h && last->chain_pos == chain_pos)
  {
    AYTHER_METRIC_ADD(duplicates, 1);
    return;
  }
  last->generation = ayther_sprite_generation;
  last->yr = yr; last->xr = xr; last->attr = attr;
  last->w = w; last->h = h; last->chain_pos = chain_pos;

  slot = ayther_sprite_hash_key(
      yr, xr, attr, w, h, sat_idx, chain_pos) &
      (AYTHER_SPRITE_CAPTURE_HASH_SIZE - 1u);
  for (probe = 1; probe <= AYTHER_SPRITE_CAPTURE_HASH_SIZE; ++probe)
  {
    ayther_sprite_hash_entry *entry = &ayther_sprite_hash[slot];
    AYTHER_METRIC_ADD(hash_probes, 1);
    if (entry->generation != ayther_sprite_generation)
    {
      AytherSpr *sprite;
      if (ayther_sprite_n >= AYTHER_SPRITE_CAPTURE_CAPACITY)
      {
        ayther_sprite_overflow = 1;
        AYTHER_METRIC_ADD(overflow_records, 1);
        AYTHER_METRIC_MAX(max_probe, probe);
        return;
      }
      entry->generation = ayther_sprite_generation;
      entry->sprite_index = ayther_sprite_n;
      sprite = &ayther_sprites[ayther_sprite_n++];
      sprite->yr = yr;
      sprite->xr = xr;
      sprite->attr = attr;
      sprite->w = w;
      sprite->h = h;
      sprite->sat_idx = sat_idx;
      sprite->chain_pos = chain_pos;
      AYTHER_METRIC_ADD(unique_records, 1);
      AYTHER_METRIC_MAX(max_probe, probe);
      return;
    }
    if (ayther_sprite_equal(&ayther_sprites[entry->sprite_index],
                            yr, xr, attr, w, h, sat_idx, chain_pos))
    {
      AYTHER_METRIC_ADD(duplicates, 1);
      AYTHER_METRIC_MAX(max_probe, probe);
      return;
    }
    slot = (slot + 1u) & (AYTHER_SPRITE_CAPTURE_HASH_SIZE - 1u);
  }

  /* The table is twice the public capacity, so this is defensive only. */
  ayther_sprite_overflow = 1;
  AYTHER_METRIC_ADD(overflow_records, 1);
  AYTHER_METRIC_MAX(max_probe, AYTHER_SPRITE_CAPTURE_HASH_SIZE);
}
