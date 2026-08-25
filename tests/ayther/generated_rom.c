/* Redistributable Mega Drive ROM generator for AYTHER integration tests.
 *
 * The fixture intentionally exercises four families without embedding any
 * third-party ROM data:
 *   - CRAM writes from the horizontal interrupt (raster effects);
 *   - per-line Plane A/Plane B horizontal scroll;
 *   - 24 linked sprites sharing a visible band (sprite pressure);
 *   - repeated PSG and YM2612 writes from the vertical interrupt.
 *
 * Instructions and data are emitted directly in big-endian 68000 form so CI
 * does not need a cross assembler.
 */

#include "generated_rom.h"

#include <string.h>

#define RESET_PC 0x00000200u
#define RAM_FRAME 0x00ff0000u
#define RAM_LINE 0x00ff0002u
#define VDP_DATA 0x00c00000u
#define VDP_CONTROL 0x00c00004u
#define PSG_PORT 0x00c00011u
#define YM_ADDRESS_0 0x00a04000u
#define YM_DATA_0 0x00a04001u
#define Z80_BUS_REQUEST 0x00a11100u
#define Z80_RESET 0x00a11200u
#define TMSS 0x00a14000u

struct rom_builder
{
  uint8_t *rom;
  size_t pc;
  int failed;
};

static void put_u16(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)(value >> 8);
  destination[1] = (uint8_t)value;
}

static void put_u32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)(value >> 24);
  destination[1] = (uint8_t)(value >> 16);
  destination[2] = (uint8_t)(value >> 8);
  destination[3] = (uint8_t)value;
}

static void emit_u16(struct rom_builder *builder, uint16_t value)
{
  if (builder->pc + 2u > AYTHER_GENERATED_ROM_SIZE)
  {
    builder->failed = 1;
    return;
  }
  put_u16(builder->rom + builder->pc, value);
  builder->pc += 2u;
}

static void emit_u32(struct rom_builder *builder, uint32_t value)
{
  emit_u16(builder, (uint16_t)(value >> 16));
  emit_u16(builder, (uint16_t)value);
}

static void emit_move_word_immediate_absolute(struct rom_builder *builder,
                                               uint16_t value,
                                               uint32_t address)
{
  emit_u16(builder, 0x33fcu);
  emit_u16(builder, value);
  emit_u32(builder, address);
}

static void emit_move_byte_immediate_absolute(struct rom_builder *builder,
                                               uint8_t value,
                                               uint32_t address)
{
  emit_u16(builder, 0x13fcu);
  emit_u16(builder, value);
  emit_u32(builder, address);
}

static void emit_move_long_immediate_absolute(struct rom_builder *builder,
                                               uint32_t value,
                                               uint32_t address)
{
  emit_u16(builder, 0x23fcu);
  emit_u32(builder, value);
  emit_u32(builder, address);
}

static void emit_move_word_absolute_d0(struct rom_builder *builder,
                                       uint32_t address)
{
  emit_u16(builder, 0x3039u);
  emit_u32(builder, address);
}

static void emit_move_word_d0_absolute(struct rom_builder *builder,
                                       uint32_t address)
{
  emit_u16(builder, 0x33c0u);
  emit_u32(builder, address);
}

static void emit_addq_word_absolute(struct rom_builder *builder,
                                    uint32_t address)
{
  emit_u16(builder, 0x5279u);
  emit_u32(builder, address);
}

static uint32_t vram_write_command(uint16_t address)
{
  return 0x40000000u |
         ((uint32_t)(address & 0x3fffu) << 16) |
         ((uint32_t)(address & 0xc000u) >> 14);
}

static uint32_t cram_write_command(uint16_t address)
{
  return 0xc0000000u | ((uint32_t)(address & 0x007eu) << 16);
}

static void emit_vdp_register(struct rom_builder *builder,
                              unsigned int reg, unsigned int value)
{
  emit_move_word_immediate_absolute(builder,
                                    (uint16_t)(0x8000u | (reg << 8) | value),
                                    VDP_CONTROL);
}

