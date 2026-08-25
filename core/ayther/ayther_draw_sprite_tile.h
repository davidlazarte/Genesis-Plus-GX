/* DRAW_SPRITE_TILE con el bit de sprite exacto, para el clon observado.  (#43 punto 4)
 *
 * Esto vivia adentro de `vdp_render.c`, el archivo mas divergente de upstream
 * y el que hay que releer entero en cada rebase. Se incluye en el MISMO punto
 * en que estaba, asi que el orden de declaraciones es identico y el codigo
 * generado tambien: lo unico que cambia es que deja de aparecer en el diff
 * con upstream, donde nunca tuvo nada que hacer.
 *
 * NO es un header reutilizable: se incluye UNA sola vez, desde vdp_render.c.
 */

#ifndef AYTHER_DRAW_SPRITE_TILE_H
#define AYTHER_DRAW_SPRITE_TILE_H

/* AYTHER (#31/#37/#41): DRAW_SPRITE_TILE con el bit de sprite EXACTO.

   `ayther_observed` es un parametro del clon, no una variable: en el clon
   rapido es la constante 0 y el compilador borra la rama entera, asi que el
   perfil sin subscribers no paga ni una comparacion por pixel.

   La regla es la misma que aplica la LUT: gana el sprite si tiene prioridad, o
   si lo que ya estaba no la tiene. Se replica en vez de comparar el resultado
   contra el valor anterior porque dos capas pueden dar el MISMO byte, y ahi
   comparar contesta cualquier cosa -- que es justo el defecto que esto arregla.

   El store es un OR y no una asignacion: los sprites se dibujan en orden, y un
   sprite posterior que PIERDE contra uno anterior no puede borrar la marca del
   que gano. */
#define AYTHER_DRAW_SPRITE_TILE(WIDTH,ATTR,TABLE)  \
  for (i=0;i<WIDTH;i++) \
  { \
    temp = *src++; \
    if (temp & 0x0f) \
    { \
      uint32 ayther_under = lb[i]; \
      temp |= (ayther_under << 8); \
      lb[i] = TABLE[temp | ATTR]; \
      status |= ((temp & 0x8000) >> 10); \
      if (ayther_observed) \
      { \
        uint32 ayther_sb = (ATTR) | (temp & 0x0F); \
        spx[i] |= (uint8)(AYTHER_SPRITE_WINS(ayther_sb, ayther_under) && \
                          !(ayther_sh && AYTHER_SPRITE_IS_OPERATOR(ayther_sb))); \
      } \
    } \
  }
#endif /* AYTHER_DRAW_SPRITE_TILE_H */
