# Checkpoint matrix with local ROMs: three save points per ROM, both SOUND_PROBE profiles — 2026-09-18

Generado a partir de los dos informes de `tests/ci/validate_roms.sh` (#123) y completado a mano
con la identidad de los binarios y los limites. No se guarda ni un byte del contenido de los
ROMs: solo nombre y el resultado del contrato.

## Version validada

| | |
|---|---|
| commit del core | `8d8539ae` (master; los binarios se construyeron ahi, ver `build_id`) |
| commit del probe | `9a573539` (rama `test/issue-123-checkpoint-matrix`: `--checkpoint` con lista) |
| binario SOUND_PROBE=1 | el asset `genesis_plus_gx_libretro_ayther_x64.dll` del release ayther-abi-1.10-r3; SHA-256 `2b062dc5056b7e572381f268e899e347bec634f527b72868852b9a9afc3f4bab`; `build_id` `Genesis Plus GX AYTHER ABI 1.10; core v1.7.4 8d8539ae`; `sound_probe` segun exports: `True` |
| binario SOUND_PROBE=0 | build local del mismo commit con `make -B -f Makefile.libretro platform=win64 AYTHER_EXTENSIONS=1 SOUND_PROBE=0` (el perfil estandar del README; no es asset del release); SHA-256 `f3c92fdd07963874cf1e7cbc763ef7a59a3400bdbdfe0cda8eaef429bcde5d8b`; `build_id` `Genesis Plus GX AYTHER ABI 1.10; core v1.7.4 1d8be486`; `sound_probe` segun exports: `False` |
| toolchain | llvm-mingw MSVCRT `20260519` (scoop), clang 22.1.6, el mismo que pinnea la CI |
| host | Windows 11 x64 |
| opciones del core | defaults (el probe no contesta `RETRO_ENVIRONMENT_GET_VARIABLE`) |
| entrada | la secuencia determinista de siempre, funcion del numero de frame: START 2 frames cada 180 desde el 60, A 2 frames cada 120 desde el 90, puerto 0 |
| checkpoints | 300, 900 y 1.500 de 1.800 frames; cada uno es una corrida entera de la ROM (original hasta 1.800 + continuacion restaurada desde el checkpoint, en el mismo proceso) |
| corpus | los mismos 14 `.md` de los informes anteriores |
| corridas | 14 ROMs x 3 checkpoints x 2 perfiles = 84 |

## Contrato

Por corrida: el contrato de raster en las dos partidas (recomponer cada frame y fallar si una
diferencia tiene mascara cero), y la continuacion restaurada tiene que dar la misma huella que
la original, frame a frame, en video, audio y estado serializado. `passed` exige las dos cosas.

## Resultado

### SOUND_PROBE=1

| | |
|---|---:|
| corridas (ROM x checkpoint) | 42 |
| frames juzgados por el contrato de raster | 113.400 |
| recomposicion distinta con mascara cero | 0 |
| recomposicion no disponible sin motivo | 0 |
| frames restaurados | 37.800 |
| video / audio / estado distintos del original | 0 / 0 / 0 |
| mascara de motivos distinta (informativo) | 0 |
| `passed` | true |

Por ROM y checkpoint (frames restaurados con video / audio / estado igual al original, de 900):

| ROM | 300 | 900 | 1.500 |
|---|---|---|---|
| Aladdin (USA) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Battle Mania (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Battle Mania Daiginjou (Japan, Korea) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Ecco the Dolphin (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Golden Axe (World) (Rev A) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Golden Axe II (World) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Musha Aleste - Full Metal Fighter Ellinor (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic & Knuckles + Sonic The Hedgehog 3 (Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic The Hedgehog (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic The Hedgehog 2 (World) (Rev A) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Streets of Rage 2 (USA) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Teenage Mutant Ninja Turtles - Return of the Shredder (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Toy Story (Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Vectorman (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |

### SOUND_PROBE=0

| | |
|---|---:|
| corridas (ROM x checkpoint) | 42 |
| frames juzgados por el contrato de raster | 113.400 |
| recomposicion distinta con mascara cero | 0 |
| recomposicion no disponible sin motivo | 0 |
| frames restaurados | 37.800 |
| video / audio / estado distintos del original | 0 / 0 / 0 |
| mascara de motivos distinta (informativo) | 0 |
| `passed` | true |

Por ROM y checkpoint (frames restaurados con video / audio / estado igual al original, de 900):

| ROM | 300 | 900 | 1.500 |
|---|---|---|---|
| Aladdin (USA) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Battle Mania (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Battle Mania Daiginjou (Japan, Korea) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Ecco the Dolphin (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Golden Axe (World) (Rev A) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Golden Axe II (World) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Musha Aleste - Full Metal Fighter Ellinor (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic & Knuckles + Sonic The Hedgehog 3 (Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic The Hedgehog (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Sonic The Hedgehog 2 (World) (Rev A) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Streets of Rage 2 (USA) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Teenage Mutant Ninja Turtles - Return of the Shredder (Japan) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Toy Story (Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |
| Vectorman (USA, Europe) | 1500 / 1500 / 1500 de 1500 | 900 / 900 / 900 de 900 | 300 / 300 / 300 de 300 |

### Total

| | |
|---|---:|
| corridas | 84 |
| frames juzgados por el contrato de raster | 226.800 |
| frames restaurados | 75.600 |
| **video / audio / estado distintos del original** | **0 / 0 / 0** |
| **recomposicion distinta con mascara cero / no disponible sin motivo** | **0 / 0** |
| mascara de motivos distinta (informativo) | 0 |

## Limites de soporte

- **Lo que esta corrida prueba**: para este corpus y esta entrada, en tres momentos distintos de
  cada partida y en los dos perfiles con extensiones, guardar y restaurar en el mismo proceso
  reproduce exactamente la continuacion, con el contrato de raster intacto. Tres puntos no son
  cualquier frame: son evidencia de regresion, no una prueba universal.
- **Mismo proceso**: el restore ocurre en la sesion que guardo. Entre procesos lo cubren
  `check-state-cross-process` (SMS, MD con el 68000 libre, Sega CD; #122) con fixtures sinteticos.
- **Estados de otro binario**: un estado de 1.10-r2 o anterior carga sin el bloque ATIM y NO
  garantiza continuidad exacta (`check-state-atim-compat`, #121; notas de r3).
- **Sistemas y plataforma**: 14 ROMs de Mega Drive, Windows x64 MSVCRT. `AYTHER_LEGACY_PROFILE=1`
  no se corrio contra el corpus. Linux y macOS validan el mismo commit con `check-rom-probe`
  (ROM sintetico libre, dos checkpoints), no con ROMs.

## Como se rehace

```sh
make -B -f Makefile.libretro platform=win64 AYTHER_EXTENSIONS=1 SOUND_PROBE=1 -j8   # o SOUND_PROBE=0
make -C tests raster-rom-probe CC=clang
tests/ci/validate_roms.sh "$(cygpath -m "$(pwd)/genesis_plus_gx_libretro.dll")" <directorio-de-roms> 1800 300,900,1500 probe1
```

Los informes por perfil, con la salida completa del probe, son
[`raster-roms-2026-09-18-checkpoint-probe1.md`](raster-roms-2026-09-18-checkpoint-probe1.md) y
[`raster-roms-2026-09-18-checkpoint-probe0.md`](raster-roms-2026-09-18-checkpoint-probe0.md).
