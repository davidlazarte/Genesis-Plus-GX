/* #37.4: la supresion de tiles por PLANO, plano por plano.
 *
 * La region 0x105 esconde el grafico de un tile dondequiera que aparezca en su
 * plano. Hasta ahora no habia un solo test que la ejercitara: los unicos que la
 * tocaban -- full_core_replay-- la usan como region que se escribe para probar
 * que escribirla no rompe el determinismo, no para verificar que ESCONDE algo.
 *
 * Eso importa mas de lo normal acá porque el gate cambió. `ayther_plane_suppress_active`
 * era UN flag para los TRES planos: ocultar un tile de A hacia que B y Window
 * perdieran el fast path de DRAW_COLUMN y consultaran, por cada columna de cada
 * linea, una mascara vacia. Ahora cada plano decide por separado
 * (`ayther_psup_any[]`), y el riesgo del cambio es exactamente el contrario del
 * que arregla: que un plano quede marcado como vacio cuando no lo esta y su
 * supresion no se aplique nunca.
 *
 * El fixture S/H sirve de oraculo sin reimplementar el renderer: los planos A y
 * B tienen el MISMO contenido -- pattern 1, paleta 0, en las 256 celdas de las
 * primeras cuatro filas-- y A tapa a B. Entonces:
 *
 *   sin supresion   -> la capa de cada pixel de fondo es PLANO A
 *   se oculta en A  -> A queda transparente y aparece B: la capa es PLANO B,
 *                      y la IMAGEN no cambia (B dibuja lo mismo que dibujaba A)
 *   se oculta en A y B -> no queda nada: backdrop
 *
 * La segunda es la que prueba el gate nuevo: A no esta vacio, B si, y B tiene
 * que seguir dibujando bien por el camino rapido.
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

#define ROM_SIZE   AYTHER_GENERATED_ROM_SIZE
#define FRAMES     8
#define BG_ROWS_PX 32     /* el ROM llena cuatro filas de celdas */

/* Clave de la mascara: (pattern << 2) | paleta. El fondo del fixture es
   pattern 1, paleta 0 -> clave 4 -> byte 0, bit 4. */
#define SUP_PATTERN 1u
#define SUP_PALETTE 0u
#define SUP_KEY     (((SUP_PATTERN) << 2) | (SUP_PALETTE))
#define SUP_BYTE    ((SUP_KEY) >> 3)
#define SUP_BIT     (1u << ((SUP_KEY) & 7u))
#define PLANE_BYTES 1024u

static struct retro_game_info_ext gi_ext;
static uint8_t attrib[320 * 240];
static uint32_t attrib_w, attrib_h;
static uint16_t frame_px[320 * 240];
static uint32_t frame_w, frame_h;

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

static void vid_cb(const void *d, unsigned w, unsigned h, size_t pitch)
{
  unsigned y;
  if (!d || w > 320 || h > 240) return;
  frame_w = w; frame_h = h;
  for (y = 0; y < h; ++y)
    memcpy(&frame_px[(size_t)y * w], (const uint8_t *)d + (size_t)y * pitch,
           (size_t)w * sizeof(uint16_t));
}
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* Pixeles de fondo (sin sprite encima) de las filas con celdas escritas, por capa. */
static int count_layer(int layer)
{
  int x, y, n = 0;
  for (y = 0; y < BG_ROWS_PX && (uint32_t)y < attrib_h; ++y)
    for (x = 0; x < (int)attrib_w; ++x) {
      uint8_t a = attrib[(size_t)y * attrib_w + x];
      if (a & AYTHER_ATTRIB_SPRITE) continue;
      if (((a & AYTHER_ATTRIB_LAYER_MASK) >> AYTHER_ATTRIB_LAYER_SHIFT) ==
          (unsigned)layer)
        ++n;
    }
  return n;
}

static int count_bg_px(void)
{
  int x, y, n = 0;
  for (y = 0; y < BG_ROWS_PX && (uint32_t)y < attrib_h; ++y)
    for (x = 0; x < (int)attrib_w; ++x)
      if (!(attrib[(size_t)y * attrib_w + x] & AYTHER_ATTRIB_SPRITE)) ++n;
  return n;
}

