/* #39.C: por que cada sprite se dibujo o no.
 *
 * `PARSED_SPRITES` dice que sprites vio el parser. No dice que les paso
 * despues, y "aparece en la lista" no es lo mismo que "se dibujo": el VDP
 * todavia puede descartarlo por el limite de sprites de la linea, por el
 * presupuesto de pixeles, o taparlo con la mascara de x=0.
 *
 * Un frontend que quiere saber por que su sprite no aparece tenia que deducir
 * esas reglas por su cuenta -- contar sprites por linea, reimplementar el orden
 * de la cadena de la SAT-, con su copia de las reglas y sin nadie que avise
 * cuando se separan de las del core.
 *
 * El fixture es el oraculo: 24 sprites en la MISMA linea, cuatro mas que los 20
 * que el VDP dibuja en H40, y el slot 12 en x=0. Con eso las dos afirmaciones
 * del criterio de aceptacion son exactas y no aproximadas:
 *
 *   - los cuatro ultimos de la cadena llevan DROP_LINE;
 *   - los que vienen DESPUES del de x=0 llevan MASKED_X0, y los anteriores no.
 *
 * Y una tercera que no esta en el issue pero es la que mas se usa: suprimir un
 * slot con la mascara 0x103 tiene que dar SUPPRESSED y no un descarte del
 * hardware. "No se dibujo porque vos lo pediste" y "no se dibujo porque el VDP
 * no daba" son respuestas distintas.
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

static struct retro_game_info_ext gi_ext;

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
static void vid_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

static const char *bits_str(uint8_t b, char *buf)
{
  buf[0] = 0;
  if (b & AYTHER_SPR_OUT_PARSED)     strcat(buf, "parsed ");
  if (b & AYTHER_SPR_OUT_DRAWN)      strcat(buf, "drawn ");
  if (b & AYTHER_SPR_OUT_DROP_LINE)  strcat(buf, "drop-line ");
  if (b & AYTHER_SPR_OUT_DROP_PIXEL) strcat(buf, "drop-pixel ");
  if (b & AYTHER_SPR_OUT_MASKED_X0)  strcat(buf, "masked-x0 ");
  if (b & AYTHER_SPR_OUT_SUPPRESSED) strcat(buf, "suppressed ");
  if (!b) strcat(buf, "-");
  return buf;
}

int main(int argc, char **argv)
{
  static uint8_t rom[ROM_SIZE];
  static uint8_t outcome[AYTHER_SPRITE_SAT_SLOTS];
  static uint8_t mask[16];
  const ayther_interface_v1 *api;
  ayther_region_info_v1 info;
  struct retro_game_info gi;
  library_t lib;
  int f, fail = 0;
  unsigned i;

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

    if (!ayther_build_generated_rom_sprites(rom, ROM_SIZE)) {
      fprintf(stderr, "no se pudo construir el ROM\n"); close_library(lib); return 2;
    }
    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = "ayther-spr-v1.md"; gi_ext.dir = "."; gi_ext.name = "ayther-spr-v1";
    gi_ext.ext = "md"; gi_ext.data = rom; gi_ext.size = ROM_SIZE;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = "ayther-spr-v1.md"; gi.data = rom; gi.size = ROM_SIZE;
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

    api = p_iface(0);
    if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }
    if (!(api->capabilities & AYTHER_CAP_SPRITE_OUTCOME_V1)) {
      printf("sprite-outcome: SALTEADO (el core no declara la capability)\n");
      close_library(lib); return 0;
    }

    /* --- sin suscripcion no hay respuesta ------------------------------- */
    api->set_subscriptions(0);
    for (f = 0; f < 4; f++) p_run();
    if (api->read_region(AYTHER_REGION_SPRITE_OUTCOME, 0, outcome, sizeof(outcome),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_NOT_SUBSCRIBED) {
      printf("  FALLA: la region contesta sin suscripcion\n");
      fail = 1;
    }

    api->set_subscriptions(AYTHER_SUB_SPRITE_CAPTURE | AYTHER_SUB_RENDER_CONTROLS);
    for (f = 0; f < FRAMES; f++) p_run();

    memset(&info, 0, sizeof(info));
    if (api->query_region(AYTHER_REGION_SPRITE_OUTCOME, &info, sizeof(info))
          != AYTHER_STATUS_OK || info.byte_size != sizeof(outcome)) {
      printf("  FALLA: la region mide %u y se esperaban %u\n",
             (unsigned)info.byte_size, (unsigned)sizeof(outcome));
      close_library(lib); return 1;
    }
    if (api->read_region(AYTHER_REGION_SPRITE_OUTCOME, 0, outcome, sizeof(outcome),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
      printf("  FALLA: la region no se puede leer\n");
      close_library(lib); return 1;
    }

    {
      char buf[96];
      printf("slot  resultado\n");
      for (i = 0; i < AYTHER_SPR_FIXTURE_COUNT; ++i)
        printf("%4u  %s\n", i, bits_str(outcome[i], buf));
    }

    /* --- 1. el limite por linea: los que sobran no entran ---------------- */
    {
      unsigned dropped = 0, drawn = 0;
      for (i = 0; i < AYTHER_SPR_FIXTURE_COUNT; ++i) {
        if (outcome[i] & AYTHER_SPR_OUT_DROP_LINE) ++dropped;
        if (outcome[i] & AYTHER_SPR_OUT_DRAWN) ++drawn;
      }
      printf("\nlimite por linea: %u descartados, %u dibujados de %u\n",
             dropped, drawn, (unsigned)AYTHER_SPR_FIXTURE_COUNT);
      if (dropped != AYTHER_SPR_FIXTURE_COUNT - AYTHER_SPR_FIXTURE_LIMIT) {
        printf("  FALLA: en H40 entran %u; los otros %u tienen que decir por que\n",
               (unsigned)AYTHER_SPR_FIXTURE_LIMIT,
               (unsigned)(AYTHER_SPR_FIXTURE_COUNT - AYTHER_SPR_FIXTURE_LIMIT));
        fail = 1;
      }
      /* Y son los ULTIMOS de la cadena, no cuatro cualesquiera. */
      for (i = AYTHER_SPR_FIXTURE_LIMIT; i < AYTHER_SPR_FIXTURE_COUNT; ++i)
        if (!(outcome[i] & AYTHER_SPR_OUT_DROP_LINE)) {
          printf("  FALLA: el slot %u pasa del limite y no esta marcado\n", i);
          fail = 1;
        }
    }

    /* --- 2. la mascara de x=0 tapa a los de MENOR prioridad -------------- */
    {
      unsigned before = 0, after = 0;
      for (i = 0; i < AYTHER_SPR_FIXTURE_MASK_SLOT; ++i)
        if (outcome[i] & AYTHER_SPR_OUT_MASKED_X0) ++before;
      for (i = AYTHER_SPR_FIXTURE_MASK_SLOT + 1u; i < AYTHER_SPR_FIXTURE_LIMIT; ++i)
        if (outcome[i] & AYTHER_SPR_OUT_MASKED_X0) ++after;
      printf("mascara x=0 (slot %u): %u tapados antes, %u despues\n",
             (unsigned)AYTHER_SPR_FIXTURE_MASK_SLOT, before, after);
      if (before) {
        printf("  FALLA: la mascara tapa a los de MENOR prioridad, no a los "
               "anteriores\n");
        fail = 1;
      }
      if (!after) {
        printf("  FALLA: despues de un sprite en x=0 no puede dibujarse ninguno\n");
        fail = 1;
      }
    }

    /* --- 3. suprimido por el frontend != descartado por el VDP ----------- */
    {
      const unsigned slot = 3u;
      memset(mask, 0, sizeof(mask));
      mask[slot >> 3] = (uint8_t)(1u << (slot & 7u));
      if (api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask, sizeof(mask),
                             AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
        printf("  FALLA: no se pudo escribir la mascara de supresion\n");
        fail = 1;
      }
      for (f = 0; f < FRAMES; f++) p_run();
      if (api->read_region(AYTHER_REGION_SPRITE_OUTCOME, 0, outcome, sizeof(outcome),
                           AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OK)
      {
        char buf[96];
        printf("slot %u con la mascara puesta: %s\n", slot, bits_str(outcome[slot], buf));
        if (!(outcome[slot] & AYTHER_SPR_OUT_SUPPRESSED)) {
          printf("  FALLA: lo oculto el frontend y la region no lo dice\n");
          fail = 1;
        }
        if (outcome[slot] & AYTHER_SPR_OUT_DRAWN) {
          printf("  FALLA: esta suprimido y aparece como dibujado\n");
          fail = 1;
        }
      }
    }
  }

  printf("\nsprite-outcome: %s\n", fail ? "FALLA" : "OK");
  close_library(lib);
  return fail;
}
