/*
 * AYTHER public core interface
 *
 * This header is the versioned, in-process contract between the AYTHER fork
 * and a frontend. It deliberately does not include libretro headers.
 */

#ifndef AYTHER_API_H
#define AYTHER_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#define AYTHER_CALL __cdecl
#else
#define AYTHER_CALL
#endif

#if defined(AYTHER_CORE_EXPORTS)
#  if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#    if defined(__GNUC__)
#      define AYTHER_API __attribute__((__dllexport__))
#    else
#      define AYTHER_API __declspec(dllexport)
#    endif
#  elif defined(__GNUC__) && (__GNUC__ >= 4)
#    define AYTHER_API __attribute__((__visibility__("default")))
#  else
#    define AYTHER_API
#  endif
#else
#  define AYTHER_API
#endif

#define AYTHER_ABI_VERSION_1_0 UINT32_C(0x00010000)
#define AYTHER_ABI_VERSION_LATEST AYTHER_ABI_VERSION_1_0

#define AYTHER_GENERATION_ANY UINT64_MAX
#define AYTHER_LEGACY_MEMORY_NONE UINT32_MAX

/* ayther_get_interface(0) returns the latest supported interface. An explicit
 * unsupported version returns NULL. A missing symbol identifies a stock or
 * pre-ABI AYTHER core and must be treated as zero capabilities. */

enum ayther_status
{
  AYTHER_STATUS_OK                = 0,
  AYTHER_STATUS_INVALID_ARGUMENT  = -1,
  AYTHER_STATUS_NOT_FOUND         = -2,
  AYTHER_STATUS_BUFFER_TOO_SMALL  = -3,
  AYTHER_STATUS_OUT_OF_BOUNDS     = -4,
  AYTHER_STATUS_READ_ONLY         = -5,
  AYTHER_STATUS_STALE_GENERATION  = -6,
  AYTHER_STATUS_BUSY              = -7,
  AYTHER_STATUS_UNSUPPORTED       = -8,
  AYTHER_STATUS_NOT_SUBSCRIBED    = -9
};

enum ayther_recompose_error
{
  AYTHER_RC_ERR_NOT_MODE5 = -1,
  AYTHER_RC_ERR_INTERLACE2 = -2,
  AYTHER_RC_ERR_NTSC_FILTER = -3,
  AYTHER_RC_ERR_INVALID_PARAMS = -4
};

enum ayther_endianness
{
  AYTHER_ENDIAN_LITTLE = 1,
  AYTHER_ENDIAN_BIG    = 2
};

/* Capabilities are additive. Unknown bits must be ignored. */
#define AYTHER_CAP_LEGACY_MEMORY       (UINT64_C(1) << 0)
#define AYTHER_CAP_REGION_QUERY        (UINT64_C(1) << 1)
#define AYTHER_CAP_REGION_READ         (UINT64_C(1) << 2)
#define AYTHER_CAP_CONTROL_WRITE       (UINT64_C(1) << 3)
#define AYTHER_CAP_FRAME_SNAPSHOT      (UINT64_C(1) << 4)
#define AYTHER_CAP_PARSED_SPRITES_V1   (UINT64_C(1) << 5)
#define AYTHER_CAP_AUDIO_WRITES_V1     (UINT64_C(1) << 6)
#define AYTHER_CAP_RASTER_FALLBACK_V1  (UINT64_C(1) << 7)
#define AYTHER_CAP_RECOMPOSE_V1        (UINT64_C(1) << 8)
#define AYTHER_CAP_AUDIO_PROBE_V1      (UINT64_C(1) << 9)
#define AYTHER_CAP_SUBSCRIPTIONS_V1    (UINT64_C(1) << 10)
#define AYTHER_CAP_FRAME_DELTA_V1      (UINT64_C(1) << 11)

/* Observation and control work is opt-in. A requested mask becomes active at
 * the beginning of the next frame; unknown bits are rejected. */
#define AYTHER_SUB_VDP_MEMORY       (UINT32_C(1) << 0)
#define AYTHER_SUB_SPRITE_CAPTURE   (UINT32_C(1) << 1)
#define AYTHER_SUB_RENDER_CONTROLS  (UINT32_C(1) << 2)
#define AYTHER_SUB_RASTER_TRACKING  (UINT32_C(1) << 3)
#define AYTHER_SUB_AUDIO_WRITES     (UINT32_C(1) << 4)
#define AYTHER_SUB_RECOMPOSITION    (UINT32_C(1) << 5)
#define AYTHER_SUB_AUDIO_EVENTS     (UINT32_C(1) << 6)
#define AYTHER_SUB_ALL              UINT32_C(0x7F)

