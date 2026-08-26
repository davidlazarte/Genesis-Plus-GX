/* #40, ultimo criterio de aceptacion: scroll lock y cambio de paleta a mitad
 * de frame en Mode 4, con la recomposicion medida contra el frame emitido.
 *
 * Dos escenas del fixture SMS, en este orden:
 *
 *   A. solo SCROLL_LOCK: un efecto de REGISTRO, fijo durante el frame. La
 *      recomposicion desde el estado final tiene que dar el mismo frame, y el
 *      core tiene que decir que no hubo raster (0x10E en cero, journal vacio).
 *
 *   B. SCROLL_LOCK + PALETTE_SPLIT: la interrupcion de linea cambia la entrada
 *      1 de CRAM en SPLIT_LINE. Eso es un cambio de CRAM a mitad de pantalla,
 *      y en Mode 4 no hay replay (recompose_multilayer es de Mode 5), asi que
 *      la recomposicion NO PUEDE ser correcta. Lo que se afirma es que el core
 *      lo DIGA: bit CRAM en 0x10E, el evento en el journal en esa linea, y la
 *      diferencia con el frame confinada a un solo lado del split.
 *
 * Antes de este test el camino de escritura de la Master System no marcaba
 * CRAM ni VRAM: la escena B dibujaba bien, 0x10E quedaba en cero y un frontend
 * que se fia de la mascara usaba una recomposicion equivocada sin enterarse.
 * Es la clase de defecto que no da error, y por eso el test mira la mascara y
 * no solo los pixeles.
 *
 * Los oraculos son por COLOR y por COORDENADA, sin reimplementar el renderer:
 * el marcador de la fila 0 queda en x=64 (el lock no lo deja scrollear) y el
 * de la fila 4 aparece HSCROLL pixeles a la derecha; la columna x=200 es roja
 * arriba del split y amarilla abajo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "ayther_api.h"
#include "ayther_raster.h"   /* AYTHER_RASTER_REASON_*: los bits de 0x10E */
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

#define MAX_PX  (320 * 240)
#define FRAMES  120

/* RGB565 de los colores --BBGGRR del fixture (2 bits por canal, expandidos). */
#define PX_RED     0xF800u
#define PX_YELLOW  0xFFE0u
#define PX_WHITE   0xFFFFu

#define MARKER_X   (AYTHER_SMS_MARKER_COL * 8u)          /* 64 */
#define MARKER_Y   (AYTHER_SMS_MARKER_ROW * 8u)          /* 32 */
#define PROBE_X    200u   /* columna libre de sprites y marcadores */

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

static uint16_t px(unsigned x, unsigned y) { return frame_px[(size_t)y * frame_w + x]; }

/* Cuantos pixeles de la fila y, entre x0 y x0+8, son del color dado. */
static unsigned run_of(unsigned x0, unsigned y, uint16_t color)
{
  unsigned x, n = 0;
  for (x = x0; x < x0 + 8u && x < frame_w; ++x) if (px(x, y) == color) ++n;
  return n;
}

struct scene
{
  const char *name;
  unsigned int flags;
};

typedef struct
{
  void (*run)(void);
  bool (*load)(const struct retro_game_info *);
  void (*unload)(void);
  ayther_get_interface_fn iface;
} core_fns;

