#!/usr/bin/env bash
# Puntos de contacto con upstream (#68, #71): DONDE estamos parados dentro de
# cada archivo de upstream, contra una linea base versionada.
#
# La friccion de un sync no la mide el tamanio del diff sino en cuantos lugares
# de un archivo ajeno estamos parados: cada uno es un conflicto en potencia si
# upstream toca cerca. La primera version de este gate contaba hunks por
# archivo; un conteo no distingue "un hook nuevo y uno viejo que se fue" de
# "nada cambio", y esa es justo la diferencia que importa. Ahora cada hunk
# tiene una IDENTIDAD:
#
#     archivo | funcion que lo contiene | primera linea de codigo agregada
#
# y la linea base es la lista de identidades. Un hunk cuya identidad no esta en
# la base es un punto de contacto nuevo y falla el gate; uno que desaparece se
# informa. Cambiar un comentario o mover un hook de linea no cambia su
# identidad; cambiar la primera linea de codigo del hook si, y eso es
# deliberado: es un hook distinto.
#
# El gate no prohibe agregar puntos de contacto -- a veces hace falta un hook
# nuevo--, prohibe agregarlos SIN DECIRLO: el PR que los agrega regenera la
# linea base en el mismo commit, y el diff de tests/ci/upstream_contact.txt
# muestra exactamente cuales, con la justificacion en el mensaje. Es el mismo
# trato que reciben los goldens.
#
# Se mide contra el merge-base con upstream/master y no contra upstream/master
# a secas: entre sync y sync upstream se mueve, y eso cambiaria el resultado sin
# que nosotros hayamos tocado nada. El sync es el momento de regenerar.
#
# Uso:  check_upstream_contact.sh            compara contra la linea base
#       check_upstream_contact.sh --regen    reescribe la linea base
set -uo pipefail

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
baseline="$here/upstream_contact.txt"
mode=${1:-check}

cd "$root" || exit 2
git remote get-url upstream >/dev/null 2>&1 \
  || git remote add upstream https://github.com/libretro/Genesis-Plus-GX.git
if ! git fetch --no-tags --quiet upstream master; then
  echo "no pude traer upstream/master; sin eso no hay contra que medir" >&2
  exit 2
fi
base=$(git merge-base HEAD upstream/master) || exit 2

# Una linea por hunk: "archivo<TAB>funcion<TAB>firma". La funcion la da git
# (el encabezado @@ ... @@); la firma es la primera linea agregada que no es
# comentario, directiva de preprocesador ni llave suelta, con el espacio
# normalizado. Un hunk que solo agrega comentarios firma como "(comentario)".
measure() {
  git diff --name-only "$base" -- . | while IFS= read -r f; do
    # Un archivo que no existe en upstream es nuestro entero: no cuenta.
    git cat-file -e "$base:$f" 2>/dev/null || continue
    [ -f "$f" ] || continue
    git diff -U0 "$base" -- "$f" | awk -v file="$f" '
      function flush() {
        if (inhunk) {
          if (sig == "") sig = "(comentario)"
          printf "%s\t%s\t%s\n", file, fn, sig
        }
        inhunk = 0; sig = ""; incomment = 0
      }
      /^@@/ {
        flush(); inhunk = 1
        fn = $0; sub(/^@@[^@]*@@ */, "", fn); if (fn == "") fn = "(sin funcion)"
        next
      }
      inhunk && /^\+/ && !/^\+\+\+/ {
        if (sig != "") next
        line = substr($0, 2); sub(/\r$/, "", line)
        gsub(/^[ \t]+|[ \t]+$/, "", line)
        if (incomment) { if (line ~ /\*\//) incomment = 0; next }
        if (line ~ /^\/\*/) { if (line !~ /\*\//) incomment = 1; next }
        if (line == "" || line ~ /^\/\// || line ~ /^#(if|ifdef|ifndef|else|endif)/ || line ~ /^[{}]$/) next
        gsub(/[ \t]+/, " ", line)
        sig = line
      }
      END { flush() }
    '
  done | sort
}

current=$(measure)
n_current=$(printf '%s\n' "$current" | grep -c .)

if [ "$mode" = "--regen" ]; then
  {
    echo "# Puntos de contacto con upstream (#68, #71): un hunk nuestro por linea,"
    echo "# en archivos DE upstream, medidos contra el merge-base con upstream/master."
    echo "# archivo<TAB>funcion<TAB>primera linea de codigo del hunk."
    echo "# Regenerar con: bash tests/ci/check_upstream_contact.sh --regen"
    echo "# Agregar un punto de contacto es legitimo si se dice por que en el mismo PR."
    printf '%s\n' "$current"
  } > "$baseline"
  echo "linea base regenerada: $baseline ($n_current hunks en $(printf '%s\n' "$current" | cut -f1 | sort -u | grep -c .) archivos)"
  exit 0
fi

[ -f "$baseline" ] || { echo "falta $baseline; generarla con --regen" >&2; exit 2; }
known=$(grep -v '^#' "$baseline" | grep .)

fail=0
new=$(comm -13 <(printf '%s\n' "$known" | sort) <(printf '%s\n' "$current" | sort))
gone=$(comm -23 <(printf '%s\n' "$known" | sort) <(printf '%s\n' "$current" | sort))
if [ -n "$new" ]; then
  echo "== puntos de contacto NUEVOS, no estan en la linea base ==" >&2
  printf '%s\n' "$new" | awk -F'\t' '{ printf "  %s  en %s:\n      %s\n", $1, $2, $3 }' >&2
  fail=1
fi
if [ -n "$gone" ]; then
  echo "== puntos de contacto que ya no estan (regenerar la base para fijarlo) =="
  printf '%s\n' "$gone" | awk -F'\t' '{ printf "  %s  en %s:\n      %s\n", $1, $2, $3 }'
fi

# Resumen por archivo, que es lo que se lee en el runbook.
printf '%s\n' "$current" | cut -f1 | sort | uniq -c | sort -rn | awk '{ printf "  %4d  %s\n", $1, $2 }' | head -8
echo "puntos de contacto con upstream: $n_current hunks en $(printf '%s\n' "$current" | cut -f1 | sort -u | grep -c .) archivos"
if [ "$fail" != 0 ]; then
  echo "Si el hook nuevo hace falta, regenerar la linea base en el mismo PR y decir por que:" >&2
  echo "  bash tests/ci/check_upstream_contact.sh --regen" >&2
fi
exit "$fail"
