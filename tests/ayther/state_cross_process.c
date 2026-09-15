/* #75, segundo criterio: un savestate de Master System con FM Nuked tiene que
 * cargar EN UN PROCESO NUEVO y sonar igual que si nunca se hubiera guardado.
 *
 * Por que un proceso nuevo y no un dlopen mas. Los dos issues de upstream que
 * este test persigue -- libretro/Genesis-Plus-GX#403 (crash) y #290 (audio
 * roto)-- dicen los dos lo mismo: "cargar el savestate EN UNA SESION NUEVA".
 * Esa frase no es decorativa, es la condicion del bug. En la sesion que guardo,
 * el chip venia corriendo y sus contadores e indices ya eran coherentes; en una
 * sesion nueva el chip esta recien reseteado y el blob se copia crudo encima.
 * Reproducirlo dentro del mismo proceso no se puede: dlopen del mismo .so
 * devuelve el mismo handle y los globales del core -- opll, opll_cycles,
 * opll_accm-- son los que ya estaban. El segundo proceso no es una comodidad
 * del test: es la mitad del caso.
 *
 * QUE AFIRMA, exactamente:
 *
 *   1. el core acepta su propio savestate en un proceso nuevo y no crashea;
 *   2. continuar normalmente y restaurar el estado dan los MISMOS hashes de
 *      audio y video. Es el criterio de fondo: un savestate que carga pero
 *      cambia como suena lo que sigue no restauro el estado, restauro una
 *      parte;
 *   3. cargarlo en un proceso NUEVO da bit por bit lo mismo que cargarlo en el
 *      MISMO proceso. Parece implicado por 2, y no lo esta: aisla lo que
 *      depende del proceso -- un puntero serializado, una direccion movida por
 *      el ASLR-- de lo que le falta al savestate. Cuando 2 falla, es la
 *      comparacion que dice cual de las dos cosas paso;
 *   4. el fixture suena. Dos silencios comparados dan "igual" sin probar nada,
 *      asi que se mide la energia de la corrida nativa y se exige que no sea
 *      cero.
 *
 * UNA ADVERTENCIA SOBRE PLATAFORMAS. El defecto que destapo este test -- un
 * puntero a una tabla estatica del core, serializado y desreferenciado despues-
 * se ve en Linux y NO necesariamente en Windows: ahi la base de una DLL se
 * aleatoriza una vez por ARRANQUE y es la misma para todos los procesos, asi
 * que el puntero viejo suele seguir siendo valido y la carga anda. Medido: un
 * DLL sin el arreglo pasa este test con los mismos hashes que uno con el
 * arreglo. Por eso la prueba negativa esta tomada en Linux, y por eso el issue
 * de upstream le aparece a unos y a otros no.
 *
 * El test igual corre en los tres sistemas: la afirmacion -- el proceso nuevo
 * tiene que dar lo mismo que el mismo proceso-- vale en todos, y hay estado
 * dependiente del proceso que si se veria en Windows.
 *
 * Uso:
 *   state_cross_process <core> <dir-de-trabajo>
 *       el driver: se relanza tres veces y compara.
 *   state_cross_process --save <core> <estado> <hashes>
 *       arranca limpio, guarda el checkpoint y deja los hashes NATIVOS.
 *   state_cross_process --same <core> <estado> <hashes>
 *       ida y vuelta dentro del mismo proceso: la referencia de la afirmacion 2.
 *   state_cross_process --load <core> <estado> <hashes>
 *       proceso nuevo: carga el checkpoint y deja sus hashes.
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
/* No se cierra la biblioteca a proposito: cada modo termina el proceso y el
   SO la suelta. Cerrarla a mano despues de deinit solo agrega una via de
   falla en el camino de salida. */
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *load_symbol(library_t l, const char *n) { return dlsym(l, n); }

#endif

#define ROM_SIZE      AYTHER_GENERATED_ROM_SIZE
#define BOOT_FRAMES   30u   /* el fixture ya escribio el FM y la envolvente corre */
#define CROSS_FRAMES  10u   /* los que pide el criterio de cierre                 */
#define FNV_OFFSET    UINT64_C(0xcbf29ce484222325)
#define FNV_PRIME     UINT64_C(0x100000001B3)

