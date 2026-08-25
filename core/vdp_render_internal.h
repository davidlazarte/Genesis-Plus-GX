/* Estado interno del renderer, compartido SOLO entre `vdp_render.c` y el
 * recompositor del fork (`core/ayther/ayther_core.c`).  (#43 punto 2)
 *
 * Por que existe. `ayther_core.c` -- 700 lineas, el recompositor completo--
 * entraba al build con un `#include "ayther/ayther_core.c"` al final de
 * `vdp_render.c`. Funcionaba, y era la unica forma de alcanzar los estaticos
 * del renderer, pero costaba tres cosas que no se ven hasta que duelen:
 *
 *   - el archivo no aparecia en el grafo de `make`: cambiarlo no invalidaba
 *     nada, y compilarlo o testearlo aislado era imposible;
 *   - cualquier error suyo se reportaba con la linea de `vdp_render.c`;
 *   - `vdp_render.c` -- ya el archivo mas divergente de upstream-- crecia 700
 *     lineas mas en cada diff de rebase, por codigo que no es de upstream.
 *
 * Que NO es este header. No es una API: nada de afuera del renderer puede
 * incluirlo. Lo que el frontend consume vive en `ayther_api.h` y en las
 * funciones de `vdp_render.h`. Esto es el contrato entre dos archivos que
 * comparten un buffer, escrito para que ese contrato sea explicito en vez de
 * conseguirse pegando un `.c` adentro de otro.
 *
 * Requiere haber incluido `shared.h` antes: los tipos y los defines de formato
 * de pixel vienen de ahi.
 */

#ifndef _VDP_RENDER_INTERNAL_H_
#define _VDP_RENDER_INTERNAL_H_

/* Tipo del pixel de salida. Vivia en vdp_render.c; lo necesitan los dos. */
#if defined(USE_8BPP_RENDERING)
#define PIXEL_OUT_T uint8
#elif defined(USE_32BPP_RENDERING)
#define PIXEL_OUT_T uint32
#else
#define PIXEL_OUT_T uint16
#endif

/* Un sprite ya parseado de la SAT para la linea en curso. */
typedef struct
{
  uint16 ypos;
  uint16 xpos;
  uint16 attr;
  uint16 size;
} object_info_t;

/* AYTHER (#270): 80 en vez de MAX_SPRITES_PER_LINE (20) -- headroom para la
   recomposicion con el limite de sprites desactivado. El render normal sigue
   acotado por `max`, no por la capacidad del array. */
#ifdef AYTHER_EXTENSIONS
#define AYTHER_OBJ_INFO_SLOTS 80
#else
#define AYTHER_OBJ_INFO_SLOTS MAX_SPRITES_PER_LINE
#endif

/* Recorte de Window y plano A por columna: [0] es plano A, [1] es Window, y
   ocupan rangos de x disjuntos en la linea. El recompositor los pisa cuando
   desactiva la ventana y los restaura al salir. */
struct clip_t
{
  uint8 left;
  uint8 right;
  uint8 enable;
};
extern struct clip_t clip[2];

/* Buffers de linea: [0] es plano B y despues el fondo fusionado + sprites;
   [1] es plano A y Window. El recompositor los pisa y los restaura. */
extern uint8 linebuf[2][0x200];

/* Tabla de color resuelta al formato del build, con S/H ya aplicado. */
extern PIXEL_OUT_T pixel[0x100];

/* Lista de sprites por linea y su cuenta, mas el flag de mascara. */
extern object_info_t obj_info[2][AYTHER_OBJ_INFO_SLOTS];
extern uint8 object_count[2];
extern uint8 spr_ovr;

#ifdef AYTHER_EXTENSIONS
/* Flags de recomposicion (#270): sin limite de sprites por linea y sin la
   mascara de x=0. Solo los toca el recompositor, y los restaura al salir. */
extern uint8 ayther_rc_nolimit;
extern uint8 ayther_rc_nomask;
#endif

#endif /* _VDP_RENDER_INTERNAL_H_ */
