/* #34: la recomposicion con estado del VDP arbitrario.
 *
 * `recompose_frame` y `recompose_multilayer` vuelven a renderizar el frame con
 * el mismo renderer del core, pisando globales del VDP y restaurandolas al
 * salir. Dos cosas pueden salir mal ahi y ninguna crashea sola:
 *
 *   - escribir fuera del buffer que le dio el frontend;
 *   - dejar el estado del emulador distinto de como lo encontro, que rompe el
 *     determinismo del replay sin ningun sintoma inmediato.
 *
 * El fuzzer mueve la memoria del VDP -- VRAM, CRAM, VSRAM y los registros, por
 * los punteros mutables legacy-, las mascaras de control y los flags de
 * recomposicion, y despues verifica las dos cosas:
 *
 *   1. Canarios alrededor del buffer de salida, intactos.
 *   2. Hash del estado serializado antes y despues: igual. Es la definicion
 *      operativa de "no perturba la emulacion", y es la propiedad de la que
 *      dependen los tres pases del replay determinista.
 */

#include "fuzz_common.h"

#define RC_W 320
#define RC_H 240
#define RC_PIXELS (RC_W * RC_H)
#define RC_GUARD  256u
#define RC_CANARY 0xA5

static uint64_t hash_bytes(const void *p, size_t n)
{
  const uint8_t *b = (const uint8_t *)p;
  uint64_t h = UINT64_C(1469598103934665603);
  size_t i;
  for (i = 0; i < n; ++i) { h ^= b[i]; h *= UINT64_C(1099511628211); }
  return h;
}

/* Los punteros crudos de la interfaz legacy: es la unica via para ensuciar la
   memoria del VDP desde afuera, y justamente por eso hay que fuzzearla. */
static void *mem(fuzz_core *c, unsigned id)
{
  static void *(*get_data)(unsigned);
  static size_t (*get_size)(unsigned);
  if (!get_data) {
    *(void **)&get_data = FUZZ_SYM(c->lib, "retro_get_memory_data");
    *(void **)&get_size = FUZZ_SYM(c->lib, "retro_get_memory_size");
  }
  (void)get_size;
  return get_data ? get_data(id) : NULL;
}

static size_t mem_size(fuzz_core *c, unsigned id)
{
  static size_t (*get_size)(unsigned);
  if (!get_size) *(void **)&get_size = FUZZ_SYM(c->lib, "retro_get_memory_size");
  return get_size ? get_size(id) : 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  static uint16_t *buf;      /* salida + guardas a los dos lados */
  static uint8_t *state;
  static size_t state_size;
  fuzz_core *c = fuzz_core_get();
  fuzz_reader r;
  uint16_t *out;
  uint64_t before, after;
  uint32_t w = 0, h = 0, flags;
  int32_t st;
  int muts = 0;

  if (!c->api || !AYTHER_IFACE_HAS(c->api, recompose_multilayer)) return 0;

  if (!buf) {
    buf = (uint16_t *)malloc((RC_PIXELS + 2 * RC_GUARD) * sizeof(uint16_t));
    state_size = c->serialize_size();
    state = (uint8_t *)malloc(state_size ? state_size : 1);
    if (!buf || !state) return 0;
  }
  out = buf + RC_GUARD;

  c->api->set_subscriptions(AYTHER_SUB_ALL);
  c->run();

  fuzz_reader_init(&r, data, size);
  flags = fuzz_u32(&r);

  /* Ensuciar la memoria del VDP. Los ids son los legacy: 0x003 VRAM,
     0x100 CRAM, 0x101 registros, 0x107 VSRAM. */
  {
    static const unsigned ids[] = {0x003u, 0x100u, 0x101u, 0x107u};
    while (fuzz_more(&r) && muts < 512)
    {
      unsigned id = ids[fuzz_u8(&r) & 3u];
      uint8_t *p = (uint8_t *)mem(c, id);
      size_t n = mem_size(c, id);
      uint32_t off = fuzz_u32(&r);
      if (p && n) p[off % n] = (uint8_t)fuzz_u8(&r);
      ++muts;
    }
  }

  /* El estado DESPUES de ensuciar es la referencia: lo que se afirma es que
     recomponer no cambia nada, no que la basura sea inofensiva. */
  before = c->serialize(state, state_size) ? hash_bytes(state, state_size) : 0;

  memset(buf, RC_CANARY, (RC_PIXELS + 2 * RC_GUARD) * sizeof(uint16_t));
  memset(out, 0, RC_PIXELS * sizeof(uint16_t));

  st = c->api->recompose_multilayer(out, NULL, NULL, NULL, NULL,
                                    RC_PIXELS, flags, &w, &h);
  (void)st;

  /* Invariante 1: las guardas no se tocaron. */
  {
    size_t i;
    const uint8_t *lo = (const uint8_t *)buf;
    const uint8_t *hi = (const uint8_t *)(out + RC_PIXELS);
    for (i = 0; i < RC_GUARD * sizeof(uint16_t); ++i)
      if (lo[i] != RC_CANARY || hi[i] != RC_CANARY) {
        fprintf(stderr, "recompose escribio fuera del buffer (flags %08x)\n", flags);
        abort();
      }
  }

  /* Invariante 2: el estado del emulador quedo como estaba. */
  after = c->serialize(state, state_size) ? hash_bytes(state, state_size) : 0;
  if (before != after) {
    fprintf(stderr, "recompose perturbo el estado del core (flags %08x)\n", flags);
    abort();
  }

  c->api->set_subscriptions(0);
  return 0;
}
