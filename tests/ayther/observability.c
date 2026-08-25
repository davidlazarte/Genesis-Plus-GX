/* #39 A/D/E: journal raster, hashes del frame y paleta resuelta.
 *
 * Las tres regiones existen para lo mismo: cosas que el core YA sabe y que el
 * frontend tenia que volver a deducir por su cuenta, cada una con su copia de
 * las reglas y sin nadie que avise cuando se separan.
 *
 * Cada afirmacion tiene un oraculo que no reimplementa el core:
 *
 *   A (journal)  -- la CANTIDAD de eventos ya se exponia por otro camino
 *                   (`raster_event_count` del frame delta). Los dos numeros
 *                   salen de la misma fuente: si no coinciden, uno de los dos
 *                   la esta leyendo mal. Y un diario no va hacia atras: sus
 *                   lineas tienen que ir en orden.
 *   D (hashes)   -- el criterio de aceptacion del issue: el `video_hash` de la
 *                   region tiene que dar lo MISMO que el hash calculado afuera
 *                   sobre el frame que llega por `video_refresh`. Mismo
 *                   algoritmo, misma semilla, dos implementaciones.
 *   E (paleta)   -- la tabla tiene que EXPLICAR el frame: todo color que
 *                   aparece en la imagen emitida tiene que estar en ella. Una
 *                   tabla vieja, truncada o del formato equivocado rompe esa
 *                   propiedad, y afirmarla no exige reimplementar la conversion
 *                   de 9 bits que la region existe para evitar.
 *
 * Y las tres tienen que contestar NOT_SUBSCRIBED sin suscripcion: una region
 * que devuelve datos sin que nadie los haya pedido cobra trabajo que nadie
 * encargo.
 *
 * DOS fixtures, y la razon es un hallazgo de este test. La paleta es la tabla
 * VIGENTE, no una por linea: en el ROM de siempre el handler de H-int escribe
 * CRAM a mitad de frame, asi que la imagen tiene colores de varias paletas y la
 * tabla final no puede explicarlos a todos. Eso no es un defecto de la region
 * -- para el caso por linea esta LINE_CRAM (#42)-- pero si es una propiedad que
 * hay que medir donde vale: el fixture de S/H no tiene H-int, y ahi la paleta
 * no se mueve durante el frame.
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

#define ROM_SIZE   AYTHER_GENERATED_ROM_SIZE
#define FRAMES     12
#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME  UINT64_C(1099511628211)

static struct retro_game_info_ext gi_ext;
static uint64_t last_video_hash;
static uint16_t frame_px[720 * 480];
static unsigned frame_w, frame_h;

static uint64_t hash_bytes(uint64_t h, const void *p, size_t n)
{
  const uint8_t *b = (const uint8_t *)p;
  size_t i;
  for (i = 0; i < n; ++i) { h ^= (uint64_t)b[i]; h *= FNV_PRIME; }
  return h;
}

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

/* El mismo recorrido que hace el core: fila por fila, `width` pixeles de a dos
   bytes, saltando por `pitch`. Es la definicion del criterio de aceptacion. */
static void vid_cb(const void *d, unsigned w, unsigned h, size_t pitch)
{
  uint64_t hash = FNV_OFFSET;
  unsigned y;
  if (!d || !w || !h) return;
  for (y = 0; y < h; ++y)
    hash = hash_bytes(hash, (const uint8_t *)d + (size_t)y * pitch,
                      (size_t)w * sizeof(uint16_t));
  last_video_hash = hash;
  frame_w = (w > 720) ? 720 : w;
  frame_h = (h > 480) ? 480 : h;
  for (y = 0; y < frame_h; ++y)
    memcpy(&frame_px[(size_t)y * frame_w],
           (const uint8_t *)d + (size_t)y * pitch,
           (size_t)frame_w * sizeof(uint16_t));
}
static size_t aud_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

struct core
{
  library_t lib;
  void (*run)(void);
  const ayther_interface_v1 *api;
};

