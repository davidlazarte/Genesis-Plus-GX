/* Fixed-capacity, frame-scoped sprite observation for AYTHER. */

#ifndef AYTHER_SPRITE_CAPTURE_H
#define AYTHER_SPRITE_CAPTURE_H

#include <stdint.h>

#include "ayther_api.h"

#define AYTHER_SPRITE_CAPTURE_CAPACITY 128u
#define AYTHER_SPRITE_CAPTURE_HASH_SIZE 256u

typedef ayther_sprite_v1 AytherSpr;

extern AytherSpr ayther_sprites[AYTHER_SPRITE_CAPTURE_CAPACITY];
extern uint8_t ayther_sprite_n;
extern uint32_t ayther_sprite_overflow;

void ayther_sprite_capture_begin_frame(void);
void ayther_sprite_capture_record(uint16_t yr, uint16_t xr, uint16_t attr,
                                  uint8_t w, uint8_t h, uint8_t sat_idx,
                                  uint8_t chain_pos);

#ifdef AYTHER_SPRITE_CAPTURE_METRICS
typedef struct ayther_sprite_capture_metrics
{
  uint32_t record_calls;
  uint32_t hash_probes;
  uint32_t duplicates;
  uint32_t unique_records;
  uint32_t overflow_records;
  uint32_t max_probe;
} ayther_sprite_capture_metrics;

extern ayther_sprite_capture_metrics ayther_sprite_metrics;
#endif

#endif /* AYTHER_SPRITE_CAPTURE_H */
