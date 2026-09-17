/* #76: que el savestate este COMPLETO, medido en vez de razonado.
 *
 * El oraculo es mecanico y no necesita saber nada del hardware: se guarda un
 * checkpoint en el frame N, se corren M frames y se anota lo que salio; despues
 * se vuelve a cargar el MISMO checkpoint, se corren los MISMOS M frames y se
 * compara. Si algo del estado no viaja en el blob, las dos corridas divergen y
 * la divergencia ES el campo que falta. No hay golden: el oraculo es la corrida
 * de al lado.
 *
 * Se comparan tres cosas, y las tres hacen falta:
 *
 *   video   lo que se ve. Se le escapa todo lo que todavia no llego a pantalla.
 *   audio   lo que suena. Atrapa el camino de sonido, que es donde estaba #93.
 *   estado  se vuelve a serializar al final y se hashea el blob entero. Esta es
 *           la que atrapa lo que no se ve ni se oye TODAVIA: un contador de
 *           EEPROM a mitad de una escritura, un latch de puerto, un registro
 *           que recien importa dentro de veinte frames. Sin ella el test diria
 *           "todo bien" hasta que el sintoma aparece lejos de la causa.
 *
 * Y se corre sobre las combinaciones que el fork puede armar con sus fixtures
 * sinteticos: Mega Drive, Master System, Master System con el FM de Nuked,
 * Game Gear, Mega Drive con EEPROM y Sega CD. La consola la elige la EXTENSION del archivo -- el core la
 * mira en loadrom.c-, asi que Game Gear sale del mismo ROM que Master System
 * con otro nombre: lo que cambia es el hardware que el core levanta alrededor,
 * que es justo lo que #76 pone en duda.
 *
 * HASTA DONDE LLEGA EL ORACULO. El hash del estado no puede ver lo que NO esta
 * en el estado: si un campo no se serializa, no aparece en el blob de ninguna de
 * las dos corridas y las dos dan el mismo hash. Lo que delata a un campo que
 * falta es el VIDEO o el AUDIO -- el efecto de ese campo sobre lo que sale-, y
 * solo si el programa del fixture lo usa para algo observable dentro de la
 * ventana medida. El hash del estado cubre la otra mitad: campos que SI se
 * serializan pero quedan mal, o que todavia no llegaron a verse.
 *
 * De ahi que un fixture que no ejercita un subsistema no diga nada sobre el.
 * El caso que motivo esta advertencia ya esta cubierto: la EEPROM I2C de
 * libretro/Genesis-Plus-GX#404 no se serializaba, y el quinto fixture
 * ("eeprom", generated_rom_eeprom) deja una transaccion I2C a medias en cada
 * borde de frame y usa el byte leido para el scroll horizontal, asi que la
 * perdida se ve en el VIDEO; el fork la serializa detras de STATE_VERSION
 * 1.7.8 (#76, core/state.h). Lo que sigue sin cubrir es lo que ningun fixture
 * toca: los mappers de cartucho con estado propio (SVP, Game Genie, Action
 * Replay, MegaSD) y los perifericos de entrada que no sean el pad.
 *
 * #97: el sexto fixture es un Sega CD entero -- BIOS e imagen sinteticas, ver
 * cd_fixture.h--, que es el sistema donde el savestate tiene MAS que perder:
 * dos 68000, PRG-RAM, Word-RAM, CDC, CDD, PCM y el ASIC grafico, todo en
 * bloques propios del blob que hasta este fixture nadie cargaba en un test.
 *
 * Uso: state_roundtrip <core> [directorio-de-trabajo] [fixture]
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

#define ROM_SIZE     AYTHER_GENERATED_ROM_SIZE
#define BOOT_FRAMES  30u
#define RUN_FRAMES   20u
#define FNV_OFFSET   UINT64_C(0xcbf29ce484222325)
#define FNV_PRIME    UINT64_C(0x100000001B3)

struct fixture
{
  const char *name;
  const char *ext;      /* lo que decide la consola en loadrom.c */
  int         fm;       /* YM2413 de Nuked encendido */
  int         sms_rom;  /* el ROM sintetico de Master System */
  int         eeprom;   /* cartucho de MD con EEPROM I2C serie */
  int         cd;       /* #97: Sega CD, BIOS e imagen sinteticas en disco */
  unsigned    flags;    /* escena, si sms_rom */
};

static const struct fixture FIXTURES[] = {
  { "md",     "md",  0, 0, 0, 0, 0 },
  { "sms",    "sms", 0, 1, 0, 0, 0 },
  { "sms-fm", "sms", 1, 1, 0, 0, AYTHER_SMS_SCENE_FM },
  { "gg",     "gg",  0, 1, 0, 0, 0 },
  { "eeprom", "md",  0, 0, 1, 0, 0 },
  { "scd",    "iso", 0, 0, 0, 1, 0 },
};
#define N_FIXTURES ((int)(sizeof(FIXTURES) / sizeof(FIXTURES[0])))

