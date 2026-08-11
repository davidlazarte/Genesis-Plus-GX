#include <stdint.h>
#include <stdio.h>

#include "ayther/ayther_sprite_capture.h"

static int passed;
static int failed;

#define CHECK(condition, description) do { \
  if (condition) ++passed; \
  else { ++failed; fprintf(stderr, "FAIL: %s\n", description); } \
} while (0)

static void record_variant(unsigned int value)
{
  ayther_sprite_capture_record(
      (uint16_t)(0x80u + value), (uint16_t)(0x100u + value * 3u),
      (uint16_t)(0x2000u + value),
      (uint8_t)(1u + (value & 3u)),
      (uint8_t)(1u + ((value >> 2) & 3u)),
      (uint8_t)(value & 0x7fu), (uint8_t)(value & 0x7fu));
}

int main(void)
{
  unsigned int index;

  ayther_sprite_capture_begin_frame();
  CHECK(ayther_sprite_n == 0 && ayther_sprite_overflow == 0,
        "frame reset clears count and overflow");

  ayther_sprite_capture_record(0x81, 0x101, 0x2001, 1, 2, 3, 4);
  CHECK(ayther_sprite_n == 1, "first identity is recorded");
  CHECK(ayther_sprites[0].sat_idx == 3 &&
        ayther_sprites[0].chain_pos == 4,
        "SAT slot and chain priority are preserved");
  ayther_sprite_capture_record(0x81, 0x101, 0x2001, 1, 2, 3, 4);
  CHECK(ayther_sprite_n == 1, "exact scanline duplicate is removed");

  ayther_sprite_capture_record(0x81, 0x101, 0x2001, 2, 2, 3, 4);
  ayther_sprite_capture_record(0x81, 0x101, 0x2001, 1, 2, 4, 4);
  ayther_sprite_capture_record(0x81, 0x101, 0x2001, 1, 2, 3, 5);
  CHECK(ayther_sprite_n == 4,
        "geometry, SAT slot and chain position are identity fields");

  ayther_sprite_capture_record(0x81, 0x101, 0x2002, 1, 2, 3, 4);
  CHECK(ayther_sprite_n == 5,
        "a mid-frame SAT attribute rewrite remains observable");
  CHECK(ayther_sprites[4].attr == 0x2002,
        "rewritten SAT payload is stored in deterministic order");

  ayther_sprite_capture_record(0x381, 0x101, 0x2001, 1, 2, 3, 4);
  CHECK(ayther_sprites[5].yr == 0x381,
        "interlace-mode ten-bit Y values are preserved");

  ayther_sprite_capture_begin_frame();
  for (index = 0; index < AYTHER_SPRITE_CAPTURE_CAPACITY + 1u; ++index)
    record_variant(index);
  CHECK(ayther_sprite_n == AYTHER_SPRITE_CAPTURE_CAPACITY,
        "fixed capture capacity is enforced");
  CHECK(ayther_sprite_overflow == 1,
        "unique sprite overflow is visible");
  CHECK(ayther_sprite_metrics.unique_records ==
        AYTHER_SPRITE_CAPTURE_CAPACITY &&
        ayther_sprite_metrics.overflow_records == 1,
        "metrics distinguish accepted and overflow records");

  ayther_sprite_capture_begin_frame();
  record_variant(7);
  record_variant(7);
  CHECK(ayther_sprite_n == 1 && ayther_sprite_metrics.duplicates == 1,
        "generation tags isolate frames without clearing the hash table");
  CHECK(ayther_sprite_metrics.hash_probes < 8,
        "duplicate lookup uses a bounded O(1) probe sequence");

  printf("sprite capture unit tests: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
