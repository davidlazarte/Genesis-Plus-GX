#!/usr/bin/env bash
# Decide si los reportes de sanitizer de un log son UB YA CONOCIDO o UB NUEVO.
#
# Vivia adentro de run_sanitizers.sh. Se saco cuando el job nocturno de fuzzing
# (#34) necesito la misma decision: dos copias de "que UB se acepta" divergen
# -- una se actualiza y la otra no-, y el modo de fallar es el peor, porque el
# gate que se quedo viejo sigue verde.
#
# Uso:  filter_known_ub.sh <log> [known_ub.txt]
# Sale 0 si todo lo reportado esta en la lista, 1 si aparece algo que no.
set -uo pipefail

log=${1:?uso: filter_known_ub.sh <log> [known_ub.txt]}
here=$(cd "$(dirname "$0")" && pwd)
known=${2:-"$here/known_ub.txt"}

[ -f "$log" ] || { echo "no existe el log: $log" >&2; exit 2; }
[ -f "$known" ] || { echo "no existe la lista: $known" >&2; exit 2; }

# Los reportes se agrupan por SITIO (archivo:linea:columna + tipo), no por texto
# crudo: cada reporte lleva la direccion concreta, asi que sin normalizar el
# mismo store desalineado aparece miles de veces y ahoga a cualquier hallazgo
# nuevo en el ruido.
reports=$(grep -E 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer' "$log" |
          sed -e 's/^.*[\/]//' \
              -e 's/(0x[0-9a-fA-F]*)//g' -e 's/0x[0-9a-fA-F]*/0xADDR/g' |
          sort -u || true)

unknown=""
while IFS= read -r line; do
  [ -n "$line" ] || continue
  matched=0
  while IFS= read -r pattern; do
    case $pattern in ''|'#'*) continue ;; esac
    # Un patron puede pedir VARIAS subcadenas separadas por '|', y tienen que
    # estar TODAS. Hace falta para poder decir "este archivo Y esta clase de UB"
    # sin fijar archivo:linea:columna, que se desactualiza en cuanto alguien
    # edita el archivo y deja el gate rojo sin que haya UB nuevo.
    matched=1
    rest=$pattern
    while [ -n "$rest" ]; do
      part=${rest%%|*}
      case $rest in *"|"*) rest=${rest#*|} ;; *) rest="" ;; esac
      [ -n "$part" ] || continue
      case $line in *"$part"*) ;; *) matched=0; break ;; esac
    done
    [ "$matched" = 1 ] && break
  done < <(tr -d '\r' < "$known")
  [ "$matched" = 0 ] && unknown="$unknown$line"$'\n'
done <<< "$reports"

if [ -n "$reports" ]; then
  echo "== reportes de sanitizer (unicos por sitio) =="
  printf '%s\n' "$reports"
fi

if [ -n "${unknown//[$'\n' ]/}" ]; then
  echo
  echo "== UB NUEVO, no listado en known_ub.txt =="
  printf '%s' "$unknown"
  echo "Arreglar la causa. Ampliar known_ub.txt solo con justificacion explicita."
  exit 1
fi

echo "sin UB nuevo"