static const struct fixture *g_fx;
static struct retro_game_info_ext gi_ext;
/* Donde se escriben los archivos del fixture de CD, y donde el core busca la
   BIOS y deja la backup RAM: el BUILD_DIR del Makefile, que ya se limpia. */
static const char *g_workdir = ".";
static uint64_t g_video, g_audio;
static uint64_t g_audio_n;
static int g_capturing;

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
      /* Sin el bit 2: fast_savestates apagado a proposito. Con el encendido la
         continuidad del audio viaja por la memoria del proceso (#93) y este
         test dejaria de mirar el BLOB, que es lo que quiere mirar. */
      if (data) *(int *)data = 3;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
      /* El CD entra por RUTA: cdd_load abre la imagen del disco, no la
         recibe en memoria. Sin info_ext el core usa retro_game_info.path. */
      if (g_fx && g_fx->cd) return false;
      if (data) *(const struct retro_game_info_ext **)data = &gi_ext;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data) *(const char **)data = g_workdir;
      return data != NULL;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *)data;
      if (!v || !v->key) return false;
      v->value = NULL;
      if (!strcmp(v->key, "genesis_plus_gx_ym2413"))
        v->value = (g_fx && g_fx->fm) ? "enabled" : "disabled";
      else if (!strcmp(v->key, "genesis_plus_gx_ym2413_core"))
        v->value = (g_fx && g_fx->fm) ? "nuked" : "mame";
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      return true;
    default:
      return false;
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
      g_video ^= row[x];
      g_video *= FNV_PRIME;
    }
  }
}

static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  if (!g_capturing) return frames;
  for (i = 0; i < frames * 2u; ++i) {
    g_audio ^= (uint64_t)(uint16_t)d[i];
    g_audio *= FNV_PRIME;
    g_audio_n++;
  }
  return frames;
}
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

