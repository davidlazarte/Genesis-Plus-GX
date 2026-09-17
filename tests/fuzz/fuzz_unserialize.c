/* #34: `retro_unserialize` con savestates corrompidos.
 *
 * La otra superficie por la que entran bytes que el core no eligio. Un
 * savestate viene de un archivo: puede estar truncado, ser de otra version del
 * core, de otro juego, o directamente estar podrido.
 *
 * La entrada del fuzzer NO es el savestate entero. Es una lista de mutaciones
 * (offset, byte) que se aplican sobre un estado VALIDO. La diferencia importa:
 * un estado valido son ~1 MB, y arrancar de bytes al azar gastaria todo el
 * presupuesto del fuzzer en descubrir el encabezado en vez de en explorar que
 * pasa cuando UN campo esta mal. El corpus queda de decenas de bytes en vez de
 * decenas de megas.
 *
 * Invariantes:
 *
 *   1. `unserialize` nunca crashea, acepte o rechace.
 *   2. Si ACEPTA, el core tiene que poder seguir corriendo frames. Aceptar un
 *      estado y despues morir es peor que rechazarlo: el frontend ya siguio.
 *   3. Un tamanio distinto del declarado se rechaza siempre.
 *   4. Cada entrada arranca del MISMO estado, y el core lo acepta (#108).
 *
 * Sobre la 4, que es la que faltaba. El target cerraba cada entrada
 * "restaurando un estado sano" con serialize + unserialize del estado ACTUAL,
 * que despues de una entrada aceptada y corrida ya podia estar podrido: la
 * corrupcion se arrastraba a la entrada siguiente del mismo proceso, y el
 * `crash-*` que dejaba libFuzzer no reproducia solo. Paso dos veces (#63,
 * #105) y las dos veces hubo que fabricar la version determinista a mano.
 * Encima la referencia se capturaba con run() + serialize() en CADA entrada,
 * asi que ni dos corridas de la misma entrada partian del mismo estado.
 *
 * Ahora el estado inicial se captura UNA vez, no se toca nunca mas, y se
 * restaura ANTES de cada entrada. Que la restauracion tuvo exito se comprueba:
 * si el core rechaza su propio estado valido, la entrada anterior lo dejo
 * inutilizable, y eso es un hallazgo -- se aborta, no se sigue en silencio.
 */

#include "fuzz_common.h"

/* Huella FNV-1a de 64 bits del estado serializado DESPUES de la entrada. Es lo
   que permite AFIRMAR el aislamiento en vez de declararlo: si dos corridas de la
   misma entrada -sola, repetida, o detras de otras- dan la misma huella, el
   resultado no depende de la historia del proceso. La imprime el target y no
   el driver porque es el unico que ve el core; solo cuando AYTHER_FUZZ_DIGEST
   esta en el entorno, para no llenar la salida de libFuzzer. Lo consume
   tests/ci/check_fuzz_isolation.sh. */
static uint64_t fuzz_digest(const uint8_t *p, size_t n)
{
  uint64_t h = 0xcbf29ce484222325ull;
  size_t i;
  for (i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
  return h;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  static uint8_t *golden;      /* el estado inicial: valido, capturado una vez, inmutable */
  static uint8_t *state;       /* copia de trabajo: aca van las mutaciones */
  static size_t state_size;
  static int want_digest;
  fuzz_core *c = fuzz_core_get();
  fuzz_reader r;
  int muts = 0;
  int accepted;

  if (!golden)
  {
    state_size = c->serialize_size();
    if (!state_size) { fprintf(stderr, "serialize_size devolvio 0\n"); exit(2); }
    golden = (uint8_t *)malloc(state_size);
    state  = (uint8_t *)malloc(state_size);
    if (!golden || !state) { fprintf(stderr, "sin memoria para el estado\n"); exit(2); }
    /* Un frame para que el estado tenga algo adentro, y de ahi en adelante
       este blob no cambia. */
    c->run();
    if (!c->serialize(golden, state_size)) {
      fprintf(stderr, "no se pudo capturar el estado inicial\n");
      exit(2);
    }
    want_digest = getenv("AYTHER_FUZZ_DIGEST") != NULL;
  }

  /* Invariante 4: la entrada arranca del estado inicial, siempre el mismo, y
     el core tiene que aceptarlo. Se restaura desde una COPIA para que el
     golden no pueda ser tocado ni por el core ni por las mutaciones. */
  memcpy(state, golden, state_size);
  if (!c->unserialize(state, state_size)) {
    fprintf(stderr, "el core rechazo el estado inicial valido: "
                    "la entrada anterior lo dejo inutilizable\n");
    abort();
  }

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

  accepted = c->unserialize(state, state_size);
  if (accepted)
  {
    /* Invariante 2: si lo acepto, se banca correr. */
    c->run();
    c->run();
    c->run();
  }

  if (want_digest)
  {
    if (!c->serialize(state, state_size)) {
      fprintf(stderr, "serialize fallo despues de la entrada\n");
      abort();
    }
    printf("  digest %016llx %s\n",
           (unsigned long long)fuzz_digest(state, state_size),
           accepted ? "acepto" : "rechazo");
    fflush(stdout);
  }
  return 0;
}
