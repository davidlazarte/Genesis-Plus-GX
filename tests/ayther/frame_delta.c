/* #30: el Frame Delta deja de consumirse al leerlo, y gana historial.
 *
 * Era consume-on-poll: leer el bitmap lo vaciaba. Con el Lab de debug y el
 * motor HD leyendo el MISMO frame, el segundo recibia un bitmap vacio y no
 * invalidaba sus assets. No es un caso raro, es el uso previsto.
 *
 * Las tres conductas que fija este test:
 *   1. dos lecturas del mismo frame devuelven lo mismo, y no vacio
 *   2. delta_since de una generacion reciente contiene al menos el ultimo frame
 *   3. una generacion fuera del ring devuelve DELTA_HISTORY_LOST con todo sucio
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "ayther_api.h"
#include "generated_rom.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE library_t;
static library_t open_library(const char *p) { return LoadLibraryA(p); }
static void *load_symbol(library_t l, const char *n) { return (void *)(uintptr_t)GetProcAddress(l, n); }
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *load_symbol(library_t l, const char *n) { return dlsym(l, n); }
#endif

#define ROM_SIZE AYTHER_GENERATED_ROM_SIZE
static struct retro_game_info_ext gi_ext;

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return data && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      if (data) *(bool *)data = true; return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (data) *(bool *)data = false; return data != NULL;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      if (data) *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return data != NULL;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (data) *(int *)data = 3; return data != NULL;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      if (data) *(const struct retro_game_info_ext **)data = &gi_ext;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data) *(const char **)data = "."; return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE: return false;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d,size_t f){(void)d;return f;}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

static unsigned count_bits(const uint8_t *p, size_t n)
{
  unsigned c = 0; size_t i; int b;
  for (i = 0; i < n; i++) for (b = 0; b < 8; b++) if (p[i] & (1u << b)) c++;
  return c;
}

#define SYM(v,n) do { *(void **)&v = load_symbol(lib,n); \
  if(!v){fprintf(stderr,"falta %s\n",n);return 2;} } while(0)

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }
  library_t lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  void (*p_set_env)(retro_environment_t); void (*p_set_vid)(retro_video_refresh_t);
  void (*p_set_aud)(retro_audio_sample_batch_t); void (*p_set_poll)(retro_input_poll_t);
  void (*p_set_inp)(retro_input_state_t); void (*p_init)(void);
  bool (*p_load)(const struct retro_game_info *); void (*p_run)(void);
  ayther_get_interface_fn p_iface;
  SYM(p_set_env,"retro_set_environment"); SYM(p_set_vid,"retro_set_video_refresh");
  SYM(p_set_aud,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_inp,"retro_set_input_state"); SYM(p_init,"retro_init");
  SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run");
  SYM(p_iface,"ayther_get_interface");

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom(rom, ROM_SIZE)) { fprintf(stderr,"ROM\n"); return 2; }
  memset(&gi_ext,0,sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr,"load_game fallo\n"); return 2; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr,"sin ABI\n"); return 2; }
  printf("ABI %u.%u\n", AYTHER_ABI_VERSION_MAJOR(api->abi_version),
                        AYTHER_ABI_VERSION_MINOR(api->abi_version));
  if (!AYTHER_IFACE_HAS(api, frame_delta_since)) {
    fprintf(stderr, "el descriptor no llega a frame_delta_since\n"); return 1;
  }

  api->set_subscriptions(AYTHER_SUB_VDP_MEMORY);
  { int f; for (f = 0; f < 20; f++) p_run(); }

  int fail = 0;
  static ayther_frame_delta_v1 a, b;

  if (api->poll_frame_delta(&a, sizeof(a)) != AYTHER_STATUS_OK ||
      api->poll_frame_delta(&b, sizeof(b)) != AYTHER_STATUS_OK) {
    fprintf(stderr, "poll fallo\n"); return 2;
  }
  {
    unsigned na = count_bits(a.dirty_patterns, sizeof(a.dirty_patterns));
    unsigned nb = count_bits(b.dirty_patterns, sizeof(b.dirty_patterns));
    int same = memcmp(a.dirty_patterns, b.dirty_patterns, sizeof(a.dirty_patterns)) == 0;
    printf("1. dos lecturas del mismo frame: %u y %u bits -> %s\n", na, nb,
           (same && na > 0) ? "IGUALES y no vacias (correcto)"
                            : (na > 0 ? "DISTINTAS (consume-on-poll)" : "vacias"));
    if (!same || na == 0) fail = 1;
  }
  {
    static ayther_frame_delta_v1 c;
    uint64_t from = (a.frame_generation > 3) ? a.frame_generation - 3 : 0;
    int32_t rc = api->frame_delta_since(from, &c, sizeof(c));
    unsigned nc = count_bits(c.dirty_patterns, sizeof(c.dirty_patterns));
    unsigned na = count_bits(a.dirty_patterns, sizeof(a.dirty_patterns));
    printf("2. delta_since(gen-3): rc=%d, %u bits (ultimo frame: %u) -> %s\n",
           (int)rc, nc, na, (rc == AYTHER_STATUS_OK && nc >= na) ? "OK" : "MAL");
    if (rc != AYTHER_STATUS_OK || nc < na) fail = 1;
  }
  {
    static ayther_frame_delta_v1 d;
    int32_t rc = api->frame_delta_since(0u, &d, sizeof(d));
    unsigned nd = count_bits(d.dirty_patterns, sizeof(d.dirty_patterns));
    unsigned all = (unsigned)(sizeof(d.dirty_patterns) * 8u);
    printf("3. delta_since(0) con 20 frames: rc=%d, %u/%u bits -> %s\n",
           (int)rc, nd, all,
           (rc == AYTHER_STATUS_DELTA_HISTORY_LOST && nd == all)
             ? "HISTORY_LOST con todo sucio (correcto)" : "MAL");
    if (rc != AYTHER_STATUS_DELTA_HISTORY_LOST || nd != all) fail = 1;
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
