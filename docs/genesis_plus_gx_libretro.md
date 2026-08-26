# Genesis Plus GX (core libretro) — Documentación técnica

> Documentación del core **`genesis_plus_gx_libretro`** con énfasis en las
> herramientas y capacidades que ofrece para la **emulación de Mega Drive /
> Genesis**. Cubre la interfaz libretro completa, el núcleo de emulación y los
> *deltas* propios del fork **AYTHER** (rama `aether/expose-vram-video-ram`).
>
> Versión del core: **Genesis Plus GX v1.7.4** (`retro_get_system_info`,
> [libretro/libretro.c:3128](../libretro/libretro.c)). Savestate del fork:
> **STATE_VERSION 1.7.7**.

---

## 1. Visión general

Genesis Plus GX es un emulador open-source de las consolas **Sega de 8 y 16
bits**, centrado en **precisión** y **portabilidad**. El core libretro es la
encarnación del emulador como librería dinámica (`.dll`/`.so`/`.dylib`) que se
carga desde un frontend libretro (RetroArch, etc.) o desde un runner embebido
(como el **Lab de AYTHER**).

### Sistemas emulados

| Familia | Sistemas (`system_hw`) | Notas |
|---|---|---|
| 8-bit Z80 | SG-1000, SG-1000 II (+RAM ext.), Mark III | `SYSTEM_SG`, `SYSTEM_SGII`, `SYSTEM_MARKIII` |
| 8-bit Z80 | Master System, Master System II | `SYSTEM_SMS`, `SYSTEM_SMS2` / `SYSTEM_GGMS` (modo SMS en GG) |
| 8-bit Z80 | Game Gear | `SYSTEM_GG` |
| **16-bit 68000** | **Mega Drive / Genesis** | **`SYSTEM_MD`** (foco de este documento) |
| 16-bit 68000 | Mega Drive con Power Base Converter | `SYSTEM_PBC` (compatibilidad SMS) |
| 16-bit 68000 | Sega Pico | `SYSTEM_PICO` |
| 16-bit 68000 | **Sega/Mega CD** | `SYSTEM_MCD` (añade un segundo 68000 "sub-CPU") |

Compatibilidad declarada: **100%** con el software publicado de Genesis/Mega
Drive, Sega/Mega CD, Master System, Game Gear y SG-1000, incluyendo dumps no
licenciados y piratas, y los modos de retrocompatibilidad.

### Arquitectura de capas

```
┌──────────────────────────────────────────────────────────┐
│ Frontend libretro (RetroArch / RetroRunner de AYTHER)     │
└───────────────▲───────────────────────┬──────────────────┘
   callbacks env/video/audio/input       │ retro_* entry points
┌───────────────┴───────────────────────▼──────────────────┐
│ libretro/libretro.c  — capa de adaptación libretro        │
│   • opciones de core   • memory access   • disk control    │
│   • input mapping      • savestates      • cheats          │
├───────────────────────────────────────────────────────────┤
│ core/  — núcleo de emulación portable                      │
│   system.c · genesis.c · loadrom.c · state.c · io_ctrl.c   │
│   ├ CPU:   m68k/ (68000, sub-68000)   z80/                 │
│   ├ Video: vdp_ctrl.c · vdp_render.c · ntsc/               │
│   ├ Audio: sound/ (YM2612/3438, SN76489, YM2413, blip)     │
│   ├ Input: input_hw/  (pads, mouse, guns, multitaps…)      │
│   ├ Cart:  cart_hw/   (mappers, SRAM/EEPROM, SVP, lock-on) │
│   └ CD:    cd_hw/     (CDD, CDC, GFX, PCM, BRAM)           │
└───────────────────────────────────────────────────────────┘
```

---

## 2. La interfaz libretro (el "API" del core)

El archivo [libretro/libretro.c](../libretro/libretro.c) (~4100 líneas)
implementa el contrato libretro. Estas son las herramientas que el core expone
al frontend.

### 2.1 Identidad y ciclo de vida

| Función | Propósito |
|---|---|
| `retro_api_version()` | Devuelve `RETRO_API_VERSION`. |
| `retro_get_system_info()` | Nombre (`Genesis Plus GX`), versión (`v1.7.4`), extensiones válidas. |
| `retro_get_system_av_info()` | Geometría de video, FPS y sample rate (ver §2.3). |
| `retro_init()` / `retro_deinit()` | Inicializa/libera el core. |
| `retro_load_game()` / `retro_load_game_special()` | Carga la ROM/CD. |
| `retro_unload_game()` | Descarga el contenido. |
| `retro_reset()` | Reset en caliente del sistema. |
| `retro_run()` | Emula **un frame** (corre CPU/VDP/sonido y entrega video+audio). |
| `retro_get_region()` | `RETRO_REGION_PAL` o `RETRO_REGION_NTSC` según `vdp_pal`. |

**Extensiones de archivo aceptadas** (`retro_get_system_info`,
[libretro.c:3129](../libretro/libretro.c)):

```
m3u | mdx | md | smd | gen | bin | cue | iso | chd | bms | sms | gg | sg | 68k | sgd
```

- **Mega Drive**: `md`, `smd` (interleaved), `gen`, `bin`, `mdx` (mappers
  extendidos), `68k`.
- **Mega CD**: `cue`, `iso`, `chd` (+ `m3u` para multi-disco).
- 8-bit: `sms`, `bms`, `gg`, `sg`, `sgd`.

