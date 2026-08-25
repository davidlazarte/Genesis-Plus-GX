/* #34: `write_control` con secuencias arbitrarias.
 *
 * Es una de las dos superficies por las que un frontend mete bytes que el core
 * no eligio (la otra es `unserialize`). Hasta aca la cubrian tests con entradas
 * que elegi yo, que es exactamente el sesgo que un fuzzer no tiene.
 *
 * Tres invariantes, y la segunda es la interesante:
 *
 *   1. El status siempre es uno de los documentados. Un codigo que no esta en
 *      la lista es un contrato roto aunque no crashee: el frontend no puede
 *      manejar lo que no puede nombrar.
 *   2. Si la escritura se RECHAZA, la region no cambio ni un byte. Una
 *      escritura parcial seguida de un error es el peor resultado posible --
 *      el frontend cree que no paso nada y el core quedo a medio camino-, y es
 *      un bug que no crashea, asi que solo se encuentra mirandolo.
 *   3. Despues de cualquier secuencia, el core sigue corriendo frames.
 *
 * El fuzzer tambien mueve las suscripciones: gran parte de la logica de
 * `write_control` depende de ellas, y dejarlas fijas dejaria la mitad del
 * arbol de decision sin explorar.
 */

#include "fuzz_common.h"

/* Las regiones se prueban TODAS, no solo las escribibles: pedir escritura sobre
   una de solo lectura tiene que dar READ_ONLY, y sobre un id inexistente
   NOT_FOUND. Que el rechazo sea el correcto es parte del contrato. */
#define FUZZ_MAX_REGION_BYTES 4096u

static int status_is_documented(int32_t s)
{
  switch (s) {
    case AYTHER_STATUS_OK:
    case AYTHER_STATUS_INVALID_ARGUMENT:
    case AYTHER_STATUS_NOT_FOUND:
    case AYTHER_STATUS_BUFFER_TOO_SMALL:
    case AYTHER_STATUS_OUT_OF_BOUNDS:
    case AYTHER_STATUS_READ_ONLY:
    case AYTHER_STATUS_STALE_GENERATION:
    case AYTHER_STATUS_BUSY:
    case AYTHER_STATUS_UNSUPPORTED:
    case AYTHER_STATUS_NOT_SUBSCRIBED:
    case AYTHER_STATUS_DELTA_HISTORY_LOST:
    case AYTHER_STATUS_UNSUPPORTED_MODE:
      return 1;
    default:
      return 0;
  }
}


/* Cada entrada arranca del mismo estado. Un fuzzer que acumula estado encuentra
   cosas, pero no puede decir CUAL entrada las encontro, y un hallazgo que no se
   reproduce desde su archivo no sirve para arreglar nada. */
static void reset_controls(fuzz_core *c)
{
  static const struct { uint32_t id; uint8_t fill; } regs[] = {
    { AYTHER_REGION_LAYER_MASK,            0x0F },  /* todo visible */
    { AYTHER_REGION_LAYER_DIM,             0x00 },
    { AYTHER_REGION_SPRITE_SUPPRESS,       0x00 },
    { AYTHER_REGION_TILE_SUPPRESS,         0x00 },
    { AYTHER_REGION_PLANE_TILE_SUPPRESS,   0x00 },
    { AYTHER_REGION_PLANE_SUPPRESS_ACTIVE, 0x00 },
    { AYTHER_REGION_AUDIO_MUTE,            0x00 }
  };
  static uint8_t zero[FUZZ_MAX_REGION_BYTES];
  size_t k;

  c->api->set_subscriptions(AYTHER_SUB_ALL);
  for (k = 0; k < sizeof(regs) / sizeof(regs[0]); ++k)
  {
    ayther_region_info_v1 info;
    memset(&info, 0, sizeof(info));
    if (c->api->query_region(regs[k].id, &info, sizeof(info)) != AYTHER_STATUS_OK)
      continue;
    if (!info.byte_size || info.byte_size > sizeof(zero)) continue;
    memset(zero, regs[k].fill, info.byte_size);
    c->api->write_control(regs[k].id, 0, zero, info.byte_size,
                          AYTHER_GENERATION_ANY, NULL);
  }
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  static uint8_t before[FUZZ_MAX_REGION_BYTES];
  static uint8_t after[FUZZ_MAX_REGION_BYTES];
  static uint8_t payload[FUZZ_MAX_REGION_BYTES];
  fuzz_core *c = fuzz_core_get();
  fuzz_reader r;
  int ops = 0;

  if (!c->api) return 0;

  reset_controls(c);
  fuzz_reader_init(&r, data, size);

  /* Primeros bytes: las suscripciones. Gran parte de la logica de
     `write_control` depende de ellas; dejarlas fijas dejaria la mitad del
     arbol de decision sin explorar. */
  c->api->set_subscriptions(fuzz_u32(&r) & AYTHER_SUB_ALL);

  while (fuzz_more(&r) && ops < 64)
  {
    ayther_region_info_v1 info;
    uint32_t region = fuzz_u8(&r) % (AYTHER_REGION_COUNT + 2u); /* +2: ids invalidos */
    uint32_t offset = fuzz_u32(&r);
    uint32_t count  = fuzz_u32(&r);
    int32_t st, qst;
    uint32_t i, snap = 0;

    ++ops;

    memset(&info, 0, sizeof(info));
    qst = c->api->query_region(region, &info, sizeof(info));

    /* La carga sale del propio buffer del fuzzer; si se acabo, ceros. */
    if (count > sizeof(payload)) count %= (sizeof(payload) + 1u);
    for (i = 0; i < count; ++i) payload[i] = (uint8_t)fuzz_u8(&r);

    /* Foto previa de la region, cuando entra en el buffer. */
    if (qst == AYTHER_STATUS_OK && info.byte_size &&
        info.byte_size <= sizeof(before))
    {
      if (c->api->read_region(region, 0, before, info.byte_size,
                              AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OK)
        snap = info.byte_size;
    }

    st = c->api->write_control(region, offset, payload, count,
                               AYTHER_GENERATION_ANY, NULL);

    if (!status_is_documented(st)) {
      fprintf(stderr, "status no documentado: %d (region %u off %u n %u)\n",
              (int)st, region, offset, count);
      abort();
    }

    /* Invariante 2: rechazo => la region quedo intacta. */
    if (st != AYTHER_STATUS_OK && snap)
    {
      if (c->api->read_region(region, 0, after, snap,
                              AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OK &&
          memcmp(before, after, snap) != 0)
      {
        fprintf(stderr, "escritura RECHAZADA (%d) que igual modifico la region %u\n",
                (int)st, region);
        abort();
      }
    }
  }

  /* Invariante 3: el core sigue vivo despues de la secuencia. */
  c->run();

  reset_controls(c);
  c->api->set_subscriptions(0);
  return 0;
}
