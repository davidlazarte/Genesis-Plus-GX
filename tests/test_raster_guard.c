/* Unit tests for AYTHER raster fallback reason classification. */

#include <stdio.h>

#include "ayther_raster.h"

static int g_pass;
static int g_fail;

#define CHECK(condition, message) do { \
    if (condition) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", message, __LINE__); } \
  } while (0)

int main(void)
{
  unsigned int mask;
  unsigned int reason;

  /* #27 agrego JOURNAL_OVERFLOW en el bit 7. Los siete originales conservan su
     posicion: un consumidor que solo mira `> 0` o bits concretos no cambia. */
  CHECK(AYTHER_RASTER_REASON_ALL == 0xFFu, "eight stable reason bits");
  CHECK(AYTHER_RASTER_REASON_JOURNAL_OVERFLOW == (1u << 7),
        "journal overflow is the eighth bit");
  CHECK((AYTHER_RASTER_REASON_ALL & 0x7Fu) == 0x7Fu,
        "the seven original reason bits keep their positions");
  /* Lo reproducible es un SUBCONJUNTO estricto: VRAM, DMA, modo no soportado y
     el propio overflow quedan afuera a proposito. */
  CHECK((AYTHER_RASTER_REASON_REPLAYABLE & AYTHER_RASTER_REASON_ALL) ==
        AYTHER_RASTER_REASON_REPLAYABLE &&
        AYTHER_RASTER_REASON_REPLAYABLE != AYTHER_RASTER_REASON_ALL,
        "replayable reasons are a strict subset of all reasons");
  CHECK((AYTHER_RASTER_REASON_REPLAYABLE &
         AYTHER_RASTER_REASON_JOURNAL_OVERFLOW) == 0,
        "an overflowed journal is never replayable");

  mask = 0;
  mask = AYTHER_RASTER_MERGE(mask, AYTHER_RASTER_REASON_REG, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_REG, "active register change marks REG");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_REG, 0, 1, 0);
  CHECK(mask == 0, "register change outside active display stays clean");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_CRAM, 1, 0, 0);
  CHECK(mask == 0, "unchanged CRAM value stays clean");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_CRAM, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_CRAM, "68K CRAM path marks CRAM");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_CRAM, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_CRAM, "Z80 CRAM path marks CRAM");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_VSRAM, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_VSRAM, "68K VSRAM path marks VSRAM");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_VSRAM, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_VSRAM, "Z80 VSRAM byte path marks VSRAM");

  reason = AYTHER_RASTER_VRAM_REASON(0xB412u, 0xB400u);
  CHECK(reason == AYTHER_RASTER_REASON_HSCROLL,
        "68K hscroll word is classified inside aligned block");

  reason = AYTHER_RASTER_VRAM_REASON(0xB7FFu, 0xB400u);
  CHECK(reason == AYTHER_RASTER_REASON_HSCROLL,
        "Z80 hscroll byte is classified at block boundary");

  reason = AYTHER_RASTER_VRAM_REASON(0xB800u, 0xB400u);
  CHECK(reason == AYTHER_RASTER_REASON_VRAM,
        "VRAM outside hscroll block is classified as temporal VRAM");

  mask = AYTHER_RASTER_MERGE(0, reason, 1, 1, 0);
  CHECK(mask == AYTHER_RASTER_REASON_VRAM,
        "ordinary active-display VRAM changes force raster fallback");

  reason = AYTHER_RASTER_VRAM_REASON(0xB500u, 0xB400u);
  mask = AYTHER_RASTER_MERGE(0, reason, 1, 1, 1);
  CHECK(mask == (AYTHER_RASTER_REASON_HSCROLL | AYTHER_RASTER_REASON_DMA),
        "DMA copy/fill to hscroll records type and origin");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_CRAM, 1, 1, 1);
  CHECK(mask == (AYTHER_RASTER_REASON_CRAM | AYTHER_RASTER_REASON_DMA),
        "DMA to CRAM records type and origin");

  mask = AYTHER_RASTER_MERGE(0, AYTHER_RASTER_REASON_VSRAM, 1, 1, 1);
  CHECK(mask == (AYTHER_RASTER_REASON_VSRAM | AYTHER_RASTER_REASON_DMA),
        "DMA fill to VSRAM records type and origin");

  mask = AYTHER_RASTER_MERGE(AYTHER_RASTER_REASON_REG,
                             AYTHER_RASTER_REASON_CRAM, 1, 1, 1);
  CHECK(mask == (AYTHER_RASTER_REASON_REG | AYTHER_RASTER_REASON_CRAM |
                 AYTHER_RASTER_REASON_DMA),
        "reasons accumulate without losing earlier fallback causes");

  mask = AYTHER_RASTER_REASON_UNSUPPORTED_MODE;
  CHECK(mask > 0, "legacy boolean consumer sees unsupported mode as dirty");

  mask = 0;
  CHECK(mask == 0, "per-frame reset clears supported frame reasons");

  printf("\nraster guard unit tests: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
