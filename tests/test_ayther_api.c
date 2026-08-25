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
  CHECK(AYTHER_ABI_VERSION_1_1 == UINT32_C(0x00010001),
        "ABI 1.1 is a minor bump over 1.0");
  CHECK(AYTHER_ABI_VERSION_1_2 == UINT32_C(0x00010002),
        "ABI 1.2 is a minor bump over 1.1");
  CHECK(AYTHER_ABI_VERSION_1_3 == UINT32_C(0x00010003),
        "ABI 1.3 is a minor bump over 1.2");
  CHECK(AYTHER_ABI_VERSION_1_4 == UINT32_C(0x00010004),
        "ABI 1.4 is a minor bump over 1.3");
  CHECK(AYTHER_ABI_VERSION_1_5 == UINT32_C(0x00010005),
        "ABI 1.5 is a minor bump over 1.4");
  CHECK(AYTHER_ABI_VERSION_1_6 == UINT32_C(0x00010006),
        "ABI 1.6 is a minor bump over 1.5");
  CHECK(AYTHER_ABI_VERSION_LATEST == AYTHER_ABI_VERSION_1_6,
        "latest ABI is 1.6");

  /* #30: la 1.4 es ADITIVA. Lo que hay que fijar no es que los campos nuevos
     existan -- eso lo dice el compilador-- sino que los viejos NO se movieron:
     un consumidor compilado contra 1.0 lee por offset, y si `frame_delta_since`
     hubiera entrado en el medio del descriptor leeria basura sin enterarse. */
  CHECK(offsetof(ayther_interface_v1, frame_delta_since) >
        offsetof(ayther_interface_v1, recompose_multilayer),
        "frame_delta_since va DESPUES de todo lo de 1.3");
  CHECK(AYTHER_STATUS_DELTA_HISTORY_LOST == -10,
        "DELTA_HISTORY_LOST no pisa ningun status previo");
  CHECK(AYTHER_CAP_FRAME_DELTA_SINCE_V1 == (UINT64_C(1) << 14),
        "la capability de 1.4 usa un bit libre");
  CHECK(AYTHER_FRAME_DELTA_HISTORY == 8,
        "el ring guarda 8 generaciones");
  CHECK(AYTHER_ABI_VERSION_MAJOR(AYTHER_ABI_VERSION_1_1) == 1 &&
        AYTHER_ABI_VERSION_MINOR(AYTHER_ABI_VERSION_1_1) == 1,
        "major/minor accessors split the version");
  CHECK(sizeof(ayther_recompose_stats_v1) == 48,
        "recompose stats v1 is exactly 48 bytes");
  CHECK(offsetof(ayther_recompose_stats_v1, controls_fingerprint) == 40,
        "controls fingerprint offset is frozen");
  /* Lo que hace que 1.1 sea compatible con un cliente 1.0 no es la version que
     el core reporta, sino que los campos de 1.0 no se hayan movido. Congelarlo
     acá convierte un reordenamiento accidental en un test rojo y no en un
     frontend que lee punteros corridos. */
  CHECK(offsetof(ayther_interface_v1, recompose_stats_size) ==
        offsetof(ayther_interface_v1, poll_frame_delta) +
        sizeof(ayther_poll_frame_delta_v1_fn),
        "1.1 fields are appended right after the 1.0 block");
  CHECK(offsetof(ayther_interface_v1, query_region) <
        offsetof(ayther_interface_v1, poll_frame_delta) &&
        offsetof(ayther_interface_v1, poll_frame_delta) <
        offsetof(ayther_interface_v1, get_recompose_stats) &&
        offsetof(ayther_interface_v1, get_recompose_stats) <
        offsetof(ayther_interface_v1, recompose_multilayer),
        "interface fields are append-only");
  CHECK(offsetof(ayther_interface_v1, recompose_multilayer) ==
        offsetof(ayther_interface_v1, get_recompose_stats) +
        sizeof(ayther_get_recompose_stats_v1_fn),
        "1.2 appends right after the 1.1 block");
  /* El flag de word-swap ocupa el bit 5; los cinco anteriores no se mueven. */
  CHECK(AYTHER_REGION_WORD_SWAPPED_LE == (UINT32_C(1) << 5) &&
        AYTHER_REGION_DEPRECATED_LEGACY == (UINT32_C(1) << 4),
        "region access flags are append-only");
  CHECK(AYTHER_STATUS_RC_JOURNAL_OVERFLOW == -24 &&
        AYTHER_RC_ERR_JOURNAL_OVERFLOW == -5,
        "the journal-overflow status codes are stable");
  CHECK(AYTHER_REGION_COUNT == 21, "every region is inventoried");
  /* #41/#39: la region nueva va al FINAL del enum. Meterla en el medio
     correria los ids de todas las siguientes, y los ids viajan por la ABI. */
  CHECK(AYTHER_REGION_LINE_CRAM == AYTHER_REGION_COUNT - 1 &&
        AYTHER_REGION_LINE_REGS == AYTHER_REGION_LINE_CRAM - 1 &&
        AYTHER_REGION_SYSTEM == AYTHER_REGION_LINE_REGS - 1 &&
        AYTHER_REGION_ATTRIBUTION == AYTHER_REGION_SYSTEM - 1 &&
        AYTHER_REGION_RASTER_FALLBACK_REASONS == AYTHER_REGION_ATTRIBUTION - 1,
        "region ids are append-only");

  /* #39.B: el descriptor de sistema. Los offsets se congelan aca porque el
     struct viaja por la ABI: agregar campos al final es aditivo, moverlos no. */
  CHECK(sizeof(ayther_system_v1) == 44,
        "the system descriptor has a frozen size");
  CHECK(offsetof(ayther_system_v1, struct_size) == 0 &&
        offsetof(ayther_system_v1, layout_version) == 4 &&
        offsetof(ayther_system_v1, system_hw) == 8 &&
        offsetof(ayther_system_v1, lines_per_frame) == 14 &&
        offsetof(ayther_system_v1, viewport_x) == 16 &&
        offsetof(ayther_system_v1, master_clock) == 28 &&
        offsetof(ayther_system_v1, rom_crc32) == 36 &&
        offsetof(ayther_system_v1, rom_bytes) == 40,
        "system descriptor fields are at frozen offsets");
  CHECK(AYTHER_SYSTEM_HW_MD == 0x80 && AYTHER_SYSTEM_HW_MCD == 0x84 &&
        AYTHER_SYSTEM_HW_GG == 0x40 && AYTHER_SYSTEM_HW_SMS == 0x20,
        "system_hw uses the core SYSTEM_* values");

  /* #42: el estado por linea. Los offsets se congelan porque el struct viaja
     por la ABI: agregar campos al final es aditivo, moverlos no lo es. */
  CHECK(sizeof(ayther_line_regs_v1) == 32,
        "one scanline of register state fits in 32 bytes");
  CHECK(offsetof(ayther_line_regs_v1, xscroll_a) == 0 &&
        offsetof(ayther_line_regs_v1, yscroll_a) == 4 &&
        offsetof(ayther_line_regs_v1, ntab) == 8 &&
        offsetof(ayther_line_regs_v1, reg1) == 18 &&
        offsetof(ayther_line_regs_v1, clip_a_start) == 26 &&
        offsetof(ayther_line_regs_v1, flags) == 30,
        "line register fields are at frozen offsets");
  CHECK(sizeof(ayther_line_header_v1) == 24 &&
        offsetof(ayther_line_header_v1, lines) == 8 &&
        offsetof(ayther_line_header_v1, frame_generation) == 16,
        "the per-line header is frozen");
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
  CHECK(AYTHER_CAP_FRAME_DELTA_V1 == (UINT64_C(1) << 11) &&
        AYTHER_CAP_RECOMPOSE_STATS_V1 == (UINT64_C(1) << 12),
        "capability bits added after v1 keep their positions");
  /* #42: dos bits nuevos al final. Los ocho de antes no se mueven: los ids de
     suscripcion viajan por la ABI igual que los de region. */
  CHECK(AYTHER_SUB_ALL == UINT32_C(0x3FF),
        "all subscription bits are accounted for");
  CHECK(AYTHER_SUB_LINE_STATE == (UINT32_C(1) << 8) &&
        AYTHER_SUB_LINE_CRAM == (UINT32_C(1) << 9) &&
        AYTHER_SUB_ATTRIBUTION == (UINT32_C(1) << 7),
        "subscription bits are append-only");
  CHECK((AYTHER_SUB_ALL & UINT32_C(0x7F)) == UINT32_C(0x7F) &&
        AYTHER_SUB_ATTRIBUTION == (UINT32_C(1) << 7),
        "the v1 subscription bits keep their positions");
  CHECK(AYTHER_CAP_ATTRIBUTION_V1 == (UINT64_C(1) << 13),
        "the attribution capability bit is stable");
  /* Layout del byte de atribucion: los campos no se pisan y cubren el byte. */
  CHECK((AYTHER_ATTRIB_LAYER_MASK | AYTHER_ATTRIB_PRIORITY |
         AYTHER_ATTRIB_PALETTE_MASK | AYTHER_ATTRIB_SH_MASK |
         AYTHER_ATTRIB_SPRITE) == UINT8_C(0xFF),
        "the attribution byte fields cover it exactly");
  CHECK((AYTHER_ATTRIB_LAYER_MASK & AYTHER_ATTRIB_PALETTE_MASK) == 0 &&
        (AYTHER_ATTRIB_PALETTE_MASK & AYTHER_ATTRIB_SH_MASK) == 0 &&
        (AYTHER_ATTRIB_SH_MASK & AYTHER_ATTRIB_SPRITE) == 0,
        "the attribution byte fields do not overlap");
  CHECK(AYTHER_ATTRIB_LAYER_WINDOW == 3 && AYTHER_ATTRIB_LAYER_BACKDROP == 0,
        "attribution layer codes are stable");
  CHECK(AYTHER_AUDIO_TRANSPORT_OBSERVATION_ACTIVE == UINT32_C(2),
        "observation-active transport flag is stable");

  printf("ayther API header tests: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
