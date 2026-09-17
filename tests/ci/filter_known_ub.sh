#!/usr/bin/env bash
# Decide si los reportes de sanitizer de un log son UB YA CONOCIDO o UB NUEVO.
#
# Vivia adentro de run_sanitizers.sh. Se saco cuando el job nocturno de fuzzing
# (#34) necesito la misma decision: dos copias de "que UB se acepta" divergen
# -- una se actualiza y la otra no-, y el modo de fallar es el peor, porque el
# gate que se quedo viejo sigue verde.
#
# Uso:  filter_known_ub.sh <log> [known_ub.txt]
# Sale 0 si todo lo reportado esta en la lista, 1 si aparece algo que no,
# 2 si no se puede decidir (falta el log, falta la lista, o la lista tiene un
# patron mal formado). Los tres codigos son distintos a proposito: "hay UB
# nuevo" y "el gate esta roto" no son la misma noticia.
set -uo pipefail

log=${1:?uso: filter_known_ub.sh <log> [known_ub.txt]}
here=$(cd "$(dirname "$0")" && pwd)
known=${2:-"$here/known_ub.txt"}

[ -f "$log" ] || { echo "no existe el log: $log" >&2; exit 2; }
[ -f "$known" ] || { echo "no existe la lista: $known" >&2; exit 2; }

# --------------------------------------------------------------------------
# Que cuenta como reporte (#103).
#
# Esto era `runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer` y
# tenia un agujero que importa: ThreadSanitizer y MemorySanitizer NO se anuncian
# con `ERROR:` sino con `WARNING:`. Un data race o un uso de memoria sin
# inicializar pasaba entero por delante del gate sin que nadie lo viera, y el
# job quedaba verde. Tampoco entraba `Sanitizer:DEADLYSIGNAL`, que es como se
# ve un crash DENTRO del runtime del sanitizer: el caso en que menos se quiere
# estar en verde.
#
# La regla ahora es por FAMILIA y no por texto exacto: las cinco familias, con
# sus dos prefijos, mas DEADLYSIGNAL. Agregar una clase de UB no deberia
# requerir editar este grep; agregar un sanitizer nuevo, si.
detector='runtime error:|(ERROR|WARNING): (Address|Leak|Thread|Memory|UndefinedBehavior)Sanitizer|Sanitizer:DEADLYSIGNAL'

# --------------------------------------------------------------------------
# Normalizacion.
#
# Los reportes se agrupan por SITIO (archivo:linea:columna + tipo), no por texto
# crudo: cada reporte lleva la direccion concreta, asi que sin normalizar el
# mismo store desalineado aparece miles de veces y ahoga a cualquier hallazgo
# nuevo en el ruido.
#
# Tres cosas, y las tres tienen motivo:
#
#   1. El pid. `==2590==ERROR: LeakSanitizer...` cambia en cada corrida, asi que
#      sin normalizarlo el `sort -u` no deduplica NADA de ASan/TSan y el
#      inventario crece con ruido puro. Antes no se tocaba porque el strip de
#      ruta solo actuaba sobre lineas con `/`, y estas no tienen.
#   2. La ruta. Era `s/^.*[\/]//`, greedy: se comia hasta la ULTIMA barra de la
#      linea, asi que un mensaje que contuviera una barra despues del nombre de
#      archivo se llevaba puesto el nombre de archivo -- y el patron
#      `archivo|clase` dejaba de matchear sin que se notara. Ahora se saca solo
#      el prefijo de ruta que esta PEGADO al token `archivo.ext:linea:col`.
#   3. Las direcciones, como antes.
reports=$(grep -E "$detector" "$log" |
          sed -E -e 's/^==[0-9]+==/==PID==/' \
                 -e 's#(^|[[:space:]])[^[:space:]]*/([A-Za-z0-9_.+-]+\.(c|h|cc|cpp|hpp|inc):)#\1\2#g' \
                 -e 's/\(0x[0-9a-fA-F]*\)//g' -e 's/0x[0-9a-fA-F]*/0xADDR/g' |
          sort -u || true)

