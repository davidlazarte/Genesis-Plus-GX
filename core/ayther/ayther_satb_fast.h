/* El parser rapido de la SAT, para cuando no hay nada que observar ni suprimir.  (#43 punto 4)
 *
 * Esto vivia adentro de `vdp_render.c`, el archivo mas divergente de upstream
 * y el que hay que releer entero en cada rebase. Se incluye en el MISMO punto
 * en que estaba, asi que el orden de declaraciones es identico y el codigo
 * generado tambien: lo unico que cambia es que deja de aparecer en el diff
 * con upstream, donde nunca tuvo nada que hacer.
 *
 * NO es un header reutilizable: se incluye UNA sola vez, desde vdp_render.c.
 */

#ifndef AYTHER_SATB_FAST_H
#define AYTHER_SATB_FAST_H

#ifdef AYTHER_EXTENSIONS
/* Stock Mode 5 parser selected once per scanline when sprite observation and
   render controls are idle. It avoids subscription/suppression branches in
   the SAT chain loop. */
static void parse_satb_m5_fast(int line, int im2)
{
  int ypos;
  int height;
  int size;
  int link = 0;
  int count = 0;
  int total = max_sprite_pixels >> 2;
  uint16 *p = (uint16 *)&vram[satb];
  uint16 *q = (uint16 *)&sat[0];
  object_info_t *object_info = obj_info[(line + 1) & 1];

  line = im2 ? (((line + 0x81) << 1) + odd_frame) : (line + 0x81);
  do
  {
    ypos = q[link] & (im2 ? 0x3FF : 0x1FF);
    if (line >= ypos)
    {
      size = q[link + 1] >> 8;
      height = (im2 ? 16 : 8) +
        ((size & 3) << (im2 ? 4 : 3));
      ypos = line - ypos;
      if (ypos < height)
      {
        if (count == MODE5_MAX_SPRITES_PER_LINE)
        {
          status |= 0x40;
          break;
        }
        object_info->attr = p[link + 2];
        object_info->xpos = p[link + 3] & 0x1ff;
        object_info->ypos = ypos;
        object_info->size = size & 0x0f;
        ++count;
        ++object_info;
      }
    }
    link = (q[link + 1] & 0x7F) << 2;
    if ((link == 0) || (link >= bitmap.viewport.w)) break;
  }
  while (--total);
  object_count[im2 ? ((line >> 1) & 1) : (line & 1)] = count;
}
#endif
#endif /* AYTHER_SATB_FAST_H */
