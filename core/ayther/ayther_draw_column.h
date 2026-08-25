/* La columna de fondo con supresion de tiles por plano (id 0x105).  (#43 punto 4)
 *
 * Esto vivia adentro de `vdp_render.c`, el archivo mas divergente de upstream
 * y el que hay que releer entero en cada rebase. Se incluye en el MISMO punto
 * en que estaba, asi que el orden de declaraciones es identico y el codigo
 * generado tambien: lo unico que cambia es que deja de aparecer en el diff
 * con upstream, donde nunca tuvo nada que hacer.
 *
 * NO es un header reutilizable: se incluye UNA sola vez, desde vdp_render.c.
 */

#ifndef AYTHER_DRAW_COLUMN_H
#define AYTHER_DRAW_COLUMN_H

/* AYTHER fork delta: dibuja una columna de 2 celdas igual que DRAW_COLUMN, pero
   saltea (deja transparente, color 0) las celdas cuyo patrón+paleta esté en la
   máscara de supresión por-plano `ps` (id 0x105). `ps == NULL` → camino idéntico a
   DRAW_COLUMN (sin costo para los demás juegos). Respeta el orden de dibujo
   (LSB/MSB) según el endianness, igual que DRAW_COLUMN. Devuelve el `dst` avanzado. */
#ifdef AYTHER_EXTENSIONS
#ifdef ALIGN_LONG
#define AYTHER_PUT0()  do { WRITE_LONG(dst, 0); dst++; WRITE_LONG(dst, 0); dst++; } while (0)
#define AYTHER_PUTS()  do { WRITE_LONG(dst, src[0] | atex); dst++; WRITE_LONG(dst, src[1] | atex); dst++; } while (0)
#else
#define AYTHER_PUT0()  do { *dst++ = 0; *dst++ = 0; } while (0)
#define AYTHER_PUTS()  do { *dst++ = (src[0] | atex); *dst++ = (src[1] | atex); } while (0)
#endif
INLINE uint32 *ayther_draw_col(uint32 *dst, uint32 atbuf, uint32 v_line, const uint8 *ps)
{
  uint32 atex, *src;
#ifdef LSB_FIRST
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
#else
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
#endif
  return dst;
}
/* Atajo: usa el fast path (DRAW_COLUMN) cuando no hay supresión en este plano. */
#define DRAW_COLUMN_AE(ATTR, LINE, PS) \
  do { if (PS) dst = ayther_draw_col(dst, (ATTR), (LINE), (PS)); else { DRAW_COLUMN((ATTR), (LINE)) } } while (0)

/* AYTHER (#28): el mismo dibujo de columna para interlace mode 2. Los
   renderers de im2 usan su propio par de getters -el patrón se indexa distinto
   porque cada tile guarda dos campos-, así que no alcanzaba con reutilizar
   `ayther_draw_col`: sin esta variante, la supresión de tiles de plano
   simplemente no existía en im2. La clave de supresión es la misma (patrón +
   paleta), para que el frontend no tenga que saber en qué modo está el VDP. */
INLINE uint32 *ayther_draw_col_im2(uint32 *dst, uint32 atbuf, uint32 v_line,
                                   const uint8 *ps)
{
  uint32 atex, *src;
#ifdef LSB_FIRST
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
#else
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
#endif
  return dst;
}
#define DRAW_COLUMN_IM2_AE(ATTR, LINE, PS) \
  do { if (PS) dst = ayther_draw_col_im2(dst, (ATTR), (LINE), (PS)); \
       else { DRAW_COLUMN_IM2((ATTR), (LINE)) } } while (0)
#else
#define DRAW_COLUMN_AE(ATTR, LINE, PS) DRAW_COLUMN((ATTR), (LINE))
#define DRAW_COLUMN_IM2_AE(ATTR, LINE, PS) DRAW_COLUMN_IM2((ATTR), (LINE))
#endif
#endif /* AYTHER_DRAW_COLUMN_H */
