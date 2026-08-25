/* Infraestructura compartida de los targets de fuzzing. (#34)
 *
 * Cada target es una funcion `LLVMFuzzerTestOneInput(data, size)` y nada mas.
 * Quien la llama depende de con que se compilo:
 *
 *   con libFuzzer   -- el fuzzer, con entradas mutadas, en el job nocturno;
 *   sin libFuzzer   -- `fuzz_replay.c`, que le pasa los archivos del corpus y
 *                      de regressions/, y eso corre en `make check`.
 *
 * Los dos caminos importan. El primero busca; el segundo garantiza que lo
 * encontrado siga arreglado, y -- no menos importante-- que los targets se
 * puedan compilar y ejercitar en una maquina sin clang. Un target de fuzzing
 * que solo existe adentro del job nocturno es un target que nadie mira hasta
 * que se rompe, y para entonces nadie sabe si se rompio el target o el core.
 *
 * El core se carga UNA vez por proceso. libFuzzer llama a la funcion millones
 * de veces: hacer dlopen por entrada convertiria el fuzzer en un medidor de la
 * velocidad de dlopen.
 */

#ifndef AYTHER_FUZZ_COMMON_H
#define AYTHER_FUZZ_COMMON_H

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
typedef HMODULE fuzz_lib_t;
#define FUZZ_OPEN(p)     LoadLibraryA(p)
#define FUZZ_SYM(l, n)   ((void *)(uintptr_t)GetProcAddress((l), (n)))
#else
#include <dlfcn.h>
typedef void *fuzz_lib_t;
#define FUZZ_OPEN(p)     dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define FUZZ_SYM(l, n)   dlsym((l), (n))
#endif

typedef struct fuzz_core
{
  fuzz_lib_t lib;
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  void (*init)(void);
  void (*deinit)(void);
  bool (*load_game)(const struct retro_game_info *);
  void (*unload_game)(void);
  void (*run)(void);
  size_t (*serialize_size)(void);
  bool (*serialize)(void *, size_t);
  bool (*unserialize)(const void *, size_t);
  const ayther_interface_v1 *api;
} fuzz_core;

static fuzz_core fuzz_g;
static uint8_t fuzz_rom[AYTHER_GENERATED_ROM_SIZE];
static struct retro_game_info_ext fuzz_gi_ext;

static bool fuzz_env_cb(unsigned cmd, void *data)
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
      if (data) *(const struct retro_game_info_ext **)data = &fuzz_gi_ext;
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
static void fuzz_vid_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static size_t fuzz_aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void fuzz_poll_cb(void) {}
static int16_t fuzz_input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* La ruta del core viaja por el entorno: libFuzzer es dueno de argv y no deja
   pasar argumentos propios. El driver de replay setea la misma variable. */
static const char *fuzz_core_path(void)
{
  const char *p = getenv("AYTHER_FUZZ_CORE");
  return (p && *p) ? p : NULL;
}

/* Carga el core y el ROM sintetico. Devuelve NULL si no se puede: un target de
   fuzzing que no encuentra el core tiene que DECIRLO y salir, no fuzzear el
   vacio en silencio y reportar cero hallazgos. */
static fuzz_core *fuzz_core_get(void)
{
  const char *path;
  struct retro_game_info gi;

  if (fuzz_g.lib) return &fuzz_g;

  path = fuzz_core_path();
  if (!path) {
    fprintf(stderr, "AYTHER_FUZZ_CORE no esta seteada\n");
    exit(2);
  }
  fuzz_g.lib = FUZZ_OPEN(path);
  if (!fuzz_g.lib) {
    fprintf(stderr, "no carga el core: %s\n", path);
    exit(2);
  }

#define FUZZ_BIND(field, name) \
  do { *(void **)&fuzz_g.field = FUZZ_SYM(fuzz_g.lib, name); \
       if (!fuzz_g.field) { fprintf(stderr, "falta %s\n", name); exit(2); } } while (0)
  FUZZ_BIND(set_environment,        "retro_set_environment");
  FUZZ_BIND(set_video_refresh,      "retro_set_video_refresh");
  FUZZ_BIND(set_audio_sample_batch, "retro_set_audio_sample_batch");
  FUZZ_BIND(set_input_poll,         "retro_set_input_poll");
  FUZZ_BIND(set_input_state,        "retro_set_input_state");
  FUZZ_BIND(init,                   "retro_init");
  FUZZ_BIND(deinit,                 "retro_deinit");
  FUZZ_BIND(load_game,              "retro_load_game");
  FUZZ_BIND(unload_game,            "retro_unload_game");
  FUZZ_BIND(run,                    "retro_run");
  FUZZ_BIND(serialize_size,         "retro_serialize_size");
  FUZZ_BIND(serialize,              "retro_serialize");
  FUZZ_BIND(unserialize,            "retro_unserialize");
#undef FUZZ_BIND

  {
    ayther_get_interface_fn iface =
      (ayther_get_interface_fn)FUZZ_SYM(fuzz_g.lib, "ayther_get_interface");
    fuzz_g.api = iface ? iface(0) : NULL;
  }

  fuzz_g.set_environment(fuzz_env_cb);
  fuzz_g.set_video_refresh(fuzz_vid_cb);
  fuzz_g.set_audio_sample_batch(fuzz_aud_cb);
  fuzz_g.set_input_poll(fuzz_poll_cb);
  fuzz_g.set_input_state(fuzz_input_cb);
  fuzz_g.init();

  if (!ayther_build_generated_rom(fuzz_rom, sizeof(fuzz_rom))) {
    fprintf(stderr, "no se pudo construir el ROM sintetico\n");
    exit(2);
  }
  memset(&fuzz_gi_ext, 0, sizeof(fuzz_gi_ext));
  fuzz_gi_ext.full_path = "ayther-fuzz.md"; fuzz_gi_ext.dir = ".";
  fuzz_gi_ext.name = "ayther-fuzz"; fuzz_gi_ext.ext = "md";
  fuzz_gi_ext.data = fuzz_rom; fuzz_gi_ext.size = sizeof(fuzz_rom);
  fuzz_gi_ext.persistent_data = true;
  memset(&gi, 0, sizeof(gi));
  gi.path = "ayther-fuzz.md"; gi.data = fuzz_rom; gi.size = sizeof(fuzz_rom);
  if (!fuzz_g.load_game(&gi)) {
    fprintf(stderr, "load_game fallo con el ROM sintetico\n");
    exit(2);
  }
  return &fuzz_g;
}

/* Lector de bits del buffer del fuzzer: los targets necesitan sacar numeros
   acotados de un array de bytes arbitrario sin salirse ni sesgar de mas. */
typedef struct fuzz_reader { const uint8_t *p; size_t n, i; } fuzz_reader;

static void fuzz_reader_init(fuzz_reader *r, const uint8_t *d, size_t n)
{ r->p = d; r->n = n; r->i = 0; }

static uint32_t fuzz_u8(fuzz_reader *r)
{ return (r->i < r->n) ? r->p[r->i++] : 0u; }

static uint32_t fuzz_u32(fuzz_reader *r)
{
  uint32_t v = fuzz_u8(r);
  v |= fuzz_u8(r) << 8; v |= fuzz_u8(r) << 16; v |= fuzz_u8(r) << 24;
  return v;
}

/* Quedan bytes por consumir. */
static int fuzz_more(const fuzz_reader *r) { return r->i < r->n; }

#endif /* AYTHER_FUZZ_COMMON_H */