enum ayther_region_id
{
  AYTHER_REGION_VRAM = 1,
  AYTHER_REGION_CRAM,
  AYTHER_REGION_VDP_REGS,
  AYTHER_REGION_VSRAM,
  AYTHER_REGION_LAYER_MASK,
  AYTHER_REGION_SPRITE_SUPPRESS,
  AYTHER_REGION_TILE_SUPPRESS,
  AYTHER_REGION_PLANE_TILE_SUPPRESS,
  AYTHER_REGION_PLANE_SUPPRESS_ACTIVE,
  AYTHER_REGION_LAYER_DIM,
  AYTHER_REGION_AUDIO_WRITES,
  AYTHER_REGION_AUDIO_WRITE_COUNT,
  AYTHER_REGION_PARSED_SPRITES,
  AYTHER_REGION_PARSED_SPRITE_COUNT,
  AYTHER_REGION_AUDIO_MUTE,
  AYTHER_REGION_RASTER_FALLBACK_REASONS,
  AYTHER_REGION_COUNT
};

/* Deprecated compatibility IDs. New frontends should use region IDs and the
 * functions in ayther_interface_v1 instead of mutable direct pointers. */
enum ayther_legacy_memory_id
{
  AYTHER_LEGACY_MEMORY_VRAM                  = 0x003,
  AYTHER_LEGACY_MEMORY_CRAM                  = 0x100,
  AYTHER_LEGACY_MEMORY_VDP_REGS              = 0x101,
  AYTHER_LEGACY_MEMORY_LAYER_MASK            = 0x102,
  AYTHER_LEGACY_MEMORY_SPRITE_SUPPRESS       = 0x103,
  AYTHER_LEGACY_MEMORY_TILE_SUPPRESS         = 0x104,
  AYTHER_LEGACY_MEMORY_PLANE_TILE_SUPPRESS   = 0x105,
  AYTHER_LEGACY_MEMORY_PLANE_SUPPRESS_ACTIVE = 0x106,
  AYTHER_LEGACY_MEMORY_VSRAM                 = 0x107,
  AYTHER_LEGACY_MEMORY_LAYER_DIM             = 0x108,
  AYTHER_LEGACY_MEMORY_AUDIO_WRITES          = 0x109,
  AYTHER_LEGACY_MEMORY_AUDIO_WRITE_COUNT     = 0x10A,
  AYTHER_LEGACY_MEMORY_PARSED_SPRITES        = 0x10B,
  AYTHER_LEGACY_MEMORY_PARSED_SPRITE_COUNT   = 0x10C,
  AYTHER_LEGACY_MEMORY_AUDIO_MUTE            = 0x10D,
  AYTHER_LEGACY_MEMORY_RASTER_DIRTY          = 0x10E
};

#define AYTHER_REGION_ACCESS_READ          (UINT32_C(1) << 0)
#define AYTHER_REGION_ACCESS_CONTROL_WRITE (UINT32_C(1) << 1)
#define AYTHER_REGION_FRAME_SCOPED         (UINT32_C(1) << 2)
#define AYTHER_REGION_NATIVE_ENDIAN        (UINT32_C(1) << 3)
#define AYTHER_REGION_DEPRECATED_LEGACY    (UINT32_C(1) << 4)

#define AYTHER_LAYOUT_RAW_V1         UINT32_C(1)
#define AYTHER_LAYOUT_SPRITE_V1      UINT32_C(1)
#define AYTHER_LAYOUT_AUDIO_WRITE_V1 UINT32_C(1)
#define AYTHER_LAYOUT_AUDIO_EVENT_V1 UINT32_C(1)
#define AYTHER_LAYOUT_FRAME_DELTA_V1 UINT32_C(1)

/* Native-endian in-process layout. Multi-byte fields use host endianness as
 * reported by ayther_interface_v1.host_endianness. Pointers are never stored
 * in captured data. */
typedef struct ayther_sprite_v1
{
  uint16_t yr;
  uint16_t xr;
  uint16_t attr;
  uint8_t w;
  uint8_t h;
  uint8_t sat_idx;
  uint8_t chain_pos;
} ayther_sprite_v1;

