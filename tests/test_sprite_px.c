/* #31/#37/#41: que la regla de "el sprite gana" sea LA MISMA que aplica la LUT.
 *
 * El bit de sprite se derivaba comparando el line buffer antes y despues de
 * render_obj, y esa via pierde los pixeles de sprite iguales al fondo y marca
 * como sprite los operadores de brillo. La respuesta correcta no sale del
 * resultado sino de la REGLA de prioridad.
 *
 * Replicar una regla sin probar que la replica coincide es exactamente como se
 * separa una copia del original. Asi que este test tiene, al lado, una copia de
 * `make_lut_bgobj` -- la funcion de upstream que construye la LUT de sprite
 * contra fondo-- y compara las dos sobre las 65536 combinaciones posibles de
 * (byte de abajo, byte de sprite).
 *
 * La equivalencia que se afirma: AYTHER_SPRITE_WINS(s, b) es verdadero
 * exactamente cuando la LUT devuelve el COLOR DEL SPRITE y no el de abajo.
 */
#include <stdio.h>
#include <stdint.h>

#include "../core/ayther/ayther_sprite_px.h"

static int passed, failed;

/* Copia literal de core/vdp_render.c. No se incluye el archivo porque arrastra
   el core entero; que sea una copia es justamente lo que este test vigila. */
static uint32_t make_lut_bgobj(uint32_t bx, uint32_t sx)
{
  int c;

  int bf = (bx & 0x3F);
  int bs = (bx & 0x80);
  int bp = (bx & 0x40);
  int b  = (bx & 0x0F);

  int sf = (sx & 0x3F);
  int sp = (sx & 0x40);
  int s  = (sx & 0x0F);

  if (s == 0) return bx;

  /* Previous sprite has higher priority */
  if (bs) return bx;

  c = (sp ? sf : (bp ? (b ? bf : sf) : sf));

  /* Strip palette & priority bits from transparent pixels */
  if ((c & 0x0F) == 0x00) c &= 0x80;

  return (c | 0x80);
}

int main(void)
{
  uint32_t bx, sx;
  int mismatches = 0;
  unsigned long checked = 0;
  int first_bx = -1, first_sx = -1;

  for (bx = 0; bx < 0x100; ++bx)
  {
    for (sx = 0; sx < 0x100; ++sx)
    {
      /* El bit 7 del byte de abajo significa "ya hay un sprite opaco aca".
         En ese caso la LUT conserva lo que estaba -- que TAMBIEN es un sprite-,
         asi que el predicado no tiene por que contestar que si: el pixel ya
         quedo marcado cuando ese sprite anterior se dibujo. El store acumula
         con OR justamente por esto. Ese caso queda fuera de la comparacion. */
      uint32_t out = make_lut_bgobj(bx, sx);
      int lut_took_sprite;
      int predicate;

      if (bx & 0x80) continue;

      /* Si los dos colores son IGUALES, el resultado no dice quien gano: es la
         misma ambiguedad por la que el diff contra el fondo fallaba. Se comparan
         los pares donde el resultado distingue, que es donde la comparacion
         significa algo; el caso de colores iguales se afirma aparte, abajo. */
      if ((sx & 0x3F) == (bx & 0x3F)) continue;

      /* La LUT tomo el color del sprite si el resultado (sin el marcador de
         opacidad) coincide con los seis bits bajos del sprite y el sprite
         tenia color. */
      lut_took_sprite = (sx & 0x0F) && ((out & 0x3F) == (sx & 0x3F));
      predicate = AYTHER_SPRITE_WINS(sx, bx) ? 1 : 0;

      ++checked;
      if (lut_took_sprite != predicate)
      {
        if (!mismatches) { first_bx = (int)bx; first_sx = (int)sx; }
        ++mismatches;
      }
    }
  }

  if (mismatches)
  {
    printf("FALLA: %d desacuerdos de %lu; el primero en bajo=%02x sprite=%02x\n",
           mismatches, checked, first_bx, first_sx);
    ++failed;
  }
  else
  {
    printf("la regla coincide con make_lut_bgobj en las %lu combinaciones\n",
           checked);
    ++passed;
  }

  /* Los tres casos que el diff contra el fondo contesta mal, por si el barrido
     de arriba alguna vez se vuelve laxo: hay que poder leerlos con nombre. */
  {
    /* 1. sprite identico al fondo: mismo indice, misma paleta, sin prioridad.
          El byte no cambia, asi que el diff no lo ve. La regla si. */
    if (AYTHER_SPRITE_WINS(0x01, 0x01)) ++passed;
    else { printf("FALLA: sprite identico al fondo tiene que ganar\n"); ++failed; }

    /* 2. fondo con prioridad y opaco: gana el fondo. */
    if (!AYTHER_SPRITE_WINS(0x01, 0x41)) ++passed;
    else { printf("FALLA: un fondo opaco con prioridad gana\n"); ++failed; }

    /* 3. fondo con prioridad pero TRANSPARENTE: gana el sprite. Es el caso que
          un `!(bp)` a secas contesta mal. */
    if (AYTHER_SPRITE_WINS(0x01, 0x40)) ++passed;
    else { printf("FALLA: un fondo transparente no puede ganar por prioridad\n"); ++failed; }

    /* 4. el operador de S/H no es color. */
    if (AYTHER_SPRITE_IS_OPERATOR(0x3E) && AYTHER_SPRITE_IS_OPERATOR(0x3F)) ++passed;
    else { printf("FALLA: paleta 3 indices 14/15 son operadores\n"); ++failed; }

    /* 5. y paleta 3 con otro indice SI es color. */
    if (!AYTHER_SPRITE_IS_OPERATOR(0x3D)) ++passed;
    else { printf("FALLA: paleta 3 indice 13 es un color normal\n"); ++failed; }
  }

  printf("sprite pixel rule tests: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
