/* #34: `retro_unserialize` con savestates corrompidos.
 *
 * La otra superficie por la que entran bytes que el core no eligio. Un
 * savestate viene de un archivo: puede estar truncado, ser de otra version del
 * core, de otro juego, o directamente estar podrido.
 *
 * La entrada del fuzzer NO es el savestate entero. Es una lista de mutaciones
 * (offset, byte) que se aplican sobre un estado VALIDO recien serializado. La
 * diferencia importa: un estado valido son ~1 MB, y arrancar de bytes al azar
 * gastaria todo el presupuesto del fuzzer en descubrir el encabezado en vez de
 * en explorar que pasa cuando UN campo esta mal. El corpus queda de decenas de
 * bytes en vez de decenas de megas.
 *
 * Invariantes:
 *
 *   1. `unserialize` nunca crashea, acepte o rechace.
 *   2. Si ACEPTA, el core tiene que poder seguir corriendo frames. Aceptar un
 *      estado y despues morir es peor que rechazarlo: el frontend ya siguio.
 *   3. Un tamanio distinto del declarado se rechaza siempre.
 */

#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  static uint8_t *state;
  static size_t state_size;
  fuzz_core *c = fuzz_core_get();
  fuzz_reader r;
  int muts = 0;

  if (!state)
  {
    state_size = c->serialize_size();
    if (!state_size) return 0;
    state = (uint8_t *)malloc(state_size);
    if (!state) return 0;
  }

  /* Estado limpio y valido en cada entrada: sin esto la mutacion N se aplica
     sobre el resultado de la N-1 y el hallazgo no se reproduce solo. */
  c->run();
  if (!c->serialize(state, state_size)) return 0;

  fuzz_reader_init(&r, data, size);
  while (fuzz_more(&r) && muts < 256)
  {
    uint32_t off = fuzz_u32(&r) % (uint32_t)state_size;
    state[off] = (uint8_t)fuzz_u8(&r);
    ++muts;
  }

  /* Invariante 3: el tamanio es parte del contrato, no una sugerencia. */
  if (state_size > 1 && c->unserialize(state, state_size - 1)) {
    fprintf(stderr, "unserialize acepto un tamanio incorrecto\n");
    abort();
  }

  if (c->unserialize(state, state_size))
  {
    /* Invariante 2: si lo acepto, se banca correr. */
    c->run();
    c->run();
    c->run();
  }

  /* Sea cual sea el resultado, el core queda utilizable para la proxima
     entrada: se restaura un estado sano. */
  if (c->serialize(state, state_size))
    c->unserialize(state, state_size);
  return 0;
}