static void emit_vdp_words(struct rom_builder *builder, uint16_t address,
                           const uint16_t *values, size_t count)
{
  size_t index;
  emit_move_long_immediate_absolute(builder, vram_write_command(address),
                                    VDP_CONTROL);
  for (index = 0; index < count; ++index)
    emit_move_word_immediate_absolute(builder, values[index], VDP_DATA);
}

static void emit_fixture_data(struct rom_builder *builder)
{
  static const uint16_t palette[16] =
  {
    0x0000u, 0x000eu, 0x00e0u, 0x0e00u,
    0x00eeu, 0x0e0eu, 0x0ee0u, 0x0eeeu,
    0x0008u, 0x0080u, 0x0800u, 0x0088u,
    0x0808u, 0x0880u, 0x0444u, 0x0aaau
  };
  uint16_t words[512];
  size_t index;

  emit_move_long_immediate_absolute(builder, cram_write_command(0),
                                    VDP_CONTROL);
  for (index = 0; index < 16u; ++index)
    emit_move_word_immediate_absolute(builder, palette[index], VDP_DATA);

  for (index = 0; index < 16u; ++index)
    words[index] = 0x1111u;
  for (index = 16u; index < 32u; ++index)
    words[index] = 0x2222u;
  emit_vdp_words(builder, 0x0020u, words, 32u);

  for (index = 0; index < 64u; ++index)
    words[index] = (uint16_t)(1u + (index & 1u));
  emit_vdp_words(builder, 0xc000u, words, 64u);
  emit_vdp_words(builder, 0xe000u, words, 64u);

  /* Line scroll table: one word per plane for all 224 visible lines. */
  for (index = 0; index < 224u; ++index)
  {
    words[index * 2u] = (uint16_t)(0u - (uint16_t)(index & 31u));
    words[index * 2u + 1u] = (uint16_t)(index & 31u);
  }
  emit_vdp_words(builder, 0xf000u, words, 448u);

  /* 24 linked 1x1 sprites on the same band deliberately exceed the normal
   * per-line sprite budget. Coordinates include the VDP's 128-pixel bias. */
  for (index = 0; index < 24u; ++index)
  {
    words[index * 4u] = 208u;
    words[index * 4u + 1u] =
      (uint16_t)((index + 1u < 24u) ? index + 1u : 0u);
    words[index * 4u + 2u] = (uint16_t)(1u + (index & 1u));
    words[index * 4u + 3u] =
      (uint16_t)(128u + ((index * 13u) % 320u));
  }
  emit_vdp_words(builder, 0xd800u, words, 96u);
}


/* #29/#35: el fixture original hace key-on (0x28 = 0xF0) pero NUNCA programa
 * los operadores: sin TL, sin attack rate y sin frecuencia, el YM2612 queda en
 * silencio. Medido: silenciar el FM entero no movia un solo sample, asi que no
 * habia forma de distinguir "el mute funciona" de "no habia nada que silenciar".
 *
 * Esto programa un canal con un patch minimo pero AUDIBLE. Algoritmo 7: los
 * cuatro operadores van directo a la salida, que es la configuracion mas simple
 * en la que cualquier operador con TL bajo suena.
 *
 * Vive en un ROM APARTE (ayther_build_generated_rom_fm) y no toca el fixture de
 * siempre: mezclarlos habria movido los goldens de las dos plataformas sin
 * ninguna necesidad. */
static void emit_ym_write(struct rom_builder *builder, uint8_t reg, uint8_t value)
{
  emit_move_byte_immediate_absolute(builder, reg, YM_ADDRESS_0);
  emit_move_byte_immediate_absolute(builder, value, YM_DATA_0);
}