static struct retro_game_info_ext gi_ext;
static uint64_t g_audio_hash;
static uint64_t g_video_hash;
static uint64_t g_audio_energy;   /* que el FM no este mudo: un silencio es
                                     igual a otro silencio, y dos silencios
                                     comparados dan "OK" sin probar nada */
static uint64_t g_audio_samples;
static int      g_capturing;

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
    /* Las dos variables que ponen el YM2413 de Nuked en el camino. Sin la
       segunda el core usa el YM2413 de MAME y el test no toca ni una linea de
       lo que los issues de upstream describen. */
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *)data;
      if (!v || !v->key) return false;
      v->value = NULL;
      if (!strcmp(v->key, "genesis_plus_gx_ym2413"))      v->value = "enabled";
      else if (!strcmp(v->key, "genesis_plus_gx_ym2413_core")) v->value = "nuked";
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}

static void vid_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
  const uint8_t *p = (const uint8_t *)data;
  unsigned y, x;
  if (!g_capturing || !p) return;
  for (y = 0; y < h; ++y) {
    const uint16_t *row = (const uint16_t *)(const void *)(p + (size_t)y * pitch);
    for (x = 0; x < w; ++x) {
      g_video_hash ^= row[x];
      g_video_hash *= FNV_PRIME;
    }
  }
}

static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  if (!g_capturing) return frames;
  for (i = 0; i < frames * 2u; ++i) {
    g_audio_hash ^= (uint64_t)(uint16_t)d[i];
    g_audio_hash *= FNV_PRIME;
    g_audio_energy += (uint64_t)(d[i] < 0 ? -d[i] : d[i]);
    g_audio_samples++;
  }
  return frames;
}
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

struct core_api {
  library_t lib;
  void   (*set_environment)(retro_environment_t);
  void   (*set_video_refresh)(retro_video_refresh_t);
  void   (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void   (*set_input_poll)(retro_input_poll_t);
  void   (*set_input_state)(retro_input_state_t);
  void   (*init)(void);
  void   (*deinit)(void);
  bool   (*load_game)(const struct retro_game_info *);
  void   (*unload_game)(void);
  void   (*run)(void);
  size_t (*serialize_size)(void);
  bool   (*serialize)(void *, size_t);
  bool   (*unserialize)(const void *, size_t);
};

static int load_api(const char *path, struct core_api *api)
{
  memset(api, 0, sizeof(*api));
  api->lib = open_library(path);
  if (!api->lib) { fprintf(stderr, "no carga el core: %s\n", path); return 0; }
#define BIND(field, name) \
  do { *(void **)&api->field = load_symbol(api->lib, name); \
       if (!api->field) { fprintf(stderr, "falta %s\n", name); return 0; } } while (0)
  BIND(set_environment,        "retro_set_environment");
  BIND(set_video_refresh,      "retro_set_video_refresh");
  BIND(set_audio_sample_batch, "retro_set_audio_sample_batch");
  BIND(set_input_poll,         "retro_set_input_poll");
  BIND(set_input_state,        "retro_set_input_state");
  BIND(init,                   "retro_init");
  BIND(deinit,                 "retro_deinit");
  BIND(load_game,              "retro_load_game");
  BIND(unload_game,            "retro_unload_game");
  BIND(run,                    "retro_run");
  BIND(serialize_size,         "retro_serialize_size");
  BIND(serialize,              "retro_serialize");
  BIND(unserialize,            "retro_unserialize");
#undef BIND
  return 1;
}

/* El fixture SMS con la escena FM: escribe el YM2413 y deja tres canales en
   key-on. Sin eso el chip queda en reset y su contexto no distingue una carga
   sana de una podrida -- el test seria verde por vacio. */
static int boot(struct core_api *api, uint8_t *rom, struct retro_game_info *game)
{
  if (!ayther_build_generated_rom_sms_scene(rom, ROM_SIZE, AYTHER_SMS_SCENE_FM)) {
    fprintf(stderr, "no se pudo construir el fixture SMS con FM\n");
    return 0;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path = "ayther-sms-fm.sms"; gi_ext.dir = ".";
  gi_ext.name = "ayther-sms-fm"; gi_ext.ext = "sms";
  gi_ext.data = rom; gi_ext.size = ROM_SIZE;
  gi_ext.persistent_data = true;
  memset(game, 0, sizeof(*game));
  game->path = "ayther-sms-fm.sms"; game->data = rom; game->size = ROM_SIZE;

  api->set_environment(env_cb);
  api->set_video_refresh(vid_cb);
  api->set_audio_sample_batch(aud_cb);
  api->set_input_poll(poll_cb);
  api->set_input_state(input_cb);
  api->init();
  if (!api->load_game(game)) {
    fprintf(stderr, "el core rechazo el fixture SMS\n");
    return 0;
  }
  return 1;
}

static int write_hashes(const char *path)
{
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "no se puede escribir %s\n", path); return 0; }
  fprintf(f, "%016llx %016llx %llu %llu\n",
          (unsigned long long)g_audio_hash, (unsigned long long)g_video_hash,
          (unsigned long long)g_audio_energy, (unsigned long long)g_audio_samples);
  fclose(f);
  return 1;
}

