#!/usr/bin/env bash
# Puntos de contacto con upstream (#68): cuantos hunks nuestros hay en cada
# archivo DE UPSTREAM, contra una linea base versionada.
#
# La friccion de un sync no la mide el tamanio del diff sino en cuantos lugares
# de un archivo ajeno estamos parados: cada uno es un conflicto en potencia si
# upstream toca cerca. Este gate no prohibe subir el numero -- a veces hace
# falta un hook nuevo--, prohibe subirlo SIN DECIRLO: el PR que agrega puntos de
# contacto regenera la linea base en el mismo commit, y el cambio queda a la
# vista en el diff de tests/ci/upstream_contact.txt, con su justificacion en el
# mensaje. Es el mismo trato que reciben los goldens.
#
# Se mide contra el merge-base con upstream/master y no contra upstream/master
# a secas: entre sync y sync upstream se mueve, y eso cambiaria el numero sin
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

measure() {
  git diff --name-only "$base" -- . | while IFS= read -r f; do
    # Un archivo que no existe en upstream es nuestro entero: no cuenta.
    git cat-file -e "$base:$f" 2>/dev/null || continue
    [ -f "$f" ] || continue
    n=$(git diff -U0 "$base" -- "$f" | grep -c '^@@')
    [ "$n" -gt 0 ] && printf '%d %s\n' "$n" "$f"
  done | sort -k2
}

current=$(measure)

if [ "$mode" = "--regen" ]; then
  {
    echo "# Puntos de contacto con upstream (#68): hunks nuestros por archivo de"
    echo "# upstream, medidos contra el merge-base con upstream/master."
    echo "# Regenerar con: bash tests/ci/check_upstream_contact.sh --regen"
    echo "# Subir un numero es legitimo si se dice por que en el mismo PR."
    printf '%s\n' "$current"
  } > "$baseline"
  echo "linea base regenerada: $baseline ($(printf '%s\n' "$current" | grep -c .) archivos, $(printf '%s\n' "$current" | awk '{s+=$1} END {print s+0}') hunks)"
  exit 0
fi

[ -f "$baseline" ] || { echo "falta $baseline; generarla con --regen" >&2; exit 2; }

fail=0
while read -r n f; do
  [ -n "$f" ] || continue
  b=$(awk -v f="$f" '$2 == f { print $1 }' "$baseline")
  if [ -z "$b" ]; then
    echo "NUEVO   $f: $n hunks, y no estaba en la linea base" >&2
    fail=1
  elif [ "$n" -gt "$b" ]; then
    echo "SUBE    $f: $n hunks, la linea base dice $b" >&2
    fail=1
  elif [ "$n" -lt "$b" ]; then
    echo "baja    $f: $n hunks (linea base $b); regenerar para fijarlo"
  fi
done <<< "$current"

total=$(printf '%s\n' "$current" | awk '{s+=$1} END {print s+0}')
if [ "$fail" = 0 ]; then
  echo "puntos de contacto con upstream: $total hunks en $(printf '%s\n' "$current" | grep -c .) archivos, ninguno por encima de la linea base"
else
  echo "Si el hook nuevo hace falta, regenerar la linea base en el mismo PR y decir por que:" >&2
  echo "  bash tests/ci/check_upstream_contact.sh --regen" >&2
fi
exit "$fail"
