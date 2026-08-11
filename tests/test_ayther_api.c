/* Compile-time and constant-level checks for the public AYTHER ABI header. */

#include <stddef.h>
#include <stdio.h>

#include "ayther/ayther_api.h"

static int passed;
static int failed;

#define CHECK(condition, message) do { \
  if (condition) { ++passed; } \
  else { ++failed; fprintf(stderr, "FAIL: %s\n", message); } \
} while (0)

int main(void)
{
  uint64_t required;

  CHECK(sizeof(ayther_sprite_v1) == 10, "sprite v1 is exactly 10 bytes");
  CHECK(offsetof(ayther_sprite_v1, sat_idx) == 8,
        "sprite v1 SAT index offset is frozen");
  CHECK(offsetof(ayther_sprite_v1, chain_pos) == 9,
        "sprite v1 chain position offset is frozen");
  CHECK(sizeof(ayther_audio_write_v1) == 8,
        "audio write v1 is exactly 8 bytes");
  CHECK(offsetof(ayther_audio_write_v1, data) == 6,
        "audio write v1 data offset is frozen");
  CHECK(sizeof(ayther_audio_voice_v1) == 40,
        "audio voice v1 is exactly 40 bytes");
  CHECK(offsetof(ayther_audio_voice_v1, block_fnum) == 36,
        "audio voice pitch offset is frozen");
  CHECK(sizeof(ayther_audio_event_v1) == 80,
        "audio event v1 is exactly 80 bytes");
  CHECK(offsetof(ayther_audio_event_v1, group) == 20,
        "audio event group offset is frozen");
  CHECK(offsetof(ayther_audio_event_v1, voice) == 24,
        "audio event voice offset is frozen");
  CHECK(sizeof(ayther_audio_transport_stats_v1) == 32,
        "audio transport stats v1 is exactly 32 bytes");
  CHECK(sizeof(ayther_region_info_v1) == 32,
        "region descriptor v1 is exactly 32 bytes");
  CHECK(sizeof(ayther_frame_snapshot_v1) == 48,
        "frame snapshot v1 is exactly 48 bytes");
  CHECK(offsetof(ayther_frame_snapshot_v1, frame_generation) == 16,
        "frame generation offset is frozen");
  CHECK(sizeof(ayther_subscription_state_v1) == 32,
        "subscription state v1 is exactly 32 bytes");
  CHECK(offsetof(ayther_subscription_state_v1, activation_frame) == 24,
        "subscription activation frame offset is frozen");
  CHECK(AYTHER_ABI_VERSION_1_0 == UINT32_C(0x00010000),
        "ABI version uses major/minor encoding");
  CHECK(AYTHER_REGION_COUNT == 17, "all sixteen v1 regions are inventoried");
  CHECK(AYTHER_LEGACY_MEMORY_CRAM == 0x100,
        "legacy memory IDs remain stable");
  CHECK(AYTHER_LEGACY_MEMORY_RASTER_DIRTY == 0x10E,
        "legacy raster ID remains stable");

  required = AYTHER_CAP_LEGACY_MEMORY | AYTHER_CAP_REGION_QUERY |
             AYTHER_CAP_REGION_READ | AYTHER_CAP_CONTROL_WRITE |
             AYTHER_CAP_FRAME_SNAPSHOT | AYTHER_CAP_PARSED_SPRITES_V1 |
             AYTHER_CAP_AUDIO_WRITES_V1 |
             AYTHER_CAP_RASTER_FALLBACK_V1 | AYTHER_CAP_RECOMPOSE_V1 |
             AYTHER_CAP_AUDIO_PROBE_V1 | AYTHER_CAP_SUBSCRIPTIONS_V1;
  CHECK(required == UINT64_C(0x7FF), "v1 capability bits are stable");
  CHECK(AYTHER_SUB_ALL == UINT32_C(0x7F),
        "all v1 subscription bits are stable");
  CHECK(AYTHER_AUDIO_TRANSPORT_OBSERVATION_ACTIVE == UINT32_C(2),
        "observation-active transport flag is stable");

  printf("ayther API header tests: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
