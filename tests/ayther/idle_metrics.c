/* #36: "cero trabajo sin subscribers", pero CONTADO.
 *
 * Hasta acá esa promesa —la central del fork— descansaba en dos cosas frágiles:
 * leer el código, y un bench cuyo ruido medido (±3%) es del mismo orden que lo
 * que se quiere demostrar. Un cronómetro no puede distinguir "no hace nada" de
 * "hace poco". Un contador en cero sí, y es falsable: si alguien vuelve a
 * colgar trabajo del path de idle, este test se pone rojo con un número.
 *
 * Lo que se afirma, con extensions compilado y CERO suscripciones, durante 120
 * frames de emulación real:
 *
 *   vram_dirty_marks == 0   el bitmap de patterns sucios no se toca
 *   satb_slow_path   == 0   el parser de sprites toma siempre el clon rápido
 *
 * Y después, para que el cero no sea el cero trivial de un fixture que no hace
 * nada: se suscribe, se corren otros 120 frames, y los mismos contadores tienen
 * que ser DISTINTOS de cero. Sin esa segunda mitad, un core que no compilara
 * las extensiones pasaría el test igual.
 *
 * Necesita un core con -DAYTHER_METRICS. Si el símbolo no está, el test se
 * saltea en vez de fallar: es un build de diagnóstico, no uno publicado.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "ayther_api.h"
#include "ayther_metrics.h"
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
#define FRAMES   120

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
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d, size_t f){(void)d;return f;}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

typedef int32_t (*metrics_read_fn)(ayther_metrics_v1 *, uint32_t);
typedef void (*metrics_reset_fn)(void);

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }

  library_t lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  void (*p_set_env)(retro_environment_t)        = load_symbol(lib, "retro_set_environment");
  void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(lib, "retro_set_video_refresh");
  void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(lib, "retro_set_audio_sample_batch");
  void (*p_set_poll)(retro_input_poll_t)        = load_symbol(lib, "retro_set_input_poll");
  void (*p_set_inp)(retro_input_state_t)        = load_symbol(lib, "retro_set_input_state");
  void (*p_init)(void)                          = load_symbol(lib, "retro_init");
  bool (*p_load)(const struct retro_game_info *)= load_symbol(lib, "retro_load_game");
  void (*p_run)(void)                           = load_symbol(lib, "retro_run");
  ayther_get_interface_fn p_iface = (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
  metrics_read_fn  p_metrics_read  = (metrics_read_fn)load_symbol(lib, "ayther_metrics_read");
  metrics_reset_fn p_metrics_reset = (metrics_reset_fn)load_symbol(lib, "ayther_metrics_reset");

  if (!p_metrics_read || !p_metrics_reset) {
    printf("idle-metrics: SALTEADO (core sin -DAYTHER_METRICS)\n");
    close_library(lib);
    return 0;
  }
  if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom(rom, ROM_SIZE)) { close_library(lib); return 2; }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }

  int fail = 0, f;
  ayther_metrics_v1 idle, busy;

  /* --- idle: compilado con extensions, sin una sola suscripcion ---------- */
  api->set_subscriptions(0);
  p_run();                       /* la suscripcion se aplica al frame siguiente */
  p_metrics_reset();
  for (f = 0; f < FRAMES; f++) p_run();
  memset(&idle, 0, sizeof(idle));
  if (p_metrics_read(&idle, sizeof(idle)) != AYTHER_STATUS_OK) {
    fprintf(stderr, "metrics_read fallo\n"); close_library(lib); return 2;
  }

  printf("idle    (%d frames, 0 suscripciones): vram_dirty_marks=%llu satb_slow_path=%llu\n",
         FRAMES, (unsigned long long)idle.vram_dirty_marks,
         (unsigned long long)idle.satb_slow_path);
  if (idle.vram_dirty_marks != 0) {
    printf("  FALLA: el bitmap de VRAM se marca sin subscribers\n"); fail = 1;
  }
  if (idle.satb_slow_path != 0) {
    printf("  FALLA: parse_satb toma el parser completo sin subscribers\n"); fail = 1;
  }
  if (idle.begin_frame_calls != (uint64_t)FRAMES) {
    printf("  FALLA: %llu frames contados, %d corridos -- el fixture no emulo\n",
           (unsigned long long)idle.begin_frame_calls, FRAMES); fail = 1;
  }

  /* --- activo: los mismos contadores tienen que MOVERSE ------------------ */
  /* Sin esta mitad, un core sin extensiones compiladas pasaria el test. */
  api->set_subscriptions(AYTHER_SUB_VDP_MEMORY | AYTHER_SUB_SPRITE_CAPTURE);
  p_run();
  p_metrics_reset();
  for (f = 0; f < FRAMES; f++) p_run();
  memset(&busy, 0, sizeof(busy));
  p_metrics_read(&busy, sizeof(busy));

  printf("activo  (%d frames, VDP_MEMORY+SPRITE_CAPTURE): vram_dirty_marks=%llu satb_slow_path=%llu\n",
         FRAMES, (unsigned long long)busy.vram_dirty_marks,
         (unsigned long long)busy.satb_slow_path);
  if (busy.vram_dirty_marks == 0) {
    printf("  FALLA: suscrito y el bitmap sigue sin marcarse -- el cero de arriba no prueba nada\n");
    fail = 1;
  }
  if (busy.satb_slow_path == 0) {
    printf("  FALLA: suscrito a SPRITE_CAPTURE y el parser completo no corre nunca\n");
    fail = 1;
  }

  /* --- suscrito a RENDER_CONTROLS, pero sin nada suprimido -------------- */
  /* Este es el caso que #36 arregla: la suscripcion sola bastaba para que
     parse_satb tomara el parser completo linea por linea. Se pagaba la
     CAPACIDAD de suprimir, no la supresion. Con la mascara en cero el parser
     rapido produce lo mismo, asi que el contador tiene que quedar en cero. */
  {
    ayther_metrics_v1 armed;
    uint8_t mask[16];
    uint64_t gen = 0;

    api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS);
    p_run();
    p_metrics_reset();
    for (f = 0; f < FRAMES; f++) p_run();
    memset(&armed, 0, sizeof(armed));
    p_metrics_read(&armed, sizeof(armed));
    printf("armado  (%d frames, RENDER_CONTROLS, mascara en cero): satb_slow_path=%llu\n",
           FRAMES, (unsigned long long)armed.satb_slow_path);
    if (armed.satb_slow_path != 0) {
      printf("  FALLA: se paga el parser completo sin un solo slot suprimido\n");
      fail = 1;
    }

    /* Y con un slot suprimido de verdad, el parser completo TIENE que correr:
       si no, la supresion seria un no-op silencioso. */
    memset(mask, 0, sizeof(mask));
    mask[0] = 0x08;   /* slot 3 */
    if (api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask, sizeof(mask),
                           AYTHER_GENERATION_ANY, &gen) != AYTHER_STATUS_OK) {
      printf("  FALLA: no se pudo escribir SPRITE_SUPPRESS\n");
      fail = 1;
    } else {
      p_run();
      p_metrics_reset();
      for (f = 0; f < FRAMES; f++) p_run();
      memset(&armed, 0, sizeof(armed));
      p_metrics_read(&armed, sizeof(armed));
      printf("activo  (%d frames, RENDER_CONTROLS, slot 3 suprimido): satb_slow_path=%llu\n",
             FRAMES, (unsigned long long)armed.satb_slow_path);
      if (armed.satb_slow_path == 0) {
        printf("  FALLA: hay un slot suprimido y el parser completo no corre\n");
        fail = 1;
      }
    }
  }

  close_library(lib);
  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