`need_fullpath = true`: el core lee el archivo por ruta (necesario para
imágenes de CD).

### 2.2 Callbacks de entorno usados

El core negocia capacidades con el frontend vía `RETRO_ENVIRONMENT_*`:

- `SET_PIXEL_FORMAT` → **RGB565** ([libretro.c:3490](../libretro/libretro.c)).
- `SET_SYSTEM_AV_INFO` / `SET_GEOMETRY` → cambia resolución/aspecto en caliente.
- `SET_MEMORY_MAPS` → mapa de RAM para herramientas (cheats/RetroAchievements).
- `SET_CONTROLLER_INFO` / `SET_INPUT_DESCRIPTORS` → tipos de mando y etiquetas.
- `SET_CORE_OPTIONS_V2` / `GET_VARIABLE` → opciones de configuración (§4).
- `SET_DISK_CONTROL_INTERFACE` → cambio de disco en Mega CD (§2.8).
- `SET_AUDIO_BUFFER_STATUS_CALLBACK` → frameskip automático.
- `GET_VFS_INTERFACE`, `GET_LED_INTERFACE`, `GET_MESSAGE_INTERFACE_VERSION`,
  `GET_GAME_INFO_EXT`, `GET_SYSTEM_DIRECTORY`, `GET_SAVE_DIRECTORY`,
  `GET_AUDIO_VIDEO_ENABLE` (fast savestates).

### 2.3 Video

- **Formato de pixel**: RGB565 (16 bpp) hacia el frontend.
- **Geometría** (`retro_get_system_av_info`,
  [libretro.c:3134](../libretro/libretro.c)):
  - Mega Drive: ancho base 256/320 px (H32/H40); `max_width` 320 + bordes (28),
    o ensanchado por el filtro NTSC de Blargg (`MD_NTSC_OUT_WIDTH`).
  - Alto: 240 (NTSC) / 288 (PAL); doble (480/576) en **Interlaced Mode 2**
    (`config.render`).
  - `aspect_ratio` configurable (Auto / NTSC PAR / PAL PAR / 4:3 / Uncorrected).
- **FPS exacto**: `system_clock / lines_per_frame / MCYCLES_PER_LINE`
  (~59.92 Hz NTSC, ~49.70 Hz PAL).
- Filtros opcionales: **Blargg NTSC** (monochrome/composite/svideo/rgb) y
  **LCD ghosting** (Game Gear / Nomad). Ver §5 y §4.

### 2.4 Audio

- Sample rate de salida: `SOUND_FREQUENCY` (48 kHz por defecto).
- Entrega por lotes vía `retro_set_audio_sample_batch` (16-bit, estéreo).
- Mezcla y resampleo internos con **blip buffers** (banda limitada). Detalles
  de chips y filtros en §6.

### 2.5 Input

`retro_set_controller_port_device(port, device)`
([libretro.c:3178](../libretro/libretro.c)) selecciona el periférico por puerto.
Tipos `RETRO_DEVICE_*` reconocidos (subclasificados sobre los base de libretro):

| Device libretro | Periférico emulado |
|---|---|
| `RETRO_DEVICE_MDPAD_3B` / `_6B` | Pad de control de 3 / 6 botones |
| `RETRO_DEVICE_MSPAD_2B` | Pad SMS de 2 botones |
| `RETRO_DEVICE_MDPAD_3B/6B_TEAMPLAYER` | Sega Team Player (4 pads/puerto) |
| `RETRO_DEVICE_MDPAD_3B/6B_WAYPLAY` | EA 4-Way Play (4 pads usando ambos puertos) |
| `RETRO_DEVICE_MSPAD_2B_MASTERTAP` | Master Tap (4 pads de 2 botones) |
| `RETRO_DEVICE_MENACER` | Sega Menacer (puerto B) |
| `RETRO_DEVICE_JUSTIFIERS` | Konami Justifier (hasta 2 pistolas, puerto B) |
| `RETRO_DEVICE_PHASER` | Sega Light Phaser |
| `RETRO_DEVICE_PADDLE` | Sega Paddle Control |
| `RETRO_DEVICE_SPORTSPAD` | Sega Sports Pad |
| `RETRO_DEVICE_XE_1AP` | XE-1AP analógico |
| `RETRO_DEVICE_MOUSE` | Sega Mouse / Mega Mouse |
| `RETRO_DEVICE_GRAPHIC_BOARD` | Sega Graphic Board |

El polling de botones se traduce en `retro_run` desde
`RETRO_DEVICE_ID_JOYPAD_*` a los bitmasks `INPUT_*` del core. Detalle del
hardware de input en §7.

### 2.6 Savestates (serialización)

| Función | Detalle |
|---|---|
| `retro_serialize_size()` | Devuelve `STATE_SIZE` (tamaño fijo del bloque). |
| `retro_serialize()` | Vuelca el estado con `state_save()` ([core/state.c](../core/state.c)). |
| `retro_unserialize()` | Restaura con `state_load()`; reaplica overclock. |

- El savestate empieza con la cadena de versión **`STATE_VERSION`** (`"GENPLUS-GX
  1.7.7"`), validada en [state.c:53](../core/state.c). Cargar un estado de
  versión distinta falla de forma segura.
