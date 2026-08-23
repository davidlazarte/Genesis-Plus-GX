/* Deterministic end-to-end libretro replay using a generated Mega Drive ROM. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE library_t;
#else
#include <dlfcn.h>
#include <time.h>
typedef void *library_t;
#endif

#include "libretro.h"
#include "ayther/ayther_api.h"
#include "ayther_raster.h"   /* #27: capacidad del journal raster */
#include "generated_rom.h"

#define REPLAY_FRAMES 120u
#define BOOTSTRAP_FRAMES 8u
#define EVENT_BATCH 256u
#define MAX_RECOMPOSE_PIXELS (720u * 576u)
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)
#define FIXTURE_CONFIGURATION \
  "region=auto;overscan=disabled;aspect=auto;sprite_limit=hardware;" \
"ntsc_filter=disabled;audio_filter=disabled;pixel_format=rgb565;" \
"audio_video=enabled;subscriptions=0x7f"

struct core_api
{
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample)(retro_audio_sample_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  void (*init)(void);
  void (*deinit)(void);
  void (*reset)(void);
  bool (*load_game)(const struct retro_game_info *game);
  void (*unload_game)(void);
  void (*run)(void);
  size_t (*serialize_size)(void);
  bool (*serialize)(void *data, size_t size);
  bool (*unserialize)(const void *data, size_t size);
  /* #33: la superficie legacy de memoria no tenia ni un test, empezando por
     RETRO_MEMORY_VIDEO_RAM, que es el delta que da nombre a esta rama. */
  void *(*get_memory_data)(unsigned id);
  size_t (*get_memory_size)(unsigned id);
  ayther_get_interface_fn get_ayther_interface;
  const ayther_interface_v1 *ayther;
  /* #32: se resuelve por el DESCRIPTOR (ABI 1.2). Cuando salia del export
     directo, quitar ese simbolo del perfil estandar habria dejado el test de
     aislamiento del raster replay sin ejecutarse, en silencio y en verde. */
  ayther_recompose_multilayer_v1_fn recompose_multilayer;
};

struct frame_record
{
  uint64_t video_hash;
  uint64_t audio_hash;
  uint64_t state_hash;
  uint64_t telemetry_hash;
  uint64_t digest;
  uint32_t input_mask;
  uint32_t width;
  uint32_t height;
  uint32_t audio_frames;
  uint32_t fallback_reasons;
  uint32_t sprite_count;
  uint32_t audio_write_count;
  uint32_t event_count;
  uint32_t different_pixels;
  uint8_t false_clean;
  uint8_t recompose_unavailable;
};

struct replay_summary
{
  uint64_t video_hash;
  uint64_t audio_hash;
  uint64_t state_hash;
  uint64_t telemetry_hash;
  uint64_t input_hash;
  uint64_t configuration_hash;
  uint64_t replay_hash;
  uint64_t total_events;
  uint32_t fallback_frames;
  uint32_t false_clean_frames;
  uint32_t unavailable_without_reason;
  uint32_t replay_mismatches;
  uint32_t max_sprites;
  uint32_t max_audio_writes;
  uint32_t max_audio_frames;
};

struct benchmark_stats
{
  double minimum;
  double p50;
  double p95;
  double p99;
  double maximum;
};

struct profile_result
{
  uint64_t video_hash;
  uint64_t audio_hash;
  uint64_t state_hash;
  uint64_t input_hash;
  struct benchmark_stats timing;
  uint64_t core_binary_bytes;
  size_t state_size;
};

struct golden
{
  unsigned int schema;
  unsigned int frames;
  uint64_t video_hash;
  uint64_t audio_hash;
  uint64_t state_hash;
  uint64_t telemetry_hash;
  uint64_t input_hash;
  uint64_t configuration_hash;
  uint64_t replay_hash;
  unsigned int fallback_frames;
  unsigned int false_clean_frames;
};

static uint8_t *video_pixels;
static size_t video_capacity;
static size_t video_size;
static unsigned int video_width;
static unsigned int video_height;
static uint64_t current_video_hash;
static uint64_t current_audio_hash;
static uint32_t current_audio_frames;
static uint32_t current_input_mask;
static int current_video_valid;
static int requested_audio_video = 3;
static struct retro_game_info_ext generated_game_info;
static char reference_state_path[1024];

static uint64_t hash_byte(uint64_t hash, unsigned int value)
{
  hash ^= (uint8_t)value;
  return hash * FNV_PRIME;
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
  hash = hash_byte(hash, value);
  return hash_byte(hash, value >> 8);
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
  unsigned int index;
  for (index = 0; index < 4u; ++index)
    hash = hash_byte(hash, value >> (index * 8u));
  return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
  unsigned int index;
  for (index = 0; index < 8u; ++index)
    hash = hash_byte(hash, (unsigned int)(value >> (index * 8u)));
  return hash;
}

static uint64_t hash_bytes(uint64_t hash, const uint8_t *data, size_t size)
{
  size_t index;
  for (index = 0; index < size; ++index)
    hash = hash_byte(hash, data[index]);
  return hash;
}

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
  return (double)value.tv_sec * 1000000000.0 + (double)value.tv_nsec;
#endif
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

