#ifndef AYTHER_GENERATED_ROM_H
#define AYTHER_GENERATED_ROM_H

#include <stddef.h>
#include <stdint.h>

#define AYTHER_GENERATED_ROM_SIZE 65536u

/* Builds the redistributable Mega Drive workload used by the deterministic
 * full-core fixture. Returns the ROM size, or zero when capacity is too small
 * or the generated 68000 program does not fit. */
size_t ayther_build_generated_rom(uint8_t *rom, size_t capacity);

/* Variante con TRES canales de FM sonando. El fixture de arriba hace key-on
 * pero no programa los operadores, asi que el YM2612 queda mudo: sirve para
 * determinismo de video, no para verificar los controles de audio (#29).
 * Va aparte a proposito -- cambiar el de arriba habria movido los goldens de
 * las dos plataformas sin necesidad. */
size_t ayther_build_generated_rom_fm(uint8_t *rom, size_t capacity);

#endif