static void emit_fm_voice(struct rom_builder *builder, uint8_t channel,
                          uint8_t block_fnum_hi, uint8_t fnum_lo)
{
  uint8_t op;
  for (op = 0u; op < 4u; ++op)
  {
    uint8_t slot = (uint8_t)(channel + op * 4u);
    emit_ym_write(builder, (uint8_t)(0x30u + slot), 0x01u); /* DT=0 MUL=1     */
    emit_ym_write(builder, (uint8_t)(0x40u + slot), 0x00u); /* TL=0 (maximo)  */
    emit_ym_write(builder, (uint8_t)(0x50u + slot), 0x1fu); /* RS=0 AR=31     */
    emit_ym_write(builder, (uint8_t)(0x60u + slot), 0x00u); /* AM=0 D1R=0     */
    emit_ym_write(builder, (uint8_t)(0x70u + slot), 0x00u); /* D2R=0          */
    emit_ym_write(builder, (uint8_t)(0x80u + slot), 0x0fu); /* D1L=0 RR=15    */
    emit_ym_write(builder, (uint8_t)(0x90u + slot), 0x00u); /* SSG-EG off     */
  }
  /* Frecuencia: el orden importa, el alto se latchea al escribir el bajo. */
  emit_ym_write(builder, (uint8_t)(0xa4u + channel), block_fnum_hi);
  emit_ym_write(builder, (uint8_t)(0xa0u + channel), fnum_lo);
  emit_ym_write(builder, (uint8_t)(0xb0u + channel), 0x07u); /* FB=0 ALG=7    */
  emit_ym_write(builder, (uint8_t)(0xb4u + channel), 0xc0u); /* pan L+R       */
  emit_ym_write(builder, 0x28u, (uint8_t)(0xf0u | channel)); /* key-on 4 ops  */
}

static void emit_reset_program(struct rom_builder *builder)
{
  unsigned int index;

  emit_u16(builder, 0x46fcu); /* move.w #$2700,sr */
  emit_u16(builder, 0x2700u);
  emit_move_long_immediate_absolute(builder, 0x53454741u, TMSS); /* SEGA */
  emit_move_word_immediate_absolute(builder, 0x0100u, Z80_BUS_REQUEST);
  emit_move_word_immediate_absolute(builder, 0x0100u, Z80_RESET);
  emit_move_word_immediate_absolute(builder, 0u, RAM_FRAME);
  emit_move_word_immediate_absolute(builder, 0u, RAM_LINE);

  emit_vdp_register(builder, 0, 0x04u);
  emit_vdp_register(builder, 1, 0x14u);
  emit_vdp_register(builder, 2, 0x30u);
  emit_vdp_register(builder, 3, 0x3eu);
  emit_vdp_register(builder, 4, 0x07u);
  emit_vdp_register(builder, 5, 0x6cu);
  emit_vdp_register(builder, 7, 0x00u);
  emit_vdp_register(builder, 10, 0x00u);
  emit_vdp_register(builder, 11, 0x03u);
  emit_vdp_register(builder, 12, 0x81u);
  emit_vdp_register(builder, 13, 0x3cu);
  emit_vdp_register(builder, 15, 0x02u);
  emit_vdp_register(builder, 16, 0x01u);
  emit_vdp_register(builder, 17, 0x00u);
  emit_vdp_register(builder, 18, 0x00u);

  emit_fixture_data(builder);

  /* Program a simple YM2612 tone and enable the display/interrupts. */
  emit_move_byte_immediate_absolute(builder, 0x22u, YM_ADDRESS_0);
  emit_move_byte_immediate_absolute(builder, 0x00u, YM_DATA_0);
  emit_move_byte_immediate_absolute(builder, 0x28u, YM_ADDRESS_0);
  emit_move_byte_immediate_absolute(builder, 0xf0u, YM_DATA_0);
  for (index = 0; index < 8u; ++index)
    emit_move_byte_immediate_absolute(builder,
      (uint8_t)(0x90u | ((index * 3u) & 0x0fu)), PSG_PORT);
  emit_move_word_immediate_absolute(builder, 0x0000u, Z80_BUS_REQUEST);
  emit_vdp_register(builder, 0, 0x14u);
  emit_vdp_register(builder, 1, 0x74u);

  emit_u16(builder, 0x4e72u); /* stop #$2300 */
  emit_u16(builder, 0x2300u);
  emit_u16(builder, 0x60fau); /* bra.s back to stop */
}


