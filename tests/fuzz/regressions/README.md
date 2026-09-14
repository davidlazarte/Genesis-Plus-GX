# Casos de regresión del fuzzing (#34)

Cada archivo acá es una entrada que **rompió** algo alguna vez. `make -C tests
check-fuzz` los vuelve a pasar por el target correspondiente en cada PR, sin
fuzzer y de forma determinística, para que el bug no pueda volver en silencio.

## Qué atrapa este gate y qué no

El replay falla cuando el proceso **crashea**: un abort, un segfault, un error
de ASan. Eso cubre la mayoría de los hallazgos.

No cubre **UB que solo se reporta**. UBSan por defecto imprime `runtime error`
y sigue, así que el replay termina en cero y el caso figura como `ok`. Y aunque
se le pusiera `halt_on_error=1`, el gate se pondría rojo en el primer UB
*aceptado* de [`../../ci/known_ub.txt`](../../ci/known_ub.txt) — los stores
desalineados del renderer upstream —, que es un motivo equivocado para frenar
un merge.

Hay una segunda razón, más de fondo: `check-fuzz` corre contra el core que le
pasen por `CORE=`, que normalmente **no** está instrumentado. El UB de un
hallazgo suele estar dentro del core, no en el driver, así que ahí no hay
chequeo que dispare.

Quien sí los ejerce con la lupa puesta es el job nocturno
([`ayther-fuzz.yml`](../../../.github/workflows/ayther-fuzz.yml)): compila el
core con ASan+UBSan y filtra los reportes por `known_ub.txt`, que es donde vive
la decisión de qué UB se acepta.

## Reproducir un caso con la lupa

```sh
# core instrumentado
make -f Makefile.libretro platform=unix AYTHER_EXTENSIONS=1 SOUND_PROBE=1 \
  CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g"

make -C tests/fuzz .build/replay_recompose
tests/fuzz/.build/replay_recompose \
  "$(pwd)/genesis_plus_gx_libretro.so" tests/fuzz/regressions/recompose
```

## Los casos

- **`recompose/cram-fuera-de-rango-en-pixel-lut`** — escribe bytes crudos en
  CRAM, que es una región expuesta al frontend (id legacy `0x100`), y recompone.
  `color_update_m5` indexaba `pixel_lut[3][0x200]` con la word tal cual: en el
  camino de escritura el empaquetado del bus la deja en 9 bits, pero
  reconstruir la paleta al recomponer —o al cargar un savestate— lee `cram[]` y
  la pasa sin empaquetar. Con el core instrumentado y sin la máscara, este
  archivo produce `index 43839 out of bounds for type 'unsigned short[512]'`.

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