static int read_hashes(const char *path, unsigned long long out[4])
{
  FILE *f = fopen(path, "rb");
  int n;
  if (!f) { fprintf(stderr, "no se puede leer %s\n", path); return 0; }
  n = fscanf(f, "%llx %llx %llu %llu", &out[0], &out[1], &out[2], &out[3]);
  fclose(f);
  if (n != 4) { fprintf(stderr, "%s no tiene los cuatro valores\n", path); return 0; }
  return 1;
}

/* --save: arranca limpio, serializa el checkpoint y sigue diez frames. Esos
   diez son la corrida NATIVA, la referencia contra la que se compara. */
static int mode_save(const char *core, const char *state_path, const char *hash_path)
{
  struct core_api api;
  struct retro_game_info game;
  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  uint8_t *state = NULL;
  size_t size;
  unsigned f;
  int rc = 1;
  FILE *out;

  if (!rom || !load_api(core, &api)) goto done;
  if (!boot(&api, rom, &game)) goto done;
  for (f = 0; f < BOOT_FRAMES; ++f) api.run();

  size = api.serialize_size();
  if (!size) { fprintf(stderr, "serialize_size es cero\n"); goto done; }
  state = (uint8_t *)malloc(size);
  if (!state || !api.serialize(state, size)) {
    fprintf(stderr, "no se pudo serializar el checkpoint\n");
    goto done;
  }
  out = fopen(state_path, "wb");
  if (!out) { fprintf(stderr, "no se puede escribir %s\n", state_path); goto done; }
  if (fwrite(state, 1, size, out) != size) {
    fprintf(stderr, "escritura corta de %s\n", state_path);
    fclose(out);
    goto done;
  }
  fclose(out);

  g_audio_hash = g_video_hash = FNV_OFFSET;
  g_audio_energy = g_audio_samples = 0;
  g_capturing = 1;
  for (f = 0; f < CROSS_FRAMES; ++f) api.run();
  g_capturing = 0;
  rc = write_hashes(hash_path) ? 0 : 1;

  api.unload_game();
  api.deinit();
done:
  free(state);
  free(rom);
  return rc;
}

/* --same: el viaje de ida y vuelta DENTRO del mismo proceso, y la referencia
   contra la que se compara --load. Sin esta mitad, un rojo de --load no dice
   cual de dos cosas distintas paso: que falte estado en el savestate (que se
   veria igual cargando en el mismo proceso) o que algo dependa del proceso
   (que es lo que este test persigue). Con las dos, la diferencia entre ellas
   ES la respuesta. */
