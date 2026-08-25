/* #42: el estado de render POR SCANLINE, capturado del lado de la LECTURA.
 *
 * El raster journal aproxima ese estado desde el lado de la ESCRITURA: hay que
 * adivinar en que ciclo de que linea cayo cada write y que efecto tuvo.
 * Capturarlo donde el renderer lo CONSUME es exacto -- es literalmente el valor
 * que uso-- y ademas mas barato: no hay nada que reconstruir.
 *
 * Lo que se verifica aca:
 *
 *   1. El scroll horizontal por linea que la region reporta coincide, linea por
 *      linea, con lo que el ROM escribio en la tabla de hscroll. La tabla es el
 *      oraculo: el test la construye y sabe que valor le toca a cada linea.
 *
 *   2. La CRAM por linea distingue un frame con escrituras de paleta a mitad de
 *      pantalla de uno sin ellas. Sin escrituras la region entrega UNA entrada y
 *      el flag CRAM_UNIFORM -- 128 B en vez de 30 KB-; con ellas, la linea de
 *      antes del write y la de despues son distintas.
 *
 *   3. Sin suscripcion, las dos regiones contestan NOT_SUBSCRIBED en vez de
 *      devolver un buffer vacio con OK.
 *
 * El fixture de siempre sirve de oraculo para (1) y (2): su handler de H-int
 * escribe CRAM en cada linea, y su tabla de hscroll tiene un valor distinto por
 * linea con una formula conocida.
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
#define FRAMES   12

static struct retro_game_info_ext gi_ext;
static uint8_t regs_buf[64 * 1024];
static uint8_t cram_buf[64 * 1024];

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
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d, size_t f){(void)d;return f;}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core>\n", argv[0]); return 2; }

  library_t lib = open_library(argv[1]);
  if (!lib) { fprintf(stderr, "no carga %s\n", argv[1]); return 2; }

  void (*p_set_env)(retro_environment_t)        = load_symbol(lib, "retro_set_environment");
  void (*p_set_vid)(retro_video_refresh_t)      = load_symbol(lib, "retro_set_video_refresh");
  void (*p_set_aud)(retro_audio_sample_batch_t) = load_symbol(lib, "retro_set_audio_sample_batch");
  void (*p_set_poll)(retro_input_poll_t)        = load_symbol(lib, "retro_set_input_poll");
  void (*p_set_inp)(retro_input_state_t)        = load_symbol(lib, "retro_set_input_state");
  void (*p_init)(void)                          = load_symbol(lib, "retro_init");
  bool (*p_load)(const struct retro_game_info *)= load_symbol(lib, "retro_load_game");
  void (*p_run)(void)                           = load_symbol(lib, "retro_run");
  ayther_get_interface_fn p_iface = (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
  if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 2;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom(rom, ROM_SIZE)) {
    fprintf(stderr, "no se pudo construir el ROM\n"); close_library(lib); return 2;
  }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 2; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 2; }

  ayther_region_info_v1 info;
  int f, fail = 0;

  /* --- 3 (primero, porque despues nos suscribimos): sin suscripcion ------ */
  api->set_subscriptions(0);
  for (f = 0; f < 4; f++) p_run();
  memset(&info, 0, sizeof(info));
  if (api->query_region(AYTHER_REGION_LINE_REGS, &info, sizeof(info)) !=
      AYTHER_STATUS_OK) {
    printf("line-state: SALTEADO (core sin la capability)\n");
    close_library(lib); return 0;
  }
  {
    int32_t rc = api->read_region(AYTHER_REGION_LINE_REGS, 0, regs_buf,
                                  sizeof(ayther_line_header_v1),
                                  AYTHER_GENERATION_ANY, NULL);
    printf("sin suscripcion: read_region -> %d\n", (int)rc);
    if (rc != AYTHER_STATUS_NOT_SUBSCRIBED) {
      printf("  FALLA: tendria que ser NOT_SUBSCRIBED (%d)\n",
             (int)AYTHER_STATUS_NOT_SUBSCRIBED);
      fail = 1;
    }
  }

  /* --- 1: el scroll por linea contra la tabla de hscroll ---------------- */
  api->set_subscriptions(AYTHER_SUB_LINE_STATE | AYTHER_SUB_LINE_CRAM);
  for (f = 0; f < FRAMES; f++) p_run();

  memset(&info, 0, sizeof(info));
  api->query_region(AYTHER_REGION_LINE_REGS, &info, sizeof(info));
  if (!info.byte_size || info.byte_size > sizeof(regs_buf)) {
    printf("  FALLA: region de registros por linea inesperada (%u B)\n",
           (unsigned)info.byte_size);
    close_library(lib); return 1;
  }
  if (api->read_region(AYTHER_REGION_LINE_REGS, 0, regs_buf, info.byte_size,
                       AYTHER_GENERATION_ANY, NULL) != AYTHER_STATUS_OK) {
    printf("  FALLA: la region de registros no se puede leer\n");
    close_library(lib); return 1;
  }

  {
    const ayther_line_header_v1 *h = (const ayther_line_header_v1 *)regs_buf;
    const ayther_line_regs_v1 *r =
      (const ayther_line_regs_v1 *)(regs_buf + sizeof(*h));
    unsigned line, mismatches = 0, first = 0;

    printf("registros por linea: %u lineas, entrada de %u B, generacion %llu\n",
           (unsigned)h->lines, (unsigned)h->entry_size,
           (unsigned long long)h->frame_generation);
    if (h->entry_size != sizeof(ayther_line_regs_v1) ||
        h->struct_size != sizeof(*h)) {
      printf("  FALLA: cabecera inconsistente\n"); fail = 1;
    }
    if (h->lines < 200u) {
      printf("  FALLA: el fixture dibuja 224 lineas activas\n"); fail = 1;
    }

    /* El ROM llena la tabla de hscroll con (0 - (linea & 31)) para el plano A.
       El VDP la lee con diez bits, asi que el valor esperado es el mismo pero
       truncado. Es un oraculo independiente del core: sale del generador. */
    /* Desde la 1: el handler de V-int del fixture PISA la primera palabra de la
       tabla con el contador de frame, a propósito —para que la escena se mueva—.
       La línea 0 es entonces el único valor que el oráculo no conoce. */
    for (line = 1; line < h->lines && line < 224u; ++line)
    {
      uint16_t expected = (uint16_t)((0u - (line & 31u)) & 0x3FFu);
      if (r[line].xscroll_a != expected)
      {
        if (!mismatches) first = line;
        ++mismatches;
      }
    }
    if (mismatches)
    {
      printf("  FALLA: %u lineas con xscroll_a distinto del que el ROM escribio; "
             "la primera es la %u (%u contra %u)\n",
             mismatches, first, r[first].xscroll_a,
             (unsigned)((0u - (first & 31u)) & 0x3FFu));
      fail = 1;
    }
    else
      printf("xscroll_a coincide con la tabla de hscroll en las %u lineas\n",
             (unsigned)(h->lines < 224u ? h->lines : 224u));

    /* Y las bases resueltas tienen que ser las que el ROM programo. */
    if (r[0].ntab != 0xC000u || r[0].ntbb != 0xE000u || r[0].hscb != 0xF000u ||
        r[0].satb != 0xD800u) {
      printf("  FALLA: bases resueltas inesperadas: ntab=%04x ntbb=%04x "
             "hscb=%04x satb=%04x\n",
             r[0].ntab, r[0].ntbb, r[0].hscb, r[0].satb);
      fail = 1;
    } else {
      printf("bases resueltas: ntab=%04x ntbb=%04x hscb=%04x satb=%04x\n",
             r[0].ntab, r[0].ntbb, r[0].hscb, r[0].satb);
    }
  }

  /* --- 2: la CRAM por linea -------------------------------------------- */
  memset(&info, 0, sizeof(info));
  api->query_region(AYTHER_REGION_LINE_CRAM, &info, sizeof(info));
  if (!info.byte_size || info.byte_size > sizeof(cram_buf)) {
    printf("  FALLA: region de CRAM por linea inesperada (%u B)\n",
           (unsigned)info.byte_size);
    close_library(lib); return 1;
  }
  api->read_region(AYTHER_REGION_LINE_CRAM, 0, cram_buf, info.byte_size,
                   AYTHER_GENERATION_ANY, NULL);
  close_library(lib);

  {
    const ayther_line_header_v1 *h = (const ayther_line_header_v1 *)cram_buf;
    const uint8_t *c = cram_buf + sizeof(*h);
    int uniform = (h->flags & AYTHER_LINES_CRAM_UNIFORM) != 0;

    printf("CRAM por linea: %u entradas, %s\n", (unsigned)h->lines,
           uniform ? "CRAM_UNIFORM" : "cambia a mitad de frame");

    /* El fixture escribe CRAM desde el handler de H-int, o sea una vez por
       scanline: este frame NO puede ser uniforme. Que lo fuera significaria que
       la captura no esta viendo los writes de mitad de pantalla, que es
       exactamente lo que la region existe para mostrar. */
    if (uniform) {
      printf("  FALLA: el fixture escribe CRAM por scanline; uniforme es un "
             "sintoma de que la captura no ve el efecto raster\n");
      fail = 1;
    } else if (h->lines < 200u) {
      printf("  FALLA: se esperaba una entrada por linea activa\n");
      fail = 1;
    } else {
      /* Cuántas paletas DISTINTAS hay en el frame. Comparar dos líneas
         consecutivas no alcanza: el write de una línea puede caer después de
         que esa línea se dibujó, y entonces el cambio aparece una línea más
         tarde. Lo que la región tiene que poder mostrar es que en el frame hubo
         más de una paleta, que es lo que un efecto raster ES. */
      unsigned distinct = 1, i;
      for (i = 1; i < h->lines; ++i)
        if (memcmp(c + (size_t)i * 128u, c + (size_t)(i - 1u) * 128u, 128u))
          ++distinct;
      printf("paletas distintas en el frame: %u sobre %u lineas\n",
             distinct, (unsigned)h->lines);
      if (distinct < 2u) {
        printf("  FALLA: el fixture escribe CRAM por scanline; una sola paleta "
               "significa que la captura no ve el efecto raster\n");
        fail = 1;
      }
    }
  }

  printf("\n%s\n", fail ? "FALLO" : "TODO OK");
  return fail;
}