/* 1 = cargado; 0 = error; -1 = el core no declara la capability. */
static int core_open(struct core *c, const char *dll, int use_sh)
{
  static uint8_t rom[ROM_SIZE];
  struct retro_game_info gi;

  memset(c, 0, sizeof(*c));
  c->lib = open_library(dll);
  if (!c->lib) { fprintf(stderr, "no carga %s\n", dll); return 0; }

  {
    void (*p_set_env)(retro_environment_t)        = load_symbol(c->lib, "retro_set_environment");
    void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(c->lib, "retro_set_video_refresh");
    void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(c->lib, "retro_set_audio_sample_batch");
    void (*p_set_poll)(retro_input_poll_t)        = load_symbol(c->lib, "retro_set_input_poll");
    void (*p_set_inp)(retro_input_state_t)        = load_symbol(c->lib, "retro_set_input_state");
    void (*p_init)(void)                          = load_symbol(c->lib, "retro_init");
    bool (*p_load)(const struct retro_game_info *)= load_symbol(c->lib, "retro_load_game");
    ayther_get_interface_fn p_iface =
      (ayther_get_interface_fn)load_symbol(c->lib, "ayther_get_interface");
    c->run = load_symbol(c->lib, "retro_run");
    if (!p_set_env || !p_init || !p_load || !c->run || !p_iface) {
      fprintf(stderr, "faltan simbolos\n"); return 0;
    }

    p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
    p_set_poll(poll_cb); p_set_inp(input_cb);
    p_init();

    if (!(use_sh ? ayther_build_generated_rom_sh(rom, ROM_SIZE)
                 : ayther_build_generated_rom(rom, ROM_SIZE))) {
      fprintf(stderr, "no se pudo construir el ROM\n"); return 0;
    }
    memset(&gi_ext, 0, sizeof(gi_ext));
    gi_ext.full_path = "ayther-v1.md"; gi_ext.dir = "."; gi_ext.name = "ayther-v1";
    gi_ext.ext = "md"; gi_ext.data = rom; gi_ext.size = ROM_SIZE;
    gi_ext.persistent_data = true;
    memset(&gi, 0, sizeof(gi));
    gi.path = "ayther-v1.md"; gi.data = rom; gi.size = ROM_SIZE;
    if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); return 0; }

    c->api = p_iface(0);
    if (!c->api) { fprintf(stderr, "sin ABI\n"); return 0; }
    if (!(c->api->capabilities & AYTHER_CAP_OBSERVABILITY_V1)) return -1;
  }
  return 1;
}

