/* End-to-end validation of AYTHER raster fallback reasons against local ROMs.
 *
 * The probe dynamically loads a libretro core, captures each emitted RGB565
 * frame and compares it with ABI v1 recomposition from the same final VDP
 * state. Issue #5's safety contract is simple: a mismatch is only safe when
 * private memory id 0x10E contains at least one fallback reason.
 *
 * ROM contents remain in memory and are never copied to the repository or the
 * JSON-lines report. The test core must export ayther_get_interface().
 */

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "ayther/ayther_api.h"
#include "generated_rom.h"   /* #117: el ROM sintetico @generated-fm */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE library_t;

static library_t library_open(const char *path)
{
  return LoadLibraryA(path);
}

static void *library_symbol(library_t library, const char *name)
{
  return (void *)(uintptr_t)GetProcAddress(library, name);
}

static void library_close(library_t library)
{
  FreeLibrary(library);
}

static void library_error(const char *path)
{
  fprintf(stderr, "cannot load %s (Windows error %lu)\n",
          path, (unsigned long)GetLastError());
}
#else
#include <dlfcn.h>
typedef void *library_t;

static library_t library_open(const char *path)
{
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void *library_symbol(library_t library, const char *name)
{
  return dlsym(library, name);
}

static void library_close(library_t library)
{
  dlclose(library);
}

static void library_error(const char *path)
{
  const char *error = dlerror();
  fprintf(stderr, "cannot load %s: %s\n",
          path, error ? error : "unknown error");
}
#endif

#define AYTHER_MEMORY_RASTER_DIRTY 0x10Eu
#define AYTHER_MEMORY_VDP_REGS 0x101u
#define AYTHER_RASTER_REASON_UNSUPPORTED_MODE (1u << 5)
#define MAX_RECOMPOSE_PIXELS (720u * 576u)

enum reason_index
{
  REASON_REG = 0,
  REASON_CRAM,
  REASON_VSRAM,
  REASON_HSCROLL,
  REASON_DMA,
  REASON_UNSUPPORTED_MODE,
  REASON_VRAM,
  REASON_COUNT
};

static const char *reason_names[REASON_COUNT] =
{
  "REG", "CRAM", "VSRAM", "HSCROLL", "DMA", "UNSUPPORTED_MODE", "VRAM"
};

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
  bool (*load_game)(const struct retro_game_info *game);
  void (*unload_game)(void);
  void (*run)(void);
  size_t (*serialize_size)(void);
  bool (*serialize)(void *data, size_t size);
  bool (*unserialize)(const void *data, size_t size);
  void *(*get_memory_data)(unsigned id);
  size_t (*get_memory_size)(unsigned id);
  ayther_get_interface_fn get_ayther_interface;
  const ayther_interface_v1 *ayther;
};

struct rom_stats
{
  uint64_t clean_equal;
  uint64_t guarded_equal;
  uint64_t clean_mismatch;
  uint64_t guarded_mismatch;
  uint64_t unsupported_guarded;
  uint64_t unavailable_without_reason;
  uint64_t missing_video;
  uint64_t different_pixels;
  uint64_t reason_frames[REASON_COUNT];
  int first_failure_frame;
  uint32_t first_failure_mask;
  uint32_t first_failure_reg_changes;
  uint64_t first_failure_pixels;
  uint8_t first_failure_previous_regs[0x20];
  uint8_t first_failure_current_regs[0x20];
  unsigned video_width;
  unsigned video_height;
  int recompose_width;
  int recompose_height;
  /* #117: el checkpoint y la continuacion restaurada desde el */
  int checkpoint_frame;               /* -1 = esta corrida no guardo estado */
  int checkpoint_failed;              /* serialize o unserialize dijeron que no */
  size_t checkpoint_bytes;
  uint64_t restored_frames;
  uint64_t restored_video_equal;
  uint64_t restored_video_mismatch;
  uint64_t restored_audio_equal;
  uint64_t restored_audio_mismatch;
  uint64_t restored_mask_differs;     /* informativo: la mascara no es parte del contrato */
  uint64_t restored_state_equal;      /* estado serializado tras el frame, identico al original */
  uint64_t restored_state_mismatch;
  int restored_first_state_mismatch;  /* frame, -1 si no hubo */
  int restored_first_mismatch;        /* frame, -1 si no hubo */
  int restored_first_video_equal;
  int restored_first_audio_equal;
  uint64_t restored_first_samples;
  uint64_t restored_first_samples_expected;
};

static uint8_t *video_frame;
static size_t video_capacity;
static size_t video_size;
static unsigned video_width;
static unsigned video_height;
static int video_valid;
static int current_frame;
static int auto_input = 1;
static const char *dump_prefix;
static int checkpoint_frame = -1;    /* el checkpoint de la corrida en curso (#117); -1 = sin checkpoint */
#define MAX_CHECKPOINTS 16
static int checkpoint_list[MAX_CHECKPOINTS];   /* --checkpoint A,B,C (#123) */
static int checkpoint_count;
static int state_diff;               /* --state-diff: diagnostico, ver probe_rom */
static uint64_t audio_digest;        /* huella FNV-1a de las muestras del frame en curso */
static uint64_t audio_sample_count;  /* muestras estereo del frame en curso */
/* Solo para @generated-fm: el core carga por RUTA cuando no se le da
   GAME_INFO_EXT, y el ROM sintetico no esta en disco. */
static struct retro_game_info_ext generated_game_info;
static int generated_active;

/* La misma FNV-1a de tests/fuzz: la huella no tiene que ser criptografica,
   tiene que ser la misma funcion en las dos corridas. */
#define FNV_OFFSET 0xcbf29ce484222325ull
#define GENERATED_FM_ROM "@generated-fm-busy"
static uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
  const uint8_t *p = (const uint8_t *)data;
  size_t i;
  for (i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
  return h;
}

