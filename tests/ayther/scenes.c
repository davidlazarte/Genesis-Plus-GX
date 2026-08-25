/* #35: un hash por MODO DE VIDEO, no uno solo para todos.
 *
 * El fixture determinista de siempre ejercita Mode 5 H40 progresivo NTSC, sin
 * window y sin DMA. Los deltas del fork que tocan los otros modos no tenian con
 * que probarse: #28 arreglo las mascaras de render en interlace 2 y en vscroll
 * enhanced SIN un fixture que los ejercitara, y que hoy funcionen es una
 * afirmacion que nadie puede rehacer.
 *
 * Cada escena es un ROM completo con una configuracion de VDP fija. Se corre,
 * se hashea video y audio por separado y se compara contra el golden. Un golden
 * por escena es lo que convierte "algo se rompio" en "se rompio el interlace 2".
 *
 * El golden se REGENERA con `make regen-scenes CORE=...`, y ese comando existe
 * a proposito: un golden que solo se puede regenerar a mano se termina editando
 * a mano, que es como un golden deja de significar algo.
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
#define FRAMES   30
#define FNV_OFFSET UINT64_C(0xCBF29CE484222325)
#define FNV_PRIME  UINT64_C(0x100000001B3)

static struct retro_game_info_ext gi_ext;
static uint64_t video_hash, audio_hash;
static unsigned video_w, video_h, video_frames;

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

static void vid_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
  const uint8_t *row = (const uint8_t *)data;
  unsigned y, x;
  if (!data) return;                    /* frame duplicado: no aporta bytes */
  video_w = w; video_h = h; ++video_frames;
  /* Se hashea fila por fila y no el buffer entero porque el pitch trae relleno
     al final de cada una, y ese relleno no es parte de la imagen: meterlo haria
     que el hash dependiera del ancho del framebuffer y no del frame emitido. */
  for (y = 0; y < h; ++y, row += pitch)
    for (x = 0; x < w * 2u; ++x)
    {
      video_hash ^= row[x];
      video_hash *= FNV_PRIME;
    }
}

static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  for (i = 0; i < frames * 2u; ++i)
  {
    audio_hash ^= (uint64_t)(uint16_t)d[i];
    audio_hash *= FNV_PRIME;
  }
  return frames;
}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

struct scene_result
{
  uint64_t video, audio;
  unsigned w, h, frames;
};

static int run_scene(const char *dll, size_t scene, struct scene_result *out)
{
  library_t lib = open_library(dll);
  unsigned f;
  if (!lib) { fprintf(stderr, "no carga %s\n", dll); return 0; }

  void (*p_set_env)(retro_environment_t)        = load_symbol(lib, "retro_set_environment");
  void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(lib, "retro_set_video_refresh");
  void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(lib, "retro_set_audio_sample_batch");
  void (*p_set_poll)(retro_input_poll_t)        = load_symbol(lib, "retro_set_input_poll");
  void (*p_set_inp)(retro_input_state_t)        = load_symbol(lib, "retro_set_input_state");
  void (*p_init)(void)                          = load_symbol(lib, "retro_init");
  bool (*p_load)(const struct retro_game_info *)= load_symbol(lib, "retro_load_game");
  void (*p_run)(void)                           = load_symbol(lib, "retro_run");
  if (!p_set_env || !p_init || !p_load || !p_run) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 0;
  }

  video_hash = FNV_OFFSET; audio_hash = FNV_OFFSET;
  video_w = video_h = video_frames = 0;

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom_scene(rom, ROM_SIZE, scene)) {
    fprintf(stderr, "no se pudo construir la escena %u\n", (unsigned)scene);
    close_library(lib); return 0;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-scene.md"; gi_ext.dir="."; gi_ext.name="ayther-scene";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-scene.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 0; }

  for (f = 0; f < FRAMES; ++f) p_run();
  close_library(lib);

  out->video = video_hash; out->audio = audio_hash;
  out->w = video_w; out->h = video_h; out->frames = video_frames;
  return 1;
}

static void write_golden(FILE *f, const struct scene_result *r, size_t n)
{
  size_t i;
  fprintf(f, "# #35: un hash por escena. Regenerar con `make -C tests regen-scenes CORE=...`\n");
  fprintf(f, "# escena video audio ancho alto frames\n");
  for (i = 0; i < n; ++i)
    fprintf(f, "%s %016llx %016llx %u %u %u\n", ayther_scene_name(i),
            (unsigned long long)r[i].video, (unsigned long long)r[i].audio,
            r[i].w, r[i].h, r[i].frames);
}

