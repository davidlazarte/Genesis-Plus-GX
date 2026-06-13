> ## ⚡ Aether Fork
>
> Esta rama (`aether/expose-vram-video-ram`) es el core de
> [**Aether**](https://github.com/davidlazarte/aether) — un "RTX Remix para
> juegos 2D retro". Mantiene **deltas mínimos** sobre el upstream
> `ekeeke/Genesis-Plus-GX`, todos en `libretro/libretro.c` salvo el de
> savestate:
>
> | Delta | Qué expone | Commit |
> |---|---|---|
> | VRAM | `retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM)` → `vram` (64KB) — upstream devuelve NULL | `7fcf9bc` |
> | Latch input-hw | serializa el estado del pad de 6 botones (fase TH) en el savestate (STATE_VERSION 1.7.7) — sin esto, restaurar un savestate a mitad de replay diverge | `539dc45` |
> | CRAM | id **privado** `0x100` → `cram` (128 B, 64 colores de 9 bits) | `195ebcb` |
> | Registros VDP | id **privado** `0x101` → `reg[0x20]` (bases de planos + tamaño, para el tilemap viewer) | `09dd082` |
>
> **Ids privados**: `0x100`/`0x101` no son estándar de libretro (la convención
> reserva ≥`0x100` para usos no estandarizados). El Lab de Aether los consume
> vía `RetroRunner`/`AetherSession`; un core stock devuelve null y el Lab
> degrada (las vistas VRAM/CRAM/tilemap quedan deshabilitadas). **VRAM y CRAM
> se exponen word-swapped en hosts little-endian** (igual que la Work RAM): el
> byte lógico `off` vive en el array en `off^1`.
>
> **Build local** (la DLL no se versiona — BYOC): con un toolchain
> **llvm-mingw de runtime MSVCRT** (`scoop install mingw-mstorsjo-llvm-msvcrt`)
> en el PATH —
> ```
> make -f Makefile.libretro platform=win64 -j8
> ```
> Un build UCRT crasha en `retro_load_game` (STATUS_STACK_BUFFER_OVERRUN); el
> MSVCRT es bit-idéntico al DLL stock en emulación. La DLL resultante se
> despliega en Aether como `third_party/cores/genesis_plus_gx_libretro_vram.dll`.
>
> Rebase con upstream: revisar que `ekeeke/Genesis-Plus-GX` no haya
> implementado `RETRO_MEMORY_VIDEO_RAM` (entraría en conflicto con `7fcf9bc`).

[![Build Status](https://travis-ci.org/libretro/Genesis-Plus-GX.svg?branch=master)](https://travis-ci.org/libretro/Genesis-Plus-GX)
[![Build status](https://ci.appveyor.com/api/projects/status/d72k6bipi13o15v4/branch/master?svg=true)](https://ci.appveyor.com/project/bparker06/genesis-plus-gx/branch/master)


Genesis Plus GX is an open-source Sega 8/16 bit emulator focused on accuracy and portability. Initially ported and developped on Gamecube / Wii consoles through [libogc / devkitPPC](http://sourceforge.net/projects/devkitpro/), this emulator is now available on many other platforms through various frontends such as:

* [Retroarch (libretro)](http://www.libretro.com)

* [Bizhawk](http://tasvideos.org/Bizhawk.html)

* [OpenEmu](http://openemu.org/)

----

The source code, initially based on Genesis Plus 1.2a by [Charles MacDonald](http://www.techno-junk.org/ ) has been heavily modified & enhanced, with respect to original goals and design, in order to improve emulation accuracy as well as adding support for new peripherals, cartridge or console hardware and many other exciting [features](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/wiki/Features.md).

The result is that Genesis Plus GX is now more a continuation of the original project than a simple port, providing very accurate emulation and [100% compatibility](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/wiki/Compatibility.md) with Genesis / Mega Drive, Sega/Mega CD, Master System, Game Gear & SG-1000 released software (including all unlicensed or pirate known dumps), also emulating backwards compatibility modes when available. All the people who contributed (directly or indirectly) to this project are listed on the [Credits](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/wiki/Credits.md) page.

----

Multi-platform sourcecode (core), which is made available for use under a specific non-commercial [license](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/LICENSE.txt), is maintained on [Bitbucket](https://bitbucket.org/eke/genesis-plus-gx/src/) / [Github](https://github.com/ekeeke/Genesis-Plus-GX) so that other Genesis Plus ports can benefit of it, as I really wish this emulator becomes a reference for _portable_ and _accurate_ Sega 8/16-bit emulation. If you ported this emulator to other platforms or need help porting it, feel free to contact me.

----

Latest official Gamecube / Wii standalone port (screenshots below) is available [here](https://github.com/ekeeke/Genesis-Plus-GX/tree/master/builds). Be sure to check the included [user manual](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/gx/docs/README.pdf) first. A [startup guide](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/wiki/Getting%20Started.md) and a [FAQ](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/wiki/Frequently%20Asked%20Questions.md) are also available.

![MainMenu.png](https://bitbucket.org/repo/7AjE6M/images/3565283297-MainMenu.png)
![menu_load.png](https://bitbucket.org/repo/7AjE6M/images/164055790-menu_load.png)

![RomBrowser.png](https://bitbucket.org/repo/7AjE6M/images/1972035547-RomBrowser.png)
![CtrlMenu.png](https://bitbucket.org/repo/7AjE6M/images/2283464354-CtrlMenu.png)

----

You can also test latest compiled builds for Gamecube / Wii and Retroarch (Windows 32-bit version only) by downloading them from [here](https://github.com/ekeeke/Genesis-Plus-GX/tree/master/builds).

----

[![btn_donate_LG.gif](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=2966212) If you like this project and want to show your appreciation, Paypal donations are always welcomed.
