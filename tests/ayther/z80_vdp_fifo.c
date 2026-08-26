/* #63: el Z80 escribiendo sin parar al puerto de datos del VDP por la ventana
 * de banco no puede tirar el core.
 *
 * Lo encontro el job nocturno de fuzzing en `write_control`, pero el archivo
 * que dejo no reproduce solo: el Z80 arrastra estado entre entradas del fuzzer
 * -- corre un frame por entrada y nunca se resetea--, y el crash dependia de
 * esa historia y no de la entrada. Este test es la version determinista: un
 * cartucho cuyo Z80 hace `ld (hl),a` en bucle sobre 0x8000 con el banco
 * apuntando a 0xC00000.
 *
 * Cada una de esas escrituras entra por zbank_write_vdp -> vdp_68k_data_w con
 * m68k.cycles clavado al final de la linea y sin nada que frene al Z80, y el
 * bucle que busca el proximo slot del FIFO se salia de fifo_timing_h40 a la
 * decima escritura de la misma linea.
 *
 * Dos cosas se verifican, y la primera es la que hace honesta a la segunda:
 *   1. las escrituras del Z80 LLEGAN al VDP: la VRAM se llena de 0x01, que es
 *      lo que escribe el programa. Sin esto, un Z80 que nunca arranco daria
 *      verde por el motivo equivocado.
 *   2. el core sigue vivo despues de 60 frames.
 *
 * Sin instrumentar, leer fuera de la tabla no se nota (cae en dma_timing, que
 * esta al lado). Quien lo ve es ASan sobre el core completo, como lo construye
 * el job nocturno. */
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
#define FRAMES   60
#define VRAM_MAX 65536u
/* Lo que escribe el Z80 (ver z80_vdp_hammer en generated_rom.c). */
#define Z80_BYTE 0x01u

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
    case RETRO_ENVIRONMENT_GET_VARIABLE: return false;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d,size_t f){(void)d;return f;}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

static unsigned count_byte(const uint8_t *p, size_t n, uint8_t v)
{
  unsigned c = 0; size_t i;
  for (i = 0; i < n; i++) if (p[i] == v) c++;
  return c;
}

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
  if (!ayther_build_generated_rom_z80_vdp(rom, ROM_SIZE)) { fprintf(stderr,"ROM\n"); return 2; }
  memset(&gi_ext,0,sizeof(gi_ext));
  gi_ext.full_path="ayther-z80-vdp.md"; gi_ext.dir="."; gi_ext.name="ayther-z80-vdp";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-z80-vdp.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr,"load_game fallo\n"); return 2; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr,"sin ABI\n"); return 2; }
  api->set_subscriptions(AYTHER_SUB_VDP_MEMORY);

  static uint8_t before[VRAM_MAX], after[VRAM_MAX];
  ayther_region_info_v1 info; memset(&info, 0, sizeof(info));
  if (api->query_region(AYTHER_REGION_VRAM, &info, sizeof(info)) != AYTHER_STATUS_OK ||
      !info.byte_size || info.byte_size > VRAM_MAX) {
    fprintf(stderr, "no puedo leer la VRAM\n"); return 2;
  }

  /* Un frame para que el 68000 termine de armar el cartucho y suelte el bus. */
  p_run();
  if (api->read_region(AYTHER_REGION_VRAM, 0, before, info.byte_size,
                       AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    fprintf(stderr, "read_region fallo\n"); return 2;
  }

  { int f; for (f = 0; f < FRAMES; f++) p_run(); }

  if (api->read_region(AYTHER_REGION_VRAM, 0, after, info.byte_size,
                       AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    fprintf(stderr, "read_region fallo\n"); return 2;
  }

  {
    unsigned nb = count_byte(before, info.byte_size, Z80_BYTE);
    unsigned na = count_byte(after,  info.byte_size, Z80_BYTE);
    /* Una linea activa tiene 18 slots en H40 y el Z80 mete decenas de
       escrituras por linea: en 60 frames son cientos de KB de 0x01, muchos mas
       que la VRAM entera. Se pide poco a proposito -- lo que se prueba es que
       el camino existe, no cuanto rinde. */
    int reached = (na > nb) && (na - nb >= 1024u);
    printf("1. bytes 0x%02X en VRAM: %u antes, %u despues de %d frames -> %s\n",
           Z80_BYTE, nb, na, FRAMES,
           reached ? "el Z80 llega al puerto de datos (correcto)"
                   : "el Z80 NO esta escribiendo: el test no ejercita nada");
    printf("2. el core sigue vivo despues de %d frames\n", FRAMES);
    if (!reached) { printf("\nFALLO\n"); return 1; }
  }

  printf("\nTODO OK\n");
  return 0;
}
