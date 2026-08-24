Here you can find binaries built using latest commited sourcecode, for the following platforms:

* Wii (standalone DOL executable)

* Gamecube (standalone DOL executable)

* Win32 (libretro core DLL)

Notes:

(1) for Gamecube platform, two DOL files are provided:

* genplus_cube.dol is the default DOL version
* genplus_cube_low-mem.dol is an alternate DOL version, with dynamically allocated ROM buffer to reduce DOL memory footprint and fixes issues with loaders using Gamecube Auxiliary RAM (ARAM)

(2) libretro core is meant to be used with Retroarch application and is only provided for testing the core on Win32 platform. It is not representative to official libretro core and is not supported.
    Please report libretro specific issues to https://github.com/libretro/Genesis-Plus-GX

---

## AYTHER fork

**Los binarios de arriba son los de upstream, y se dejan intactos a proposito.**
No traen AYTHER. Nuestros builds NO viven aca: viven en los releases del fork.

    https://github.com/davidlazarte/Genesis-Plus-GX/releases

Hubo un tiempo en que si vivian aca, bajo el mismo nombre que usa upstream, y
salio mal de la peor manera: `genesis_plus_gx_libretro.dll` es un archivo que
upstream regenera en cada release suyo, asi que cada sync lo sobreescribia. El
resultado es que durante TODO el historial del fork este directorio ofrecio un
core sin una sola linea de AYTHER, y nadie lo noto porque el archivo estaba y
tenia el nombre correcto.

Dejarlo a nombre de upstream tiene ademas un beneficio concreto: `builds/` deja
de ser superficie de conflicto en los syncs.

Que hay en cada release, con perfil y arquitectura explicitos en el nombre --
ninguno queda sin sufijo, porque un nombre pelado es justamente lo que invita a
preguntar "y este cual es":

| asset | arch | perfil |
|---|---|---|
| `genesis_plus_gx_libretro_ayther_x64.dll` | x86_64 | `AYTHER_EXTENSIONS=1 SOUND_PROBE=1` |
| `genesis_plus_gx_libretro_ayther_x86.dll` | i386 | `AYTHER_EXTENSIONS=1 SOUND_PROBE=1` |
| `genesis_plus_gx_libretro_stock_x64.dll` | x86_64 | `AYTHER_EXTENSIONS=0` -- sin ABI, el caso negativo de `abi_negociacion` |

Los `.dol` de Gamecube/Wii son los de upstream y ahi se quedan: rebuildearlos
necesita devkitPPC, que no esta en la toolchain de este fork, y esas
plataformas estan fuera del scope actual.

Para compilar los nuestros, ver `docs/upstream_sync_runbook.md`.
