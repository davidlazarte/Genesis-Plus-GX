/* Contadores de instrumentación del fork (#36).
 *
 * La promesa central del fork es "cero trabajo sin subscribers". Hasta acá esa
 * frase descansaba en leer el código y en un bench cuyo ruido (±3% medido) es
 * del mismo orden que lo que se quiere demostrar. Un bench no puede distinguir
 * "no hace nada" de "hace poco": para eso hace falta CONTAR.
 *
 * Estos contadores existen sólo con -DAYTHER_METRICS y no entran en ningún
 * build publicado. Con extensions compilado y cero suscripciones tienen que dar
 * exactamente 0 — no "casi 0" —, y eso sí es una afirmación falsable.
 */

#ifndef AYTHER_METRICS_H
#define AYTHER_METRICS_H

#include <stdint.h>

#ifdef AYTHER_METRICS

typedef struct ayther_metrics_v1
{
  uint32_t struct_size;
  uint32_t reserved0;
  /* Marcas al bitmap de patterns sucios de VRAM. Una por escritura a VRAM que
     toque un pattern, si el gate de suscripción deja pasar. */
  uint64_t vram_dirty_marks;
  /* Entradas al preámbulo AYTHER de cada frame. */
  uint64_t begin_frame_calls;
  /* Líneas en que parse_satb_m5 tomó el parser completo en vez del rápido. */
  uint64_t satb_slow_path;
  /* Bytes copiados por el path activo del frame delta. */
  uint64_t frame_delta_bytes;
} ayther_metrics_v1;

extern ayther_metrics_v1 ayther_metrics;

#define AYTHER_METRIC_INC(field)      (ayther_metrics.field++)
#define AYTHER_METRIC_ADD(field, n)   (ayther_metrics.field += (uint64_t)(n))

#else

#define AYTHER_METRIC_INC(field)      ((void)0)
#define AYTHER_METRIC_ADD(field, n)   ((void)0)

#endif /* AYTHER_METRICS */

#endif /* AYTHER_METRICS_H */