static int mode_same(const char *core, const char *state_path, const char *hash_path)
{
  struct core_api api;
  struct retro_game_info game;
  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  uint8_t *state = NULL;
  size_t size;
  unsigned f;
  int rc = 1;
  (void)state_path;

  if (!rom || !load_api(core, &api)) goto done;
  if (!boot(&api, rom, &game)) goto done;
  for (f = 0; f < BOOT_FRAMES; ++f) api.run();

  size = api.serialize_size();
  state = size ? (uint8_t *)malloc(size) : NULL;
  if (!state || !api.serialize(state, size)) {
    fprintf(stderr, "no se pudo serializar el checkpoint\n");
    goto done;
  }
  if (!api.unserialize(state, size)) {
    fprintf(stderr, "el core rechazo su propio savestate en el mismo proceso\n");
    goto done;
  }

  g_audio_hash = g_video_hash = FNV_OFFSET;
  g_audio_energy = g_audio_samples = 0;
  g_capturing = 1;
  for (f = 0; f < CROSS_FRAMES; ++f) api.run();
  g_capturing = 0;
  rc = write_hashes(hash_path) ? 0 : 1;

  api.unload_game();
  api.deinit();
done:
  free(state);
  free(rom);
  return rc;
}

/* --load: PROCESO NUEVO. El core arranca de cero, el chip esta reseteado, y el
   blob se copia encima. Es la "sesion nueva" de los issues de upstream. */
static int mode_load(const char *core, const char *state_path, const char *hash_path)
{
  struct core_api api;
  struct retro_game_info game;
  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  uint8_t *state = NULL;
  size_t size, got;
  unsigned f;
  int rc = 1;
  FILE *in;
  long len;

  if (!rom || !load_api(core, &api)) goto done;
  if (!boot(&api, rom, &game)) goto done;

  in = fopen(state_path, "rb");
  if (!in) { fprintf(stderr, "no se puede leer %s\n", state_path); goto done; }
  fseek(in, 0, SEEK_END); len = ftell(in); fseek(in, 0, SEEK_SET);
  if (len <= 0) { fprintf(stderr, "%s vacio\n", state_path); fclose(in); goto done; }
  state = (uint8_t *)malloc((size_t)len);
  got = state ? fread(state, 1, (size_t)len, in) : 0;
  fclose(in);
  if (!state || got != (size_t)len) { fprintf(stderr, "lectura corta\n"); goto done; }

  size = api.serialize_size();
  if (size != got) {
    fprintf(stderr, "el estado mide %lu y el core declara %lu\n",
            (unsigned long)got, (unsigned long)size);
    goto done;
  }
  if (!api.unserialize(state, size)) {
    fprintf(stderr, "el core RECHAZO su propio savestate en un proceso nuevo\n");
    goto done;
  }

  g_audio_hash = g_video_hash = FNV_OFFSET;
  g_audio_energy = g_audio_samples = 0;
  g_capturing = 1;
  for (f = 0; f < CROSS_FRAMES; ++f) api.run();
  g_capturing = 0;
  rc = write_hashes(hash_path) ? 0 : 1;

  api.unload_game();
  api.deinit();
done:
  free(state);
  free(rom);
  return rc;
}

/* Cada ruta va entre comillas: BUILD_DIR y CORE pueden tener espacios, y en
   Windows casi siempre los tienen.
 *
 * Y en Windows va ADEMAS un par de comillas alrededor de TODO. No es
 * paranoia: `system()` llama a `cmd /c <cadena>`, y cmd, cuando la cadena
 * empieza con comilla, saca la PRIMERA y la ULTIMA y se queda con el medio.
 * Con cinco argumentos entrecomillados eso deja una linea partida al medio y
 * cmd termina intentando ejecutar el ultimo argumento:
 *
 *     ".build" no se reconoce como un comando interno o externo
 *
 * El par de mas es lo que cmd espera comerse. Documentado en `cmd /?`, y es
 * por lo que fallaba el job de Windows y no los otros dos. */
static int spawn(const char *self, const char *mode, const char *core,
                 const char *state_path, const char *hash_path)
{
  char cmd[4096];
#if defined(_WIN32)
  const char *wrap = "\"";
#else
  const char *wrap = "";
#endif
  int n = snprintf(cmd, sizeof(cmd), "%s\"%s\" %s \"%s\" \"%s\" \"%s\"%s",
                   wrap, self, mode, core, state_path, hash_path, wrap);
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    fprintf(stderr, "la linea de comando no entra\n");
    return 0;
  }
  if (system(cmd) != 0) {
    fprintf(stderr, "el subproceso %s fallo\n", mode);
    return 0;
  }
  return 1;
}

