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

static void audio_sample_callback(int16_t left, int16_t right)
{
  (void)left;
  (void)right;
}

static size_t audio_batch_callback(const int16_t *data, size_t frames)
{
  (void)data;
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

static int probe_rom(const struct core_api *api, const char *path, int frames,
                     uint16_t *recomposed, struct rom_stats *stats,
                     size_t *rom_size)
{
  struct retro_game_info game;
  uint8_t *rom_data;
  uint32_t *dirty;
  uint8_t *regs;
  uint8_t previous_regs[0x20];
  int previous_regs_valid = 0;
  int loaded = 0;
  int frame;

  memset(stats, 0, sizeof(*stats));
  stats->first_failure_frame = -1;
  rom_data = read_file(path, rom_size);
  if (!rom_data)
    return 0;

  memset(&game, 0, sizeof(game));
  game.path = path;
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
  if (!regs || api->get_memory_size(AYTHER_MEMORY_VDP_REGS) != sizeof(previous_regs))
  {
    fprintf(stderr, "private memory id 0x101 did not expose 32 VDP registers\n");
    goto cleanup;
  }

  for (frame = 0; frame < frames; ++frame)
  {
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
    int have_regs_before = previous_regs_valid;

    current_frame = frame;
    video_valid = 0;
    api->run();
    mask = *dirty;
    memset(&snapshot, 0, sizeof(snapshot));
    abi_status = api->ayther->capture_snapshot(&snapshot, sizeof(snapshot));
    if (abi_status != AYTHER_STATUS_OK ||
        snapshot.frame_generation != (uint64_t)(frame + 1) ||
        snapshot.fallback_reasons != mask ||
        snapshot.parsed_sprite_count > 128 ||
        snapshot.audio_write_count > 8192)
    {
      fprintf(stderr,
              "ABI snapshot mismatch in %s frame %d: status=%d frame=%" PRIu64
              " fallback=%u/%u sprites=%u audio=%u\n",
              path, frame, (int)abi_status, snapshot.frame_generation,
              snapshot.fallback_reasons, mask, snapshot.parsed_sprite_count,
              snapshot.audio_write_count);
      goto cleanup;
    }
    if (previous_regs_valid)
    {
      memcpy(regs_before, previous_regs, sizeof(regs_before));
      for (reg_index = 0; reg_index < (int)sizeof(previous_regs); ++reg_index)
        if (previous_regs[reg_index] != regs[reg_index])
          reg_changes |= 1u << reg_index;
    }
    memcpy(previous_regs, regs, sizeof(previous_regs));
    previous_regs_valid = 1;
    count_reasons(stats, mask);

    if (!video_valid)
    {
      ++stats->missing_video;
      continue;
    }

    abi_status = api->ayther->recompose_frame(recomposed,
        MAX_RECOMPOSE_PIXELS, 0, &out_width, &out_height);
    available = abi_status == AYTHER_STATUS_OK;
    stats->video_width = video_width;
    stats->video_height = video_height;
    stats->recompose_width = out_width;
    stats->recompose_height = out_height;
    if (!available)
    {
      if (abi_status != AYTHER_STATUS_UNSUPPORTED)
      {
        fprintf(stderr, "ABI recomposition failed in %s frame %d: %d\n",
                path, frame, (int)abi_status);
        goto cleanup;
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
          memcpy(stats->first_failure_current_regs, regs,
                 sizeof(stats->first_failure_current_regs));
        }
      }
      continue;
    }

    expected_size = (size_t)out_width * out_height * sizeof(uint16_t);
    same_dimensions = out_width > 0 && out_height > 0 &&
      video_width == out_width && video_height == out_height &&
      video_size == expected_size;
    equal = same_dimensions && memcmp(video_frame, recomposed, expected_size) == 0;
    if (equal)
    {
      if (mask) ++stats->guarded_equal;
      else ++stats->clean_equal;
      continue;
    }

    if (same_dimensions)
    {
      pixel_delta = different_pixels((const uint16_t *)video_frame, recomposed,
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
      memcpy(stats->first_failure_current_regs, regs,
             sizeof(stats->first_failure_current_regs));
      if (same_dimensions)
      {
        dump_ppm("original", (const uint16_t *)video_frame,
                 video_width, video_height);
        dump_ppm("recomposed", recomposed, video_width, video_height);
      }
    }
  }

  if (loaded) api->unload_game();
  api->deinit();
  free(rom_data);
  return 1;

cleanup:
  if (loaded) api->unload_game();
  api->deinit();
  free(rom_data);
  return 0;
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
  fputs("}\n", output);
  fflush(output);
}

static void usage(const char *program)
{
  fprintf(stderr,
          "usage: %s [--frames N] [--no-auto-input] [--output FILE] "
          "[--dump-prefix PATH] CORE ROM [ROM...]\n", program);
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
  if (!core_path || first_rom >= argc || frames <= 0)
  {
    usage(argv[0]);
    return 2;
  }

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

  for (rom = first_rom; rom < argc; ++rom)
  {
    size_t rom_size = 0;
    int reason;
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
    failures += stats.clean_mismatch > 0 || stats.unavailable_without_reason > 0;
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
          ",\"passed\":%s}\n",
          probed, probed * frames, total.clean_equal, total.guarded_equal,
          total.clean_mismatch, total.guarded_mismatch,
          total.unsupported_guarded, total.unavailable_without_reason,
          total.missing_video, failures ? "false" : "true");

  free(video_frame);
  free(recomposed);
  library_close(library);
  if (output != stdout) fclose(output);
  return failures ? 2 : 0;
}
