/* #29: que los controles de audio se comporten igual en TODOS los cores de
 * sonido y en todos los perfiles, sin fugas de estado.
 *
 * Cubre los cuatro puntos del issue:
 *
 *   1. mute por canal (0x10D) en ym2612 Y en ym3438 (Nuked, que no tenia hook).
 *   2. gain por canal en los dos cores.
 *   3. sin offset DC en el PSG cuando la suscripcion se cae con un canal
 *      muteado -- el modo de falla que NO da error: el audio queda con un
 *      escalon permanente y nada lo reporta.
 *   4. el log AUDIO_WRITES lo produce el core, asi que trae eventos con
 *      extensions=1 aunque probe=0.
 *
 * Los tests de mute y gain se auto-validan: corren la MISMA comprobacion sobre
 * los dos cores. Que ym2612 responda es la prueba de que el fixture genera
 * audio FM; si respondiera uno solo, la diferencia es el defecto y no un
 * fixture mudo. Por eso no alcanza con probar ym3438 aislado -- un "no cambio
 * nada" seria indistinguible de "aca no habia FM que silenciar".
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
#define FRAMES   60

/* Mascara 0x10D: bits 0-5 FM, 6-9 PSG, 10-17 PCM. */
#define MUTE_FM_ALL   0x3Fu
#define MUTE_PSG_ALL  0x3C0u
#define MUTE_PSG_CH1  (1u << 7)

/* Fuentes de audio_probe_set_channel_gain (mismo enum que la ABI). */
#define AP_SRC_FM_    AYTHER_AUDIO_SOURCE_FM

