# Upstream Sync Runbook (AYTHER)

Cómo sincronizar este fork con upstream sin romper AYTHER y sin pelearse con el
historial.

## 0. Cuál es upstream

```
https://github.com/libretro/Genesis-Plus-GX.git
```

**No es `ekeeke/Genesis-Plus-GX`.** Este runbook decía eso y estaba mal: el
remoto real es el de libretro, que es el que trae el soporte libretro que
nosotros usamos. `libretro/master` ya integra lo de ekeeke, así que sincronizar
contra libretro nos deja al día con los dos.

```bash
git remote add upstream https://github.com/libretro/Genesis-Plus-GX.git
git fetch upstream
```

## 1. Políticas

### Rebase o merge: la regla real

La versión vieja de este runbook decía *«siempre preferir rebases incrementales
sobre merges gigantes»*, sin matices, y eso no se podía cumplir. La regla que sí
se cumple:

* **Ramas de tema (las nuestras, cortas): rebase.** Es barato, deja el historial
  legible, y es lo que la directiva original quería decir.
* **Syncs de upstream: merge.** Un sync trae decenas de commits de otra gente;
  rebasear nuestro trabajo sobre ellos reproduce cada commit nuestro contra la
  base nueva y multiplica los conflictos por la cantidad de commits.
* **Nunca reescribir historia ya publicada.** `aether/expose-vram-video-ram`
  está pusheada y mergeada a `master` varias veces. Rebasearla no es una
  operación de limpieza, es reescribir algo de lo que ya dependen otros refs.

El corolario práctico: **sincronizar seguido**. La política de «rebases
pequeños» era, en el fondo, una política de *rangos pequeños*. Eso se consigue
sincronizando a menudo, no eligiendo el verbo de git.

### Idioma: uno por archivo (#43)

* **Archivos propios** (`core/ayther/`, `tests/`, `bench/`, `docs/`, commits):
  español.
* **Líneas nuestras dentro de archivos de upstream** (`core/vdp_*.c`,
  `core/sound/*.c`, `libretro/libretro.c`, ...): inglés, y con el prefijo
  `AYTHER` y el `#N` del issue de este repo, para que quien lea el archivo
  entero encuentre un solo idioma y una sola forma de referirse a un tracker.

La regla aplica a lo que se escribe **de ahora en adelante**. Los comentarios
en español que ya están en archivos de upstream se quedan: reescribirlos es
tocar cientos de líneas que hoy no son superficie de conflicto para que pasen
a serlo, que es lo contrario de lo que este runbook persigue.

### Fin de línea: los archivos de upstream se guardan con los bytes de upstream

Upstream no tiene `.gitattributes` y guarda CRLF en la mayoría de los `.c`/`.h`.
Mientras nosotros normalizábamos todo a LF, **143 archivos diferían de upstream
por nada más que el fin de línea**: el 84% de nuestra divergencia aparente, y
cero de nuestro valor. En el sync de 24 commits eso costó 20 conflictos, y los 20
eran ruido: el único archivo con contenido real de los dos lados
(`libretro/Makefile.common`) automergeó solo.

Hoy `.gitattributes` dice `* -text`: git no convierte nada, y lo de upstream
queda byte a byte igual a upstream. Lo nuestro (`.sh`, `.github`, `tests`,
`bench`, `core/ayther`, `docs`) va en LF explícito con `text=auto eol=lf`.

Dos cosas que conviene no volver a aprender por las malas:

* **`eol=crlf` NO sirve** para igualar bytes con upstream. El blob de un archivo
  marcado `text` es *siempre* LF; `eol=` sólo decide cómo se ve en el working
  tree. Lo que consigue guardar bytes crudos es `-text`.
* Los `.sh` **necesitan** LF, no es preferencia: Git Bash aborta un script con
  CRLF (`$'\r': command not found`) y el job de Windows corre
  `check_exports.sh` igual que el de Linux.

Si aun así aparece un conflicto que es puro EOL, la red de seguridad es
`-Xrenormalize`, que normaliza ambos lados antes de comparar:

```bash
git merge -Xrenormalize upstream/master
```

### Aislamiento

Todo código AYTHER que no requiera intimidad con el VDP (helpers, inicialización
de estructuras) vive en `core/ayther/`. Cuanto más ahí, menos superficie de
parche en archivos de upstream, y más barato cada sync.

