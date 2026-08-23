#!/usr/bin/env bash
# Primer frame y subsistema donde dos corridas del replay divergen (#38).
#
# Los goldens de Linux y Windows no coinciden y hasta ahora se convivia con un
# golden por plataforma. Para diagnosticarlo hace falta saber DONDE empieza la
# diferencia, no que el hash agregado difiere; con eso solo se puede bisectar a
# mano, que es la razon por la que el tema quedo abierto.
#
# Uso:
#   first_divergence.sh <a.frames.jsonl> <b.frames.jsonl> [nombre-a] [nombre-b]
#
# Los .jsonl los produce full_core_replay (cuarto argumento). Basta con bajar el
# artefacto de la otra plataforma y correr esto.

set -uo pipefail

a=${1:?falta el primer frames.jsonl}
b=${2:?falta el segundo frames.jsonl}
name_a=${3:-A}
name_b=${4:-B}

field() { # $1=linea json  $2=clave -> valor sin comillas
  printf '%s' "$1" | sed -n "s/.*\"$2\":\"\{0,1\}\([^,\"}]*\)\"\{0,1\}.*/\1/p"
}

# Solo el pase de referencia: los pases de replay repiten el mismo stream y
# duplicarian cada hallazgo.
mapfile -t rows_a < <(grep '"pass":"reference"' "$a")
mapfile -t rows_b < <(grep '"pass":"reference"' "$b")

if [ "${#rows_a[@]}" -eq 0 ] || [ "${#rows_b[@]}" -eq 0 ]; then
  echo "alguno de los dos archivos no tiene el pase de referencia" >&2
  exit 2
fi
if [ "${#rows_a[@]}" -ne "${#rows_b[@]}" ]; then
  echo "distinta cantidad de frames: $name_a=${#rows_a[@]} $name_b=${#rows_b[@]}" >&2
fi

n=${#rows_a[@]}
[ "${#rows_b[@]}" -lt "$n" ] && n=${#rows_b[@]}

keys="video audio state telemetry width height audio_frames fallback_reasons sprites audio_writes events"
found=0

for ((i = 0; i < n; ++i)); do
  diverged=""
  for key in $keys; do
    va=$(field "${rows_a[$i]}" "$key")
    vb=$(field "${rows_b[$i]}" "$key")
    [ "$va" = "$vb" ] || diverged="$diverged  $key: $name_a=$va $name_b=$vb"$'\n'
  done
  if [ -n "$diverged" ]; then
    frame=$(field "${rows_a[$i]}" frame)
    echo "primera divergencia en el frame $frame:"
    printf '%s' "$diverged"
    found=1
    break
  fi
done

if [ "$found" = 0 ]; then
  echo "sin divergencias por frame en $n frames del pase de referencia"
  echo "(si el hash agregado igual difiere, la causa esta en el resumen y no"
  echo " en los frames: revisar configuration_hash e input_hash)"
  exit 0
fi

echo
echo "El subsistema que aparece arriba acota la busqueda:"
echo "  video           -> renderer o estado del VDP"
echo "  audio           -> mixer/FM/PSG; sospechar punto flotante y orden de mezcla"
echo "  state           -> serializacion; sospechar padding o punteros no canonicos"
echo "  telemetry       -> captura AYTHER, no emulacion"
echo "  fallback_reasons-> tracking raster, no emulacion"
exit 1