static const char *g_fm_core;            /* valor de genesis_plus_gx_ym2612 */
static struct retro_game_info_ext gi_ext;
static uint64_t g_audio_hash;
static uint64_t g_audio_energy;
static int64_t  g_audio_dc;              /* suma CON signo: detecta el escalon */
static uint64_t g_audio_n;
static int      g_capturing;             /* la ventana de medicion esta abierta */

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
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *v = (struct retro_variable *)data;
      if (!v || !v->key) return false;
      v->value = NULL;
      if (!strcmp(v->key, "genesis_plus_gx_ym2612")) v->value = g_fm_core;
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    default: return false;
  }
}
static void vid_cb(const void *d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static size_t aud_cb(const int16_t *d, size_t frames)
{
  size_t i;
  if (!g_capturing) return frames;
  for (i = 0; i < frames * 2; i++) {
    g_audio_hash ^= (uint64_t)(uint16_t)d[i];
    g_audio_hash *= UINT64_C(0x100000001B3);
    g_audio_energy += (uint64_t)(d[i] < 0 ? -d[i] : d[i]);
    g_audio_dc += (int64_t)d[i];
    g_audio_n++;
  }
  return frames;
}
static void poll_cb(void){}
static int16_t input_cb(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}

/* Lo que define una corrida. Todo opcional: {0} da la corrida limpia. */
typedef struct {
  const char *fm_core;
  uint32_t    mute_mask;        /* mascara 0x10D aplicada al empezar          */
  int         fm_gain;          /* gain % para los 6 canales FM; <0 = no tocar*/
  int         drop_sub_at;      /* frame en el que se suelta la suscripcion   */
  int         capture_from;     /* primer frame que entra en hash/energia/DC  */
  int         save_at;          /* frame en el que se serializa; <0 = nunca   */
  const void *load_state;       /* estado a cargar antes de correr            */
  size_t      load_state_size;
} run_opts;

typedef struct {
  uint64_t hash, energy;
  int64_t  dc;
  uint64_t samples;
  uint32_t writes;
  void    *state;               /* malloc del llamador si save_at >= 0        */
  size_t   state_size;
} run_result;

/* Una corrida completa: carga el DLL, aplica los controles y devuelve las
   metricas del audio. DLL nuevo por corrida: sin estado que se filtre. */
static int run_once(const char *dll, const run_opts *o, run_result *r)
{
  library_t lib = open_library(dll);
  int f;
  if (!lib) { fprintf(stderr, "no carga %s\n", dll); return 0; }
  g_fm_core = o->fm_core ? o->fm_core : "mame (ym2612)";
  g_audio_hash = UINT64_C(0xCBF29CE484222325);
  g_audio_energy = 0; g_audio_dc = 0; g_audio_n = 0;
  g_capturing = (o->capture_from <= 0);
  memset(r, 0, sizeof(*r));

  void (*p_set_env)(retro_environment_t)          = load_symbol(lib, "retro_set_environment");
  void (*p_set_vid)(retro_video_refresh_t)        = load_symbol(lib, "retro_set_video_refresh");
  void (*p_set_aud)(retro_audio_sample_batch_t)   = load_symbol(lib, "retro_set_audio_sample_batch");
  void (*p_set_poll)(retro_input_poll_t)          = load_symbol(lib, "retro_set_input_poll");
  void (*p_set_inp)(retro_input_state_t)          = load_symbol(lib, "retro_set_input_state");
  void (*p_init)(void)                            = load_symbol(lib, "retro_init");
  bool (*p_load)(const struct retro_game_info *)  = load_symbol(lib, "retro_load_game");
  void (*p_run)(void)                             = load_symbol(lib, "retro_run");
  size_t (*p_ser_size)(void)                      = load_symbol(lib, "retro_serialize_size");
  bool (*p_ser)(void *, size_t)                   = load_symbol(lib, "retro_serialize");
  bool (*p_unser)(const void *, size_t)           = load_symbol(lib, "retro_unserialize");
  void (*p_gain)(int, int, int)                   = load_symbol(lib, "audio_probe_set_channel_gain");
  ayther_get_interface_fn p_iface                 = (ayther_get_interface_fn)load_symbol(lib, "ayther_get_interface");
  if (!p_set_env || !p_init || !p_load || !p_run || !p_iface) {
    fprintf(stderr, "faltan simbolos\n"); close_library(lib); return 0;
  }
  if (o->fm_gain >= 0 && !p_gain) {
    /* probe=0: el gain no existe en esta build. No es un fallo del core. */
    close_library(lib); return -1;
  }

  p_set_env(env_cb); p_set_vid(vid_cb); p_set_aud(aud_cb);
  p_set_poll(poll_cb); p_set_inp(input_cb);
  p_init();

  static uint8_t rom[ROM_SIZE];
  if (!ayther_build_generated_rom_fm(rom, ROM_SIZE)) { close_library(lib); return 0; }
  memset(&gi_ext, 0, sizeof(gi_ext));
  gi_ext.full_path="ayther-generated-v1.md"; gi_ext.dir="."; gi_ext.name="ayther-generated-v1";
  gi_ext.ext="md"; gi_ext.data=rom; gi_ext.size=ROM_SIZE; gi_ext.persistent_data=true;
  struct retro_game_info gi; memset(&gi,0,sizeof(gi));
  gi.path="ayther-generated-v1.md"; gi.data=rom; gi.size=ROM_SIZE;
  if (!p_load(&gi)) { fprintf(stderr, "load_game fallo\n"); close_library(lib); return 0; }

  const ayther_interface_v1 *api = p_iface(0);
  if (!api) { fprintf(stderr, "sin ABI\n"); close_library(lib); return 0; }
  api->set_subscriptions(AYTHER_SUB_RENDER_CONTROLS | AYTHER_SUB_AUDIO_WRITES);
  p_run();  /* la suscripcion se activa al inicio del frame siguiente */

  /* Un estado cargado ANTES de aplicar controles: es el escenario del punto 3
     del issue -- savestate tomado con un canal muteado, restaurado en una
     sesion que no se suscribio a nada. */
  if (o->load_state) {
    if (!p_unser || !p_unser(o->load_state, o->load_state_size)) {
      fprintf(stderr, "unserialize fallo\n"); close_library(lib); return 0;
    }
  }

  if (o->mute_mask) {
    uint64_t gen = 0; uint32_t mask = o->mute_mask;
    int32_t rc = api->write_control(AYTHER_REGION_AUDIO_MUTE, 0, &mask,
                                    sizeof(mask), AYTHER_GENERATION_ANY, &gen);
    if (rc != AYTHER_STATUS_OK) {
      fprintf(stderr, "write_control(AUDIO_MUTE) -> %d\n", (int)rc);
      close_library(lib); return 0;
    }
  }
  if (o->fm_gain >= 0) {
    int ch;
    for (ch = 0; ch < 6; ch++) p_gain(AP_SRC_FM_, ch, o->fm_gain);
  }

  for (f = 0; f < FRAMES; f++) {
    if (o->drop_sub_at > 0 && f == o->drop_sub_at) api->set_subscriptions(0);
    if (o->capture_from > 0 && f == o->capture_from) {
      /* La ventana se abre limpia: lo que se mide es el REGIMEN, no el
         transitorio de soltar la suscripcion. */
      g_capturing = 1;
      g_audio_hash = UINT64_C(0xCBF29CE484222325);
      g_audio_energy = 0; g_audio_dc = 0; g_audio_n = 0;
    }
    p_run();
    if (o->save_at >= 0 && f == o->save_at && p_ser && p_ser_size) {
      r->state_size = p_ser_size();
      r->state = malloc(r->state_size);
      if (!r->state || !p_ser(r->state, r->state_size)) {
        fprintf(stderr, "serialize fallo\n"); close_library(lib); return 0;
      }
    }
  }

  /* #29: la region tiene que traer eventos con extensions=1, con o sin probe.
     Se lee ANTES de cerrar el DLL y despues del ultimo frame. */
  {
    uint32_t n = 0; uint64_t gen = 0;
    if (api->read_region(AYTHER_REGION_AUDIO_WRITE_COUNT, 0, &n, sizeof(n),
                         AYTHER_GENERATION_ANY, &gen) != AYTHER_STATUS_OK)
      n = 0;
    r->writes = n;
  }

  r->hash = g_audio_hash; r->energy = g_audio_energy;
  r->dc = g_audio_dc; r->samples = g_audio_n;
  close_library(lib);
  return 1;
}

static run_opts opts_for(const char *core)
{
  run_opts o; memset(&o, 0, sizeof(o));
  o.fm_core = core; o.fm_gain = -1; o.drop_sub_at = -1;
  o.capture_from = 0; o.save_at = -1;
  return o;
}

/* --- 1. el mute llega al chip de FM seleccionado ------------------------- */
static int check_mute(const char *dll, const char *core)
{
  run_opts a = opts_for(core), b = opts_for(core);
  run_result ra, rb;
  b.mute_mask = MUTE_FM_ALL;
  if (run_once(dll, &a, &ra) != 1 || run_once(dll, &b, &rb) != 1) return 1;

  int changed = (ra.hash != rb.hash), quieter = (rb.energy < ra.energy);
  printf("%-16s  mute FM: energia %llu -> %llu  %s\n", core,
         (unsigned long long)ra.energy, (unsigned long long)rb.energy,
         changed ? (quieter ? "OK" : "cambia pero NO baja")
                 : "NO-OP (el mute no llega al chip)");
  /* El log AUDIO_WRITES pertenece a la ABI, no al probe: con la capability
     anunciada, una region vacia para siempre es el peor contrato posible. */
  printf("%-16s  AUDIO_WRITES: %u eventos  %s\n", core,
         (unsigned)ra.writes, ra.writes > 0 ? "OK" : "VACIA (sin productor)");
  return (!changed || !quieter || ra.writes == 0);
}

/* --- 2. el gain llega al chip de FM seleccionado ------------------------- */
static int check_gain(const char *dll, const char *core, int *skipped)
{
  /* Con el PSG muteado lo unico que suena es el FM, asi que atenuar los seis
     canales al 50% tiene que dar la MITAD de energia. Sin mutear el PSG la
     prediccion no seria comprobable: la energia es |suma| y no suma de |.|,
     y el piso de PSG diluiria el efecto hasta hacerlo indistinguible de ruido. */
  run_opts a = opts_for(core), b = opts_for(core);
  run_result ra, rb;
  int rc_a, rc_b;
  double expected, got, err;

  a.mute_mask = MUTE_PSG_ALL;
  b.mute_mask = MUTE_PSG_ALL; b.fm_gain = 50;
  rc_a = run_once(dll, &a, &ra);
  rc_b = run_once(dll, &b, &rb);
  if (rc_a == -1 || rc_b == -1) {
    printf("%-16s  gain: SALTEADO (build sin SOUND_PROBE)\n", core);
    *skipped = 1;
    return 0;
  }
  if (rc_a != 1 || rc_b != 1) return 1;
  if (ra.energy == 0) {
    printf("%-16s  gain: FIXTURE MUDO (energia FM = 0)\n", core);
    return 1;
  }

  expected = (double)ra.energy * 0.5;
  got      = (double)rb.energy;
  err      = (got - expected) / expected;
  printf("%-16s  gain 50%%: energia %llu -> %llu (esperado ~%.0f, error %+.1f%%)  %s\n",
         core, (unsigned long long)ra.energy, (unsigned long long)rb.energy,
         expected, err * 100.0,
         (err > -0.03 && err < 0.03) ? "OK" : "FUERA DE RANGO");
  return !(err > -0.03 && err < 0.03);
}

/* --- 3. el PSG no queda con offset DC al caerse la suscripcion ----------- */

/* Frames de asentamiento entre soltar la suscripcion y empezar a medir.
 *
 * NO es holgura: es el tiempo que tarda el paso-altos de blip_buf en digerir
 * el escalon del desmute. Medido en este fixture, el audio vuelve a ser BIT A
 * BIT el de stock entre 8 y 11 frames despues de soltar la suscripcion,
 * cualquiera sea el frame en que se suelte (probado en 5 y en 20). 15 deja
 * margen sin volverse un numero magico: el orden es 0,15 s, que es justo la
 * constante de la remocion de DC de blip, no un transitorio de la emulacion.
 *
 * Un margen de 0 no probaria nada mas exigente: probaria OTRA cosa -- que el
 * filtro no tiene memoria-, y fallaria siempre. */
#define PSG_SETTLE_FRAMES 15

static int check_psg_no_dc(const char *dll)
{
  /* El modo de falla que se busca: la correccion de borde del mute vive en
     psg_update. Si solo corriera en el clon observado, soltar RENDER_CONTROLS
     con un canal muteado dejaria el nivel de ese canal restado PARA SIEMPRE.
     No da error: da un escalon de DC y un audio que ya no vuelve al de stock. */
  run_opts muted = opts_for(NULL), clean = opts_for(NULL);
  run_result rm, rc;
  double dc_m, dc_c;
  int bad = 0;
  int drop = 20;

  muted.mute_mask = MUTE_PSG_CH1;
  muted.drop_sub_at = drop;                        /* se suelta CON el mute puesto */
  muted.capture_from = drop + PSG_SETTLE_FRAMES;
  clean.drop_sub_at = drop;   /* misma coreografia sin mute: es la referencia */
  clean.capture_from = drop + PSG_SETTLE_FRAMES;

  if (run_once(dll, &muted, &rm) != 1 || run_once(dll, &clean, &rc) != 1) return 1;
  if (rm.samples == 0 || rc.samples == 0) {
    printf("psg-dc: sin muestras en la ventana\n"); return 1;
  }

  dc_m = (double)rm.dc / (double)rm.samples;
  dc_c = (double)rc.dc / (double)rc.samples;
  /* El umbral es sobre la DIFERENCIA contra la referencia, no sobre el valor
     absoluto: el fixture puede tener su propio DC y eso no es el defecto. */
  bad |= !(dc_m - dc_c < 1.0 && dc_m - dc_c > -1.0);
  printf("psg-dc: media %+.3f (referencia %+.3f, delta %+.3f)  %s\n",
         dc_m, dc_c, dc_m - dc_c, bad ? "OFFSET DC RESIDUAL" : "OK");

  /* Y el audio tiene que volver a ser EL MISMO, no solo estar centrado: el
     mute es output-only, asi que soltada la suscripcion la salida es stock. */
  if (rm.hash != rc.hash) {
    printf("psg-dc: el audio no vuelve a stock (hash %016llx vs %016llx)  FALLA\n",
           (unsigned long long)rm.hash, (unsigned long long)rc.hash);
    bad = 1;
  } else {
    printf("psg-dc: el audio vuelve a stock %d frames despues  OK\n",
           PSG_SETTLE_FRAMES);
  }
  return bad;
}

/* --- 4. un savestate tomado con mute no se lleva el mute puesto ---------- */
static int check_savestate_neutral(const char *dll)
{
  /* El mute no toca el estado del chip, asi que el estado serializado con un
     canal muteado tiene que ser BYTE A BYTE el mismo que sin mutear, y cargarlo
     en una sesion sin suscripcion tiene que sonar como stock. Si el mute se
     colara al estado, el residual viajaria dentro del savestate y aparecerian
     sesiones silenciadas sin que nadie pidiera silencio. */
  run_opts muted = opts_for(NULL), clean = opts_for(NULL);
  run_result rm, rc, pm, pc;
  run_opts play_m, play_c;
  int bad = 0;

  muted.mute_mask = MUTE_PSG_CH1; muted.save_at = 20;
  clean.save_at = 20;
  if (run_once(dll, &muted, &rm) != 1 || run_once(dll, &clean, &rc) != 1) return 1;
  if (!rm.state || !rc.state) { printf("savestate: no se serializo\n"); return 1; }

  if (rm.state_size != rc.state_size ||
      memcmp(rm.state, rc.state, rm.state_size) != 0) {
    printf("savestate: el mute se filtro al estado (%llu B)  FALLA\n",
           (unsigned long long)rm.state_size);
    bad = 1;
  } else {
    printf("savestate: identico con y sin mute (%llu B)  OK\n",
           (unsigned long long)rm.state_size);
  }

  play_m = opts_for(NULL); play_c = opts_for(NULL);
  play_m.load_state = rm.state; play_m.load_state_size = rm.state_size;
  play_m.drop_sub_at = 1; play_m.capture_from = 5;
  play_c.load_state = rc.state; play_c.load_state_size = rc.state_size;
  play_c.drop_sub_at = 1; play_c.capture_from = 5;
  if (run_once(dll, &play_m, &pm) != 1 || run_once(dll, &play_c, &pc) != 1) {
    free(rm.state); free(rc.state); return 1;
  }
  if (pm.hash != pc.hash) {
    printf("savestate: cargado sin suscripcion NO suena como stock  FALLA\n");
    bad = 1;
  } else {
    printf("savestate: cargado sin suscripcion suena como stock  OK\n");
  }
  free(rm.state); free(rc.state);
  return bad;
}

int main(int argc, char **argv)
{
  if (argc < 2) { fprintf(stderr, "uso: %s <core.dll>\n", argv[0]); return 2; }
  const char *dll = argv[1];
  const char *cores[2] = { "mame (ym2612)", "nuked (ym3438)" };
  int fail = 0, skipped = 0, i;

  for (i = 0; i < 2; i++) {
    fail |= check_mute(dll, cores[i]);
    fail |= check_gain(dll, cores[i], &skipped);
  }
  fail |= check_psg_no_dc(dll);
  fail |= check_savestate_neutral(dll);

  printf("\n%s%s\n", fail ? "FALLO" : "TODO OK",
         skipped ? " (gain salteado: build sin SOUND_PROBE)" : "");
  return fail;
}
