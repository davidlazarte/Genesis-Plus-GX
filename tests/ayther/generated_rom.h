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

/* #39.C: 24 sprites en la misma linea -- cuatro mas que los 20 que el VDP
   dibuja en H40-- y uno en x=0 en el slot 12. */
#define AYTHER_SPR_FIXTURE_COUNT     24u
#define AYTHER_SPR_FIXTURE_MASK_SLOT 12u
#define AYTHER_SPR_FIXTURE_LIMIT     20u
size_t ayther_build_generated_rom_sprites(uint8_t *rom, size_t capacity);

/* #40: un cartucho de Master System -- codigo Z80-, para poder probar Mode 4
   sin depender de un ROM comercial que no puede vivir en el repo.

   Fondo rojo solido, backdrop azul y ocho sprites verdes de 8x8 en una fila:
   apagar el fondo deja la pantalla azul y suprimir un sprite le saca su
   cuadrado, y las dos cosas se ven en un hash del frame. */
#define AYTHER_SMS_SPRITES     8u
#define AYTHER_SMS_SPRITE_Y   96u
#define AYTHER_SMS_SPRITE_X0  40u
size_t ayther_build_generated_rom_sms(uint8_t *rom, size_t capacity);

/* Coordenadas de pantalla de los dos sprites de esa escena. */
#define AYTHER_SH_SPRITE_X 64
#define AYTHER_SH_SPRITE_Y 8
#define AYTHER_SH_OPERATOR_X 128
#define AYTHER_SH_OPERATOR_Y 8
/* Rectangulo de fondo puro: dentro de las cuatro filas de celdas que el ROM
   llena, y lejos de los dos sprites. */
#define AYTHER_SH_BG_X 8
#define AYTHER_SH_BG_Y 20

/* #35: una escena por modo de video (#35 punto 1).
 *
 * El ROM de siempre ejercita Mode 5 H40 progresivo NTSC y nada mas, asi que los
 * deltas del fork que tocan los otros modos no tenian con que probarse: #28
 * arreglo las mascaras de render en interlace 2 y en vscroll enhanced sin un
 * fixture que los ejercitara, y que hoy funcionen es una afirmacion que nadie
 * puede rehacer.
 *
 * Cada escena es un ROM completo y estatico. Uno por escena, y no segmentos
 * adentro de un solo ROM, porque asi no hace falta emitir un dispatcher en
 * 68000 y porque lo que las escenas tienen que dar es un hash POR MODO: un
 * golden agregado dice "algo se rompio", uno por escena dice cual. */
size_t ayther_build_generated_rom_scene(uint8_t *rom, size_t capacity,
                                        size_t scene);
size_t ayther_scene_count(void);
const char *ayther_scene_name(size_t index);

#endif