- **"Fast savestates"**: si el frontend reporta el bit correspondiente vía
  `GET_AUDIO_VIDEO_ENABLE`, se guarda/restaura además el estado de los blip
  buffers de sonido (`save_sound_buffer`/`restore_sound_buffer`) para evitar
  artefactos al rebobinar.
- **Delta AYTHER (`539dc45`)**: el savestate ahora serializa el *latch* de la
  fase TH del pad de 6 botones (`input-hw`), imprescindible para que restaurar
  un estado a mitad de un replay no diverja.

### 2.7 Cheats

| Función | Detalle |
|---|---|
| `retro_cheat_reset()` | Limpia parches y cuenta de cheats. |
| `retro_cheat_set(index, enabled, code)` | Decodifica y aplica un código. |

- Soporta **Game Genie** y **Action Replay / PAR** (`decode_cheat`), tanto
  parches de ROM como cheats de RAM (`maxROMcheats` / `maxRAMcheats`).
- Códigos multilínea separados por `+`. Límite `MAX_CHEATS = 150`
  ([libretro.c:171](../libretro/libretro.c)).

### 2.8 Disk control (multi-disco para Mega CD)

Interfaz `RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE` con hasta
**`MAX_DISKS = 4`** ([libretro.c:2762](../libretro/libretro.c)), alimentada por
playlists `.m3u`. Permite intercambiar discos en juegos de Sega CD que lo
requieren sin recargar el core.

---

## 3. Acceso a memoria — `retro_get_memory_data/size`

Esta es la superficie más relevante para herramientas externas (cheats,
RetroAchievements, **el pipeline HD de AYTHER**). El core expone regiones por
*id* ([libretro.c:3746](../libretro/libretro.c)).

Para integraciones AYTHER nuevas, la superficie recomendada es la ABI
versionada descubierta mediante `ayther_get_interface()`: negocia capabilities,
tamaños y versiones antes de copiar datos, y mantiene estos IDs como adapter
legacy. Contrato completo: [ayther_abi_v1.md](ayther_abi_v1.md).

### 3.1 Ids estándar de libretro

| Id | Región | Tamaño | Notas |
|---|---|---|---|
| `RETRO_MEMORY_SAVE_RAM` | SRAM/EEPROM de la cartridge | hasta 64 KB | `NULL` si `sram.on` es falso; el `size` recortado al rango realmente modificado al guardar. |
| `RETRO_MEMORY_SYSTEM_RAM` | Work RAM | 64 KB (MD) / 8 KB / 2 KB / 1 KB (8-bit) | RAM principal del 68000 en Mega Drive. |
| `RETRO_MEMORY_VIDEO_RAM` | **VRAM del VDP** | **64 KB** | **Delta AYTHER `7fcf9bc`** — upstream devuelve `NULL`. |

### 3.2 Memory map para herramientas

`set_memory_maps()` ([libretro.c:2738](../libretro/libretro.c)) publica
descriptores vía `SET_MEMORY_MAPS`. Actualmente se publica el mapa del
**Mega CD** (Work RAM en `0xFF0000`, PRG-RAM, Word-RAM) para que cheats y
achievements direccionen correctamente la memoria del sub-CPU.

### 3.3 Ids privados del fork AYTHER (≥ `0x100`)

La convención libretro reserva ids `≥ 0x100` para usos no estandarizados. El
fork los usa para exponer el estado interno del VDP al **Lab de AYTHER** (un
core stock devuelve `NULL` y el Lab degrada con elegancia). Algunos son
**escribibles** desde el frontend para manipular el render. Esta superficie es
legacy y se conserva durante la migración de ABI v1.

| Id | Nombre lógico | Tamaño | R/W | Contenido |
|---|---|---|---|---|
| `0x100` | `AYTHER_MEMORY_CRAM` | 128 B | R | **CRAM**: 64 colores de 9 bits (layout `0000BBB0GGG0RRR0`). |
| `0x101` | `AYTHER_MEMORY_VDP_REGS` | 32 B | R | Los 32 registros `reg[0x20]` del VDP (bases de planos, tamaño…). |
| `0x107` | `AYTHER_MEMORY_VSRAM` | 128 B | R | **VSRAM**: 64 entradas de vscroll de 11 bits. |
| `0x102` | `AYTHER_MEMORY_LAYER_MASK` | 1 B | **R/W** | Bitmask de capas visibles (Plano A/B/Window/Sprites). |
| `0x103` | `AYTHER_MEMORY_SPRITE_SUPPRESS` | 16 B | **R/W** | Bitmask de slots del SAT a ocultar (sprite por hash). |
| `0x104` | `AYTHER_MEMORY_TILE_SUPPRESS` | 512 B | **R/W** | Máscara de celdas de tile (rejilla 64×64) a "pelar". |
| `0x105` | `AYTHER_MEMORY_PLANE_TILE_SUPPRESS` | 3072 B | **R/W** | Por-plano: bitmap `(patrón<<2 \| paleta)` a suprimir. |
| `0x106` | `AYTHER_MEMORY_PLANE_SUPPRESS_ACTIVE` | 1 B | **R/W** | Flag de gate del fast-path (¿hay tiles de plano ocultos?). |
| `0x108` | `AYTHER_MEMORY_LAYER_DIM` | 1 B | **R/W** | Atenuado de capas no-sprite. |
| `0x109` | `AYTHER_MEMORY_AUDIO_WRITES` | 65536 B | R | 8192 escrituras de audio de 8 bytes. |
| `0x10A` | `AYTHER_MEMORY_AUDIO_WRITE_COUNT` | 4 B | **R/W legacy** | Cantidad válida; la ABI v1 la reinicia automáticamente por frame. |
| `0x10B` | `AYTHER_MEMORY_PARSED_SPRITES` | 1280 B | R | 128 sprites de layout v1 congelado en 10 bytes. |
| `0x10C` | `AYTHER_MEMORY_PARSED_SPRITE_COUNT` | 1 B | **R/W legacy** | Cantidad válida; la ABI v1 la reinicia automáticamente por frame. |
| `0x10D` | `AYTHER_MEMORY_AUDIO_MUTE` | 2 B | **R/W** | Máscara de mute FM/PSG. |
| `0x10E` | `AYTHER_MEMORY_RASTER_DIRTY` | 4 B | **R/W** | Bitmask transiente por frame con motivos de fallback raster; `> 0` conserva el contrato booleano legacy. |