typedef struct ayther_audio_write_v1
{
  uint32_t cycle;
  uint16_t addr;
  uint8_t data;
  uint8_t chip;
} ayther_audio_write_v1;

enum ayther_audio_source_v1
{
  AYTHER_AUDIO_SOURCE_FM  = 0,
  AYTHER_AUDIO_SOURCE_PSG = 1,
  AYTHER_AUDIO_SOURCE_DAC = 2,
  AYTHER_AUDIO_SOURCE_PCM = 3
};

enum ayther_audio_event_type_v1
{
  AYTHER_AUDIO_EVENT_RAW_WRITE = 0,
  AYTHER_AUDIO_EVENT_NOTE_ON,
  AYTHER_AUDIO_EVENT_NOTE_OFF,
  AYTHER_AUDIO_EVENT_DAC_START,
  AYTHER_AUDIO_EVENT_DAC_STOP,
  AYTHER_AUDIO_EVENT_PATCH,
  AYTHER_AUDIO_EVENT_PITCH,
  AYTHER_AUDIO_EVENT_VOLUME,
  AYTHER_AUDIO_EVENT_RESET,
  AYTHER_AUDIO_EVENT_STATE_LOAD,
  AYTHER_AUDIO_EVENT_FRAME
};

typedef struct ayther_audio_voice_v1
{
  uint8_t op_tl[4];
  uint8_t op_ar[4];
  uint8_t op_dr[4];
  uint8_t op_sr[4];
  uint8_t op_rr[4];
  uint8_t op_mul[4];
  uint8_t op_dt[4];
  uint8_t algorithm;
  uint8_t feedback;
  uint8_t ams;
  uint8_t fms;
  uint8_t pan;
  uint32_t block_fnum;
} ayther_audio_voice_v1;

/* `group` is non-zero only for logical trigger anchors (NOTE_ON and
 * DAC_START). A fixed coincidence window is measured from the first anchor in
 * a group; raw writes, frame markers and release events always use group 0. */
typedef struct ayther_audio_event_v1
{
  uint64_t t_global;
  uint32_t t_frame;
  uint32_t t_cycles;
  uint8_t source;
  uint8_t type;
  uint8_t channel;
  uint8_t schema;
  uint32_t group;
  union {
    struct {
      uint32_t reg;
      uint32_t data;
    };
    struct {
      ayther_audio_voice_v1 voice;
      uint64_t voice_hash;
      uint64_t timbre_hash;
    };
  };
} ayther_audio_event_v1;

#define AYTHER_AUDIO_TRANSPORT_CALLBACK_ACTIVE (UINT32_C(1) << 0)
#define AYTHER_AUDIO_TRANSPORT_OBSERVATION_ACTIVE (UINT32_C(1) << 1)

/* Values are a concurrent snapshot. `capacity` is the effective number of
 * events (one slot is reserved by the SPSC full/empty protocol). The dropped
 * counter saturates at UINT32_MAX instead of wrapping. */
typedef struct ayther_audio_transport_stats_v1
{
  uint32_t struct_size;
  uint32_t transport_version;
  uint32_t event_size;
  uint32_t capacity;
  uint32_t pending;
  uint32_t high_water_mark;
  uint32_t dropped_events;
  uint32_t flags;
} ayther_audio_transport_stats_v1;

typedef struct ayther_region_info_v1
{
  uint32_t struct_size;
  uint32_t region_id;
  uint32_t data_version;
  uint32_t element_size;
  uint32_t capacity;
  uint32_t byte_size;
  uint32_t access_flags;
  uint32_t legacy_memory_id;
} ayther_region_info_v1;

#define AYTHER_SNAPSHOT_CONTENT_LOADED (UINT32_C(1) << 0)
#define AYTHER_SNAPSHOT_FRAME_ACTIVE   (UINT32_C(1) << 1)

#define AYTHER_OVERFLOW_PARSED_SPRITES (UINT32_C(1) << 0)
#define AYTHER_OVERFLOW_AUDIO_WRITES   (UINT32_C(1) << 1)

typedef struct ayther_frame_snapshot_v1
{
  uint32_t struct_size;
  uint32_t snapshot_version;
  uint64_t snapshot_generation;
  uint64_t frame_generation;
  uint32_t flags;
  uint32_t overflow_flags;
  uint32_t fallback_reasons;
  uint32_t parsed_sprite_count;
  uint32_t audio_write_count;
  uint32_t reserved0;
} ayther_frame_snapshot_v1;

