#!/usr/bin/env bash
# Closure de la superficie AYTHER exportada (#32).
#
# Un solo verificador para Linux y Windows: la lista vive en allowed_exports.txt
# y este script decide. Antes la regla estaba escrita dos veces -en el workflow
# y en el .ps1- y habian divergido sin que nada lo detectara, porque cada job
# solo corre en su plataforma.
#
# Uso:
#   check_exports.sh <lista-de-simbolos-exportados> <extensions> <legacy> <probe>
#
# El primer argumento es un archivo con UN simbolo por linea (lo produce quien
# llama, con nm -D en ELF o dumpbin/llvm-readobj en PE: extraer los nombres es
# lo unico especifico de la plataforma).

set -euo pipefail

exports_file=${1:?falta el archivo de simbolos exportados}
extensions=${2:?falta AYTHER_EXTENSIONS}
legacy=${3:?falta AYTHER_LEGACY_PROFILE}
probe=${4:?falta SOUND_PROBE}

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
allowed_file="$here/allowed_exports.txt"

fail=0
note() { printf '%s\n' "$*" >&2; }

# --- lo que la lista permite para ESTE perfil ---
allowed=""
while read -r profile symbol _rest; do
  case $profile in
    ''|'#'*) continue ;;
  esac
  [ -n "${symbol:-}" ] || continue
  case $profile in
    base)   keep=$extensions ;;
    legacy) keep=$([ "$extensions" = 1 ] && [ "$legacy" = 1 ] && echo 1 || echo 0) ;;
    probe)  keep=$([ "$extensions" = 1 ] && [ "$probe" = 1 ] && echo 1 || echo 0) ;;
    *)      note "perfil desconocido en allowed_exports.txt: $profile"; exit 2 ;;
  esac
  [ "$keep" = 1 ] && allowed="$allowed $symbol"
done < <(tr -d '\r' < "$allowed_file" | sed 's/#.*//')

# --- los simbolos AYTHER que la biblioteca realmente exporta ---
# El CR es obligatorio de sacar: en Windows la lista la produce PowerShell y
# llega con CRLF, asi que sin esto cada simbolo termina en \r y no coincide con
# nada de la lista permitida. El modo de fallar es de los peores: el closure
# reporta que sobran los simbolos que en realidad estan bien.
found=$(tr -d '\r' < "$exports_file" | grep -E '^(ayther_|audio_probe_)' | sort -u || true)

# 1. Nada de mas: cada simbolo suelto es una superficie publica extra.
for symbol in $found; do
  case " $allowed " in
    *" $symbol "*) ;;
    *) note "export AYTHER inesperado para este perfil: $symbol"; fail=1 ;;
  esac
done

# 2. Nada de menos: la lista tambien dice lo que TIENE que estar, asi que un
#    simbolo que desaparece por un cambio de build no pasa desapercibido.
for symbol in $allowed; do
  if ! printf '%s\n' "$found" | grep -qx "$symbol"; then
    note "falta el export requerido para este perfil: $symbol"
    fail=1
  fi
done

# 3. El core sin extensiones no puede filtrar NADA, ni siquiera un simbolo
#    interno: es la unica prueba de que el fork es de costo cero apagado.
if [ "$extensions" != 1 ] && [ -n "$found" ]; then
  note "el build sin extensiones exporta simbolos AYTHER:"
  printf '%s\n' "$found" >&2
  fail=1
fi

if [ "$fail" = 0 ]; then
  printf 'export closure OK (extensions=%s legacy=%s probe=%s): %s\n' \
    "$extensions" "$legacy" "$probe" "$(printf '%s' "${allowed# }")"
fi
exit "$fail"