# --------------------------------------------------------------------------
# Validacion de los patrones (#103).
#
# Un patron es una subcadena, asi que nada impedia escribir `vdp_render.c` a
# secas -- que acepta CUALQUIER clase de UB en ese archivo, incluida una que
# todavia no existe- o directamente `error`, que acepta todo. Una excepcion
# demasiado ancha no se distingue de una razonable leyendo el archivo, y el dia
# que tapa un hallazgo real nadie se entera.
#
# Las tres reglas son las que ya cumplian de hecho las entradas historicas, solo
# que ahora estan escritas y se verifican:
#
#   - al menos DOS partes: archivo Y clase. Una sola parte siempre es demasiado
#     ancha para algo;
#   - la primera parte tiene que parecer un archivo fuente;
#   - ninguna parte de menos de 4 caracteres, que es el umbral por debajo del
#     cual una subcadena empieza a matchear por accidente.
#
# Falla con 2 y no con 1: una lista mal escrita no es "hay UB nuevo", es "el
# gate no se puede evaluar".
malformados=""
while IFS= read -r pattern; do
  case $pattern in ''|'#'*) continue ;; esac
  motivo=""
  case $pattern in
    *"|"*) ;;
    *) motivo="una sola parte: acepta cualquier clase de UB en ese archivo" ;;
  esac
  if [ -z "$motivo" ]; then
    primera=${pattern%%|*}
    case $primera in
      *.c|*.h|*.cc|*.cpp|*.hpp|*.inc) ;;
      *) motivo="la primera parte no es un archivo fuente" ;;
    esac
  fi
  if [ -z "$motivo" ]; then
    rest=$pattern
    while [ -n "$rest" ]; do
      part=${rest%%|*}
      case $rest in *"|"*) rest=${rest#*|} ;; *) rest="" ;; esac
      if [ "${#part}" -lt 4 ]; then
        motivo="la parte '$part' tiene menos de 4 caracteres"
        break
      fi
    done
  fi
  [ -n "$motivo" ] && malformados="$malformados  $pattern"$'\n'"    -> $motivo"$'\n'
done < <(tr -d '\r' < "$known")

if [ -n "$malformados" ]; then
  echo "== patrones mal formados en $known ==" >&2
  printf '%s' "$malformados" >&2
  echo "Un patron es 'archivo.ext|subcadena del mensaje', con al menos dos partes." >&2
  exit 2
fi

# --------------------------------------------------------------------------
# El cruce. Se cuenta ademas cuantos reportes matcheo CADA patron, para poder
# avisar de los que no matchean nada.
unknown=""
usados=""
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
    if [ "$matched" = 1 ]; then
      usados="$usados$pattern"$'\n'
      break
    fi
  done < <(tr -d '\r' < "$known")
  [ "$matched" = 0 ] && unknown="$unknown$line"$'\n'
done <<< "$reports"

if [ -n "$reports" ]; then
  echo "== reportes de sanitizer (unicos por sitio) =="
  printf '%s\n' "$reports"
fi

# Un patron que no matchea nada es una excepcion que sobrevivio a su causa: no
# protege de nada y sigue aceptando de antemano el dia que la causa vuelva. Se
# AVISA y no se falla, a proposito: este script corre sobre logs distintos -- el
# replay, y uno por target de fuzz- y un patron legitimo puede no aparecer en
# uno de ellos. Fallar aqui haria rojo un job por no haber reproducido un bug.
sin_usar=""
while IFS= read -r pattern; do
  case $pattern in ''|'#'*) continue ;; esac
  case $usados in *"$pattern"$'\n'*) ;; *) sin_usar="$sin_usar  $pattern"$'\n' ;; esac
done < <(tr -d '\r' < "$known")

if [ -n "$sin_usar" ]; then
  echo
  echo "== patrones que no matchearon nada en este log =="
  printf '%s' "$sin_usar"
  echo "Si tampoco matchean en los otros logs, la causa se arreglo: sacalos."
fi

if [ -n "${unknown//[$'\n' ]/}" ]; then
  echo
  echo "== UB NUEVO, no listado en known_ub.txt =="
  printf '%s' "$unknown"
  echo "Arreglar la causa. Ampliar known_ub.txt solo con justificacion explicita."
  exit 1
fi

echo "sin UB nuevo"