int main(int argc, char **argv)
{
  const char *dll, *golden_path;
  int regen = 0;
  size_t n, i;
  struct scene_result results[16];
  FILE *golden;
  int fail = 0;

  if (argc < 3) {
    fprintf(stderr, "uso: %s <core> <golden> [--regen]\n", argv[0]);
    return 2;
  }
  dll = argv[1];
  golden_path = argv[2];
  regen = (argc > 3 && strcmp(argv[3], "--regen") == 0);

  n = ayther_scene_count();
  if (n > sizeof(results) / sizeof(results[0])) {
    fprintf(stderr, "mas escenas que capacidad del arreglo\n"); return 2;
  }

  for (i = 0; i < n; ++i)
    if (!run_scene(dll, i, &results[i]))
      return 2;

  if (regen) {
    /* "wb": en Windows el modo texto escribe CRLF, y el arbol de tests esta
       normalizado a LF. Sin esto, regenerar el golden en Windows lo deja
       modificado para git aunque los hashes no hayan cambiado. */
    golden = fopen(golden_path, "wb");
    if (!golden) { fprintf(stderr, "no se puede escribir %s\n", golden_path); return 2; }
    write_golden(golden, results, n);
    fclose(golden);
    printf("golden de escenas regenerado: %s (%u escenas)\n",
           golden_path, (unsigned)n);
    return 0;
  }

  golden = fopen(golden_path, "r");
  if (!golden) {
    fprintf(stderr, "falta %s; generalo con `make -C tests regen-scenes CORE=...`\n",
            golden_path);
    return 2;
  }

  for (i = 0; i < n; ++i) {
    char name[64], line[256];
    unsigned long long v = 0, a = 0;
    unsigned w = 0, h = 0, frames = 0;
    int matched = 0;

    while (fgets(line, sizeof(line), golden)) {
      if (line[0] == '#' || line[0] == '\n') continue;
      matched = sscanf(line, "%63s %llx %llx %u %u %u",
                       name, &v, &a, &w, &h, &frames);
      break;
    }
    if (matched != 6) {
      printf("%-12s FALTA en el golden\n", ayther_scene_name(i));
      fail = 1; continue;
    }
    if (strcmp(name, ayther_scene_name(i)) != 0) {
      printf("golden desalineado: esperaba %s, encontro %s\n",
             ayther_scene_name(i), name);
      fail = 1; continue;
    }

    if (results[i].video == v && results[i].audio == a &&
        results[i].w == w && results[i].h == h && results[i].frames == frames) {
      printf("%-12s OK   %ux%u  %u frames\n", name, w, h, frames);
    } else {
      printf("%-12s DIFIERE\n", name);
      if (results[i].video != v)
        printf("               video  %016llx  golden %016llx\n",
               (unsigned long long)results[i].video, v);
      if (results[i].audio != a)
        printf("               audio  %016llx  golden %016llx\n",
               (unsigned long long)results[i].audio, a);
      if (results[i].w != w || results[i].h != h)
        printf("               viewport %ux%u  golden %ux%u\n",
               results[i].w, results[i].h, w, h);
      if (results[i].frames != frames)
        printf("               frames %u  golden %u\n", results[i].frames, frames);
      fail = 1;
    }
  }
  fclose(golden);

  /* Una escena cuyo hash de video coincide con el de otra no distingue nada:
     puede romperse el modo que dice cubrir y el golden no se entera. No es un
     fallo -- hay pares que legitimamente producen el mismo frame-- pero tiene
     que estar a la vista, porque la unica forma de que una escena inutil
     sobreviva es que nadie la mire. */
  {
    int dups = 0;
    for (i = 1; i < n; ++i)
    {
      size_t j;
      for (j = 0; j < i; ++j)
        if (results[i].video == results[j].video)
        {
          printf("\n[aviso] '%s' emite el mismo frame que '%s': esa escena no "
                 "distingue nada por si sola\n",
                 ayther_scene_name(i), ayther_scene_name(j));
          ++dups;
          break;
        }
    }
    printf("\n%u escenas, %u hashes de video distintos\n",
           (unsigned)n, (unsigned)(n - (size_t)dups));
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
