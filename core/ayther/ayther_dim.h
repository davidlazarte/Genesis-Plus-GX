/* AYTHER (#31): un cuarto de brillo por canal, en el formato de píxel que el
 * build emite.
 *
 * Estaba escrito a mano en `remap_line` con máscaras RGB565. Ése es el formato
 * del perfil del fork —USE_16BPP_RENDERING + FRONTEND_SUPPORTS_RGB565— y basura
 * silenciosa para cualquier otro: bajo 15BPP los canales viven en otros bits, y
 * esas máscaras no daban "más oscuro" sino OTRO color, sin que nada lo dijera.
 *
 * Vive en un header propio para que el test pueda verificar los valores sin
 * levantar el core entero: la afirmación "el 25% ±1 LSB por canal" es
 * aritmética pura y no necesita un emulador para comprobarse.
 *
 * 8BPP queda afuera a propósito: con 3-3-2 bits por canal un cuarto de brillo
 * deja el azul en cero y el resto en un solo nivel, así que sería un recorte y
 * no una atenuación. Si alguna vez ese formato entra, esto tiene que romper la
 * compilación en vez de mentir.
 */

#ifndef AYTHER_DIM_H
#define AYTHER_DIM_H

#if defined(USE_8BPP_RENDERING)

#define AYTHER_DIM_QUARTER(p) AYTHER_DIM_UNSUPPORTED_PIXEL_FORMAT

#elif defined(USE_15BPP_RENDERING)
/* 1-5-5-5; el bit 15 se conserva. El orden R/B depende de LSB_FIRST, pero el
   cuarteo es simétrico entre canales y no necesita distinguirlos. */
#define AYTHER_DIM_QUARTER(p)                                             \
  ((PIXEL_OUT_T)(((p) & 0x8000u)                                          \
    | (((((p) >> 10) & 0x1Fu) >> 2) << 10)                                \
    | (((((p) >>  5) & 0x1Fu) >> 2) <<  5)                                \
    |  ((((p)        & 0x1Fu) >> 2))))

#elif defined(USE_32BPP_RENDERING)
/* 8-8-8-8 con alfa en los bits altos, que se conserva. */
#define AYTHER_DIM_QUARTER(p)                                             \
  ((PIXEL_OUT_T)(((p) & 0xFF000000u)                                      \
    | (((((p) >> 16) & 0xFFu) >> 2) << 16)                                \
    | (((((p) >>  8) & 0xFFu) >> 2) <<  8)                                \
    |  ((((p)        & 0xFFu) >> 2))))

#else
/* 5-6-5 */
#define AYTHER_DIM_QUARTER(p)                                             \
  ((PIXEL_OUT_T)((((((p) >> 11) & 0x1Fu) >> 2) << 11)                     \
    | (((((p) >>  5) & 0x3Fu) >> 2) <<  5)                                \
    |  ((((p)        & 0x1Fu) >> 2))))

#endif

#endif /* AYTHER_DIM_H */