static bool environment_callback(unsigned command, void *data)
{
  switch (command)
  {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return data && (*(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565);

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
      if (data) *(int *)data = 3;
      return data != NULL;

    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      if (!generated_active) return false;
      if (data) *(const struct retro_game_info_ext **)data = &generated_game_info;
      return data != NULL;

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

  video_valid = 0;
  video_width = width;
  video_height = height;
  video_size = 0;
  if (!data || !width || !height || pitch < row_bytes)
    return;

  if (required > video_capacity)
  {
    next = (uint8_t *)realloc(video_frame, required);
    if (!next)
      return;
    video_frame = next;
    video_capacity = required;
  }

  for (row = 0; row < height; ++row)
  {
    memcpy(video_frame + row * row_bytes,
           (const uint8_t *)data + row * pitch, row_bytes);
  }
  video_size = required;
  video_valid = 1;
}

/* #117: el audio se JUZGA, no se descarta. La huella de las muestras de
   cada frame es lo que se compara entre la continuacion original y la
   restaurada; antes el probe tiraba el audio y un savestate que arrancara
   con un escalon (#93) habria pasado igual. */
static void audio_sample_callback(int16_t left, int16_t right)
{
  int16_t pair[2];
  pair[0] = left;
  pair[1] = right;
  audio_digest = fnv1a(audio_digest, pair, sizeof(pair));
  ++audio_sample_count;
}

static size_t audio_batch_callback(const int16_t *data, size_t frames)
{
  if (data && frames)
    audio_digest = fnv1a(audio_digest, data, frames * 2 * sizeof(int16_t));
  audio_sample_count += frames;
  return frames;
}

static void input_poll_callback(void)
{
}

static int16_t input_state_callback(unsigned port, unsigned device,
                                    unsigned index, unsigned id)
{
  if (!auto_input || port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0)
    return 0;

  if (id == RETRO_DEVICE_ID_JOYPAD_START)
    return (current_frame >= 60 && ((current_frame - 60) % 180) < 2);
  if (id == RETRO_DEVICE_ID_JOYPAD_A)
    return (current_frame >= 90 && ((current_frame - 90) % 120) < 2);
  return 0;
}

static int load_function(library_t library, const char *name,
                         void *destination, size_t destination_size)
{
  void *symbol = library_symbol(library, name);
  if (!symbol)
  {
    fprintf(stderr, "missing dynamic export: %s\n", name);
    return 0;
  }
  if (destination_size != sizeof(symbol))
  {
    fprintf(stderr, "unsupported function pointer size for %s\n", name);
    return 0;
  }
  memcpy(destination, &symbol, sizeof(symbol));
  return 1;
}

#define LOAD_API(api, library, field, symbol) \
  load_function((library), (symbol), &(api)->field, sizeof((api)->field))

static int load_api(library_t library, struct core_api *api)
{
  memset(api, 0, sizeof(*api));
  if (!(LOAD_API(api, library, set_environment, "retro_set_environment") &&
    LOAD_API(api, library, set_video_refresh, "retro_set_video_refresh") &&
    LOAD_API(api, library, set_audio_sample, "retro_set_audio_sample") &&
    LOAD_API(api, library, set_audio_sample_batch, "retro_set_audio_sample_batch") &&
    LOAD_API(api, library, set_input_poll, "retro_set_input_poll") &&
    LOAD_API(api, library, set_input_state, "retro_set_input_state") &&
    LOAD_API(api, library, init, "retro_init") &&
    LOAD_API(api, library, deinit, "retro_deinit") &&
    LOAD_API(api, library, load_game, "retro_load_game") &&
    LOAD_API(api, library, unload_game, "retro_unload_game") &&
    LOAD_API(api, library, run, "retro_run") &&
    LOAD_API(api, library, serialize_size, "retro_serialize_size") &&
    LOAD_API(api, library, serialize, "retro_serialize") &&
    LOAD_API(api, library, unserialize, "retro_unserialize") &&
    LOAD_API(api, library, get_memory_data, "retro_get_memory_data") &&
    LOAD_API(api, library, get_memory_size, "retro_get_memory_size") &&
    LOAD_API(api, library, get_ayther_interface, "ayther_get_interface")))
    return 0;

  api->ayther = api->get_ayther_interface(AYTHER_ABI_VERSION_1_0);
  if (!api->ayther ||
      api->ayther->struct_size <
        offsetof(ayther_interface_v1, recompose_frame) +
        sizeof(api->ayther->recompose_frame) ||
      !(api->ayther->capabilities & AYTHER_CAP_FRAME_SNAPSHOT) ||
      !(api->ayther->capabilities & AYTHER_CAP_RECOMPOSE_V1) ||
      !api->ayther->capture_snapshot || !api->ayther->recompose_frame)
  {
    fprintf(stderr, "AYTHER ABI v1 lacks snapshot/recomposition capabilities\n");
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

static uint8_t *read_file(const char *path, size_t *size)
{
  FILE *file;
  long length;
  uint8_t *data;

  *size = 0;
  file = fopen(path, "rb");
  if (!file)
  {
    fprintf(stderr, "cannot open ROM %s: %s\n", path, strerror(errno));
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0)
  {
    fprintf(stderr, "cannot measure ROM %s\n", path);
    fclose(file);
    return NULL;
  }
  data = (uint8_t *)malloc((size_t)length);
  if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
  {
    fprintf(stderr, "cannot read ROM %s\n", path);
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *size = (size_t)length;
  return data;
}

/* #117: "@generated-fm-busy" en vez de una ruta construye en memoria el
   ROM sintetico con tres voces de FM y el 68000 corriendo libre
   (generated_rom.c): asi el camino del checkpoint corre en CI, donde no hay
   ROMs comerciales, audio_digest tiene algo que digerir y el estado
   serializado es sensible a la fase del refresh del bus (#118). */
static uint8_t *read_rom(const char *path, size_t *size)
{
  uint8_t *rom;
  if (strcmp(path, GENERATED_FM_ROM) != 0)
    return read_file(path, size);
  rom = (uint8_t *)calloc(1, AYTHER_GENERATED_ROM_SIZE);
  if (!rom)
    return NULL;
  *size = ayther_build_generated_rom_fm_busy(rom, AYTHER_GENERATED_ROM_SIZE);
  if (!*size)
  {
    fprintf(stderr, "cannot build the generated FM ROM\n");
    free(rom);
    return NULL;
  }
  return rom;
}

static uint64_t different_pixels(const uint16_t *left, const uint16_t *right,
                                 size_t count)
{
  size_t i;
  uint64_t different = 0;
  for (i = 0; i < count; ++i)
    different += left[i] != right[i];
  return different;
}

static void dump_ppm(const char *suffix, const uint16_t *pixels,
                     unsigned width, unsigned height)
{
  char path[1024];
  FILE *file;
  size_t count = (size_t)width * height;
  size_t i;

  if (!dump_prefix || snprintf(path, sizeof(path), "%s-%s.ppm",
                               dump_prefix, suffix) >= (int)sizeof(path))
    return;
  file = fopen(path, "wb");
  if (!file)
  {
    fprintf(stderr, "cannot create diagnostic image %s\n", path);
    return;
  }
  fprintf(file, "P6\n%u %u\n255\n", width, height);
  for (i = 0; i < count; ++i)
  {
    uint16_t pixel = pixels[i];
    unsigned red = (pixel >> 11) & 0x1f;
    unsigned green = (pixel >> 5) & 0x3f;
    unsigned blue = pixel & 0x1f;
    fputc((red << 3) | (red >> 2), file);
    fputc((green << 2) | (green >> 4), file);
    fputc((blue << 3) | (blue >> 2), file);
  }
  fclose(file);
}

static const char *base_name(const char *path)
{
  const char *slash = strrchr(path, '/');
  const char *backslash = strrchr(path, '\\');
  if (!slash || (backslash && backslash > slash))
    slash = backslash;
  return slash ? slash + 1 : path;
}

static void json_string(FILE *output, const char *value)
{
  const unsigned char *cursor = (const unsigned char *)value;
  fputc('"', output);
  while (*cursor)
  {
    switch (*cursor)
    {
      case '"': fputs("\\\"", output); break;
      case '\\': fputs("\\\\", output); break;
      case '\b': fputs("\\b", output); break;
      case '\f': fputs("\\f", output); break;
      case '\n': fputs("\\n", output); break;
      case '\r': fputs("\\r", output); break;
      case '\t': fputs("\\t", output); break;
      default:
        if (*cursor < 0x20)
          fprintf(output, "\\u%04x", *cursor);
        else
          fputc(*cursor, output);
        break;
    }
    ++cursor;
  }
  fputc('"', output);
}

static void count_reasons(struct rom_stats *stats, uint32_t mask)
{
  int reason;
  for (reason = 0; reason < REASON_COUNT; ++reason)
    stats->reason_frames[reason] += (mask & (1u << reason)) != 0;
}

/* #117: un frame con su contrato. Era el cuerpo del bucle de probe_rom;
   separado para poder correrlo DOS veces sobre la misma sesion: la partida
   original y la continuacion restaurada desde el checkpoint. Devuelve 1 si
   el frame se juzgo (bien o mal, eso queda en stats) y 0 si la ABI misma
   fallo y no hay nada que juzgar. */
struct frame_context
{
  const struct core_api *api;
  const char *path;
  uint16_t *recomposed;
  struct rom_stats *stats;
  const uint32_t *dirty;
  const uint8_t *regs;
  uint8_t previous_regs[0x20];
  int previous_regs_valid;
  uint64_t expected_generation;   /* la que tiene que traer el snapshot */
  int generation_unknown;         /* recien restaurado: se toma la que venga */
  uint32_t mask;                  /* salida: motivos del frame recien corrido */
};

static int probe_frame(struct frame_context *ctx, int frame)
{
  const struct core_api *api = ctx->api;
  struct rom_stats *stats = ctx->stats;
  const char *path = ctx->path;
  ayther_frame_snapshot_v1 snapshot;
  uint32_t mask;
  uint32_t out_width = 0;
  uint32_t out_height = 0;
  int32_t abi_status;
  int available;
  int equal;
  int same_dimensions;
  size_t expected_size;
  uint64_t pixel_delta = 0;
  uint32_t reg_changes = 0;
  int reg_index;
  uint8_t regs_before[0x20];
  int have_regs_before = ctx->previous_regs_valid;

  current_frame = frame;
  video_valid = 0;
  audio_digest = FNV_OFFSET;
  audio_sample_count = 0;
  api->run();
  mask = *ctx->dirty;
  ctx->mask = mask;
  memset(&snapshot, 0, sizeof(snapshot));
  abi_status = api->ayther->capture_snapshot(&snapshot, sizeof(snapshot));
  /* Tras restaurar un checkpoint no se sabe si la generacion sigue la cuenta
     de la sesion o vuelve a la del blob: se acepta la del primer frame y de
     ahi en adelante tiene que crecer de a uno, igual que siempre. */
  if (ctx->generation_unknown && abi_status == AYTHER_STATUS_OK)
  {
    ctx->expected_generation = snapshot.frame_generation;
    ctx->generation_unknown = 0;
  }
  if (abi_status != AYTHER_STATUS_OK ||
      snapshot.frame_generation != ctx->expected_generation ||
      snapshot.fallback_reasons != mask ||
      snapshot.parsed_sprite_count > 128 ||
      snapshot.audio_write_count > 8192)
  {
    fprintf(stderr,
            "ABI snapshot mismatch in %s frame %d: status=%d frame=%" PRIu64
            " (expected %" PRIu64 ") fallback=%u/%u sprites=%u audio=%u\n",
            path, frame, (int)abi_status, snapshot.frame_generation,
            ctx->expected_generation, snapshot.fallback_reasons, mask,
            snapshot.parsed_sprite_count, snapshot.audio_write_count);
    return 0;
  }
  ++ctx->expected_generation;
  if (ctx->previous_regs_valid)
  {
    memcpy(regs_before, ctx->previous_regs, sizeof(regs_before));
    for (reg_index = 0; reg_index < (int)sizeof(ctx->previous_regs); ++reg_index)
      if (ctx->previous_regs[reg_index] != ctx->regs[reg_index])
        reg_changes |= 1u << reg_index;
  }
  memcpy(ctx->previous_regs, ctx->regs, sizeof(ctx->previous_regs));
  ctx->previous_regs_valid = 1;
  count_reasons(stats, mask);

  if (!video_valid)
  {
    ++stats->missing_video;
    return 1;
  }

  abi_status = api->ayther->recompose_frame(ctx->recomposed,
      MAX_RECOMPOSE_PIXELS, 0, &out_width, &out_height);
  available = abi_status == AYTHER_STATUS_OK;
  stats->video_width = video_width;
  stats->video_height = video_height;
  stats->recompose_width = out_width;
  stats->recompose_height = out_height;
  if (!available)
  {
    /* #40: "no puedo con este modo" es una RESPUESTA, no un fallo. El
       probe solo conocia AYTHER_STATUS_UNSUPPORTED, que era el unico
       codigo que existia cuando se escribio; desde entonces la ABI
       desglosa el motivo, y con un cartucho de Master System devuelve
       RC_NOT_MODE5. Tratar eso como violacion del contrato hacia que el
       probe se negara a validar el modo entero -- justo el que #40
       viene a cubrir-. Lo que SI es una violacion es no poder recomponer
       y no decir por que, y eso se sigue contando aparte. */
    if (abi_status != AYTHER_STATUS_UNSUPPORTED &&
        abi_status != AYTHER_STATUS_UNSUPPORTED_MODE &&
        abi_status != AYTHER_STATUS_RC_NOT_MODE5 &&
        abi_status != AYTHER_STATUS_RC_INTERLACE2 &&
        abi_status != AYTHER_STATUS_RC_NTSC_FILTER &&
        abi_status != AYTHER_STATUS_RC_JOURNAL_OVERFLOW)
    {
      fprintf(stderr, "ABI recomposition failed in %s frame %d: %d\n",
              path, frame, (int)abi_status);
      return 0;
    }
    /* Los motivos que la ABI nombra explicitamente ya vienen guardados
       por su propio codigo: no hace falta que ademas esten en la mascara
       de raster, que es del frame y no de la llamada. */
    if (abi_status == AYTHER_STATUS_RC_NOT_MODE5 ||
        abi_status == AYTHER_STATUS_RC_INTERLACE2 ||
        abi_status == AYTHER_STATUS_RC_NTSC_FILTER ||
        abi_status == AYTHER_STATUS_RC_JOURNAL_OVERFLOW ||
        abi_status == AYTHER_STATUS_UNSUPPORTED_MODE)
    {
      ++stats->unsupported_guarded;
      return 1;
    }
    if (mask & AYTHER_RASTER_REASON_UNSUPPORTED_MODE)
      ++stats->unsupported_guarded;
    else
    {
      ++stats->unavailable_without_reason;
      if (stats->first_failure_frame < 0)
      {
        stats->first_failure_frame = frame;
        stats->first_failure_mask = mask;
        stats->first_failure_reg_changes = reg_changes;
        if (have_regs_before)
          memcpy(stats->first_failure_previous_regs, regs_before,
                 sizeof(regs_before));
        memcpy(stats->first_failure_current_regs, ctx->regs,
               sizeof(stats->first_failure_current_regs));
      }
    }
    return 1;
  }

  expected_size = (size_t)out_width * out_height * sizeof(uint16_t);
  same_dimensions = out_width > 0 && out_height > 0 &&
    video_width == out_width && video_height == out_height &&
    video_size == expected_size;
  equal = same_dimensions && memcmp(video_frame, ctx->recomposed, expected_size) == 0;
  if (equal)
  {
    if (mask) ++stats->guarded_equal;
    else ++stats->clean_equal;
    return 1;
  }

  if (same_dimensions)
  {
    pixel_delta = different_pixels((const uint16_t *)video_frame, ctx->recomposed,
                                    expected_size / sizeof(uint16_t));
    stats->different_pixels += pixel_delta;
  }
  if (mask)
    ++stats->guarded_mismatch;
  else
    ++stats->clean_mismatch;
  if (!mask && stats->first_failure_frame < 0)
  {
    stats->first_failure_frame = frame;
    stats->first_failure_mask = mask;
    stats->first_failure_reg_changes = reg_changes;
    stats->first_failure_pixels = pixel_delta;
    if (have_regs_before)
      memcpy(stats->first_failure_previous_regs, regs_before,
             sizeof(regs_before));
    memcpy(stats->first_failure_current_regs, ctx->regs,
           sizeof(stats->first_failure_current_regs));
    if (same_dimensions)
    {
      dump_ppm("original", (const uint16_t *)video_frame,
               video_width, video_height);
      dump_ppm("recomposed", ctx->recomposed, video_width, video_height);
    }
  }
  return 1;
}

/* #117: lo que se guarda de cada frame de la continuacion original para
   compararlo con el restaurado. Huellas, no frames: 1.800 frames RGB565 de
   320x224 son 258 MB por ROM y lo que se afirma es igualdad, no que pixel. */
struct continuation_record
{
  uint8_t *state;   /* solo con --state-diff: el blob serializado tras el frame */
  uint64_t state_digest;   /* siempre: huella del estado serializado tras el frame */
  uint64_t video;
  uint64_t audio;
  uint64_t samples;
  uint32_t mask;
  int video_valid;
};

/* --state-diff (#117, diagnostico): cuando la continuacion restaurada se
   aparta de la original, lo primero que hay que saber es QUE parte del estado
   emulado difiere, y en que frame. Se serializa el estado despues de cada
   frame en las dos corridas y se compara byte a byte; el primer frame que
   difiere se imprime con sus rangos de bytes, para mapearlos al layout del
   blob (io_reg, vdp, sound, m68k, z80...). Guarda un blob entero por frame:
   solo tiene sentido con una ventana corta de frames tras el checkpoint. */
static void report_state_diff(const char *path, int frame, const uint8_t *a,
                              const uint8_t *b, size_t n)
{
  size_t i = 0;
  int ranges = 0;
  size_t total = 0;
  fprintf(stderr, "state-diff %s: el estado serializado difiere por primera vez"
          " tras el frame %d\n", base_name(path), frame);
  while (i < n)
  {
    size_t start;
    size_t k;
    if (a[i] == b[i]) { ++i; continue; }
    start = i;
    while (i < n && (a[i] != b[i] || (i + 8 < n && memcmp(a + i, b + i, 8) != 0)))
      ++i;
    total += i - start;
    if (ranges < 24)
    {
      fprintf(stderr, "  [%8" PRIu64 " +%-6" PRIu64 "] original:",
              (uint64_t)start, (uint64_t)(i - start));
      for (k = start; k < i && k < start + 8; ++k) fprintf(stderr, " %02x", a[k]);
      fprintf(stderr, "  restaurado:");
      for (k = start; k < i && k < start + 8; ++k) fprintf(stderr, " %02x", b[k]);
      fputc('\n', stderr);
    }
    ++ranges;
  }
  fprintf(stderr, "  %d rangos, %" PRIu64 " bytes distintos de %" PRIu64 "\n",
          ranges, (uint64_t)total, (uint64_t)n);
}

static int probe_rom(const struct core_api *api, const char *path, int frames,
                     uint16_t *recomposed, struct rom_stats *stats,
                     size_t *rom_size)
{
  struct retro_game_info game;
  struct frame_context ctx;
  uint8_t *rom_data;
  uint32_t *dirty;
  uint8_t *regs;
  uint8_t *checkpoint = NULL;
  size_t checkpoint_size = 0;
  struct continuation_record *record = NULL;
  uint8_t *checkpoint_scratch = NULL;
  int state_diff_reported = 0;
  int loaded = 0;
  int frame;
  int ok = 0;

  memset(stats, 0, sizeof(*stats));
  stats->first_failure_frame = -1;
  stats->restored_first_mismatch = -1;
  stats->restored_first_state_mismatch = -1;
  stats->checkpoint_frame = checkpoint_frame;
  rom_data = read_rom(path, rom_size);
  if (!rom_data)
    return 0;

  memset(&game, 0, sizeof(game));
  generated_active = strcmp(path, GENERATED_FM_ROM) == 0;
  if (generated_active)
  {
    memset(&generated_game_info, 0, sizeof(generated_game_info));
    generated_game_info.full_path = "ayther-generated-fm.md";
    generated_game_info.dir = ".";
    generated_game_info.name = "ayther-generated-fm";
    generated_game_info.ext = "md";
    generated_game_info.data = rom_data;
    generated_game_info.size = *rom_size;
    generated_game_info.persistent_data = true;
  }
  game.path = generated_active ? generated_game_info.full_path : path;
  game.data = rom_data;
  game.size = *rom_size;

  install_callbacks(api);
  api->init();
  if (!api->load_game(&game))
  {
    fprintf(stderr, "retro_load_game failed for %s\n", path);
    goto cleanup;
  }
  loaded = 1;

  /* #35.4: recomponer pasa por suscripcion desde que las suscripciones
     existen, y este probe no se habia enterado: pedia la recomposicion en
     el frame 0 y se comia un NOT_SUBSCRIBED, o sea que fallaba con
     CUALQUIER ROM. El informe que hay en docs/validation es de antes de
     ese cambio.

     Y va DESPUES de load_game, no al negociar la ABI: cargar contenido
     reinicia la sesion y con ella las suscripciones. Suscribirse antes
     compila, corre, y no sirve para nada -- que es la peor de las tres
     cosas-.

     Un probe que solo se corre a mano puede quedarse roto mucho tiempo sin
     que nadie lo note; por eso `make -C tests check-rom-probe` lo ejercita
     con el ROM sintetico de FM, checkpoint incluido (#117). */
  if (AYTHER_IFACE_HAS(api->ayther, set_subscriptions) &&
      api->ayther->set_subscriptions)
    api->ayther->set_subscriptions(AYTHER_SUB_RECOMPOSITION |
                                   AYTHER_SUB_RASTER_TRACKING |
                                   AYTHER_SUB_VDP_MEMORY);
  if (api->get_memory_size(AYTHER_MEMORY_RASTER_DIRTY) != sizeof(uint32_t))
  {
    fprintf(stderr, "private memory id 0x10E is not four bytes\n");
    goto cleanup;
  }
  dirty = (uint32_t *)api->get_memory_data(AYTHER_MEMORY_RASTER_DIRTY);
  if (!dirty)
  {
    fprintf(stderr, "private memory id 0x10E returned NULL\n");
    goto cleanup;
  }
  regs = (uint8_t *)api->get_memory_data(AYTHER_MEMORY_VDP_REGS);
  if (!regs || api->get_memory_size(AYTHER_MEMORY_VDP_REGS) != sizeof(ctx.previous_regs))
  {
    fprintf(stderr, "private memory id 0x101 did not expose 32 VDP registers\n");
    goto cleanup;
  }

  memset(&ctx, 0, sizeof(ctx));
  ctx.api = api;
  ctx.path = path;
  ctx.recomposed = recomposed;
  ctx.stats = stats;
  ctx.dirty = dirty;
  ctx.regs = regs;
  ctx.expected_generation = 1;

  if (checkpoint_frame >= 0)
  {
    record = (struct continuation_record *)calloc((size_t)(frames - checkpoint_frame),
                                                  sizeof(*record));
    if (!record)
    {
      fprintf(stderr, "cannot allocate the continuation record for %s\n", path);
      goto cleanup;
    }
  }

  /* La partida original, entera. En el frame del checkpoint -ANTES de
     correrlo- se serializa, y de ahi al final se guarda la huella de cada
     frame: eso es "la continuacion original". */
  for (frame = 0; frame < frames; ++frame)
  {
    if (frame == checkpoint_frame)
    {
      checkpoint_size = api->serialize_size();
      checkpoint = checkpoint_size ? (uint8_t *)calloc(1, checkpoint_size) : NULL;
      if (!checkpoint || !api->serialize(checkpoint, checkpoint_size))
      {
        fprintf(stderr, "cannot serialize %s at frame %d (%" PRIu64 " bytes)\n",
                path, frame, (uint64_t)checkpoint_size);
        stats->checkpoint_failed = 1;
        goto cleanup;
      }
      stats->checkpoint_bytes = checkpoint_size;
      checkpoint_scratch = (uint8_t *)calloc(1, checkpoint_size);
      if (!checkpoint_scratch) goto cleanup;
    }
    if (!probe_frame(&ctx, frame))
      goto cleanup;
    if (record && frame >= checkpoint_frame)
    {
      struct continuation_record *r = &record[frame - checkpoint_frame];
      r->video_valid = video_valid;
      r->video = video_valid ? fnv1a(FNV_OFFSET, video_frame, video_size) : 0;
      r->audio = audio_digest;
      r->samples = audio_sample_count;
      r->mask = ctx.mask;
      /* El estado serializado tras el frame, como huella: lo que los CPUs y
         los chips tienen adentro, no solo lo que sale por video y audio. Un
         campo que no viaja en el savestate se delata aca antes de que se
         vea o se oiga (#118: la fase del refresh del 68000). */
      if (!api->serialize(checkpoint_scratch, checkpoint_size))
      {
        fprintf(stderr, "cannot serialize %s after frame %d\n", path, frame);
        goto cleanup;
      }
      r->state_digest = fnv1a(FNV_OFFSET, checkpoint_scratch, checkpoint_size);
      if (state_diff)
      {
        r->state = (uint8_t *)calloc(1, checkpoint_size);
        if (!r->state) goto cleanup;
        memcpy(r->state, checkpoint_scratch, checkpoint_size);
      }
    }
  }

  /* La continuacion restaurada: mismo proceso, misma sesion, mismo ROM
     cargado; se vuelve al checkpoint y se corren los mismos frames con la
     misma entrada (que es funcion del numero de frame). Video y audio
     tienen que dar la misma huella frame a frame, y el contrato de
     recomposicion se sigue afirmando en cada uno. La mascara de motivos
     puede diferir (restaurar marca memoria como sucia): se cuenta, no
     falla. */
  if (record)
  {
    if (!api->unserialize(checkpoint, checkpoint_size))
    {
      fprintf(stderr, "cannot restore the checkpoint of %s (frame %d, %" PRIu64
              " bytes)\n", path, checkpoint_frame, (uint64_t)checkpoint_size);
      stats->checkpoint_failed = 1;
      goto cleanup;
    }
    ctx.previous_regs_valid = 0;
    ctx.generation_unknown = 1;
    for (frame = checkpoint_frame; frame < frames; ++frame)
    {
      const struct continuation_record *r = &record[frame - checkpoint_frame];
      uint64_t video;
      int video_equal;
      int audio_equal;
      if (!probe_frame(&ctx, frame))
        goto cleanup;
      ++stats->restored_frames;
      video = video_valid ? fnv1a(FNV_OFFSET, video_frame, video_size) : 0;
      video_equal = video_valid == r->video_valid && video == r->video;
      audio_equal = audio_digest == r->audio && audio_sample_count == r->samples;
      if (video_equal) ++stats->restored_video_equal;
      else ++stats->restored_video_mismatch;
      if (audio_equal) ++stats->restored_audio_equal;
      else ++stats->restored_audio_mismatch;
      if (ctx.mask != r->mask) ++stats->restored_mask_differs;
      if (!api->serialize(checkpoint_scratch, checkpoint_size))
      {
        fprintf(stderr, "cannot serialize %s after restored frame %d\n", path, frame);
        goto cleanup;
      }
      if (fnv1a(FNV_OFFSET, checkpoint_scratch, checkpoint_size) == r->state_digest)
        ++stats->restored_state_equal;
      else
      {
        ++stats->restored_state_mismatch;
        if (stats->restored_first_state_mismatch < 0)
          stats->restored_first_state_mismatch = frame;
        if (state_diff && r->state && !state_diff_reported)
        {
          report_state_diff(path, frame, r->state, checkpoint_scratch, checkpoint_size);
          state_diff_reported = 1;
        }
      }
      if (!(video_equal && audio_equal) && stats->restored_first_mismatch < 0)
      {
        stats->restored_first_mismatch = frame;
        stats->restored_first_video_equal = video_equal;
        stats->restored_first_audio_equal = audio_equal;
        stats->restored_first_samples = audio_sample_count;
        stats->restored_first_samples_expected = r->samples;
      }
    }
  }
  ok = 1;

cleanup:
  if (loaded) api->unload_game();
  api->deinit();
  free(rom_data);
  free(checkpoint);
  free(checkpoint_scratch);
  if (record)
    for (frame = 0; frame < frames - checkpoint_frame; ++frame)
      free(record[frame].state);
  free(record);
  return ok;
}

static void write_rom_result(FILE *output, const char *path, size_t rom_size,
                             int frames, const struct rom_stats *stats)
{
  int reason;
  fputs("{\"type\":\"rom\",\"rom\":", output);
  json_string(output, base_name(path));
  fprintf(output,
          ",\"rom_bytes\":%" PRIu64
          ",\"frames\":%d,\"categories\":{"
          "\"clean_equal\":%" PRIu64
          ",\"guarded_equal\":%" PRIu64
          ",\"clean_mismatch\":%" PRIu64
          ",\"guarded_mismatch\":%" PRIu64
          ",\"unsupported_guarded\":%" PRIu64
          ",\"unavailable_without_reason\":%" PRIu64
          ",\"missing_video\":%" PRIu64 "},\"reason_frames\":{",
          (uint64_t)rom_size, frames,
          stats->clean_equal, stats->guarded_equal, stats->clean_mismatch,
          stats->guarded_mismatch, stats->unsupported_guarded,
          stats->unavailable_without_reason, stats->missing_video);
  for (reason = 0; reason < REASON_COUNT; ++reason)
  {
    if (reason) fputc(',', output);
    json_string(output, reason_names[reason]);
    fprintf(output, ":%" PRIu64, stats->reason_frames[reason]);
  }
  fprintf(output,
          "},\"different_pixels\":%" PRIu64
          ",\"dimensions\":\"%ux%u/%dx%d\",\"first_failure\":",
          stats->different_pixels, stats->video_width, stats->video_height,
          stats->recompose_width, stats->recompose_height);
  if (stats->first_failure_frame < 0)
    fputs("null", output);
  else
  {
    int reg_index;
    fprintf(output,
            "{\"frame\":%d,\"mask\":%u,\"reg_changes\":%u,"
            "\"different_pixels\":%" PRIu64
            ",\"previous_regs\":\"",
            stats->first_failure_frame, stats->first_failure_mask,
            stats->first_failure_reg_changes,
            stats->first_failure_pixels);
    for (reg_index = 0; reg_index < 0x20; ++reg_index)
      fprintf(output, "%02x", stats->first_failure_previous_regs[reg_index]);
    fputs("\",\"current_regs\":\"", output);
    for (reg_index = 0; reg_index < 0x20; ++reg_index)
      fprintf(output, "%02x", stats->first_failure_current_regs[reg_index]);
    fputs("\"}", output);
  }
  fputs(",\"checkpoint\":", output);
  if (stats->checkpoint_frame < 0)
    fputs("null", output);
  else
  {
    fprintf(output,
            "{\"frame\":%d,\"bytes\":%" PRIu64
            ",\"restored_frames\":%" PRIu64
            ",\"video_equal\":%" PRIu64
            ",\"video_mismatch\":%" PRIu64
            ",\"audio_equal\":%" PRIu64
            ",\"audio_mismatch\":%" PRIu64
            ",\"mask_differs\":%" PRIu64
            ",\"state_equal\":%" PRIu64
            ",\"state_mismatch\":%" PRIu64
            ",\"first_state_mismatch\":%d,\"first_mismatch\":",
            stats->checkpoint_frame, (uint64_t)stats->checkpoint_bytes,
            stats->restored_frames, stats->restored_video_equal,
            stats->restored_video_mismatch, stats->restored_audio_equal,
            stats->restored_audio_mismatch, stats->restored_mask_differs,
            stats->restored_state_equal, stats->restored_state_mismatch,
            stats->restored_first_state_mismatch);
    if (stats->restored_first_mismatch < 0)
      fputs("null", output);
    else
      fprintf(output,
              "{\"frame\":%d,\"video_equal\":%s,\"audio_equal\":%s"
              ",\"samples\":%" PRIu64 ",\"samples_expected\":%" PRIu64 "}",
              stats->restored_first_mismatch,
              stats->restored_first_video_equal ? "true" : "false",
              stats->restored_first_audio_equal ? "true" : "false",
              stats->restored_first_samples,
              stats->restored_first_samples_expected);
    fputc('}', output);
  }
  fputs("}\n", output);
  fflush(output);
}

static void usage(const char *program)
{
  fprintf(stderr,
          "usage: %s [--frames N] [--checkpoint N[,N...]] [--no-auto-input] [--output FILE] "
          "[--dump-prefix PATH] CORE ROM|@generated-fm [ROM...]\n"
          "  --checkpoint N  serializa en el frame N, corre hasta --frames, restaura y\n"
          "                  vuelve a correr N..frames: video y audio tienen que dar la\n"
          "                  misma huella frame a frame (#117)\n", program);
}

int main(int argc, char **argv)
{
  struct core_api api;
  struct rom_stats total;
  struct rom_stats stats;
  library_t library;
  uint16_t *recomposed;
  const char *core_path = NULL;
  const char *output_path = NULL;
  FILE *output = stdout;
  int frames = 600;
  int first_rom;
  int rom;
  int failures = 0;
  int probed = 0;

  memset(&total, 0, sizeof(total));
  for (first_rom = 1; first_rom < argc; ++first_rom)
  {
    if (strcmp(argv[first_rom], "--frames") == 0 && first_rom + 1 < argc)
    {
      frames = atoi(argv[++first_rom]);
    }
    else if (strcmp(argv[first_rom], "--checkpoint") == 0 && first_rom + 1 < argc)
    {
      /* #123: una lista. Cada checkpoint es una corrida entera: la partida
         original hasta --frames mas la continuacion restaurada. */
      const char *list = argv[++first_rom];
      checkpoint_count = 0;
      while (*list && checkpoint_count < MAX_CHECKPOINTS)
      {
        char *end = NULL;
        long v = strtol(list, &end, 10);
        if (end == list) break;
        checkpoint_list[checkpoint_count++] = (int)v;
        list = *end == ',' ? end + 1 : end;
      }
      checkpoint_frame = checkpoint_count ? checkpoint_list[0] : -1;
    }
    else if (strcmp(argv[first_rom], "--state-diff") == 0)
    {
      state_diff = 1;
    }
    else if (strcmp(argv[first_rom], "--no-auto-input") == 0)
    {
      auto_input = 0;
    }
    else if (strcmp(argv[first_rom], "--output") == 0 && first_rom + 1 < argc)
    {
      output_path = argv[++first_rom];
    }
    else if (strcmp(argv[first_rom], "--dump-prefix") == 0 && first_rom + 1 < argc)
    {
      dump_prefix = argv[++first_rom];
    }
    else
    {
      core_path = argv[first_rom++];
      break;
    }
  }
  {
    int k;
    for (k = 0; k < checkpoint_count; ++k)
      if (checkpoint_list[k] < 0 || checkpoint_list[k] >= frames)
      {
        fprintf(stderr, "--checkpoint %d tiene que estar entre 0 y --frames %d\n",
                checkpoint_list[k], frames);
        usage(argv[0]);
        return 2;
      }
  }
  if (!core_path || first_rom >= argc || frames <= 0)
  {
    usage(argv[0]);
    return 2;
  }
  if (!checkpoint_count) checkpoint_frame = -1;

  if (output_path)
  {
    output = fopen(output_path, "wb");
    if (!output)
    {
      fprintf(stderr, "cannot create report %s: %s\n",
              output_path, strerror(errno));
      return 1;
    }
  }

  library = library_open(core_path);
  if (!library)
  {
    library_error(core_path);
    if (output != stdout) fclose(output);
    return 1;
  }
  if (!load_api(library, &api))
  {
    library_close(library);
    if (output != stdout) fclose(output);
    return 1;
  }
  recomposed = (uint16_t *)malloc(MAX_RECOMPOSE_PIXELS * sizeof(uint16_t));
  if (!recomposed)
  {
    fprintf(stderr, "cannot allocate recomposition buffer\n");
    library_close(library);
    if (output != stdout) fclose(output);
    return 1;
  }

  /* #117: la identidad de la corrida va en el informe y no en la memoria de
     quien la lanzo: build_id del core, si es un perfil SOUND_PROBE=1 (lo
     delatan sus exports), el checkpoint y la secuencia de entrada. */
  fputs("{\"type\":\"core\",\"build_id\":", output);
  json_string(output, api.ayther->build_id ? api.ayther->build_id : "");
  fprintf(output, ",\"sound_probe\":%s,\"checkpoint_frame\":%d,\"checkpoints\":[",
          library_symbol(library, "audio_probe_get_context") ? "true" : "false",
          checkpoint_frame);
  {
    int k;
    for (k = 0; k < checkpoint_count; ++k)
      fprintf(output, "%s%d", k ? "," : "", checkpoint_list[k]);
  }
  fputs("],\"input\":", output);
  json_string(output, auto_input
              ? "port 0 joypad: START 2 frames every 180 from frame 60; "
                "A 2 frames every 120 from frame 90"
              : "none");
  fputs("}\n", output);

  for (rom = first_rom; rom < argc; ++rom)
  {
    /* #123: con varios checkpoints, la misma ROM se corre una vez por cada
       uno, de cero, y cada corrida deja su propia linea. Sin checkpoint,
       una sola corrida como siempre. */
    int runs = checkpoint_count ? checkpoint_count : 1;
    int k;
    for (k = 0; k < runs; ++k)
    {
      size_t rom_size = 0;
      int reason;
      checkpoint_frame = checkpoint_count ? checkpoint_list[k] : -1;
      if (checkpoint_count)
        fprintf(stderr, "[%d/%d] %s (checkpoint %d)\n", rom - first_rom + 1,
                argc - first_rom, base_name(argv[rom]), checkpoint_frame);
      else
        fprintf(stderr, "[%d/%d] %s\n", rom - first_rom + 1,
                argc - first_rom, base_name(argv[rom]));
      if (!probe_rom(&api, argv[rom], frames, recomposed, &stats, &rom_size))
      {
        ++failures;
        continue;
      }
      ++probed;
      write_rom_result(output, argv[rom], rom_size, frames, &stats);
      total.clean_equal += stats.clean_equal;
      total.guarded_equal += stats.guarded_equal;
      total.clean_mismatch += stats.clean_mismatch;
      total.guarded_mismatch += stats.guarded_mismatch;
      total.unsupported_guarded += stats.unsupported_guarded;
      total.unavailable_without_reason += stats.unavailable_without_reason;
      total.missing_video += stats.missing_video;
      total.different_pixels += stats.different_pixels;
      for (reason = 0; reason < REASON_COUNT; ++reason)
        total.reason_frames[reason] += stats.reason_frames[reason];
      total.restored_frames += stats.restored_frames;
      total.restored_video_mismatch += stats.restored_video_mismatch;
      total.restored_audio_mismatch += stats.restored_audio_mismatch;
      total.restored_mask_differs += stats.restored_mask_differs;
      total.restored_state_mismatch += stats.restored_state_mismatch;
      failures += stats.clean_mismatch > 0 || stats.unavailable_without_reason > 0 ||
                  stats.restored_video_mismatch > 0 || stats.restored_audio_mismatch > 0 ||
                  stats.restored_state_mismatch > 0;
    }
  }

  fprintf(output,
          "{\"type\":\"summary\",\"roms\":%d,\"frames\":%d,"
          "\"clean_equal\":%" PRIu64
          ",\"guarded_equal\":%" PRIu64
          ",\"clean_mismatch\":%" PRIu64
          ",\"guarded_mismatch\":%" PRIu64
          ",\"unsupported_guarded\":%" PRIu64
          ",\"unavailable_without_reason\":%" PRIu64
          ",\"missing_video\":%" PRIu64
          ",\"checkpoint_frame\":%d"
          ",\"restored_frames\":%" PRIu64
          ",\"restored_video_mismatch\":%" PRIu64
          ",\"restored_audio_mismatch\":%" PRIu64
          ",\"restored_mask_differs\":%" PRIu64
          ",\"restored_state_mismatch\":%" PRIu64
          ",\"checkpoints_per_rom\":%d"
          ",\"passed\":%s}\n",
          probed, probed * frames, total.clean_equal, total.guarded_equal,
          total.clean_mismatch, total.guarded_mismatch,
          total.unsupported_guarded, total.unavailable_without_reason,
          total.missing_video, checkpoint_frame, total.restored_frames,
          total.restored_video_mismatch, total.restored_audio_mismatch,
          total.restored_mask_differs, total.restored_state_mismatch,
          checkpoint_count, failures ? "false" : "true");

  free(video_frame);
  free(recomposed);
  library_close(library);
  if (output != stdout) fclose(output);
  return failures ? 2 : 0;
}
