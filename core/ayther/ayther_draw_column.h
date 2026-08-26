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
#endif /* AYTHER_EXTENSIONS */

/* #68: DRAW_COLUMN y DRAW_COLUMN_IM2 absorben la supresion por plano y el
   registro de celdas, para que los sitios de llamada de upstream vuelvan a ser
   los de upstream. Antes cada uno era `DRAW_COLUMN_AE(atbuf, v_line, psupX);
   AYTHER_CELL_RECORD(atbuf);`: veinte lineas de upstream modificadas, o sea
   veinte hunks en cada rebase.

   El macro se redefine ACA, despues de las cuatro variantes de upstream (que
   quedan intactas y sin uso en este perfil), y lee dos locales con nombre fijo
   que cada renderer declara con AYTHER_COLUMN_LOCALS y fija con
   AYTHER_COLUMN_PLANE antes de dibujar cada plano:

     ayther_psup   la mascara de supresion del plano en curso, o NULL
     ayther_cells  si el registro de celdas (#42.C) esta activo en esta linea

   Locales y no globales a proposito: en el clon rapido (render_bg_m5_impl con
   ayther_observed = 0) las dos valen 0 en tiempo de compilacion y el
   compilador deja exactamente el cuerpo de upstream, igual que hacia
   DRAW_COLUMN_AE con PS = NULL. Un global no se pliega, y eso es una carga y
   un salto por columna en el camino que no debe pagar nada.

   Solo en el renderer por defecto: ALT_RENDERER tiene sus propios render_bg_*
   sin clones ni locales, y con el macro redefinido no compilarian.

   CONTRATO, para quien agregue o toque un render_bg_* de la seccion por
   defecto (lo hace cumplir tests/ci/check_render_gates.sh en cada PR):

     1. Declarar AYTHER_COLUMN_LOCALS(cells) entre las locales, con `cells` =
        `ayther_observed && AYTHER_LINE_CELLS_ACTIVE` en los clones dual path
        y 0 en los renderers que no registran celdas. Sin esto no compila:
        ese modo de falla se cuida solo.
     2. Llamar AYTHER_COLUMN_PLANE(psupX) antes de dibujar cada plano (A, B y
        Window). Sin esto SI compila, y la supresion de ese plano queda en un
        no-op silencioso: por eso el lint lo exige por nombre.
     3. Lo que no debe cambiar: con las dos locales en 0 el macro tiene que
        generar el cuerpo de upstream. Se midio con objdump al introducirlo
        (#69): los clones rapidos de render_bg_m5, _vs y render_line, y los
        renderers im2 e im2_vs, salieron instruccion por instruccion iguales a
        los de antes del plegado. Cualquier cambio en ayther_draw_col, en
        AYTHER_CELL_PUSH o en este macro tiene que volver a medirlo.
     4. render_bg_m5_vs_enhanced declara las locales y NO fija planos: no
        aplica supresion de tiles por diseno (UNSUPPORTED_CONTROLS). Es la
        unica excepcion, y el lint la exige como excepcion.

   Sobre la forma del macro: DRAW_COLUMN se define como bloque `{ }` y no como
   `do { } while (0)` porque upstream lo invoca sin punto y coma, igual que su
   propio macro (que es una secuencia de sentencias). AYTHER_CELL_PUSH si lleva
   `do { } while (0)`: se usa como sentencia normal. */
#if defined(AYTHER_EXTENSIONS) && !defined(ALT_RENDERER)
#define AYTHER_COLUMN_LOCALS(cells) \
  const uint8 *ayther_psup = (const uint8 *)0; \
  const int ayther_cells = (cells)
#define AYTHER_COLUMN_PLANE(ps) (ayther_psup = (ps))

/* El cuerpo de upstream, en dos variantes en vez de cuatro porque AYTHER_PUTS
   ya resuelve ALIGN_LONG. Mismas sentencias, mismo codigo. */
#ifdef LSB_FIRST
#define AYTHER_DRAW_COLUMN_PLAIN(ATTR, LINE) \
  GET_LSB_TILE(ATTR, LINE) AYTHER_PUTS(); GET_MSB_TILE(ATTR, LINE) AYTHER_PUTS();
#define AYTHER_DRAW_COLUMN_IM2_PLAIN(ATTR, LINE) \
  GET_LSB_TILE_IM2(ATTR, LINE) AYTHER_PUTS(); GET_MSB_TILE_IM2(ATTR, LINE) AYTHER_PUTS();
#else
#define AYTHER_DRAW_COLUMN_PLAIN(ATTR, LINE) \
  GET_MSB_TILE(ATTR, LINE) AYTHER_PUTS(); GET_LSB_TILE(ATTR, LINE) AYTHER_PUTS();
#define AYTHER_DRAW_COLUMN_IM2_PLAIN(ATTR, LINE) \
  GET_MSB_TILE_IM2(ATTR, LINE) AYTHER_PUTS(); GET_LSB_TILE_IM2(ATTR, LINE) AYTHER_PUTS();
#endif

/* Como AYTHER_CELL_RECORD pero sin `ayther_observed`, que no existe en los
   renderers que no son clones (im2, vs_enhanced): el gate es `ayther_cells`. */
#define AYTHER_CELL_PUSH(pair) \
  do { if (ayther_cell_dst && *ayther_cell_count < AYTHER_LINE_CELL_COLUMNS) \
         ayther_cell_dst[(*ayther_cell_count)++] = (uint32)(pair); } while (0)

/* Sin `do { } while (0)`: upstream invoca DRAW_COLUMN sin punto y coma. */
#undef DRAW_COLUMN
#undef DRAW_COLUMN_IM2
#define DRAW_COLUMN(ATTR, LINE) { \
  if (ayther_psup) dst = ayther_draw_col(dst, (ATTR), (LINE), ayther_psup); \
  else { AYTHER_DRAW_COLUMN_PLAIN((ATTR), (LINE)) } \
  if (ayther_cells) AYTHER_CELL_PUSH(ATTR); }
#define DRAW_COLUMN_IM2(ATTR, LINE) { \
  (void)ayther_cells; /* los renderers im2 no registran celdas; -Wall no lo ve sin uso */ \
  if (ayther_psup) dst = ayther_draw_col_im2(dst, (ATTR), (LINE), ayther_psup); \
  else { AYTHER_DRAW_COLUMN_IM2_PLAIN((ATTR), (LINE)) } }
#else
#define AYTHER_COLUMN_LOCALS(cells) ((void)0)
#define AYTHER_COLUMN_PLANE(ps) ((void)0)
#endif
#endif /* AYTHER_DRAW_COLUMN_H */
