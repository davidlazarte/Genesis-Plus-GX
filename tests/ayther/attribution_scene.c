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
 * La escena tambien lleva el sprite que hace falta para el bit de sprite EXACTO
 * (#31/#37/#41): uno con el mismo pattern, la misma paleta y la misma prioridad
 * que el fondo que tiene debajo. Su byte en el line buffer no cambia al
 * dibujarlo, asi que un diff contra el fondo no lo ve -- daba cero de 64-- y
 * dejaba un agujero adentro del sprite.
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

  /* --- #39.B: el descriptor de sistema dice lo que la escena es ---------- */
  /* Todo esto era derivable decodificando VDP_REGS del lado del consumidor, o
     sea reimplementando las reglas del core afuera del core. La escena tiene
     una configuracion conocida, asi que sirve de oraculo. */
  {
    ayther_system_v1 sys;
    memset(&sys, 0, sizeof(sys));
    if (api->read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK)
    {
      printf("system: la region no se puede leer  FALLA\n");
      fail = 1;
    }
    else
    {
      printf("\nsystem: hw=%02x mode=%u h40=%u s/h=%u pal=%u lineas=%u "
             "viewport=%ux%u fm=%u rom=%u B crc=%08x\n",
             sys.system_hw, sys.vdp_mode, sys.h40, sys.shadow_highlight,
             sys.region_pal, sys.lines_per_frame,
             sys.viewport_w, sys.viewport_h, sys.fm_core,
             (unsigned)sys.rom_bytes, (unsigned)sys.rom_crc32);
      if (sys.layout_version != AYTHER_LAYOUT_SYSTEM_V1 ||
          sys.struct_size != sizeof(sys)) {
        printf("  FALLA: cabecera del descriptor inconsistente\n"); fail = 1;
      }
      if (sys.system_hw != AYTHER_SYSTEM_HW_MD) {
        printf("  FALLA: el fixture es un cartucho de Mega Drive\n"); fail = 1;
      }
      if (sys.vdp_mode != 5) {
        printf("  FALLA: la escena programa Mode 5\n"); fail = 1;
      }
      if (!sys.h40) {
        printf("  FALLA: la escena programa H40 (reg 12 bit 0)\n"); fail = 1;
      }
      if (!sys.shadow_highlight) {
        printf("  FALLA: la escena programa shadow/highlight (reg 12 bit 3)\n");
        fail = 1;
      }
      if (sys.region_pal || sys.lines_per_frame != 262) {
        printf("  FALLA: la escena es NTSC, 262 lineas\n"); fail = 1;
      }
      if (sys.viewport_w != attrib_w || sys.viewport_h != attrib_h) {
        printf("  FALLA: el viewport tiene que describir el MISMO rectangulo "
               "que ATTRIBUTION (%ux%u)\n", attrib_w, attrib_h);
        fail = 1;
      }
      if (sys.rom_bytes != ROM_SIZE) {
        printf("  FALLA: el tamano de ROM no coincide con el cargado\n");
        fail = 1;
      }
      if (!sys.master_clock || !sys.cpu_clock ||
          sys.cpu_clock >= sys.master_clock) {
        printf("  FALLA: los relojes no son plausibles\n"); fail = 1;
      }
    }
  }

  /* --- #31/#37/#41: el bit de sprite, exacto ---------------------------- */
  /* El sprite de la escena tiene el mismo pattern, la misma paleta y la misma
     prioridad que el fondo que tiene debajo: su byte en el line buffer NO cambia
     al dibujarlo. Un diff contra el fondo no lo ve —daba 0 de 64— y dejaba un
     agujero adentro del sprite: el dim lo emitía a brillo pleno y la atribución
     no lo marcaba. Ahora el bit lo escribe la regla de prioridad, que no depende
     de que los bytes se distingan. */
  {
    int marked = count_sprite_px(AYTHER_SH_SPRITE_X, AYTHER_SH_SPRITE_Y, 8, 8);
    printf("\nsprite idéntico al fondo (8x8 en %d,%d): %d de 64 marcados\n",
           AYTHER_SH_SPRITE_X, AYTHER_SH_SPRITE_Y, marked);
    if (marked != 64) {
      printf("  FALLA: %d agujeros — el bit volvió a salir de un diff\n",
             64 - marked);
      fail = 1;
    }
  }

  /* Y el fondo lejos de los sprites sigue sin marcarse. Sin esto, un core que
     marcara TODO pasaría el caso de arriba. */
  {
    int marked = count_sprite_px(AYTHER_SH_BG_X, AYTHER_SH_BG_Y, 32, 8);
    printf("fondo puro               (32x8 en %d,%d): %d marcados\n",
           AYTHER_SH_BG_X, AYTHER_SH_BG_Y, marked);
    if (marked != 0) {
      printf("  FALLA: el bit de sprite está puesto sobre fondo puro\n");
      fail = 1;
    }
  }

  /* El segundo sprite es un operador de shadow/highlight: paleta 3, índices 14
     y 15. No pone color —modifica el brillo del píxel de abajo—, así que no es
     un sprite para nadie que lea este bit. El diff contra el fondo lo marcaba
     igual, porque el byte SÍ cambia: ése es el defecto 2 de #31. */
  {
    int marked = count_sprite_px(AYTHER_SH_OPERATOR_X, AYTHER_SH_OPERATOR_Y, 8, 8);
    printf("operador S/H             (8x8 en %d,%d): %d marcados\n",
           AYTHER_SH_OPERATOR_X, AYTHER_SH_OPERATOR_Y, marked);
    if (marked != 0) {
      printf("  FALLA: %d píxeles de brillo contados como sprite\n", marked);
      fail = 1;
    }
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
