/* #37.6: las cinco capas de `recompose_multilayer`, pixel por pixel.
 *
 * La funcion existe desde #12C y hasta hoy nadie habia mirado lo que DEVUELVE.
 * El unico test que la llamaba -- full_core_replay-- pide dos capas y verifica
 * otra cosa: que recomponer no perturbe la emulacion. Que las capas contengan
 * lo que dicen contener no estaba afirmado en ningun lado.
 *
 * Eso convierte cualquier optimizacion del path en un salto al vacio. #37 punto
 * 6 propone derivar las cinco salidas de UN render en vez de cinco, y la unica
 * forma honesta de intentar ese cambio es tener antes un ancla que diga "estos
 * son los bytes": si el hash se mueve, el refactor cambio la imagen, que es
 * precisamente lo que no puede hacer.
 *
 * Dos fixtures, porque uno solo no alcanza:
 *
 *   sh     -- planos A y B con el MISMO contenido, mas un sprite normal y un
 *             OPERADOR de brillo. Sirve para el caso de S/H, pero justamente
 *             porque A y B son identicos no distingue una capa de la otra.
 *   window -- la escena #35 "window": A y B con patterns distintos y un plano
 *             Window en las columnas de la derecha. Aca las cinco capas dan
 *             hashes distintos, asi que confundir dos se nota.
 *
 * Ademas de los hashes hay dos afirmaciones que no dependen de ningun golden:
 *
 *   - Pedir una capa sola tiene que dar lo MISMO que pedirla junto con las
 *     otras cuatro. Un frontend que dibuja el inspector capa por capa y otro
 *     que las pide de una no pueden ver imagenes distintas.
 *   - Donde la atribucion dice "gano el plano X" y ningun sprite tapa el pixel,
 *     la capa X y el composite tienen que tener el mismo color. La unica
 *     excepcion es el OPERADOR de brillo, que cambia la intensidad sin poner
 *     color y por eso no lleva el bit de sprite: el fixture sh tiene
 *     exactamente uno, de 8x8, en una posicion conocida.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libretro.h>
#include "ayther_api.h"
#include "ayther_metrics.h"
#include "generated_rom.h"

/* #37.6: los contadores existen solo en un build con -DAYTHER_METRICS. El
   test no los exige: si el core no los exporta, se saltea esa afirmacion.
   Cuando SI estan, la afirmacion es exacta y no aproximada -- ver abajo. */
#ifndef AYTHER_METRICS
typedef struct ayther_metrics_v1
{
  uint32_t struct_size;
  uint32_t reserved0;
  uint64_t vram_dirty_marks;
  uint64_t begin_frame_calls;
  uint64_t satb_slow_path;
  uint64_t frame_delta_bytes;
  uint64_t bg_b_skipped;
} ayther_metrics_v1;
#endif
typedef int32_t (*metrics_read_fn)(ayther_metrics_v1 *, uint32_t);
typedef void (*metrics_reset_fn)(void);

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

#define ROM_SIZE   AYTHER_GENERATED_ROM_SIZE
#define FRAMES     8
#define MAX_PX     (320 * 240)
#define NLAYERS    5
#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME  UINT64_C(1099511628211)

static const char *const layer_name[NLAYERS] =
{ "bg_a", "bg_b", "window", "sprites", "composite" };

/* `operator_px` es cuantos pixeles puede cambiar el operador de S/H entre la
   capa sola y el composite: en el fixture sh son los 64 del sprite operador, y
   en una escena sin operadores tiene que ser cero. */
struct fixture
{
  const char *name;
  int scene;          /* -1 = el ROM de S/H; >=0 = indice de escena #35 */
  int operator_px;
};
static const struct fixture fixtures[] =
{
  { "sh",     -1, 64 },
  { "window",  2,  0 }   /* ayther_scenes[2] == "window" */
};
#define NFIX ((int)(sizeof(fixtures) / sizeof(fixtures[0])))

struct fixture_result
{
  uint64_t hash[NLAYERS];
  uint32_t w, h;
};

static struct retro_game_info_ext gi_ext;
static uint16_t layer[NLAYERS][MAX_PX];
static uint16_t solo[MAX_PX];
static uint8_t  attrib[MAX_PX];

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
static void vid_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

