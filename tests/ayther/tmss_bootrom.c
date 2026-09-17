/* #98: el boot ROM del TMSS, ejercitado con un boot ROM sintetico.
 *
 * Upstream 774e360a agrando el buffer del boot ROM de 2KB a 64KB porque el
 * banco de $000000 mide 64KB y una lectura mas alla de $0007FF se salia del
 * array; libretro.c expande ahora los 2KB al banco entero. La CI nunca habia
 * cargado una BIOS -- una BIOS de Mega Drive no puede vivir en el repo-, asi
 * que ese camino no tenia con que probarse. La respuesta es la misma que para
 * los cartuchos: un boot ROM generado, 2KB con "GENESIS OS" en 0x120, escrito
 * como bios_MD.bin en el directorio de sistema, con genesis_plus_gx_bios
 * encendido.
 *
 * El boot ROM lee tres palabras del banco -- la ULTIMA ($00FFFE), el primer
 * espejo ($000800) y uno del medio ($0087FE)-, las deja en work RAM, y pasa el
 * control al cartucho como lo hace el boot ROM real: escribe $A14101 desde un
 * stub en RAM, porque en ese instante el codigo de $000000 deja de existir.
 * El cartucho es el ROM sintetico de siempre.
 *
 * Lo que se afirma:
 *
 *   1. $00FFFE devuelve la marca de la ultima palabra del boot ROM: el banco
 *      esta expandido hasta el final. Sin 774e360a esa lectura cae 62KB
 *      fuera del buffer -- ASan lo dice-, y devuelve otra cosa: el test falla
 *      en cualquier plataforma, con o sin sanitizer.
 *   2. $000800 devuelve la palabra 0 y $0087FE la marca: los espejos son
 *      copias de los 2KB, no basura.
 *   3. el stub llego al final y el cartucho arranco: su contador de frames
 *      avanza. Un boot ROM que no devuelve el control es un core colgado en
 *      el arranque, que es el otro modo de fallar de este camino.
 *
 * Uso: tmss_bootrom <core> <directorio-de-trabajo>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
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
#define FRAMES   10

static const char *g_workdir = ".";
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
      if (data) *(const char **)data = g_workdir; return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *)data;
      if (!v || !v->key) return false;
      v->value = NULL;
      /* Lo unico que este harness enciende: el boot ROM. */
      if (!strcmp(v->key, "genesis_plus_gx_bios")) v->value = "enabled";
      return v->value != NULL;
    }
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

#define SYM(v,n) do { *(void **)&v = load_symbol(lib,n); \
  if(!v){fprintf(stderr,"falta %s\n",n);return 2;} } while(0)

static uint16_t ram_word(const uint8_t *ram, unsigned off)
{
  /* El core escribe la palabra con un *(uint16 *) nativo (work RAM va
     byteswapped en little endian): se lee igual, y el round-trip no depende
     del endianness del host. */
  uint16_t v;
  memcpy(&v, ram + off, sizeof(v));
  return v;
}

int main(int argc, char **argv)
{
  library_t lib;
  void (*p_set_env)(retro_environment_t); void (*p_set_vid)(retro_video_refresh_t);
  void (*p_set_aud)(retro_audio_sample_batch_t); void (*p_set_poll)(retro_input_poll_t);
  void (*p_set_inp)(retro_input_state_t); void (*p_init)(void);
  bool (*p_load)(const struct retro_game_info *); void (*p_run)(void);
  void *(*p_mem)(unsigned);
  static uint8_t rom[ROM_SIZE];
  static uint8_t boot[AYTHER_BOOT_ROM_SIZE];
  struct retro_game_info gi;
  char path[1024];
  const uint8_t *ram;
  uint16_t last, first, mid, done, frames;
  FILE *f;
  int i, fail = 0;

  if (argc < 3) { fprintf(stderr, "uso: %s <core> <directorio-de-trabajo>\n", argv[0]); return 2; }
  g_workdir = argv[2];
  lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  SYM(p_set_env,"retro_set_environment"); SYM(p_set_vid,"retro_set_video_refresh");
  SYM(p_set_aud,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_inp,"retro_set_input_state"); SYM(p_init,"retro_init");
  SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run");
  SYM(p_mem,"retro_get_memory_data");

  /* bios_MD.bin en el directorio de sistema, ANTES de load_game. */
  if (!ayther_build_generated_boot_rom(boot, sizeof(boot))) {
    fprintf(stderr, "no se pudo generar el boot ROM\n"); return 2;
  }
  snprintf(path, sizeof(path), "%s/bios_MD.bin", g_workdir);
  f = fopen(path, "wb");
  if (!f || fwrite(boot, 1, sizeof(boot), f) != sizeof(boot) || fclose(f)) {
    fprintf(stderr, "no se pudo escribir %s\n", path); return 2;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  if (!ayther_build_generated_rom(rom, ROM_SIZE)) { fprintf(stderr, "ROM\n"); return 2; }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path = "ayther-tmss.md"; gi_ext.dir = ".";
  gi_ext.name = "ayther-tmss"; gi_ext.ext = "md";
  gi_ext.data = rom; gi_ext.size = ROM_SIZE; gi_ext.persistent_data = true;
  memset(&gi, 0, sizeof(gi));
  gi.path = "ayther-tmss.md"; gi.data = rom; gi.size = ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); return 2; }

  for (i = 0; i < FRAMES; i++) p_run();

  ram = (const uint8_t *)p_mem(RETRO_MEMORY_SYSTEM_RAM);
  if (!ram) { fprintf(stderr, "sin RETRO_MEMORY_SYSTEM_RAM\n"); return 2; }
  last   = ram_word(ram, AYTHER_BOOT_RAM_LAST);
  first  = ram_word(ram, AYTHER_BOOT_RAM_FIRST);
  mid    = ram_word(ram, AYTHER_BOOT_RAM_MID);
  done   = ram_word(ram, AYTHER_BOOT_RAM_DONE);
  frames = ram_word(ram, 0); /* RAM_FRAME del cartucho, lo suma cada v-int */

  printf("1. $00FFFE (ultima palabra del banco)  -> 0x%04X %s\n", last,
         last == AYTHER_BOOT_MARK ? "(la marca: banco expandido hasta el final)"
                                  : (fail = 1, "(NO es la marca: el banco no llega)"));
  printf("2. $000800 (primer espejo)            -> 0x%04X %s\n", first,
         first == 0x00ffu ? "(palabra 0 del boot ROM)" : (fail = 1, "(NO es la palabra 0)"));
  printf("   $0087FE (espejo del medio)         -> 0x%04X %s\n", mid,
         mid == AYTHER_BOOT_MARK ? "(la marca)" : (fail = 1, "(NO es la marca)"));
  printf("3. el stub paso el control            -> %s, el cartucho conto %u frames %s\n",
         done == 1u ? "si" : (fail = 1, "NO"), frames,
         frames > 0u ? "(arranco)" : (fail = 1, "(NO arranco)"));

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