> **Endianness**: VRAM y CRAM se exponen **word-swapped** en hosts
> little-endian (igual que la Work RAM): el byte lógico `off` vive en el array
> en `off^1`.

Estas regiones habilitan, sin tocar el juego, las capacidades de autoría del
Lab: **aislar capas**, **ocultar sprites/tiles concretos** y **revelar lo que
hay detrás** (Plano B o backdrop). La implementación del *peel/merge* vive en
[core/vdp_render.c](../core/vdp_render.c) (`ayther_peel_merge`,
`ayther_layer_mask`, `ayther_*_suppress`).

---

## 4. Opciones de configuración del core

Definidas en
[libretro/libretro_core_options.h](../libretro/libretro_core_options.h) y leídas
con `GET_VARIABLE`. Todas con prefijo `genesis_plus_gx_`. Resumen por categoría
(énfasis en lo relevante a Mega Drive).

### Sistema

| Clave | Valores | Qué hace |
|---|---|---|
| `system_hw` | auto / sg-1000 / …/ **mega drive / genesis** | Fuerza la consola emulada. |
| `region_detect` | auto / ntsc-u / pal / ntsc-j | Región (50/60 Hz). |
| `vdp_mode` | auto / 60hz / 50hz | Fuerza el modo del VDP independientemente de la región. |
| `bios` | disabled / enabled | Usa el Boot ROM oficial si está presente. |
| `add_on` | auto / sega-mega cd / megasd / none | Add-on de CD en modo MD. |
| `lock_on` | disabled / game genie / action replay (pro) / sonic & knuckles | Cartridge *lock-on* (pass-through). |
| `system_bram` / `cart_bram` / `cart_size` | — | Gestión de BRAM (backup) de Sega CD. |

### Video

| Clave | Valores | Qué hace |
|---|---|---|
| `aspect_ratio` | auto / NTSC PAR / PAL PAR / 4:3 / Uncorrected | Relación de aspecto provista por el core. |
| `overscan` | disabled / top-bottom / left-right / full | Muestra los bordes de overscan. |
| `left_border` | disabled / left / left & right | Recorta bordes (SMS). |
| `gg_extra` | disabled / enabled | Pantalla extendida de Game Gear (256×192). |
| `blargg_ntsc_filter` | disabled / monochrome / composite / svideo / rgb | Filtro NTSC de Blargg. |
| `lcd_filter` | disabled / enabled | Ghosting de panel LCD (GG/Nomad). |
| `render` | single field / double field | Salida de **Interlaced Mode 2** (320×448): desentrelazado suave vs. campo doble fiel al hardware. |
| `frameskip` (+ `frameskip_threshold`) | disabled / auto / manual | Salto de frames para evitar *crackling*. |

### Audio (núcleo)

| Clave | Valores | Qué hace |
|---|---|---|
| `ym2612` | mame (ym2612) / mame (asic ym3438) / mame (enhanced ym3438) / nuked (ym2612) / nuked (ym3438) | **Chip FM del Mega Drive** y método de emulación (MAME rápido vs. **Nuked** cycle-accurate). |
| `ym2413` (+ `ym2413_core`) | auto / disabled / enabled (+ mame/nuked) | FM del Master System (OPLL). |
| `sound_output` | stereo / mono | Salida estéreo o mono. |
| `audio_filter` | disabled / low-pass / EQ | Filtro paso-bajo (sonido "Model 1") o ecualizador. |
| `lowpass_range` | 0–100% | Corte del paso-bajo. |
| `psg_preamp` / `fm_preamp` | 0–200% | Pre-amplificación PSG / FM. |
| `cdda_volume` / `pcm_volume` | — | Volúmenes de audio de CD. |
| `audio_eq_low/mid/high` | — | Ganancias del EQ de 3 bandas. |

### Audio avanzado (por canal)

Activable con `show_advanced_audio_settings`. Volumen independiente por canal:
- `psg_channel_0..3_volume` (3 tonos + ruido del SN76489).
- `md_channel_0..5_volume` (6 canales FM del YM2612).
- `sms_fm_channel_0..8_volume` (9 canales del YM2413).

### Input / temporización / precisión

