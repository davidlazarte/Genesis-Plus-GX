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

/* #76: cartucho de Mega Drive con EEPROM I2C serie, declarada por CABECERA
   (0x1b0 = "RA", 0x1b2 = 0xe8) para no depender de la base de CRCs del core.
   El reset abre una lectura y el handler vertical le da dos pulsos de reloj por
   frame, con lo cual SIEMPRE hay una transaccion a medias en el borde de un
   frame: es la condicion de libretro/Genesis-Plus-GX#404. El byte leido va a la
   tabla de scroll horizontal, asi que perder el estado del bus se ve en el
   frame. */
size_t ayther_build_generated_rom_eeprom(uint8_t *rom, size_t capacity);

/* Variante para el bit de sprite exacto (#31/#41). Fondo uniforme, un sprite
 * INDISTINGUIBLE del fondo por su byte (mismo pattern, misma paleta, misma
 * prioridad) y un sprite OPERADOR de shadow/highlight. Son los dos casos que
 * el diff viejo contestaba mal, y ninguno de los dos existe en el fixture de
 * siempre -- ahi todos los sprites caen sobre otro indice, asi que el diff los
 * encontraba igual y el arreglo no se distinguiria de lo que ya habia. */
size_t ayther_build_generated_rom_sh(uint8_t *rom, size_t capacity);

/* #74: Mode 4 leido por el 68000, que es un camino que ningun otro fixture
   recorre. check-mode4 y check-mode4-raster manejan Mode 4 desde codigo Z80 en
   un cartucho de Master System; el VDP de Mega Drive tambien entra en Mode 4
   -- apagando el bit M5 del registro 1-- y ahi las lecturas del puerto de datos
   pasan por vdp_68k_data_r_m4, que no tenia con que probarse.

   El ROM escribe dos palabras distintas en dos direcciones de VRAM que la
   formula rota confunde entre si, lee la primera y deja lo leido en dos lugares
   observables: la palabra de work RAM en AYTHER_M4_RESULT (RETRO_MEMORY_SYSTEM_
   RAM) y la entrada 0 de CRAM, que con la name table en ceros pinta el frame
   entero. Verde = leyo lo suyo; rojo = leyo el alias.

   Las direcciones no son cualquiera: la formula entrelazada mapea ADDR_READ
   exactamente sobre ADDR_ALIAS.

     entrelazada(0x2002) = ((0x2002 << 1) & 0x3FC)
                         | ((0x2002 & 0x200) >> 8)
                         | (0x2002 & 0x3C00)       = 0x2004
     plana(0x2002)       = 0x2002 & 0x3FFE         = 0x2002

   Estan lejos del tile 0 a proposito: el fondo entero dibuja ese tile, asi que
   el patron de prueba no tiene que ensuciarlo. */
#define AYTHER_M4_ADDR_READ    0x2002u
#define AYTHER_M4_ADDR_ALIAS   0x2004u
#define AYTHER_M4_VALUE_GOOD   0x00e0u  /* verde en CRAM de Mode 5 */
#define AYTHER_M4_VALUE_ALIAS  0x000eu  /* rojo                    */
#define AYTHER_M4_RESULT       0x0010u  /* offset dentro de work_ram */
size_t ayther_build_generated_rom_mode4_68k(uint8_t *rom, size_t capacity);

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

/* #40, ultimo criterio: la misma escena con los dos efectos raster que el
   issue pedia, cada uno detras de un flag para poder afirmarlos por separado.

   SCROLL_LOCK   reg 0 bit 6 (las dos filas de arriba no scrollean en
                 horizontal) con hscroll = AYTHER_SMS_HSCROLL, y dos celdas
                 marcadoras con el patron 1 en blanco: una en la fila 0, que el
                 lock deja quieta, y otra en la fila MARKER_ROW, que se corre.
   PALETTE_SPLIT interrupcion de linea en SPLIT_LINE que cambia la entrada 1
                 de CRAM (el rojo del fondo) a SPLIT_COLOR, y la interrupcion
                 de frame que la devuelve a rojo. Es un cambio de CRAM a mitad
                 de pantalla: exactamente lo que el journal raster tiene que
                 ver, y lo que una recomposicion desde el estado final no puede
                 reproducir. */
#define AYTHER_SMS_SCENE_SCROLL_LOCK    1u
#define AYTHER_SMS_SCENE_PALETTE_SPLIT  2u
/* #75: escribe el YM2413 por el bus del FM externo (0xF0 direccion,
   0xF1 dato, 0xF2 control) y deja tres canales en key-on con block y
   fnum distintos. Es lo que le da algo que perder al contexto del OPLL:
   sin esto el chip queda en reset y un savestate suyo no distingue una
   carga sana de una podrida. */
