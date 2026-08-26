# Integrar el core AYTHER ABI 1.9 en el Engine

Guion para pasar el Engine de `ayther-abi-1.3-r2` a `ayther-abi-1.9`. Va en el
orden en que conviene hacerlo: primero lo que no puede esperar (los binarios y
sus hashes), después lo que cambia de significado aunque no cambie de forma, y
al final cada capacidad nueva con su forma de uso y su prueba de aceptación.

Todo lo que dice este documento se puede verificar contra el header
(`core/ayther/ayther_api.h`) y contra la ABI escrita
(`docs/ayther_abi_v1.md`). Cuando los dos difieran, gana el header.

## 0. Resumen ejecutivo

| | 1.3-r2 | 1.9 |
|---|---|---|
| ABI | 1.3 | **1.9**, seis versiones aditivas: nada existente cambió de tamaño ni de orden |
| Regiones | 17 | **26** (`SYSTEM`, `LINE_REGS`, `LINE_CRAM`, `LINE_CELLS`, `RASTER_JOURNAL`, `FRAME_HASH`, `PALETTE`, `SPRITE_OUTCOME`, `Z80_RAM`) |
| Suscripciones | 8 bits (`0xFF`) | 12 bits (`AYTHER_SUB_ALL = 0xFFF`) |
| Capabilities | hasta el bit 13 | hasta el bit 19 |
| Estados nuevos | — | `DELTA_HISTORY_LOST` (-10), `UNSUPPORTED_MODE` (-11), `RC_*` (-20…-24) |
| Mode 4 (SMS / GG / PBC) | rechazado | supresión, captura, peel, recomposición pixel-perfect, Game Gear recortada bien |
| `build_id` | `…ABI 1.3; core v1.7.4 59443f41` | `Genesis Plus GX AYTHER ABI 1.9; core v1.7.4 31b93ee1` |

Un consumidor compilado contra 1.3 **sigue funcionando sin cambios** con estos
binarios: pide 1.3, recibe el mismo puntero, nunca lee más allá de su propio
`sizeof`. Lo que sigue es cómo aprovechar lo nuevo, y tres cambios de
comportamiento que conviene conocer aunque no se toque una línea (§2).

## 1. Los binarios

Assets de `ayther-abi-1.9`, con perfil y arquitectura en el nombre:

| asset | qué es | exports |
|---|---|---|
| `genesis_plus_gx_libretro_ayther_x64.dll` | x86_64, expone la ABI 1.9 | 33: `ayther_get_interface` + los 7 `audio_probe_*` + libretro |
| `genesis_plus_gx_libretro_ayther_x86.dll` | i386, ídem | 33 |
| `genesis_plus_gx_libretro_stock_x64.dll` | x86_64 **sin** la ABI (caso negativo de `abi_negociacion`) | 25, cero `ayther_*` |

Los SHA-256 están en las notas del release; en `third_party/cores/core.lock`
se actualizan **nombres y hashes juntos** (los nombres no cambian respecto a
1.3-r2, los hashes sí, los tres).

Compilados con el mismo toolchain que pinnea la CI (llvm-mingw MSVCRT
22.1.6-20260519) desde `master` en `31b93ee1`. Verificados contra el binario
publicado: closure de exports, `check-state-guard`, `check-full-core` (golden
`4d39e98fa62d8b4e` intacto), `check-scenes`, `check-multilayer`,
`check-plane-suppress`, `check-line-state`, `check-mode4`, `check-mode4-raster`,
`check-z80-vdp-fifo`, `check-observability`, `check-sprite-outcome`; el x86 con
un harness de 32 bits para lo que no depende del layout del estado.

### Savestates entre binarios

