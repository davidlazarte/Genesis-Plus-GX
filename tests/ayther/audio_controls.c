/* #29: que el mute por canal (0x10D) llegue a TODOS los cores de FM.
 *
 * ym2612.c aplica el mute sobre out_fm[6] antes del mix; ym3438.c (Nuked) no
 * tenia ningun hook, asi que con "nuked (ym3438)" seleccionado la mascara era
 * un no-op silencioso: el frontend pedia silencio y el chip seguia sonando.
 *
 * El test se auto-valida: corre la MISMA comprobacion sobre los dos cores. Que
 * ym2612 responda es la prueba de que el fixture genera audio FM; si respondiera
 * uno solo, la diferencia es el defecto y no un fixture mudo. Por eso no alcanza
 * con probar ym3438 aislado -- un "no cambio nada" seria indistinguible de
 * "aca no habia FM que silenciar".
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
static void close_library(library_t l) { FreeLibrary(l); }
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *load_symbol(library_t l, const char *n) { return dlsym(l, n); }
static void close_library(library_t l) { dlclose(l); }
#endif

#define ROM_SIZE AYTHER_GENERATED_ROM_SIZE
#define FRAMES   60

static const char *g_fm_core;            /* valor de genesis_plus_gx_ym2612 */
static struct retro_game_info_ext gi_ext;
static uint64_t g_audio_hash;
static uint64_t g_audio_energy;

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
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *)data;
      if (!v || !v->key) return false;
      v->value = NULL;
      if (!strcmp(v->key, "genesis_plus_gx_ym2612")) v->value = g_fm_core;
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  for (i = 0; i < frames * 2; i++) {
    g_audio_hash ^= (uint64_t)(uint16_t)d[i];
    g_audio_hash *= UINT64_C(0x100000001B3);
    g_audio_energy += (uint64_t)(d[i] < 0 ? -d[i] : d[i]);
  }
  return frames;
}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

/* Una corrida completa: carga el DLL, opcionalmente mutea FM, y devuelve
   hash y energia del audio. DLL nuevo por corrida: sin estado que se filtre. */
static int run_once(const char *dll, const char *fm_core, uint32_t mute_mask,
                    uint64_t *out_hash, uint64_t *out_energy, uint32_t *out_writes)
{
  library_t lib = open_library(dll);
  if (!lib) { fprintf(stderr, "no carga %s\n", dll); return 0; }
  g_fm_core = fm_core;
  g_audio_hash = UINT64_C(0xCBF29CE484222325);
  g_audio_energy = 0;

  void (*p_set_env)(retro_environment_t)          = load_symbol(lib, "retro_set_environment");
  void (*p_set_vid)(retro_video_refresh_t)        = load_symbol(lib, "retro_set_video_refresh");
  void (*p_set_aud)(retro_audio_sample_batch_t)   = load_symbol(lib, "retro_set_audio_sample_batch");
  void (*p_set_poll)(retro_input_poll_t)          = load_symbol(lib, "retro_set_input_poll");
  void (*p_set_inp)(retro_input_state_t)          = load_symbol(lib, "retro_set_input_state");
  void (*p_init)(void)                            = load_symbol(lib, "retro_init");
  bool (*p_load)(const struct retro_game_info *)  = load_symbol(lib, "retro_load_game");
  void (*p_run)(void)                             = load_symbol(lib, "retro_run");
  ayther_get_interface_fn p_iface                 = (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
  if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 0;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom_fm(rom, ROM_SIZE)) { close_library(lib); return 0; }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 0; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 0; }
  api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS | AYTHER_SUB_AUDIO_WRITES);
  p_run();  /* la suscripcion se activa al inicio del frame siguiente */

  if (mute_mask) {
    uint64_t gen = 0;
    int32_t rc = api->write_control(AYTHER_REGION_AUDIO_MUTE, 0, &mute_mask,
                                    sizeof(mute_mask), AYTHER_GENERATION_ANY, &gen);
    if (rc != AYTHER_STATUS_OK) {
      fprintf(stderr, "write_control(AUDIO_MUTE) -> %d\n", (int)rc);
      close_library(lib); return 0;
    }
  }

  int f;
  for (f = 0; f < FRAMES; f++) p_run();

  /* #29: la region tiene que traer eventos con extensions=1, con o sin probe.
     Se lee ANTES de cerrar el DLL y despues del ultimo frame. */
  if (out_writes)
  {
    uint32_t n = 0; uint64_t gen = 0;
    if (api->read_region(AYTHER_REGION_AUDIO_WRITE_COUNT, 0, &n, sizeof(n),
                         AYTHER_GENERATION_ANY, &gen) != AYTHER_STATUS_OK)
      n = 0;
    *out_writes = n;
  }

  *out_hash = g_audio_hash; *out_energy = g_audio_energy;
  close_library(lib);
  return 1;
}

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core.dll>\n", argv[0]); return 2; }
  const char *dll = argv[1];
  const char *cores[2] = { "mame (ym2612)", "nuked (ym3438)" };
  int fail = 0, i;

  for (i = 0; i < 2; i++) {
    uint64_t h_open = 0, e_open = 0, h_mute = 0, e_mute = 0;
    uint32_t w_open = 0;
    if (!run_once(dll, cores[i], 0u, &h_open, &e_open, &w_open)) { fail = 1; continue; }
    if (!run_once(dll, cores[i], 0x3Fu, &h_mute, &e_mute, NULL)) { fail = 1; continue; }

    int changed  = (h_open != h_mute);
    int quieter  = (e_mute < e_open);
    printf("%-16s  energia sin mute=%llu  con FM muteado=%llu  -> %s\n",
           cores[i], (unsigned long long)e_open, (unsigned long long)e_mute,
           changed ? (quieter ? "MUTE APLICADO" : "cambia pero NO baja")
                   : "NO-OP (el mute no llega al chip)");
    if (!changed || !quieter) fail = 1;

    /* #29: la capability AUDIO_WRITES se anuncia con extensions=1, asi que la
       region TIENE que producir. Que venga vacia con la capability anunciada es
       peor que no anunciarla: el consumidor no puede distinguir "no hubo
       escrituras" de "esta build no las produce". */
    printf("%-16s  escrituras en AUDIO_WRITES: %u -> %s\n",
           cores[i], (unsigned)w_open, w_open > 0 ? "OK" : "VACIA (sin productor)");
    if (w_open == 0) fail = 1;
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