int main(int argc, char **argv)
{
  const ayther_interface_v1 *api;
  ayther_region_info_v1 info;
  library_t lib;
  static uint8_t rom[ROM_SIZE];
  static uint8_t mask[3 * PLANE_BYTES];
  static uint16_t frame_plain[320 * 240];
  struct retro_game_info gi;
  int f, fail = 0, total;
  uint32_t plain_w, plain_h;

  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }
  lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  {
    void (*p_set_env)(retro_environment_t)        = load_symbol(lib, "retro_set_environment");
    void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(lib, "retro_set_video_refresh");
    void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(lib, "retro_set_audio_sample_batch");
    void (*p_set_poll)(retro_input_poll_t)        = load_symbol(lib, "retro_set_input_poll");
    void (*p_set_inp)(retro_input_state_t)        = load_symbol(lib, "retro_set_input_state");
    void (*p_init)(void)                          = load_symbol(lib, "retro_init");
    bool (*p_load)(const struct retro_game_info *)= load_symbol(lib, "retro_load_game");
    void (*p_run)(void)                           = load_symbol(lib, "retro_run");
    ayther_get_interface_fn p_iface =
      (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
    if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
      fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
    }

    p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
    p_set_poll(poll_cb); p_set_inp(input_cb);
    p_init();

    if (!ayther_build_generated_rom_sh(rom, ROM_SIZE)) {
      fprintf(stderr, "no se pudo construir el ROM\n"); close_library(lib); return 2;
    }
    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = "ayther-sh-v1.md"; gi_ext.dir = "."; gi_ext.name = "ayther-sh-v1";
    gi_ext.ext = "md"; gi_ext.data = rom; gi_ext.size = ROM_SIZE;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = "ayther-sh-v1.md"; gi.data = rom; gi.size = ROM_SIZE;
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

    api = p_iface(0);
    if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }

    /* Se necesitan las dos: CONTROLS para que la supresion se aplique,
       ATTRIBUTION para poder afirmar QUE plano quedo pintando. */
    api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS | AYTHER_SUB_ATTRIBUTION);

    memset(&info, 0, sizeof(info));
    if (api->query_region(AYTHER_REGION_PLANE_TILE_SUPPRESS, &info, sizeof(info))
          != AYTHER_STATUS_OK ||
        api->query_region(AYTHER_REGION_ATTRIBUTION, &info, sizeof(info))
          != AYTHER_STATUS_OK) {
      printf("plane-suppress: SALTEADO (core sin las capabilities)\n");
      close_library(lib); return 0;
    }