/* Igual que emit_reset_program pero con tres canales de FM sonando a distinta
 * frecuencia. Tres y no uno: con un solo canal, "mutear el canal 1" y "mutear
 * todo" darian el mismo resultado, y el test no podria distinguir un mute por
 * canal de un mute global. */
static void emit_reset_program_fm(struct rom_builder *builder)
{
  unsigned int index;

  emit_move_word_immediate_absolute(builder, 0x0100u, Z80_BUS_REQUEST);
  emit_move_word_immediate_absolute(builder, 0x0100u, Z80_RESET);
  emit_move_long_immediate_absolute(builder, 0x53454741u, TMSS);
  emit_fixture_data(builder);

  emit_ym_write(builder, 0x22u, 0x00u); /* LFO off  */
  emit_ym_write(builder, 0x27u, 0x00u); /* ch3 normal */
  emit_ym_write(builder, 0x2bu, 0x00u); /* DAC off: que suene el FM, no el DAC */

  emit_fm_voice(builder, 0u, 0x22u, 0x69u);
  emit_fm_voice(builder, 1u, 0x24u, 0x1au);
  emit_fm_voice(builder, 2u, 0x26u, 0xa3u);

  for (index = 0; index < 8u; ++index)
    emit_move_byte_immediate_absolute(builder,
      (uint8_t)(0x9fu | ((index * 3u) & 0x0fu)), PSG_PORT); /* PSG en silencio */

  emit_move_word_immediate_absolute(builder, 0x0000u, Z80_BUS_REQUEST);
  emit_vdp_register(builder, 0, 0x14u);
  emit_vdp_register(builder, 1, 0x74u);

  emit_u16(builder, 0x4e72u); /* stop #$2300 */
  emit_u16(builder, 0x2300u);
  emit_u16(builder, 0x60fau);
}

static uint32_t emit_horizontal_handler(struct rom_builder *builder)
{
  uint32_t address = (uint32_t)builder->pc;
  emit_move_word_absolute_d0(builder, VDP_CONTROL); /* acknowledge */
  emit_move_long_immediate_absolute(builder, cram_write_command(2),
                                    VDP_CONTROL);
  emit_move_word_absolute_d0(builder, RAM_LINE);
  emit_move_word_d0_absolute(builder, VDP_DATA);

  /* Rewrite the first visible SAT entry once per scanline. Its eight-line
   * lifetime therefore exposes multiple identities for one sat_idx, matching
   * games that replace sprite attributes during active display. */
  emit_move_long_immediate_absolute(builder, vram_write_command(0xd804u),
                                    VDP_CONTROL);
  emit_move_word_absolute_d0(builder, RAM_LINE);
  emit_move_word_d0_absolute(builder, VDP_DATA);
  emit_addq_word_absolute(builder, RAM_LINE);
  emit_u16(builder, 0x4e73u); /* rte */
  return address;
}

