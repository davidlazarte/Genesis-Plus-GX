/* #40 fase 2: los controles del fork en Mode 4.
 *
 * Corre con el FIXTURE SMS del repo -- codigo Z80 generado por
 * `ayther_build_generated_rom_sms`-, asi que entra en CI como cualquier otra
 * suite. Antes dependia de un ROM comercial, que no puede vivir aca, y por eso
 * se salteaba: una implementacion sin nada que la ejercite es como llegamos a
 * que `raster_rom_probe` estuviera roto durante meses.
 *
 * `AYTHER_SMS_ROM` sigue existiendo y ahora es lo OPCIONAL: apuntandola a un
 * cartucho real, el mismo test corre sobre el. El fixture prueba que el
 * contrato se cumple; un cartucho real prueba que se cumple sobre algo que
 * nadie diseno para pasarlo.
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

/* #40: cuantos pixeles difieren, y si alguno cae FUERA de un rectangulo dado.
   Con el fixture -- ocho sprites de 8x8 en posiciones conocidas-- eso convierte
   "suprimir el slot 3" en una afirmacion por coordenada en vez de un hash que
   cambio. Un hash distinto solo dice que algo se movio; esto dice QUE. */
static void diff_outside(const uint16_t *before, unsigned x0, unsigned y0,
                         unsigned w, unsigned h,
                         unsigned *total, unsigned *outside)
{
  unsigned x, y;
  *total = 0; *outside = 0;
  for (y = 0; y < frame_h; ++y)
    for (x = 0; x < frame_w; ++x)
    {
      size_t k = (size_t)y * frame_w + x;
      if (before[k] == frame_px[k]) continue;
      ++*total;
      if (x < x0 || x >= x0 + w || y < y0 || y >= y0 + h) ++*outside;
    }
}

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
  const char *rom_name;
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
  if (rom_path && *rom_path)
  {
    f = fopen(rom_path, "rb");
    if (!f) {
      fprintf(stderr, "no se puede abrir %s\n", rom_path);
      return 2;
    }
    rom_size = fread(rom, 1, sizeof(rom), f);
    fclose(f);
    if (!rom_size) { fprintf(stderr, "ROM vacio\n"); return 2; }
    rom_name = rom_path;
    printf("cartucho: %s (%lu bytes)\n", rom_path, (unsigned long)rom_size);
  }
  else
  {
    rom_size = ayther_build_generated_rom_sms(rom, sizeof(rom));
    if (!rom_size) {
      fprintf(stderr, "no se pudo construir el fixture SMS\n");
      return 2;
    }
    rom_name = "ayther-sms-v1.sms";
    printf("cartucho: fixture SMS del repo (%lu bytes)\n",
           (unsigned long)rom_size);
  }

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
    void (*p_unload)(void)                        = load_symbol(lib, "retro_unload_game");
    ayther_get_interface_fn p_iface =
      (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
    if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
      fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
    }

    p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
    p_set_poll(poll_cb); p_set_inp(input_cb);
    p_init();

    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = rom_name; gi_ext.dir = "."; gi_ext.name = "sms";
    gi_ext.ext = "sms"; gi_ext.data = rom; gi_ext.size = rom_size;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = rom_name; gi.data = rom; gi.size = rom_size;
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
        static uint16_t before[MAX_PX];
        /* Con el fixture se elige el slot 3, que es el que nombra el criterio
           de aceptacion y del que sabemos que es visible.

           Con un cartucho arbitrario no se puede saber cual lo es: DRAWN
           significa "llego al bucle de dibujo", no "pinto pixeles que se ven".
           Un sprite con patron transparente, o tapado por otro de mayor
           prioridad, llega igual. Asi que se prueban los slots dibujados hasta
           encontrar uno que de verdad cambie el frame, y lo que se afirma es
           que ALGUNO lo hace: suprimir tiene que tener efecto visible en
           alguna parte, aunque no sepamos de antemano en cual. */
        {
          int tried = 0, changed = 0;
          for (i = 0; i < (int)sizeof(outcome) && !changed && tried < 16; ++i)
          {
            if (!(outcome[i] & AYTHER_SPR_OUT_DRAWN)) continue;
            if (!rom_path && i != 3 && (outcome[3] & AYTHER_SPR_OUT_DRAWN))
              continue;              /* el fixture usa el 3 y solo el 3 */
            victim = i;
            ++tried;
            memcpy(before, frame_px, sizeof(before));
            memset(mask, 0, sizeof(mask));
            mask[victim >> 3] = (uint8_t)(1u << (victim & 7));
            if (api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask,
                                   sizeof(mask), AYTHER_GENERATION_ANY, NULL)
                  != AYTHER_STATUS_OK) {
              printf("  FALLA: suprimir un sprite en Mode 4 tendria que aceptarse\n");
              fail = 1;
              break;
            }
            {
              int f2;
              for (f2 = 0; f2 < 8; f2++) p_run();
            }
            suppressed = hash_frame();
            changed = (suppressed != base_hash);
            if (!changed) {
              memset(mask, 0, sizeof(mask));
              api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, mask,
                                 sizeof(mask), AYTHER_GENERATION_ANY, NULL);
              {
                int f2;
                for (f2 = 0; f2 < 8; f2++) p_run();
              }
            }
          }
          printf("se suprimio el slot %d (%d probados)\n", victim, tried);
          if (!changed) {
            printf("  FALLA: ningun sprite dibujado cambia el frame al "
                   "suprimirlo\n");
            fail = 1;
          }
        }
        /* El oraculo exacto: con el fixture sabemos DONDE esta ese sprite, asi
           que lo que cambio tiene que caber en sus 8x8 y nada mas. */
        if (!rom_path)
        {
          unsigned total = 0, outside = 0;
          unsigned x0 = AYTHER_SMS_SPRITE_X0 + (unsigned)victim * 24u;
          diff_outside(before, x0, AYTHER_SMS_SPRITE_Y, 8u, 8u,
                       &total, &outside);
          printf("  cambiaron %u pixeles, %u fuera del sprite %d (%u,%u 8x8)\n",
                 total, outside, victim, x0, AYTHER_SMS_SPRITE_Y);
          if (total != 64u) {
            printf("  FALLA: un sprite de 8x8 son 64 pixeles, no %u\n", total);
            fail = 1;
          }
          if (outside) {
            printf("  FALLA: suprimir un sprite tiene que sacar EXACTAMENTE ese\n");
            fail = 1;
          }
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

    /* --- 5b. suprimir una celda revela el backdrop ---------------------- */
    /*
     * La region 0x104 oculta una CELDA de pantalla. En Mode 5 eso significa
     * "pela el primer plano y mostra lo que hay detras": plano B, o el
     * backdrop si B esta vacio, y lo resuelve el merge de A con B. En Mode 4
     * hay UN plano de fondo y `render_bg_m4` no llama a `merge()`, asi que el
     * peel no tenia donde engancharse: la region se rechazaba.
     *
     * Con un solo plano la operacion sigue teniendo un significado y solo uno:
     * revelar el backdrop. El fixture lo hace comprobable por color -- fondo
     * rojo, backdrop azul- sin reimplementar el renderer.
     */
    {
      static uint16_t before[MAX_PX];
      uint8_t cells[512];
      unsigned total, outside;
      const unsigned COL_A = 2u, COL_B = 20u, ROW = 1u;

      memcpy(before, frame_px, sizeof(before));
      memset(cells, 0, sizeof(cells));
      #define MARK_CELL(r, c) do { unsigned b_ = (r) * 64u + (c);         cells[b_ >> 3] |= (uint8_t)(1u << (b_ & 7u)); } while (0)
      MARK_CELL(ROW, COL_A);
      MARK_CELL(ROW, COL_B);

      if (api->write_control(AYTHER_REGION_TILE_SUPPRESS, 0, cells,
                             sizeof(cells), AYTHER_GENERATION_ANY, NULL)
            != AYTHER_STATUS_OK) {
        printf("  FALLA: ocultar una celda en Mode 4 tendria que aceptarse\n");
        fail = 1;
      }
      for (i = 0; i < 8; i++) p_run();

      /* Solo esas dos celdas cambian. Se cuenta el total y se mira que nada
         caiga fuera de la fila: el rectangulo de `diff_outside` es uno solo,
         asi que se le pasa la fila entera y se comprueba el total aparte. */
      diff_outside(before, 0u, ROW * 8u, frame_w, 8u, &total, &outside);
      if (outside) {
        printf("  FALLA: %u pixeles cambiaron fuera de la fila de celdas %u\n",
               outside, ROW);
        fail = 1;
      }
      if (total != 128u) {
        printf("  FALLA: dos celdas de 8x8 son 128 pixeles, cambiaron %u\n",
               total);
        fail = 1;
      }

      /* Y lo que se revela es UN color, el mismo en las dos celdas y distinto
         del fondo. Eso es el backdrop sin tener que decodificar RGB565. */
      {
        uint16_t rev = frame_px[(size_t)(ROW * 8u) * frame_w + COL_A * 8u];
        uint16_t bg  = before[(size_t)(ROW * 8u) * frame_w + COL_A * 8u];
        unsigned x, y, distintos = 0;
        for (y = ROW * 8u; y < ROW * 8u + 8u; ++y) {
          for (x = COL_A * 8u; x < COL_A * 8u + 8u; ++x)
            if (frame_px[(size_t)y * frame_w + x] != rev) ++distintos;
          for (x = COL_B * 8u; x < COL_B * 8u + 8u; ++x)
            if (frame_px[(size_t)y * frame_w + x] != rev) ++distintos;
        }
        printf("celda oculta: %u pixeles, revelan %04x sobre un fondo %04x\n",
               total, rev, bg);
        if (distintos) {
          printf("  FALLA: %u de los 128 pixeles revelados no son el mismo "
                 "color\n", distintos);
          fail = 1;
        }
        if (rev == bg) {
          printf("  FALLA: ocultar la celda no cambio el color\n");
          fail = 1;
        }
      }

      /* La prueba de que se pela el FONDO y no el frame: la celda que el
         sprite 0 tapa por completo. El sprite es 8x8 solido y cae justo en
         una celda, y se dibuja DESPUES del peel, asi que ocultar esa celda no
         tiene que cambiar un solo pixel. Si el peel corriera despues de los
         sprites, o si borrara el frame en vez del fondo, aca se veria. */
      memset(cells, 0, sizeof(cells));
      api->write_control(AYTHER_REGION_TILE_SUPPRESS, 0, cells, sizeof(cells),
                         AYTHER_GENERATION_ANY, NULL);
      for (i = 0; i < 8; i++) p_run();
      memcpy(before, frame_px, sizeof(before));
      MARK_CELL(AYTHER_SMS_SPRITE_Y / 8u, AYTHER_SMS_SPRITE_X0 / 8u);
      if (api->write_control(AYTHER_REGION_TILE_SUPPRESS, 0, cells,
                             sizeof(cells), AYTHER_GENERATION_ANY, NULL)
            != AYTHER_STATUS_OK) {
        printf("  FALLA: ocultar la celda del sprite tendria que aceptarse\n");
        fail = 1;
      }
      for (i = 0; i < 8; i++) p_run();
      diff_outside(before, 0u, 0u, frame_w, frame_h, &total, &outside);
      printf("celda tapada por el sprite 0: %u pixeles cambiados\n", total);
      if (total) {
        printf("  FALLA: el sprite tapa la celda entera; pelar el fondo "
               "debajo no puede verse\n");
        fail = 1;
      }
      #undef MARK_CELL

      /* Limpiar para las secciones que siguen. */
      memset(cells, 0, sizeof(cells));
      api->write_control(AYTHER_REGION_TILE_SUPPRESS, 0, cells, sizeof(cells),
                         AYTHER_GENERATION_ANY, NULL);
      for (i = 0; i < 8; i++) p_run();
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

    /* --- 6. Game Gear: el frame emitido es 160x144 ---------------------- */
    /*
     * La Game Gear usa el MISMO VDP en Mode 4; lo que cambia es que el area
     * visible se recorta a 160x144. El core lo hace con offsets NEGATIVOS
     * (`viewport.x = -48`, `y = -24`), asi que `viewport.w/h` siguen diciendo
     * 256x192 y el frame que sale por `video_refresh` mide 160x144.
     *
     * Esa diferencia es la pregunta del issue: si las regiones del fork
     * describen el frame que el frontend recibe o el area interna del VDP. Un
     * consumidor que indexe la atribucion con las coordenadas del frame
     * emitido y reciba dimensiones internas lee la fila equivocada.
     *
     * El fixture sirve igual: son los mismos bytes, cargados con extension
     * .gg. Es lo unico que el core mira para elegir el hardware.
     */
    {
      struct retro_game_info gg;
      ayther_region_info_v1 info;

      p_unload();
      memset(&gi_ext, 0, sizeof(gi_ext));
      gi_ext.full_path = "ayther-sms-v1.gg"; gi_ext.dir = ".";
      gi_ext.name = "gg"; gi_ext.ext = "gg";
      gi_ext.data = rom; gi_ext.size = rom_size;
      gi_ext.persistent_data = true;
      memset(&gg, 0, sizeof(gg));
      gg.path = "ayther-sms-v1.gg"; gg.data = rom; gg.size = rom_size;
      if (!p_load(&gg)) {
        printf("  FALLA: el mismo fixture no carga como Game Gear\n");
        fail = 1;
      } else {
        api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS |
                               AYTHER_SUB_SPRITE_CAPTURE |
                               AYTHER_SUB_RECOMPOSITION |
                               AYTHER_SUB_ATTRIBUTION |
                               AYTHER_SUB_VDP_MEMORY);
        for (i = 0; i < FRAMES; i++) p_run();

        memset(&sys, 0, sizeof(sys));
        api->read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys),
                         AYTHER_GENERATION_ANY, NULL);
        printf("\ngame gear: hw=%02x modo=%u descriptor=%ux%u frame emitido=%ux%u\n",
               sys.system_hw, sys.vdp_mode, sys.viewport_w, sys.viewport_h,
               frame_w, frame_h);
        if (sys.system_hw != AYTHER_SYSTEM_HW_GG) {
          printf("  FALLA: el mismo ROM con extension .gg tiene que ser Game Gear\n");
          fail = 1;
        }
        if (frame_w != 160u || frame_h != 144u) {
          printf("  FALLA: la Game Gear emite 160x144\n");
          fail = 1;
        }

        /* El descriptor promete el frame emitido. Es la promesa que se
           rompia: el core recorta con offsets negativos y los campos, que son
           unsigned, publicaban 256x192 con un x envuelto a 65488. */
        if (sys.viewport_w != frame_w || sys.viewport_h != frame_h) {
          printf("  FALLA: el descriptor dice %ux%u y el frame es %ux%u\n",
                 sys.viewport_w, sys.viewport_h, frame_w, frame_h);
          fail = 1;
        }
        if (sys.viewport_x != 48u || sys.viewport_y != 24u) {
          printf("  FALLA: el offset dentro del area interna es (48,24), "
                 "no (%u,%u)\n", sys.viewport_x, sys.viewport_y);
          fail = 1;
        }

        /* ATTRIBUTION dice "un byte por pixel del frame emitido". */
        memset(&info, 0, sizeof(info));
        if (api->query_region(AYTHER_REGION_ATTRIBUTION, &info, sizeof(info))
              != AYTHER_STATUS_OK) {
          printf("  FALLA: ATTRIBUTION tendria que existir en Game Gear\n");
          fail = 1;
        } else if (info.byte_size != (uint64_t)frame_w * frame_h) {
          printf("  FALLA: atribucion de %u bytes para un frame de %ux%u\n",
                 (unsigned)info.byte_size, frame_w, frame_h);
          fail = 1;
        } else {
          printf("atribucion: %u bytes = %ux%u\n",
                 (unsigned)info.byte_size, frame_w, frame_h);
        }

        /* Y la prueba de que los offsets del recorte son los correctos y no
           dos numeros que dan el tamano justo: la recomposicion tiene que
           coincidir pixel a pixel con el frame que recibio el frontend. Si el
           recorte estuviera corrido, el tamano seguiria estando bien y esta
           comparacion fallaria en cada pixel. */
        {
          uint32_t w = 0, h = 0;
          int32_t st = api->recompose_frame(recomposed, MAX_PX, 0, &w, &h);
          if (st != AYTHER_STATUS_OK) {
            printf("  FALLA: recomponer en Game Gear devolvio %d\n", (int)st);
            fail = 1;
          } else if (w != frame_w || h != frame_h) {
            printf("  FALLA: la recomposicion mide %ux%u y el frame %ux%u\n",
                   w, h, frame_w, frame_h);
            fail = 1;
          } else {
            unsigned diff = 0, k;
            for (k = 0; k < w * h; ++k)
              if (recomposed[k] != frame_px[k]) ++diff;
            printf("recomposicion en GG: %ux%u, %u pixeles distintos de %u\n",
                   w, h, diff, w * h);
            if (diff) {
              printf("  FALLA: la recomposicion de Game Gear no reproduce el "
                     "frame emitido\n");
              fail = 1;
            }
          }
        }

        /* La descomposicion por capas es de Mode 5 y punto: la Game Gear no
           tiene plano B ni ventana. Lo que importa es que lo diga en vez de
           devolver un tamano cualquiera, asi que se afirma el rechazo. */
        {
          int32_t st = api->recompose_multilayer(recomposed, NULL, NULL, NULL,
                                                 NULL, MAX_PX, 0, NULL, NULL);
          if (st == AYTHER_STATUS_OK) {
            printf("  FALLA: descomponer en capas no existe en Mode 4; "
                   "aceptarlo devuelve un plano B inventado\n");
            fail = 1;
          }
        }
      }
    }
  }

  printf("\nmode4-controls: %s\n", fail ? "FALLA" : "OK");
  close_library(lib);
  return fail;
}
