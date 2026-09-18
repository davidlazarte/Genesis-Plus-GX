/* #121: compatibilidad del bloque ATIM (#118), medida en tres casos.
 *
 * #118 agrego al savestate un bloque a offset fijo con la fase del refresh
 * del bus y la deteccion de polling de los 68000, y un camino para los
 * estados que no lo traen (la fase se recompone desde cycles). Nada probaba
 * explicitamente ni el camino nuevo ni el viejo. Este harness afirma:
 *
 *   A. estado ACTUAL: serializar, seguir K frames, restaurar, repetir: el
 *      video, el audio y el estado serializado de cada frame son IGUALES.
 *      Continuidad exacta, que es lo que #118 vino a dar.
 *   B. estado SIN ATIM (el bloque a cero, que es como se ve uno de r2 o
 *      anterior): se ACEPTA, corre K frames, y cargarlo dos veces da la misma
 *      continuacion. Determinista: la fase ya no se hereda del proceso. Si
 *      coincide o no con la continuacion nativa se informa, no se exige:
 *      la fase recompuesta no es la que tenia la partida al guardar.
 *   C. estado GENERADO POR 1.10-r2: un blob real del binario del release,
 *      guardado en el repo en formato disperso con su procedencia. Se acepta,
 *      corre K frames, y su continuacion es IDENTICA a la del caso B: es el
 *      mismo estado emulado con el bloque ausente, y si dejara de serlo, algo
 *      cambio en la emulacion o en la carga de estados viejos.
 *
 * El ROM es el sintetico de FM con el 68000 corriendo libre: con `stop` el
 * CPU termina cada frame donde lo despierta la interrupcion y la fase del
 * refresh no se ve (ver generated_rom.c, #118).
 *
 * Uso:
 *   state_atim_compat <core> [fixture-r2.sparse]
 *   state_atim_compat --dump-sparse <core> <salida.sparse>
 *       arranca el fixture, corre BOOT_FRAMES, serializa y escribe el blob en
 *       formato disperso. Es como se genero el fixture de r2: con el binario
 *       de ayther-abi-1.10-r2, no con este.
 *
 * El fixture ayther/golden/state-md-fm-busy-1.10-r2.sparse se genero con
 * genesis_plus_gx_libretro_ayther_x64.dll del release ayther-abi-1.10-r2
 * (core 9e8b6bc6, SHA-256 0e118623132ed8ec545f4feb6bae815cb6f07d7df51e640152ddd49e577f1eb5),
 * en Windows x64, con este mismo harness en modo --dump-sparse.
 *
 * Formato disperso ("AYSP"): u32 magic, u32 tamanio total, y despues chunks
 * (u32 offset, u32 largo, bytes) de las zonas no nulas. El blob de 1 MiB del
 * fixture (RAM y VRAM casi vacias a los 60 frames) entra en 5 KB.
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

#define ROM_SIZE     AYTHER_GENERATED_ROM_SIZE
#define BOOT_FRAMES  60u   /* igual en --dump-sparse y en el test: el fixture de r2 es este frame */
#define CONT_FRAMES  60u
#define FNV_OFFSET   UINT64_C(0xcbf29ce484222325)
#define FNV_PRIME    UINT64_C(0x100000001B3)

/* Las constantes de libretro.c, por posicion desde el final del blob. */
#define TAG_BYTES    16u
#define AINC_BYTES   1024u
#define ATIM_BYTES   64u
#define AYSS_MAGIC   UINT32_C(0x53535941)
#define AINC_MAGIC   UINT32_C(0x434e4941)
#define ATIM_MAGIC   UINT32_C(0x4d495441)
#define AYSP_MAGIC   UINT32_C(0x50535941)

static struct retro_game_info_ext gi_ext;
static uint64_t g_video_hash, g_audio_hash, g_audio_energy;

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
  const uint8_t *p = (const uint8_t *)data;
  size_t i;
  for (i = 0; i < n; ++i) { h ^= p[i]; h *= FNV_PRIME; }
  return h;
}

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return data && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: if (data) *(bool *)data = true; return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: if (data) *(bool *)data = false; return data != NULL;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: if (data) *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return data != NULL;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: if (data) *(int *)data = 3; return data != NULL;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      if (data) *(const struct retro_game_info_ext **)data = &gi_ext; return data != NULL;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: if (data) *(const char **)data = "."; return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE: return false;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}