static int check_scene(const core_fns *c, const struct scene *sc,
                       uint8_t *rom, size_t rom_cap)
{
  static uint16_t recomposed[MAX_PX];
  static ayther_journal_v1 journal;
  const ayther_interface_v1 *api;
  ayther_system_v1 sys;
  struct retro_game_info gi;
  size_t rom_size;
  uint32_t reasons = 0;
  int split = (sc->flags & AYTHER_SMS_SCENE_PALETTE_SPLIT) != 0;
  int fail = 0, i;
  unsigned boundary = 0;

  printf("\n== escena %s ==\n", sc->name);

  rom_size = ayther_build_generated_rom_sms_scene(rom, rom_cap, sc->flags);
  if (!rom_size) { printf("  FALLA: no se pudo construir el fixture\n"); return 1; }

  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path = "ayther-sms-raster.sms"; gi_ext.dir = "."; gi_ext.name = "sms";
  gi_ext.ext = "sms"; gi_ext.data = rom; gi_ext.size = rom_size;
  gi_ext.persistent_data = true;
  memset(&gi, 0, sizeof(gi));
  gi.path = "ayther-sms-raster.sms"; gi.data = rom; gi.size = rom_size;
  if (!c->load(&gi)) { printf("  FALLA: load_game\n"); return 1; }

  api = c->iface(0);
  if (!api) { printf("  FALLA: sin ABI\n"); c->unload(); return 1; }
  api->set_subscriptions(AYTHER_SUB_RASTER_TRACKING | AYTHER_SUB_RECOMPOSITION |
                         AYTHER_SUB_VDP_MEMORY);
  for (i = 0; i < FRAMES; i++) c->run();

  /* --- 0. Mode 4, 256x192 ------------------------------------------------ */
  memset(&sys, 0, sizeof(sys));
  if (api->read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys),
                       AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK ||
      sys.vdp_mode != 4 || frame_w != 256 || frame_h != 192) {
    printf("  FALLA: modo %u, frame %ux%u; se esperaba Mode 4 en 256x192\n",
           sys.vdp_mode, frame_w, frame_h);
    c->unload(); return 1;
  }

  /* --- 1. scroll lock: la fila 0 no scrollea, la fila 4 si --------------- */
  {
    unsigned y0 = 3, y4 = MARKER_Y + 3;
    unsigned locked_here  = run_of(MARKER_X, y0, PX_WHITE);
    unsigned locked_moved = run_of(MARKER_X + AYTHER_SMS_HSCROLL, y0, PX_WHITE);
    unsigned free_here    = run_of(MARKER_X, y4, PX_WHITE);
    unsigned free_moved   = run_of(MARKER_X + AYTHER_SMS_HSCROLL, y4, PX_WHITE);
    int ok = locked_here == 8 && locked_moved == 0 &&
             free_here == 0 && free_moved == 8;
    printf("1. scroll lock: fila 0 tiene el marcador en x=%u (%u px) y no en x=%u (%u px);"
           " fila 4 lo tiene en x=%u (%u px) y no en x=%u (%u px) -> %s\n",
           MARKER_X, locked_here, MARKER_X + AYTHER_SMS_HSCROLL, locked_moved,
           MARKER_X + AYTHER_SMS_HSCROLL, free_moved, MARKER_X, free_here,
           ok ? "correcto" : "MAL");
    if (!ok) fail = 1;
  }

  /* --- 2. paleta: rojo arriba del split, amarillo abajo (o todo rojo) ---- */
  {
    unsigned y, red_above = 0, yellow_below = 0, other = 0;
    boundary = frame_h;
    for (y = 0; y < frame_h; ++y)
      if (px(PROBE_X, y) != PX_RED) { boundary = y; break; }
    for (y = 0; y < frame_h; ++y)
    {
      uint16_t p = px(PROBE_X, y);
      if (y < boundary) { if (p == PX_RED) ++red_above; else ++other; }
      else              { if (p == PX_YELLOW) ++yellow_below; else ++other; }
    }
    if (split)
    {
      int ok = other == 0 && boundary >= AYTHER_SMS_SPLIT_LINE - 2 &&
               boundary <= AYTHER_SMS_SPLIT_LINE + 2 && yellow_below > 0;
      printf("2. split de paleta en x=%u: rojo hasta la linea %u, amarillo desde ahi"
             " (%u rojos, %u amarillos, %u de otro color) -> %s\n",
             PROBE_X, boundary, red_above, yellow_below, other,
             ok ? "correcto" : "MAL");
      if (!ok) fail = 1;
    }
    else
    {
      int ok = boundary == frame_h;
      printf("2. sin split: la columna x=%u es roja entera -> %s\n", PROBE_X,
             ok ? "correcto" : "MAL");
      if (!ok) fail = 1;
    }
  }

  /* --- 3. lo que el core DICE del frame: 0x10E y el journal --------------- */
  if (api->read_region(AYTHER_REGION_RASTER_FALLBACK_REASONS, 0, &reasons,
                       sizeof(reasons), AYTHER_GENERATION_ANY, NULL)
      != AYTHER_STATUS_OK) {
    printf("  FALLA: no se puede leer 0x10E\n"); fail = 1;
  }
  memset(&journal, 0, sizeof(journal));
  if (api->read_region(AYTHER_REGION_RASTER_JOURNAL, 0, &journal,
                       sizeof(journal), AYTHER_GENERATION_ANY, NULL)
      != AYTHER_STATUS_OK) {
    printf("  FALLA: no se puede leer el journal\n"); fail = 1;
  }
  if (split)
  {
    unsigned k, cram_at_split = 0;
    for (k = 0; k < journal.count && k < AYTHER_JOURNAL_MAX_EVENTS; ++k)
    {
      const ayther_journal_event_v1 *ev = &journal.events[k];
      if (ev->reason == AYTHER_RASTER_REASON_CRAM && ev->address == 1 &&
          (unsigned)ev->v_counter + 1u >= boundary &&
          (unsigned)ev->v_counter <= boundary + 1u)
        ++cram_at_split;
    }
    {
      int ok = (reasons & AYTHER_RASTER_REASON_CRAM) && cram_at_split >= 1 &&
               journal.dropped == 0;
      printf("3. motivos 0x10E = 0x%x, journal con %u eventos (%u perdidos), %u de"
             " CRAM entrada 1 en la linea del split -> %s\n",
             (unsigned)reasons, (unsigned)journal.count, (unsigned)journal.dropped,
             cram_at_split, ok ? "correcto" : "MAL");
      if (!ok) fail = 1;
    }
  }
  else
  {
    int ok = reasons == 0 && journal.count == 0;
    printf("3. motivos 0x10E = 0x%x, journal con %u eventos -> %s\n",
           (unsigned)reasons, (unsigned)journal.count,
           ok ? "sin raster (correcto)" : "MAL");
    if (!ok) fail = 1;
  }

  /* --- 4. recomposicion contra el frame emitido --------------------------- */
  {
    uint32_t w = 0, h = 0;
    int32_t st = api->recompose_frame(recomposed, MAX_PX, 0, &w, &h);
    if (st != AYTHER_STATUS_OK || w != frame_w || h != frame_h) {
      printf("  FALLA: recompose_frame devolvio %d (%ux%u)\n", (int)st, w, h);
      fail = 1;
    } else {
      unsigned x, y, diff = 0, above = 0, below = 0;
      for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
          if (recomposed[(size_t)y * w + x] != frame_px[(size_t)y * w + x])
          {
            ++diff;
            if (y < boundary) ++above; else ++below;
          }
      if (!split)
      {
        printf("4. recomposicion: %u pixeles distintos de %u -> %s\n", diff, w * h,
               diff == 0 ? "pixel-perfect (correcto)" : "MAL");
        if (diff) fail = 1;
      }
      else
      {
        /* Estado final = lo que dejo la interrupcion de frame (rojo): la
           recomposicion es correcta arriba del split y equivocada abajo. Si
           alguna vez difiere de los DOS lados, ya no es "estado final" lo que
           esta pasando, y eso es lo que este numero vigila. */
        int ok = diff > 0 && (above == 0 || below == 0);
        printf("4. recomposicion desde el estado final: %u distintos (%u arriba del"
               " split, %u abajo) -> %s\n", diff, above, below,
               ok ? "confinada a un lado, como dice 0x10E (correcto)" : "MAL");
        if (!ok) fail = 1;
      }
    }
  }

  c->unload();
  return fail;
}

int main(int argc, char **argv)
{
  static uint8_t rom[AYTHER_GENERATED_ROM_SIZE];
  static const struct scene scenes[] = {
    { "A: solo scroll lock",           AYTHER_SMS_SCENE_SCROLL_LOCK },
    { "B: scroll lock + split de paleta",
      AYTHER_SMS_SCENE_SCROLL_LOCK | AYTHER_SMS_SCENE_PALETTE_SPLIT },
  };
  library_t lib;
  core_fns c;
  int fail = 0;
  size_t s;

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
    *(void **)&c.load   = load_symbol(lib, "retro_load_game");
    *(void **)&c.run    = load_symbol(lib, "retro_run");
    *(void **)&c.unload = load_symbol(lib, "retro_unload_game");
    *(void **)&c.iface  = load_symbol(lib, "ayther_get_interface");
    if (!p_set_env || !p_init || !c.load || !c.run || !c.unload || !c.iface) {
      fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
    }
    p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
    p_set_poll(poll_cb); p_set_inp(input_cb);
    p_init();
  }

  for (s = 0; s < sizeof(scenes) / sizeof(scenes[0]); ++s)
    fail |= check_scene(&c, &scenes[s], rom, sizeof(rom));

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  close_library(lib);
  return fail;
}
