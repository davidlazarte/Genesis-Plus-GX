#!/usr/bin/env bash
# Corre el probe de raster sobre una coleccion LOCAL y deja el informe. (#35.4)
#
# Los ROMs comerciales no estan en el repo ni pueden estarlo, asi que esta
# validacion es opt-in y no corre en CI. Lo que si tiene que ser es
# REPRODUCIBLE: `docs/validation/raster-roms-2026-08-09.md` existe desde hace
# meses y se genero a mano, o sea que nadie mas podia rehacerlo ni saber con que
# comando salio.
#
# Uso:
#   tests/ci/validate_roms.sh <core> <directorio-de-roms> [frames] [checkpoint]
#
# Con <checkpoint> (#117) el probe ademas guarda el estado en ese frame y
# afirma que la continuacion restaurada reproduce el video y el audio de la
# original, frame a frame; el informe registra la entrada, el perfil del core
# (build_id, SOUND_PROBE por sus exports) y el checkpoint.
#
# Escribe docs/validation/raster-roms-<fecha>.md. No guarda ni un byte del
# contenido de los ROMs: solo nombre, tamanio, CRC y el resultado del contrato.

set -euo pipefail

core=${1:?falta la ruta al core}
rom_dir=${2:?falta el directorio de ROMs}
frames=${3:-1800}
checkpoint=${4:-}

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
probe="$root/tests/.build/raster_rom_probe"
[ -x "$probe" ] || probe="$probe.exe"

if [ ! -x "$probe" ]; then
  echo "falta el probe: corre  make -C tests raster-rom-probe" >&2
  exit 2
fi
if [ ! -d "$rom_dir" ]; then
  echo "no existe el directorio $rom_dir" >&2
  exit 2
fi

# Los ROMs se buscan por extension, no por contenido: si el directorio tiene
# otra cosa, es mejor no cargarla que adivinar. Mega Drive y tambien
# Master System / Game Gear: el probe verifica el contrato de
# recomposicion, y ese contrato tiene que valer en los dos modos.
roms=()
while IFS= read -r -d '' f; do roms+=("$f"); done < <(
  find "$rom_dir" -maxdepth 1 -type f \
    \( -iname '*.md' -o -iname '*.bin' -o -iname '*.gen' -o -iname '*.smd' \
       -o -iname '*.sms' -o -iname '*.gg' -o -iname '*.sg' \) \
    -print0 | sort -z)

if [ "${#roms[@]}" -eq 0 ]; then
  echo "no hay ROMs (.md .bin .gen .smd .sms .gg .sg) en $rom_dir" >&2
  exit 2
fi

date_tag=$(date +%Y-%m-%d)
# Con checkpoint el informe es otro archivo: el de raster a secas no se pisa.
out="$root/docs/validation/raster-roms-$date_tag${checkpoint:+-checkpoint}.md"
log=$(mktemp)
trap 'rm -f "$log"' EXIT

echo "probe:  $probe"
echo "core:   $core"
echo "roms:   ${#roms[@]} en $rom_dir"
echo "frames: $frames por ROM"
[ -n "$checkpoint" ] && echo "checkpoint: frame $checkpoint, continuacion restaurada hasta $frames"
echo

status=0
checkpoint_args=()
[ -n "$checkpoint" ] && checkpoint_args=(--checkpoint "$checkpoint")
"$probe" --frames "$frames" "${checkpoint_args[@]}" "$core" "${roms[@]}" 2>&1 | tee "$log" || status=$?

mkdir -p "$root/docs/validation"
{
  echo "# Raster fallback validation with local ROMs — $date_tag"
  echo
  echo "Generado con \`tests/ci/validate_roms.sh\`. No se guarda ni un byte del"
  echo "contenido de los ROMs: solo el resultado del contrato."
  echo
  echo '## Como se rehace'
  echo
  echo '```sh'
  echo 'make -C tests raster-rom-probe'
  echo "tests/ci/validate_roms.sh <core> <directorio-de-roms> $frames${checkpoint:+ $checkpoint}"
  echo '```'
  echo
  echo '## Corrida'
  echo
  echo "- core: \`$(basename "$core")\`"
  echo "- ROMs: ${#roms[@]}"
  echo "- frames por ROM: $frames"
  if [ -n "$checkpoint" ]; then
    echo "- checkpoint: estado guardado en el frame $checkpoint; continuacion $checkpoint..$frames corrida dos veces (original y restaurada)"
  fi
  echo "- core (linea \`core\` del probe): \`$(grep -m1 '^{"type":"core"' "$log" || echo '?')\`"
  echo "- resultado: $([ "$status" -eq 0 ] && echo 'sin violaciones del contrato' || echo "FALLO (exit $status)")"
  echo
  echo '## Salida del probe'
  echo
  echo '```'
  cat "$log"
  echo '```'
} > "$out"

echo
echo "informe: $out"
exit "$status"