struct core_api
{
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

static int load_api(library_t lib, struct core_api *api)
{
  memset(api, 0, sizeof(*api));
#define BIND(f, n) do { *(void **)&api->f = load_symbol(lib, n); \
    if (!api->f) { fprintf(stderr, "falta %s\n", n); return 0; } } while (0)
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

static uint64_t hash_bytes(const uint8_t *p, size_t n)
{
  uint64_t h = FNV_OFFSET;
  size_t i;
  for (i = 0; i < n; ++i) { h ^= p[i]; h *= FNV_PRIME; }
  return h;
}

struct outcome { uint64_t video, audio, state; uint64_t samples; };

/* Corre RUN_FRAMES anotando video y audio, y al final vuelve a serializar. */
static int measure(struct core_api *api, uint8_t *scratch, size_t size,
                   struct outcome *out)
{
  unsigned f;
  g_video = g_audio = FNV_OFFSET;
  g_audio_n = 0;
  g_capturing = 1;
  for (f = 0; f < RUN_FRAMES; ++f) api->run();
  g_capturing = 0;
  if (!api->serialize(scratch, size)) {
    fprintf(stderr, "no se pudo serializar al final de la corrida\n");
    return 0;
  }
  out->video = g_video;
  out->audio = g_audio;
  out->samples = g_audio_n;
  out->state = hash_bytes(scratch, size);
  return 1;
}

static int run_fixture(library_t lib, const struct fixture *fx)
{
  struct core_api api;
  struct retro_game_info game;
  uint8_t *rom = (uint8_t *)malloc(ROM_SIZE);
  uint8_t *checkpoint = NULL, *scratch = NULL, *scratch2 = NULL;
  char path[1024];
  size_t size;
  unsigned f;
  struct outcome sin_recarga, con_recarga;
  int bad = 0;

  g_fx = fx;
  if (!rom || !load_api(lib, &api)) { free(rom); return 1; }

  memset(&game, 0, sizeof(game));
  if (fx->cd) {
    /* #97: BIOS e imagen al disco, y el core recibe la ruta de la imagen. */
    if (!ayther_cd_fixture_write(g_workdir, path, sizeof(path))) {
      fprintf(stderr, "%s: no se pudo escribir el fixture de CD en %s\n",
              fx->name, g_workdir);
      free(rom); return 1;
    }
    game.path = path;
  } else {
    if (fx->sms_rom
          ? !ayther_build_generated_rom_sms_scene(rom, ROM_SIZE, fx->flags)
          : (fx->eeprom ? !ayther_build_generated_rom_eeprom(rom, ROM_SIZE)
                        : !ayther_build_generated_rom(rom, ROM_SIZE))) {
      fprintf(stderr, "%s: no se pudo construir el fixture\n", fx->name);
      free(rom); return 1;
    }
    snprintf(path, sizeof(path), "ayther-%s.%s", fx->name, fx->ext);

    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = path; gi_ext.dir = ".";
    gi_ext.name = fx->name; gi_ext.ext = fx->ext;
    gi_ext.data = rom; gi_ext.size = ROM_SIZE;
    gi_ext.persistent_data = true;
    game.path = path; game.data = rom; game.size = ROM_SIZE;
  }

  api.set_environment(env_cb);
  api.set_video_refresh(vid_cb);
  api.set_audio_sample_batch(aud_cb);
  api.set_input_poll(poll_cb);
  api.set_input_state(input_cb);
  api.init();
  if (!api.load_game(&game)) {
    fprintf(stderr, "%s: el core rechazo el fixture\n", fx->name);
    free(rom); return 1;
  }

  for (f = 0; f < BOOT_FRAMES; ++f) api.run();

  /* calloc y no malloc: retro_serialize escribe ~14% de STATE_SIZE y deja el
     resto como estaba, y el hash del estado cubre el blob ENTERO. Con malloc,
     dos buffers reciclados del heap traen colas distintas y el test falla por
     basura que no es del core -- paso en Windows con el fixture eeprom. */
  size = api.serialize_size();
  checkpoint = size ? (uint8_t *)calloc(1, size) : NULL;
  scratch    = size ? (uint8_t *)calloc(1, size) : NULL;
  scratch2   = size ? (uint8_t *)calloc(1, size) : NULL;
  if (!checkpoint || !scratch || !scratch2 || !api.serialize(checkpoint, size)) {
    fprintf(stderr, "%s: no se pudo guardar el checkpoint\n", fx->name);
    bad = 1;
    goto done;
  }

  /* Primera corrida: se sigue de largo desde el checkpoint. */
  if (!measure(&api, scratch, size, &sin_recarga)) { bad = 1; goto done; }

  /* Segunda: se vuelve al MISMO checkpoint y se repite. */
  if (!api.unserialize(checkpoint, size)) {
    fprintf(stderr, "%s: el core rechazo su propio checkpoint\n", fx->name);
    bad = 1;
    goto done;
  }
  if (!measure(&api, scratch2, size, &con_recarga)) { bad = 1; goto done; }

  printf("  %-7s video %016llx %s  audio %016llx %s  estado %016llx %s",
         fx->name,
         (unsigned long long)con_recarga.video,
         sin_recarga.video == con_recarga.video ? "=" : "!",
         (unsigned long long)con_recarga.audio,
         sin_recarga.audio == con_recarga.audio ? "=" : "!",
         (unsigned long long)con_recarga.state,
         sin_recarga.state == con_recarga.state ? "=" : "!");

  if (sin_recarga.video != con_recarga.video ||
      sin_recarga.audio != con_recarga.audio ||
      sin_recarga.state != con_recarga.state) {
    printf("  FALLA\n");
    if (sin_recarga.video != con_recarga.video)
      printf("           video  sin recarga %016llx\n",
             (unsigned long long)sin_recarga.video);
    if (sin_recarga.audio != con_recarga.audio)
      printf("           audio  sin recarga %016llx\n",
             (unsigned long long)sin_recarga.audio);
    if (sin_recarga.state != con_recarga.state) {
      /* Los dos blobs estan a mano: decir DONDE difieren, que es lo que
         convierte "el estado no viaja" en "este campo no viaja". */
      size_t i = 0, shown = 0;
      printf("           estado sin recarga %016llx\n",
             (unsigned long long)sin_recarga.state);
      while (i < size && shown < 8) {
        if (scratch[i] != scratch2[i]) {
          size_t j = i;
          while (j < size && scratch[j] != scratch2[j]) ++j;
          size_t k, w = (j - i < 8u) ? 8u : (j - i);
          printf("           difiere en [%zu, %zu):", i, j);
          printf("  sin recarga");
          for (k = 0; k < w && i + k < size; ++k) printf(" %02x", scratch[i + k]);
          printf("  con recarga");
          for (k = 0; k < w && i + k < size; ++k) printf(" %02x", scratch2[i + k]);
          printf("\n");
          shown++;
          i = j;
        } else {
          ++i;
        }
      }
    }
    bad = 1;
  } else if (sin_recarga.samples == 0) {
    /* Un fixture mudo hace que la comparacion de audio no afirme nada. */
    printf("  (sin audio)\n");
  } else {
    printf("  OK\n");
  }

  api.unload_game();
  api.deinit();
done:
  free(scratch2);
  free(scratch);
  free(checkpoint);
  free(rom);
  return bad;
}

int main(int argc, char **argv)
{
  library_t lib;
  int i, fail = 0, ran = 0;

  if (argc < 2) {
    fprintf(stderr, "uso: %s <core> [directorio-de-trabajo] [fixture]\n", argv[0]);
    return 2;
  }
  lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga el core: %s\n", argv[1]); return 2; }
  if (argc > 2) g_workdir = argv[2];

  printf("roundtrip del savestate: guardar, correr %u frames, recargar y repetir\n",
         (unsigned)RUN_FRAMES);
  printf("  '=' es que la recarga reprodujo la corrida; '!' que no.\n\n");

  for (i = 0; i < N_FIXTURES; ++i) {
    if (argc > 3 && strcmp(argv[3], FIXTURES[i].name) != 0) continue;
    fail |= run_fixture(lib, &FIXTURES[i]);
    ran++;
  }
  if (!ran) { fprintf(stderr, "no existe ese fixture\n"); return 2; }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
