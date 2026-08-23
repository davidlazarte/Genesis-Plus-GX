/***************************************************************************************
 * AYTHER raster fallback reasons
 *
 * The value exposed through private memory id 0x10E is a bitmask. Legacy
 * consumers that only test `value > 0` remain compatible.
 ****************************************************************************************/

#ifndef _AYTHER_RASTER_H_
#define _AYTHER_RASTER_H_

#define AYTHER_RASTER_REASON_REG              (1u << 0)
#define AYTHER_RASTER_REASON_CRAM             (1u << 1)
#define AYTHER_RASTER_REASON_VSRAM            (1u << 2)
#define AYTHER_RASTER_REASON_HSCROLL          (1u << 3)
#define AYTHER_RASTER_REASON_DMA              (1u << 4)
#define AYTHER_RASTER_REASON_UNSUPPORTED_MODE (1u << 5)
#define AYTHER_RASTER_REASON_VRAM             (1u << 6)
/* #27: el journal se lleno y hubo eventos que no se registraron. Reproducir
   solo un prefijo produce un frame que parece bueno y no lo es, asi que este
   bit obliga al fallback en vez de dejar pasar un exito parcial. */
#define AYTHER_RASTER_REASON_JOURNAL_OVERFLOW (1u << 7)

#define AYTHER_RASTER_REASON_ALL \
  (AYTHER_RASTER_REASON_REG | AYTHER_RASTER_REASON_CRAM | \
   AYTHER_RASTER_REASON_VSRAM | AYTHER_RASTER_REASON_HSCROLL | \
   AYTHER_RASTER_REASON_DMA | AYTHER_RASTER_REASON_UNSUPPORTED_MODE | \
   AYTHER_RASTER_REASON_VRAM | AYTHER_RASTER_REASON_JOURNAL_OVERFLOW)

/* Motivos que el raster replay SI sabe reproducir. Lo que quede fuera de esta
   mascara mantiene el frame en fallback: reproducir la mitad de los eventos da
   una imagen plausible y equivocada, que es peor que declararse incapaz. */
#define AYTHER_RASTER_REASON_REPLAYABLE \
  (AYTHER_RASTER_REASON_REG | AYTHER_RASTER_REASON_CRAM | \
   AYTHER_RASTER_REASON_VSRAM | AYTHER_RASTER_REASON_HSCROLL)

/* The current recompositor treats the complete aligned 1 KiB hscroll block as
   temporal state. This is deliberately conservative and matches its fetch
   range for all horizontal-scroll modes. */
#define AYTHER_RASTER_VRAM_REASON(address, hscroll_base) \
  ((((unsigned int)(address) & 0xFC00u) == \
    ((unsigned int)(hscroll_base) & 0xFC00u)) \
    ? AYTHER_RASTER_REASON_HSCROLL : AYTHER_RASTER_REASON_VRAM)

/* Merge one effective write into an existing frame mask. DMA is an origin bit
   and is therefore combined with the affected memory reason. */
#define AYTHER_RASTER_MERGE(mask, reason, active, changed, from_dma) \
  ((unsigned int)(mask) | \
   (((active) && (changed)) \
    ? ((unsigned int)(reason) | \
       ((from_dma) ? AYTHER_RASTER_REASON_DMA : 0u)) \
    : 0u))

#ifndef __ASSEMBLER__
#include <stdint.h>
typedef struct {
  uint16_t v_counter;
  uint16_t reason; /* e.g. AYTHER_RASTER_REASON_CRAM */
  uint16_t address;
  uint16_t data;
} ayther_raster_event_t;

#define AYTHER_RASTER_JOURNAL_MAX 256
extern ayther_raster_event_t ayther_raster_journal[AYTHER_RASTER_JOURNAL_MAX];
extern int ayther_raster_journal_count;
/* #27: eventos que no entraron por falta de espacio. Se cuenta en vez de
   guardarse solo un bit para que el frontend pueda distinguir "se paso por uno"
   de "este frame es un festival de splits" al dimensionar el journal. */
extern int ayther_raster_journal_dropped;
#endif

#endif /* _AYTHER_RASTER_H_ */
