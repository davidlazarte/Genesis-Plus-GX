/* #41: la region ATTRIBUTION con una suscripcion que pide SOLO atribucion.
 *
 * El defecto: suscribirse a AYTHER_SUB_ATTRIBUTION y a nada mas devolvia una
 * region de CERO bytes, y `read_region` contestaba OK. El frontend pedia el
 * dato, el core decia que si, y no llegaba nada. Sin error de por medio.
 *
 * La causa eran dos gates escritos con el predicado equivocado. Que render_line
 * y render_bg tomen el clon OBSERVADO no es lo mismo que que los CONTROLES esten
 * activos: la atribucion no controla nada, solo mira, pero se captura adentro de
 * ese clon. Y el clon rapido de render_bg usa `merge_fast`, que no tiene el
 * hook, asi que ni siquiera las capas salian: el frame entero decia "backdrop".
 *
 * El fixture es una escena hecha a medida para poder afirmar cosas por
 * coordenada: fondo UNIFORME (el mismo pattern en todas las celdas de A y B)
 * sobre las primeras cuatro filas, y backdrop debajo. Con eso, la capa de cada
 * pixel es predecible sin reimplementar el renderer.
 *
 * La escena tambien contiene los dos sprites que hacen falta para el bit de
 * sprite EXACTO (#31/#37): uno indistinguible del fondo y uno operador de
 * shadow/highlight. Ese bit todavia sale de un diff contra el fondo, asi que el
 * test los REPORTA sin exigir nada: cuando el store en el bucle interno entre,
 * este es el lugar donde se convierten en asserts.
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
#define FRAMES   8

/* El ROM llena cuatro filas de celdas: 32 scanlines de fondo real. */
#define BG_ROWS_PX 32

static struct retro_game_info_ext gi_ext;
static uint8_t attrib[320 * 240];
static uint32_t attrib_w, attrib_h;

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

static int count_layer(int y0, int h, int layer)
{
  int x, y, n = 0;
  for (y = y0; y < y0 + h && (uint32_t)y < attrib_h; ++y)
    for (x = 0; x < (int)attrib_w; ++x)
      if (((attrib[(size_t)y * attrib_w + x] & AYTHER_ATTRIB_LAYER_MASK) >>
           AYTHER_ATTRIB_LAYER_SHIFT) == (unsigned)layer)
        ++n;
  return n;
}

static int count_sprite_px(int x0, int y0, int w, int h)
{
  int x, y, n = 0;
  for (y = y0; y < y0 + h && (uint32_t)y < attrib_h; ++y)
    for (x = x0; x < x0 + w && (uint32_t)x < attrib_w; ++x)
      if (attrib[(size_t)y * attrib_w + x] & AYTHER_ATTRIB_SPRITE) ++n;
  return n;
}

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
  if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom_sh(rom, ROM_SIZE)) {
    fprintf(stderr, "no se pudo construir el ROM\n"); close_library(lib); return 2;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-sh-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-sh-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-sh-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }

  ayther_region_info_v1 info;
  int f, fail = 0;

  /* SOLO atribucion: es exactamente el caso que estaba roto. */
  api->set_subscriptions(AYTHER_SUB_ATTRIBUTION);
  for (f = 0; f < FRAMES; f++) p_run();

  memset(&info, 0, sizeof(info));
  if (api->query_region(AYTHER_REGION_ATTRIBUTION, &info, sizeof(info)) !=
      AYTHER_STATUS_OK) {
    printf("attribution-scene: SALTEADO (core sin la capability)\n");
    close_library(lib); return 0;
  }

  printf("region con solo ATTRIBUTION suscrito: %u bytes\n",
         (unsigned)info.byte_size);
  if (!info.byte_size) {
    printf("  FALLA: la region viene vacia -- el gate mira CONTROLS y no OBSERVED\n");
    close_library(lib); return 1;
  }
  if (info.byte_size > sizeof(attrib)) {
    fprintf(stderr, "region mas grande que el buffer (%u)\n",
            (unsigned)info.byte_size);
    close_library(lib); return 2;
  }
  if (api->read_region(AYTHER_REGION_ATTRIBUTION, 0, attrib, info.byte_size,
                       AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    fprintf(stderr, "la region no se puede leer\n");
    close_library(lib); return 2;
  }
  attrib_w = 320;
  attrib_h = info.byte_size / attrib_w;
  close_library(lib);

  /* --- las capas son exactas sobre una escena de capa conocida ----------- */
  {
    int plane_a_top = count_layer(0, BG_ROWS_PX, AYTHER_ATTRIB_LAYER_PLANE_A);
    int backdrop_top = count_layer(0, BG_ROWS_PX, AYTHER_ATTRIB_LAYER_BACKDROP);
    int backdrop_bottom = count_layer(BG_ROWS_PX, (int)attrib_h - BG_ROWS_PX,
                                      AYTHER_ATTRIB_LAYER_BACKDROP);
    int expect_top = BG_ROWS_PX * (int)attrib_w;
    int expect_bottom = ((int)attrib_h - BG_ROWS_PX) * (int)attrib_w;

    printf("filas con fondo (y<%d): plano A=%d de %d, backdrop=%d\n",
           BG_ROWS_PX, plane_a_top, expect_top, backdrop_top);
    if (plane_a_top != expect_top) {
      printf("  FALLA: el fondo es uniforme, la capa tendria que serlo tambien\n");
      fail = 1;
    }
    printf("filas sin fondo (y>=%d): backdrop=%d de %d\n",
           BG_ROWS_PX, backdrop_bottom, expect_bottom);
    if (backdrop_bottom != expect_bottom) {
      printf("  FALLA: sin celdas escritas la capa tendria que ser backdrop\n");
      fail = 1;
    }
  }

  /* --- el bit de sprite: se reporta, todavia no se exige ----------------- */
  /* Los dos sprites de la escena son los casos que un diff contra el fondo
     contesta mal. Mientras el bit siga saliendo de ese diff, los numeros de
     abajo son el TAMANIO del defecto, no una regresion. Cuando el store en el
     bucle interno de render_obj entre (#31/#37/#41), estas dos lineas pasan a
     ser asserts: 64 y 0. */
  printf("\n[informativo] bit de sprite, todavia derivado del diff contra el fondo:\n");
  printf("  sprite identico al fondo (8x8 en %d,%d): %d de 64 marcados (exacto seria 64)\n",
         AYTHER_SH_SPRITE_X, AYTHER_SH_SPRITE_Y,
         count_sprite_px(AYTHER_SH_SPRITE_X, AYTHER_SH_SPRITE_Y, 8, 8));
  printf("  operador S/H            (8x8 en %d,%d): %d de 64 marcados (exacto seria 0)\n",
         AYTHER_SH_OPERATOR_X, AYTHER_SH_OPERATOR_Y,
         count_sprite_px(AYTHER_SH_OPERATOR_X, AYTHER_SH_OPERATOR_Y, 8, 8));

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
