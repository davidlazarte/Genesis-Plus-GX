/* #34: cargar un cartucho corrompido.
 *
 * `retro_load_game` mira el header, decide region, mapper y tamanio, y despues
 * el 68000 ejecuta lo que haya en los vectores. Todo eso son datos de un
 * archivo que el usuario eligio, y el core no puede confiar en ninguno.
 *
 * Igual que en unserialize, la entrada es una lista de mutaciones sobre el ROM
 * sintetico y no un ROM al azar: partir del vacio gastaria el presupuesto en
 * llegar a "esto parece un cartucho de Mega Drive" en vez de en explorar que
 * pasa cuando el header dice una cosa y el contenido otra.
 *
 * Invariantes: cargar no crashea, y si la carga tiene exito el core aguanta 30
 * frames. Aceptar un cartucho y morir al tercer frame es peor que rechazarlo.
 *
 * Este target descarga y recarga el juego en cada entrada, asi que es el mas
 * lento de los cinco. Es inevitable: lo que se prueba es justamente la carga.
 */

#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  static uint8_t rom[AYTHER_GENERATED_ROM_SIZE];
  fuzz_core *c = fuzz_core_get();
  struct retro_game_info gi;
  fuzz_reader r;
  int muts = 0, i;

  if (!ayther_build_generated_rom(rom, sizeof(rom))) return 0;

  fuzz_reader_init(&r, data, size);
  while (fuzz_more(&r) && muts < 512)
  {
    uint32_t off = fuzz_u32(&r) % (uint32_t)sizeof(rom);
    rom[off] = (uint8_t)fuzz_u8(&r);
    ++muts;
  }

  c->unload_game();

  memset(&fuzz_gi_ext, 0, sizeof(fuzz_gi_ext));
  fuzz_gi_ext.full_path = "ayther-fuzz.md"; fuzz_gi_ext.dir = ".";
  fuzz_gi_ext.name = "ayther-fuzz"; fuzz_gi_ext.ext = "md";
  fuzz_gi_ext.data = rom; fuzz_gi_ext.size = sizeof(rom);
  fuzz_gi_ext.persistent_data = true;
  memset(&gi, 0, sizeof(gi));
  gi.path = "ayther-fuzz.md"; gi.data = rom; gi.size = sizeof(rom);

  if (c->load_game(&gi))
    for (i = 0; i < 30; ++i) c->run();

  /* Se restaura el cartucho sano: la proxima entrada tiene que partir del
     mismo lugar que esta. */
  c->unload_game();
  ayther_build_generated_rom(fuzz_rom, sizeof(fuzz_rom));
  fuzz_gi_ext.data = fuzz_rom; fuzz_gi_ext.size = sizeof(fuzz_rom);
  gi.data = fuzz_rom; gi.size = sizeof(fuzz_rom);
  if (!c->load_game(&gi)) {
    fprintf(stderr, "el ROM sano dejo de cargar despues de una mutacion\n");
    abort();
  }
  return 0;
}