static uint64_t hash_px(const uint16_t *px, size_t n)
{
  uint64_t h = FNV_OFFSET;
  size_t i;
  for (i = 0; i < n; ++i) {
    h ^= (uint64_t)(px[i] & 0xFFu);        h *= FNV_PRIME;
    h ^= (uint64_t)((px[i] >> 8) & 0xFFu); h *= FNV_PRIME;
  }
  return h;
}

/* 1 = corrio y las afirmaciones pasaron; 0 = fallaron; -1 = el core no puede
   contestar (sin la capability, o modo no soportado) y el test se saltea. */
static int run_fixture(const char *dll, const struct fixture *fx,
                       struct fixture_result *out)
{
  static uint8_t rom[ROM_SIZE];
  library_t lib = open_library(dll);
  struct retro_game_info gi;
  const ayther_interface_v1 *api;
  ayther_recompose_multilayer_v1_fn multi;
  uint32_t w = 0, h = 0;
  int f, i, fail = 0;

  if (!lib) { fprintf(stderr, "no carga %s\n", dll); return -1; }

  {
    void (*p_set_env)(retro_environment_t)        = load_symbol(lib, "retro_set_environment");
    void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(lib, "retro_set_video_refresh");
    void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(lib, "retro_set_audio_sample_batch");
    void (*p_set_poll)(retro_input_poll_t)        = load_symbol(lib, "retro_set_input_poll");
    void (*p_set_inp)(retro_input_state_t)        = load_symbol(lib, "retro_set_input_state");
    void (*p_init)(void)                          = load_symbol(lib, "retro_init");
    bool (*p_load)(const struct retro_game_info *)= load_symbol(lib, "retro_load_game");
    void (*p_run)(void)                           = load_symbol(lib, "retro_run");
    ayther_get_interface_fn p_iface =
      (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
    if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
      fprintf(stderr, "faltan simbolos\n"); close_library(lib); return -1;
    }

    p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
    p_set_poll(poll_cb); p_set_inp(input_cb);
    p_init();

    if (fx->scene < 0) {
      if (!ayther_build_generated_rom_sh(rom, ROM_SIZE)) {
        fprintf(stderr, "no se pudo construir el ROM\n"); close_library(lib); return -1;
      }
    } else {
      if (!ayther_build_generated_rom_scene(rom, ROM_SIZE, (size_t)fx->scene)) {
        fprintf(stderr, "no se pudo construir la escena %d\n", fx->scene);
        close_library(lib); return -1;
      }
    }
    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = "ayther-ml-v1.md"; gi_ext.dir = "."; gi_ext.name = "ayther-ml-v1";
    gi_ext.ext = "md"; gi_ext.data = rom; gi_ext.size = ROM_SIZE;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = "ayther-ml-v1.md"; gi.data = rom; gi.size = ROM_SIZE;
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return -1; }