static void vid_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
  const uint8_t *p = (const uint8_t *)data;
  unsigned y;
  if (!p) return;
  for (y = 0; y < h; ++y)
    g_video_hash = fnv(g_video_hash, p + (size_t)y * pitch, (size_t)w * 2u);
}
static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  g_audio_hash = fnv(g_audio_hash, d, frames * 2u * sizeof(int16_t));
  for (i = 0; i < frames * 2u; ++i) g_audio_energy += (uint64_t)(d[i] < 0 ? -d[i] : d[i]);
  return frames;
}
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

struct core_api {
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
  library_t lib = open_library(path);
  memset(api, 0, sizeof(*api));
  if (!lib) { fprintf(stderr, "no carga el core: %s\n", path); return 0; }
#define BIND(field, name) \
  do { *(void **)&api->field = load_symbol(lib, name); \
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

static int boot(struct core_api *api, uint8_t *rom)
{
  struct retro_game_info game;
  unsigned f;
  if (!ayther_build_generated_rom_fm_busy(rom, ROM_SIZE)) {
    fprintf(stderr, "no se pudo construir el ROM de FM con el 68000 libre\n");
    return 0;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path = "ayther-fm-busy.md"; gi_ext.dir = ".";
  gi_ext.name = "ayther-fm-busy"; gi_ext.ext = "md";
  gi_ext.data = rom; gi_ext.size = ROM_SIZE; gi_ext.persistent_data = true;
  memset(&game, 0, sizeof(game));
  game.path = "ayther-fm-busy.md"; game.data = rom; game.size = ROM_SIZE;
  api->set_environment(env_cb);
  api->set_video_refresh(vid_cb);
  api->set_audio_sample_batch(aud_cb);
  api->set_input_poll(poll_cb);
  api->set_input_state(input_cb);
  api->init();
  if (!api->load_game(&game)) { fprintf(stderr, "el core rechazo el ROM\n"); return 0; }
  for (f = 0; f < BOOT_FRAMES; ++f) api->run();
  return 1;
}

/* --- formato disperso ------------------------------------------------------ */

static int put_u32(FILE *f, uint32_t v) { return fwrite(&v, 1, 4, f) == 4; }
static int get_u32(FILE *f, uint32_t *v) { return fread(v, 1, 4, f) == 4; }

static int write_sparse(const char *path, const uint8_t *blob, size_t n)
{
  FILE *f = fopen(path, "wb");
  size_t i = 0;
  if (!f) { fprintf(stderr, "no se puede escribir %s\n", path); return 0; }
  if (!put_u32(f, AYSP_MAGIC) || !put_u32(f, (uint32_t)n)) goto bad;
  while (i < n) {
    size_t start, end, k;
    if (blob[i] == 0) { ++i; continue; }
    start = i;
    end = i;
    /* un hueco de menos de 64 ceros no corta el chunk: menos cabeceras */
    for (k = i; k < n; ++k) {
      if (blob[k]) end = k + 1;
      else if (k - end >= 64) break;
    }
    if (!put_u32(f, (uint32_t)start) || !put_u32(f, (uint32_t)(end - start)) ||
        fwrite(blob + start, 1, end - start, f) != end - start)
      goto bad;
    i = end;
  }
  return fclose(f) == 0;
bad:
  fclose(f);
  fprintf(stderr, "escritura corta de %s\n", path);
  return 0;
}

static uint8_t *read_sparse(const char *path, size_t *n_out)
{
  FILE *f = fopen(path, "rb");
  uint32_t magic, n, off, len;
  uint8_t *blob = NULL;
  if (!f) { fprintf(stderr, "no se puede leer %s\n", path); return NULL; }
  if (!get_u32(f, &magic) || magic != AYSP_MAGIC || !get_u32(f, &n) || !n) {
    fprintf(stderr, "%s no es un blob disperso AYSP\n", path); fclose(f); return NULL;
  }
  blob = (uint8_t *)calloc(1, n);
  if (!blob) { fclose(f); return NULL; }
  while (get_u32(f, &off)) {
    if (!get_u32(f, &len) || off > n || len > n - off || fread(blob + off, 1, len, f) != len) {
      fprintf(stderr, "%s: chunk corrupto en %u\n", path, off);
      free(blob); fclose(f); return NULL;
    }
  }
  fclose(f);
  *n_out = n;
  return blob;
}

static uint32_t magic_at(const uint8_t *blob, size_t n, size_t from_end)
{
  uint32_t v;
  memcpy(&v, blob + n - from_end, 4);
  return v;
}

/* --- el test ------------------------------------------------------------------ */

struct cont {
  uint64_t video[CONT_FRAMES];
  uint64_t audio[CONT_FRAMES];
  uint64_t state[CONT_FRAMES];
  uint64_t energy;
};

static int run_continuation(struct core_api *api, uint8_t *scratch, size_t n, struct cont *c)
{
  unsigned f;
  c->energy = 0;
  for (f = 0; f < CONT_FRAMES; ++f) {
    g_video_hash = g_audio_hash = FNV_OFFSET;
    g_audio_energy = 0;
    api->run();
    if (!api->serialize(scratch, n)) { fprintf(stderr, "serialize fallo en el frame %u\n", f); return 0; }
    c->video[f] = g_video_hash;
    c->audio[f] = g_audio_hash;
    c->state[f] = fnv(FNV_OFFSET, scratch, n);
    c->energy += g_audio_energy;
  }
  return 1;
}

/* -1 si son iguales; si no, el primer frame distinto. */
static int first_diff(const struct cont *a, const struct cont *b, const char **what)
{
  unsigned f;
  for (f = 0; f < CONT_FRAMES; ++f) {
    if (a->video[f] != b->video[f]) { *what = "video"; return (int)f; }
    if (a->audio[f] != b->audio[f]) { *what = "audio"; return (int)f; }
    if (a->state[f] != b->state[f]) { *what = "estado"; return (int)f; }
  }
  return -1;
}

static int mode_dump(const char *core, const char *out)
{
  struct core_api api;
  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  uint8_t *blob;
  size_t n;
  if (!rom || !load_api(core, &api) || !boot(&api, rom)) return 2;
  n = api.serialize_size();
  blob = (uint8_t *)calloc(1, n);
  if (!blob || !api.serialize(blob, n)) { fprintf(stderr, "serialize fallo\n"); return 2; }
  if (!write_sparse(out, blob, n)) return 2;
  printf("blob de %lu bytes escrito disperso en %s (ATIM %s)\n", (unsigned long)n, out,
         magic_at(blob, n, TAG_BYTES + AINC_BYTES + ATIM_BYTES) == ATIM_MAGIC ? "presente" : "ausente");
  return 0;
}

int main(int argc, char **argv)
{
  struct core_api api;
  uint8_t *rom, *S, *S_noatim, *scratch, *R2 = NULL;
  size_t n, n2 = 0;
  struct cont N, A, B1, B2, C;
  const char *what;
  int d, fail = 0;
  const char *core, *fixture = NULL;

  if (argc == 4 && !strcmp(argv[1], "--dump-sparse")) return mode_dump(argv[2], argv[3]);
  if (argc < 2) { fprintf(stderr, "uso: %s <core> [fixture-r2.sparse] | --dump-sparse <core> <salida>\n", argv[0]); return 2; }
  core = argv[1];
  if (argc > 2) fixture = argv[2];

  rom = (uint8_t *)malloc(ROM_SIZE);
  if (!rom || !load_api(core, &api) || !boot(&api, rom)) return 2;
  n = api.serialize_size();
  S = (uint8_t *)calloc(1, n);
  S_noatim = (uint8_t *)calloc(1, n);
  scratch = (uint8_t *)calloc(1, n);
  if (!S || !S_noatim || !scratch || !api.serialize(S, n)) { fprintf(stderr, "serialize fallo\n"); return 2; }

  printf("blob: %lu bytes; tag AYSS %s, AINC %s, ATIM %s\n", (unsigned long)n,
         magic_at(S, n, TAG_BYTES) == AYSS_MAGIC ? "ok" : "AUSENTE",
         magic_at(S, n, TAG_BYTES + AINC_BYTES) == AINC_MAGIC ? "ok" : "AUSENTE",
         magic_at(S, n, TAG_BYTES + AINC_BYTES + ATIM_BYTES) == ATIM_MAGIC ? "ok" : "AUSENTE");
  if (magic_at(S, n, TAG_BYTES + AINC_BYTES + ATIM_BYTES) != ATIM_MAGIC) {
    printf("   este core no escribe el bloque ATIM: no hay nada que probar\n");
    fail = 1;
  }

  /* la continuacion nativa: desde S sin cargar nada */
  if (!run_continuation(&api, scratch, n, &N)) return 2;
  if (N.energy == 0) { printf("   el fixture no suena: el audio no probaria nada\n"); fail = 1; }

  /* A. estado actual: continuidad exacta */
  if (!api.unserialize(S, n)) { printf("A. estado actual: RECHAZADO (MAL)\n"); fail = 1; }
  else if (!run_continuation(&api, scratch, n, &A)) return 2;
  else {
    d = first_diff(&N, &A, &what);
    printf("A. estado actual: aceptado; continuacion %s\n",
           d < 0 ? "IDENTICA a la nativa en video, audio y estado (correcto)" : "DISTINTA (MAL)");
    if (d >= 0) { printf("   primer frame distinto: %d (%s)\n", d, what); fail = 1; }
  }

  /* B. estado sin ATIM: acepta, corre, determinista */
  memcpy(S_noatim, S, n);
  memset(S_noatim + n - TAG_BYTES - AINC_BYTES - ATIM_BYTES, 0, ATIM_BYTES);
  if (!api.unserialize(S_noatim, n)) { printf("B. estado sin ATIM: RECHAZADO (MAL: rompe los estados de r2)\n"); fail = 1; }
  else if (!run_continuation(&api, scratch, n, &B1)) return 2;
  else if (!api.unserialize(S_noatim, n)) { printf("B. estado sin ATIM: RECHAZADO la segunda vez (MAL)\n"); fail = 1; }
  else if (!run_continuation(&api, scratch, n, &B2)) return 2;
  else {
    d = first_diff(&B1, &B2, &what);
    printf("B. estado sin ATIM: aceptado; dos cargas dan continuacion %s\n",
           d < 0 ? "IDENTICA entre si (determinista, correcto)" : "DISTINTA entre si (MAL: la fase se hereda del proceso)");
    if (d >= 0) { printf("   primer frame distinto: %d (%s)\n", d, what); fail = 1; }
    /* Informativo. El estado serializado difiere siempre (el bloque ATIM
       recompuesto no es el original); lo que interesa documentar es cuando
       se VE y se OYE la diferencia. */
    {
      int dv = -1, da = -1;
      unsigned f;
      for (f = 0; f < CONT_FRAMES && dv < 0; ++f) if (N.video[f] != B1.video[f]) dv = (int)f;
      for (f = 0; f < CONT_FRAMES && da < 0; ++f) if (N.audio[f] != B1.audio[f]) da = (int)f;
      printf("   respecto de la nativa (informativo, es el limite de los estados sin bloque):\n"
             "   video %s, audio %s\n",
             dv < 0 ? "identico en los 60 frames" : "distinto",
             da < 0 ? "identico en los 60 frames" : "distinto");
      if (dv >= 0) printf("   primer frame de video distinto: %d\n", dv);
      if (da >= 0) printf("   primer frame de audio distinto: %d\n", da);
    }
  }

  /* C. estado real de 1.10-r2 */
  if (!fixture) {
    printf("C. estado de 1.10-r2: sin fixture (pasar la ruta del .sparse)\n");
  } else if (!(R2 = read_sparse(fixture, &n2))) {
    fail = 1;
  } else if (n2 != n) {
    printf("C. estado de 1.10-r2: mide %lu y el core declara %lu (MAL)\n", (unsigned long)n2, (unsigned long)n);
    fail = 1;
  } else {
    printf("C. estado de 1.10-r2: tag AYSS %s, AINC %s, ATIM %s (tiene que estar ausente)\n",
           magic_at(R2, n, TAG_BYTES) == AYSS_MAGIC ? "ok" : "AUSENTE",
           magic_at(R2, n, TAG_BYTES + AINC_BYTES) == AINC_MAGIC ? "ok" : "AUSENTE",
           magic_at(R2, n, TAG_BYTES + AINC_BYTES + ATIM_BYTES) == ATIM_MAGIC ? "PRESENTE (MAL)" : "ausente");
    if (magic_at(R2, n, TAG_BYTES + AINC_BYTES + ATIM_BYTES) == ATIM_MAGIC) fail = 1;
    if (!api.unserialize(R2, n)) { printf("   RECHAZADO (MAL: un estado del release anterior no carga)\n"); fail = 1; }
    else if (!run_continuation(&api, scratch, n, &C)) return 2;
    else {
      d = first_diff(&B1, &C, &what);
      printf("   aceptado; continuacion %s\n",
             d < 0 ? "IDENTICA a la del estado sin ATIM (correcto: mismo estado emulado, sin bloque)"
                   : "DISTINTA de la del estado sin ATIM (MAL)");
      if (d >= 0) { printf("   primer frame distinto: %d (%s)\n", d, what); fail = 1; }
      if (memcmp(R2, S_noatim, n - TAG_BYTES - AINC_BYTES - ATIM_BYTES) == 0)
        printf("   y el blob de r2 es byte a byte el de este core hasta el bloque ATIM\n");
      else
        printf("   nota: el blob de r2 difiere del de este core antes del bloque ATIM (la emulacion cambio\n"
               "   entre r2 y este commit; la continuacion igual coincide)\n");
    }
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  api.unload_game();
  api.deinit();
  free(rom); free(S); free(S_noatim); free(scratch); free(R2);
  return fail;
}
