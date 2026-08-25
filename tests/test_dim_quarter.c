/* #31: que el atenuado del dim (id 0x108) sea el 25% real por canal, en TODOS
 * los formatos de pixel que el core compila.
 *
 * El cuarteo estaba escrito a mano con mascaras RGB565 dentro de `remap_line`.
 * Correcto para el perfil del fork, y basura silenciosa para los demas: bajo
 * 15BPP los canales viven en otros bits, asi que esas mascaras no producian un
 * color mas oscuro sino OTRO color, sin que nada lo reportara.
 *
 * La afirmacion es aritmetica pura -- "cada canal queda en un cuarto, +-1 LSB
 * por el truncamiento"-- asi que no necesita un emulador: alcanza con incluir
 * el mismo header que usa el core, una vez por formato.
 */
#include <stdio.h>
#include <stdint.h>

static int failures;
static int checks;

#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    ++checks;                                                              \
    if (!(cond)) { ++failures; printf("  FALLA: "); printf(__VA_ARGS__); }  \
  } while (0)

/* Un canal atenuado: el valor entero mas cercano por debajo de v/4. El shift
   trunca, asi que el error admisible es de 1 LSB y nunca hacia arriba. */
static int quarter_ok(unsigned original, unsigned dimmed, const char *fmt,
                      const char *chan)
{
  unsigned expected = original / 4u;
  if (dimmed == expected) return 1;
  printf("  FALLA [%s] canal %s: %u -> %u, esperado %u\n",
         fmt, chan, original, dimmed, expected);
  return 0;
}

/* --- 5-6-5 --------------------------------------------------------------- */
#define PIXEL_OUT_T uint16_t
#include "../core/ayther/ayther_dim.h"

static void check_rgb565(void)
{
  unsigned r, g, b;
  printf("RGB565 (5-6-5):\n");
  for (r = 0; r < 32u; ++r)
    for (g = 0; g < 64u; ++g)
      for (b = 0; b < 32u; ++b)
      {
        uint16_t p = (uint16_t)((r << 11) | (g << 5) | b);
        uint16_t d = AYTHER_DIM_QUARTER(p);
        ++checks;
        if (!quarter_ok(r, (unsigned)((d >> 11) & 0x1F), "565", "R") ||
            !quarter_ok(g, (unsigned)((d >>  5) & 0x3F), "565", "G") ||
            !quarter_ok(b, (unsigned)( d        & 0x1F), "565", "B"))
        {
          ++failures;
          return;   /* con uno alcanza: el patron es sistematico */
        }
      }
  printf("  32x64x32 combinaciones, todas al cuarto exacto\n");
}
#undef AYTHER_DIM_QUARTER
#undef AYTHER_DIM_H
#undef PIXEL_OUT_T

/* --- 1-5-5-5 ------------------------------------------------------------- */
#define USE_15BPP_RENDERING 1
#define PIXEL_OUT_T uint16_t
#include "../core/ayther/ayther_dim.h"

static void check_rgb555(void)
{
  unsigned a, c0, c1, c2;
  printf("RGB555 (1-5-5-5):\n");
  for (a = 0; a < 2u; ++a)
    for (c0 = 0; c0 < 32u; ++c0)
      for (c1 = 0; c1 < 32u; ++c1)
        for (c2 = 0; c2 < 32u; ++c2)
        {
          uint16_t p = (uint16_t)((a << 15) | (c0 << 10) | (c1 << 5) | c2);
          uint16_t d = AYTHER_DIM_QUARTER(p);
          ++checks;
          /* El bit 15 es estructural: el formato lo lleva siempre en 1 y el
             atenuado no puede apagarlo, o el pixel deja de ser opaco. */
          if (((d >> 15) & 1u) != a)
          {
            ++failures;
            printf("  FALLA [555] el bit alto cambio: %u -> %u\n",
                   a, (unsigned)((d >> 15) & 1u));
            return;
          }
          if (!quarter_ok(c0, (unsigned)((d >> 10) & 0x1F), "555", "c0") ||
              !quarter_ok(c1, (unsigned)((d >>  5) & 0x1F), "555", "c1") ||
              !quarter_ok(c2, (unsigned)( d        & 0x1F), "555", "c2"))
          {
            ++failures;
            return;
          }
        }
  printf("  2x32x32x32 combinaciones, todas al cuarto exacto y con el bit alto intacto\n");
}
#undef AYTHER_DIM_QUARTER
#undef AYTHER_DIM_H
#undef PIXEL_OUT_T
#undef USE_15BPP_RENDERING

/* --- 8-8-8-8 ------------------------------------------------------------- */
#define USE_32BPP_RENDERING 1
#define PIXEL_OUT_T uint32_t
#include "../core/ayther/ayther_dim.h"

static void check_xrgb8888(void)
{
  unsigned v;
  printf("XRGB8888 (8-8-8-8):\n");
  for (v = 0; v < 256u; ++v)
  {
    uint32_t p = 0xFF000000u | (v << 16) | (v << 8) | v;
    uint32_t d = AYTHER_DIM_QUARTER(p);
    ++checks;
    if ((d & 0xFF000000u) != 0xFF000000u)
    {
      ++failures;
      printf("  FALLA [8888] el alfa se perdio: %08x -> %08x\n",
             (unsigned)p, (unsigned)d);
      return;
    }
    if (!quarter_ok(v, (unsigned)((d >> 16) & 0xFF), "8888", "R") ||
        !quarter_ok(v, (unsigned)((d >>  8) & 0xFF), "8888", "G") ||
        !quarter_ok(v, (unsigned)( d        & 0xFF), "8888", "B"))
    {
      ++failures;
      return;
    }
  }
  printf("  256 niveles, todos al cuarto exacto y con el alfa intacto\n");
}

int main(void)
{
  check_rgb565();
  check_rgb555();
  check_xrgb8888();
  printf("\ndim quarter: %d comprobaciones, %d fallas\n", checks, failures);
  return failures ? 1 : 0;
}
