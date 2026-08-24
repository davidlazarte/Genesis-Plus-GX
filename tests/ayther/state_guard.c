/* El guard de layout del savestate, verificado contra el core real.
 *
 * El formato de upstream es un volcado crudo de memoria, y varios structs se
 * vuelcan enteros con sizeof. Z80_Regs mide 88 bytes en x64 y 76 en x86 porque
 * lleva dos punteros a funcion adentro, asi que un estado escrito por un
 * binario no se puede leer con otro de distinto ABI.
 *
 * Y NADA lo detectaba: STATE_SIZE es 0xfd000 hardcodeado, identico en las dos
 * arquitecturas, y la firma "GENPLUS-GX 1.7.7" tambien. El estado pasaba las
 * dos validaciones y se cargaba mal EN SILENCIO.
 *
 * Este test fija las tres conductas del guard. La 1 ya la cubria el roundtrip
 * de full_core_replay; las otras dos no las cubria nadie, que es justo donde
 * un guard se rompe sin que se note:
 *
 *   1. estado propio            -> se acepta
 *   2. layout de otra arch      -> se RECHAZA (antes: corrupcion silenciosa)
 *   3. estado viejo sin tag     -> se acepta (compatibilidad hacia atras)
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
static library_t open_library(const char *path) { return LoadLibraryA(path); }
static void *load_symbol(library_t lib, const char *name)
{
  return (void *)(uintptr_t)GetProcAddress(lib, name);
}
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *path)
{
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static void *load_symbol(library_t lib, const char *name)
{
  return dlsym(lib, name);
}
#endif

#define ROM_SIZE AYTHER_GENERATED_ROM_SIZE

static void (*p_set_environment)(retro_environment_t);
static void (*p_set_video_refresh)(retro_video_refresh_t);
static void (*p_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_set_input_poll)(retro_input_poll_t);
static void (*p_set_input_state)(retro_input_state_t);
static void (*p_init)(void);
static bool (*p_load_game)(const struct retro_game_info *);
static void (*p_run)(void);
static size_t (*p_serialize_size)(void);
static bool (*p_serialize)(void *, size_t);
static bool (*p_unserialize)(const void *, size_t);

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

#define SYM(v,n) do { *(void **)&v = load_symbol(lib,n); \
  if(!v){fprintf(stderr,"falta %s\n",n);return 2;} } while(0)

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core.dll>\n", argv[0]); return 2; }
  library_t lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  SYM(p_set_environment,"retro_set_environment");
  SYM(p_set_video_refresh,"retro_set_video_refresh");
  SYM(p_set_audio_sample_batch,"retro_set_audio_sample_batch");
  SYM(p_set_input_poll,"retro_set_input_poll");
  SYM(p_set_input_state,"retro_set_input_state");
  SYM(p_init,"retro_init");
  SYM(p_load_game,"retro_load_game");
  SYM(p_run,"retro_run");
  SYM(p_serialize_size,"retro_serialize_size");
  SYM(p_serialize,"retro_serialize");
  SYM(p_unserialize,"retro_unserialize");

  p_set_environment(env_cb); p_set_video_refresh(vid_cb);
  p_set_audio_sample_batch(aud_cb); p_set_input_poll(poll_cb); p_set_input_state(input_cb);
  p_init();

  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  if (!rom || ayther_build_generated_rom(rom, ROM_SIZE) == 0) { fprintf(stderr,"ROM\n"); return 2; }
  memset(&gi_ext,0,sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data = rom; gi.size = ROM_SIZE;
  if (!p_load_game(&gi)) { fprintf(stderr,"load_game fallo\n"); return 2; }
  for (int i = 0; i < 10; i++) p_run();

  size_t n = p_serialize_size();
  unsigned char *buf = (unsigned char *)malloc(n);
  if (!p_serialize(buf, n)) { fprintf(stderr,"serialize fallo\n"); return 2; }

  unsigned char *tag = buf + n - 16;
  uint32_t magic=0, layout=0;
  memcpy(&magic,tag,4); memcpy(&layout,tag+4,4);
  printf("tag magic  : 0x%08x %s\n", magic, magic==0x53535941u?"(AYSS, presente)":"(AUSENTE)");
  printf("layout id  : 0x%08x  (puntero %u bits)\n", layout, (unsigned)(sizeof(void*)*8));

  int fail = 0;
  /* 1. mismo layout -> debe cargar */
  printf("1. estado propio                 -> %s\n",
         p_unserialize(buf,n) ? "ACEPTADO (correcto)" : (fail=1,"RECHAZADO (MAL)"));

  /* 2. layout distinto (lo que seria un estado de la otra arquitectura) */
  uint32_t otro = layout ^ 0xFFFFFFFFu;
  memcpy(tag+4,&otro,4);
  printf("2. layout de otra arquitectura   -> %s\n",
         p_unserialize(buf,n) ? (fail=1,"ACEPTADO (MAL: corrompe en silencio)") : "RECHAZADO (correcto)");

  /* 3. sin tag: estados viejos deben seguir cargando */
  memset(tag,0,16);
  printf("3. estado viejo sin tag          -> %s\n",
         p_unserialize(buf,n) ? "ACEPTADO (correcto, compat)" : (fail=1,"RECHAZADO (MAL)"));

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