static uint32_t emit_vertical_handler(struct rom_builder *builder)
{
  static const uint8_t psg_values[8] =
  {
    0x80u, 0x04u, 0x90u, 0xa0u, 0x08u, 0xb0u, 0xe4u, 0xf0u
  };
  uint32_t address = (uint32_t)builder->pc;
  unsigned int index;

  emit_move_word_absolute_d0(builder, VDP_CONTROL); /* acknowledge */
  emit_move_word_immediate_absolute(builder, 0u, RAM_LINE);
  emit_addq_word_absolute(builder, RAM_FRAME);

  /* Animate the first line-scroll word from the frame counter. */
  emit_move_long_immediate_absolute(builder, vram_write_command(0xf000u),
                                    VDP_CONTROL);
  emit_move_word_absolute_d0(builder, RAM_FRAME);
  emit_move_word_d0_absolute(builder, VDP_DATA);

  for (index = 0; index < 8u; ++index)
    emit_move_byte_immediate_absolute(builder, psg_values[index], PSG_PORT);
  emit_move_byte_immediate_absolute(builder, 0x2au, YM_ADDRESS_0);
  emit_move_byte_immediate_absolute(builder, 0x80u, YM_DATA_0);
  emit_u16(builder, 0x4e73u); /* rte */
  return address;
}

static void write_header(uint8_t *rom)
{
  static const char console[] = "SEGA MEGA DRIVE ";
  static const char domestic[48] = "AYTHER GENERATED DETERMINISM FIXTURE";
  static const char international[48] = "AYTHER GENERATED DETERMINISM FIXTURE";
  static const char product[] = "GM AYTHER-0001";

  memcpy(rom + 0x100u, console, sizeof(console));
  memcpy(rom + 0x120u, domestic, sizeof(domestic));
  memcpy(rom + 0x150u, international, sizeof(international));
  memcpy(rom + 0x180u, product, sizeof(product));
  memcpy(rom + 0x1f0u, "JUE             ", 16u);
  put_u32(rom + 0x1a0u, 0u);
  put_u32(rom + 0x1a4u, AYTHER_GENERATED_ROM_SIZE - 1u);
  put_u32(rom + 0x1a8u, 0x00ff0000u);
  put_u32(rom + 0x1acu, 0x00ffffffu);
}

static void write_checksum(uint8_t *rom)
{
  uint32_t checksum = 0;
  size_t offset;
  for (offset = 0x200u; offset < AYTHER_GENERATED_ROM_SIZE; offset += 2u)
    checksum += ((uint32_t)rom[offset] << 8) | rom[offset + 1u];
  put_u16(rom + 0x18eu, (uint16_t)checksum);
}

/* El armado es identico para los dos ROMs; lo unico que cambia es el programa
   de reset. Parametrizarlo evita que las dos copias se separen con el tiempo,
   que es como el fixture original termino con un key-on sin operadores. */
static size_t build_rom(uint8_t *rom, size_t capacity,
                        void (*emit_reset)(struct rom_builder *))
{
  struct rom_builder builder;
  uint32_t horizontal_handler;
  uint32_t vertical_handler;
  uint32_t default_handler;
  size_t vector;

  if (!rom || capacity < AYTHER_GENERATED_ROM_SIZE)
    return 0;

  memset(rom, 0, AYTHER_GENERATED_ROM_SIZE);
  write_header(rom);
  builder.rom = rom;
  builder.pc = RESET_PC;
  builder.failed = 0;

  emit_reset(&builder);
  horizontal_handler = emit_horizontal_handler(&builder);
  vertical_handler = emit_vertical_handler(&builder);
  default_handler = (uint32_t)builder.pc;
  emit_u16(&builder, 0x4e73u); /* rte */
  if (builder.failed)
    return 0;

  put_u32(rom, 0x00ffff00u);
  for (vector = 1u; vector < 64u; ++vector)
    put_u32(rom + vector * 4u, default_handler);
  put_u32(rom + 4u, RESET_PC);
  put_u32(rom + 28u * 4u, horizontal_handler);
  put_u32(rom + 30u * 4u, vertical_handler);
  write_checksum(rom);
  return AYTHER_GENERATED_ROM_SIZE;
}

size_t ayther_build_generated_rom(uint8_t *rom, size_t capacity)
{
  return build_rom(rom, capacity, emit_reset_program);
}

size_t ayther_build_generated_rom_fm(uint8_t *rom, size_t capacity)
{
  return build_rom(rom, capacity, emit_reset_program_fm);
}