| Clave | Qué hace |
|---|---|
| `gun_input` / `gun_cursor` | Fuente y cursor de pistola de luz. |
| `invert_mouse` | Invierte eje Y del mouse. |
| `no_sprite_limit` | **Elimina el límite de sprites por línea** del VDP (menos *flicker*). |
| `enhanced_vscroll` (+ `_limit`) | Scroll vertical por celda interpolado (suaviza el vscroll por columna). |
| `overclock` | Overclock del CPU (`HAVE_OVERCLOCK`). |
| `force_dtack` | Evita *lockups* por acceso a buses no mapeados. |
| `addr_error` | Emula address errors del 68000 (precisión). |
| `cd_latency` / `cd_precache` | Latencia/precarga de la unidad de CD. |

---

## 5. CPUs

| CPU | Rol | Implementación |
|---|---|---|
| **Motorola 68000** | CPU principal del Mega Drive | [core/m68k/](../core/m68k/) — intérprete Musashi (`m68kcpu.c`), tablas de ciclos por instrucción. |
| **Sub-68000** | CPU del Sega/Mega CD (~12.5 MHz) | `s68kcpu.c` (segunda instancia del Musashi, config `s68kconf.h`). |
| **Zilog Z80** | Coprocesador de sonido (MD) / CPU principal (SMS/GG/SG) | [core/z80/z80.c](../core/z80/z80.c). |

Capacidades de precisión expuestas como opciones: emulación de **address
errors**, **force DTACK** ante buses no mapeados, y **overclock** opcional.

---

## 6. Subsistema de Video (VDP)

Implementado en [core/vdp_ctrl.c](../core/vdp_ctrl.c) (control, DMA, timing) y
[core/vdp_render.c](../core/vdp_render.c) (rasterizado). Es el corazón gráfico
del Mega Drive y la fuente de todo lo que AYTHER inspecciona.

### 6.1 Qué emula

- **Planos** Scroll A y Scroll B, plano **Window**, capa de **sprites** y color
  de fondo (*backdrop*).
- **Scroll**: horizontal por línea / por celda / por página (`hscroll_mask`);
  vertical global o por columna (VSRAM), con el modo **enhanced vscroll** del
  fork.
- **Sprites**: vía Sprite Attribute Table (SAT) como lista enlazada en Mode 5;
  límite de sprites/línea (`max_sprite_pixels`, 256/320) y opción
  `no_sprite_limit`.
- **Shadow/Highlight**, **interlace** (IM1 doblado / IM2 alta resolución),
  detección de colisión de sprites.
- **DMA**: 68K→VRAM/CRAM/VSRAM, VRAM Fill y VRAM Copy, con FIFO y timing
  ciclo-exacto.
- **Contadores H/V** con latch (para pistolas de luz).

### 6.2 Memorias del VDP

| Array | Tamaño | Contenido |
|---|---|---|
| `vram[]` | 64 KB | Tiles, name tables, SAT, tabla de hscroll. |
| `cram[]` | 128 B | 64 colores de 9 bits (paleta). |
| `vsram[]` | 128 B | 64 entradas de scroll vertical (11 bits). |
| `reg[0x20]` | 32 B | Registros de control del VDP. |
| `sat[0x400]` | 1 KB | Copia interna del SAT. |

Registros notables: `reg[2]` base de Plano A (`ntab`), `reg[3]` Window
(`ntwb`), `reg[4]` Plano B (`ntbb`), `reg[5]` base del SAT (`satb`), `reg[12]`
ancho H40/interlace/shadow-highlight, `reg[13]` tabla de hscroll, `reg[19..23]`
DMA. **Las cuatro memorias y los registros se exponen por los ids `0x100`,
`0x101`, `0x107` y `RETRO_MEMORY_VIDEO_RAM`** (§3.3).

### 6.3 Pipeline de render por scanline

`render_line()` ([vdp_render.c:5037](../core/vdp_render.c)):

1. **Cache de patrones**: tiles "sucios" se decodifican on-demand a
   `bg_pattern_cache` (seguimiento por `bg_name_dirty`/`bg_name_list`).
2. **Fondo**: puntero `render_bg()` despacha al renderer del modo
   (`render_bg_m5`, `render_bg_m5_vs`, `render_bg_m5_vs_enhanced`, variantes
   IM2…).
3. **Composición de planos**: macro `merge` con LUT de prioridad. **El fork
   inyecta `ayther_peel_merge`**: en las celdas marcadas en `0x104` "pela" el
   primer plano (A/Window) y revela el Plano B, o el *backdrop* si la celda es
   Plano B puro.
4. **Sprites**: `parse_satb` (respeta `ayther_sprite_suppress`, id `0x103`) +
   `render_obj_m5`/`_ste`.
5. **Remap final** (`remap_line`): CRAM → LUT de pixel → salida, aplicando el
   filtro **NTSC** (`md_ntsc_blit`/`sms_ntsc_blit`) o el **LCD ghosting** si
   están activos.

### 6.4 Modos de resolución

- **H32** (256 px) / **H40** (320 px) de ancho.
- 224 / 240 líneas activas; 262 (NTSC) / 313 (PAL) líneas por frame.
- **Interlaced Mode 2**: 320×448 (NTSC) / 320×512 (PAL) reales.
- Filtro **Blargg NTSC** en 4 perfiles (monochrome/composite/svideo/rgb).

### 6.5 Herramientas de inspección/manipulación (AYTHER)