Un savestate lleva un **tag de layout**; uno de otra arquitectura (o de otro
sistema operativo) se rechaza limpio en `retro_unserialize` en vez de cargarse
corrido. Los estados viejos sin tag se aceptan. Nuevo en este release: un estado
tomado con el perfil `probe` carga en el perfil `estándar` y viceversa, y los
diez frames siguientes son idénticos (`check-state-cross-profile`, bloqueante
en CI). Un estado **corrupto** ya no puede tirar el proceso por CRAM fuera de
rango ni por el contexto del YM2612 (hallazgos del fuzzing, #60/#62): se acepta
o se rechaza, pero no crashea. El Engine no necesita envolver la carga en un
guard propio por eso; sí sigue debiendo mirar el `bool` que devuelve.

## 2. Tres cambios de comportamiento que afectan a un consumidor de 1.3

Ninguno cambia una firma. Los tres cambian lo que un valor **significa**.

1. **`0x10E` en Mode 4 ahora dice la verdad.** Hasta 1.3 un cartucho de
   Master System arrancaba cada frame con `UNSUPPORTED_MODE` y el Engine caía a
   fallback siempre. Desde #40 Mode 4 está soportado: la máscara arranca en
   cero, y desde este release los caminos de escritura del Z80 marcan `CRAM` y
   `VRAM` a mitad de frame igual que en Mode 5 (antes no marcaban: un split de
   paleta en SMS dejaba la máscara en cero y una recomposición equivocada sin
   aviso). Consecuencia: **en Mode 4 hay que volver a confiar en `0x10E`**, y
   no en "es SMS, fallback siempre".

2. **`v_counter` del journal es la primera línea que VE el cambio.** El core
   renderiza la línea N antes de correr la CPU durante N, así que una escritura
   "en la línea N" recién se dibuja en la N+1. El journal anotaba N; ahora anota
   N+1. Un consumidor que usaba `v_counter` para partir el frame en franjas
   obtiene ahora la franja correcta; uno que lo comparaba con un valor guardado
   ve todos sus eventos una línea más abajo. Efecto colateral correcto: una
   escritura durante la última línea visible ya no es raster (la dibujaría el
   blanking).

3. **`recompose_multilayer` con eventos de CRAM cambia de salida.** El replay
   escribía la entrada `n/2` de CRAM (leía un índice de entrada como offset en
   bytes) y aplicaba cada evento una línea antes. Con el fixture de siempre no
   se notaba porque converge al estado final; con escenas que deshacen el
   evento antes del fin del frame, se midió y se arregló (#35). Si el Engine
   guardaba hashes de capas recompuestas de frames con splits de paleta,
   cambian, y el valor nuevo es el correcto: cinco escenas raster son ahora
   pixel-perfect contra el frame emitido.

## 3. Negociación y descubrimiento

Sin cambios de protocolo; sí de lo que hay para descubrir.

```c
const ayther_interface_v1 *api = ayther_get_interface(0);
if (!api || AYTHER_ABI_VERSION_MAJOR(api->abi_version) != 1) -> stock, sin ABI
if (AYTHER_ABI_VERSION_MINOR(api->abi_version) < N)          -> falta lo que necesito
if (api->struct_size < offsetof(...) + sizeof(...))          -> el campo no existe en este binario
```

La regla es **major igual, minor ≥ el que necesito**, nunca `==`. Cada
capacidad nueva tiene su bit; comprobarlo antes de leer la región, y no deducir
la presencia de una región por la versión:

| bit | capability | desde | habilita |
|---:|---|---|---|
| 14 | `AYTHER_CAP_FRAME_DELTA_SINCE_V1` | 1.4 | `frame_delta_since` en el descriptor |
| 15 | `AYTHER_CAP_SYSTEM_V1` | 1.5 | región `SYSTEM` |
| 16 | `AYTHER_CAP_MODE4_CONTROLS` | 1.5 | los controles funcionan en Mode 4 (§5) |
| 17 | `AYTHER_CAP_LINE_STATE_V1` | 1.6 | `LINE_REGS`, `LINE_CRAM`, `LINE_CELLS` |
| 18 | `AYTHER_CAP_OBSERVABILITY_V1` | 1.7 | `RASTER_JOURNAL`, `FRAME_HASH`, `PALETTE` |
| 19 | `AYTHER_CAP_SPRITE_OUTCOME_V1` | 1.8 | `SPRITE_OUTCOME` |

`Z80_RAM` (1.9) no tiene bit propio: existe si `abi_version ≥ 1.9`, y
`query_region(AYTHER_REGION_Z80_RAM)` devuelve `NOT_FOUND` si no.

Estados que el Engine tiene que saber leer y no tenía en 1.3:

| valor | estado | cuándo |
|---:|---|---|
| -10 | `AYTHER_STATUS_DELTA_HISTORY_LOST` | `frame_delta_since` con una generación fuera del ring: **todo sucio** |
| -11 | `AYTHER_STATUS_UNSUPPORTED_MODE` | un control sin referente en el modo actual (ej. ocultar plano B en Mode 4) |
| -20…-24 | `AYTHER_STATUS_RC_NOT_MODE5` … `RC_JOURNAL_OVERFLOW` | motivos de la recomposición, traducidos de los internos |

`RC_JOURNAL_OVERFLOW` (-24) merece regla propia: `recompose_multilayer` lo
devuelve en vez de reproducir un prefijo del frame. Un prefijo produce una
imagen plausible y equivocada, que es lo que un frontend no puede detectar;
ante -24, fallback al frame emitido.

## 4. Suscripciones

Mismo protocolo (`get_subscriptions` → quedarse con `supported_mask` →
`set_subscriptions` entre frames → `retro_run` activa). Cuatro bits nuevos,
`AYTHER_SUB_ALL` pasa de `0xFF` a `0xFFF`:

| bit | suscripción | habilita | costo cuando está activa |
|---:|---|---|---|
| 7 | `AYTHER_SUB_ATTRIBUTION` | `ATTRIBUTION` (1.3) | un byte por pixel, en el clon observado |
| 8 | `AYTHER_SUB_LINE_STATE` | `LINE_REGS` | ~40 B por línea, capturados donde el renderer los usa |
| 9 | `AYTHER_SUB_LINE_CRAM` | `LINE_CRAM` | 128 B por línea **solo si la paleta cambió a mitad de frame**; si no, una entrada |
| 10 | `AYTHER_SUB_LINE_CELLS` | `LINE_CELLS` | el par de name table por columna; el dato ya estaba en un registro |
| 11 | `AYTHER_SUB_FRAME_HASH` | `FRAME_HASH` | ~100 KB recorridos por frame: es la única de 1.7 con suscripción propia |

Regla que no cambió y conviene recordar: sin suscripción, la región contesta
`NOT_SUBSCRIBED`; sin capability, `UNSUPPORTED`. Un `OK` con cero bytes es un
bug, no un estado (así se encontraron dos, #41 y #42).

Pedir sólo lo que se va a leer. El core mide en cada PR que un binario con la
ABI y **cero suscripciones** rinde igual que uno sin la ABI (overhead p50 en el
ruido del runner); cada bit activo mueve el renderer al clon observado, que
paga lo que captura.

## 5. Capacidad por capacidad

### 5.1 `SYSTEM` (1.5): dejar de decodificar registros

`read_region(AYTHER_REGION_SYSTEM, 0, &sys, sizeof(sys), AYTHER_GENERATION_ANY, NULL)`
devuelve un `ayther_system_v1`: `system_hw`, `region_pal`, **`vdp_mode`** (4, 5,
o 0 mientras el VDP no eligió), `interlace`, `h40`, `shadow_highlight`,
`lines_per_frame`, y el viewport. Solo lectura, **sin suscripción**, se llena
al leer.

- Usar `vdp_mode` para elegir el camino Mode 4 / Mode 5 en el Engine, en vez
  de leer `reg[1]` bit 2. Las reglas de decodificación ya se corrigieron una
  vez en el core (#28) y la copia del Engine no se enteró.
- `viewport_w/h` son las dimensiones del frame que llega por `video_refresh`:
  256×224 MD, 256×192 SMS, **160×144 Game Gear**. `viewport_x/y` es el offset
  del recorte: `(48,24)` sólo en Game Gear. `ATTRIBUTION` y `recompose_frame`
  describen **ese mismo rectángulo** (en 1.3 no era así en Game Gear:
  describían el VDP interno y la atribución indexaba corrida).

Prueba de aceptación en el Engine: cargar un `.gg`, exigir `vdp_mode == 4`,
`viewport 160x144`, `ATTRIBUTION` de 23 040 bytes y `recompose_frame` de
160×144 con 0 píxeles distintos del frame emitido.

### 5.2 Controles en Mode 4 (`AYTHER_CAP_MODE4_CONTROLS`)

Tabla control × modo, tal como está medida:

| control | Mode 5 | Mode 4 |
|---|---|---|
| `LAYER_MASK` bit sprites | sí | sí |
| `LAYER_MASK` bit A | plano A | **el** fondo (Mode 4 tiene uno solo) |
| `LAYER_MASK` bits B / W | sí | `UNSUPPORTED_MODE`: esos planos no existen |
| `SPRITE_SUPPRESS` (0x103) | sí | sí, misma máscara (la SAT tiene 64 entradas; se usan los primeros 64 bits) |
| `TILE_SUPPRESS` / peel (0x104) | revela B, o el backdrop donde B está vacío | revela el backdrop |
| `PLANE_TILE_SUPPRESS` (0x105) | sí | sí, misma clave (patrón, paleta): 9 y 1 bits en vez de 11 y 2 |
| `LAYER_DIM` (0x108) | sí | sí |
| mute / gain de audio (0x10D) | sí | sí |
| `ATTRIBUTION` | sí | sólo la capa de fondo |
| `SPRITE_OUTCOME` | seis bits | `PARSED`, `DRAWN`, `DROP_LINE`, `SUPPRESSED` (sin máscara x=0 ni presupuesto de píxeles) |
| recomposición | sí | sí, pixel-perfect (0 de 49 152 en SMS, 0 de 23 040 en GG) |
| captura de sprites | sí | sí, mismo layout |

Un `write_control` rechazado con `UNSUPPORTED_MODE` además levanta
`AYTHER_RASTER_REASON_UNSUPPORTED_CONTROLS` (bit 8) en `0x10E`, así que un
Engine que ya mira la máscara lo ve sin revisar cada retorno.

**Unidades de `ayther_sprite_v1` que se leen al revés de lo que parecen**
(no cambiaron: se documentaron, porque no estaban): `w`/`h` van en **celdas**
de 8×8, no en píxeles (Mode 4: siempre 1 de ancho, 1 o 2 de alto); `yr`/`xr`
son los valores **crudos** de la SAT, no coordenadas de pantalla, y en Mode 4
la Y cruda es la de pantalla **menos uno**. No hay campo `mode` en el sprite,
a propósito (rompería el indexado del array para consumidores ya compilados):
el modo lo da `SYSTEM`, una vez por frame.

### 5.3 `frame_delta_since` (1.4): dos lectores del mismo frame

`poll_frame_delta` **ya no consume**: devuelve el delta congelado del último
frame completo, así que el Lab y el motor HD pueden leer el mismo frame y ven
lo mismo (antes el segundo leía cero). `frame_delta_since(gen, &out, size)`
acumula todo lo sucio desde una generación; si `gen` quedó fuera del ring
devuelve `DELTA_HISTORY_LOST` con **todo marcado sucio**: es el resultado
seguro, no un error a ignorar.

### 5.4 Estado por scanline (1.6): de celda de pantalla a tile del plano

Es la pieza que un pipeline de sustitución HD necesita para **anclar un
asset**: la celda que el Engine puede ocultar hoy (0x104) es de espacio de
pantalla, y con scroll por línea nunca coincide con un tile del plano.

- `LINE_REGS`: cabecera `ayther_line_header_v1` (`struct_size`, `entry_size`,
  `lines`, `flags`, **`frame_generation`**) seguida de un `ayther_line_regs_v1`
  por línea: `xscroll_a/b`, `yscroll_a/b` **ya resueltos** desde la tabla de
  hscroll y VSRAM, bases `ntab/ntbb/ntwb/hscb/satb`, los registros que
  importan para reconstruir, el recorte de ventana y `flags`
  (`AYTHER_LINE_WINDOW_ACTIVE`, `AYTHER_LINE_VSCROLL_COLUMN`). La cabecera va
  *dentro* del buffer porque el consumidor necesita `lines` y la generación en
  la **misma** lectura.
- `LINE_CRAM`: 128 B por línea con la paleta vigente en esa línea. Si nadie
  escribió CRAM a mitad de frame, viene **una** entrada y el flag
  `AYTHER_LINES_CRAM_UNIFORM`: leer `flags` antes de indexar por línea.
- `LINE_CELLS`: por cada columna de 16 px de cada línea, el **par** de entradas
  de name table que el VDP leyó (`name_a/b/w[21]`) y la fila del tile
  (`row_*`, `shift_*`, por línea y por plano, no por columna). La región declara
  `AYTHER_REGION_WORD_SWAPPED_LE`: en un host little-endian los pares vienen con
  el orden de bytes interno del core; leerlos sin honrar el flag da celdas
  invertidas **sin ningún síntoma**.

Receta: `xscroll_a` de `LINE_REGS` para la línea + `name_a[col]` de
`LINE_CELLS` = el tile exacto (patrón, paleta, flips, prioridad) que ocupa esa
celda de pantalla en esa línea. Con eso el asset HD se ancla al tile y no al
píxel.

### 5.5 Observabilidad (1.7)

- **`RASTER_JOURNAL`**: `ayther_journal_v1` con `count`, `dropped` (cuenta, no
  bit) y hasta 256 `ayther_journal_event_v1` (`v_counter`, `reason`, `address`,
  `data`). Unidad de `address` según el motivo, ahora escrita en el header:
  `REG` = número de registro; `CRAM` = **índice de entrada (0-63)**;
  `VSRAM` = byte par; `HSCROLL` = dirección de VRAM. Gated en
  `AYTHER_SUB_RASTER_TRACKING`. Con esto el Engine puede saber *qué* partió el
  frame, no sólo que se partió.
- **`FRAME_HASH`**: `frame_index`, `video_hash`, `audio_hash`, `vram_hash`,
  `cram_hash`, `vsram_hash`, FNV-1a de 64 bits en el orden interno del core.
  Detecta desincronización sin serializar 1 MB. El de video recorre el frame
  **fila por fila**, `width` píxeles, saltando por `pitch`: para recomputarlo
  en el Engine hay que hacer lo mismo. No hay `state_hash` a propósito: eso es
  `retro_serialize`.
- **`PALETTE`**: los 256 colores ya resueltos al formato de píxel del build,
  con S/H aplicado, indexados por el byte del line buffer. **`element_size` lo
  da `query_region`**: no asumir 2 bytes. Es la tabla *actual*, no una por
  línea; para un frame con splits de paleta usar `LINE_CRAM`.

### 5.6 `SPRITE_OUTCOME` (1.8): qué le pasó a cada sprite

80 bytes, uno por slot de la SAT (region paralela, no un campo nuevo en
`ayther_sprite_v1`). Bits: `PARSED` (0x01), `DRAWN` (0x02), `DROP_LINE`
(0x04, se pasó el límite por línea), `DROP_PIXEL` (0x08, se pasó el
presupuesto de píxeles), `MASKED_X0` (0x10, tapado por la máscara de x=0),
`SUPPRESSED` (0x20, por 0x103). Responde la pregunta que antes obligaba a
recontar píxeles: "¿este sprite se dibujó o no, y por qué?". Un slot
`SUPPRESSED` no debe recibir sustitución HD; uno `DROP_*` se dibujó en
hardware… tampoco, y ahora se sabe cuál.

### 5.7 `Z80_RAM` (1.9): disparar sonido por id

Los 8 KB de RAM del Z80 (`0xA00000-0xA01FFF` desde el bus del 68000). Varios
juegos dejan ahí el id del tema a tocar, y sin la región la herramienta de
Sound Test se quedaba sin dónde mirar (pasó con Golden Axe). **Escribible**, y
ahí está el cuidado: escribir mientras el Z80 corre es una carrera. Dos
opciones válidas para el Engine: escribir con el bus tomado, o aceptar que lo
escrito puede durar un frame — que para disparar un tema por id alcanza, y para
cualquier otra cosa no.

### 5.8 Fallback raster: los bits que hay que decodificar

`0x10E` sigue siendo `valor > 0 = fallback` para un consumidor viejo. Bits
que el Engine debería distinguir ahora: `REG` (0), `CRAM` (1), `VSRAM` (2),
`HSCROLL` (3), `DMA` (4, origen), `UNSUPPORTED_MODE` (5), `VRAM` (6),
`JOURNAL_OVERFLOW` (7), `UNSUPPORTED_CONTROLS` (8). Reglas:

- `REG | CRAM | VSRAM | HSCROLL` son **reproducibles**: `recompose_multilayer`
  las rejuega desde el journal (en Mode 5). `VRAM` no: es fallback aunque el
  journal esté limpio.
- En **Mode 4 no hay replay** (`recompose_multilayer` es Mode 5 y devuelve
  `RC_NOT_MODE5`): todo motivo reproducible es ahí un fallback liso, y
  `recompose_frame` es correcto arriba del primer evento y equivocado abajo.
- `JOURNAL_OVERFLOW`: más de 256 eventos; fallback, no prefijo.
- `UNSUPPORTED_CONTROLS`: un control que el Engine pidió no aplica en este
  modo (p. ej. `PLANE_TILE_SUPPRESS` con *enhanced vscroll*); apagar la
  sustitución en vez de confiar en un resultado a medias.

Contrato completo: `docs/ayther_raster_fallback.md`.

## 6. Orden de integración recomendado

1. **`core.lock`**: los tres nombres (iguales) con los tres SHA-256 nuevos;
   `abi_negociacion` contra el stock tiene que seguir dando "sin ABI".
2. **Negociación**: aceptar minor ≥ 3 como hasta ahora; leer `capabilities`
   hasta el bit 19; mapear los estados -10, -11 y -20…-24.
3. **`SYSTEM`** en el arranque de cada contenido: modo, viewport, offsets.
   Reemplazar la decodificación de registros del Engine por esto.
4. **Mode 4**: quitar el "es SMS → fallback siempre"; confiar en `0x10E`;
   activar los controles con `AYTHER_CAP_MODE4_CONTROLS`; usar `viewport_x/y`
   para Game Gear.
5. **Journal**: si el Engine usaba `v_counter`, revisar el corrimiento de una
   línea (§2.2). Decodificar `address` por motivo.
6. **Sustitución HD**: `LINE_REGS` + `LINE_CELLS` para anclar por tile;
   `SPRITE_OUTCOME` para no sustituir lo que el hardware no dibujó;
   `ATTRIBUTION` ya describe el frame emitido en todos los sistemas.
7. **Sound Test**: `Z80_RAM` como segundo lugar donde buscar el id de tema.
8. **Desync / Lab**: `FRAME_HASH` bajo su suscripción sólo cuando el Lab está
   abierto; `PALETTE` con `element_size`.

Cada paso tiene su prueba en `tests/ayther/` (el mismo nombre que la
capacidad); replicarlas en el Engine contra los fixtures del repo
(`generated_rom*.c` no depende de ROMs comerciales) es la forma más barata de
saber que la integración hace lo que cree.
