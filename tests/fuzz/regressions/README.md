# Casos de regresión del fuzzing (#34)

Cada archivo acá es una entrada que **rompió** algo alguna vez. `make -C tests
check-fuzz` los vuelve a pasar por el target correspondiente en cada PR, sin
fuzzer y de forma determinística, para que el bug no pueda volver en silencio.

## Qué atrapa este gate y qué no

El replay falla cuando el proceso **crashea**: un abort, un segfault, un error
de ASan. Eso cubre la mayoría de los hallazgos.

Por sí solo **no** cubre **UB que solo se reporta**: UBSan por defecto imprime
`runtime error` y sigue, así que el replay termina en cero y el caso figura
como `ok`. Por eso ni el job de PR ni el nocturno miran solo el código de
salida: guardan el log y le aplican
[`filter_known_ub.sh`](../../ci/filter_known_ub.sh) contra
[`known_ub.txt`](../../ci/known_ub.txt), que es donde vive la decisión de qué
UB se acepta — y que está **vacía** desde #101, así que hoy el veredicto es
"sin UB" a secas.

- El job `fuzz-regressions` de
  [`ayther-ci.yml`](../../../.github/workflows/ayther-ci.yml) corre en cada
  PR: compila el core con ASan+UBSan, reproduce el corpus y `regressions/` y
  el fixture Z80/VDP, y filtra **los dos logs** (#107). Hasta #107 solo el
  crash ponía ese job en rojo; un UB recuperable quedaba en el log y nadie lo
  miraba.
- El nocturno ([`ayther-fuzz.yml`](../../../.github/workflows/ayther-fuzz.yml))
  hace lo mismo con el log de cada target mientras **busca** entradas nuevas.

Hay una segunda razón para que `make -C tests check-fuzz` a secas no alcance:
corre contra el core que le pasen por `CORE=`, que normalmente **no** está
instrumentado. El UB de un hallazgo suele estar dentro del core, no en el
driver, así que ahí no hay chequeo que dispare. La lupa la pone el core
instrumentado de la sección siguiente, y el veredicto,
`bash tests/ci/filter_known_ub.sh <log>` sobre lo que ese core imprimió.

## Reproducir un caso con la lupa

```sh
# core instrumentado
make -f Makefile.libretro platform=unix AYTHER_EXTENSIONS=1 SOUND_PROBE=1 \
  CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g"

make -C tests/fuzz .build/replay_recompose
tests/fuzz/.build/replay_recompose \
  "$(pwd)/genesis_plus_gx_libretro.so" tests/fuzz/regressions/recompose
```

## Escenas (#75)

El target `unserialize` corre en cuatro escenas, y la escena decide **qué
hardware** hay detrás del blob:

| escena | consola | lo que entra crudo del blob |
|---|---|---|
| `md` (la de siempre) | Mega Drive | `YM2612LoadContext` (MAME) |
| `md-nuked` | Mega Drive | `config.ym3438` → struct de Nuked OPN2 |
| `sms-fm` | Master System | `config.opll` → struct de Nuked YM2413 |
| `scd` (#97) | Sega CD | el bloque `SCD!` entero: dos 68000, PRG-RAM, Word-RAM, CDC, CDD, PCM, ASIC gráfico |

Hasta #75 solo existía la primera, así que las otras dos ramas —structs enteros
que también entran crudos del blob— **nunca se ejercitaban**. Ahí es donde
apuntan los dos issues abiertos de upstream sobre cargar un savestate de Master
System con Nuked (libretro/Genesis-Plus-GX#403 y #290).

La cuarta (#97) es un Sega CD entero con **BIOS e imagen sintéticas**
([`../../ayther/cd_fixture.h`](../../ayther/cd_fixture.h)): el core las lee del
disco, así que el replay las escribe en `BUILD_DIR` (`--workdir`) antes de
cargar. Es el sistema con más bloques propios en el savestate, y hasta esa
escena ninguna mutación tocaba uno.

El fuzzer corre una escena por proceso (`SCENE=`, un job por escena en el
nocturno): cambiarla implica `load_game`, y hacerlo por entrada convertiría al
fuzzer en un medidor de `load_game`. El replay las corre **todas** sobre los
mismos archivos, que sale barato y cubre de más: un caso es una lista de
mutaciones, y vale en cualquier escena.

```sh
# una escena puntual, a mano
tests/fuzz/.build/replay_unserialize --scene sms-fm \
  "$(pwd)/genesis_plus_gx_libretro.so" tests/fuzz/regressions/unserialize
```

## Los casos

- **`recompose/cram-fuera-de-rango-en-pixel-lut`** — escribe bytes crudos en
  CRAM, que es una región expuesta al frontend (id legacy `0x100`), y recompone.
  `color_update_m5` indexaba `pixel_lut[3][0x200]` con la word tal cual: en el
  camino de escritura el empaquetado del bus la deja en 9 bits, pero
  reconstruir la paleta al recomponer —o al cargar un savestate— lee `cram[]` y
  la pasa sin empaquetar. Con el core instrumentado y sin la máscara, este
  archivo produce `index 43839 out of bounds for type 'unsigned short[512]'`.

- **`generated_rom/pila-sobre-el-puerto-del-svp-sin-svp`** y
  **`generated_rom/lectura-del-puerto-del-svp-sin-svp`** (#128) — los primeros
  del target, y un bug de upstream que no necesita un cartucho corrupto: alcanza
  con uno **que no sea Virtua Racing**. `$A15000-$A15005` son los registros del
  SVP, y `ctrl_io_read_byte`, `ctrl_io_read_word` y `ctrl_io_write_word`
  desreferenciaban `svp` sin mirar; `md_cart.c` lo deja en `NULL` en todo
  cartucho sin SVP —y el resto del core sí pregunta `if (svp)`—. El arreglo le
  da a esas direcciones el trato de las demás no usadas del bloque.

  Están **fabricados a mano**, porque el archivo del nocturno
  (`crash-b4bc49e6…`) *no reproduce solo*: medido, pasa limpio con y sin el
  arreglo. Sus mutaciones mueven el SSP inicial de `$00FFFF00` a `$00A6FF00` y
  tocan cuatro bytes más del ROM, y por sí solas no llevan la pila hasta
  `$A15000`, que es donde el CI vio al 68000 empujar un stack frame de address
  error. Un archivo que pasa con y sin el arreglo no es una regresión: misma
  conclusión que en #63 y #105. La versión
  determinística del hallazgo es el primer archivo: cuatro mutaciones que ponen
  el SSP del vector de reset en `$00A15006`, con lo cual la primera excepción
  —el v-int del ROM sintético— empuja PC y SR sobre el puerto. Con el core
  instrumentado y sin el arreglo produce `member access within null pointer of
  type 'struct svp_t'` y un `SEGV on unknown address 0x00000004042e` en
  `ctrl_io_write_word`: mismo sitio y misma dirección que reportó el CI.

  El segundo cubre los dos sitios de lectura, que el nocturno no tocó pero
  tienen el mismo defecto: apunta el PC de reset a `$F000` y escribe ahí
  `tst.w $A15000`, `tst.b $A15005`, `tst.b $A15001`, `bra.s *`. Sin el arreglo
  muere en la primera (`SEGV` en `ctrl_io_read_word`), así que la prueba
  negativa medida es la de la lectura de word; de la de byte queda medida la
  positiva —con el arreglo las tres corren limpias—.

- **`generated_rom/shifts-por-registro-con-cuenta-de-32-o-mas`** (#133) — UB del
  intérprete de upstream que tampoco necesita un cartucho corrupto: alcanza con
  `asl d0,d1` y `d0 >= 32`. Los doce shifts por registro de `m68kops.h`
  (`asr`/`asl`/`lsr`/`lsl` × byte/word/long) hacen `shift = DX & 0x3f` —hasta
  63— y calculan `res = src >> shift` **antes** de mirar la cuenta; el resultado
  se descarta cuando es grande, pero la expresión ya es UB. El nocturno los
  reportó dos noches sin dejar archivo (UBSan imprime y sigue: es el hueco de
  #134), así que el fixture está **fabricado a mano**: apunta el PC de reset a
  `$F000` y ejecuta ahí las 24 operaciones por registro (las cuatro familias
  de shift y las dos de rotación, en los dos sentidos y los tres anchos) con
  cuentas 0, 31, 32, 33 y 63 sobre `d1 = $FFFFFFFF`, y termina en `bra.s *`.

  Recorrer las 24 y no solo las 12 que alcanzó el fuzzer encontró dos sitios
  más: `roxr_32_r` y `roxl_32_r` —la rama sin `M68K_USE_64_BIT`, que es la que
  compilamos— usan `32 - shift` y `shift - 1` como cuenta antes de preguntar
  si `shift` es cero (`shift exponent 32` y `shift exponent 4294967295`). Las
  rotaciones de 8 y 16 bits y `ror`/`rol` de 32 salen limpias: upstream ya las
  enmascara.

  Sin el arreglo, con el core instrumentado: 18 reportes en 14 sitios. Con el
  arreglo, cero. No crashea en ningún caso: lo ve `filter_known_ub.sh` (#107).
  El arreglo es `& 31` sobre la cuenta en esos 14 sitios, que no cambia nada
  donde el resultado se usa (`shift < 32`).

- **`unserialize/pms-corrupto-indexa-lfo-pm-table`** — muta bytes de un
  savestate válido y lo carga. `YM2612LoadContext` copia el blob crudo sobre el
  struct, y `pms`, `ams`, `lfo_cnt` y `LFO_PM` indexan tablas del proceso sin
  que nadie los acote: en el camino normal los acota quien los escribe
  (`OPNWriteReg` para `0xb4-0xb6`, el paso del LFO), y un savestate corrupto no
  pasa por ahí. Con el core instrumentado y sin el saneado en la carga, este
  archivo produce `index 3388997632 out of bounds for type 'INT32[32768]'` y un
  SEGV a continuación.

- **`unserialize/algo-corrupto-deja-los-connect-en-null-79`** y **`-80`** —
  mismo camino, otro campo del mismo blob: `ALGO`. Indexa `op_mask[8][4]` y
  elige el `case` de `setup_connection`, y quien lo escribe lo acota con
  `v&7` (`OPNWriteReg`, `0xb0-0xb2`). Con un `ALGO` fuera de `0..7` el switch
  no toma ningún case y los cinco punteros de conexión del canal quedan como
  vinieron del blob —`NULL`, que es lo que escribe `YM2612SaveContext`—, y
  `chan_calc` los usa igual. Con el core instrumentado y sin el saneado en la
  carga, los dos archivos producen `index 236 out of bounds for type
  'UINT32 [8][4]'`, `store to null pointer` y un SEGV en `chan_calc`. Son dos
  archivos porque son dos hallazgos del job nocturno (#79 y #80) con la misma
  causa: distintas listas de mutación que terminan en el mismo campo.

- **`unserialize/ksr-corrupto-indexa-eg-rate-shift-83`** a **`-88`** — la
  misma familia, del lado de la envolvente. `ksr` (`kcode >> KSR`, 0..31)
  entra crudo del blob y se suma a la tasa para indexar `eg_rate_shift` y
  `eg_rate_select[128]` en la primera escritura de `0x50-0x8f` (`set_dr`,
  `set_sl_rr`). El `ksr` podrido no lo limpia el reset —`reset_channels` no
  lo toca—, así que explota recién en la carga siguiente: el `system_reset`
  de `state_load` llama a `YM2612ResetChip`, que reescribe `0x30-0xb2` con el
  `ksr` que quedó. Con el core instrumentado y sin el saneado en la carga, los
  seis producen `index 148..289 out of bounds for type 'UINT8 [128]'`, y cuatro
  de ellos un `global-buffer-overflow` de ASan en `set_sl_rr`. Son seis
  archivos porque son seis hallazgos del job nocturno (#83 a #88) con la
  misma causa: cada uno pudre el `ksr` de un slot distinto.

- **`unserialize/eg-sel-corrupto-indexa-eg-inc`** — fabricado a mano, porque
  los jobs del 2026-09-07 y del 2026-09-11 reportaron sin dejar archivo: UBSan
  imprime y sigue, y el fuzzer no guarda un caso que no crashea. Son dos
  mutaciones sobre `CH[0].SLOT[0]`, que en el estado base del ROM sintético
  está en ataque: `eg_sh_ar = 0` (para que entre en cada paso de la
  envolvente) y `eg_sel_ar = 205`, el índice que reportó el CI. Los offsets
  (140716 y 140717) son 16 bytes de versión + RAM/IO + bloque VDP +
  `config.ym3438` + `offsetof` dentro de `YM2612`; se verificaron sobre un
  estado volcado, donde a esa altura aparece la firma de un chip reseteado.
  Con el core instrumentado y sin el saneado, produce `index 210 out of bounds
  for type 'UINT8 [152]'` en `advance_eg_channels`. El saneado ahora acota
  las entradas de la envolvente (`ar`/`d1r`/`d2r`/`rr`, `ksr`, `KSR`,
  `kcode`, `FB`) y recomputa los pares `eg_sh_*`/`eg_sel_*` con la fórmula
  de quien los escribe.

- **`unserialize/pc-corrupto-indexa-z80-readmap`** (#82) — el mismo patrón, pero
  en el Z80. `state.c` copia el blob crudo sobre `Z80_Regs`, y los `PAIR` llevan
  un invariante que declara el propio tipo en [`z80/osd_cpu.h`](../../../core/z80/osd_cpu.h):
  *“the upper bytes h2 and h3 normally contain zero (16 bit CPU cores) thus
  PAIR.d can be used to pass arguments to the memory system”*. El núcleo lo
  cumple porque **nada** escribe `.d` entero —todas las escrituras van por
  `.w.l` o `.b.*`—, y un savestate corrupto no pasa por ahí. Este archivo deja
  `pc.d = 0x00510039`, con lo cual `cpu_readop` indexa `z80_readmap[64]` en
  5184.

  Como el YM2612 de #83-#88, no explota donde se carga: `z80_reset` escribe
  `PC = 0x0000`, que es `.w.l`, así que la mitad alta sobrevive al reset. Y
  `EXX`/`EX AF,AF'` copian `PAIR` enteros, con lo cual una mitad alta podrida
  en un registro sombra migra al principal. Con el core instrumentado y sin el
  saneado produce `index 5184 out of bounds for type 'unsigned char *[64]'` y
  un `SEGV on unknown address 0x39` a continuación, en `ROP`.

- **`unserialize/zbank-corrupto-indexa-zbank-memory-map`** (#105) — el mismo
  patrón, en la ventana de banco del Z80. `state.c` carga `zbank` crudo del blob
  y `z80_memory_r`/`z80_memory_w` lo usan como **índice**: arman
  `address = zbank | (address & 0x7FFF)` y entran a
  `zbank_memory_map[address >> 16]` y a `m68k.memory_map[address >> 16]`, los dos
  de 256 entradas. El invariante lo declara su único escritor, `gen_zbank_w()`,
  que enmascara con `& 0xFF8000`; un savestate no pasa por ahí.

  Está **fabricado a mano**, porque el archivo que dejó el nocturno
  (`crash-571106fe…`) *no reproduce solo*. Sus dos mutaciones son
  `(72088, 0xc7)` y `(32736, 0x40)`: la primera cae en `zram` y la segunda en
  `work_ram`, y ninguna de las dos toca `zbank`. El `zbank` podrido venía
  **arrastrado de una entrada anterior**: el target cierra cada entrada
  “restaurando un estado sano” con `serialize` + `unserialize`, y eso vuelve a
  serializar el estado *actual*, que a esa altura ya estaba podrido. Mismo modo
  de falla que el `write_control` de #63, y la misma conclusión: un archivo que
  pasa con y sin el arreglo no es una regresión.

  Las seis mutaciones de este archivo son la versión determinística del
  hallazgo: `zram[0..2]` = `LD ($8000),A`, para que el Z80 toque la ventana de
  banco; `zstate = 1`, que es la única condición con la que `system_frame_gen`
  corre el Z80; y `zbank = 0x14550000`. Con el core instrumentado y sin la
  máscara produce `index 5205 out of bounds for type 't_zbank_memory_map [256]'`
  —el mismo índice que reportó el CI— y un `SEGV` a continuación, al **llamar**
  el puntero de función que salió de ahí: no un puntero podrido, uno elegido por
  los bytes del archivo.

- **`unserialize/opll-cycles-corrupto-indexa-opll-accm`** y
  **`unserialize/ym3438-cycles-corrupto-indexa-ym3438-accm`** (#75) — el mismo
  patrón que el YM2612, en los dos cores de Nuked. `sound_context_load` carga
  `opll_cycles` y `ym3438_cycles` crudos del blob, y los dos son **índices**:
  `OPLL2413_Update` hace `opll_accm[opll_cycles]` y `YM3438_Update` hace
  `ym3438_accm[ym3438_cycles]`, en ambos casos **antes** del `% 18` / `% 24`
  que los acota. Lo mismo vale para el `chip->cycles` de adentro de cada
  struct, que indexa `ch_offset[18]`, `pg_phase[18]`, `eg_state[18]` y
  `eg_level[18]`.

  Están fabricados a mano, con los offsets medidos sobre un estado volcado
  (75651 para `opll_cycles`, 142008 para `ym3438_cycles`): una sola mutación
  que ensucia el byte más alto del `int`. Sin el saneado producen
  `index 2130706444 out of bounds for type 'int [18][2]'` y un `SEGV` en
  `OPLL_Clock` / `OPN2_Clock`.

  **Cada uno solo reproduce en su escena.** Bajo `md` los dos pasan en silencio,
  porque esa escena no entra por ninguna de las dos ramas: eso *es* el punto
  ciego que #75 vino a tapar, y es por qué el replay corre las tres.

- **`unserialize/connect-corrupto-indexa-fm-algorithm-129`** y los seis que lo
  acompañan (#129) — la misma familia, **adentro** del struct de Nuked OPN2.
  #75 acotó los dos contadores de slot y dejó el resto del `ym3438_t` entrando
  crudo: `sound_context_load` lo copia entero del blob, y `OPN2_Clock` usa una
  docena de sus campos como índice de tabla o como exponente de un shift. En
  el camino normal los acota su único escritor (`OPN2_DoRegWrite` enmascara
  cada registro; `OPN2_Clock` deriva `channel = cycles % 6`), y un savestate no
  pasa por ahí.

  El primero es el archivo del nocturno tal cual, y **reproduce solo** (desde
  #108 el target restaura el estado inicial antes de cada entrada): una sola
  mutación, `connect[2] = 50`, contra `fm_algorithm[4][6][8]`. Los otros seis
  están fabricados a mano con los offsets medidos (`ym3438_t` arranca en 140652
  en la escena `md-nuked`; `offsetof` de cada campo sumado a eso), porque el
  nocturno reportó `ks` y `pg_block` sin dejar archivo —UBSan imprime y sigue—
  y los demás salieron de recorrer el struct con la misma pregunta. Sin el
  saneado, medido con el core instrumentado:

  | archivo | campo | lo que produce |
  |---|---|---|
  | `connect-…-129` | `connect[2] = 50` | `index 50 out of bounds for type 'Bit32u [8]'` + `global-buffer-overflow` en `OPN2_FMPrepare` |
  | `channel-…` | `channel = 0x7f000000` | `index 2130706432 out of bounds for type 'Bit16s [6]'` + `SEGV` en `OPN2_ChGenerate` |
  | `lfo-freq-…` | `lfo_freq = 255` | `index 255 out of bounds for type 'Bit32u [8]'` + `global-buffer-overflow` en `OPN2_UpdateLFO` |
  | `pms-nuked-…` | `pms[0..5] = 255` | `index 255 out of bounds for type 'Bit32u [8][8]'` y shifts de 17400 bits |
  | `ks-…` | `ks[0] = 255` | `shift exponent 252 is too large` (el del CI) |
  | `pg-block-…` | `pg_block = 34` | `shift exponent 34 is too large` (el del CI) |
  | `fb-…` | `fb[0..5] = 255` | `shift exponent -245 is negative` |

  Los cuatro últimos **no crashean**: terminan en cero y solo los ve
  `filter_known_ub.sh` sobre el log (#107). Con el saneado, los siete dan cero
  reportes. Como los de #75, solo reproducen en la escena `md-nuked`.

- **`unserialize/fnum-corrupto-indexa-eg-ksltable-132`** y los diez que lo
  acompañan (#132) — lo mismo que #129, del lado del otro Nuked: el `opll_t`
  del YM2413. #75 acotó `cycles` y conservó `patchrom`/`chip_type`, y el resto
  del struct seguía entrando crudo. En el camino normal lo acotan
  `OPLL_DoRegWrite` y `OPLL_DoModeWrite` (cada registro con su máscara) y el
  propio `OPLL_Clock` (los derivados); un savestate no pasa por ahí.

  El primero es el archivo del nocturno tal cual y reproduce solo: de sus siete
  mutaciones la que importa es el byte alto de `fnum[6]` (`0x76`), que deja
  `c_ksl_freq = fnum >> 5 = 176` contra `eg_ksltable[16]`. Los otros diez están
  fabricados con los offsets medidos (`opll_t`, 392 bytes, arranca en 75111 en
  la escena `sms-fm`: `opll_cycles` está en 75651, menos `opll_sample` y
  `opll_accm[18][2]`; `offsetof` de cada campo sumado a eso) recorriendo
  `opll.c` con la pregunta de siempre: ¿qué campo es índice o cantidad de un
  shift? Sin el saneado, medido con el core instrumentado (gcc, WSL):

  | archivo | campo | lo que produce |
  |---|---|---|
  | `fnum-…-132` | `fnum[6] = 0x76xx` | `index 176 out of bounds for type 'uint32_t [16]'` en `OPLL_EnvelopeKSLTL` (con clang, además `global-buffer-overflow`: el del CI) |
  | `block-opll-…` | `block[0..8] = 255` | `shift exponent 255 is too large` |
  | `inst-…` | `inst[0..8] = 255` | `patchrom[opll_patch_1 + 254]`: lee fuera de la tabla y el `ksl` que saca de ahí da `shift exponent -7 is negative` |
  | `c-multi-…` | `c_multi = 255` | `index 255 out of bounds for type 'uint32_t [16]'` (`pg_multi`) |
  | `c-block-…` | `c_block = 255` | `shift exponent 255 is too large` |
  | `c-fb-…` | `cycles = 0`, `c_fb = 255` | `shift exponent -248 is negative` |
  | `eg-timer-low-lock-…` | `eg_timer_low_lock = 255` | `index 255 out of bounds for type 'uint32_t [4]'` (`eg_stephi`) |
  | `eg-rate-hi-…` | `eg_state[] = attack`, `eg_kon = 2`, `eg_rate_hi = 255` | `shift exponent -239 is negative` |
  | `op-exp-s-…` | `op_exp_s = 0xffff` | `shift exponent 65535 is too large` |
  | `patch-multi-…` | `inst[] = 0`, `patch.multi[] = 255` | `index 255 out of bounds` (`pg_multi`, por el patch de usuario) |
  | `patch-ksl-…` | `inst[] = 0`, `patch.ksl[] = 255` | `shift exponent -252 is negative` |

  Los `c_*` y los `eg_*` son valores **latcheados**: `OPLL_PreparePatch2` y
  `OPLL_EnvelopeGenerate` los recalculan al final de cada ciclo, así que el
  valor podrido solo vive un `OPLL_Clock`, el primero después de cargar. Por
  eso dos de los archivos acomodan además el contexto (`cycles`, `eg_state`)
  para que ese primer ciclo pase por la rama que los usa.

  Con gcc ninguno crashea: terminan en cero y los ve `filter_known_ub.sh`
  sobre el log (#107). Con el saneado, los once dan cero reportes. Solo
  reproducen en la escena `sms-fm`, que es la única que entra por la rama del
  OPLL de Nuked.

- **`write_control` (#63) — sin archivo, a propósito.** El caso que dejó el
  fuzzer (`crash-269aa8d4…`) no reproduce solo: el Z80 arrastra estado entre
  entradas —corre un frame por entrada y nunca se resetea—, y el desborde
  dependía de esa historia y no de la entrada. Un archivo que pasa con y sin el
  arreglo no es una regresión, es ruido con nombre. La versión determinista es
  [`../../ayther/z80_vdp_fifo.c`](../../ayther/z80_vdp_fifo.c) sobre el fixture
  `ayther_build_generated_rom_z80_vdp`: un Z80 que martilla el puerto de datos
  del VDP por la ventana de banco, que es exactamente el camino del hallazgo.
  Corre en cada PR sin instrumentar (`make -C tests check-z80-vdp-fifo`) y de
  noche con ASan (`ayther-fuzz.yml`, job `regressions`), que es donde ve la
  lectura fuera de `fifo_timing_h40`.

## Con la lupa, de noche

Desde #63 el job nocturno tiene un segundo job, `regressions`, que reproduce
este directorio y el fixture del Z80 contra el core con ASan+UBSan. Es lo que
convierte la salvedad de arriba —"el replay no ve UB dentro del core"— en algo
que sí se mira, aunque sea una vez por día y no en cada PR.
