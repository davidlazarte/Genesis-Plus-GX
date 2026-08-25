#!/usr/bin/env bash
# Corre el core COMPLETO bajo sanitizers (#38).
#
# Hasta ahora ASan/UBSan solo cubrian los binarios de tests/ construidos contra
# el stub: es decir, todo menos vdp_render.c, vdp_ctrl.c, ym2612.c y libretro.c,
# que es donde vive el fork. El unico test que los ejercita -full_core_replay-
# nunca corria instrumentado.
#
# Uso:
#   run_sanitizers.sh <ruta-al-core> [ruta-al-golden]
#
# La logica de "que reportes se aceptan" vive en known_ub.txt y no en flags de
# compilacion: apagar -fsanitize=alignment para tapar un hallazgo conocido
# tambien tapa los futuros, y esa es una decision que despues nadie revisa.

set -uo pipefail

core=${1:?falta la ruta al core}
golden=${2:-}

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tests_dir=$(CDPATH= cd -- "$here/.." && pwd)
known="$here/known_ub.txt"
log=$(mktemp)
trap 'rm -f "$log"' EXIT

# #45: un solo golden. Dejo de depender del sistema operativo desde que la
# emulacion coincide byte a byte entre plataformas (ver tests/Makefile).
if [ -z "$golden" ]; then
  golden="$tests_dir/ayther/golden/full_core_replay-x64.json"
fi

mkdir -p "$tests_dir/artifacts"

# halt_on_error=0 a proposito: se quiere el INVENTARIO completo de un solo pase,
# no el primer hallazgo. El veredicto lo da el filtro de abajo.
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:report_error_type=1"
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:detect_stack_use_after_return=1"

# El nombre del binario lo pone el Makefile segun la plataforma; hardcodear
# `.exe` hacia que el script solo anduviera en Windows, y en Linux fallaba con
# "command not found" -exit 127-, que no se parece en nada a la causa.
replay="$tests_dir/.build/full_core_replay"
[ -x "$replay" ] || replay="$replay.exe"
if [ ! -x "$replay" ]; then
  echo "no encuentro el harness en $tests_dir/.build (¿falta make full-core-replay?)" >&2
  exit 2
fi

echo "== full-core replay bajo sanitizers =="
"$replay" "$core" "$golden" \
  "$tests_dir/artifacts/sanitize_replay.actual.json" \
  "$tests_dir/artifacts/sanitize_replay.frames.jsonl" \
  "$tests_dir/artifacts/sanitize_benchmark.json" 2>&1 | tee "$log"
replay_status=${PIPESTATUS[0]}

# Los reportes se agrupan por SITIO (archivo:linea:columna + tipo), no por texto
# crudo: cada reporte lleva la direccion concreta, asi que sin normalizar el
# mismo store desalineado aparece miles de veces y ahoga a cualquier hallazgo
# nuevo en el ruido.
reports=$(grep -E 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer' "$log" |
          sed -e 's/^.*[\\/]//' \
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

echo
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

if [ "$replay_status" -ne 0 ]; then
  echo "el replay fallo por si mismo (golden o assert), no por el sanitizer" >&2
  exit "$replay_status"
fi

echo "sanitizers: sin UB fuera de lo conocido; replay y golden OK"