#define AYTHER_SMS_SCENE_FM             4u
#define AYTHER_SMS_HSCROLL      8u
#define AYTHER_SMS_MARKER_COL   8u      /* celda: x = 64..71 sin scroll   */
#define AYTHER_SMS_MARKER_ROW   4u      /* fila 4: y = 32..39             */
#define AYTHER_SMS_SPLIT_LINE   96u
#define AYTHER_SMS_SPLIT_COLOR  0x0Fu   /* --BBGGRR: amarillo             */
size_t ayther_build_generated_rom_sms_scene(uint8_t *rom, size_t capacity,
                                            unsigned int flags);

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

/* #35 (traido de #27): escenas con un evento raster A MITAD DE FRAME.
 *
 * El fixture de siempre solo produce eventos que convergen al estado final
 * (CRAM desde el h-int, cada linea, con el valor de la linea), asi que un
 * replay que no restaurara nada era indistinguible de uno correcto -- se
 * verifico rompiendo la restauracion de CRAM y viendo el test pasar igual.
 *
 * Cada una de estas escenas hace el evento en LINE_A y lo DESHACE en LINE_B:
 * el estado final es el de arriba del evento, y el frame emitido tiene la
 * franja del medio distinta. Una recomposicion que no rejuegue el journal se
 * equivoca en esa franja; una que no restaure lo que toco deja el core sucio
 * para el frame siguiente. Las dos cosas se miden en scenes.c.
 *
 *   REG11     modo de hscroll (reg 11) a pantalla entera y de vuelta
 *   CRAM      entrada 1 de CRAM con un valor DISTINTO al final del frame
 *   HSCROLL   una entrada de la tabla de hscroll, en la franja del medio
 *   DISPLAY   pantalla apagada (reg 1) entre dos lineas
 *   HSCB      reg 13 mueve la BASE de la tabla de hscroll a otra tabla
 *   H32       reg 12 bit 0 (H40->H32) a mitad de pantalla: UNSUPPORTED_MODE
 *   OVERFLOW  dos escrituras a CRAM por linea: mas eventos que el journal */
#define AYTHER_SCENE_RASTER_NONE      0u
#define AYTHER_SCENE_RASTER_REG11     1u
#define AYTHER_SCENE_RASTER_CRAM      2u
#define AYTHER_SCENE_RASTER_HSCROLL   3u
#define AYTHER_SCENE_RASTER_DISPLAY   4u
#define AYTHER_SCENE_RASTER_HSCB      5u
#define AYTHER_SCENE_RASTER_H32       6u
#define AYTHER_SCENE_RASTER_OVERFLOW  7u
/* Dentro de las cuatro filas de celdas que el fondo llena (lineas 0-31): mas
   abajo solo hay backdrop y el evento no se veria en el frame emitido. */
#define AYTHER_RASTER_LINE_A 8u
#define AYTHER_RASTER_LINE_B 24u
unsigned int ayther_scene_raster(size_t index);

/* #63: el Z80 escribiendo sin parar al puerto de datos del VDP por la ventana
   de banco. Es el camino por el que el bucle de slots del FIFO se salia de la
   tabla, y ningun otro fixture tiene codigo Z80 en un cartucho de Mega Drive.
   El fondo no importa: lo que se observa es que el core siga vivo y que la
   VRAM se llene con lo que escribe el Z80. */
size_t ayther_build_generated_rom_z80_vdp(uint8_t *rom, size_t capacity);

/* #97: un Sega CD entero, sintetico. Dos archivos, porque el core los lee del
   disco (ver cd_fixture.h):

   La BIOS (128KB, lo que load_bios exige). La mitad baja es el programa del
   68000 principal: levanta el VDP y el FM/PSG como el cartucho de siempre,
   copia la mitad alta a PRG-RAM y suelta el sub-CPU escribiendo SRES=1 en
   $A12001. La mitad alta es el programa del sub-68000: llena la wave RAM del
   RF5C164, deja el canal 0 sonando, manda un Play al CDD desde el LBA 0 y
   despues cuenta sin parar en el buzon $FF8020, que el handler vertical del
   principal lee en $A12020 y escribe en la entrada 0 de CRAM. Asi el progreso
   del sub-CPU se VE (color de fondo), el PCM se OYE, y el CDD/CDC tienen
   estado que perder (LBA avanzando, sectores leidos).

   La imagen: sectores de 2048 bytes (MODE1 cocido) con "SEGADISCSYSTEM" al
   frente, que es lo unico que cdd_load mira para montarla. Una pista de datos
   de AYTHER_CD_ISO_SECTORS sectores, cada uno con un patron distinto, para
   que lo que lee el CDC no sea todo ceros. */
#define AYTHER_CD_BIOS_SIZE    0x20000u
#define AYTHER_CD_SECTOR_SIZE  2048u
#define AYTHER_CD_ISO_SECTORS  300u
#define AYTHER_CD_ISO_SIZE     (AYTHER_CD_ISO_SECTORS * AYTHER_CD_SECTOR_SIZE)
#define AYTHER_CD_ISO_NAME     "ayther-cd.iso"
size_t ayther_build_generated_cd_bios(uint8_t *bios, size_t capacity);
size_t ayther_build_generated_cd_image(uint8_t *iso, size_t capacity);

#endif
