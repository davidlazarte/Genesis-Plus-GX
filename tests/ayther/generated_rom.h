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

/* Variante para el bit de sprite exacto (#31/#41). Fondo uniforme, un sprite
 * INDISTINGUIBLE del fondo por su byte (mismo pattern, misma paleta, misma
 * prioridad) y un sprite OPERADOR de shadow/highlight. Son los dos casos que
 * el diff viejo contestaba mal, y ninguno de los dos existe en el fixture de
 * siempre -- ahi todos los sprites caen sobre otro indice, asi que el diff los
 * encontraba igual y el arreglo no se distinguiria de lo que ya habia. */
size_t ayther_build_generated_rom_sh(uint8_t *rom, size_t capacity);

/* Coordenadas de pantalla de los dos sprites de esa escena. */
#define AYTHER_SH_SPRITE_X 64
#define AYTHER_SH_SPRITE_Y 8
#define AYTHER_SH_OPERATOR_X 128
#define AYTHER_SH_OPERATOR_Y 8
/* Rectangulo de fondo puro: dentro de las cuatro filas de celdas que el ROM
   llena, y lejos de los dos sprites. */
#define AYTHER_SH_BG_X 8
#define AYTHER_SH_BG_Y 20

#endif