## 2. Procedimiento

```bash
git fetch upstream
git switch -c sync/upstream-AAAA-MM-DD master
git merge upstream/master          # agregar -Xrenormalize si aparece ruido de EOL
# resolver, verificar (sección 4), commitear
gh pr create --base master
```

Rama corta, PR, merge, y la rama se borra. Nadie rebasea 74 commits nunca.

## 3. Dónde miran nuestros parches

Medido contra `upstream/master` el 2026-08-26, **fuera** de lo que es
enteramente nuestro (`core/ayther/`, `core/debug/`, `tests/`, `bench/`, docs):

| Archivo | Líneas | Qué vive ahí |
|---|---:|---|
| `libretro/libretro.c` | 1668 | `retro_get_memory_data`, ABI AYTHER, inicialización |
| `core/vdp_render.c` | 1071 | hooks de `render_bg_*`/`render_obj_*`/`render_line`, `parse_satb_*` |
| `core/vdp_ctrl.c` | 427 | journal raster, `raster_dirty`, marcas en los caminos de escritura |
| `core/sound/psg.c` | 169 | telemetría PSG, dual path |
| `core/sound/sound.c` | 126 | loop del mixer |
| `core/sound/ym2612.c` | 116 | shadow registers, dual path, saneado al cargar estado |
| `core/sound/sound.h` | 112 | contrato del mixer |
| `core/vdp_render.h` | 106 | lo que `ayther_core.c` necesita ver |
| `core/vdp_render_internal.h` | 90 | idem, statics que dejaron de serlo |
| `core/cd_hw/pcm.c` | 74 | eventos PCM |
| `core/sound/ym3438.c` | 61 | mute por canal en Nuked |
| `core/vdp_ctrl.h` | 47 | símbolos del journal |
| `core/state.c` | 30 | latch de input hardware en el savestate |
| `libretro/Makefile.common` | 32 | perfiles `AYTHER_EXTENSIONS` / `SOUND_PROBE` |
| `core/input_hw/gamepad.c` | 22 | latch de fase TH |

Para regenerar la tabla:

```bash
git fetch upstream master
git diff upstream/master --stat | grep -v 'core/ayther\|core/debug\|tests/\|docs/\|bench/'
```

### La métrica que importa en `vdp_render.c`

El número de líneas engaña. Lo que decide cuánto duele un sync es **en cuántos
lugares** de un archivo de upstream estamos parados, porque cada uno es un
conflicto en potencia si upstream toca cerca. Medido el 2026-08-26:

```bash
git diff upstream/master -U0 -- core/vdp_render.c | grep -c '^@@'   # 147 hunks
git diff upstream/master -U0 -- core/vdp_render.c \
  | awk '/^@@/{if(h!="")print a+d" "h; h=$0; a=0; d=0; next} /^\+[^+]/{a++} /^-[^-]/{d++} END{print a+d" "h}' \
  | sort -rn | head                                                  # el mayor: 29 líneas
```

Son **147 hunks y ninguno pasa de 30 líneas**: los gates ya no son bloques de
cincuenta líneas dentro de un bucle (#43 partía de eso; `ayther_core.c` es una
unidad propia, los clones fast/observed salen de `AYTHER_DUAL_PATH`, y los
hooks viven en `core/ayther/*.h`). Lo que queda es la *cantidad* de puntos de
contacto, y bajarla exige otra forma de enganchar el renderer — no se hace
moviendo líneas. Cuando se intente, este par de comandos es la vara: menos
hunks, no menos líneas.

El resto (`core/vdp_render.h`, `core/sound/sound.h`, `core/vdp_ctrl.h`,
`core/shared.h`, `core/system.c`, `core/loadrom.c`, `core/debug/cpuhook.h`,
`libretro/link.T`, `Makefile.libretro`, `.gitignore`, los README) son parches
chicos.

`libretro/Makefile.common` merece atención aparte: es el que históricamente
choca de verdad, porque upstream también lo edita.

### Rebase de prueba: cuánto nos costaría sincronizar hoy

Se puede saber **sin tocar la rama de trabajo**: un worktree descartable, un
merge sin commit, la lista de archivos en conflicto, y se tira todo.