| Variable (vdp_render.c) | Id | Efecto |
|---|---|---|
| `ayther_layer_mask` | `0x102` | Oculta Plano A/B/Window/Sprites en el viewport. |
| `ayther_sprite_suppress[16]` | `0x103` | Saltea slots concretos del SAT en `parse_satb`. |
| `ayther_tile_suppress[512]` | `0x104` | "Pela" celdas (rejilla 64×64) revelando lo de atrás. |
| `ayther_plane_tile_suppress[3072]` | `0x105` | Suprime patrones de plano `(patrón\|paleta)`. |
| `ayther_plane_suppress_active` | `0x106` | Gate del fast-path de `render_bg_m5/_vs`. |

Junto con las lecturas de VRAM/CRAM/VSRAM/regs, estas escrituras permiten al Lab
**descomponer la imagen en capas y entidades** sin parchear la ROM — la base del
enfoque "RTX Remix para 2D".

---

## 7. Subsistema de Audio

Mezcla y resampleo en [core/sound/](../core/sound/) y
[core/system.c](../core/system.c), con **blip buffers** de banda limitada.

### Chips emulados

| Chip | Rol | Fuente | Opciones |
|---|---|---|---|
| **YM2612 / YM3438** | FM principal del Mega Drive (6 canales, 4 operadores) | `ym2612.c` (MAME), `ym3438.c` (**Nuked OPN2**) | `ym2612`: discrete / ASIC / enhanced / Nuked. |
| **SN76489 (PSG)** | 3 tonos + ruido (MD y SMS/GG) | `psg.c` | preamp, paneo, volumen por canal, variante discrete/integrated. |
| **YM2413 (OPLL)** | FM del Master System | `ym2413.c` (MAME), `opll.c` (Nuked) | `ym2413` + `ym2413_core`. |
| **RF5C164 (PCM)** | 8 canales PCM del Mega CD | `cd_hw/pcm.c` | `pcm_volume`. |
| **CD-DA** | Audio de pistas de CD | `cd_hw/cdd.c` (Vorbis/CHD/PCM) | `cdda_volume`. |

### Cadena de salida

- Todos los chips se sincronizan al **master clock** (NTSC 53.69 MHz / PAL
  53.20 MHz) y vuelcan deltas a blip buffers, que se resamplean al sample rate
  de salida al cierre de cada frame.
- **Calidad de resampleo** por chip (`hq_fm`/`hq_psg`): banda limitada vs.
  interpolación lineal.
- **Post-proceso**: paso-bajo de un polo (`audio_filter=low-pass`,
  `lowpass_range`), **EQ de 3 bandas** (`audio_eq_*`), y *downmix* mono.
- **Estado de los blip buffers** se preserva en *fast savestates* para rebobinado
  limpio.

---

## 8. Hardware de Input / Periféricos

[core/input_hw/](../core/input_hw/) + [core/io_ctrl.c](../core/io_ctrl.c). El
core abstrae 3 puertos físicos (A, B, expansión) con punteros de función
`data_r`/`data_w`, sobre los que se mapean los periféricos.

### Periféricos Mega Drive

- **Pads** de 3 y 6 botones (`gamepad.c`), con máquina de estados de fase TH (el
  *latch* del pad de 6 botones es lo que el fork serializa en savestates).
- **Sega Mouse / Mega Mouse** (`mouse.c`).
- **Pistolas de luz**: Light Phaser, **Menacer**, **Konami Justifier** (2
  pistolas) (`lightgun.c`).
- **XE-1AP** analógico (`xe_1ap.c`), **Sega Activator** (16 zonas IR,
  `activator.c`).
- **Paddle Control** (`paddle.c`), **Sports Pad** (`sportspad.c`), **Graphic
  Board** (`graphic_board.c`).
- **Sega Pico** (tablet) y **Terebi Oekaki** (`terebi_oekaki.c`).
- **Multitaps**: **Team Player** (4 pads/puerto → hasta 8 jugadores), **EA
  4-Way Play**, **Master Tap**, y **J-Cart** (2 pads extra vía cartridge).

Constantes en `input.h`: `SYSTEM_*` (tipo por puerto), `DEVICE_*` (dispositivo),
`INPUT_*` (bitmasks de botones). Capacidad: `MAX_DEVICES = 8` slots.

---

## 9. Hardware de Cartridge (Mega Drive)

[core/cart_hw/](../core/cart_hw/), con asignación de mapper en
`md_cart_init()` ([md_cart.c:277](../core/cart_hw/md_cart.c)) y una **base de
datos de ~150 juegos** (`rom_database[]`) para hardware especial.

### Tipos de guardado

- **SRAM** (hasta 64 KB) — `sram.c`, autodetectada del header (`RA` en `0x1B0`)
  o por la base de datos cuando el header miente.
- **EEPROM serial**: I²C 24Cxx (`eeprom_i2c.c`), Microwire 93C46
  (`eeprom_93c.c`), SPI 25XX512 (`eeprom_spi.c`).
- **Flash CFI** (M29W320EB, S29GL064N) — `flash_cfi.c`.

### Mappers y hardware especial

- Bank switching: **SSF2 / Super Street Fighter II**, SEGA/Everdrive SSF,
  **MegaSD** (SSF2 mejorado y ROM mapper), Realtec, Radica multi-juego, y
  decenas de bootlegs (Pier Solar T-5740, SF-001/002/004, Flashkit-MD…).
- **SVP** — el DSP SSP1601 de *Virtua Racing* (`svp/`), autodetectado por `SV`
  en el header.
