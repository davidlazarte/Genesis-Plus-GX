/* #74: la lectura de VRAM en Mode 4 hecha por el 68000.
 *
 * vdp_68k_data_r_m4 calculaba el indice de VRAM con una formula entrelazada
 * mientras que vdp_68k_data_w_m4 -- y todo el camino del Z80-- usan
 * `addr & 0x3FFE`. Escribir y leer la misma direccion daba dos lugares
 * distintos de VRAM. Upstream lo arreglo en 29ba0a88; en este fork Mode 4 esta
 * soportado desde #40 y no habia un solo test que recorriera esa funcion:
 * check-mode4 y check-mode4-raster entran a Mode 4 con codigo Z80 en un
 * cartucho de Master System, y el VDP de Mega Drive en Mode 4 es otro camino.
 *
 * El fixture escribe dos palabras distintas en las dos direcciones que la
 * formula rota confunde entre si, lee la primera, y deja lo leido en dos
 * lugares que este harness mira por separado:
 *
 *   1. la palabra de work RAM en AYTHER_M4_RESULT, que es el valor exacto que
 *      el 68000 recibio del puerto de datos;
 *   2. la entrada 0 de CRAM, que con la name table en ceros pinta el frame
 *      entero -- verde si la lectura fue correcta, rojo si trajo el alias.
 *
 * Los dos se afirman: el primero es preciso y el segundo prueba que el valor
 * de veras atraveso el core hasta el frame emitido.
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
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *load_symbol(library_t l, const char *n) { return dlsym(l, n); }
#endif

#define ROM_SIZE AYTHER_GENERATED_ROM_SIZE
#define FRAMES 8

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

/* Ultimo frame emitido. Se guarda una copia porque el puntero que el core pasa
   deja de ser valido apenas retro_run vuelve. */
#define MAX_PIXELS (512u * 512u)
static uint16_t frame[MAX_PIXELS];
static unsigned frame_w, frame_h;
static int frame_seen;

static void vid_cb(const void *d, unsigned w, unsigned h, size_t pitch)
{
  unsigned y;
  const uint8_t *src = (const uint8_t *)d;
  if (!d || !w || !h || (size_t)w * h > MAX_PIXELS) return;
  for (y = 0; y < h; y++)
    memcpy(frame + (size_t)y * w, src + (size_t)y * pitch, (size_t)w * 2u);
  frame_w = w; frame_h = h; frame_seen = 1;
}

static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

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
  void *(*p_mem)(unsigned);
  SYM(p_set_env,"retro_set_environment"); SYM(p_set_vid,"retro_set_video_refresh");
  SYM(p_set_aud,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_inp,"retro_set_input_state"); SYM(p_init,"retro_init");
  SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run");
  SYM(p_mem,"retro_get_memory_data");

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom_mode4_68k(rom, ROM_SIZE)) {
    fprintf(stderr, "no se pudo generar el ROM\n"); return 2;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path = "ayther-mode4-68k.md"; gi_ext.dir = ".";
  gi_ext.name = "ayther-mode4-68k"; gi_ext.ext = "md";
  gi_ext.data = rom; gi_ext.size = ROM_SIZE; gi_ext.persistent_data = true;
  struct retro_game_info gi; memset(&gi, 0, sizeof(gi));
  gi.path = "ayther-mode4-68k.md"; gi.data = rom; gi.size = ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); return 2; }

  { int f; for (f = 0; f < FRAMES; f++) p_run(); }

  int fail = 0;

  /* 1. el valor exacto que el 68000 leyo del puerto de datos.
     El core guarda la palabra con un `*(uint16 *)` nativo, asi que se lee de la
     misma forma: el round-trip no depende del endianness del host. */
  {
    const uint8_t *ram = (const uint8_t *)p_mem(RETRO_MEMORY_SYSTEM_RAM);
    uint16_t got = 0;
    if (!ram) { fprintf(stderr, "sin RETRO_MEMORY_SYSTEM_RAM\n"); return 2; }
    memcpy(&got, ram + AYTHER_M4_RESULT, sizeof(got));
    printf("1. el 68000 leyo 0x%04X de VRAM 0x%04X en Mode 4 -> %s\n",
           got, AYTHER_M4_ADDR_READ,
           (got == AYTHER_M4_VALUE_GOOD) ? "lo que escribio ahi (correcto)"
             : (got == AYTHER_M4_VALUE_ALIAS)
                 ? "el ALIAS de 0x2004: formula entrelazada"
                 : "un valor que no es ninguno de los dos");
    if (got != AYTHER_M4_VALUE_GOOD) fail = 1;
  }

  /* 2. el mismo valor, atravesando el core hasta el frame emitido. */
  if (!frame_seen) {
    fprintf(stderr, "el core no emitio ningun frame\n"); return 2;
  } else {
    size_t i, n = (size_t)frame_w * frame_h;
    uint16_t first = frame[0];
    size_t distintos = 0;
    unsigned r, g, b;
    for (i = 0; i < n; i++) if (frame[i] != first) distintos++;
    r = (first >> 11) & 0x1Fu; g = (first >> 5) & 0x3Fu; b = first & 0x1Fu;
    printf("2. frame %ux%u uniforme en 0x%04X (r=%u g=%u b=%u), %u pixeles distintos -> %s\n",
           frame_w, frame_h, first, r, g, b, (unsigned)distintos,
           (distintos == 0 && g > r) ? "verde: la lectura llego al frame"
             : (distintos == 0) ? "ROJO: el frame muestra el alias"
                                : "el frame no es uniforme");
    if (distintos != 0 || g <= r) fail = 1;
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
