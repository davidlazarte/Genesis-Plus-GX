#!/usr/bin/env bash
# La deteccion de shell de mk/shell.mk, probada (#104).
#
# El bug que motiva esto no se ve leyendo el codigo: la condicion era
# `$(findstring sh,$(notdir $(SHELL)))`, y "pwsh" y "powershell" CONTIENEN "sh".
# Con SHELL=pwsh.exe la deteccion elegia la rama POSIX y le mandaba
# `test -n ... || (echo ...; exit 2)` a PowerShell, que contesta "The term
# 'test' is not recognized". Y la receta terminaba en 1 en vez de 2, asi que el
# rechazo de un CORE ausente dejaba de distinguirse de cualquier otra falla.
#
# Una subcadena responde "se parece a"; lo que hace falta saber es "es". Por eso
# ahora hay lista blanca, y por eso hay este test: la proxima vez que alguien
# toque la deteccion, el falso positivo tiene que aparecer aca y no en el runner
# de Windows tres semanas despues.
#
# Dos decisiones del metodo, las dos aprendidas rompiendose la cara contra ellas:
#
#   El SHELL de cada caso se escribe DENTRO del makefile de prueba, no se pasa
#   por la linea de comandos. Medido: `make SHELL="C:/Program Files/.../pwsh.exe"`
#   se ignora en silencio -- make se queda con el suyo-- y el test pasaba a
#   probar el parser de make en vez del modulo. Adentro del makefile no hay
#   ambiguedad.
#
#   El sabor se lee con $(info) y `make -n`, asi que se puede preguntar por
#   shells que NO existen en esta maquina -- pwsh en Linux, por ejemplo-- sin
#   ejecutar una sola receta.
#
# Uso: check_shell_detection.sh
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
raiz=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# El `include` va ADENTRO de un makefile generado, asi que no pasa por la
# conversion de rutas de MSYS -- esa solo toca los argumentos de la linea de
# comandos. Sin cygpath, en Git Bash el make de Windows recibe "/c/Users/..." y
# no encuentra nada, y el test falla por su propia culpa en vez de por el
# modulo, que es la peor forma de fallar que puede tener un test.
if command -v cygpath >/dev/null 2>&1; then
  raiz_mk=$(cygpath -m "$raiz")
else
  raiz_mk=$raiz
fi

fallas=0
n=0

# arma <archivo> <SHELL> [AYTHER_SHELL_FLAVOR]
arma() {
  {
    [ -n "${2:-}" ] && printf 'AYTHER_SHELL_PROBE := %s\n' "$2"
    [ -n "${3:-}" ] && printf 'AYTHER_SHELL_FLAVOR := %s\n' "$3"
    printf 'include %s/mk/shell.mk\n' "$raiz_mk"
    printf '$(info FLAVOR=$(SHELL_FLAVOR))\n'
    printf '$(info NAME=$(SHELL_NAME))\n'
    printf 'nada:\n'
    printf '\t@echo nada\n'
  } > "$1"
}

# sabor <esperado> <nombre> <SHELL> [FLAVOR]
sabor() {
  local esperado="$1" nombre="$2" sh="$3" ov="${4:-}"
  local salida
  n=$((n + 1))
  arma "$tmp/p.mk" "$sh" "$ov"
  salida=$(make -f "$tmp/p.mk" -n nada 2>&1)
  case $salida in
    *"FLAVOR=$esperado"*) printf '  ok    %-52s -> %s\n' "$nombre" "$esperado" ;;
    *) printf '  FALLA %-52s (esperaba %s)\n' "$nombre" "$esperado"
       printf '%s\n' "$salida" | head -3 | sed 's/^/          /'
       fallas=$((fallas + 1)) ;;
  esac
}

# rechaza <nombre> <SHELL>
rechaza() {
  local nombre="$1" sh="$2"
  local salida rc
  n=$((n + 1))
  arma "$tmp/p.mk" "$sh" ""
  salida=$(make -f "$tmp/p.mk" -n nada 2>&1)
  rc=$?
  # Sin pipeline: con `set -o pipefail`, `make ... | grep -q` da falso negativo
  # porque make sale distinto de cero justamente cuando el test quiere pasar.
  if [ "$rc" != 0 ] &&
     case $salida in *"no se de que sabor es"*) true ;; *) false ;; esac &&
     case $salida in *"AYTHER_SHELL_FLAVOR"*) true ;; *) false ;; esac; then
    printf '  ok    %-52s -> rechazado (exit=%s) y dice como forzarlo\n' "$nombre" "$rc"
  else
    printf '  FALLA %-52s (exit=%s)\n' "$nombre" "$rc"
    printf '%s\n' "$salida" | head -3 | sed 's/^/          /'
    fallas=$((fallas + 1))
  fi
}

echo "== shells POSIX: la rama posix =="
sabor posix "sh"                        /bin/sh
sabor posix "bash"                      /bin/bash
sabor posix "dash"                      /usr/bin/dash
sabor posix "zsh"                       /usr/bin/zsh
sabor posix "busybox"                   /bin/busybox
sabor posix "sh.exe de Git for Windows" "C:/Git/usr/bin/sh.exe"
# El que casi se rompe al arreglar esto: $(notdir) parte por ESPACIOS, asi que
# "Program Files" le sale como dos palabras y la lista blanca no matchea nada.
sabor posix "ruta CON ESPACIOS"         "C:/Program Files/Git/usr/bin/sh.exe"
sabor posix "ruta con backslashes"      'C:\Git\usr\bin\sh.exe'