- **Lock-on** (pass-through): **Game Genie** (`ggenie.c`), **Action Replay /
  PAR** (`areplay.c`), **Sonic & Knuckles** (S&K + Upmem para Sonic 2/3).
- **YX5200** (reproductor de audio de homebrews).

### Carga de ROM

`load_rom()` ([loadrom.c:554](../core/loadrom.c)) detecta el sistema por
extensión/contenido, des-interleave de `.smd`, byteswap en hosts little-endian,
detecta región (header país `0x1F0`) y periféricos declarados, y asigna mapper +
tipo de guardado.

---

## 10. Hardware Sega / Mega CD

[core/cd_hw/](../core/cd_hw/). Añade un **segundo 68000** y los subsistemas del
add-on de CD:

| Bloque | Archivo | Función |
|---|---|---|
| Controlador maestro | `scd.c` | Orquesta PRG-RAM (512 KB), Word-RAM (256 KB), registros ASIC, BRAM (8 KB). |
| Drive (CDD) | `cdd.c` | TOC, lectura de sectores, CD-DA; formatos **CUE/BIN, ISO, CHD, OGG Vorbis**. |
| Controlador de datos (CDC) | `cdc.c` | LC8951x: DMA de sector, decodificador, host transfer. |
| Procesador gráfico (GFX) | `gfx.c` | Escalado/rotación (stamps) en modos 1M/2M. |
| PCM | `pcm.c` | RF5C164: 8 canales PCM. |

- **Modelos**: Mega CD estándar, **Wondermega / X'Eye**, **CDX / Multi-Mega**
  (autodetectados por el Boot ROM).
- **BRAM**: configurable per-BIOS/per-game; cartucho de backup con tamaño
  ajustable (128 Kbit–4 Mbit).
- **Multi-disco** vía `.m3u` + disk control interface (§2.8).
- **MegaSD** como add-on alternativo para CDDA en juegos de cartridge.

---

## 11. Build y despliegue (fork AYTHER)

La DLL **no se versiona** (BYOC — *build your own core*). Toolchain requerido:
**llvm-mingw con runtime MSVCRT**:

```sh
scoop install mingw-mstorsjo-llvm-msvcrt   # en el PATH
make -f Makefile.libretro platform=win64 -j8
```

> Un build **UCRT** aborta en `retro_load_game` (`STATUS_STACK_BUFFER_OVERRUN`);
> el **MSVCRT** es bit-idéntico al DLL stock en emulación. La salida se
> despliega en AYTHER como
> `third_party/cores/genesis_plus_gx_libretro_vram.dll`.

#### Sobre el fallo con UCRT (#38)

El nombre del código de estado despista y conviene dejarlo escrito, porque
orientó mal la búsqueda durante bastante tiempo:

