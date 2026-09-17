#!/usr/bin/env bash
# El aislamiento de las entradas de fuzz_unserialize, medido (#108).
#
# El target decia aislar cada entrada "restaurando un estado sano" y no lo
# hacia: restauraba el estado ACTUAL, ya podrido, y la corrupcion se arrastraba
# entre entradas del mismo proceso. Se descubrio dos veces por el sintoma (#63,
# #105: el crash-* del nocturno no reproduce solo) y ninguna por un test, porque
# no habia ninguno. Este es ese test.
#
# Lo que se afirma: el resultado de una entrada -la huella del estado
# serializado despues de correrla, que imprime el target con
# AYTHER_FUZZ_DIGEST=1- es el MISMO
#
#   - ejecutada sola,
#   - ejecutada dos veces seguidas (la segunda huella igual a la primera),
#   - ejecutada detras de todas las demas, en orden directo y en orden inverso.
#
# Si alguna de las cuatro difiere, la entrada depende de la historia del proceso
# y el aislamiento es una declaracion, no un hecho.
#
# Uso: check_fuzz_isolation.sh <core> [escena] [log]
#
# Necesita el replay ya compilado (make -C tests/fuzz .build/replay_unserialize,
# o cualquier check-fuzz previo). El stderr de todas las corridas -donde caen
# los reportes de los sanitizers- se acumula en <log> para que quien juzga UB
# pueda pasarle filter_known_ub.sh despues.
set -uo pipefail

core=${1:?uso: check_fuzz_isolation.sh <core> [escena] [log]}
scene=${2:-md}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fuzz=$(CDPATH= cd -- "$here/../fuzz" && pwd)
log=${3:-"$fuzz/artifacts/isolation-$scene.log"}

replay="$fuzz/.build/replay_unserialize"
[ -x "$replay" ] || replay="$replay.exe"
if [ ! -x "$replay" ]; then
  echo "no encuentro $fuzz/.build/replay_unserialize (falta make -C tests/fuzz .build/replay_unserialize)" >&2
  exit 2
fi
case $core in /*|[A-Za-z]:*) ;; *) core="$(pwd)/$core" ;; esac

mkdir -p "$(dirname "$log")"
: > "$log"
export AYTHER_FUZZ_DIGEST=1

# Todas las entradas del target: las regresiones y el corpus. El orden es el
# del nombre, fijo, que es justamente lo que el driver NO garantiza cuando se le
# pasa un directorio (readdir); aca los archivos van uno por uno.
files=()
for f in "$fuzz"/regressions/unserialize/* "$fuzz"/corpus/unserialize/*; do
  [ -f "$f" ] && files+=("$f")
done
if [ "${#files[@]}" -lt 2 ]; then
  echo "hacen falta al menos dos entradas para probar el aislamiento" >&2
  exit 2
fi

# huellas <archivo>... : imprime una huella por entrada, en el orden dado.
# Deja el codigo de salida del replay en $rc.
rc=0
huellas() {
  local out
  out=$("$replay" --scene "$scene" "$core" "$@" 2>>"$log")
  rc=$?
  printf '%s\n' "$out" | sed -n 's/^  digest \([0-9a-f]*\) .*/\1/p'
}

fallas=0
n=0
falla() { printf '  FALLA %s\n' "$1"; fallas=$((fallas + 1)); }

echo "== $scene: ${#files[@]} entradas, orden directo e inverso =="
directo=$(huellas "${files[@]}"); rc_d=$rc
inverso_files=()
for ((i = ${#files[@]} - 1; i >= 0; i--)); do inverso_files+=("${files[$i]}"); done
inverso=$(huellas "${inverso_files[@]}"); rc_i=$rc

n=$((n + 2))
[ "$rc_d" = 0 ] && printf '  ok    orden directo: exit 0\n'  || falla "orden directo: exit $rc_d"
[ "$rc_i" = 0 ] && printf '  ok    orden inverso: exit 0\n'  || falla "orden inverso: exit $rc_i"

mapfile -t D <<< "$directo"
mapfile -t I <<< "$inverso"
n=$((n + 1))
if [ "${#D[@]}" = "${#files[@]}" ] && [ "${#I[@]}" = "${#files[@]}" ]; then
  printf '  ok    una huella por entrada en las dos corridas\n'
else
  falla "huellas: directo ${#D[@]}, inverso ${#I[@]}, esperaba ${#files[@]}"
fi

echo
echo "== cada entrada: sola, repetida, y detras de las demas =="
for ((i = 0; i < ${#files[@]}; i++)); do
  f=${files[$i]}
  nombre=$(basename "$f")
  ref=${D[$i]:-}
  inv=${I[$(( ${#files[@]} - 1 - i ))]:-}

  sola=$(huellas "$f"); rc_s=$rc
  rep=$(huellas "$f" "$f"); rc_r=$rc
  rep1=$(printf '%s\n' "$rep" | sed -n 1p)
  rep2=$(printf '%s\n' "$rep" | sed -n 2p)

  n=$((n + 1))
  if [ "$rc_s" = 0 ] && [ "$rc_r" = 0 ] && [ -n "$ref" ] &&
     [ "$sola" = "$ref" ] && [ "$rep1" = "$ref" ] && [ "$rep2" = "$ref" ] && [ "$inv" = "$ref" ]; then
    printf '  ok    %-52s %s\n' "$nombre" "$ref"
  else
    falla "$nombre"
    printf '          directo  %s\n' "${ref:-?}"
    printf '          inverso  %s\n' "${inv:-?}"
    printf '          sola     %s (exit %s)\n' "${sola:-?}" "$rc_s"
    printf '          repetida %s / %s (exit %s)\n' "${rep1:-?}" "${rep2:-?}" "$rc_r"
  fi
done

echo
if [ "$fallas" = 0 ]; then
  echo "aislamiento de unserialize [$scene]: $n comprobaciones, 0 fallas"
else
  echo "aislamiento de unserialize [$scene]: $n comprobaciones, $fallas FALLAS" >&2
  echo "una entrada que da distinto segun lo que corrio antes depende del estado del proceso: el target no aisla" >&2
fi
[ "$fallas" = 0 ]
