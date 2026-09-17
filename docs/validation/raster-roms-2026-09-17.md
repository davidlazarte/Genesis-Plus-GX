# Raster fallback validation with local ROMs — 2026-09-17 (version candidata)

Generado con `tests/ci/validate_roms.sh` y completado a mano con la identidad del
candidato y sus limites. No se guarda ni un byte del contenido de los ROMs:
solo nombre, tamanio y el resultado del contrato.

## Version candidata

| | |
|---|---|
| commit | `54c0db20` (`master`, merge de #111; incluye #102, #106, #109, #110 y #111) |
| CI remota | [run 35249133897](https://github.com/davidlazarte/Genesis-Plus-GX/actions/runs/35249133897): 22 jobs, todos `success` |
| core validado | `genesis_plus_gx_libretro.dll`, SHA-256 `6e1131b1c7fc591e078d286d54117ecde7a9a634e4f224a89155d9be9f1d757e` |
| comando de build | `make -B -f Makefile.libretro platform=win64 -j16` (perfil estandar del README) |
| perfil | `AYTHER_EXTENSIONS=1 SOUND_PROBE=0 AYTHER_LEGACY_PROFILE=0` (defaults de `Makefile.libretro`) |
| toolchain | llvm-mingw MSVCRT `20260519` (scoop `mingw-mstorsjo-llvm-msvcrt`), clang 22.1.6, el mismo que pinnea la CI |
| host | Windows 11 x64 |
| opciones del core | defaults (el probe no contesta `RETRO_ENVIRONMENT_GET_VARIABLE`) |
| entrada | pulsos deterministas de START/A de dos frames para pasar las pantallas de titulo (`raster_rom_probe.c`) |
| corpus | 14 archivos `.md` de la coleccion local, los mismos 14 titulos del informe del 2026-08-09 |
| frames | 1.800 por ROM, 25.200 en total |

### Matriz remota del commit

Todos en `success`:

- ASan and UBSan
- Full core under UBSan (Linux)
- Full core under UBSan (macOS arm64)
- Full-core determinism and performance (Linux)
- Fuzz corpus and regressions under ASan
- LTO build (opt-in, no adoptado)
- Linux core (clang, extensions=1, probe=1, legacy=0)
- Linux core (gcc, extensions=0, probe=0, legacy=0)
- Linux core (gcc, extensions=1, probe=0, legacy=0)
- Linux core (gcc, extensions=1, probe=1, legacy=0)
- Linux core (gcc, extensions=1, probe=1, legacy=1)
- Source quality
- ThreadSanitizer SPSC stress
- Windows UCRT (#45.A)
- Windows x64 MSVCRT (extensions=0, probe=0, legacy=0)
- Windows x64 MSVCRT (extensions=1, probe=0, legacy=0)
- Windows x64 MSVCRT (extensions=1, probe=1, legacy=0)
- Windows x64 MSVCRT (extensions=1, probe=1, legacy=1)
- macOS arm64 (extensions=0, probe=0, legacy=0)
- macOS arm64 (extensions=1, probe=0, legacy=0)
- macOS arm64 (extensions=1, probe=1, legacy=0)
- macOS arm64 (extensions=1, probe=1, legacy=1)

## Contrato y metodo

Para cada frame emulado, `tests/ayther/raster_rom_probe.c` captura el frame RGB565
del callback de video, lee la mascara de motivos del id privado `0x10E`, negocia la
ABI v1 y recompone el frame desde el mismo estado final del VDP, compara dimension
y cada pixel, y **falla** si una diferencia tiene mascara cero o si la recomposicion
no esta disponible sin `UNSUPPORTED_MODE`. Ademas valida `frame_generation`, la
paridad del snapshot de fallback y los limites de captura en cada frame.

## Resultado

| Clasificacion | Frames | 2026-08-09 |
|---|---:|---:|
| Recomposicion exacta, mascara cero | 22.988 | 22.915 |
| Recomposicion exacta, mascara conservadora | 1.686 | 1.760 |
| Recomposicion distinta, protegida por mascara | 526 | 525 |
| **Recomposicion distinta, mascara cero** | **0** | **0** |
| **Recomposicion no disponible sin motivo** | **0** | **0** |
| Sin callback de video | 0 | 0 |
| Total | 25.200 | 25.200 |

`passed: true`, exit 0. Los dos invariantes de seguridad se cumplen en los 25.200 frames.

Los conteos se corrieron unas decenas de frames respecto de agosto (mas exactos con
mascara cero, menos con mascara conservadora): entre las dos corridas el core absorbio
el sync con upstream de #74 (18 commits de ekeeke) y los arreglos de #97; el contrato
no cambio y ninguna diferencia cayo del lado equivocado.

Motivos por frame (un frame puede tener varios bits):

| Motivo | Frames |
|---|---:|
| `REG` | 324 |
| `VRAM` | 1.576 |
| `CRAM` | 10 |
| `VSRAM` | 422 |
| `HSCROLL` | 31 |
| `DMA` | 175 |
| `UNSUPPORTED_MODE` | 1 |

Por ROM:

| ROM | Exacta limpia | Exacta protegida | Distinta protegida | Falsos negativos | Sin motivo |
|---|---:|---:|---:|---:|---:|
| Aladdin (USA) | 1.445 | 351 | 4 | 0 | 0 |
| Battle Mania (Japan) | 1.769 | 24 | 7 | 0 | 0 |
| Battle Mania Daiginjou (Japan, Korea) | 1.759 | 35 | 6 | 0 | 0 |
| Ecco the Dolphin (USA, Europe) | 1.744 | 56 | 0 | 0 | 0 |
| Golden Axe (World) (Rev A) | 1.783 | 12 | 5 | 0 | 0 |
| Golden Axe II (World) | 1.780 | 19 | 1 | 0 | 0 |
| Musha Aleste - Full Metal Fighter Ellinor (Japan) | 1.574 | 225 | 1 | 0 | 0 |
| Sonic & Knuckles + Sonic The Hedgehog 3 (Europe) | 1.617 | 182 | 1 | 0 | 0 |
| Sonic The Hedgehog (USA, Europe) | 1.533 | 266 | 1 | 0 | 0 |
| Sonic The Hedgehog 2 (World) (Rev A) | 1.586 | 213 | 1 | 0 | 0 |
| Streets of Rage 2 (USA) | 1.740 | 51 | 9 | 0 | 0 |
| Teenage Mutant Ninja Turtles - Return of the Shredder (Japan) | 1.649 | 121 | 30 | 0 | 0 |
| Toy Story (Europe) | 1.627 | 115 | 58 | 0 | 0 |
| Vectorman (USA, Europe) | 1.382 | 16 | 402 | 0 | 0 |

## Limites de soporte

- **Lo que esta corrida prueba**: el contrato de fallback de raster (#5) sobre este corpus,
  con las opciones por defecto del core y una secuencia de entrada fija. No es una prueba
  para cualquier ROM, opcion o rama de savestate/entrada; es evidencia de regresion.
- **Sistemas**: los 14 ROMs son de Mega Drive. Master System, Game Gear y Sega CD no estan
  en este corpus; su cobertura en CI es sintetica (`check-mode4*`, `check-state-roundtrip`
  con `sms`, `sms-fm`, `gg` y `scd`, y la escena `scd` de `check-fuzz`).
- **Perfil**: se valido el perfil estandar (`SOUND_PROBE=0`). Los perfiles con
  `SOUND_PROBE=1` y `AYTHER_LEGACY_PROFILE=1` comparten el mismo golden de emulacion en la
  CI (`check-full-core`, `check-state-cross-profile`) pero no se corrieron contra este corpus.
- **Plataformas**: el corpus corrio en Windows x64 MSVCRT. Linux y macOS arm64 validan el
  mismo commit con el golden unico de `full_core_replay-x64.json` y con UBSan, no con ROMs.
- **Modo**: los frames con `UNSUPPORTED_MODE` (1 en Aladdin) no se recomponen por diseno y
  el motivo lo declara; el resto de los modos de video se cubre con las escenas sinteticas
  de `check-scenes`.
- **Savestates**: el probe no guarda ni carga estados durante la corrida. La completitud del
  savestate se afirma aparte (`check-state-roundtrip`, seis fixtures) y su validacion frente a
  bytes corruptos la cubre `check-fuzz` con el core instrumentado. (Cubierto despues por #117:
  `raster-roms-2026-09-17-checkpoint.md`, que sobre este mismo binario encontro #118.)

## Como se rehace

```sh
make -B -f Makefile.libretro platform=win64 -j16
make -C tests raster-rom-probe CC=clang
tests/ci/validate_roms.sh "$(cygpath -m "$(pwd)/genesis_plus_gx_libretro.dll")" <directorio-de-roms> 1800
```

## Salida del probe

```
probe:  /c/Users/david/Workspaces/gpgx-src/tests/.build/raster_rom_probe.exe
core:   C:/Users/david/Workspaces/gpgx-src/genesis_plus_gx_libretro.dll
roms:   14 en C:/Users/david/ROMs/Genesis
frames: 1800 por ROM
[1/14] Aladdin (USA).md
{"type":"rom","rom":"Aladdin (USA).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":1445,"guarded_equal":351,"clean_mismatch":0,"guarded_mismatch":4,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":14,"CRAM":3,"VSRAM":0,"HSCROLL":13,"DMA":13,"UNSUPPORTED_MODE":1,"VRAM":351},"different_pixels":20192,"dimensions":"320x224/320x224","first_failure":null}
[2/14] Battle Mania (Japan).md
{"type":"rom","rom":"Battle Mania (Japan).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":1769,"guarded_equal":24,"clean_mismatch":0,"guarded_mismatch":7,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":5,"CRAM":1,"VSRAM":0,"HSCROLL":0,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":25},"different_pixels":23806,"dimensions":"320x224/320x224","first_failure":null}
[3/14] Battle Mania Daiginjou (Japan, Korea).md
{"type":"rom","rom":"Battle Mania Daiginjou (Japan, Korea).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":1759,"guarded_equal":35,"clean_mismatch":0,"guarded_mismatch":6,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":10,"CRAM":1,"VSRAM":0,"HSCROLL":0,"DMA":2,"UNSUPPORTED_MODE":0,"VRAM":32},"different_pixels":67512,"dimensions":"320x224/320x224","first_failure":null}
[4/14] Ecco the Dolphin (USA, Europe).md
{"type":"rom","rom":"Ecco the Dolphin (USA, Europe).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":1744,"guarded_equal":56,"clean_mismatch":0,"guarded_mismatch":0,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":0,"HSCROLL":3,"DMA":26,"UNSUPPORTED_MODE":0,"VRAM":54},"different_pixels":0,"dimensions":"320x224/320x224","first_failure":null}
[5/14] Golden Axe (World) (Rev A).md
{"type":"rom","rom":"Golden Axe (World) (Rev A).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":1783,"guarded_equal":12,"clean_mismatch":0,"guarded_mismatch":5,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":3,"CRAM":1,"VSRAM":2,"HSCROLL":4,"DMA":3,"UNSUPPORTED_MODE":0,"VRAM":13},"different_pixels":54790,"dimensions":"320x224/320x224","first_failure":null}
[6/14] Golden Axe II (World).md
{"type":"rom","rom":"Golden Axe II (World).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":1780,"guarded_equal":19,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":18,"CRAM":0,"VSRAM":0,"HSCROLL":1,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":2},"different_pixels":4800,"dimensions":"320x224/320x224","first_failure":null}
[7/14] Musha Aleste - Full Metal Fighter Ellinor (Japan).md
{"type":"rom","rom":"Musha Aleste - Full Metal Fighter Ellinor (Japan).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":1574,"guarded_equal":225,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":3,"CRAM":2,"VSRAM":0,"HSCROLL":2,"DMA":13,"UNSUPPORTED_MODE":0,"VRAM":223},"different_pixels":12201,"dimensions":"320x224/320x224","first_failure":null}
[8/14] Sonic & Knuckles + Sonic The Hedgehog 3 (Europe).md
{"type":"rom","rom":"Sonic & Knuckles + Sonic The Hedgehog 3 (Europe).md","rom_bytes":4194304,"frames":1800,"categories":{"clean_equal":1617,"guarded_equal":182,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":4,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":11,"UNSUPPORTED_MODE":0,"VRAM":180},"different_pixels":104,"dimensions":"320x224/320x224","first_failure":null}
[9/14] Sonic The Hedgehog (USA, Europe).md
{"type":"rom","rom":"Sonic The Hedgehog (USA, Europe).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":1533,"guarded_equal":266,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":2,"CRAM":1,"VSRAM":1,"HSCROLL":1,"DMA":6,"UNSUPPORTED_MODE":0,"VRAM":265},"different_pixels":19520,"dimensions":"320x224/320x224","first_failure":null}
[10/14] Sonic The Hedgehog 2 (World) (Rev A).md
{"type":"rom","rom":"Sonic The Hedgehog 2 (World) (Rev A).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":1586,"guarded_equal":213,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":4,"UNSUPPORTED_MODE":0,"VRAM":213},"different_pixels":4384,"dimensions":"320x224/320x224","first_failure":null}
[11/14] Streets of Rage 2 (USA).md
{"type":"rom","rom":"Streets of Rage 2 (USA).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":1740,"guarded_equal":51,"clean_mismatch":0,"guarded_mismatch":9,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":4,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":3,"UNSUPPORTED_MODE":0,"VRAM":57},"different_pixels":1885,"dimensions":"320x224/320x224","first_failure":null}
[12/14] Teenage Mutant Ninja Turtles - Return of the Shredder (Japan).md
{"type":"rom","rom":"Teenage Mutant Ninja Turtles - Return of the Shredder (Japan).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":1649,"guarded_equal":121,"clean_mismatch":0,"guarded_mismatch":30,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":91,"CRAM":1,"VSRAM":0,"HSCROLL":1,"DMA":29,"UNSUPPORTED_MODE":0,"VRAM":88},"different_pixels":34297,"dimensions":"256x224/256x224","first_failure":null}
[13/14] Toy Story (Europe).md
{"type":"rom","rom":"Toy Story (Europe).md","rom_bytes":4194304,"frames":1800,"categories":{"clean_equal":1627,"guarded_equal":115,"clean_mismatch":0,"guarded_mismatch":58,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":167,"CRAM":0,"VSRAM":4,"HSCROLL":6,"DMA":65,"UNSUPPORTED_MODE":0,"VRAM":71},"different_pixels":866520,"dimensions":"320x224/320x224","first_failure":null}
[14/14] Vectorman (USA, Europe).md
{"type":"rom","rom":"Vectorman (USA, Europe).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":1382,"guarded_equal":16,"clean_mismatch":0,"guarded_mismatch":402,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":415,"HSCROLL":0,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":2},"different_pixels":9958746,"dimensions":"320x224/320x224","first_failure":null}
{"type":"summary","roms":14,"frames":25200,"clean_equal":22988,"guarded_equal":1686,"clean_mismatch":0,"guarded_mismatch":526,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0,"passed":true}
```
