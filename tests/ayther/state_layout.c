/* #45: que estructura serializada diverge entre plataformas.
 *
 * El issue pregunta por que `state_hash` difiere entre Linux y Windows, y
 * sugiere mirar "padding en las estructuras serializadas". Que divergen ya esta
 * probado: el layout id que imprime state_guard da 0x7e56354f en Windows x64 y
 * 0x670dec82 en Linux x64 -los DOS de 64 bits-. Lo que faltaba es cual.
 *
 * Esto lo contesta imprimiendo los sizeof de los structs que `state_save`
 * vuelca enteros. Corriendolo en los dos jobs del CI, la comparacion es leer
 * dos logs.
 *
 * Solo usa printf a proposito: shared.h arrastra el VFS de libretro, que
 * redefine FILE, fprintf y compania a las variantes RFILE. printf no esta en
 * esa lista; fprintf si, y por eso este archivo no la usa.
 */
#include <stdio.h>
#include "shared.h"
#include "ym3438.h"
#include "opll.h"
#include "ssp16.h"

#define P(t) printf("  %-24s %6u\n", #t, (unsigned)sizeof(t))

int main(void)
{
  printf("layout de structs serializados (puntero %u bits)\n",
         (unsigned)(sizeof(void *) * 8));
  P(void *);
  P(Z80_Regs);
  P(m68ki_cpu_core);
  P(cpu_memory_map);
  P(cart_hw_t);
  P(md_cart_t);

  /* Los chips de sonido y el SVP tambien se vuelcan enteros con sizeof, en
     sound_context_save y md_cart_context_save. Sin estos la respuesta seria
     parcial: alcanza UNO distinto para explicar la divergencia. */
  P(ym3438_t);
  P(opll_t);
  P(ssp1601_t);
  return 0;
}
