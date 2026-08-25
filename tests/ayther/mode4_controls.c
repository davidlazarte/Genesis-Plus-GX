/* #40 fase 2: los controles del fork en Mode 4, contra un cartucho real.
 *
 * Es OPT-IN: necesita un ROM de Master System, y un ROM comercial no puede
 * estar en el repo. Sin `AYTHER_SMS_ROM` apuntando a uno, el test se saltea y
 * lo dice. En CI se saltea siempre.
 *
 * Por que existe igual. Hasta fase 1 estos controles contestaban
 * UNSUPPORTED_MODE en Mode 4 -- honesto, pero inutil: el frontend sabia que no
 * podia ocultar nada-. Fase 2 los implementa, y una implementacion sin nada que
 * la ejercite es como llegamos a que `raster_rom_probe` estuviera roto durante
 * meses. Mientras no exista un fixture SMS en el repo -- el generador emite
 * 68000 y un cartucho de Master System corre Z80-, esto es lo que hay, y es
 * mucho mejor que nada.
 *
 * Lo que afirma, todo con oraculo por diferencia contra el mismo frame sin
 * tocar nada:
 *
 *   1. El descriptor de sistema dice Mode 4 (si no, el test no prueba lo que
 *      cree probar).
 *   2. Apagar la capa de fondo CAMBIA el frame; apagar B o Window, que en Mode
 *      4 no existen, se RECHAZA en vez de aceptarse y no hacer nada.
 *   3. La captura de sprites devuelve sprites.
 *   4. Suprimir un slot que estaba dibujandose cambia el frame, y la region de
 *      resultado lo reporta como SUPPRESSED y no como dibujado.
 *   5. Recomponer da el MISMO frame que emitio el core, pixel por pixel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "ayther_api.h"

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

#define MAX_PX  (320 * 240)
#define FRAMES  240

static struct retro_game_info_ext gi_ext;
static uint16_t frame_px[MAX_PX];
static unsigned frame_w, frame_h;

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd) {
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
      if (data) *(int *)data = 3;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      if (data) *(const struct retro_game_info_ext **)data = &gi_ext;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data) *(const char **)data = ".";
      return data != NULL;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d, unsigned w, unsigned h, size_t pitch)
{
  unsigned y;
  if (!d || !w || !h || (size_t)w * h > MAX_PX) return;
  frame_w = w; frame_h = h;
  for (y = 0; y < h; ++y)
    memcpy(&frame_px[(size_t)y * w], (const uint8_t *)d + (size_t)y * pitch,
           (size_t)w * sizeof(uint16_t));
}
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

static uint64_t hash_frame(void)
{
  uint64_t h = UINT64_C(1469598103934665603);
  size_t i, n = (size_t)frame_w * frame_h;
  for (i = 0; i < n; ++i) {
    h ^= (uint64_t)(frame_px[i] & 0xFFu);        h *= UINT64_C(1099511628211);
    h ^= (uint64_t)((frame_px[i] >> 8) & 0xFFu); h *= UINT64_C(1099511628211);
  }
  return h;
}

int main(int argc, char **argv)
{
  const char *rom_path = getenv("AYTHER_SMS_ROM");
  static uint8_t rom[1u << 20];
  static uint16_t recomposed[MAX_PX];
  static uint8_t outcome[AYTHER_SPRITE_SAT_SLOTS];
  static uint8_t mask[16];
  const ayther_interface_v1 *api;
  ayther_system_v1 sys;
  struct retro_game_info gi;
  library_t lib;
  FILE *f;
  size_t rom_size;
  uint64_t base_hash;
  int i, fail = 0;

  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }
  if (!rom_path || !*rom_path) {
    printf("mode4-controls: SALTEADO (defini AYTHER_SMS_ROM=<ruta a un .sms>)\n");
    return 0;
  }
  f = fopen(rom_path, "rb");
  if (!f) {
    printf("mode4-controls: SALTEADO (no se puede abrir %s)\n", rom_path);
    return 0;
  }
  rom_size = fread(rom, 1, sizeof(rom), f);
  fclose(f);
  if (!rom_size) { printf("mode4-controls: SALTEADO (ROM vacio)\n"); return 0; }

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

    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = rom_path; gi_ext.dir = "."; gi_ext.name = "sms";
    gi_ext.ext = "sms"; gi_ext.data = rom; gi_ext.size = rom_size;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = rom_path; gi.data = rom; gi.size = rom_size;
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

    api = p_iface(0);
    if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }

    api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS | AYTHER_SUB_SPRITE_CAPTURE |
                           AYTHER_SUB_RECOMPOSITION | AYTHER_SUB_VDP_MEMORY);
    for (i = 0; i < FRAMES; i++) p_run();
    base_hash = hash_frame();

    /* --- 1. esto es de verdad Mode 4 ----------------------------------- */
    memset(&sys, 0, sizeof(sys));
    if (api->read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK ||
        sys.vdp_mode != 4) {
      printf("  FALLA: el descriptor dice modo %u; este test necesita Mode 4\n",
             sys.vdp_mode);
      close_library(lib); return 1;
    }
    printf("sistema: hw=%02x modo=%u viewport=%ux%u\n",
           sys.system_hw, sys.vdp_mode, sys.viewport_w, sys.viewport_h);

    /* --- 2. la capa de fondo se puede apagar; B y Window no existen ----- */
    {
      uint8_t m = 0x0E;   /* fondo (A) apagado, sprites y el resto encendidos */
      uint64_t hidden;
      if (api->write_control(AYTHER_REGION_LAYER_MASK, 0, &m, 1,
                             AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
        printf("  FALLA: apagar el fondo en Mode 4 tendria que aceptarse\n");
        fail = 1;
      }
      for (i = 0; i < 4; i++) p_run();
      hidden = hash_frame();
      printf("capa de fondo apagada: hash %016llx (antes %016llx)\n",
             (unsigned long long)hidden, (unsigned long long)base_hash);
      if (hidden == base_hash) {
        printf("  FALLA: apagar el fondo no cambio un solo pixel\n");
        fail = 1;
      }
      m = 0x0F;
      api->write_control(AYTHER_REGION_LAYER_MASK, 0, &m, 1,
                         AYTHER_GENERATION_ANY, NULL);
      /* Apagar el plano B, que en Mode 4 no existe, tiene que RECHAZARSE. */
      m = 0x0D;
      if (api->write_control(AYTHER_REGION_LAYER_MASK, 0, &m, 1,
                             AYTHER_GENERATION_ANY, NULL) !=
          AYTHER_STATUS_UNSUPPORTED_MODE) {
        printf("  FALLA: el plano B no existe en Mode 4; apagarlo no puede "
               "contestar OK\n");
        fail = 1;
      }
      m = 0x0F;
      api->write_control(AYTHER_REGION_LAYER_MASK, 0, &m, 1,
                         AYTHER_GENERATION_ANY, NULL);
      for (i = 0; i < 4; i++) p_run();
    }

    /* --- 3. y 4. captura y supresion por slot --------------------------- */
    {
      uint8_t count = 0;
      int victim = -1;
      api->read_region(AYTHER_REGION_PARSED_SPRITE_COUNT, 0, &count, 1,
                       AYTHER_GENERATION_ANY, NULL);
      printf("sprites capturados: %u\n", count);
      if (!count) {
        printf("  FALLA: la escena tiene sprites y la captura no devolvio ninguno\n");
        fail = 1;
      }
      if (api->read_region(AYTHER_REGION_SPRITE_OUTCOME, 0, outcome,
                           sizeof(outcome), AYTHER_GENERATION_ANY, NULL)
            == AYTHER_STATUS_OK)
        for (i = 0; i < (int)sizeof(outcome); ++i)
          if (outcome[i] & AYTHER_SPR_OUT_DRAWN) { victim = i; break; }

      if (victim < 0) {
        printf("  FALLA: ningun slot aparece como dibujado\n");
        fail = 1;
      } else {
        uint64_t suppressed;
        printf("se suprime el slot %d, que estaba dibujandose\n", victim);
        memset(mask, 0, sizeof(mask));
        mask[victim >> 3] = (uint8_t)(1u << (victim & 7));
        if (api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask,
                               sizeof(mask), AYTHER_GENERATION_ANY, NULL)
              != AYTHER_STATUS_OK) {
          printf("  FALLA: suprimir un sprite en Mode 4 tendria que aceptarse\n");
          fail = 1;
        }
        for (i = 0; i < 8; i++) p_run();
        suppressed = hash_frame();
        if (suppressed == base_hash) {
          printf("  FALLA: el sprite se dibujaba y suprimirlo no cambio nada\n");
          fail = 1;
        }
        if (api->read_region(AYTHER_REGION_SPRITE_OUTCOME, 0, outcome,
                             sizeof(outcome), AYTHER_GENERATION_ANY, NULL)
              == AYTHER_STATUS_OK)
        {
          if (!(outcome[victim] & AYTHER_SPR_OUT_SUPPRESSED)) {
            printf("  FALLA: lo oculto el frontend y la region no lo dice\n");
            fail = 1;
          }
          if (outcome[victim] & AYTHER_SPR_OUT_DRAWN) {
            printf("  FALLA: esta suprimido y aparece como dibujado\n");
            fail = 1;
          }
        }
        memset(mask, 0, sizeof(mask));
        api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask, sizeof(mask),
                           AYTHER_GENERATION_ANY, NULL);
        for (i = 0; i < 8; i++) p_run();
      }
    }

    /* --- 5. recomponer da el mismo frame -------------------------------- */
    {
      uint32_t w = 0, h = 0;
      int32_t st = api->recompose_frame(recomposed, MAX_PX, 0, &w, &h);
      if (st != AYTHER_STATUS_OK) {
        printf("  FALLA: recomponer en Mode 4 devolvio %d\n", (int)st);
        fail = 1;
      } else if (w != frame_w || h != frame_h) {
        printf("  FALLA: recompone %ux%u y el core emite %ux%u\n",
               w, h, frame_w, frame_h);
        fail = 1;
      } else {
        size_t k, n = (size_t)w * h, diff = 0;
        for (k = 0; k < n; ++k)
          if (recomposed[k] != frame_px[k]) ++diff;
        printf("recomposicion %ux%u: %lu pixeles distintos de %lu\n",
               w, h, (unsigned long)diff, (unsigned long)n);
        if (diff) {
          printf("  FALLA: la recomposicion tiene que dar el mismo frame\n");
          fail = 1;
        }
      }
    }
  }

  printf("\nmode4-controls: %s\n", fail ? "FALLA" : "OK");
  close_library(lib);
  return fail;
}
