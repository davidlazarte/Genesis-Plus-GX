#!/usr/bin/env bash
# Ningun archivo compartido con upstream cambia de fin de linea. (#43)
#
# La regla de .gitattributes es que lo de upstream se guarda con los MISMOS
# BYTES que upstream. Un archivo convertido entero al otro EOL diverge entero
# -a cambio de nada- y conflictua en cada sync; bajo rebase, una vez por commit.
#
# Esto ya se verificaba en CI, pero con dos limitaciones:
#
#   1. Miraba solo los archivos TOCADOS por el PR. Una conversion que entre en
#      un commit y que ningun PR posterior vuelva a tocar no se revisa nunca
#      mas: la ventana se corre y el archivo queda convertido para siempre.
#      Esto barre todo lo que hoy difiere del arbol de upstream.
#   2. Vivia adentro del workflow, asi que la unica forma de correrlo era
#      pushear. `libretro/Makefile.common` -136 lineas de ruido, convertido al
#      agregarle un flag- costo una corrida roja de CI justamente por eso.
#
# Compara los BLOBS de HEAD, no el working tree: lo que importa es lo que se va
# a pushear. Un working tree con otro EOL que el indice es un problema de
# configuracion local (core.autocrlf), no del repo, y no es lo que este chequeo
# tiene que cazar.
#
# Uso:  tests/ci/check_eol.sh [ref-de-upstream]     (default: upstream/master)

set -euo pipefail

ref=${1:-upstream/master}

if ! git rev-parse --verify --quiet "$ref" >/dev/null; then
  echo "no existe la ref '$ref'." >&2
  echo "corre: git remote add upstream https://github.com/libretro/Genesis-Plus-GX.git" >&2
  echo "       git fetch --no-tags upstream master" >&2
  exit 2
fi

status=0
checked=0

# Dos puntos, no tres: interesa TODO lo que hoy difiere del arbol de upstream,
# no lo que cambio desde una base comun. Un archivo identico no aparece; uno que
# difiere solo por el EOL, si -- que es justamente el que hay que cazar.
#
# `numstat` marca los binarios con '-': se saltean sin tener que buscar NUL.
while IFS=$'\t' read -r add _del path; do
  [ "$add" != "-" ] || continue
  git cat-file -e "$ref:$path" 2>/dev/null || continue   # no es de upstream
  checked=$((checked + 1))
  u=$(git cat-file blob "$ref:$path" | tr -cd '\r' | wc -c)
  m=$(git cat-file blob "HEAD:$path" | tr -cd '\r' | wc -c)
  if { [ "$u" -eq 0 ] && [ "$m" -gt 0 ]; } || { [ "$u" -gt 0 ] && [ "$m" -eq 0 ]; }; then
    echo "EOL distinto al de upstream: $path (upstream $u CR, nosotros $m CR)" >&2
    status=1
  fi
done < <(git -c core.quotePath=false diff --numstat --no-renames "$ref" HEAD)

if [ "$status" -ne 0 ]; then
  echo "" >&2
  echo "Un archivo con otro fin de linea que upstream diverge entero, a cambio" >&2
  echo "de nada, y en cada sync conflictua. Ver .gitattributes." >&2
  exit 1
fi

echo "EOL OK: $checked archivos compartidos con upstream, ninguno cambio de fin de linea"