**`STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) no es agotamiento de pila.** El
agotamiento de pila es `STATUS_STACK_OVERFLOW` (0xC00000FD). 0xC0000409 es lo
que produce `__fastfail`, y en UCRT `abort()` está implementado justamente sobre
`__fastfail(FAST_FAIL_FATAL_APP_EXIT)`. También lo produce el *invalid parameter
handler* de UCRT, que MSVCRT no tiene: MSVCRT es tolerante donde UCRT termina el
proceso.

Es decir: la hipótesis con más respaldo es que el build UCRT **llama a `abort()`
o al manejador de parámetro inválido**, no que se le acabe la pila. Eso también
explica por qué MSVCRT "funciona": no es que no haya un problema, es que no
reacciona igual ante uno.

Evidencia que apoya descartar la pila, medida con
`clang -Wframe-larger-than` sobre el build win64:

| Función | Frame |
|---|---:|
| `load_rom` | 16.504 B |
| `check_variables` | 4.392 B |
| `ayther_core_recompose_multilayer` | 2.840 B |
| `ayther_recompose_frame` | 1.480 B |
| `vdp_reg_w` | 1.112 B |

El mayor es `load_rom` con ~16 KB, tres órdenes de magnitud por debajo del 1 MB
de pila por defecto en Windows, y no hay recursión en esa ruta.

Lo que falta para cerrarlo es un toolchain llvm-mingw **UCRT** instalado para
reproducirlo y capturar el `__fastfail` en el depurador; la CI lo previene
prohibiendo imports de `ucrtbase.dll`, que es una contención, no un
diagnóstico.

Otros targets: `Makefile.gc`/`Makefile.gc.low-mem` (GameCube), `Makefile.wii`
(Wii), y los proyectos en `libretro/` para Android (`jni/`), MSVC (`msvc/`,
`libretro_msvc/`) y UWP (`uwp/`).

### Rebase con upstream

Al rebasear sobre `ekeeke/Genesis-Plus-GX`, verificar que upstream **no** haya
implementado `RETRO_MEMORY_VIDEO_RAM` (colisionaría con el delta `7fcf9bc`).

---

## 12. Mapa de archivos clave

| Ruta | Responsabilidad |
|---|---|
| [libretro/libretro.c](../libretro/libretro.c) | Capa libretro: entry points, memoria, input, savestates, cheats, disk control. |
| [libretro/libretro_core_options.h](../libretro/libretro_core_options.h) | Definición de todas las opciones `genesis_plus_gx_*`. |
| [core/system.c](../core/system.c) · [core/genesis.c](../core/genesis.c) | Bucle de sistema, wiring de CPU/VDP/sonido. |
| [core/loadrom.c](../core/loadrom.c) | Carga de ROM, detección de sistema/región/mapper. |
| [core/state.c](../core/state.c) | Serialización de savestates (`STATE_VERSION`). |
| [core/vdp_ctrl.c](../core/vdp_ctrl.c) · [core/vdp_render.c](../core/vdp_render.c) | VDP: control/DMA y rasterizado (+ deltas AYTHER). |
| [core/io_ctrl.c](../core/io_ctrl.c) · [core/input_hw/](../core/input_hw/) | Puertos de I/O y periféricos. |
| [core/sound/](../core/sound/) | Chips de sonido y mezcla. |
| [core/cart_hw/](../core/cart_hw/) · [core/cd_hw/](../core/cd_hw/) | Hardware de cartridge y Sega/Mega CD. |
| [core/m68k/](../core/m68k/) · [core/z80/](../core/z80/) | CPUs. |


## Qué control soporta cada renderer (#28)

Los gates de capa viven **dentro** de cada renderer de fondo Mode 5, y hay
cinco. Cuando se agregaron, tres se quedaron sin ellos, y el modo de fallar era
el peor posible: escribir la máscara no hacía nada — ni efecto, ni error, ni
motivo de fallback. El frontend creía haber ocultado un plano.

| Renderer | Cuándo corre | Máscara de capas (`0x102`) | Supresión de tiles de plano (`0x105`) | Peel por celda (`0x104`) |
|---|---|:--:|:--:|:--:|
| `render_bg_m5` | Mode 5, vscroll global | sí | sí | sí |
| `render_bg_m5_vs` | Mode 5, vscroll por columna | sí | sí | sí |
| `render_bg_m5_vs_enhanced` | opción *enhanced vscroll* | sí | **no** → `UNSUPPORTED_CONTROLS` | sí |
| `render_bg_m5_im2` | interlace mode 2 | sí | sí | sí |
| `render_bg_m5_im2_vs` | interlace mode 2 + vscroll | sí | sí | sí |
| `render_bg_m4` | Mode 4 (SMS / GG / PBC) | sí, con el bit de "Plano A" reinterpretado como *el* fondo | sí | sí, desde el hook de línea (#40) |
| `render_bg_m0/m1/m1x/m2/m3/m3x/inv` | modos TMS (SG-1000, ColecoVision) | **no** para el fondo; el bit de sprites sí, que se aplica en `render_line` | **no** | sí por construcción, sin medir |

En Mode 4 el peel no puede vivir donde vive en Mode 5: hay **un** plano de
fondo, `render_bg_m4` no llama a `merge()` y el hook del merge nunca corre. Con
un solo plano, "pelar la capa de adelante para ver qué hay detrás" sólo puede
significar revelar el backdrop, que es la misma rama que ya toma
`ayther_peel_merge` cuando la celda no tiene primer plano opaco. Va en el hook
de línea, entre `render_bg` y los sprites — la ventana exacta que dura el peel,
por eso un sprite sobre una celda oculta se sigue viendo, igual que en Mode 5.

Los renderers TMS caen del mismo lado del gate (`reg[1] & 0x04` en cero y
`system_hw` que no es Mega Drive), y el peel opera sobre `linebuf`, donde `0x40`
también es el backdrop. Por construcción les aplica. **No está medido**: la
suite no tiene escena TMS, y este fork distingue entre lo que se comprobó y lo
que se dedujo leyendo el código — la fila dice cuál es cuál.

Lo que los modos TMS **no** heredan son los otros dos gates: ocultar el fondo o
filtrar por patrón+paleta ahí se acepta y no hace nada. Es la misma clase de
defecto que #28 cerró para Mode 5 y #40 para Mode 4, todavía abierta para unos
modos que no tienen con qué probarse; queda dicho acá en vez de descubrirse en
uso.

`render_bg_m5_vs_enhanced` dibuja cada media columna con su propio `v_line` —
para eso existe— y no pasa por el camino de columna donde vive el filtro de
patrón+paleta. En vez de aplicarlo a medias, el core enciende
`AYTHER_RASTER_REASON_UNSUPPORTED_CONTROLS` (bit 8) en `0x10E`: un control que
se ignora en silencio es peor que uno que declara que no puede.

**ALT_RENDERER es incompatible con `AYTHER_EXTENSIONS`** y da error de
compilación. Trae su propio juego de renderers Mode 5 sin ninguno de los gates,
y solo lo activan GameCube, Wii, GCW0, Vita y PSP2 — plataformas que este fork
no compila. Un `#error` es mejor que un fallback en tiempo de ejecución: la
alternativa era que alguien portara el fork a una de esas plataformas y
descubriera que todas las máscaras son no-op.

El peel por celda usa coordenadas del frame **emitido**. Con salida entrelazada
(`interlaced && config.render`) `remap_line` duplica la fila de salida, y ese
ajuste ahora se replica al calcular la fila de celda; sin él la máscara caía en
la mitad de la fila marcada, es decir ocultaba la celda equivocada. En interlace
mode 2 *sin* `config.render` la salida no se dobla y la fila ya era correcta.

Verificación: `tests/ci/check_render_gates.sh` (en el job *Source quality*)
exige que los cinco renderers consulten `hide_a/hide_b/hide_w` y limpien ambos
buffers de línea. Es una guarda estructural, no de píxeles: los asserts
pixel-perfect por modo necesitan escenas de interlace y window en el ROM
sintético (#35).
