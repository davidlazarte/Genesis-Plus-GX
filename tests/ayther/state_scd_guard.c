/* #97: un savestate de Sega CD con un indice de pista invalido se RECHAZA, no
 * se carga corrido.
 *
 * Upstream (c06f586b) le puso a cdd_context_load una guarda contra un indice
 * de pista fuera de la TOC, y la guarda devuelve 0. Pero el contrato de esa
 * funcion es "bytes consumidos", y el llamador los suma: con 0, el bloque del
 * PCM y todo lo que sigue del CD se parsean desde el offset equivocado, y
 * scd_context_load le propaga el mismo corrimiento a state_load. O sea: la
 * lectura fuera de rango que motivo el commit se evita, y en su lugar queda
 * una carga corrupta que retro_unserialize da por buena.
 *
 * Aca el 0 se convierte en rechazo de verdad -- el mismo camino que ya usa
 * state_load cuando el blob no trae el id "SCD!"-, y este test lo fija:
 *
 *   1. el estado propio del fixture de CD   -> se acepta
 *   2. el mismo estado con cdd.index = 200  -> se RECHAZA
 *   3. el estado propio despues del rechazo -> se acepta y el core corre
 *
 * Por que 200 y no 2: la imagen del fixture tiene UNA pista de datos, pero
 * cdd_load le inventa a toda imagen MODE1 sin pistas de audio la TOC por
 * defecto de 99 pistas (dos segundos de audio cada una), asi que toc.last es
 * 99 y un indice 2 es una pista simulada perfectamente valida. Un CD no puede
 * tener mas de 99 pistas: 200 esta fuera de cualquier TOC, real o inventada.
 *
 * El 3 importa tanto como el 2: un rechazo a mitad del blob deja cargado lo
 * que venia antes del CD, y lo que se afirma es que el core sigue aceptando
 * un estado valido despues, que es lo que hace el frontend.
 *
 * Donde esta cdd.index dentro del blob no se fija por constante: cdd_context_
 * save escribe cycles, latency, index, lba, scanOffset y fader[2], y el
 * fixture recien arrancado tiene index = 0, scanOffset = 0 y los dos faders
 * en 0x400 (volumen maximo, el valor de reset). Se busca esa firma detras del
 * id "SCD!" y se exige que aparezca UNA vez: si el layout del bloque cambia,
 * el test lo dice en vez de mutar otro campo en silencio.
 *
 * Uso: state_scd_guard <core> <directorio-de-trabajo>
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

#define BOOT_FRAMES 30

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

static const char *g_workdir = ".";

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
    /* Sin GAME_INFO_EXT a proposito: el CD entra por ruta. */
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data) *(const char **)data = g_workdir; return data != NULL;
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

/* Busca la firma del bloque del CDD detras de "SCD!". Devuelve el offset de
   cdd.index, o 0 si no hay exactamente una coincidencia. */
static size_t find_cdd_index(const uint8_t *blob, size_t n)
{
  static const uint8_t fader[4] = { 0x00, 0x04, 0x00, 0x04 };
  const uint8_t *scd = NULL;
  size_t i, found = 0, at = 0;

  for (i = 0; i + 4 <= n; ++i)
    if (!memcmp(blob + i, "SCD!", 4)) { scd = blob + i + 4; break; }
  if (!scd) { fprintf(stderr, "el blob no trae el id SCD!\n"); return 0; }

  /* index(=0) lba(4) scanOffset(=0) fader[2](=0x400,0x400) */
  for (i = (size_t)(scd - blob); i + 16 <= n; ++i) {
    uint32_t lba;
    if (memcmp(blob + i, "\0\0\0\0", 4)) continue;
    if (memcmp(blob + i + 8, "\0\0\0\0", 4)) continue;
    if (memcmp(blob + i + 12, fader, 4)) continue;
    memcpy(&lba, blob + i + 4, 4);
    if (lba > AYTHER_CD_ISO_SECTORS) continue;
    found++;
    at = i;
  }
  if (found != 1) {
    fprintf(stderr, "la firma del CDD aparece %u veces, esperaba 1\n", (unsigned)found);
    return 0;
  }
  return at;
}

int main(int argc, char **argv)
{
  library_t lib;
  struct retro_game_info gi;
  char iso[1024];
  uint8_t *buf, *keep;
  size_t n, at;
  uint32_t lba;
  int i, fail = 0;

  if (argc < 3) { fprintf(stderr, "uso: %s <core> <directorio-de-trabajo>\n", argv[0]); return 2; }
  g_workdir = argv[2];
  lib = open_library(argv[1]);
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

  if (!ayther_cd_fixture_write(g_workdir, iso, sizeof(iso))) {
    fprintf(stderr, "no se pudo escribir el fixture de CD en %s\n", g_workdir);
    return 2;
  }
  memset(&gi, 0, sizeof(gi));
  gi.path = iso;
  if (!p_load_game(&gi)) { fprintf(stderr, "load_game fallo con el Sega CD sintetico\n"); return 2; }
  for (i = 0; i < BOOT_FRAMES; i++) p_run();

  n = p_serialize_size();
  buf = (uint8_t *)malloc(n);
  keep = (uint8_t *)malloc(n);
  if (!buf || !keep || !p_serialize(buf, n)) { fprintf(stderr, "serialize fallo\n"); return 2; }
  memcpy(keep, buf, n);

  at = find_cdd_index(buf, n);
  if (!at) return 2;
  memcpy(&lba, buf + at + 4, 4);
  printf("cdd.index en el offset %u (lba %u, pista 0)\n", (unsigned)at, (unsigned)lba);

  printf("1. estado propio                   -> %s\n",
         p_unserialize(buf, n) ? "ACEPTADO (correcto)" : (fail = 1, "RECHAZADO (MAL)"));

  /* cdd.index = 200: fuera de cualquier TOC (99 pistas como maximo). Es el
     caso que c06f586b quiso rechazar. */
  buf[at] = 200;
  printf("2. indice de pista fuera de la TOC -> %s\n",
         p_unserialize(buf, n) ? (fail = 1, "ACEPTADO (MAL: se carga corrido)") : "RECHAZADO (correcto)");

  printf("3. estado propio tras el rechazo   -> %s\n",
         p_unserialize(keep, n) ? "ACEPTADO (correcto)" : (fail = 1, "RECHAZADO (MAL)"));
  for (i = 0; i < 5; i++) p_run();

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