    api = p_iface(0);
    if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return -1; }
    if (!AYTHER_IFACE_HAS(api, recompose_multilayer) || !api->recompose_multilayer) {
      printf("multilayer: SALTEADO (el core no expone recompose_multilayer)\n");
      close_library(lib); return -1;
    }
    multi = api->recompose_multilayer;

    api->set_subscriptions(AYTHER_SUB_RECOMPOSITION | AYTHER_SUB_RASTER_TRACKING |
                           AYTHER_SUB_ATTRIBUTION);
    for (f = 0; f < FRAMES; f++) p_run();

    /* --- las cinco de una sola vez ------------------------------------- */
    {
      metrics_read_fn  m_read  = (metrics_read_fn)load_symbol(lib, "ayther_metrics_read");
      metrics_reset_fn m_reset = (metrics_reset_fn)load_symbol(lib, "ayther_metrics_reset");
      ayther_metrics_v1 m;
      int32_t st;
      if (m_read && m_reset) m_reset();
      st = multi(layer[0], layer[1], layer[2], layer[3], layer[4],
                 MAX_PX, 0u, &w, &h);
      if (st != AYTHER_STATUS_OK) {
        printf("[%s] la recomposicion fallo (%d)\n", fx->name, (int)st);
        close_library(lib);
        return (st == AYTHER_STATUS_UNSUPPORTED_MODE) ? -1 : 0;
      }
      if (!w || !h || (size_t)w * h > MAX_PX) {
        fprintf(stderr, "dimensiones invalidas %ux%u\n", w, h);
        close_library(lib); return -1;
      }
      /* De las cinco pasadas, TRES ocultan el plano B: bg_a, window y
         sprites. Antes de #37.6 esas tres lo dibujaban igual y lo borraban
         antes del merge. El contador tiene que dar exactamente 3 lineas por
         cada linea de la pantalla: ni menos -- el salteo no se aplico-- ni
         mas -- se saltearon pasadas que necesitaban B. */
      if (m_read && m_reset)
      {
        memset(&m, 0, sizeof(m));
        m.struct_size = (uint32_t)sizeof(m);
        if (m_read(&m, (uint32_t)sizeof(m)) == 0)
        {
          printf("  plano B salteado: %llu lineas (3 x %u = %u)\n",
                 (unsigned long long)m.bg_b_skipped, h, 3u * h);
          if (m.bg_b_skipped != (uint64_t)3u * h) {
            printf("  FALLA: el salteo del plano B no cubre las tres pasadas\n");
            fail = 1;
          }
        }
      }
    }
    printf("[%s] %ux%u\n", fx->name, w, h);
    out->w = w; out->h = h;
    for (i = 0; i < NLAYERS; ++i)
      out->hash[i] = hash_px(layer[i], (size_t)w * h);

    /* --- pedir una capa sola da lo mismo que pedir las cinco ------------ */
    for (i = 0; i < NLAYERS; ++i)
    {
      uint16_t *outs[NLAYERS] = {0, 0, 0, 0, 0};
      uint32_t sw = 0, sh = 0;
      int32_t st;
      outs[i] = solo;
      st = multi(outs[0], outs[1], outs[2], outs[3], outs[4], MAX_PX, 0u, &sw, &sh);
      if (st != AYTHER_STATUS_OK || sw != w || sh != h) {
        printf("  FALLA: pedir solo %s dio status %d y %ux%u\n",
               layer_name[i], (int)st, sw, sh);
        fail = 1;
        continue;
      }
      if (memcmp(solo, layer[i], (size_t)w * h * sizeof(uint16_t)) != 0) {
        printf("  FALLA: %s cambia segun se pida sola o junto con las otras\n",
               layer_name[i]);
        fail = 1;
      }
    }

    /* --- la atribucion y el composite tienen que estar de acuerdo -------- */
    {
      ayther_region_info_v1 info;
      memset(&info, 0, sizeof(info));
      if (api->query_region(AYTHER_REGION_ATTRIBUTION, &info, sizeof(info))
            == AYTHER_STATUS_OK && info.byte_size &&
          info.byte_size <= sizeof(attrib) &&
          api->read_region(AYTHER_REGION_ATTRIBUTION, 0, attrib, info.byte_size,
                           AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OK)
      {
        size_t n = info.byte_size < (size_t)w * h ? info.byte_size : (size_t)w * h;
        size_t k, checked = 0, bad = 0, bad_outside = 0;
        for (k = 0; k < n; ++k)
        {
          unsigned lay = (attrib[k] & AYTHER_ATTRIB_LAYER_MASK) >>
                          AYTHER_ATTRIB_LAYER_SHIFT;
          const uint16_t *src;
          if (attrib[k] & AYTHER_ATTRIB_SPRITE) continue;  /* lo tapa un sprite */
          if (lay == AYTHER_ATTRIB_LAYER_PLANE_A)      src = layer[0];
          else if (lay == AYTHER_ATTRIB_LAYER_PLANE_B) src = layer[1];
          else if (lay == AYTHER_ATTRIB_LAYER_WINDOW)  src = layer[2];
          else continue;                                  /* backdrop */
          ++checked;
          if (src[k] != layer[4][k])
          {
            unsigned px = (unsigned)(k % w), py = (unsigned)(k / w);
            ++bad;
            if (px < AYTHER_SH_OPERATOR_X || px >= AYTHER_SH_OPERATOR_X + 8u ||
                py < AYTHER_SH_OPERATOR_Y || py >= AYTHER_SH_OPERATOR_Y + 8u)
              ++bad_outside;
          }
        }
        printf("  atribucion vs composite: %lu verificados, %lu distintos "
               "(%lu fuera del operador)\n",
               (unsigned long)checked, (unsigned long)bad,
               (unsigned long)bad_outside);
        if (!checked) {
          printf("  FALLA: no se verifico un solo pixel, el chequeo quedo vacio\n");
          fail = 1;
        }
        if (bad_outside) {
          printf("  FALLA: si la capa X gano el pixel y ningun sprite lo tapa, "
                 "la capa X y el composite tienen que coincidir\n");
          fail = 1;
        }
        if ((int)bad != fx->operator_px) {
          printf("  FALLA: se esperaban %d pixeles cambiados por el operador de "
                 "S/H y hubo %lu\n", fx->operator_px, (unsigned long)bad);
          fail = 1;
        }
      }
      else
      {
        printf("  FALLA: la atribucion no se pudo leer\n");
        fail = 1;
      }
    }
  }

  close_library(lib);
  return fail ? 0 : 1;
}

