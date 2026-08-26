/* #77: el descriptor de sistema dice de que frame habla.
 *
 * Lo encontro la integracion en el Engine: en Sonic 2, el primer frame con
 * modo decia `vdp_mode = 5, h40 = 1, viewport = 256x192`. Contradictorio en
 * el mismo struct. La causa es del core: `viewport_w/h` e `interlace` salen de
 * `bitmap.viewport`, que system_frame_gen aplica al INICIO del frame
 * siguiente (bloque `changed & 2`), mientras `h40` salia de reg 12, que el
 * juego escribe durante el frame. Leido entre frames, el descriptor mezclaba
 * el frame que salio con el que viene.
 *
 * Desde 1.10: `h40` describe el frame emitido, como `viewport_w`, y
 * `flags & AYTHER_SYSTEM_GEOMETRY_PENDING` esta puesto exactamente mientras los
 * registros ya cambiaron la geometria y el frame que viene la aplica.
 *
 * El fixture de siempre reproduce la transicion: escribe reg 12 = H40 y reg 1
 * = Mode 5 durante el frame 1, que sale con la geometria de reset. Lo que se
 * afirma, frame por frame, con `video_refresh` como oraculo:
 *
 *   1. viewport_w/h == lo que video_refresh entrego en ESE frame, siempre;
 *   2. h40 == (viewport_w == 320), siempre;
 *   3. frame 1: 256x192 y PENDING puesto; frame 2: 320x224 y PENDING apagado;
 *   4. vdp_mode == 5 ya en el frame 1: el modo es estado de registro, y el
 *      descriptor lo dice, no lo esconde.
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
#define FRAMES   6

static struct retro_game_info_ext gi_ext;
static unsigned last_w, last_h;

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
static void vid_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)p; last_w = w; last_h = h; }
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

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
  if (AYTHER_ABI_VERSION_MINOR(api->abi_version) < 10) {
    fprintf(stderr, "ABI %u.%u: este test necesita 1.10\n",
            AYTHER_ABI_VERSION_MAJOR(api->abi_version),
            AYTHER_ABI_VERSION_MINOR(api->abi_version));
    return 1;
  }

  int fail = 0, f;
  for (f = 1; f <= FRAMES; f++)
  {
    ayther_system_v1 sys; memset(&sys, 0, sizeof(sys));
    int pending, ok;
    last_w = last_h = 0;
    p_run();
    if (api->read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
      printf("frame %d: no se puede leer SYSTEM\n", f); return 2;
    }
    pending = (sys.flags & AYTHER_SYSTEM_GEOMETRY_PENDING) != 0;
    ok = sys.viewport_w == last_w && sys.viewport_h == last_h &&
         sys.h40 == (sys.viewport_w == 320);
    if (f == 1) ok = ok && sys.viewport_w == 256 && sys.viewport_h == 192 &&
                     pending && sys.vdp_mode == 5;
    if (f == 2) ok = ok && sys.viewport_w == 320 && sys.viewport_h == 224 &&
                     !pending && sys.h40 == 1;
    if (f >  2) ok = ok && !pending;
    printf("frame %d: video_refresh %ux%u | SYSTEM viewport %ux%u h40=%u modo=%u"
           " pending=%d -> %s\n", f, last_w, last_h, sys.viewport_w, sys.viewport_h,
           sys.h40, sys.vdp_mode, pending, ok ? "correcto" : "MAL");
    if (!ok) fail = 1;
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