static library_t library_open(const char *path)
{
#if defined(_WIN32)
  return LoadLibraryA(path);
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *library_symbol(library_t library, const char *name)
{
#if defined(_WIN32)
  return (void *)(uintptr_t)GetProcAddress(library, name);
#else
  return dlsym(library, name);
#endif
}

static void library_close(library_t library)
{
#if defined(_WIN32)
  FreeLibrary(library);
#else
  dlclose(library);
#endif
}

static void library_error(const char *path)
{
#if defined(_WIN32)
  fprintf(stderr, "cannot load %s (Windows error %lu)\n",
          path, (unsigned long)GetLastError());
#else
  const char *error = dlerror();
  fprintf(stderr, "cannot load %s: %s\n", path,
          error ? error : "unknown error");
#endif
}

static bool environment_callback(unsigned command, void *data)
{
  switch (command)
  {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return data && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      if (data) *(bool *)data = true;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (data) *(bool *)data = false;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      if (data) *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (data) *(int *)data = requested_audio_video;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      if (data) *(const struct retro_game_info_ext **)data = &generated_game_info;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
      if (data)
      {
        struct retro_variable *variable = (struct retro_variable *)data;
        variable->value = NULL;
        if (!variable->key) return false;
        if (!strcmp(variable->key, "genesis_plus_gx_region_detect"))
          variable->value = "auto";
        else if (!strcmp(variable->key, "genesis_plus_gx_overscan"))
          variable->value = "disabled";
        else if (!strcmp(variable->key, "genesis_plus_gx_aspect_ratio"))
          variable->value = "auto";
        else if (!strcmp(variable->key, "genesis_plus_gx_no_sprite_limit"))
          variable->value = "disabled";
        else if (!strcmp(variable->key,
                         "genesis_plus_gx_blargg_ntsc_filter"))
          variable->value = "disabled";
        else if (!strcmp(variable->key, "genesis_plus_gx_audio_filter"))
          variable->value = "disabled";
        return variable->value != NULL;
      }
      return false;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      return true;
    default:
      return false;
  }
}

static void video_callback(const void *data, unsigned width, unsigned height,
                           size_t pitch)
{
  size_t row;
  size_t row_bytes = (size_t)width * sizeof(uint16_t);
  size_t required = row_bytes * height;
  uint8_t *next;

  if (!data || !width || !height || pitch < row_bytes)
    return;
  if (required > video_capacity)
  {
    next = (uint8_t *)realloc(video_pixels, required);
    if (!next)
      return;
    video_pixels = next;
    video_capacity = required;
  }

  current_video_hash = FNV_OFFSET;
  for (row = 0; row < height; ++row)
  {
    const uint16_t *source =
      (const uint16_t *)((const uint8_t *)data + row * pitch);
    uint16_t *destination = (uint16_t *)(video_pixels + row * row_bytes);
    size_t column;
    memcpy(destination, source, row_bytes);
    for (column = 0; column < width; ++column)
      current_video_hash = hash_u16(current_video_hash, source[column]);
  }
  video_width = width;
  video_height = height;
  video_size = required;
  current_video_valid = 1;
}

static void hash_audio_sample(int16_t left, int16_t right)
{
  current_audio_hash = hash_u16(current_audio_hash, (uint16_t)left);
  current_audio_hash = hash_u16(current_audio_hash, (uint16_t)right);
  ++current_audio_frames;
}

static void audio_sample_callback(int16_t left, int16_t right)
{
  hash_audio_sample(left, right);
}

static size_t audio_batch_callback(const int16_t *data, size_t frames)
{
  size_t index;
  for (index = 0; index < frames; ++index)
    hash_audio_sample(data[index * 2u], data[index * 2u + 1u]);
  return frames;
}

static void input_poll_callback(void)
{
}

static int16_t input_state_callback(unsigned port, unsigned device,
                                    unsigned index, unsigned id)
{
  if (port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0)
    return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return (int16_t)current_input_mask;
  return id < 16u && (current_input_mask & (1u << id)) ? 1 : 0;
}

static int load_function(library_t library, const char *name,
                         void *destination, size_t destination_size)
{
  void *symbol = library_symbol(library, name);
  if (!symbol || destination_size != sizeof(symbol))
  {
    fprintf(stderr, "missing or incompatible dynamic export: %s\n", name);
    return 0;
  }
  memcpy(destination, &symbol, sizeof(symbol));
  return 1;
}

#define LOAD_API(api, library, field, symbol) \
  load_function((library), (symbol), &(api)->field, sizeof((api)->field))

static int load_api(library_t library, struct core_api *api,
                    int require_ayther)
{
  memset(api, 0, sizeof(*api));
  if (!(LOAD_API(api, library, set_environment, "retro_set_environment") &&
        LOAD_API(api, library, set_video_refresh, "retro_set_video_refresh") &&
        LOAD_API(api, library, set_audio_sample, "retro_set_audio_sample") &&
        LOAD_API(api, library, set_audio_sample_batch,
                 "retro_set_audio_sample_batch") &&
        LOAD_API(api, library, set_input_poll, "retro_set_input_poll") &&
        LOAD_API(api, library, set_input_state, "retro_set_input_state") &&
        LOAD_API(api, library, init, "retro_init") &&
        LOAD_API(api, library, deinit, "retro_deinit") &&
        LOAD_API(api, library, reset, "retro_reset") &&
        LOAD_API(api, library, load_game, "retro_load_game") &&
        LOAD_API(api, library, unload_game, "retro_unload_game") &&
        LOAD_API(api, library, run, "retro_run") &&
        LOAD_API(api, library, serialize_size, "retro_serialize_size") &&
        LOAD_API(api, library, serialize, "retro_serialize") &&
        LOAD_API(api, library, unserialize, "retro_unserialize") &&
        LOAD_API(api, library, get_memory_data, "retro_get_memory_data") &&
        LOAD_API(api, library, get_memory_size, "retro_get_memory_size")))
    return 0;

  if (!require_ayther)
    return 1;
  if (!LOAD_API(api, library, get_ayther_interface, "ayther_get_interface"))
    return 0;


  api->ayther = api->get_ayther_interface(AYTHER_ABI_VERSION_1_0);
  if (api->ayther && AYTHER_IFACE_HAS(api->ayther, recompose_multilayer))
    api->recompose_multilayer = api->ayther->recompose_multilayer;
  if (!api->ayther ||
      !(api->ayther->capabilities & AYTHER_CAP_FRAME_SNAPSHOT) ||
      !(api->ayther->capabilities & AYTHER_CAP_RECOMPOSE_V1) ||
      !(api->ayther->capabilities & AYTHER_CAP_AUDIO_PROBE_V1) ||
      !(api->ayther->capabilities & AYTHER_CAP_SUBSCRIPTIONS_V1) ||
      !api->ayther->capture_snapshot || !api->ayther->recompose_frame ||
      !api->ayther->poll_audio_events || !api->ayther->get_subscriptions ||
      !api->ayther->set_subscriptions)
  {
    fprintf(stderr, "AYTHER ABI v1 lacks deterministic frame capabilities\n");
    return 0;
  }
  return 1;
}

static void install_callbacks(const struct core_api *api)
{
  api->set_environment(environment_callback);
  api->set_video_refresh(video_callback);
  api->set_audio_sample(audio_sample_callback);
  api->set_audio_sample_batch(audio_batch_callback);
  api->set_input_poll(input_poll_callback);
  api->set_input_state(input_state_callback);
}

static uint32_t input_for_frame(unsigned int frame)
{
  uint32_t input = 0;
  if ((frame / 12u) & 1u) input |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
  if ((frame / 20u) & 1u) input |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
  if ((frame % 17u) < 3u) input |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
  if (frame == 31u || frame == 79u)
    input |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
  return input;
}

static uint64_t hash_audio_event(uint64_t hash,
                                 const ayther_audio_event_v1 *event)
{
  unsigned int index;
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
  for (index = 0; index < 4u; ++index)
  {
    hash = hash_byte(hash, event->voice.op_tl[index]);
    hash = hash_byte(hash, event->voice.op_ar[index]);
    hash = hash_byte(hash, event->voice.op_dr[index]);
    hash = hash_byte(hash, event->voice.op_sr[index]);
    hash = hash_byte(hash, event->voice.op_rr[index]);
    hash = hash_byte(hash, event->voice.op_mul[index]);
    hash = hash_byte(hash, event->voice.op_dt[index]);
  }
  hash = hash_byte(hash, event->voice.algorithm);
  hash = hash_byte(hash, event->voice.feedback);
  hash = hash_byte(hash, event->voice.ams);
  hash = hash_byte(hash, event->voice.fms);
  hash = hash_byte(hash, event->voice.pan);
  hash = hash_u32(hash, event->voice.block_fnum);
  hash = hash_u64(hash, event->voice_hash);
  return hash_u64(hash, event->timbre_hash);
}

static void drain_audio_events(const struct core_api *api)
{
  ayther_audio_event_v1 events[EVENT_BATCH];
  uint32_t count;
  if (!(api->ayther->capabilities & AYTHER_CAP_AUDIO_PROBE_V1) ||
      !api->ayther->poll_audio_events)
    return;
  do
  {
    count = 0;
    if (api->ayther->poll_audio_events(events, EVENT_BATCH, &count) !=
        AYTHER_STATUS_OK)
      break;
  } while (count == EVENT_BATCH);
}

static uint32_t collect_audio_events(const struct core_api *api,
                                     uint64_t *hash)
{
  ayther_audio_event_v1 events[EVENT_BATCH];
  uint32_t total = 0;
  uint32_t count;
  uint32_t index;
  *hash = FNV_OFFSET;
  if (!(api->ayther->capabilities & AYTHER_CAP_AUDIO_PROBE_V1) ||
      !api->ayther->poll_audio_events)
    return 0;
  do
  {
    count = 0;
    if (api->ayther->poll_audio_events(events, EVENT_BATCH, &count) !=
        AYTHER_STATUS_OK)
      return total;
    for (index = 0; index < count; ++index)
      *hash = hash_audio_event(*hash, &events[index]);
    total += count;
  } while (count == EVENT_BATCH);
  return total;
}

static uint32_t compare_recomposition(const struct core_api *api,
                                      uint16_t *recomposed,
                                      uint32_t *unavailable)
{
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t different = 0;
  size_t pixels;
  size_t index;
  int32_t status = api->ayther->recompose_frame(
    recomposed, MAX_RECOMPOSE_PIXELS, 0, &width, &height);
  *unavailable = status != AYTHER_STATUS_OK;
  if (status != AYTHER_STATUS_OK || !video_size ||
      width != video_width || height != video_height)
    return 0;
  pixels = (size_t)width * height;
  for (index = 0; index < pixels; ++index)
    different += recomposed[index] != ((uint16_t *)video_pixels)[index];
  return different;
}

/* #26: el cache de recomposicion tiene que estar indexado tambien por el
 * contenido de las regiones de control.
 *
 * El defecto que cubre: escribir una mascara entre dos recomposiciones del
 * MISMO frame devolvia la imagen anterior, porque la clave era solo
 * (generacion de frame, flags, layer_mask). El Lab hace exactamente esa
 * secuencia con el emulador pausado, asi que el bug se veia como "la UI no
 * responde" sin ningun error.
 *
 * La prueba alterna A -> B -> A -> A sobre un frame fijo:
 *   - B tiene que dar pixeles distintos de A   (con el bug, daba los de A);
 *   - el segundo A tiene que reproducir el primero bit a bit (fallo, porque
 *     el cache quedo con B) — que el resultado dependa SOLO del estado y no
 *     del orden de las llamadas;
 *   - el tercer A tiene que ser ACIERTO de cache y byte-identico, para que
 *     arreglar la correccion no equivalga a apagar el cache.
 * Se afirma sobre los contadores y no sobre tiempos: cronometrar en un runner
 * compartido mide ruido, no el mecanismo. */
static int recompose_probe(const struct core_api *api, uint16_t *out,
                           uint32_t *pixels)
{
  uint32_t width = 0;
  uint32_t height = 0;
  if (api->ayther->recompose_frame(out, MAX_RECOMPOSE_PIXELS, 0,
                                   &width, &height) != AYTHER_STATUS_OK)
    return 0;
  *pixels = width * height;
  return *pixels != 0;
}

static int recompose_stats(const struct core_api *api,
                           ayther_recompose_stats_v1 *out)
{
  memset(out, 0, sizeof(*out));
  out->struct_size = sizeof(*out);
  return api->ayther->get_recompose_stats(out, sizeof(*out)) ==
         AYTHER_STATUS_OK;
}

static int check_recompose_control_cache(const struct core_api *api)
{
  /* Un control por caso: el valor "activo" y el neutro al que se vuelve. La
     lista arranca con layer_dim porque atenua TODO pixel que no sea sprite:
     cualquier frame con algo dibujado cambia, asi que el caso no depende de
     que la ROM sintetica tenga sprites o tiles en un lugar concreto. */
  static const struct {
    const char *name;
    uint32_t region;
    uint32_t offset;
    uint8_t active;
  } cases[] = {
    { "layer_dim",            AYTHER_REGION_LAYER_DIM,       0, 1 },
    { "sprite_suppress",      AYTHER_REGION_SPRITE_SUPPRESS, 0, 0xFF },
    { "tile_suppress",        AYTHER_REGION_TILE_SUPPRESS,   0, 0xFF },
    { "plane_tile_suppress",  AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, 0xFF }
  };
  static uint16_t px_a1[MAX_RECOMPOSE_PIXELS];
  static uint16_t px_b[MAX_RECOMPOSE_PIXELS];
  static uint16_t px_a2[MAX_RECOMPOSE_PIXELS];
  static uint16_t px_a3[MAX_RECOMPOSE_PIXELS];
  const uint8_t neutral = 0;
  size_t c;
  int ok = 1;

  if (!api->ayther->get_recompose_stats)
  {
    fprintf(stderr, "core lacks get_recompose_stats (ABI 1.1)\n");
    return 0;
  }

  for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
  {
    ayther_recompose_stats_v1 s0, s1, s2, s3;
    uint32_t n_a1 = 0, n_b = 0, n_a2 = 0, n_a3 = 0;
    uint32_t changed;

    if (!recompose_probe(api, px_a1, &n_a1) || !recompose_stats(api, &s0))
    {
      fprintf(stderr, "recompose cache: baseline failed (%s)\n",
              cases[c].name);
      return 0;
    }

    if (api->ayther->write_control(cases[c].region, cases[c].offset,
          &cases[c].active, 1, AYTHER_GENERATION_ANY, NULL) !=
        AYTHER_STATUS_OK)
    {
      fprintf(stderr, "recompose cache: write_control failed (%s)\n",
              cases[c].name);
      return 0;
    }
    if (!recompose_probe(api, px_b, &n_b) || !recompose_stats(api, &s1))
    {
      fprintf(stderr, "recompose cache: probe failed with %s active\n",
              cases[c].name);
      return 0;
    }

    if (s1.controls_fingerprint == s0.controls_fingerprint)
    {
      fprintf(stderr, "recompose cache: fingerprint ignored %s\n",
              cases[c].name);
      ok = 0;
    }
    if (s1.single_hits != s0.single_hits)
    {
      fprintf(stderr, "recompose cache: stale hit after writing %s\n",
              cases[c].name);
      ok = 0;
    }

    /* Volver al estado neutro y recomponer: tiene que reproducir el primer
       resultado exactamente. Aca es donde el bug original se manifestaba al
       reves (el cache seguia sirviendo B). */
    if (api->ayther->write_control(cases[c].region, cases[c].offset,
          &neutral, 1, AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK)
    {
      fprintf(stderr, "recompose cache: cannot restore %s to neutral\n",
              cases[c].name);
      return 0;
    }
    if (!recompose_probe(api, px_a2, &n_a2) || !recompose_stats(api, &s2))
    {
      fprintf(stderr, "recompose cache: probe failed after restoring %s\n",
              cases[c].name);
      return 0;
    }
    if (n_a2 != n_a1 || memcmp(px_a1, px_a2, n_a1 * sizeof(uint16_t)) != 0)
    {
      fprintf(stderr,
              "recompose cache: result depends on call order (%s)\n",
              cases[c].name);
      ok = 0;
    }

    /* Sin cambios: ahora SI tiene que ser acierto, y byte-identico. */
    if (!recompose_probe(api, px_a3, &n_a3) || !recompose_stats(api, &s3))
    {
      fprintf(stderr, "recompose cache: repeat probe failed (%s)\n",
              cases[c].name);
      return 0;
    }
    if (s3.single_hits != s2.single_hits + 1)
    {
      fprintf(stderr, "recompose cache: no hit for unchanged state (%s)\n",
              cases[c].name);
      ok = 0;
    }
    if (n_a3 != n_a1 || memcmp(px_a1, px_a3, n_a1 * sizeof(uint16_t)) != 0)
    {
      fprintf(stderr, "recompose cache: hit served wrong pixels (%s)\n",
              cases[c].name);
      ok = 0;
    }

    changed = 0;
    if (n_b == n_a1)
    {
      uint32_t i;
      for (i = 0; i < n_a1; ++i)
        changed += px_b[i] != px_a1[i];
    }
    /* layer_dim es el unico caso con efecto garantizado sobre cualquier frame
       no vacio; los demas dependen de donde caiga el contenido de la ROM, asi
       que su valor esta en las afirmaciones de arriba (huella, fallo, orden). */
    if (c == 0 && changed == 0)
    {
      fprintf(stderr,
              "recompose cache: %s did not change any pixel\n",
              cases[c].name);
      ok = 0;
    }
    if (!ok)
      return 0;
  }
  return 1;
}

/* #27: el raster replay no puede dejar rastro.
 *
 * `ayther_core_recompose_multilayer` reproduce los eventos del frame sobre el
 * estado real del VDP (CRAM, VSRAM, la tabla de hscroll en VRAM, los registros
 * y todo el estado DERIVADO que se calcula al escribirlos) y despues lo
 * devuelve a su lugar. Es una funcion declarada de solo lectura, y de eso
 * dependen los tres pases del replay determinista: si dejara una sola word
 * movida, el frame siguiente divergiria y el sintoma apareceria lejos de la
 * causa.
 *
 * Se verifican las tres cosas que el replay toca: la memoria del VDP, los
 * registros, y el bitmask publico de motivos de fallback -que el replay
 * anotaba desde adentro, mutando estado publico desde una lectura-. */
static int check_raster_replay_isolation(const struct core_api *api,
                                         uint16_t *scratch)
{
#define ISOLATION_SNAPSHOT_BYTES 0x10000u
  static uint8_t before[4][ISOLATION_SNAPSHOT_BYTES];
  static uint8_t after[ISOLATION_SNAPSHOT_BYTES];
  static uint16_t layer_a[MAX_RECOMPOSE_PIXELS];
  static uint16_t layer_b[MAX_RECOMPOSE_PIXELS];
  static const struct { uint32_t id; uint32_t size; const char *name; } regions[] = {
    { AYTHER_REGION_VRAM,     0x10000, "vram" },
    { AYTHER_REGION_CRAM,     0x80,    "cram" },
    { AYTHER_REGION_VSRAM,    0x80,    "vsram" },
    { AYTHER_REGION_VDP_REGS, 0x20,    "vdp regs" }
  };
  ayther_recompose_stats_v1 stats_before, stats_after;
  uint32_t fallback_before = 0;
  uint32_t fallback_after = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  size_t r;
  int ok = 1;

  if (!api->recompose_multilayer)
    return 1;   /* perfil sin el símbolo: nada que verificar */

  /* TODO se fotografia antes de UNA sola recomposicion. Con una llamada por
     region, la primera hacia el replay y las siguientes se servian del cache
     multicapa: el test pasaba sin haber ejecutado nunca el codigo que dice
     vigilar. */
  for (r = 0; r < sizeof(regions) / sizeof(regions[0]); ++r)
  {
    if (api->ayther->read_region(regions[r].id, 0, before[r], regions[r].size,
          AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK)
    {
      fprintf(stderr, "raster isolation: cannot read %s\n", regions[r].name);
      return 0;
    }
  }
  if (api->ayther->read_region(AYTHER_REGION_RASTER_FALLBACK_REASONS, 0,
        &fallback_before, sizeof(fallback_before), AYTHER_GENERATION_ANY,
        NULL) != AYTHER_STATUS_OK ||
      !recompose_stats(api, &stats_before))
    return 0;

  /* Dos capas y el composite: el camino que ejercita el replay completo,
     incluido el bucle por linea que aplica los eventos. */
  {
    int32_t status = api->recompose_multilayer(layer_a, layer_b, NULL, NULL,
          scratch, MAX_RECOMPOSE_PIXELS, 0, &width, &height);
    if (status != AYTHER_STATUS_OK)
    {
      /* Un error temprano tambien deja el replay sin ejecutar. Sin este
         chequeo el test daria verde sin haber mirado nunca lo que vigila. */
      fprintf(stderr,
              "raster isolation: multilayer recomposition failed (%d)\n",
              (int)status);
      return 0;
    }
  }

  if (!recompose_stats(api, &stats_after))
  {
    fprintf(stderr, "raster isolation: recompose stats unavailable\n");
    return 0;
  }
  if (stats_after.multilayer_hits != stats_before.multilayer_hits)
  {
    /* Si la llamada se sirvio del cache no se ejecuto el replay, y entonces
       este test no verifico nada. Decirlo es mejor que dar un verde vacio. */
    fprintf(stderr,
            "raster isolation: the probe hit the cache and never replayed\n");
    return 0;
  }

  for (r = 0; r < sizeof(regions) / sizeof(regions[0]); ++r)
  {
    if (api->ayther->read_region(regions[r].id, 0, after, regions[r].size,
          AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK)
      return 0;
    if (memcmp(before[r], after, regions[r].size) != 0)
    {
      fprintf(stderr, "raster isolation: recomposition modified %s\n",
              regions[r].name);
      ok = 0;
    }
  }
  if (api->ayther->read_region(AYTHER_REGION_RASTER_FALLBACK_REASONS, 0,
        &fallback_after, sizeof(fallback_after), AYTHER_GENERATION_ANY,
        NULL) != AYTHER_STATUS_OK)
    return 0;
  if (fallback_after != fallback_before)
  {
    fprintf(stderr,
            "raster isolation: recomposition changed fallback reasons "
"(%u -> %u)\n", fallback_before, fallback_after);
    ok = 0;
  }
  return ok;
#undef ISOLATION_SNAPSHOT_BYTES
}

/* #27: contabilidad del journal. Un desborde silencioso dejaba que la
 * recomposicion reprodujera un prefijo del frame y devolviera exito, que es
 * justo lo que un frontend no puede detectar solo. */
static int check_raster_journal_accounting(const struct core_api *api)
{
  ayther_frame_delta_v1 delta;
  memset(&delta, 0, sizeof(delta));
  delta.struct_size = sizeof(delta);
  if (api->ayther->poll_frame_delta(&delta, sizeof(delta)) != AYTHER_STATUS_OK)
  {
    fprintf(stderr, "journal accounting: frame delta unavailable\n");
    return 0;
  }
  if (delta.raster_event_count > AYTHER_RASTER_JOURNAL_MAX)
  {
    fprintf(stderr, "journal accounting: count %u exceeds capacity\n",
            delta.raster_event_count);
    return 0;
  }
  /* El journal solo guarda lo REPRODUCIBLE, asi que en esta ROM -que escribe
     VRAM durante el display activo todo el tiempo- tiene que quedar lejos del
     tope. Cuando se llenaba con eventos que el replay ni mira, desbordaba en
     los 120 frames y perdia los que si importaban. */
  if (delta.raster_events_dropped != 0)
  {
    fprintf(stderr,
            "journal accounting: %u events dropped on the fixture\n",
            delta.raster_events_dropped);
    return 0;
  }
  fprintf(stderr, "raster journal: %u/%u events, %u dropped\n",
          delta.raster_event_count, (unsigned)AYTHER_RASTER_JOURNAL_MAX,
          delta.raster_events_dropped);
  return 1;
}

/* #33: la superficie de memoria legacy no tenia cobertura.
 *
 * Empezando por `RETRO_MEMORY_VIDEO_RAM`, que es el delta que da nombre a esta
 * rama: upstream devuelve NULL y el fork expone los 64 KB de VRAM. Que ese
 * puntero siga siendo valido y del mismo tamano DESPUES de un reset o de cargar
 * un savestate es justo lo que un frontend asume sin poder verificarlo -lo
 * cachea una vez al cargar el core- y lo que nadie estaba comprobando.
 *
 * La estabilidad del puntero importa mas de lo que parece: si `retro_reset`
 * reasignara los buffers, el frontend seguiria leyendo memoria liberada y el
 * sintoma serian graficos corruptos intermitentes, lejos de la causa. */
static int check_memory_regions(const struct core_api *api,
                                const void *checkpoint, size_t state_size)
{
  static const struct { unsigned id; size_t size; const char *name; } expected[] = {
    { RETRO_MEMORY_VIDEO_RAM,                  0x10000, "VIDEO_RAM (vram)" },
    { AYTHER_LEGACY_MEMORY_CRAM,               0x80,    "CRAM" },
    { AYTHER_LEGACY_MEMORY_VDP_REGS,           0x20,    "VDP regs" },
    { AYTHER_LEGACY_MEMORY_VSRAM,              0x80,    "VSRAM" },
    { AYTHER_LEGACY_MEMORY_LAYER_MASK,         1,       "layer mask" },
    { AYTHER_LEGACY_MEMORY_SPRITE_SUPPRESS,    16,      "sprite suppress" },
    { AYTHER_LEGACY_MEMORY_TILE_SUPPRESS,      512,     "tile suppress" },
    { AYTHER_LEGACY_MEMORY_PLANE_TILE_SUPPRESS, 3 * 1024, "plane tile suppress" },
    { AYTHER_LEGACY_MEMORY_PLANE_SUPPRESS_ACTIVE, 1,   "plane suppress active" },
    { AYTHER_LEGACY_MEMORY_LAYER_DIM,          1,       "layer dim" },
    { AYTHER_LEGACY_MEMORY_RASTER_DIRTY,       4,       "raster reasons" }
  };
  void *before[sizeof(expected) / sizeof(expected[0])];
  size_t i;
  int ok = 1;

  for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
  {
    before[i] = api->get_memory_data(expected[i].id);
    if (!before[i])
    {
      fprintf(stderr, "memory regions: %s is NULL\n", expected[i].name);
      ok = 0;
      continue;
    }
    if (api->get_memory_size(expected[i].id) != expected[i].size)
    {
      fprintf(stderr, "memory regions: %s is %u bytes, expected %u\n",
              expected[i].name,
              (unsigned)api->get_memory_size(expected[i].id),
              (unsigned)expected[i].size);
      ok = 0;
    }
  }

  /* Un id que el core no conoce tiene que devolver NULL y tamano 0, no un
     puntero a cualquier cosa. */
  if (api->get_memory_data(0x1FF) != NULL ||
      api->get_memory_size(0x1FF) != 0)
  {
    fprintf(stderr, "memory regions: an unknown id returned a mapping\n");
    ok = 0;
  }

  /* Estabilidad a traves de reset y de unserialize. */
  api->reset();
  for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
  {
    if (before[i] && api->get_memory_data(expected[i].id) != before[i])
    {
      fprintf(stderr, "memory regions: %s moved across retro_reset\n",
              expected[i].name);
      ok = 0;
    }
  }
  if (!api->unserialize(checkpoint, state_size))
  {
    fprintf(stderr, "memory regions: could not restore the checkpoint\n");
    return 0;
  }
  for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
  {
    if (before[i] && api->get_memory_data(expected[i].id) != before[i])
    {
      fprintf(stderr, "memory regions: %s moved across retro_unserialize\n",
              expected[i].name);
      ok = 0;
    }
  }

  /* El puntero legacy de VRAM y la region de la ABI tienen que ver lo mismo:
     son dos ventanas a la misma memoria, y si divergieran el Lab mostraria una
     cosa y el motor HD usaria otra. */
  {
    static uint8_t via_abi[0x10000];
    const uint8_t *via_legacy = (const uint8_t *)before[0];
    if (via_legacy &&
        api->ayther->read_region(AYTHER_REGION_VRAM, 0, via_abi,
          sizeof(via_abi), AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OK &&
        memcmp(via_abi, via_legacy, sizeof(via_abi)) != 0)
    {
      fprintf(stderr, "memory regions: legacy VRAM and the ABI region differ\n");
      ok = 0;
    }
  }
  return ok;
}

/* #33: ida y vuelta del savestate.
 *
 * El replay ya prueba que restaurar y re-simular da los mismos hashes, pero eso
 * no cubre que el estado SERIALIZADO sea estable: un campo sin inicializar o
 * padding sin limpiar produce dos volcados distintos del mismo estado, y el
 * sintoma no aparece hasta que alguien compara savestates o los versiona. */
static int check_savestate_roundtrip(const struct core_api *api,
                                     size_t state_size)
{
  uint8_t *first = (uint8_t *)malloc(state_size);
  uint8_t *second = (uint8_t *)malloc(state_size);
  int ok = 1;

  if (!first || !second)
  {
    free(first); free(second);
    fprintf(stderr, "savestate: out of memory\n");
    return 0;
  }

  /* Primero: cuanto escribe realmente el core de lo que declara.
     `retro_serialize_size` devuelve una COTA, no el tamano exacto, asi que el
     resto del buffer queda como lo dejo quien lo asigno. Se mide llenando con
     dos patrones distintos y viendo hasta donde el core los piso. No es un
     fallo -es la semantica de libretro- pero significa que el archivo que el
     frontend guarda lleva una cola no inicializada, y por lo tanto que dos
     savestates del mismo estado no son iguales byte a byte. Vale la pena verlo
     escrito en vez de descubrirlo comparando archivos. */
  {
    size_t written_a = 0, written_b = 0, at;
    memset(first, 0x00, state_size);
    memset(second, 0xFF, state_size);
    if (api->serialize(first, state_size) && api->serialize(second, state_size))
    {
      for (at = state_size; at > 0; --at)
        if (first[at - 1] != 0x00) { written_a = at; break; }
      for (at = state_size; at > 0; --at)
        if (second[at - 1] != 0xFF) { written_b = at; break; }
      if (written_a > state_size || written_b > state_size)
      {
        fprintf(stderr, "savestate: the core wrote past the declared size\n");
        ok = 0;
      }
      fprintf(stderr,
              "savestate: %u of %u declared bytes are written (%.1f%%)\n",
              (unsigned)(written_a > written_b ? written_a : written_b),
              (unsigned)state_size,
              100.0 * (double)(written_a > written_b ? written_a : written_b) /
                (double)state_size);
    }
  }

  /* Ahora si, la propiedad que importa: los bytes que el core SI escribe tienen
     que ser los mismos para el mismo estado. Ambos buffers arrancan iguales,
     asi que cualquier diferencia viene del core y no de la cola sin tocar. */
  memset(first, 0x00, state_size);
  memset(second, 0x00, state_size);

  if (!api->serialize(first, state_size) ||
      !api->unserialize(first, state_size) ||
      !api->serialize(second, state_size))
  {
    fprintf(stderr, "savestate: serialize/unserialize round trip failed\n");
    ok = 0;
  }
  else if (memcmp(first, second, state_size) != 0)
  {
    /* El rango y la cantidad, no solo el primer byte: un puntero suelto son 8
       bytes contiguos y una estructura con padding son islas dispersas, y la
       diferencia entre esos dos casos es la mitad del diagnostico. */
    size_t at, lo = state_size, hi = 0, count = 0;
    for (at = 0; at < state_size; ++at)
    {
      if (first[at] != second[at])
      {
        if (at < lo) lo = at;
        hi = at;
        ++count;
      }
    }
    fprintf(stderr,
            "savestate: save->load->save differs in %u bytes, "
            "offsets %u..%u of %u\n",
            (unsigned)count, (unsigned)lo, (unsigned)hi,
            (unsigned)state_size);
    for (at = lo; at <= hi && at < lo + 32; ++at)
      fprintf(stderr, "  [%u] %02x -> %02x%s\n", (unsigned)at,
              first[at], second[at],
              first[at] == second[at] ? " (igual)" : "");
    ok = 0;
  }

  if (api->serialize_size() != state_size)
  {
    fprintf(stderr, "savestate: serialize_size changed between frames\n");
    ok = 0;
  }
  /* Un buffer mas chico tiene que ser rechazado, no truncado en silencio. */
  if (state_size > 1 && api->serialize(first, state_size - 1))
  {
    fprintf(stderr, "savestate: serialize accepted an undersized buffer\n");
    ok = 0;
  }
  if (state_size > 1 && api->unserialize(first, state_size - 1))
  {
    fprintf(stderr, "savestate: unserialize accepted a truncated state\n");
    ok = 0;
  }
  /* Y un estado con la cabecera rota tampoco puede pasar por bueno. */
  {
    uint8_t saved = first[0];
    first[0] = (uint8_t)~saved;
    if (api->unserialize(first, state_size))
    {
      fprintf(stderr, "savestate: unserialize accepted a corrupted header\n");
      ok = 0;
    }
    first[0] = saved;
    if (!api->unserialize(first, state_size))
    {
      fprintf(stderr, "savestate: could not restore after the corruption probe\n");
      ok = 0;
    }
  }

  free(first);
  free(second);
  return ok;
}

static uint64_t frame_digest(const struct frame_record *record)
{
  uint64_t hash = FNV_OFFSET;
  hash = hash_u64(hash, record->video_hash);
  hash = hash_u64(hash, record->audio_hash);
  hash = hash_u64(hash, record->state_hash);
  hash = hash_u64(hash, record->telemetry_hash);
  hash = hash_u32(hash, record->input_mask);
  hash = hash_u32(hash, record->width);
  hash = hash_u32(hash, record->height);
  hash = hash_u32(hash, record->audio_frames);
  hash = hash_u32(hash, record->fallback_reasons);
  hash = hash_u32(hash, record->sprite_count);
  hash = hash_u32(hash, record->audio_write_count);
  return hash_u32(hash, record->event_count);
}

static void write_frame_report(FILE *report, const char *pass,
                               unsigned int frame,
                               const struct frame_record *record)
{
  if (!report) return;
  fprintf(report,
    "{\"pass\":\"%s\",\"frame\":%u,\"input\":%u,"
"\"video\":\"%016" PRIx64 "\",\"audio\":\"%016" PRIx64
"\",\"state\":\"%016" PRIx64 "\",\"telemetry\":\"%016" PRIx64
"\",\"width\":%u,\"height\":%u,\"audio_frames\":%u,"
"\"fallback_reasons\":%u,\"sprites\":%u,\"audio_writes\":%u,"
"\"events\":%u,\"different_pixels\":%u,\"false_clean\":%s,"
"\"recompose_unavailable\":%s}\n",
    pass, frame, record->input_mask, record->video_hash, record->audio_hash,
    record->state_hash, record->telemetry_hash, record->width, record->height,
    record->audio_frames, record->fallback_reasons, record->sprite_count,
    record->audio_write_count, record->event_count, record->different_pixels,
    record->false_clean ? "true" : "false",
    record->recompose_unavailable ? "true" : "false");
  fflush(report);
}

static int run_pass(const struct core_api *api, const void *checkpoint,
                    size_t state_size, void *state_buffer,
                    uint16_t *recomposed, struct frame_record *records,
                    const struct frame_record *expected, double *timings,
                    FILE *report, const char *pass,
                    struct replay_summary *summary, int observe)
{
  unsigned int frame;
  if (!api->unserialize(checkpoint, state_size))
  {
    fprintf(stderr, "cannot restore deterministic checkpoint for %s\n", pass);
    return 0;
  }
  drain_audio_events(api);

  for (frame = 0; frame < REPLAY_FRAMES; ++frame)
  {
    ayther_frame_snapshot_v1 snapshot;
    struct frame_record *record = &records[frame];
    uint32_t unavailable;
    double start;
    double end;

    memset(record, 0, sizeof(*record));
    current_input_mask = input_for_frame(frame);
    current_video_hash = FNV_OFFSET;
    current_video_valid = 0;
    current_audio_hash = FNV_OFFSET;
    current_audio_frames = 0;
    start = monotonic_ns();
    api->run();
    end = monotonic_ns();
    if (timings) timings[frame] = end - start;

    memset(state_buffer, 0, state_size);
    if (!current_video_valid || !video_size || !current_audio_frames ||
        !api->serialize(state_buffer, state_size))
    {
      fprintf(stderr, "frame %u did not produce video or serializable state\n",
              frame);
      return 0;
    }
    record->video_hash = current_video_hash;
    record->audio_hash = current_audio_hash;
    record->state_hash = hash_bytes(FNV_OFFSET, state_buffer, state_size);
    if (!expected && frame == 0u && reference_state_path[0])
    {
      FILE *state_file = fopen(reference_state_path, "wb");
      if (!state_file || fwrite(state_buffer, 1, state_size, state_file) !=
                         state_size)
      {
        if (state_file) fclose(state_file);
        fprintf(stderr, "cannot write reference state diagnostic %s\n",
                reference_state_path);
        return 0;
      }
      fclose(state_file);
    }
    record->input_mask = current_input_mask;
    record->width = video_width;
    record->height = video_height;
    record->audio_frames = current_audio_frames;
    record->telemetry_hash = FNV_OFFSET;
    if (observe)
    {
      record->event_count = collect_audio_events(api,
                                                 &record->telemetry_hash);
      memset(&snapshot, 0, sizeof(snapshot));
      snapshot.struct_size = sizeof(snapshot);
      if (api->ayther->capture_snapshot(&snapshot, sizeof(snapshot)) !=
          AYTHER_STATUS_OK)
      {
        fprintf(stderr, "cannot capture AYTHER snapshot at frame %u\n", frame);
        return 0;
      }
      record->fallback_reasons = snapshot.fallback_reasons;
      record->sprite_count = snapshot.parsed_sprite_count;
      record->audio_write_count = snapshot.audio_write_count;
      record->different_pixels = compare_recomposition(api, recomposed,
                                                        &unavailable);
      record->recompose_unavailable = (uint8_t)unavailable;
      record->false_clean = (uint8_t)(
        (!unavailable && record->different_pixels > 0u &&
         record->fallback_reasons == 0u) ||
        (unavailable && record->fallback_reasons == 0u));
    }
    record->digest = frame_digest(record);
    write_frame_report(report, pass, frame, record);

    if (summary && !expected)
    {
      summary->video_hash = hash_u64(summary->video_hash, record->video_hash);
      summary->audio_hash = hash_u64(summary->audio_hash, record->audio_hash);
      summary->state_hash = hash_u64(summary->state_hash, record->state_hash);
      summary->telemetry_hash = hash_u64(summary->telemetry_hash,
                                         record->telemetry_hash);
      summary->input_hash = hash_u32(summary->input_hash, record->input_mask);
      summary->replay_hash = hash_u64(summary->replay_hash, record->digest);
      summary->total_events += record->event_count;
      summary->fallback_frames += record->fallback_reasons != 0u;
      summary->false_clean_frames += record->false_clean;
      summary->unavailable_without_reason +=
        record->recompose_unavailable && record->fallback_reasons == 0u;
      if (record->sprite_count > summary->max_sprites)
        summary->max_sprites = record->sprite_count;
      if (record->audio_write_count > summary->max_audio_writes)
        summary->max_audio_writes = record->audio_write_count;
      if (record->audio_frames > summary->max_audio_frames)
        summary->max_audio_frames = record->audio_frames;
    }
    if (expected && record->digest != expected[frame].digest)
    {
      fprintf(stderr,
        "replay mismatch at frame %u: expected=%016" PRIx64
" actual=%016" PRIx64 "\n",
        frame, expected[frame].digest, record->digest);
      if (summary) ++summary->replay_mismatches;
    }
  }
  return 1;
}

static int emulation_records_equal(const struct frame_record *idle,
                                   const struct frame_record *observed)
{
  unsigned int frame;
  for (frame = 0; frame < REPLAY_FRAMES; ++frame)
  {
    if (idle[frame].video_hash != observed[frame].video_hash ||
        idle[frame].audio_hash != observed[frame].audio_hash ||
        idle[frame].state_hash != observed[frame].state_hash ||
        idle[frame].input_mask != observed[frame].input_mask ||
        idle[frame].width != observed[frame].width ||
        idle[frame].height != observed[frame].height ||
        idle[frame].audio_frames != observed[frame].audio_frames)
    {
      fprintf(stderr,
        "idle/observed emulation mismatch at frame %u: "
"video=%016" PRIx64 "/%016" PRIx64 " state=%016" PRIx64
"/%016" PRIx64 "\n",
        frame, idle[frame].video_hash, observed[frame].video_hash,
        idle[frame].state_hash, observed[frame].state_hash);
      return 0;
    }
  }
  return 1;
}

static int compare_double(const void *left, const void *right)
{
  double a = *(const double *)left;
  double b = *(const double *)right;
  return (a > b) - (a < b);
}

static struct benchmark_stats summarize_timings(double *timings)
{
  struct benchmark_stats stats;
  qsort(timings, REPLAY_FRAMES, sizeof(timings[0]), compare_double);
  stats.minimum = timings[0];
  stats.p50 = timings[(REPLAY_FRAMES * 50u + 99u) / 100u - 1u];
  stats.p95 = timings[(REPLAY_FRAMES * 95u + 99u) / 100u - 1u];
  stats.p99 = timings[(REPLAY_FRAMES * 99u + 99u) / 100u - 1u];
  stats.maximum = timings[REPLAY_FRAMES - 1u];
  return stats;
}

static uint64_t file_size(const char *path);

static int profile_core(const char *core_path, struct profile_result *result)
{
  struct core_api api;
  struct retro_game_info game;
  library_t library = NULL;
  uint8_t *rom = NULL;
  void *checkpoint = NULL;
  void *state = NULL;
  double timings[REPLAY_FRAMES];
  size_t state_size = 0;
  unsigned int frame;
  unsigned int warmup;
  int initialized = 0;
  int loaded = 0;
  int success = 0;

  memset(result, 0, sizeof(*result));
  result->video_hash = FNV_OFFSET;
  result->audio_hash = FNV_OFFSET;
  result->state_hash = FNV_OFFSET;
  result->input_hash = FNV_OFFSET;
  library = library_open(core_path);
  if (!library)
  {
    library_error(core_path);
    goto cleanup;
  }
  if (!load_api(library, &api, 0))
    goto cleanup;
  rom = (uint8_t *)malloc(AYTHER_GENERATED_ROM_SIZE);
  if (!rom || !ayther_build_generated_rom(rom, AYTHER_GENERATED_ROM_SIZE))
  {
    fprintf(stderr, "cannot build generated ROM for profile comparison\n");
    goto cleanup;
  }
  memset(&generated_game_info, 0, sizeof(generated_game_info));
  generated_game_info.full_path = "ayther-generated-v1.md";
  generated_game_info.dir = ".";
  generated_game_info.name = "ayther-generated-v1";
  generated_game_info.ext = "md";
  generated_game_info.data = rom;
  generated_game_info.size = AYTHER_GENERATED_ROM_SIZE;
  generated_game_info.persistent_data = true;
  memset(&game, 0, sizeof(game));
  game.path = "ayther-generated-v1.md";
  game.data = rom;
  game.size = AYTHER_GENERATED_ROM_SIZE;
  install_callbacks(&api);
  api.init();
  initialized = 1;
  if (!api.load_game(&game))
  {
    fprintf(stderr, "core rejected generated ROM during profile comparison\n");
    goto cleanup;
  }
  loaded = 1;
  for (frame = 0; frame < BOOTSTRAP_FRAMES; ++frame)
  {
    current_input_mask = 0;
    current_video_hash = FNV_OFFSET;
    current_audio_hash = FNV_OFFSET;
    api.run();
  }
  state_size = api.serialize_size();
  checkpoint = malloc(state_size);
  state = malloc(state_size);
  if (!state_size || !checkpoint || !state ||
      !api.serialize(checkpoint, state_size))
  {
    fprintf(stderr, "cannot create profile checkpoint\n");
    goto cleanup;
  }

  for (warmup = 0; warmup < 2u; ++warmup)
  {
    if (!api.unserialize(checkpoint, state_size))
      goto cleanup;
    for (frame = 0; frame < REPLAY_FRAMES; ++frame)
    {
      current_input_mask = input_for_frame(frame);
      current_video_hash = FNV_OFFSET;
      current_audio_hash = FNV_OFFSET;
      api.run();
    }
  }
  if (!api.unserialize(checkpoint, state_size))
    goto cleanup;
  for (frame = 0; frame < REPLAY_FRAMES; ++frame)
  {
    double start;
    double end;
    current_input_mask = input_for_frame(frame);
    current_video_hash = FNV_OFFSET;
    current_video_valid = 0;
    current_audio_hash = FNV_OFFSET;
    current_audio_frames = 0;
    start = monotonic_ns();
    api.run();
    end = monotonic_ns();
    timings[frame] = end - start;
    if (((requested_audio_video & 1) && !current_video_valid) ||
        ((requested_audio_video & 2) && !current_audio_frames) ||
        !api.serialize(state, state_size))
    {
      fprintf(stderr, "profile frame %u produced incomplete output\n", frame);
      goto cleanup;
    }
    result->video_hash = hash_u64(result->video_hash, current_video_hash);
    result->audio_hash = hash_u64(result->audio_hash, current_audio_hash);
    result->state_hash = hash_u64(result->state_hash,
      hash_bytes(FNV_OFFSET, state, state_size));
    result->input_hash = hash_u32(result->input_hash, current_input_mask);
  }
  result->timing = summarize_timings(timings);
  result->core_binary_bytes = file_size(core_path);
  result->state_size = state_size;
  success = 1;

cleanup:
  if (loaded) api.unload_game();
  if (initialized) api.deinit();
  if (library) library_close(library);
  free(state);
  free(checkpoint);
  free(rom);
  free(video_pixels);
  video_pixels = NULL;
  video_capacity = 0;
  video_size = 0;
  return success;
}

static int compare_profiles(const char *off_core, const char *idle_core,
                            const char *output_path)
{
  enum { PROFILE_ROUNDS = 7 };
  struct profile_result off[PROFILE_ROUNDS];
  struct profile_result idle[PROFILE_ROUNDS];
  double off_p50[PROFILE_ROUNDS];
  double off_p95[PROFILE_ROUNDS];
  double idle_p50[PROFILE_ROUNDS];
  double idle_p95[PROFILE_ROUNDS];
  double overheads[PROFILE_ROUNDS];
  FILE *output;
  double overhead;
  int identical = 1;
  unsigned int round;

  for (round = 0; round < PROFILE_ROUNDS; ++round)
  {
    int ok;
    if (round & 1u)
      ok = profile_core(idle_core, &idle[round]) &&
           profile_core(off_core, &off[round]);
    else
      ok = profile_core(off_core, &off[round]) &&
           profile_core(idle_core, &idle[round]);
    if (!ok) return 1;
    off_p50[round] = off[round].timing.p50;
    off_p95[round] = off[round].timing.p95;
    idle_p50[round] = idle[round].timing.p50;
    idle_p95[round] = idle[round].timing.p95;
    overheads[round] = off_p50[round] > 0.0
      ? ((idle_p50[round] / off_p50[round]) - 1.0) * 100.0 : 0.0;
    {
      int round_identical =
      off[round].video_hash == idle[round].video_hash &&
      off[round].audio_hash == idle[round].audio_hash &&
      off[round].input_hash == idle[round].input_hash &&
      off[round].state_size == idle[round].state_size &&
      off[round].video_hash == off[0].video_hash &&
      off[round].audio_hash == off[0].audio_hash;
      if (!round_identical)
        fprintf(stderr,
          "profile round %u mismatch: off=%016" PRIx64 "/%016" PRIx64
"/%016" PRIx64 " idle=%016" PRIx64 "/%016" PRIx64
"/%016" PRIx64 "\n",
          round, off[round].video_hash, off[round].audio_hash,
          off[round].state_hash, idle[round].video_hash,
          idle[round].audio_hash, idle[round].state_hash);
      identical = identical && round_identical;
    }
  }
  qsort(off_p50, PROFILE_ROUNDS, sizeof(off_p50[0]), compare_double);
  qsort(off_p95, PROFILE_ROUNDS, sizeof(off_p95[0]), compare_double);
  qsort(idle_p50, PROFILE_ROUNDS, sizeof(idle_p50[0]), compare_double);
  qsort(idle_p95, PROFILE_ROUNDS, sizeof(idle_p95[0]), compare_double);
  qsort(overheads, PROFILE_ROUNDS, sizeof(overheads[0]), compare_double);
  overhead = overheads[PROFILE_ROUNDS / 2u];
  output = fopen(output_path, "wb");
  if (!output)
  {
    fprintf(stderr, "cannot create profile comparison %s: %s\n",
            output_path, strerror(errno));
    return 1;
  }
  fprintf(output,
    "{\"schema\":1,\"fixture\":\"generated-v1\",\"frames\":%u,"
"\"rounds\":%u,"
"\"bit_identical\":%s,\"state_hash_cross_process_compared\":false,"
"\"idle_overhead_percent_p50\":%.3f,"
"\"target_percent\":1.0,\"within_target\":%s,"
"\"extensions_off\":{\"p50_ns\":%.3f,\"p95_ns\":%.3f,"
"\"binary_bytes\":%" PRIu64 "},"
"\"compiled_idle\":{\"p50_ns\":%.3f,\"p95_ns\":%.3f,"
"\"binary_bytes\":%" PRIu64 "},"
"\"video_hash\":\"%016" PRIx64 "\","
"\"audio_hash\":\"%016" PRIx64 "\","
"\"state_hash\":\"%016" PRIx64 "\"}\n",
    REPLAY_FRAMES, PROFILE_ROUNDS, identical ? "true" : "false", overhead,
    overhead < 1.0 ? "true" : "false",
    off_p50[PROFILE_ROUNDS / 2u], off_p95[PROFILE_ROUNDS / 2u],
    off[0].core_binary_bytes,
    idle_p50[PROFILE_ROUNDS / 2u], idle_p95[PROFILE_ROUNDS / 2u],
    idle[0].core_binary_bytes,
    off[0].video_hash, off[0].audio_hash, off[0].state_hash);
  fclose(output);
  if (!identical)
  {
    fprintf(stderr, "extensions-off and compiled-idle results differ\n");
    return 1;
  }
  printf("compiled-idle overhead vs extensions-off: %.3f%% (p50)\n",
         overhead);
  if (overhead >= 1.0)
  {
    fprintf(stderr, "compiled-idle overhead exceeds the 1%% target\n");
    return 1;
  }
  return 0;
}

static uint64_t file_size(const char *path)
{
  FILE *file = fopen(path, "rb");
  long size;
  if (!file) return 0;
  if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0)
    size = 0;
  fclose(file);
  return (uint64_t)size;
}

static int read_golden(const char *path, struct golden *golden)
{
  FILE *file = fopen(path, "rb");
  char buffer[1024];
  char video[17], audio[17], state[17], telemetry[17], input[17], config[17];
  char replay[17];
  if (!file || !fgets(buffer, sizeof(buffer), file))
  {
    if (file) fclose(file);
    fprintf(stderr, "cannot read full-core golden: %s\n", path);
    return 0;
  }
  fclose(file);
  if (sscanf(buffer,
      "{\"schema\":%u,\"fixture\":\"generated-v1\",\"frames\":%u,"
"\"video_hash\":\"%16[0-9a-fA-F]\","
"\"audio_hash\":\"%16[0-9a-fA-F]\","
"\"state_hash\":\"%16[0-9a-fA-F]\","
"\"telemetry_hash\":\"%16[0-9a-fA-F]\","
"\"input_hash\":\"%16[0-9a-fA-F]\","
"\"configuration_hash\":\"%16[0-9a-fA-F]\","
"\"replay_hash\":\"%16[0-9a-fA-F]\","
"\"fallback_frames\":%u,\"false_clean_frames\":%u}",
      &golden->schema, &golden->frames, video, audio, state, telemetry,
      input, config, replay, &golden->fallback_frames,
      &golden->false_clean_frames) != 11)
  {
    fprintf(stderr, "invalid full-core golden format: %s\n", path);
    return 0;
  }
  golden->video_hash = strtoull(video, NULL, 16);
  golden->audio_hash = strtoull(audio, NULL, 16);
  golden->state_hash = strtoull(state, NULL, 16);
  golden->telemetry_hash = strtoull(telemetry, NULL, 16);
  golden->input_hash = strtoull(input, NULL, 16);
  golden->configuration_hash = strtoull(config, NULL, 16);
  golden->replay_hash = strtoull(replay, NULL, 16);
  return 1;
}

static void write_summary(FILE *file, const struct replay_summary *summary)
{
  fprintf(file,
    "{\"schema\":1,\"fixture\":\"generated-v1\",\"frames\":%u,"
"\"video_hash\":\"%016" PRIx64 "\","
"\"audio_hash\":\"%016" PRIx64 "\","
"\"state_hash\":\"%016" PRIx64 "\","
"\"telemetry_hash\":\"%016" PRIx64 "\","
"\"input_hash\":\"%016" PRIx64 "\","
"\"configuration_hash\":\"%016" PRIx64 "\","
"\"replay_hash\":\"%016" PRIx64 "\","
"\"fallback_frames\":%u,\"false_clean_frames\":%u}\n",
    REPLAY_FRAMES, summary->video_hash, summary->audio_hash,
    summary->state_hash, summary->telemetry_hash, summary->input_hash,
    summary->configuration_hash, summary->replay_hash,
    summary->fallback_frames,
    summary->false_clean_frames);
}

static int golden_matches(const struct golden *golden,
                          const struct replay_summary *summary)
{
  return golden->schema == 1u && golden->frames == REPLAY_FRAMES &&
    golden->video_hash == summary->video_hash &&
    golden->audio_hash == summary->audio_hash &&
    golden->state_hash == summary->state_hash &&
    golden->telemetry_hash == summary->telemetry_hash &&
    golden->input_hash == summary->input_hash &&
    golden->configuration_hash == summary->configuration_hash &&
    golden->replay_hash == summary->replay_hash &&
    golden->fallback_frames == summary->fallback_frames &&
    golden->false_clean_frames == summary->false_clean_frames;
}

static int write_benchmark(const char *path,
                           const struct benchmark_stats *idle_stats,
                           const struct benchmark_stats *observed_stats,
                           const struct replay_summary *summary,
                           const char *core_path, size_t state_size)
{
  FILE *file = fopen(path, "wb");
  if (!file)
  {
    fprintf(stderr, "cannot create frame benchmark %s: %s\n",
            path, strerror(errno));
    return 0;
  }
  fprintf(file,
    "{\"schema\":1,\"fixture\":\"generated-v1\","
"\"target\":\"%s\",\"compiler\":\"%s\","
"\"unit\":\"ns/frame\",\"warmup_frames\":%u,"
"\"samples\":%u,\"frame_time\":{\"min\":%.3f,\"p50\":%.3f,"
"\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},"
"\"idle_frame_time\":{\"min\":%.3f,\"p50\":%.3f,"
"\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},"
"\"observed_overhead_percent_p50\":%.3f,"
"\"memory\":{\"rom_bytes\":%u,\"serialized_state_bytes\":%zu,"
"\"framebuffer_peak_bytes\":%zu,\"recompose_buffer_bytes\":%zu},"
"\"core_binary_bytes\":%" PRIu64 ",\"events\":%" PRIu64
",\"events_per_frame\":%.3f,\"fallback_frames\":%u,"
"\"false_clean_frames\":%u,\"replay_mismatches\":%u,"
"\"max_sprites\":%u,\"max_audio_writes\":%u,"
"\"max_audio_frames\":%u}\n",
    target_name(), compiler_name(),
    BOOTSTRAP_FRAMES + (2u * REPLAY_FRAMES), REPLAY_FRAMES,
    observed_stats->minimum, observed_stats->p50,
    observed_stats->p95, observed_stats->p99, observed_stats->maximum,
    idle_stats->minimum, idle_stats->p50, idle_stats->p95,
    idle_stats->p99, idle_stats->maximum,
    idle_stats->p50 > 0.0
      ? ((observed_stats->p50 / idle_stats->p50) - 1.0) * 100.0 : 0.0,
    AYTHER_GENERATED_ROM_SIZE,
    state_size, video_capacity,
    (size_t)MAX_RECOMPOSE_PIXELS * sizeof(uint16_t), file_size(core_path),
    summary->total_events, (double)summary->total_events / REPLAY_FRAMES,
    summary->fallback_frames, summary->false_clean_frames,
    summary->replay_mismatches, summary->max_sprites,
    summary->max_audio_writes, summary->max_audio_frames);
  fclose(file);
  return 1;
}

static void usage(const char *program)
{
  fprintf(stderr,
    "usage: %s CORE GOLDEN_JSON ACTUAL_JSON FRAME_REPORT_JSONL "
"BENCHMARK_JSON\n"
"       %s --compare-profiles OFF_CORE IDLE_CORE OUTPUT_JSON\n",
    program, program);
}

int main(int argc, char **argv)
{
  struct core_api api;
  struct retro_game_info game;
  struct frame_record idle[REPLAY_FRAMES];
  struct frame_record reference[REPLAY_FRAMES];
  struct frame_record replay[REPLAY_FRAMES];
  struct replay_summary summary;
  struct benchmark_stats idle_benchmark;
  struct benchmark_stats observed_benchmark;
  struct golden golden;
  ayther_subscription_state_v1 subscriptions;
  ayther_frame_snapshot_v1 idle_snapshot;
  ayther_audio_transport_stats_v1 idle_audio_stats;
  library_t library;
  uint8_t *rom = NULL;
  void *checkpoint = NULL;
  void *state_buffer = NULL;
  uint16_t *recomposed = NULL;
  double idle_timings[REPLAY_FRAMES];
  double observed_timings[REPLAY_FRAMES];
  FILE *actual = NULL;
  FILE *report = NULL;
  size_t state_size;
  unsigned int frame;
  int loaded = 0;
  int success = 0;
  const char *profile_av = getenv("AYTHER_PROFILE_AV");

  if (profile_av && profile_av[0] >= '0' && profile_av[0] <= '3' &&
      profile_av[1] == '\0')
    requested_audio_video = profile_av[0] - '0';

  if (argc == 5 && strcmp(argv[1], "--compare-profiles") == 0)
    return compare_profiles(argv[2], argv[3], argv[4]);
  if (argc != 6)
  {
    usage(argv[0]);
    return 2;
  }
  library = library_open(argv[1]);
  if (!library)
  {
    library_error(argv[1]);
    return 2;
  }
  if (!load_api(library, &api, 1))
    goto cleanup;
  if (snprintf(reference_state_path, sizeof(reference_state_path),
               "%s.state0.bin", argv[4]) >=
      (int)sizeof(reference_state_path))
  {
    fprintf(stderr, "frame report path is too long\n");
    goto cleanup;
  }

  rom = (uint8_t *)malloc(AYTHER_GENERATED_ROM_SIZE);
  recomposed = (uint16_t *)malloc(
    (size_t)MAX_RECOMPOSE_PIXELS * sizeof(uint16_t));
  if (!rom || !recomposed ||
      ayther_build_generated_rom(rom, AYTHER_GENERATED_ROM_SIZE) == 0)
  {
    fprintf(stderr, "cannot build generated ROM fixture\n");
    goto cleanup;
  }

  install_callbacks(&api);
  api.init();
  memset(&game, 0, sizeof(game));
  memset(&generated_game_info, 0, sizeof(generated_game_info));
  generated_game_info.full_path = "ayther-generated-v1.md";
  generated_game_info.dir = ".";
  generated_game_info.name = "ayther-generated-v1";
  generated_game_info.ext = "md";
  generated_game_info.data = rom;
  generated_game_info.size = AYTHER_GENERATED_ROM_SIZE;
  generated_game_info.persistent_data = true;
  game.path = "ayther-generated-v1.md";
  game.data = rom;
  game.size = AYTHER_GENERATED_ROM_SIZE;
  if (!api.load_game(&game))
  {
    fprintf(stderr, "core rejected generated ROM fixture\n");
    api.deinit();
    goto cleanup;
  }
  loaded = 1;

  memset(&subscriptions, 0, sizeof(subscriptions));
  if (api.ayther->get_subscriptions(&subscriptions,
        sizeof(subscriptions)) != AYTHER_STATUS_OK ||
      subscriptions.active_mask != 0 || subscriptions.requested_mask != 0)
  {
    fprintf(stderr, "AYTHER observations are not idle after content load\n");
    goto cleanup;
  }
  current_input_mask = 0;
  current_audio_hash = FNV_OFFSET;
  current_video_hash = FNV_OFFSET;
  api.run();
  memset(&idle_snapshot, 0, sizeof(idle_snapshot));
  idle_snapshot.struct_size = sizeof(idle_snapshot);
  memset(&idle_audio_stats, 0, sizeof(idle_audio_stats));
  if (api.ayther->capture_snapshot(&idle_snapshot, sizeof(idle_snapshot)) !=
        AYTHER_STATUS_OK || idle_snapshot.parsed_sprite_count != 0 ||
      idle_snapshot.audio_write_count != 0 ||
      idle_snapshot.fallback_reasons != 0 ||
      api.ayther->get_audio_transport_stats(&idle_audio_stats,
        sizeof(idle_audio_stats)) != AYTHER_STATUS_OK ||
      idle_audio_stats.pending != 0 ||
      (idle_audio_stats.flags & AYTHER_AUDIO_TRANSPORT_OBSERVATION_ACTIVE))
  {
    fprintf(stderr, "idle AYTHER profile captured data without consumers\n");
    goto cleanup;
  }

  api.reset();
  if (api.ayther->set_subscriptions(AYTHER_SUB_ALL) != AYTHER_STATUS_OK ||
      api.ayther->get_subscriptions(&subscriptions,
        sizeof(subscriptions)) != AYTHER_STATUS_OK ||
      subscriptions.active_mask != 0 ||
      subscriptions.requested_mask != AYTHER_SUB_ALL)
  {
    fprintf(stderr, "AYTHER subscription request did not remain pending\n");
    goto cleanup;
  }
  for (frame = 0; frame < BOOTSTRAP_FRAMES; ++frame)
  {
    current_input_mask = 0;
    current_audio_hash = FNV_OFFSET;
    current_video_hash = FNV_OFFSET;
    api.run();
  }
  if (api.ayther->get_subscriptions(&subscriptions,
        sizeof(subscriptions)) != AYTHER_STATUS_OK ||
      subscriptions.active_mask != AYTHER_SUB_ALL ||
      subscriptions.requested_mask != AYTHER_SUB_ALL)
  {
    fprintf(stderr, "AYTHER subscriptions did not activate on frame boundary\n");
    goto cleanup;
  }

  state_size = api.serialize_size();
  checkpoint = malloc(state_size);
  state_buffer = malloc(state_size);
  if (!state_size || !checkpoint || !state_buffer)
  {
    fprintf(stderr, "cannot allocate initial savestate checkpoint\n");
    goto cleanup;
  }
  memset(checkpoint, 0, state_size);
  if (!api.serialize(checkpoint, state_size))
  {
    fprintf(stderr, "cannot create initial savestate checkpoint\n");
    goto cleanup;
  }
  report = fopen(argv[4], "wb");
  if (!report)
  {
    fprintf(stderr, "cannot create frame report %s: %s\n",
            argv[4], strerror(errno));
    goto cleanup;
  }

  memset(&summary, 0, sizeof(summary));
  summary.video_hash = FNV_OFFSET;
  summary.audio_hash = FNV_OFFSET;
  summary.state_hash = FNV_OFFSET;
  summary.telemetry_hash = FNV_OFFSET;
  summary.input_hash = FNV_OFFSET;
  summary.configuration_hash = hash_bytes(
    FNV_OFFSET, (const uint8_t *)FIXTURE_CONFIGURATION,
    strlen(FIXTURE_CONFIGURATION));
  summary.replay_hash = FNV_OFFSET;
  if (api.ayther->set_subscriptions(0) != AYTHER_STATUS_OK ||
      !run_pass(&api, checkpoint, state_size, state_buffer, recomposed,
                idle, NULL, idle_timings, report, "idle", NULL, 0))
    goto cleanup;
  memset(&idle_snapshot, 0, sizeof(idle_snapshot));
  idle_snapshot.struct_size = sizeof(idle_snapshot);
  memset(&idle_audio_stats, 0, sizeof(idle_audio_stats));
  if (api.ayther->capture_snapshot(&idle_snapshot, sizeof(idle_snapshot)) !=
        AYTHER_STATUS_OK || idle_snapshot.parsed_sprite_count != 0 ||
      idle_snapshot.audio_write_count != 0 ||
      idle_snapshot.fallback_reasons != 0 ||
      api.ayther->get_audio_transport_stats(&idle_audio_stats,
        sizeof(idle_audio_stats)) != AYTHER_STATUS_OK ||
      idle_audio_stats.pending != 0 ||
      (idle_audio_stats.flags & AYTHER_AUDIO_TRANSPORT_OBSERVATION_ACTIVE))
  {
    fprintf(stderr, "idle replay captured AYTHER observations\n");
    goto cleanup;
  }
  if (api.ayther->set_subscriptions(AYTHER_SUB_ALL) != AYTHER_STATUS_OK ||
      !run_pass(&api, checkpoint, state_size, state_buffer, recomposed,
                reference, NULL, NULL, report, "reference", &summary, 1) ||
      !emulation_records_equal(idle, reference) ||
      !run_pass(&api, checkpoint, state_size, state_buffer, recomposed,
                replay, reference, NULL, report, "replay-1", &summary, 1) ||
      !run_pass(&api, checkpoint, state_size, state_buffer, recomposed,
                replay, reference, observed_timings, report, "replay-2",
                &summary, 1))
    goto cleanup;

  /* Va DESPUES de los pases hasheados: escribe regiones de control, y aunque
     las deja neutras, cualquier efecto suyo sobre los hashes seria un falso
     positivo dificil de leer. Aca no puede contaminar nada. */
  if (!check_recompose_control_cache(&api) ||
      !check_raster_replay_isolation(&api, recomposed) ||
      !check_raster_journal_accounting(&api) ||
      !check_savestate_roundtrip(&api, state_size) ||
      !check_memory_regions(&api, checkpoint, state_size))
    goto cleanup;

  actual = fopen(argv[3], "wb");
  if (!actual)
  {
    fprintf(stderr, "cannot create actual summary %s: %s\n",
            argv[3], strerror(errno));
    goto cleanup;
  }
  write_summary(actual, &summary);
  fclose(actual);
  actual = NULL;
  write_summary(stdout, &summary);

  idle_benchmark = summarize_timings(idle_timings);
  observed_benchmark = summarize_timings(observed_timings);
  if (!write_benchmark(argv[5], &idle_benchmark, &observed_benchmark,
                       &summary, argv[1], state_size))
    goto cleanup;
  if (!read_golden(argv[2], &golden))
    goto cleanup;
  if (!golden_matches(&golden, &summary))
  {
    fprintf(stderr, "full-core golden mismatch; inspect %s and %s\n",
            argv[3], argv[4]);
    goto cleanup;
  }
  if (summary.false_clean_frames || summary.unavailable_without_reason ||
      summary.replay_mismatches)
  {
    fprintf(stderr,
      "determinism safety failure: false_clean=%u unavailable=%u replay=%u\n",
      summary.false_clean_frames, summary.unavailable_without_reason,
      summary.replay_mismatches);
    goto cleanup;
  }
  if (summary.max_sprites <= 20u)
  {
    fprintf(stderr,
      "sprite rewrite fixture did not preserve multiple SAT identities: max=%u\n",
      summary.max_sprites);
    goto cleanup;
  }
  success = 1;

cleanup:
  if (actual) fclose(actual);
  if (report) fclose(report);
  if (loaded)
  {
    api.unload_game();
    api.deinit();
  }
  free(video_pixels);
  free(recomposed);
  free(state_buffer);
  free(checkpoint);
  free(rom);
  library_close(library);
  return success ? 0 : 1;
}