typedef struct ayther_subscription_state_v1
{
  uint32_t struct_size;
  uint32_t state_version;
  uint32_t supported_mask;
  uint32_t active_mask;
  uint32_t requested_mask;
  uint32_t reserved0;
  uint64_t activation_frame;
} ayther_subscription_state_v1;

typedef int32_t (AYTHER_CALL *ayther_query_region_v1_fn)(
    uint32_t region_id, ayther_region_info_v1 *out, uint32_t out_size);

typedef int32_t (AYTHER_CALL *ayther_read_region_v1_fn)(
    uint32_t region_id, uint32_t offset, void *out, uint32_t byte_count,
    uint64_t expected_generation, uint64_t *actual_generation);

typedef int32_t (AYTHER_CALL *ayther_write_control_v1_fn)(
    uint32_t region_id, uint32_t offset, const void *data,
    uint32_t byte_count, uint64_t expected_generation,
    uint64_t *new_generation);

typedef int32_t (AYTHER_CALL *ayther_capture_snapshot_v1_fn)(
    ayther_frame_snapshot_v1 *out, uint32_t out_size);

typedef int32_t (AYTHER_CALL *ayther_recompose_frame_v1_fn)(
    uint16_t *out_pixels, uint32_t pixel_capacity, uint32_t flags,
    uint32_t *out_width, uint32_t *out_height);

typedef int32_t (AYTHER_CALL *ayther_poll_audio_events_v1_fn)(
    ayther_audio_event_v1 *out, uint32_t event_capacity,
    uint32_t *out_event_count);

typedef int32_t (AYTHER_CALL *ayther_get_audio_transport_stats_v1_fn)(
    ayther_audio_transport_stats_v1 *out, uint32_t out_size);

typedef int32_t (AYTHER_CALL *ayther_get_subscriptions_v1_fn)(
    ayther_subscription_state_v1 *out, uint32_t out_size);

typedef int32_t (AYTHER_CALL *ayther_set_subscriptions_v1_fn)(
    uint32_t requested_mask);

typedef struct ayther_frame_delta_v1
{
  uint32_t struct_size;
  uint32_t delta_version;
  uint64_t frame_generation;
  uint32_t raster_event_count;
  uint32_t parsed_sprite_count;
  uint32_t audio_write_count;
  uint32_t reserved0;
  uint8_t dirty_patterns[2048];
} ayther_frame_delta_v1;

typedef int32_t (AYTHER_CALL *ayther_poll_frame_delta_v1_fn)(
    ayther_frame_delta_v1 *out, uint32_t out_size);

typedef struct ayther_interface_v1
{
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t capabilities;
  uint32_t host_endianness;
  uint32_t pointer_size;
  uint32_t region_info_size;
  uint32_t frame_snapshot_size;
  uint32_t sprite_size;
  uint32_t audio_write_size;
  const char *build_id;
  uint32_t build_id_size;
  uint32_t reserved0;
  ayther_query_region_v1_fn query_region;
  ayther_read_region_v1_fn read_region;
  ayther_write_control_v1_fn write_control;
  ayther_capture_snapshot_v1_fn capture_snapshot;
  ayther_recompose_frame_v1_fn recompose_frame;
  uint32_t audio_event_size;
  uint32_t audio_transport_stats_size;
  ayther_poll_audio_events_v1_fn poll_audio_events;
  ayther_get_audio_transport_stats_v1_fn get_audio_transport_stats;
  uint32_t subscription_state_size;
  uint32_t reserved1;
  ayther_get_subscriptions_v1_fn get_subscriptions;
  ayther_set_subscriptions_v1_fn set_subscriptions;
  uint32_t frame_delta_size;
  ayther_poll_frame_delta_v1_fn poll_frame_delta;
} ayther_interface_v1;

typedef const ayther_interface_v1 *(AYTHER_CALL *ayther_get_interface_fn)(
    uint32_t requested_version);

AYTHER_API const ayther_interface_v1 *AYTHER_CALL ayther_get_interface(
    uint32_t requested_version);

AYTHER_API int32_t AYTHER_CALL ayther_recompose_multilayer(
    uint16_t *out_bg_a, uint16_t *out_bg_b, uint16_t *out_window,
    uint16_t *out_sprites, uint16_t *out_composite,
    uint32_t pixel_capacity, uint32_t flags,
    uint32_t *out_width, uint32_t *out_height);

#ifdef __cplusplus
}
#endif

#endif /* AYTHER_API_H */