#define RUN_AND_READ()                                                        \
    do {                                                                      \
      for (f = 0; f < FRAMES; f++) p_run();                                   \
      memset(&info, 0, sizeof(info));                                         \
      if (api->query_region(AYTHER_REGION_ATTRIBUTION, &info, sizeof(info))   \
            != AYTHER_STATUS_OK || !info.byte_size ||                         \
          info.byte_size > sizeof(attrib)) {                                  \
        fprintf(stderr, "atribucion no disponible\n");                        \
        close_library(lib); return 2;                                         \
      }                                                                       \
      if (api->read_region(AYTHER_REGION_ATTRIBUTION, 0, attrib,              \
                           info.byte_size, AYTHER_GENERATION_ANY, NULL)       \
            != AYTHER_STATUS_OK) {                                            \
        fprintf(stderr, "atribucion ilegible\n");                             \
        close_library(lib); return 2;                                         \
      }                                                                       \
      attrib_w = 320; attrib_h = info.byte_size / attrib_w;                   \
    } while (0)

    /* --- 1. sin supresion: el fondo es plano A -------------------------- */
    RUN_AND_READ();
    total = count_bg_px();
    memcpy(frame_plain, frame_px, sizeof(frame_plain));
    plain_w = frame_w; plain_h = frame_h;
    {
      int a = count_layer(AYTHER_ATTRIB_LAYER_PLANE_A);
      printf("sin supresion:      plano A = %d de %d pixeles de fondo\n", a, total);
      if (a != total) {
        printf("  FALLA: A tapa a B en toda la zona con celdas escritas\n");
        fail = 1;
      }
    }

    /* --- 2. oculto el tile SOLO en el plano A --------------------------- */
    memset(mask, 0, sizeof(mask));
    mask[0 * PLANE_BYTES + SUP_BYTE] = (uint8_t)SUP_BIT;
    if (api->write_control(AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, mask,
                           sizeof(mask), AYTHER_GENERATION_ANY, NULL)
          != AYTHER_STATUS_OK) {
      fprintf(stderr, "no se pudo escribir la mascara\n");
      close_library(lib); return 2;
    }
    RUN_AND_READ();
    {
      int a = count_layer(AYTHER_ATTRIB_LAYER_PLANE_A);
      int b = count_layer(AYTHER_ATTRIB_LAYER_PLANE_B);
      printf("oculto en A:        plano A = %d, plano B = %d de %d\n", a, b, total);
      if (a != 0) {
        printf("  FALLA: el tile de A esta oculto, A no puede seguir pintando\n");
        fail = 1;
      }
      if (b != total) {
        printf("  FALLA: al desaparecer A tiene que verse B\n");
        fail = 1;
      }
      /* El gate por plano: B quedo marcado como vacio y toma el fast path.
         Como su contenido es identico al de A, la imagen no puede cambiar. */
      if (frame_w != plain_w || frame_h != plain_h ||
          memcmp(frame_px, frame_plain,
                 (size_t)frame_w * frame_h * sizeof(uint16_t)) != 0) {
        printf("  FALLA: A y B dibujan lo mismo -- la imagen tendria que ser "
               "identica; el fast path del plano vacio la cambio\n");
        fail = 1;
      } else {
        printf("  imagen identica a la de A (el fast path de B dibuja igual)  OK\n");
      }
    }

    /* --- 3. oculto el tile en A y en B: no queda nada -------------------- */
    mask[1 * PLANE_BYTES + SUP_BYTE] = (uint8_t)SUP_BIT;
    if (api->write_control(AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, mask,
                           sizeof(mask), AYTHER_GENERATION_ANY, NULL)
          != AYTHER_STATUS_OK) {
      fprintf(stderr, "no se pudo escribir la mascara\n");
      close_library(lib); return 2;
    }
    RUN_AND_READ();
    {
      int bd = count_layer(AYTHER_ATTRIB_LAYER_BACKDROP);
      printf("oculto en A y B:    backdrop = %d de %d\n", bd, total);
      if (bd != total) {
        printf("  FALLA: sin A ni B no queda mas que el backdrop\n");
        fail = 1;
      }
    }

    /* --- 4. borrar la mascara devuelve todo a su lugar ------------------- */
    memset(mask, 0, sizeof(mask));
    if (api->write_control(AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, mask,
                           sizeof(mask), AYTHER_GENERATION_ANY, NULL)
          != AYTHER_STATUS_OK) {
      fprintf(stderr, "no se pudo borrar la mascara\n");
      close_library(lib); return 2;
    }
    RUN_AND_READ();
    {
      int a = count_layer(AYTHER_ATTRIB_LAYER_PLANE_A);
      printf("mascara borrada:    plano A = %d de %d\n", a, total);
      if (a != total) {
        printf("  FALLA: borrar la mascara tiene que devolver el frame original\n");
        fail = 1;
      }
      if (frame_w != plain_w || frame_h != plain_h ||
          memcmp(frame_px, frame_plain,
                 (size_t)frame_w * frame_h * sizeof(uint16_t)) != 0) {
        printf("  FALLA: la imagen no volvio a ser la de antes de suprimir\n");
        fail = 1;
      }
    }
  }

  printf("plane-suppress: %s\n", fail ? "FALLA" : "OK");
  close_library(lib);
  return fail;
}
