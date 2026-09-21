#!/usr/bin/env bash
# Abre el issue de un hallazgo del nocturno de fuzzing (#34, #134).
#
# Vivia inline en ayther-fuzz.yml y solo conocia un caso: libFuzzer dejo un
# `crash-*`. Pero el job tiene DOS maneras de fallar, y la otra no deja archivo:
# UBSan corre con halt_on_error=0, imprime y sigue, el fuzzer termina sus 600 s
# sin crash y el que pone el job en rojo es filter_known_ub.sh. Ahi el paso
# decia "hay que leer el log a mano" y no abria nada. Medido: corridas
# 35421489395 y 35489371333, dos targets en rojo dos noches sin un solo issue
# (#133 y parte de #129 se encontraron leyendo logs mientras se miraba el
# backlog). Es la misma familia que #107: un hallazgo que solo existe si
# alguien mira.
#
# Se saco a un script por lo mismo que filter_known_ub.sh: adentro del YAML no
# se puede probar, y un paso que decide si alguien se entera de un bug merece
# tabla de verdad (tests/ci/check_fuzz_open_issue.sh).
#
# Uso: fuzz_open_issue.sh <id> <target> <escena> <dir-artefactos> <log> <url-corrida>
#
#   <id> es el del matrix (unserialize-sms-fm); <target> y <escena> son los
#   que entiende el replay (unserialize, sms-fm).
#
# Dos claves de deduplicacion, y son distintas a proposito:
#
#   - con archivo, el sha1 que libFuzzer puso en el nombre, contra issues en
#     CUALQUIER estado: la misma entrada es el mismo hallazgo para siempre.
#   - sin archivo, `archivo clase-de-UB` del primer sitio no listado
#     (`m68kops.h invalid-shift-exponent`), contra issues ABIERTOS. No lleva
#     linea: una familia como la de #133 aparece cada noche por un sitio
#     distinto del mismo archivo y tiene que ser UN issue, y la linea se corre
#     en cuanto alguien edita. Y solo abiertos porque la clave no identifica
#     una entrada sino una clase: si el issue se cerro y la clase vuelve, es
#     un bug nuevo o un arreglo incompleto, y las dos cosas merecen issue.
#
# `gh` se toma de $AYTHER_GH para que la prueba pueda poner uno falso.
# Sale 0 siempre que pudo decidir (abrio, ya existia, o no habia nada que
# abrir); 2 si le faltan argumentos o el log.
set -uo pipefail

id=${1:?uso: fuzz_open_issue.sh <id> <target> <escena> <dir-artefactos> <log> <url-corrida>}
target=${2:?falta el target}
scene=${3:?falta la escena}
artifacts=${4:?falta el directorio de artefactos}
log=${5:?falta el log}
run_url=${6:?falta la url de la corrida}
gh=${AYTHER_GH:-gh}
here=$(cd "$(dirname "$0")" && pwd)

[ -f "$log" ] || { echo "no existe el log: $log" >&2; exit 2; }

body=$(mktemp)
trap 'rm -f "$body"' EXIT

# ya_existe <estado> <clave> <titulo>: hay un issue con ESE titulo. La busqueda
# de GitHub es por tokens y devuelve parecidos; el que decide es el grep exacto.
ya_existe() {
  "$gh" issue list --state "$1" --search "$2 in:title" --json title --jq '.[].title' 2>/dev/null |
    tr -d '\r' | grep -Fxq -- "$3"
}

replay="tests/fuzz/.build/replay_$target --scene $scene --workdir tests/fuzz/.build <core.so>"

