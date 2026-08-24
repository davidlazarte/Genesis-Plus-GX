Here you can find binaries built using latest commited sourcecode, for the following platforms:

* Wii (standalone DOL executable)

* Gamecube (standalone DOL executable)

* Windows x64 and x86 (libretro core DLL, AYTHER build)

Notes:

(1) for Gamecube platform, two DOL files are provided:

* genplus_cube.dol is the default DOL version
* genplus_cube_low-mem.dol is an alternate DOL version, with dynamically allocated ROM buffer to reduce DOL memory footprint and fixes issues with loaders using Gamecube Auxiliary RAM (ARAM)

(2) libretro core is meant to be used with Retroarch application and is only provided for testing the core. It is not representative to official libretro core and is not supported.
    Please report libretro specific issues to https://github.com/libretro/Genesis-Plus-GX

(3) AYTHER fork delta: the libretro DLLs are OUR build, not upstream's. Both are
    compiled with llvm-mingw (msvcrt) at AYTHER_EXTENSIONS=1 SOUND_PROBE=1
    AYTHER_LEGACY_PROFILE=0, so they export the negotiated AYTHER surface:
    ayther_get_interface plus the seven audio_probe_* transport entry points.
    The deprecated loose ayther_recompose_multilayer symbol is NOT exported --
    it lives in the descriptor as `recompose_multilayer` since ABI 1.2, and the
    legacy profile is off. tests/ci/allowed_exports.txt is the source of truth.

* genesis_plus_gx_libretro.dll     -- x86_64. This is the architecture CI builds
                                      and tests; it matches the full-core replay
                                      golden (tests/ayther/golden/full_core_replay-windows-x64.json).
* genesis_plus_gx_libretro_x86.dll -- i386, provided for 32-bit frontends. CI
                                      does not build or test this one.

    The two cores emulate identically -- against the x64 golden the i386 build
    reproduces video_hash, audio_hash and telemetry_hash exactly. What differs
    is state_hash: the savestate is 252 bytes shorter on i386, because struct
    padding and pointer width change the serialized layout. SAVESTATES ARE NOT
    PORTABLE BETWEEN THE TWO DLLs. There is no windows-x86 golden for the same
    reason the project keeps one golden per platform.

    The Gamecube/Wii DOL files are still upstream's binaries: rebuilding them
    needs devkitPPC, which is not part of this fork's toolchain.
