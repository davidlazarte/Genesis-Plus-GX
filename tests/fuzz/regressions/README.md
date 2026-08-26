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