int main(int argc, char **argv)
{
  struct fixture_result res[NFIX];
  const char *golden_path;
  int regen, i, k, fail = 0;
  FILE *fp;

  if (argc < 3) {
    fprintf(stderr, "uso: %s <core> <golden> [--regen]\n", argv[0]);
    return 2;
  }
  golden_path = argv[2];
  regen = (argc > 3 && strcmp(argv[3], "--regen") == 0);

  memset(res, 0, sizeof(res));
  for (i = 0; i < NFIX; ++i)
  {
    int r = run_fixture(argv[1], &fixtures[i], &res[i]);
    if (r < 0) return 0;      /* el core no puede contestar: salteado */
    if (r == 0) fail = 1;
  }

  if (regen)
  {
    /* "wb": en Windows el modo texto escribe CRLF y el arbol de tests esta
       normalizado a LF. */
    fp = fopen(golden_path, "wb");
    if (!fp) { fprintf(stderr, "no se puede escribir %s\n", golden_path); return 2; }
    fprintf(fp, "# #37.6: hashes FNV-1a de las cinco capas de recompose_multilayer,\n");
    fprintf(fp, "# %d frames, flags=0. Regenerar con: make regen-multilayer CORE=<core>\n",
            FRAMES);
    for (i = 0; i < NFIX; ++i)
    {
      fprintf(fp, "fixture %s %u %u\n", fixtures[i].name, res[i].w, res[i].h);
      for (k = 0; k < NLAYERS; ++k)
        fprintf(fp, "  %-10s %016llx\n", layer_name[k],
                (unsigned long long)res[i].hash[k]);
    }
    fclose(fp);
    printf("golden regenerado: %s\n", golden_path);
    return fail;
  }

  fp = fopen(golden_path, "rb");
  if (!fp) {
    printf("no existe %s -- corre `make regen-multilayer CORE=<core>` una vez\n",
           golden_path);
    return 2;
  }
  {
    char line[256];
    int fix = -1, seen = 0;
    while (fgets(line, sizeof(line), fp))
    {
      char name[32];
      unsigned long long want;
      unsigned gw, gh;
      if (line[0] == '#') continue;
      if (sscanf(line, "fixture %31s %u %u", name, &gw, &gh) == 3)
      {
        fix = -1;
        for (i = 0; i < NFIX; ++i)
          if (strcmp(name, fixtures[i].name) == 0) fix = i;
        if (fix < 0) {
          printf("  FALLA: el golden tiene un fixture desconocido: %s\n", name);
          fail = 1;
        } else if (gw != res[fix].w || gh != res[fix].h) {
          printf("  FALLA: [%s] el golden es de %ux%u y la escena da %ux%u\n",
                 name, gw, gh, res[fix].w, res[fix].h);
          fail = 1;
        }
        continue;
      }
      if (fix < 0) continue;
      if (sscanf(line, " %31s %llx", name, &want) != 2) continue;
      for (k = 0; k < NLAYERS; ++k)
        if (strcmp(name, layer_name[k]) == 0)
        {
          ++seen;
          printf("[%s] %-10s %016llx", fixtures[fix].name, layer_name[k],
                 (unsigned long long)res[fix].hash[k]);
          if (res[fix].hash[k] != want) {
            printf("  FALLA (golden %016llx)\n", want);
            fail = 1;
          } else printf("  OK\n");
        }
    }
    fclose(fp);
    if (seen != NLAYERS * NFIX) {
      printf("  FALLA: el golden trae %d hashes de %d\n", seen, NLAYERS * NFIX);
      fail = 1;
    }
  }

  printf("multilayer: %s\n", fail ? "FALLA" : "OK");
  return fail;
}
