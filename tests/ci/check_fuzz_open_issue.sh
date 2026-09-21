#!/usr/bin/env bash
# fuzz_open_issue.sh, probado con un `gh` falso (#134).
#
# El paso que abre issues en el nocturno no se puede ejercitar en un PR: corre
# una vez por noche, solo si algo fallo, y contra la API de GitHub. Hasta #134
# eso significo que su unico caso ciego -el job en rojo por UB recuperable, sin
# `crash-*`- estuvo dos noches sin abrir nada y nadie lo supo. Aca se afirma la
# tabla entera con logs sinteticos y un `gh` que anota lo que le piden:
#
#   con archivo   / sin issue previo           -> abre, con el hash en el titulo
#   con archivo   / issue previo (aun CERRADO) -> no abre
#   sin archivo   / UB no listado              -> abre, con `archivo clase`
#   sin archivo   / mismo UB, issue ABIERTO    -> no abre
#   sin archivo   / mismo UB, issue CERRADO    -> abre (la clase volvio)
#   sin archivo   / otra linea, misma clase    -> misma clave: no abre
#   sin archivo   / log sin UB (build roto)    -> no abre, y lo dice
#   sin archivo   / log estilo gcc, sin SUMMARY-> abre con la clase generica
#
# Uso: check_fuzz_open_issue.sh
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
script="$here/fuzz_open_issue.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# El gh falso. `issue list --state <s>` contesta los titulos de $tmp/<s>.txt
# (y `all` los dos); `issue create` anota el titulo y guarda el cuerpo.
cat > "$tmp/gh" <<'EOF'
#!/usr/bin/env bash
dir=$(cd "$(dirname "$0")" && pwd)
if [ "$1 $2" = "issue list" ]; then
  state=open
  while [ $# -gt 0 ]; do [ "$1" = "--state" ] && state=$2; shift; done
  case $state in
    all) cat "$dir/open.txt" "$dir/closed.txt" 2>/dev/null ;;
    *)   cat "$dir/$state.txt" 2>/dev/null ;;
  esac
  exit 0