int main(int argc, char **argv)
{
  static ayther_journal_v1 journal, journal2;
  static uint8_t palette[AYTHER_PALETTE_ENTRIES * 4];
  static ayther_frame_delta_v1 delta;
  ayther_frame_hash_v1 hashes;
  ayther_region_info_v1 info;
  struct core c;
  int f, r, fail = 0;

  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }

  /* ============ fixture con escrituras a mitad de frame ================= */
  r = core_open(&c, argv[1], 0);
  if (r < 0) { printf("observability: SALTEADO (sin la capability)\n"); return 0; }
  if (!r) return 2;

  /* --- sin suscripcion, las tres se niegan ------------------------------ */
  c.api->set_subscriptions(0);
  for (f = 0; f < 4; f++) c.run();
  {
    struct { uint32_t id; const char *name; } reg[3] = {
      { AYTHER_REGION_RASTER_JOURNAL, "journal" },
      { AYTHER_REGION_FRAME_HASH,     "frame hash" },
      { AYTHER_REGION_PALETTE,        "paleta" }
    };
    int i, bad = 0;
    for (i = 0; i < 3; ++i)
    {
      uint8_t dummy[8];
      int32_t st = c.api->read_region(reg[i].id, 0, dummy, sizeof(dummy),
                                      AYTHER_GENERATION_ANY, NULL);
      if (st != AYTHER_STATUS_NOT_SUBSCRIBED) {
        printf("  FALLA: %s contesta %d sin suscripcion, tendria que ser "
               "NOT_SUBSCRIBED (%d)\n", reg[i].name, (int)st,
               (int)AYTHER_STATUS_NOT_SUBSCRIBED);
        bad = 1;
      }
    }
    if (bad) fail = 1;
    else printf("sin suscripcion: las tres regiones se niegan  OK\n");
  }

  c.api->set_subscriptions(AYTHER_SUB_RASTER_TRACKING | AYTHER_SUB_FRAME_HASH |
                           AYTHER_SUB_VDP_MEMORY);
  for (f = 0; f < FRAMES; f++) c.run();

  /* --- A: el journal ---------------------------------------------------- */
  memset(&info, 0, sizeof(info));
  if (c.api->query_region(AYTHER_REGION_RASTER_JOURNAL, &info, sizeof(info))
        != AYTHER_STATUS_OK || info.byte_size != sizeof(journal)) {
    printf("  FALLA: la region del journal mide %u y la struct %u\n",
           (unsigned)info.byte_size, (unsigned)sizeof(journal));
    fail = 1;
  }
  else if (c.api->read_region(AYTHER_REGION_RASTER_JOURNAL, 0, &journal,
                              sizeof(journal), AYTHER_GENERATION_ANY, NULL)
             != AYTHER_STATUS_OK) {
    printf("  FALLA: el journal no se puede leer\n");
    fail = 1;
  }
  else
  {
    uint32_t i;
    int monotonic = 1;
    memset(&delta, 0, sizeof(delta));
    c.api->poll_frame_delta(&delta, sizeof(delta));

    printf("\njournal: %u eventos, %u perdidos (el frame delta dice %u)\n",
           journal.count, journal.dropped, delta.raster_event_count);

    if (journal.layout_version != AYTHER_LAYOUT_JOURNAL_V1 ||
        journal.struct_size != sizeof(journal)) {
      printf("  FALLA: cabecera del journal inconsistente\n"); fail = 1;
    }
    if (journal.count != delta.raster_event_count) {
      printf("  FALLA: la region y el frame delta cuentan distinto\n"); fail = 1;
    }
    if (!journal.count) {
      printf("  FALLA: el fixture escribe a mitad de frame; el journal no puede "
             "estar vacio\n");
      fail = 1;
    }
    for (i = 1; i < journal.count; ++i)
      if (journal.events[i].v_counter < journal.events[i - 1].v_counter)
        monotonic = 0;
    if (!monotonic) {
      printf("  FALLA: las lineas del journal no van en orden\n"); fail = 1;
    }
    else if (journal.count)
      printf("  lineas en orden, de %u a %u\n", journal.events[0].v_counter,
             journal.events[journal.count - 1].v_counter);
    for (i = 0; i < journal.count; ++i)
    {
      unsigned reason = journal.events[i].reason;
      if (!reason || (reason & (reason - 1u))) {
        printf("  FALLA: el evento %u tiene motivo %04x y tiene que ser un solo "
               "bit\n", i, reason);
        fail = 1;
        break;
      }
    }
    /* Es una foto, no una cola: leerla de nuevo en el mismo frame da lo mismo. */
    if (c.api->read_region(AYTHER_REGION_RASTER_JOURNAL, 0, &journal2,
                           sizeof(journal2), AYTHER_GENERATION_ANY, NULL)
          == AYTHER_STATUS_OK &&
        memcmp(&journal, &journal2, sizeof(journal)) != 0) {
      printf("  FALLA: dos lecturas del mismo frame dan journals distintos\n");
      fail = 1;
    }
  }

  /* --- D: los hashes ---------------------------------------------------- */
  memset(&hashes, 0, sizeof(hashes));
  if (c.api->read_region(AYTHER_REGION_FRAME_HASH, 0, &hashes, sizeof(hashes),
                         AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    printf("  FALLA: los hashes no se pueden leer\n");
    fail = 1;
  }
  else
  {
    printf("\nframe hash: frame %llu  video %016llx (afuera %016llx)\n",
           (unsigned long long)hashes.frame_index,
           (unsigned long long)hashes.video_hash,
           (unsigned long long)last_video_hash);
    printf("            vram %016llx cram %016llx vsram %016llx\n",
           (unsigned long long)hashes.vram_hash,
           (unsigned long long)hashes.cram_hash,
           (unsigned long long)hashes.vsram_hash);
    if (hashes.layout_version != AYTHER_LAYOUT_FRAME_HASH_V1 ||
        hashes.struct_size != sizeof(hashes)) {
      printf("  FALLA: cabecera de los hashes inconsistente\n"); fail = 1;
    }
    if (hashes.video_hash != last_video_hash) {
      printf("  FALLA: el hash del core y el de afuera miran frames distintos\n");
      fail = 1;
    }
    if (!hashes.vram_hash || !hashes.cram_hash) {
      printf("  FALLA: hashear la memoria del VDP no puede dar cero\n");
      fail = 1;
    }
    {
      uint64_t before = hashes.frame_index;
      c.run();
      memset(&hashes, 0, sizeof(hashes));
      if (c.api->read_region(AYTHER_REGION_FRAME_HASH, 0, &hashes,
                             sizeof(hashes), AYTHER_GENERATION_ANY, NULL)
            == AYTHER_STATUS_OK)
      {
        if (hashes.frame_index <= before) {
          printf("  FALLA: el indice de frame no avanzo\n"); fail = 1;
        }
        if (hashes.video_hash != last_video_hash) {
          printf("  FALLA: en el frame siguiente los dos hashes se separan\n");
          fail = 1;
        }
        else printf("  el hash del core sigue al frame emitido  OK\n");
      }
    }
  }
  close_library(c.lib);

  /* ============ fixture sin H-int: la paleta no se mueve ================= */
  r = core_open(&c, argv[1], 1);
  if (r <= 0) return r ? 2 : 0;
  c.api->set_subscriptions(AYTHER_SUB_VDP_MEMORY);
  for (f = 0; f < FRAMES; f++) c.run();

  memset(&info, 0, sizeof(info));
  if (c.api->query_region(AYTHER_REGION_PALETTE, &info, sizeof(info))
        != AYTHER_STATUS_OK) {
    printf("  FALLA: la region de paleta no se puede consultar\n");
    fail = 1;
  }
  else if (info.byte_size > sizeof(palette) ||
           c.api->read_region(AYTHER_REGION_PALETTE, 0, palette, info.byte_size,
                              AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    printf("  FALLA: la paleta no se puede leer (%u bytes)\n",
           (unsigned)info.byte_size);
    fail = 1;
  }
  else
  {
    printf("\npaleta: %u entradas de %u bytes\n",
           (unsigned)info.capacity, (unsigned)info.element_size);
    if (info.capacity != AYTHER_PALETTE_ENTRIES ||
        info.byte_size != info.capacity * info.element_size) {
      printf("  FALLA: la region no describe su propio tamanio\n"); fail = 1;
    }
    else if (info.element_size != sizeof(uint16_t)) {
      printf("  este build no es de 16 bpp; el chequeo de colores se saltea\n");
    }
    else
    {
      const uint16_t *pal = (const uint16_t *)palette;
      unsigned i, x, y, distinct = 0, missing = 0;
      static uint8_t seen[65536];
      memset(seen, 0, sizeof(seen));
      for (y = 0; y < frame_h; ++y)
        for (x = 0; x < frame_w; ++x)
        {
          uint16_t px = frame_px[(size_t)y * frame_w + x];
          if (seen[px]) continue;
          seen[px] = 1;
          ++distinct;
          for (i = 0; i < info.capacity; ++i)
            if (pal[i] == px) break;
          if (i == info.capacity) {
            if (missing < 4)
              printf("  color %04x del frame no esta en la paleta\n", px);
            ++missing;
          }
        }
      printf("  %u colores distintos en el frame, %u sin entrada en la paleta\n",
             distinct, missing);
      if (distinct < 2) {
        printf("  FALLA: un frame de un solo color no prueba nada\n"); fail = 1;
      }
      if (missing) {
        printf("  FALLA: sin escrituras de CRAM a mitad de frame, la paleta "
               "tiene que explicar todo color emitido\n");
        fail = 1;
      }
    }
  }
  close_library(c.lib);

  printf("\nobservability: %s\n", fail ? "FALLA" : "OK");
  return fail;
}
