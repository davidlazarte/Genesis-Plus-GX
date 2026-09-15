#!/usr/bin/env bash
# El gate de UB, probado contra logs sinteticos (#103).
#
# filter_known_ub.sh es lo unico que separa "el core esta limpio" de "el core
# tiene UB que nadie miro", y hasta #103 nadie lo habia probado: se confiaba en
# que un hallazgo real lo pusiera en rojo alguna vez. Un gate sin prueba
# negativa es exactamente lo que la casa no acepta en un test de regresion, y
# este es mas importante que un test de regresion porque decide por todos.
#
# Lo que se afirma aca es la tabla de verdad entera, no solo el caso feliz:
# un reporte que NO matchea tiene que salir 1, uno que SI matchea tiene que
# salir 0, y una lista rota tiene que salir 2 -- distinto de 1, porque "hay UB
# nuevo" y "el gate no se puede evaluar" son dos noticias distintas.
#
# Uso: check_filter_known_ub.sh
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
filtro="$here/filter_known_ub.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fallas=0
n=0

# corrida <esperado> <nombre> <contenido-lista> <contenido-log>
corrida() {
  local esperado="$1" nombre="$2" lista="$3" registro="$4"
  local salida rc
  n=$((n + 1))
  printf '%s\n' "$lista" > "$tmp/known.txt"
  printf '%s\n' "$registro" > "$tmp/log.txt"
  salida=$(bash "$filtro" "$tmp/log.txt" "$tmp/known.txt" 2>&1)
  rc=$?
  if [ "$rc" = "$esperado" ]; then
    printf '  ok    %-58s exit=%s\n' "$nombre" "$rc"
  else
    printf '  FALLA %-58s exit=%s (esperaba %s)\n' "$nombre" "$rc" "$esperado"
    printf '%s\n' "$salida" | sed 's/^/          /'
    fallas=$((fallas + 1))
  fi
}

# contiene <texto> <nombre> <lista> <log>
contiene() {
  local aguja="$1" nombre="$2" lista="$3" registro="$4"
  local salida
  n=$((n + 1))
  printf '%s\n' "$lista" > "$tmp/known.txt"
  printf '%s\n' "$registro" > "$tmp/log.txt"
  salida=$(bash "$filtro" "$tmp/log.txt" "$tmp/known.txt" 2>&1)
  case $salida in
    *"$aguja"*) printf '  ok    %-58s\n' "$nombre" ;;
    *) printf '  FALLA %-58s (no dijo "%s")\n' "$nombre" "$aguja"
       printf '%s\n' "$salida" | sed 's/^/          /'
       fallas=$((fallas + 1)) ;;
  esac
}

VACIA='# sin excepciones'
UB_VDP='core/vdp_render.c:1735:5: runtime error: store to misaligned address 0x7248e3a5bb1f for type '"'"'unsigned int'"'"''
UB_OTRO='core/sound/ym2612.c:2350:9: runtime error: index 289 out of bounds for type '"'"'uint32 [128]'"'"''

echo "== lo que tiene que FALLAR =="
corrida 1 "lista vacia + un UB cualquiera"            "$VACIA" "$UB_VDP"
corrida 1 "UB en otro ARCHIVO que el aceptado"        'vdp_render.c|store to misaligned address' "$UB_OTRO"
corrida 1 "UB de otra CLASE en el archivo aceptado"   'vdp_render.c|store to misaligned address' \
  'core/vdp_render.c:99:1: runtime error: index 5 out of bounds for type '"'"'int [4]'"'"''
corrida 1 "ASan heap-buffer-overflow"                 "$VACIA" \
  '==1234==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000118'
corrida 1 "LeakSanitizer"                             "$VACIA" \
  '==1234==ERROR: LeakSanitizer: detected memory leaks'

# Las dos familias que el detector viejo NO veia: se anuncian con WARNING, no
# con ERROR, asi que un data race o un uso de memoria sin inicializar pasaban
# enteros y el job quedaba verde.
corrida 1 "ThreadSanitizer (se anuncia con WARNING)"  "$VACIA" \
  '==1234==WARNING: ThreadSanitizer: data race (pid=1234)'
