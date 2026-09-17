/* #98: accesos de palabra a direcciones impares y al borde de un banco, con la
 * emulacion de address error apagada.
 *
 * Tres commits de upstream (3d948cab m68kcpu.h, 7b74da34 mem68k.c, f9caf203
 * scd.c) cambiaron `& 0xffff` por `& 0xfffe` en los accesos crudos de 16 bits:
 * con la mascara vieja, una direccion impar leia o escribia un uint16
 * desalineado, y en la ultima palabra de un banco un byte fuera del array.
 * Entraron por el sync de #74 sin mover un hash, porque ningun fixture hacia
 * accesos impares y todos usan direcciones pares.
 *
 * Y no alcanza con un savestate mutado: con la emulacion de address error
 * encendida -el default- el 68000 convierte cada acceso impar en una
 * excepcion ANTES de llegar al acceso crudo, y la mascara no se ejecuta.
 * genesis_plus_gx_addr_error es una opcion del core que el usuario puede
 * apagar, y con ella apagada los accesos crudos son el camino normal: es la
 * configuracion que este harness pide, y la unica en la que las tres
 * correcciones hacen algo.
 *
 * Dos fixtures (ver generated_rom.h): el cartucho, para m68kcpu.h y
 * mem68k.c; el Sega CD, para scd.c. Cada acceso impar deja lo leido en work
 * RAM, y lo que se afirma es que devolvio la palabra PAR de al lado, que es lo
 * que la mascara corregida hace. Bajo ASan/UBSan, ademas, cada acceso con la
 * mascara vieja es un reporte: desalineado en todos, y fuera del array en los
 * del borde del banco.
 *
 * Uso: odd_word_access <core> <directorio-de-trabajo> [md|scd]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "generated_rom.h"
#include "cd_fixture.h"

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
static int g_cd;
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
      if (g_cd) return false;   /* el CD entra por ruta */
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
      /* Lo unico que este harness cambia del default. */
      if (!strcmp(v->key, "genesis_plus_gx_addr_error")) v->value = "disabled";
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
  uint16_t v;
  memcpy(&v, ram + off, sizeof(v));
  return v;
}
static uint32_t ram_long(const uint8_t *ram, unsigned off)
{
  /* El 68000 escribe el long como dos palabras nativas: alta y despues baja. */
  return ((uint32_t)ram_word(ram, off) << 16) | ram_word(ram, off + 2u);
}

static int check(const char *what, unsigned got, unsigned want)
{
  printf("  %-44s 0x%0*X %s\n", what, want > 0xffffu ? 8 : 4, got,
         got == want ? "(correcto)" : "(MAL)");
  return got != want;
}

