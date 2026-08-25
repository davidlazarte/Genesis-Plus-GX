/* AYTHER (#31/#37/#41): "¿el píxel que se ve acá lo puso el sprite?"
 *
 * Se contestaba comparando `linebuf[0]` antes y después de `render_obj`. Esa
 * vía da dos respuestas malas, y las dos importan:
 *
 *   - un píxel de sprite cuyo byte COINCIDE con el del fondo —mismo índice de
 *     color, misma prioridad— no aparece en el diff. El `layer_dim` lo dejaba a
 *     brillo pleno y la atribución no lo marcaba: un agujero adentro del sprite;
 *   - un píxel que sólo cambió por el operador de shadow/highlight sí aparece, y
 *     se marcaba como sprite. No lo es: no pone color, cambia el brillo del que
 *     está abajo.
 *
 * La respuesta correcta no se deduce del resultado: se deduce de la REGLA. Ésta
 * es la misma que aplica `make_lut_bgobj` —la LUT de upstream que resuelve
 * sprite contra fondo—, escrita como predicado:
 *
 *     el sprite tiene color            s != 0
 *     y una de tres:
 *       el sprite tiene prioridad      sp
 *       el fondo no la tiene          !bp
 *       el fondo es transparente       b == 0
 *
 * Vive en un header propio para que un test la pueda comparar contra una copia
 * de `make_lut_bgobj`, píxel por píxel, sobre las 65536 combinaciones posibles.
 * Replicar una regla sin una prueba de que la réplica coincide es exactamente
 * cómo se separa una copia del original.
 *
 * Los operadores de shadow/highlight (paleta 3, índices 14 y 15) se excluyen
 * aparte: para la LUT son un color como cualquier otro, pero para el consumidor
 * no son un sprite.
 */

#ifndef AYTHER_SPRITE_PX_H
#define AYTHER_SPRITE_PX_H

/* Byte del sprite: d0-d3 índice, d4-d5 paleta, d6 prioridad.
   Byte de abajo: lo que ya había en el line buffer, mismo layout. */
#define AYTHER_SPRITE_WINS(sprite_byte, under_byte)                       \
  (((sprite_byte) & 0x0F) &&                                              \
   (((sprite_byte) & 0x40) ||                                             \
    !((under_byte) & 0x40) ||                                             \
    !((under_byte) & 0x0F)))

/* Un operador de S/H no es color: paleta 3 con índice 14 o 15. */
#define AYTHER_SPRITE_IS_OPERATOR(sprite_byte)                            \
  (((sprite_byte) & 0x3F) >= 0x3E)

#endif /* AYTHER_SPRITE_PX_H */