corrida 1 "MemorySanitizer (se anuncia con WARNING)"  "$VACIA" \
  '==1234==WARNING: MemorySanitizer: use-of-uninitialized-value'
# Un crash DENTRO del runtime del sanitizer: el caso en que menos se quiere
# estar en verde.
corrida 1 "DEADLYSIGNAL"                              "$VACIA" \
  'AddressSanitizer:DEADLYSIGNAL'

echo
echo "== lo que tiene que PASAR =="
corrida 0 "log sin ningun reporte"                    "$VACIA" 'todo bien por aca'
corrida 0 "el UB aceptado, con su patron"             'vdp_render.c|store to misaligned address' "$UB_VDP"
corrida 0 "mismo UB en otra linea del mismo archivo"  'vdp_render.c|store to misaligned address' \
  'core/vdp_render.c:1827:7: runtime error: store to misaligned address 0xdeadbeef for type '"'"'unsigned int'"'"''

echo
echo "== listas rotas: exit 2, distinto de 1 =="
corrida 2 "patron de una sola parte"                  'vdp_render.c' "$UB_VDP"
corrida 2 "patron que acepta cualquier cosa"          'error' "$UB_VDP"
corrida 2 "primera parte no es un archivo"            'store|misaligned' "$UB_VDP"
corrida 2 "una parte demasiado corta"                 'vdp_render.c|st' "$UB_VDP"

# El caso de "no existe" necesita que el archivo NO este, asi que no pasa por
# `corrida`, que siempre escribe una lista.
n=$((n + 1))
printf '%s\n' "$UB_VDP" > "$tmp/log.txt"
bash "$filtro" "$tmp/log.txt" "$tmp/no-existe.txt" >/dev/null 2>&1
rc=$?
if [ "$rc" = 2 ]; then
  printf '  ok    %-58s exit=2\n' "lista inexistente"
else
  printf '  FALLA %-58s exit=%s (esperaba 2)\n' "lista inexistente" "$rc"
  fallas=$((fallas + 1))
fi

echo
echo "== avisos =="
contiene "no matchearon nada" "un patron que sobrevivio a su causa se avisa" \
  'vdp_render.c|store to misaligned address' 'todo bien por aca'
contiene "UB NUEVO" "el UB nuevo se nombra en la salida" "$VACIA" "$UB_VDP"

echo
echo "== normalizacion =="
# Dos corridas del mismo leak con pid distinto tienen que colapsar en UNO.
n=$((n + 1))
printf '%s\n' '# sin excepciones' > "$tmp/known.txt"
{ echo '==111==ERROR: LeakSanitizer: detected memory leaks'
  echo '==222==ERROR: LeakSanitizer: detected memory leaks'; } > "$tmp/log.txt"
veces=$(bash "$filtro" "$tmp/log.txt" "$tmp/known.txt" 2>&1 | grep -c 'LeakSanitizer: detected')
if [ "$veces" = 2 ]; then   # una en el inventario, una en "UB NUEVO"
  printf '  ok    %-58s\n' "el pid se normaliza: dos corridas, un solo sitio"
else
  printf '  FALLA %-58s (aparecio %s veces, esperaba 2)\n' "el pid se normaliza" "$veces"
  fallas=$((fallas + 1))
fi

# Un mensaje con una barra DESPUES del nombre de archivo no puede comerse el
# nombre de archivo: si se lo come, el patron archivo|clase deja de matchear y
# el gate se pone rojo sin que haya UB nuevo.
corrida 0 "una barra en el mensaje no rompe el patron" \
  'vdp_render.c|misaligned address' \
  'core/vdp_render.c:10:1: runtime error: store to misaligned address 0xADDR for type '"'"'a/b'"'"''

echo
if [ "$fallas" = 0 ]; then
  echo "filtro de UB conocido: $n comprobaciones, 0 fallas"
else
  echo "filtro de UB conocido: $n comprobaciones, $fallas FALLAS" >&2
fi
exit $([ "$fallas" = 0 ] && echo 0 || echo 1)
