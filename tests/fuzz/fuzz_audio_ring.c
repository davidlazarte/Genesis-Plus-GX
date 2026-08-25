/* #34: el ring de eventos de audio con secuencias arbitrarias.
 *
 * Este target NO carga el core: compila `audio_probe.c` directamente, igual que
 * `test_audio_probe_concurrency.c`. El ring es un SPSC lock-free, y lo que hay
 * que romper es su protocolo -- indices, wrap-around, descarte por lleno-, no
 * la emulacion.
 *
 * Lo que aporta sobre el test de concurrencia que ya existe: ese empuja un
 * patron FIJO a maxima velocidad y verifica que no se pierdan ni se dupliquen
 * eventos. Este alterna escrituras y lecturas en el orden que decida el fuzzer
 * -- rafagas, pausas, drenajes a mitad, lecturas sin nada que leer-, y ahi es
 * donde vive el bug de indices que un patron regular no toca nunca.
 *
 * Tres invariantes:
 *
 *   1. Nada de UB ni accesos fuera de rango: lo dicen ASan y UBSan.
 *   2. Lo que sale salio en ORDEN y sin inventar: cada evento lleva un contador
 *      en `data`, y el consumidor exige la secuencia. Un ring que entrega
 *      basura sin crashear es justo lo que un test de "no crashea" no ve.
 *   3. Los contadores cierran: entregados + descartados == intentados. Si no
 *      cierran, el ring perdio un evento sin contarlo -- y un frontend que se
 *      fia de `dropped_events` para saber si vio todo estaria equivocado.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "shared.h"      /* el stub de tests/: tipos y globales del emulador */
#include "audio_probe.h"

/* `audio_probe.c` lee un punado de globales del emulador para el contexto que
   reporta (region, hardware, reloj). Contra el stub de tests/ esas globales
   estan declaradas y las define quien linkea, que aca es este archivo: el
   target prueba el RING, no la emulacion, y darle un core entero solo para
   resolver cinco simbolos meteria ruido entre el fuzzer y lo que se rompe. */
ROMINFO rominfo;
uint8 vdp_pal;
uint8 system_hw;
uint32 system_clock;
uint16 lines_per_frame;
void psg_refresh_gain(void) { }

typedef struct { const uint8_t *p; size_t n, i; } rd;
static uint32_t r8(rd *r) { return (r->i < r->n) ? r->p[r->i++] : 0u; }
static int more(const rd *r) { return r->i < r->n; }

#define DRAIN_MAX 64

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  ap_event_t ev[DRAIN_MAX];
  ap_transport_stats_t stats;
  rd r;
  uint32_t pushed = 0, popped = 0, expect = 0;
  int ops = 0;

  audio_probe_reset();
  audio_probe_set_callback(NULL, NULL);
  audio_probe_reset_transport_stats();
  audio_probe_set_enabled(1);

  r.p = data; r.n = size; r.i = 0;

  while (more(&r) && ops < 8192)
  {
    uint32_t op = r8(&r);
    ++ops;

    if ((op & 3u) == 3u)
    {
      /* Leer un lote de tamanio arbitrario. */
      int want = (int)((op >> 2) % (DRAIN_MAX + 1u));
      int got = audio_probe_poll(ev, want);
      int k;
      if (got < 0 || got > want) {
        fprintf(stderr, "poll devolvio %d pidiendo %d\n", got, want);
        abort();
      }
      for (k = 0; k < got; ++k)
      {
        /* Invariante 2: ESTRICTAMENTE CRECIENTE, no contiguo. El ring puede
           descartar por lleno, y eso deja huecos legitimos en la secuencia;
           lo que no puede hacer es entregar algo repetido, desordenado o que
           nunca se escribio. */
        if (ev[k].data < expect || ev[k].data >= pushed) {
          fprintf(stderr, "el ring entrego data=%u fuera de [%u, %u)\n",
                  (unsigned)ev[k].data, (unsigned)expect, (unsigned)pushed);
          abort();
        }
        expect = ev[k].data + 1u;
        ++popped;
      }
    }
    else
    {
      audio_probe_fm_raw(r8(&r), pushed);
      ++pushed;
    }
  }

  /* Drenar lo que quedo. */
  for (;;)
  {
    int got = audio_probe_poll(ev, DRAIN_MAX), k;
    if (got <= 0) break;
    for (k = 0; k < got; ++k)
    {
      if (ev[k].data < expect || ev[k].data >= pushed) {
        fprintf(stderr, "al drenar: data=%u fuera de [%u, %u)\n",
                (unsigned)ev[k].data, (unsigned)expect, (unsigned)pushed);
        abort();
      }
      expect = ev[k].data + 1u;
      ++popped;
    }
  }

  memset(&stats, 0, sizeof(stats));
  audio_probe_get_transport_stats(&stats);
  if (popped + stats.dropped_events != pushed) {
    fprintf(stderr, "los contadores no cierran: push %u pop %u drop %u\n",
            (unsigned)pushed, (unsigned)popped, (unsigned)stats.dropped_events);
    abort();
  }
  return 0;
}