int main(int argc, char **argv)
{
  char state_path[1024], native_path[1024], loaded_path[1024], same_path[1024];
  unsigned long long native[4], loaded[4], same[4];
  int ok_audio, ok_video, ok_energy, ok_same;

  if (argc == 5 && !strcmp(argv[1], "--save"))
    return mode_save(argv[2], argv[3], argv[4]);
  if (argc == 5 && !strcmp(argv[1], "--same"))
    return mode_same(argv[2], argv[3], argv[4]);
  if (argc == 5 && !strcmp(argv[1], "--load"))
    return mode_load(argv[2], argv[3], argv[4]);
  if (argc != 3) {
    fprintf(stderr,
      "uso: %s <core> <dir-de-trabajo>\n"
      "     %s --save <core> <estado> <hashes>\n"
      "     %s --load <core> <estado> <hashes>\n",
      argv[0], argv[0], argv[0]);
    return 2;
  }

  snprintf(state_path,  sizeof(state_path),  "%s/sms-fm.state", argv[2]);
  snprintf(native_path, sizeof(native_path), "%s/sms-fm.native", argv[2]);
  snprintf(loaded_path, sizeof(loaded_path), "%s/sms-fm.loaded", argv[2]);
  snprintf(same_path,   sizeof(same_path),   "%s/sms-fm.same", argv[2]);

  printf("savestate de Master System con FM Nuked, entre procesos\n");
  printf("  core:   %s\n", argv[1]);
  printf("  %u frames de arranque, %u frames comparados\n\n",
         (unsigned)BOOT_FRAMES, (unsigned)CROSS_FRAMES);

  if (!spawn(argv[0], "--save", argv[1], state_path, native_path)) return 1;
  if (!spawn(argv[0], "--same", argv[1], state_path, same_path)) return 1;
  if (!spawn(argv[0], "--load", argv[1], state_path, loaded_path)) return 1;
  if (!read_hashes(native_path, native) || !read_hashes(loaded_path, loaded) ||
      !read_hashes(same_path, same))
    return 1;

  /* La afirmacion de fondo: restaurar tiene que dar lo mismo que seguir. */
  ok_audio  = loaded[0] == native[0];
  ok_video  = loaded[1] == native[1];
  /* Y la que separa las causas cuando la de arriba falla. */
  ok_same   = loaded[0] == same[0] && loaded[1] == same[1];
  /* Un fixture mudo haria pasar el test sin probar nada: dos silencios son
     iguales. La energia es la que afirma que habia FM que romper. */
  ok_energy = native[2] > 0 && native[3] > 0;

  printf("  continuar normalmente vs restaurar el estado:\n");
  printf("    audio   %016llx vs %016llx  -> %s\n",
         native[0], loaded[0], ok_audio ? "igual" : "DISTINTO");
  printf("    video   %016llx vs %016llx  -> %s\n",
         native[1], loaded[1], ok_video ? "igual" : "DISTINTO");
  printf("  proceso nuevo vs mismo proceso: %s\n",
         ok_same ? "igual" : "DISTINTO");
  printf("  energia del FM en la corrida nativa: %llu en %llu muestras -> %s\n",
         native[2], native[3], ok_energy ? "suena" : "MUDO");

  if (ok_audio && ok_video && ok_same && ok_energy) {
    printf("\nTODO OK\n");
    return 0;
  }
  if (!ok_energy) {
    fprintf(stderr, "\nFALLO: el fixture no produjo audio, el test no probo nada\n");
  } else if (!ok_same) {
    fprintf(stderr, "\nFALLO: cargar en un proceso nuevo no da lo mismo que en el\n"
                    "mismo proceso: hay estado que DEPENDE DEL PROCESO (un puntero\n"
                    "serializado, una direccion movida por el ASLR)\n");
  } else {
    fprintf(stderr, "\nFALLO: restaurar el estado no reproduce la continuacion\n"
                    "nativa. El mismo proceso da el mismo hash, asi que no es cosa\n"
                    "del proceso: FALTA ESTADO en el savestate\n");
  }
  return 1;
}