int main(int argc, char **argv)
{
  library_t lib;
  void (*p_set_env)(retro_environment_t); void (*p_set_vid)(retro_video_refresh_t);
  void (*p_set_aud)(retro_audio_sample_batch_t); void (*p_set_poll)(retro_input_poll_t);
  void (*p_set_inp)(retro_input_state_t); void (*p_init)(void);
  bool (*p_load)(const struct retro_game_info *); void (*p_run)(void);
  void (*p_unload)(void); void (*p_deinit)(void);
  void *(*p_mem)(unsigned);
  static uint8_t rom[ROM_SIZE];
  struct retro_game_info gi;
  char path[1024];
  const uint8_t *ram;
  const char *only = NULL;
  int i, fx, fail = 0;

  if (argc < 3) { fprintf(stderr, "uso: %s <core> <directorio-de-trabajo> [md|scd]\n", argv[0]); return 2; }
  g_workdir = argv[2];
  if (argc > 3) only = argv[3];
  lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  SYM(p_set_env,"retro_set_environment"); SYM(p_set_vid,"retro_set_video_refresh");
  SYM(p_set_aud,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_inp,"retro_set_input_state"); SYM(p_init,"retro_init");
  SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run");
  SYM(p_unload,"retro_unload_game"); SYM(p_deinit,"retro_deinit");
  SYM(p_mem,"retro_get_memory_data");

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);

  for (fx = 0; fx < 2; ++fx) {
    g_cd = fx;
    if (only && strcmp(only, g_cd ? "scd" : "md") != 0) continue;
    p_init();
    memset(&gi, 0, sizeof(gi));
    if (g_cd) {
      if (!ayther_cd_fixture_write_ex(g_workdir, path, sizeof(path), AYTHER_CD_BIOS_ODD_ACCESS)) {
        fprintf(stderr, "no se pudo escribir el fixture de CD en %s\n", g_workdir); return 2;
      }
      gi.path = path;
    } else {
      if (!ayther_build_generated_rom_odd_access(rom, ROM_SIZE)) { fprintf(stderr, "ROM\n"); return 2; }
      memset(&gi_ext, 0, sizeof(gi_ext));
      gi_ext.full_path = "ayther-odd.md"; gi_ext.dir = ".";
      gi_ext.name = "ayther-odd"; gi_ext.ext = "md";
      gi_ext.data = rom; gi_ext.size = ROM_SIZE; gi_ext.persistent_data = true;
      gi.path = "ayther-odd.md"; gi.data = rom; gi.size = ROM_SIZE;
    }
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo (%s)\n", g_cd ? "scd" : "md"); return 2; }
    for (i = 0; i < FRAMES; i++) p_run();
    ram = (const uint8_t *)p_mem(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram) { fprintf(stderr, "sin RETRO_MEMORY_SYSTEM_RAM\n"); return 2; }

    if (!g_cd) {
      printf("cartucho, address error apagado:\n");
      fail |= check("$FFFFFF leido (m68ki_read_16, borde del banco)", ram_word(ram, AYTHER_ODD_RAM_LAST_R), AYTHER_ODD_VALUE_LAST);
      fail |= check("$FFFFFE tras escribir $FFFFFF (m68ki_write_16)", ram_word(ram, AYTHER_ODD_RAM_LAST_W), AYTHER_ODD_VALUE_LAST_W);
      /* El long en $FFFFFD son las palabras $FFFFFC (nunca escrita: 0) y
         $FFFFFE (lo que dejo la escritura de arriba). Y el long escrito en
         $FFFFFD despues tiene que haber caido en esas dos palabras pares. */
      fail |= check("long leido en $FFFFFD (m68ki_read_32)", ram_long(ram, AYTHER_ODD_RAM_LONG),
                    AYTHER_ODD_VALUE_LAST_W);
      fail |= check("long escrito en $FFFFFD (m68ki_write_32)", ram_long(ram, 0xfffcu), 0x0badf00du);
      printf("  %-44s 0x%04X\n", "palabra del bus sin usar con PC impar", ram_word(ram, AYTHER_ODD_RAM_BUS));
      fail |= check("el stub en PC impar volvio (m68k_read_bus_16)", ram_word(ram, AYTHER_ODD_RAM_DONE), 1u);
    } else {
      printf("Sega CD, address error apagado:\n");
      fail |= check("espejo impar de PRG-RAM (prg_ram_m68k_read_word)", ram_word(ram, AYTHER_ODD_RAM_PRG), AYTHER_ODD_VALUE_PRG);
      fail |= check("espejo impar de Word-RAM (word_ram_m68k_read_word)", ram_word(ram, AYTHER_ODD_RAM_WORD), AYTHER_ODD_VALUE_WORD);
      fail |= check("escritura impar por el espejo (word_ram_m68k_write_word)", ram_word(ram, AYTHER_ODD_RAM_WORD_W), AYTHER_ODD_VALUE_WORD_W);
      printf("  %-44s 0x%04X\n", "palabra que el sub leyo en $0C0000 con PC impar", ram_word(ram, AYTHER_ODD_RAM_SUB_BUS));
      fail |= check("el sub volvio de PC impar (s68k_read_bus_16)", ram_word(ram, AYTHER_ODD_RAM_SUB_DONE), 1u);
    }
    p_unload();
    p_deinit();
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