fi
if [ "$1 $2" = "issue create" ]; then
  while [ $# -gt 0 ]; do
    [ "$1" = "--title" ] && printf '%s\n' "$2" >> "$dir/created.txt"
    [ "$1" = "--body-file" ] && cp "$2" "$dir/body.md"
    shift
  done
  exit 0
fi
exit 1
EOF
chmod +x "$tmp/gh"

fallas=0
n=0

LOG_CLANG='core/m68k/m68kops.h:3956:14: runtime error: shift exponent 61 is too large for 32-bit type '"'"'unsigned int'"'"'
    #0 0x7f6ded832561 in m68k_op_asl_8_r /home/runner/work/x/x/core/m68k/m68kops.h:3956:14
SUMMARY: UndefinedBehaviorSanitizer: invalid-shift-exponent core/m68k/m68kops.h:3956:14
Done 19306 runs in 601 second(s)'
LOG_OTRA_LINEA=${LOG_CLANG//3956/9657}
LOG_GCC='core/sound/opll.c:524:22: runtime error: index 176 out of bounds for type '"'"'uint32_t [16]'"'"'
Done 100 runs in 601 second(s)'
LOG_LIMPIO='make: *** [Makefile:12: fuzz-generated_rom] Error 2'

T_HASH='[fuzz][generated_rom] hallazgo 0123456789abcdef0123456789abcdef01234567'
T_UB='[fuzz][generated_rom] UB sin caso: m68kops.h invalid-shift-exponent'
T_GCC='[fuzz][unserialize-sms-fm] UB sin caso: opll.c ub-sin-clasificar'

# caso <nombre> <titulo-esperado|-> <con-archivo 0|1> <abiertos> <cerrados> <log> [id target escena]
caso() {
  local nombre="$1" esperado="$2" con_archivo="$3" abiertos="$4" cerrados="$5" registro="$6"
  local id="${7:-generated_rom}" target="${8:-generated_rom}" escena="${9:-md}"
  local creado rc
  n=$((n + 1))
  rm -rf "$tmp/art" "$tmp/created.txt" "$tmp/body.md"
  mkdir -p "$tmp/art"
  [ "$con_archivo" = 1 ] && : > "$tmp/art/crash-0123456789abcdef0123456789abcdef01234567"
  printf '%s\n' "$abiertos" > "$tmp/open.txt"
  printf '%s\n' "$cerrados" > "$tmp/closed.txt"
  printf '%s\n' "$registro" > "$tmp/log.txt"
  AYTHER_GH="$tmp/gh" bash "$script" "$id" "$target" "$escena" "$tmp/art" "$tmp/log.txt" "https://example.invalid/run/1" > "$tmp/out.txt" 2>&1
  rc=$?
  creado=$(cat "$tmp/created.txt" 2>/dev/null || true)
  [ -n "$creado" ] || creado="-"
  if [ "$rc" = 0 ] && [ "$creado" = "$esperado" ]; then
    printf '  ok    %s\n' "$nombre"
  else
    printf '  FALLA %s\n          exit=%s, abrio: %s\n          esperaba: %s\n' "$nombre" "$rc" "$creado" "$esperado"
    sed 's/^/          /' "$tmp/out.txt"
    fallas=$((fallas + 1))
  fi
}

# afirma <nombre> <comando...>: sobre lo que dejo el ultimo caso.
afirma() {
  local nombre="$1"; shift
  n=$((n + 1))
  if "$@" > /dev/null 2>&1; then printf '  ok    %s\n' "$nombre"
  else printf '  FALLA %s\n' "$nombre"; fallas=$((fallas + 1)); fi
}

echo "== con archivo =="
caso "crash sin issue previo: abre con el hash"            "$T_HASH" 1 "" "" "$LOG_CLANG"
afirma "  el cuerpo trae el replay con --scene"            grep -q -- "replay_generated_rom --scene md" "$tmp/body.md"
caso "crash con issue ABIERTO del mismo hash: no abre"     "-"       1 "$T_HASH" "" "$LOG_CLANG"
caso "crash con issue CERRADO del mismo hash: no abre"     "-"       1 "" "$T_HASH" "$LOG_CLANG"
caso "crash y ademas UB: gana el archivo"                  "$T_HASH" 1 "" "" "$LOG_CLANG"

echo "== sin archivo =="
caso "UB no listado: abre con archivo y clase"             "$T_UB"   0 "" "" "$LOG_CLANG"
afirma "  el cuerpo lista el sitio"                        grep -q "m68kops.h:3956:14" "$tmp/body.md"
afirma "  el cuerpo trae la corrida"                       grep -q "https://example.invalid/run/1" "$tmp/body.md"
caso "mismo UB con issue ABIERTO: no abre"                 "-"       0 "$T_UB" "" "$LOG_CLANG"
caso "otra linea, misma clase, issue ABIERTO: no abre"     "-"       0 "$T_UB" "" "$LOG_OTRA_LINEA"
caso "mismo UB con issue CERRADO: abre (la clase volvio)"  "$T_UB"   0 "" "$T_UB" "$LOG_CLANG"
caso "issue abierto PARECIDO pero de otro target: abre"    "$T_UB"   0 "[fuzz][recompose] UB sin caso: m68kops.h invalid-shift-exponent" "" "$LOG_CLANG"
caso "log sin UB (build roto): no abre"                    "-"       0 "" "" "$LOG_LIMPIO"
afirma "  y dice que hay que leer el log"                  grep -q "hay que leerlo a mano" "$tmp/out.txt"
caso "log estilo gcc, sin SUMMARY: clase generica"         "$T_GCC"  0 "" "" "$LOG_GCC" unserialize-sms-fm unserialize sms-fm
afirma "  el replay usa target y escena, no el id"         grep -q -- "replay_unserialize --scene sms-fm" "$tmp/body.md"

echo
echo "fuzz_open_issue: $n comprobaciones, $fallas fallas"
[ "$fallas" = 0 ]