echo
echo "== cmd: la rama cmd =="
sabor cmd "cmd.exe"                     cmd.exe
sabor cmd "ruta completa a cmd"         "C:/WINDOWS/System32/cmd.exe"
sabor cmd "CMD.EXE en mayusculas"       "C:/WINDOWS/System32/CMD.EXE"

echo
echo "== PowerShell: ni una cosa ni la otra, y hay que decirlo =="
# Este es EL bug: antes las tres caian en la rama POSIX por contener "sh".
rechaza "pwsh.exe"                      pwsh.exe
rechaza "powershell.exe"                powershell.exe
rechaza "ruta completa a pwsh, con espacios" "C:/Program Files/PowerShell/7/pwsh.exe"

echo
echo "== la salida de emergencia =="
sabor posix "AYTHER_SHELL_FLAVOR=posix sobre un shell raro" /opt/raro/mishell posix
sabor cmd   "AYTHER_SHELL_FLAVOR=cmd sobre un shell raro"   /opt/raro/mishell cmd
# Y tambien desde la linea de comandos, que es como lo va a usar una persona.
n=$((n + 1))
arma "$tmp/p.mk" pwsh.exe ""
if make -f "$tmp/p.mk" -n AYTHER_SHELL_FLAVOR=cmd nada >/dev/null 2>&1; then
  printf '  ok    %-52s\n' "el override tambien vale por linea de comandos"
else
  printf '  FALLA %-52s\n' "el override no vale por linea de comandos"
  fallas=$((fallas + 1))
fi
n=$((n + 1))
arma "$tmp/p.mk" /bin/sh perl
if make -f "$tmp/p.mk" -n nada >/dev/null 2>&1; then
  printf '  FALLA %-52s\n' "un valor invalido del override se acepta"
  fallas=$((fallas + 1))
else
  printf '  ok    %-52s\n' "un valor invalido del override se rechaza"
fi

echo
echo "== el cableado: sin costura, se mira el \$(SHELL) de verdad =="
# Todo lo de arriba entra por AYTHER_SHELL_PROBE, que es la unica forma de
# probar la tabla entera (make descarta un SHELL que no exista en disco). Este
# caso no pasa costura: confirma que cuando nadie la usa, lo que se mira es el
# shell que make eligio de verdad -- y en cualquier maquina donde corra este
# test, ese shell es POSIX.
n=$((n + 1))
arma "$tmp/p.mk" "" ""
salida=$(make -f "$tmp/p.mk" -n nada 2>&1)
case $salida in
  *"FLAVOR=posix"*) printf '  ok    %-52s -> posix\n' "el \$(SHELL) real de esta maquina" ;;
  *) printf '  FALLA %-52s\n' "el \$(SHELL) real de esta maquina"
     printf '%s\n' "$salida" | head -3 | sed 's/^/          /'
     fallas=$((fallas + 1)) ;;
esac

echo
echo "== la comprobacion de CORE, con el shell de verdad de esta maquina =="
core_probe="$tmp/core.mk"
{
  printf 'include %s/mk/shell.mk\n' "$raiz_mk"
  printf 'pide:\n'
  printf '\t$(call require_core)\n'
  printf '\t@echo LLEGUE\n'
} > "$core_probe"

n=$((n + 1))
salida=$(make -f "$core_probe" --no-print-directory pide 2>&1)
rc=$?
ruido=$(printf '%s\n' "$salida" |
        grep -icE "not recognized|no se reconoce|command not found|CommandNotFound|ParserError|syntax error")
if [ "$rc" = 2 ] && [ "$ruido" = 0 ]; then
  printf '  ok    %-52s -> exit=2, sin ruido\n' "sin CORE: rechaza"
else
  printf '  FALLA %-52s (exit=%s, lineas de ruido=%s)\n' "sin CORE: rechaza" "$rc" "$ruido"
  printf '%s\n' "$salida" | head -4 | sed 's/^/          /'
  fallas=$((fallas + 1))
fi

n=$((n + 1))
salida=$(make -f "$core_probe" --no-print-directory CORE=/un/core/cualquiera pide 2>&1)
rc=$?
case $salida in
  *LLEGUE*)
    if [ "$rc" = 0 ]; then
      printf '  ok    %-52s -> exit=0\n' "con CORE: deja pasar"
    else
      printf '  FALLA %-52s (exit=%s)\n' "con CORE: deja pasar" "$rc"
      fallas=$((fallas + 1))
    fi ;;
  *) printf '  FALLA %-52s (no ejecuto la receta)\n' "con CORE: deja pasar"
     printf '%s\n' "$salida" | head -3 | sed 's/^/          /'
     fallas=$((fallas + 1)) ;;
esac

echo
if [ "$fallas" = 0 ]; then
  echo "deteccion de shell: $n comprobaciones, 0 fallas"
else
  echo "deteccion de shell: $n comprobaciones, $fallas FALLAS" >&2
fi
[ "$fallas" = 0 ]
