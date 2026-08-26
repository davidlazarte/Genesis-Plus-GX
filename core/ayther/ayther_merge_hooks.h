/* Los enganches del merge: peel de celdas, atribucion por pixel y bit de sprite exacto.  (#43 punto 4)
 *
 * Esto vivia adentro de `vdp_render.c`, el archivo mas divergente de upstream
 * y el que hay que releer entero en cada rebase. Se incluye en el MISMO punto
 * en que estaba, asi que el orden de declaraciones es identico y el codigo
 * generado tambien: lo unico que cambia es que deja de aparecer en el diff
 * con upstream, donde nunca tuvo nada que hacer.
 *
 * NO es un header reutilizable: se incluye UNA sola vez, desde vdp_render.c.
 */

#ifndef AYTHER_MERGE_HOOKS_H
#define AYTHER_MERGE_HOOKS_H

/* AYTHER: merge que oculta los tiles marcados (id 0x104) revelando lo de atrás.
   Procesa de a una celda (tramo de 8 px alineado al frame) y decide por celda:
     · la celda tiene PRIMER PLANO (algún pixel de A/Window opaco) → es un elemento
       de adelante: se pela A/W (merge con A transparente, `table[(b<<8)|0]`) y se
       revela el PLANO B de atrás, uniforme y limpio (sin inversión en tiles con
       transparencias, p. ej. texto);
     · la celda es PLANO B puro (sin primer plano, un fondo) → se revela el
       BACKDROP (índice 0x40), porque no hay nada debajo de B.
   Así se oculta CUALQUIER tile (de adelante o de fondo) viendo lo que queda detrás.
   La decisión es por tramo de celda-fila (8 px), no por pixel → un fondo de Plano B
   puro (sin primer plano en ninguna fila) queda limpio en backdrop, y un elemento
   de primer plano revela B de forma uniforme. Función aparte (no inline) para no
   inflar el fast path de merge(), que sigue intacto. */
#ifdef AYTHER_EXTENSIONS
static void ayther_peel_merge(uint8 *srca, uint8 *srcb, uint8 *dst, uint8 *table, int width)
{
  int x = 0;
  while (width > 0)
  {
    int fx   = x + ayther_peel_vx;
    int seg  = 8 - (fx & 7);            /* px hasta el próximo borde de celda */
    int fcol = fx >> 3;
    int i;
    if (seg > width) seg = width;
    if (fcol < AYTHER_TILE_COLS && AYTHER_TILE_SUPPRESSED(ayther_peel_row, fcol))
    {
      /* #31: "hay primer plano en esta celda" es OPACIDAD, no "el byte no es
         cero". Un píxel de plano A transparente pero con el bit de prioridad
         puesto vale 0x40, y contaba como primer plano para las ocho columnas de
         la celda. Lo que decide es el índice de color. */
      /* #37 punto 5: el ternario estaba ADENTRO del bucle, y `has_fg` es
         constante para la celda entera. Dos bucles rectos: el branch se decide
         una vez y cada uno queda vectorizable. */
      int has_fg = 0;                   /* ¿algún pixel OPACO de A/W en la celda? */
      for (i = 0; i < seg; i++) if (srca[i] & 0x0F) { has_fg = 1; break; }
      if (has_fg)
        for (i = 0; i < seg; i++) dst[i] = table[(srcb[i] << 8)]; /* revela B */
      else
      {
        const uint8 backdrop = table[0];
        for (i = 0; i < seg; i++) dst[i] = backdrop;
      }
    }
    else
    {
      for (i = 0; i < seg; i++) dst[i] = table[(srcb[i] << 8) | srca[i]];   /* merge normal */
    }
    srca += seg; srcb += seg; dst += seg; x += seg; width -= seg;
  }
}
#endif

#ifdef AYTHER_EXTENSIONS
/* AYTHER (#41): atribución de una línea de fondo, con la MISMA regla que
   `make_lut_bg`: gana A si A tiene prioridad y es opaco; si no, gana B si B
   tiene prioridad y es opaco; si ninguno tiene prioridad, gana A si es opaco.
   Se replica la regla en vez de comparar el resultado contra las fuentes porque
   dos capas pueden dar el mismo byte, y ahí comparar responde cualquier cosa.

   A y Window comparten `linebuf[1]`, pero ocupan rangos de x DISJUNTOS en la
   línea (clip[0] para A, clip[1] para Window), así que distinguirlos es exacto
   y gratis: alcanza con mirar en qué rango cae la columna. */
