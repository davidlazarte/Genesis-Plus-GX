#!/usr/bin/env bash
# Todo renderer de fondo Mode 5 tiene que consultar los gates de capa (#28).
#
# El defecto que cubre no es el de un renderer concreto sino su forma: los gates
# viven DENTRO de cada renderer, y hay cinco. Cuando se agregaron, tres se
# quedaron sin ellos -interlace mode 2, su variante con vscroll, y el vscroll
# mejorado- y el sintoma fue el peor posible: escribir la mascara no hacia nada,
# sin error ni motivo de fallback. Nadie lo noto durante mucho tiempo.
#
# Un test de pixeles necesita una escena por modo (eso es #35). Esto es mas
# barato y ataca la reincidencia: un renderer NUEVO sin gates falla el build.

set -uo pipefail

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-"$here/../../core/vdp_render.c"}

fail=0

# Los renderers de fondo Mode 5 fuera del bloque ALT_RENDERER, que es
# incompatible con AYTHER_EXTENSIONS por #error y no se compila en este fork.
renderers="render_bg_m5_impl render_bg_m5_vs_impl render_bg_m5_vs_enhanced render_bg_m5_im2 render_bg_m5_im2_vs"

for name in $renderers; do
  # Cuerpo de la funcion: desde su definicion hasta la llave de cierre en
  # columna 0. Sirve porque el archivo usa ese estilo de forma consistente.
  body=$(awk -v fn="$name" '
    $0 ~ ("^(AYTHER_HOT_INLINE )?(static )?void " fn "\\(int line") { inside = 1 }
    inside { print }
    inside && /^\}/ { exit }
  ' "$src")

  if [ -z "$body" ]; then
    echo "no se encontro el renderer $name (¿se renombro?)" >&2
    fail=1
    continue
  fi

  for gate in hide_a hide_b hide_w; do
    if ! printf '%s' "$body" | grep -q "$gate"; then
      echo "$name no consulta '$gate': la mascara de capas seria un no-op silencioso" >&2
      fail=1
    fi
  done

  # Y que el plano oculto se limpie, no solo que no se dibuje: el buffer es
  # compartido entre lineas y lo que quedo de la anterior se veria igual.
  if ! printf '%s' "$body" | grep -q "memset(&linebuf\[1\]\[0x20\]"; then
    echo "$name no limpia linebuf[1] al ocultar Plano A/Window" >&2
    fail=1
  fi
  if ! printf '%s' "$body" | grep -q "memset(&linebuf\[0\]\[0x20\], 0"; then
    echo "$name no limpia linebuf[0] al ocultar Plano B" >&2
    fail=1
  fi

  # #68: DRAW_COLUMN plegado. El macro lee dos locales con nombre fijo que
  # cada renderer declara (AYTHER_COLUMN_LOCALS) y fija antes de cada plano
  # (AYTHER_COLUMN_PLANE). Olvidar las locales no compila -- ese modo de falla
  # se cuida solo--; olvidar un plano SI compila y deja la supresion de ese
  # plano en un no-op silencioso. Esto es lo que lo vuelve ruidoso.
  if ! printf '%s' "$body" | grep -q "AYTHER_COLUMN_LOCALS("; then
    echo "$name no declara AYTHER_COLUMN_LOCALS: DRAW_COLUMN plegado no compila sin ellas" >&2
    fail=1
  fi
  if [ "$name" = render_bg_m5_vs_enhanced ]; then
    # Este renderer no aplica supresion de tiles por diseno (la declara
    # UNSUPPORTED_CONTROLS): dibuja medias columnas por otro camino, y fijar un
    # plano aca la aplicaria a medias.
    if printf '%s' "$body" | grep -q "AYTHER_COLUMN_PLANE("; then
      echo "$name fija un plano de supresion, y la supresion ahi es a medias por diseno" >&2
      fail=1
    fi
  else
    for plane in psupA psupB psupW; do
      if ! printf '%s' "$body" | grep -q "AYTHER_COLUMN_PLANE($plane)"; then
        echo "$name no fija $plane antes de dibujar ese plano: su supresion seria un no-op silencioso" >&2
        fail=1
      fi
    done
  fi
done

# Y que nadie vuelva al macro viejo: los sitios de llamada son los de upstream.
if grep -q 'DRAW_COLUMN_AE(\|DRAW_COLUMN_IM2_AE(' "$src"; then
  echo "vdp_render.c usa DRAW_COLUMN_AE: desde #68 los sitios de llamada son DRAW_COLUMN a secas" >&2
  fail=1
fi

if [ "$fail" = 0 ]; then
  echo "render gates OK: $(printf '%s' "$renderers" | wc -w) renderers Mode 5 consultan las mascaras y cumplen el contrato de DRAW_COLUMN"
fi
exit "$fail"
