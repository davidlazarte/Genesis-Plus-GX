# Raster fallback validation with local ROMs, with a mid-game checkpoint — 2026-09-17

Generado con `tests/ci/validate_roms.sh` (#117) y completado a mano con la identidad del
binario, la comparacion antes/despues de #118 y los limites. No se guarda ni un byte del
contenido de los ROMs: solo nombre, tamanio y el resultado del contrato.

## Corrida

- core: `genesis_plus_gx_libretro.dll`
- ROMs: 14
- frames por ROM: 1800
- checkpoint: estado guardado en el frame 900; continuacion 900..1800 corrida dos veces (original y restaurada)
- core (linea `core` del probe): `{"type":"core","build_id":"Genesis Plus GX AYTHER ABI 1.10; core v1.7.4 058817d4","sound_probe":true,"checkpoint_frame":900,"input":"port 0 joypad: START 2 frames every 180 from frame 60; A 2 frames every 120 from frame 90"}`
- resultado: sin violaciones del contrato

## Version validada

| | |
|---|---|
| commit | `058817d4` (rama `feat/issue-117-checkpoint-roms`, PR #120: `test (#117)` + `fix (#118)`, apilada sobre PR #119) |
| core validado | `genesis_plus_gx_libretro.dll` construido desde ese commit, arbol limpio; SHA-256 `c9b453315a61f1d472ab44de8740a8b8121475e5b0d9720756834162f13d3fa3` |
| comando de build | `make -f Makefile.libretro platform=win64 AYTHER_EXTENSIONS=1 SOUND_PROBE=1 -j8` |
| perfil | `AYTHER_EXTENSIONS=1 SOUND_PROBE=1 AYTHER_LEGACY_PROFILE=0` -- el perfil del asset `_ayther_x64` del release; la linea `core` del probe lo confirma por los exports (`sound_probe: true`) |
| toolchain | llvm-mingw MSVCRT `20260519` (scoop), clang 22.1.6, el mismo que pinnea la CI |
| host | Windows 11 x64 |
| opciones del core | defaults (el probe no contesta `RETRO_ENVIRONMENT_GET_VARIABLE`) |
| entrada | la misma secuencia determinista del informe de raster, funcion del numero de frame: START 2 frames cada 180 desde el 60, A 2 frames cada 120 desde el 90, puerto 0 (la registra la linea `core` del probe) |
| checkpoint | frame 900 de 1.800: `retro_serialize` entre frames (1.036.288 bytes), continuacion 900..1.799 corrida, `retro_unserialize` en el mismo proceso, misma continuacion corrida de nuevo |
| corpus | los mismos 14 `.md` del informe de raster del 2026-09-17 |
| frames | 1.800 por ROM mas 900 restaurados: 2.700 juzgados por ROM, 37.800 en total |

## Contrato y metodo

Ademas del contrato de raster (recomponer cada frame y fallar si una diferencia tiene mascara
cero), `raster_rom_probe --checkpoint` guarda por frame de la continuacion original tres huellas
FNV-1a -- el frame RGB565, las muestras de audio del frame y el **estado serializado** tras el
frame-- y exige que la continuacion restaurada de las tres iguales frame a frame. La mascara de
motivos puede diferir (restaurar marca memoria como sucia): se cuenta, no falla. El contrato de
raster se afirma en las dos corridas.

## Resultado

Raster, sobre los 37.800 frames juzgados (1.800 originales + 900 restaurados por ROM):

| Clasificacion | Frames |
|---|---:|
| Recomposicion exacta, mascara cero | 35.116 |
| Recomposicion exacta, mascara conservadora | 2.053 |
| Recomposicion distinta, protegida por mascara | 631 |
| **Recomposicion distinta, mascara cero** | **0** |
| **Recomposicion no disponible sin motivo** | **0** |

Checkpoint, sobre los 12.600 frames restaurados (900 por ROM):

| | Frames |
|---|---:|
| video igual al original | 12.600 |
| audio igual al original | 12.600 |
| estado serializado igual al original | 12.600 |
| mascara de motivos distinta | 0 |
| **video, audio o estado distintos** | **0** |

`passed: true`, exit 0.

### Lo que la misma corrida midio ANTES del arreglo (#118)

El primer pase se hizo con el binario del release `ayther-abi-1.10-r2` (`9e8b6bc6`, SHA-256
`0e118623...`), identico salvo por #118. Con el mismo corpus, checkpoint y entrada, 12 de 14 ROMs
no reproducian la continuacion: el audio difiere desde el primer frame restaurado en casi todas y
Musha Aleste diverge tambien en video 78 frames despues. `--state-diff` mostro, tras el primer
frame restaurado, distintos el PC del 68000, el pc y los cycles del Z80 y el bloque de continuidad
del audio: los dos CPUs terminaban el frame en otro punto porque `m68k.refresh_cycles` (la fase
del stall de refresh del bus) no viajaba en el savestate.

| ROM | antes: frames con video / audio distintos (primer frame) | despues: video igual | audio igual | video / audio / estado distintos |
|---|---|---:|---:|---|
| Aladdin (USA) | 0 / 337 (frame 1172) | 900 | 900 | 0 / 0 / 0 |
| Battle Mania (Japan) | 0 / 26 (frame 900) | 900 | 900 | 0 / 0 / 0 |
| Battle Mania Daiginjou (Japan, Korea) | 0 / 32 (frame 900) | 900 | 900 | 0 / 0 / 0 |
| Ecco the Dolphin (USA, Europe) | 0 / 0 | 900 | 900 | 0 / 0 / 0 |
| Golden Axe (World) (Rev A) | 0 / 133 (frame 901) | 900 | 900 | 0 / 0 / 0 |
| Golden Axe II (World) | 0 / 7 (frame 970) | 900 | 900 | 0 / 0 / 0 |
| Musha Aleste - Full Metal Fighter Ellinor (Japan) | 118 / 155 (frame 978) | 900 | 900 | 0 / 0 / 0 |
| Sonic & Knuckles + Sonic The Hedgehog 3 (Europe) | 0 / 19 (frame 901) | 900 | 900 | 0 / 0 / 0 |
| Sonic The Hedgehog (USA, Europe) | 0 / 12 (frame 900) | 900 | 900 | 0 / 0 / 0 |
| Sonic The Hedgehog 2 (World) (Rev A) | 0 / 18 (frame 900) | 900 | 900 | 0 / 0 / 0 |
| Streets of Rage 2 (USA) | 0 / 0 | 900 | 900 | 0 / 0 / 0 |
| Teenage Mutant Ninja Turtles - Return of the Shredder (Japan) | 0 / 528 (frame 941) | 900 | 900 | 0 / 0 / 0 |
| Toy Story (Europe) | 0 / 50 (frame 961) | 900 | 900 | 0 / 0 / 0 |
| Vectorman (USA, Europe) | 0 / 39 (frame 903) | 900 | 900 | 0 / 0 / 0 |

## Limites de soporte

- **Lo que esta corrida prueba**: que, para este corpus y esta entrada, guardar en el frame 900
  y restaurar en el mismo proceso reproduce exactamente la continuacion (video, audio y estado
  serializado por frame) con el contrato de raster intacto. Un checkpoint por ROM, en un solo
  frame: no es una prueba para cualquier frame ni para estados guardados por otro binario.
- **Mismo proceso**: el restore ocurre en la sesion que guardo. El caso entre procesos lo cubre
  `check-state-cross-process` con fixtures sinteticos; el caso entre binarios distintos (un
  estado de 1.10-r2 cargado por este core) entra por el camino "sin bloque ATIM", que recompone
  la fase del refresh desde `cycles` y NO garantiza continuidad exacta.
- **Sistemas y perfil**: 14 ROMs de Mega Drive, perfil `SOUND_PROBE=1`. `SOUND_PROBE=0` y
  `AYTHER_LEGACY_PROFILE=1` comparten el golden de emulacion en CI (`check-full-core`,
  `check-profile-comparison`) pero no se corrieron contra este corpus. Master System, Game Gear
  y Sega CD siguen cubiertos solo por fixtures sinteticos.
- **Plataforma**: Windows x64 MSVCRT. Linux y macOS validan el mismo commit con
  `check-rom-probe` (ROM sintetico libre, checkpoint en el frame 60 de 180), no con ROMs.

## Como se rehace

```sh
make -f Makefile.libretro platform=win64 AYTHER_EXTENSIONS=1 SOUND_PROBE=1 -j8
make -C tests raster-rom-probe CC=clang
tests/ci/validate_roms.sh "$(cygpath -m "$(pwd)/genesis_plus_gx_libretro.dll")" <directorio-de-roms> 1800 900
```

## Salida del probe

```
[1/14] Aladdin (USA).md
{"type":"core","build_id":"Genesis Plus GX AYTHER ABI 1.10; core v1.7.4 058817d4","sound_probe":true,"checkpoint_frame":900,"input":"port 0 joypad: START 2 frames every 180 from frame 60; A 2 frames every 120 from frame 90"}
{"type":"rom","rom":"Aladdin (USA).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":2328,"guarded_equal":365,"clean_mismatch":0,"guarded_mismatch":7,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":26,"CRAM":6,"VSRAM":0,"HSCROLL":26,"DMA":24,"UNSUPPORTED_MODE":1,"VRAM":364},"different_pixels":37250,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[2/14] Battle Mania (Japan).md
{"type":"rom","rom":"Battle Mania (Japan).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":2661,"guarded_equal":32,"clean_mismatch":0,"guarded_mismatch":7,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":5,"CRAM":1,"VSRAM":0,"HSCROLL":0,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":33},"different_pixels":23806,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[3/14] Battle Mania Daiginjou (Japan, Korea).md
{"type":"rom","rom":"Battle Mania Daiginjou (Japan, Korea).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":2635,"guarded_equal":55,"clean_mismatch":0,"guarded_mismatch":10,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":13,"CRAM":1,"VSRAM":0,"HSCROLL":0,"DMA":3,"UNSUPPORTED_MODE":0,"VRAM":54},"different_pixels":71141,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[4/14] Ecco the Dolphin (USA, Europe).md
{"type":"rom","rom":"Ecco the Dolphin (USA, Europe).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":2643,"guarded_equal":57,"clean_mismatch":0,"guarded_mismatch":0,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":0,"HSCROLL":4,"DMA":27,"UNSUPPORTED_MODE":0,"VRAM":54},"different_pixels":0,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[5/14] Golden Axe (World) (Rev A).md
{"type":"rom","rom":"Golden Axe (World) (Rev A).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":2680,"guarded_equal":13,"clean_mismatch":0,"guarded_mismatch":7,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":3,"CRAM":1,"VSRAM":2,"HSCROLL":4,"DMA":4,"UNSUPPORTED_MODE":0,"VRAM":16},"different_pixels":57822,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[6/14] Golden Axe II (World).md
{"type":"rom","rom":"Golden Axe II (World).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":2680,"guarded_equal":19,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":18,"CRAM":0,"VSRAM":0,"HSCROLL":1,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":2},"different_pixels":4800,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[7/14] Musha Aleste - Full Metal Fighter Ellinor (Japan).md
{"type":"rom","rom":"Musha Aleste - Full Metal Fighter Ellinor (Japan).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":2474,"guarded_equal":225,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":3,"CRAM":2,"VSRAM":0,"HSCROLL":2,"DMA":13,"UNSUPPORTED_MODE":0,"VRAM":223},"different_pixels":12201,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[8/14] Sonic & Knuckles + Sonic The Hedgehog 3 (Europe).md
{"type":"rom","rom":"Sonic & Knuckles + Sonic The Hedgehog 3 (Europe).md","rom_bytes":4194304,"frames":1800,"categories":{"clean_equal":2467,"guarded_equal":232,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":4,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":11,"UNSUPPORTED_MODE":0,"VRAM":230},"different_pixels":104,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[9/14] Sonic The Hedgehog (USA, Europe).md
{"type":"rom","rom":"Sonic The Hedgehog (USA, Europe).md","rom_bytes":524288,"frames":1800,"categories":{"clean_equal":2390,"guarded_equal":309,"clean_mismatch":0,"guarded_mismatch":1,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":2,"CRAM":1,"VSRAM":1,"HSCROLL":1,"DMA":6,"UNSUPPORTED_MODE":0,"VRAM":308},"different_pixels":19520,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[10/14] Sonic The Hedgehog 2 (World) (Rev A).md
{"type":"rom","rom":"Sonic The Hedgehog 2 (World) (Rev A).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":2419,"guarded_equal":279,"clean_mismatch":0,"guarded_mismatch":2,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":4,"UNSUPPORTED_MODE":0,"VRAM":280},"different_pixels":8768,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[11/14] Streets of Rage 2 (USA).md
{"type":"rom","rom":"Streets of Rage 2 (USA).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":2616,"guarded_equal":66,"clean_mismatch":0,"guarded_mismatch":18,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":4,"CRAM":0,"VSRAM":0,"HSCROLL":0,"DMA":3,"UNSUPPORTED_MODE":0,"VRAM":81},"different_pixels":3770,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[12/14] Teenage Mutant Ninja Turtles - Return of the Shredder (Japan).md
{"type":"rom","rom":"Teenage Mutant Ninja Turtles - Return of the Shredder (Japan).md","rom_bytes":1048576,"frames":1800,"categories":{"clean_equal":2444,"guarded_equal":198,"clean_mismatch":0,"guarded_mismatch":58,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":158,"CRAM":1,"VSRAM":0,"HSCROLL":2,"DMA":39,"UNSUPPORTED_MODE":0,"VRAM":134},"different_pixels":46890,"dimensions":"256x224/256x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[13/14] Toy Story (Europe).md
{"type":"rom","rom":"Toy Story (Europe).md","rom_bytes":4194304,"frames":1800,"categories":{"clean_equal":2399,"guarded_equal":185,"clean_mismatch":0,"guarded_mismatch":116,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":290,"CRAM":0,"VSRAM":5,"HSCROLL":8,"DMA":93,"UNSUPPORTED_MODE":0,"VRAM":104},"different_pixels":1733040,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
[14/14] Vectorman (USA, Europe).md
{"type":"rom","rom":"Vectorman (USA, Europe).md","rom_bytes":2097152,"frames":1800,"categories":{"clean_equal":2280,"guarded_equal":18,"clean_mismatch":0,"guarded_mismatch":402,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0},"reason_frames":{"REG":1,"CRAM":0,"VSRAM":415,"HSCROLL":0,"DMA":0,"UNSUPPORTED_MODE":0,"VRAM":4},"different_pixels":9958746,"dimensions":"320x224/320x224","first_failure":null,"checkpoint":{"frame":900,"bytes":1036288,"restored_frames":900,"video_equal":900,"video_mismatch":0,"audio_equal":900,"audio_mismatch":0,"mask_differs":0,"state_equal":900,"state_mismatch":0,"first_state_mismatch":-1,"first_mismatch":null}}
{"type":"summary","roms":14,"frames":25200,"clean_equal":35116,"guarded_equal":2053,"clean_mismatch":0,"guarded_mismatch":631,"unsupported_guarded":0,"unavailable_without_reason":0,"missing_video":0,"checkpoint_frame":900,"restored_frames":12600,"restored_video_mismatch":0,"restored_audio_mismatch":0,"restored_mask_differs":0,"restored_state_mismatch":0,"passed":true}
```