# --------------------------------------------------------------------------
# Caso 1: libFuzzer dejo el archivo.
file=$(cd "$artifacts" 2>/dev/null && ls crash-* timeout-* oom-* leak-* 2>/dev/null | head -1 || true)
if [ -n "$file" ]; then
  hash=${file#*-}
  title="[fuzz][$id] hallazgo $hash"
  if ya_existe all "$hash" "$title"; then
    echo "ya existe un issue para $hash"
    exit 0
  fi
  {
    echo "El job nocturno de fuzzing encontro un caso en el target \`$id\`."
    echo
    echo "El archivo esta en los artefactos de la corrida, como \`$file\`."
    echo "Para reproducirlo sin fuzzer y con ASan/UBSan puestos:"
    echo
    echo '```'
    echo "make -C tests/fuzz .build/replay_$target"
    echo "$replay <ruta-al-archivo>"
    echo '```'
    echo
    echo "Cuando este arreglado, el archivo va a \`tests/fuzz/regressions/$target/\`:"
    echo "ahi lo reproduce \`make -C tests check-fuzz\` en cada PR y el bug no"
    echo "puede volver en silencio."
    echo
    echo "Corrida: $run_url"
  } > "$body"
  "$gh" issue create --title "$title" --label ci --body-file "$body"
  exit 0
fi

# --------------------------------------------------------------------------
# Caso 2: no hay archivo. Se le pregunta al MISMO filtro que puso el job en
# rojo que fue lo que rechazo, para no tener dos definiciones de "UB nuevo".
nuevos=$(bash "$here/filter_known_ub.sh" "$log" 2>/dev/null |
         sed -n '/^== UB NUEVO/,$p' | sed '1d' | grep -E '[A-Za-z0-9_.+-]+\.(c|h|cc|cpp|hpp|inc):[0-9]+' || true)

if [ -z "$nuevos" ]; then
  echo "el job fallo sin dejar un caso y sin UB nuevo en el log: hay que leerlo a mano" >&2
  exit 0
fi

# El primer sitio no listado, en el orden (estable) en que los da el filtro.
sitio=$(printf '%s\n' "$nuevos" | grep -oE '[A-Za-z0-9_.+-]+\.(c|h|cc|cpp|hpp|inc):[0-9]+(:[0-9]+)?' | head -1)
archivo=${sitio%%:*}

# La clase la da la linea SUMMARY de ese sitio (report_error_type=1). gcc no la
# imprime: ahi la clave queda con una clase generica, que deduplica de mas
# -todo el UB sin caso de ese archivo en un issue- pero nunca de menos.
clase=$(grep -E "SUMMARY: [A-Za-z]+Sanitizer: " "$log" | grep -F -- "$sitio" | head -1 |
        sed -E 's/^.*SUMMARY: [A-Za-z]+Sanitizer: ([A-Za-z0-9_-]+).*$/\1/' || true)
[ -n "$clase" ] || clase="ub-sin-clasificar"

key="$archivo $clase"
title="[fuzz][$id] UB sin caso: $key"
if ya_existe open "$key" "$title"; then
  echo "ya hay un issue abierto para '$key'"
  exit 0
fi

{
  echo "El job nocturno de fuzzing termino el target \`$id\` **sin crash** y quedo en"
  echo "rojo por \`filter_known_ub.sh\`: UBSan reporto UB que no esta en"
  echo "\`tests/ci/known_ub.txt\`. No hay archivo \`crash-*\` porque UBSan corre con"
  echo "\`halt_on_error=0\`: imprime y sigue."
  echo
  echo "Sitios no listados (unicos, normalizados):"
  echo
  echo '```'
  printf '%s\n' "$nuevos"
  echo '```'
  echo
  echo "El log completo, con los stacks, esta en los artefactos de la corrida"
  echo "(\`artifacts/$id.log\`). Como no hay entrada que reproduzca, la regresion hay"
  echo "que fabricarla: los stacks dicen por donde entro, y el replay se corre asi:"
  echo
  echo '```'
  echo "make -C tests/fuzz .build/replay_$target"
  echo "$replay <archivo-fabricado>"
  echo '```'
  echo
  echo "Este issue se deduplica por \`$key\` mientras este ABIERTO: otras noches con"
  echo "la misma clase de UB en el mismo archivo no abren otro."
  echo
  echo "Corrida: $run_url"
} > "$body"
"$gh" issue create --title "$title" --label ci --body-file "$body"