```bash
git fetch upstream master
git rev-list --count HEAD..upstream/master          # commits que nos faltan
git worktree add --detach /tmp/wt-upstream HEAD
( cd /tmp/wt-upstream \
  && git merge --no-commit --no-ff upstream/master >/dev/null 2>&1 \
  ; git diff --name-only --diff-filter=U \
  ; git merge --abort 2>/dev/null )
git worktree remove --force /tmp/wt-upstream
```

Los archivos que salgan tienen que estar en la tabla de arriba; uno que no
esté es un parche nuestro que nadie inventarió. Último resultado, 2026-08-26:
`master` al día con `upstream/master` (`b7e79b36`, 2026-08-21), 0 commits por
traer, 0 conflictos.

## 4. Verificación obligatoria

Antes de abrir el PR del sync, correr **todo** esto:

```bash
# 1. El core compila en el perfil principal
make -B -f Makefile.libretro platform=win AYTHER_EXTENSIONS=1 SOUND_PROBE=1 -j8

# 2. Suite completa + traza determinista
make -C tests check

# 3. Replay full-core contra el golden (save/load incluido)
make -C tests check-full-core CORE=../genesis_plus_gx_libretro.dll

# 4. Closure de exports: que no se haya escapado ni un símbolo AYTHER de más
llvm-readobj --coff-exports genesis_plus_gx_libretro.dll \
  | sed -n 's/^[[:space:]]*Name:[[:space:]]*\(\S*\)[[:space:]]*$/\1/p' | sort -u > /tmp/exp.txt
bash tests/ci/check_exports.sh /tmp/exp.txt 1 0 1

# 5. El perfil sin extensiones no filtra implementación
make -B -f Makefile.libretro platform=win AYTHER_EXTENSIONS=0 SOUND_PROBE=0 -j8
```

Si falla el determinismo (`audio_probe_trace`) o el replay full-core, **evaluar
la regresión antes de tocar el golden**. Ajustarlo sólo si el cambio de upstream
está fundamentado (por ejemplo, corrección de un bug de emulación) — y dejar
escrito en el commit *cuál* fue ese cambio.

Ojo con los hashes del replay: `video_hash`, `audio_hash` y `telemetry_hash`
dependen sólo de la emulación, pero `state_hash` depende del **layout
serializado**. Desde #45 hay **un solo golden** para Linux y Windows x64: la
divergencia que había no era de emulación sino de `rand()` en `gen_reset` (que
es de la libc) más el tag de layout metido en el hash del estado. Los detalles
están en `tests/README.md`.

Lo que sigue siendo cierto: entre el core x64 y el x86 los hashes de
video/audio/telemetría son idénticos pero `state_hash` no, porque el padding de
los structs y el ancho de puntero cambian el layout. Los savestates no son
portables entre arquitecturas — y ese es justamente el caso que el tag de layout
detecta, por lo que queda **fuera** del hash del estado.

## 5. Después del sync

Upstream actualiza sus binarios precompilados en `builds/`. **Eso está bien y no
hay que tocarlo**: `builds/` es de upstream, se deja intacto, y por eso dejó de
ser superficie de conflicto en los syncs.

Nuestros binarios NO viven ahí. Viven en los releases del fork, con perfil y
arquitectura explícitos en el nombre (`_ayther_x64`, `_ayther_x86`,
`_stock_x64`). Ver `builds/README.md`.

Hubo un tiempo en que sí vivían en `builds/`, bajo el mismo nombre que usa
upstream, y salió mal de la peor manera: upstream regenera
`genesis_plus_gx_libretro.dll` en cada release suyo, así que cada sync lo
sobreescribía. Durante todo el historial del fork ese directorio ofreció un core
sin una línea de AYTHER, y nadie lo notó porque el archivo estaba y se llamaba
como correspondía.

Si el sync cambia algo que afecte al core publicado, lo que hay que hacer es
**un release nuevo**, no tocar `builds/`. Y avisar los SHA-256 nuevos, porque el
CI del Engine los verifica contra `third_party/cores/core.lock`.

Los `.dol` de Gamecube/Wii siguen siendo los de upstream: necesitan devkitPPC,
que no está en la toolchain de este fork, y esas plataformas están fuera del
scope actual.