static void ayther_attrib_bg(const uint8 *srca, const uint8 *srcb,
                             const uint8 *dst, int width, int shadow_mode)
{
  const int w_left  = clip[1].enable ? (clip[1].left  << 4) : 0;
  const int w_right = clip[1].enable ? (clip[1].right << 4) : 0;
  int x;

  for (x = 0; x < width; ++x)
  {
    const uint8 ax = srca[x], bx = srcb[x];
    const int a = ax & 0x0F, b = bx & 0x0F;
    const int ap = ax & 0x40, bp = bx & 0x40;
    const int a_wins = ap ? (a != 0) : (bp ? (b == 0) : (a != 0));
    const uint8 win = a_wins ? ax : bx;
    uint8 attr;

    if (!(win & 0x0F))
    {
      /* Ninguna capa puso color: se ve el backdrop. */
      ayther_attrib_line[x] = AYTHER_ATTRIB_LAYER_BACKDROP << AYTHER_ATTRIB_LAYER_SHIFT;
      continue;
    }

    if (a_wins)
      attr = (uint8)(((x >= w_left && x < w_right) ? AYTHER_ATTRIB_LAYER_WINDOW
                                                   : AYTHER_ATTRIB_LAYER_PLANE_A)
                     << AYTHER_ATTRIB_LAYER_SHIFT);
    else
      attr = (uint8)(AYTHER_ATTRIB_LAYER_PLANE_B << AYTHER_ATTRIB_LAYER_SHIFT);

    if (win & 0x40) attr |= AYTHER_ATTRIB_PRIORITY;
    attr |= (uint8)((((win >> 4) & 3) << AYTHER_ATTRIB_PALETTE_SHIFT) &
                    AYTHER_ATTRIB_PALETTE_MASK);
    /* En modo shadow/highlight el bit 7 del byte fusionado es "intensidad
       completa" (lo pone la LUT cuando alguna capa tenía prioridad); apagado
       significa sombra. El highlight lo introducen los operadores de sprite, en
       la etapa siguiente. */
    if (shadow_mode && !(dst[x] & 0x80))
      attr |= (uint8)(AYTHER_ATTRIB_SH_SHADOW << AYTHER_ATTRIB_SH_SHIFT);

    ayther_attrib_line[x] = attr;
  }
}
#endif

#ifdef AYTHER_EXTENSIONS

/* Fuera de `merge` y sin inlinear a proposito: `merge` es INLINE y se expande en
   cada renderer, asi que meterle este cuerpo adentro engorda todos los sitios de
   llamada aunque la rama no se tome. */
static AYTHER_NOINLINE void ayther_merge_capture(uint8 *srca, uint8 *srcb,
                                                 uint8 *dst, uint8 *table,
                                                 int width)
{
  /* La atribución necesita las DOS fuentes, y el merge las consume in-place
     (dst == srcb en todos los renderers). Se copian antes; sólo la sombra se
     resuelve después, que es lo único que depende del resultado. */
  static uint8 snap_a[0x200], snap_b[0x200];
  const int shadow_mode = (reg[12] & 0x08) != 0;
  memcpy(snap_a, srca, width);
  memcpy(snap_b, srcb, width);
  if (ayther_peel_active) ayther_peel_merge(srca, srcb, dst, table, width);
  else { int i; for (i = 0; i < width; ++i) dst[i] = table[(snap_b[i] << 8) | snap_a[i]]; }
  ayther_attrib_bg(snap_a, snap_b, dst, width, shadow_mode);
}

/* #31/#37/#41: el merge de la capa de sprites (familia S/H) es quien decide si
   el sprite gana, asi que es el unico lugar donde la pregunta tiene respuesta
   exacta ahi. `srca` es la capa de sprites y `srcb` el fondo ya fusionado.

   Los operadores de shadow/highlight -- paleta 3, indices 14 y 15-- NO cuentan:
   no ponen color, modifican el brillo del pixel de abajo. Contarlos como
   sprite es el defecto 2 de #31, y por el diff entraban siempre. */
static AYTHER_NOINLINE void ayther_obj_capture(const uint8 *srca,
                                               const uint8 *srcb, int width)
{
  const int shadow_mode = (reg[12] & 0x08) != 0;
  uint8 *out = &ayther_sprite_px[0x20];
  int x;

  for (x = 0; x < width; ++x)
  {
    const uint8 sx = srca[x], bx = srcb[x];
    const int s = sx & 0x0F, sp = sx & 0x40;
    const int b = bx & 0x0F, bp = bx & 0x40;
    int is_sprite;

    (void)s; (void)sp; (void)b; (void)bp;
    if (shadow_mode && AYTHER_SPRITE_IS_OPERATOR(sx))
      is_sprite = 0;              /* operador S/H: brillo, no color */
    else
      is_sprite = AYTHER_SPRITE_WINS(sx, bx) ? 1 : 0;

    out[x] = (uint8)(is_sprite ? 1 : 0);
  }
  ayther_obj_px_exact = 1;
}
#endif

/* #43.4: los tres enganches que `merge` consulta, en una linea.
 *
 * `merge` es el punto por el que pasan los CINCO renderers Mode 5, y por eso
 * es donde viven las capturas que necesitan las dos fuentes. El bloque estaba
 * escrito adentro de la funcion, en el archivo mas divergente de upstream, para
 * tres condiciones que se evaluan en orden y salen temprano.
 *
 * El orden importa y por eso esta aca escrito una sola vez: la captura de
 * sprites va ANTES -- el merge consume las fuentes in-place, dst == srcb-, y
 * la atribucion tiene que ganarle al peel porque hace el merge ella misma. */
#ifdef AYTHER_EXTENSIONS
#define AYTHER_MERGE_HOOKS(srca, srcb, dst, table, width)                    \
  do {                                                                       \
    if (ayther_obj_pass)                                                     \
      ayther_obj_capture((srca), (srcb), (width));                           \
    if (ayther_attrib_capture)                                               \
    {                                                                        \
      ayther_merge_capture((srca), (srcb), (dst), (table), (width));         \
      return;                                                                \
    }                                                                        \
    if (ayther_peel_active)                                                  \
    {                                                                        \
      ayther_peel_merge((srca), (srcb), (dst), (table), (width));            \
      return;                                                                \
    }                                                                        \
  } while (0)
#else
/* Sin extensiones no hay nada que enganchar: el merge de upstream, tal cual. */
#define AYTHER_MERGE_HOOKS(srca, srcb, dst, table, width) ((void)0)
#endif


#endif /* AYTHER_MERGE_HOOKS_H */
