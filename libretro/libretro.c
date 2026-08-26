/****************************************************************************
 *  libretro.c
 *
 *  Genesis Plus GX libretro port
 *
 *  Copyright Eke-Eke (2007-2022)
 *
 *  Copyright Daniel De Matteis (2012-2016)
 *
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************/

#ifndef _MSC_VER
#include <stdbool.h>
#endif
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#ifdef _XBOX1
#include <xtl.h>
#endif

#define RETRO_DEVICE_MDPAD_3B             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_MDPAD_6B             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)
#define RETRO_DEVICE_MSPAD_2B             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 2)
#define RETRO_DEVICE_MDPAD_3B_WAYPLAY     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 3)
#define RETRO_DEVICE_MDPAD_6B_WAYPLAY     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 4)
#define RETRO_DEVICE_MDPAD_3B_TEAMPLAYER  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 5)
#define RETRO_DEVICE_MDPAD_6B_TEAMPLAYER  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 6)
#define RETRO_DEVICE_MSPAD_2B_MASTERTAP   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 7)
#define RETRO_DEVICE_PADDLE               RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_SPORTSPAD            RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_XE_1AP               RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_PHASER               RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define RETRO_DEVICE_MENACER              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1)
#define RETRO_DEVICE_JUSTIFIERS           RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 2)
#define RETRO_DEVICE_GRAPHIC_BOARD        RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_POINTER, 0)

#include <libretro.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <compat/strl.h>

#include "libretro_core_options.h"

#define AYTHER_CORE_EXPORTS 1
#include "shared.h"
#include "ayther/ayther_api.h"
#ifdef AYTHER_EXTENSIONS
#include "ayther/ayther_runtime.h"
#include "ayther/ayther_metrics.h"
#endif
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "osd.h"

#define STATIC_ASSERT(name, test) typedef struct { int assert_[(test)?1:-1]; } assert_ ## name ## _
#define M68K_MAX_CYCLES 1107
#define Z80_MAX_CYCLES 345
#define OVERCLOCK_FRAME_DELAY 100

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifdef M68K_OVERCLOCK_SHIFT
#ifndef HAVE_OVERCLOCK
#define HAVE_OVERCLOCK
#endif
STATIC_ASSERT(m68k_overflow,
              M68K_MAX_CYCLES <= UINT_MAX >> (M68K_OVERCLOCK_SHIFT + 1));
#endif

#ifdef Z80_OVERCLOCK_SHIFT
#ifndef HAVE_OVERCLOCK
#define HAVE_OVERCLOCK
#endif
STATIC_ASSERT(z80_overflow,
              Z80_MAX_CYCLES <= UINT_MAX >> (Z80_OVERCLOCK_SHIFT + 1));
#endif

#ifdef AYTHER_EXTENSIONS
STATIC_ASSERT(ayther_sprite_v1_size, sizeof(ayther_sprite_v1) == 10);
STATIC_ASSERT(ayther_sprite_internal_size, sizeof(AytherSpr) == sizeof(ayther_sprite_v1));
STATIC_ASSERT(ayther_sprite_sat_idx_offset, offsetof(AytherSpr, sat_idx) == 8);
STATIC_ASSERT(ayther_audio_write_v1_size, sizeof(ayther_audio_write_v1) == 8);
STATIC_ASSERT(ayther_audio_write_internal_size,
              sizeof(AytherAudioWrite) == sizeof(ayther_audio_write_v1));
STATIC_ASSERT(ayther_audio_write_data_offset,
              offsetof(AytherAudioWrite, data) == 6);
STATIC_ASSERT(ayther_region_info_v1_size, sizeof(ayther_region_info_v1) == 32);
STATIC_ASSERT(ayther_frame_snapshot_v1_size, sizeof(ayther_frame_snapshot_v1) == 48);
#endif

t_config config;

sms_ntsc_t *sms_ntsc = NULL;
md_ntsc_t  *md_ntsc = NULL;

char GG_ROM[256];
char AR_ROM[256];
char SK_ROM[256];
char SK_UPMEM[256];
char MD_BIOS[256];
char GG_BIOS[256];
char MS_BIOS_EU[256];
char MS_BIOS_JP[256];
char MS_BIOS_US[256];
char CD_BIOS_EU[256];
char CD_BIOS_US[256];
char CD_BIOS_JP[256];
char CD_BRAM_JP[256];
char CD_BRAM_US[256];
char CD_BRAM_EU[256];
char CART_BRAM[256];

static int vwidth;
static int vheight;
static int vwoffset;
static int bmdoffset;
static unsigned int max_width;
static unsigned int max_height;
static double vaspect_ratio;
static double retro_fps;

static uint32_t brm_crc[2];
static uint8_t brm_format[0x40] =
{
  0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
  0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f
};
uint8_t cart_size;

static bool is_running = 0;
#ifdef AYTHER_EXTENSIONS
static bool ayther_content_loaded = false;
static bool ayther_frame_active = false;

/* #30: el delta era consume-on-poll -- leerlo lo vaciaba--, asi que solo servia
   para UN consumidor por frame. Con el Lab de debug y el motor HD leyendo el
   mismo frame, el segundo recibia un bitmap vacio y no invalidaba sus assets.
   No es un caso raro: es el uso previsto.

   Doble buffer: lo que se acumula durante el frame se CONGELA al terminarlo, y
   la lectura devuelve el congelado sin tocarlo. Leerlo N veces da lo mismo N
   veces. Ademas cada frame entra en un ring, para que `frame_delta_since`
   pueda contestar "que cambio desde la generacion X". */
static uint8_t ayther_delta_frozen[2048];
static uint8_t ayther_delta_ring[AYTHER_FRAME_DELTA_HISTORY][2048];
static uint64_t ayther_delta_ring_gen[AYTHER_FRAME_DELTA_HISTORY];
static bool ayther_delta_ring_valid[AYTHER_FRAME_DELTA_HISTORY];

static void ayther_delta_freeze(uint64_t completed_generation)
{
   size_t slot = (size_t)(completed_generation % AYTHER_FRAME_DELTA_HISTORY);
   memcpy(ayther_delta_frozen, ayther_vram_dirty, sizeof(ayther_delta_frozen));
   memcpy(ayther_delta_ring[slot], ayther_vram_dirty, sizeof(ayther_delta_frozen));
   ayther_delta_ring_gen[slot] = completed_generation;
   ayther_delta_ring_valid[slot] = true;
   memset(ayther_vram_dirty, 0, sizeof(ayther_vram_dirty));
}
static uint64_t ayther_frame_generation = 0;
static uint64_t ayther_snapshot_generation = 1;
#endif
static bool restart_eq = false;
static uint8_t temp[0x10000];
static int16 soundbuffer[3068];
static uint16_t bitmap_data_[720 * 576];
static uint8_t reg0_prev = 0;

static char g_rom_dir[256];
static char g_rom_name[256];
static const void *g_rom_data = NULL;
static size_t g_rom_size      = 0;
static char *save_dir         = NULL;

#ifdef AYTHER_EXTENSIONS
static void ayther_clear_frame_capture(void)
{
   ayther_sprite_capture_begin_frame();
   ayther_audio_write_n = 0;
   ayther_audio_write_overflow = 0;
}

static void ayther_invalidate_snapshot(void)
{
   ayther_frame_active = false;
   ayther_clear_frame_capture();
   ayther_raster_dirty = 0;
   ayther_snapshot_generation++;

   /* #30: esto es lo que el consume-on-poll compraba y hay que conservar. Un
      unserialize puede cambiar la VRAM ENTERA sin que corra un solo frame, y
      vdp_ctrl.c marca todo sucio al cargar el contexto. Sin esto, el consumidor
      no se enteraria hasta el proximo end_frame y pintaria un frame con assets
      viejos. El congelado se actualiza sin esperar al fin de frame. */
   memcpy(ayther_delta_frozen, ayther_vram_dirty, sizeof(ayther_delta_frozen));
}

static void ayther_reset_session(bool content_loaded)
{
   ayther_content_loaded = content_loaded;
   ayther_frame_generation = 0;
   ayther_subscription_reset();
   ayther_invalidate_snapshot();
}

/* AYTHER (#39.B): descriptor del sistema emulado.

   Se rellena AL LEERLO y no una vez por frame, a proposito: son unos cuarenta
   bytes que nadie mira en la mayoria de los frames, y rellenarlos siempre seria
   exactamente el tipo de trabajo-en-idle que #36 saco de otros seis lugares.
   Entre frames -- que es cuando el frontend puede leer-- el estado del VDP es
   el del frame que acaba de terminar, que es la respuesta correcta. */
static ayther_system_v1 ayther_system_desc;

/* #42: las regiones por linea se entregan como CABECERA + ENTRADAS en un solo
   buffer contiguo. La cabecera va adentro y no aparte porque el consumidor
   necesita saber cuantas lineas trae y de que frame son EN EL MISMO read: dos
   lecturas separadas pueden caer a los dos lados de un frame. */
static uint8_t ayther_line_regs_buf[sizeof(ayther_line_header_v1) +
                                    AYTHER_LINE_MAX * sizeof(ayther_line_regs_v1)];
static uint8_t ayther_line_cram_buf[sizeof(ayther_line_header_v1) +
                                    AYTHER_LINE_MAX * 128];
static uint8_t ayther_line_cells_buf[sizeof(ayther_line_header_v1) +
                                     AYTHER_LINE_MAX * sizeof(ayther_line_cells_v1)];
static uint32_t ayther_line_cells_bytes;
static uint32_t ayther_line_regs_bytes;
static uint32_t ayther_line_cram_bytes;

static void ayther_fill_line_header(void *dst, uint32_t entry_size,
                                    uint32_t lines, uint32_t flags)
{
   ayther_line_header_v1 h;
   memset(&h, 0, sizeof(h));
   h.struct_size = (uint32_t)sizeof(h);
   h.entry_size = entry_size;
   h.lines = lines;
   h.flags = flags;
   h.frame_generation = ayther_frame_generation;
   memcpy(dst, &h, sizeof(h));
}

static void ayther_fill_line_regs(void)
{
   uint32_t lines = ayther_line_count;
   if (lines > AYTHER_LINE_MAX) lines = AYTHER_LINE_MAX;
   ayther_fill_line_header(ayther_line_regs_buf,
                           (uint32_t)sizeof(ayther_line_regs_v1),
                           lines, ayther_line_flags);
   memcpy(ayther_line_regs_buf + sizeof(ayther_line_header_v1),
          ayther_line_regs, lines * sizeof(ayther_line_regs_v1));
   ayther_line_regs_bytes = (uint32_t)(sizeof(ayther_line_header_v1) +
                                       lines * sizeof(ayther_line_regs_v1));
}

static void ayther_fill_line_cells(void)
{
   uint32_t lines = ayther_line_count;
   if (lines > AYTHER_LINE_MAX) lines = AYTHER_LINE_MAX;
   ayther_fill_line_header(ayther_line_cells_buf,
                           (uint32_t)sizeof(ayther_line_cells_v1),
                           lines, ayther_line_flags);
   memcpy(ayther_line_cells_buf + sizeof(ayther_line_header_v1),
          ayther_line_cells, lines * sizeof(ayther_line_cells_v1));
   ayther_line_cells_bytes = (uint32_t)(sizeof(ayther_line_header_v1) +
                                        lines * sizeof(ayther_line_cells_v1));
}

static void ayther_fill_line_cram(void)
{
   /* Un frame sin writes de paleta a mitad de camino entrega UNA entrada y el
      flag CRAM_UNIFORM, no 240 copias iguales: 128 B contra 30 KB. */
   uint32_t lines = ayther_line_count;
   uint32_t flags = ayther_line_flags;
   if (lines > AYTHER_LINE_MAX) lines = AYTHER_LINE_MAX;
   if (ayther_line_flags & AYTHER_LINES_CRAM_UNIFORM)
      lines = lines ? 1u : 0u;
   ayther_fill_line_header(ayther_line_cram_buf, 128u, lines, flags);
   memcpy(ayther_line_cram_buf + sizeof(ayther_line_header_v1),
          ayther_line_cram, lines * 128u);
   ayther_line_cram_bytes = (uint32_t)(sizeof(ayther_line_header_v1) + lines * 128u);
}

static void ayther_fill_system(void)
{
   memset(&ayther_system_desc, 0, sizeof(ayther_system_desc));
   ayther_system_desc.struct_size = (uint32_t)sizeof(ayther_system_desc);
   ayther_system_desc.layout_version = AYTHER_LAYOUT_SYSTEM_V1;

   ayther_system_desc.system_hw = system_hw;
   ayther_system_desc.region_pal = vdp_pal ? 1 : 0;
   /* El VDP no elige modo hasta que el programa escribe reg 1; hasta entonces
      decir "modo 4" seria inventar. 0 significa "todavia no". */
   ayther_system_desc.vdp_mode = (reg[1] & 0x04) ? 5
                               : ((system_hw & SYSTEM_PBC) == SYSTEM_MD ? 0 : 4);
   ayther_system_desc.interlace = (uint8_t)(im2_flag ? 2 : (interlaced ? 1 : 0));
   /* 1.10: del frame EMITIDO, como viewport_w. reg 12 bit 0 vive en VDP_REGS y
      puede ir un frame adelante; el flag de abajo dice cuando. */
   ayther_system_desc.h40 = (bitmap.viewport.w >= 320) ? 1 : 0;
   ayther_system_desc.shadow_highlight = (reg[12] & 0x08) ? 1 : 0;
   ayther_system_desc.lines_per_frame = (uint16_t)lines_per_frame;

   /* #40: el descriptor promete el frame EMITIDO, y en Game Gear no coincide
      con el area interna del VDP. El core recorta la GG con offsets NEGATIVOS
      (`viewport.x = -48`, `y = -24`) dejando `viewport.w/h` en 256x192,
      mientras el frontend recibe 160x144.

      Copiar los campos tal cual rompia dos veces: el tamano mentia, y como
      `viewport_x` es unsigned el -48 se envolvia a 65488, asi que el consumidor
      ni siquiera podia deducir el recorte. Se publica lo que promete el
      contrato: w/h del frame emitido, y x/y como el offset POSITIVO donde ese
      frame empieza dentro del area interna, que es el dato que hace falta para
      volver a mapear. En Mega Drive los offsets son 0 y nada cambia. */
   ayther_system_desc.viewport_x = (uint16_t)((bitmap.viewport.x < 0)
                                              ? -bitmap.viewport.x : 0);
   ayther_system_desc.viewport_y = (uint16_t)((bitmap.viewport.y < 0)
                                              ? -bitmap.viewport.y : 0);
   ayther_system_desc.viewport_w = (uint16_t)(bitmap.viewport.w +
                                              2 * bitmap.viewport.x);
   ayther_system_desc.viewport_h = (uint16_t)(bitmap.viewport.h +
                                              2 * bitmap.viewport.y);
   /* 1.10: `changed & 2` es exactamente "hubo un cambio de geometria en los
      registros que el proximo frame aplica" (system_frame_gen lo consume al
      arrancar). Entre frames, si esta puesto, viewport y registros hablan de
      frames distintos. */
   ayther_system_desc.flags = (bitmap.viewport.changed & 2)
                            ? AYTHER_SYSTEM_GEOMETRY_PENDING : 0;

   ayther_system_desc.master_clock = system_clock;
   ayther_system_desc.cpu_clock = ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
      ? (system_clock / 7) : (system_clock / 15);

#if defined(HAVE_YM3438_CORE)
   ayther_system_desc.fm_core = (config.ym2612 >= YM2612_ENHANCED) ? 1 : 0;
#else
   ayther_system_desc.fm_core = 0;
#endif
   ayther_system_desc.psg_present = 1;
   ayther_system_desc.pcm_present = (system_hw == SYSTEM_MCD) ? 1 : 0;

   ayther_system_desc.rom_crc32 = (uint32_t)rominfo.crc32;
   ayther_system_desc.rom_bytes = (uint32_t)cart.romsize;
}

static ayther_journal_v1    ayther_journal_desc;
static ayther_frame_hash_v1 ayther_frame_hash_desc;

/* #39.A: el journal se copia EN LA CONSULTA, igual que el descriptor de
   sistema. Copiarlo una vez por frame seria trabajo que casi siempre nadie
   lee; la region es de solo lectura y el journal no se mueve entre el fin
   del frame y la consulta. */
static void ayther_fill_journal(void)
{
   int i;
   int n = ayther_raster_journal_count;
   if (n < 0) n = 0;
   if (n > AYTHER_JOURNAL_MAX_EVENTS) n = AYTHER_JOURNAL_MAX_EVENTS;

   memset(&ayther_journal_desc, 0, sizeof(ayther_journal_desc));
   ayther_journal_desc.layout_version = AYTHER_LAYOUT_JOURNAL_V1;
   ayther_journal_desc.struct_size = (uint32_t)sizeof(ayther_journal_desc);
   ayther_journal_desc.count = (uint32_t)n;
   ayther_journal_desc.dropped = (ayther_raster_journal_dropped > 0)
      ? (uint32_t)ayther_raster_journal_dropped : 0u;
   for (i = 0; i < n; ++i)
   {
      ayther_journal_desc.events[i].v_counter = ayther_raster_journal[i].v_counter;
      ayther_journal_desc.events[i].reason    = ayther_raster_journal[i].reason;
      ayther_journal_desc.events[i].address   = ayther_raster_journal[i].address;
      ayther_journal_desc.events[i].data      = ayther_raster_journal[i].data;
   }
}

/* #39.D: FNV-1a de 64 bits, el mismo que usa full_core_replay, para que el
   frontend pueda recalcular y comparar en vez de tener que confiar. */
#define AYTHER_FNV_OFFSET UINT64_C(1469598103934665603)
#define AYTHER_FNV_PRIME  UINT64_C(1099511628211)

static uint64_t ayther_hash_bytes(uint64_t h, const void *p, size_t n)
{
   const uint8_t *b = (const uint8_t *)p;
   size_t i;
   for (i = 0; i < n; ++i)
   {
      h ^= (uint64_t)b[i];
      h *= AYTHER_FNV_PRIME;
   }
   return h;
}

/* Se calcula al CERRAR el frame, que es el unico momento en que el video y
   el audio del frame estan los dos completos. Solo bajo suscripcion: son
   ~100 KB recorridos, y cobrarselos a quien no los pidio contradice la
   promesa del fork. */
static void ayther_update_frame_hash(const int16 *samples, unsigned frames)
{
   uint64_t video = AYTHER_FNV_OFFSET;
   const int rw = vwidth - vwoffset;
   int y;

   if (!AYTHER_SUBSCRIBED(AYTHER_SUB_FRAME_HASH))
      return;

   /* Exactamente el frame que el frontend recibe: el mismo puntero, el mismo
      ancho y el mismo pitch que el video_cb de mas abajo, y fila por fila
      -- el pitch cubre 720 px y el frame ocupa menos, asi que hashear de un
      saque meteria relleno que nadie ve-. Es la misma cuenta que hace
      `full_core_replay` desde afuera, y por eso los dos numeros se pueden
      comparar: si no coinciden, uno de los dos esta mirando otra cosa. */
   if (bitmap.data && rw > 0 && vheight > 0)
   {
      const uint8 *base = bitmap.data + bmdoffset;
      for (y = 0; y < vheight; ++y)
         video = ayther_hash_bytes(video,
                                   base + (size_t)y * (size_t)bitmap.pitch,
                                   (size_t)rw * sizeof(uint16_t));
   }

   ayther_frame_hash_desc.layout_version = AYTHER_LAYOUT_FRAME_HASH_V1;
   ayther_frame_hash_desc.struct_size =
      (uint32_t)sizeof(ayther_frame_hash_desc);
   ayther_frame_hash_desc.frame_index = ayther_frame_generation;
   ayther_frame_hash_desc.video_hash = video;
   ayther_frame_hash_desc.audio_hash = (samples && frames)
      ? ayther_hash_bytes(AYTHER_FNV_OFFSET, samples,
                          (size_t)frames * 2u * sizeof(int16))
      : AYTHER_FNV_OFFSET;
   ayther_frame_hash_desc.vram_hash  = ayther_hash_bytes(AYTHER_FNV_OFFSET, vram, 0x10000);
   ayther_frame_hash_desc.cram_hash  = ayther_hash_bytes(AYTHER_FNV_OFFSET, cram, 0x80);
   ayther_frame_hash_desc.vsram_hash = ayther_hash_bytes(AYTHER_FNV_OFFSET, vsram, 0x80);
}

/* #36: las suscripciones que gatean el bitmap de VRAM sucia. Quien lee el
   frame delta esta leyendo, por definicion, que cambio en la memoria del VDP. */
#define AYTHER_SUB_DELTA_PRODUCERS \
   (AYTHER_SUB_VDP_MEMORY | AYTHER_SUB_RASTER_TRACKING)

static void ayther_begin_frame(void)
{
   uint32_t before = ayther_subscription_active_mask;

   if (ayther_subscription_begin_frame())
      ayther_snapshot_generation++;

   /* #36: el bitmap de patterns sucios deja de acumularse sin subscribers, asi
      que al ACTIVAR la suscripcion hay que darle al recien llegado todo lo que
      no vio. Todo sucio es la unica respuesta correcta: el consumidor necesita
      un SUPERCONJUNTO de lo que cambio, y "no se" no es un valor que el bitmap
      pueda expresar. Es la misma politica que load y reset ya aplicaban. */
   if (!(before & AYTHER_SUB_DELTA_PRODUCERS) &&
       (ayther_subscription_active_mask & AYTHER_SUB_DELTA_PRODUCERS))
   {
      memset(ayther_vram_dirty, 0xFF, sizeof(ayther_vram_dirty));
   }

   ayther_clear_frame_capture();
   ayther_frame_active = true;
   AYTHER_METRIC_INC(begin_frame_calls);
}

static void ayther_end_frame(const int16 *samples, unsigned frames)
{
   ayther_frame_active = false;
   /* #39.D: antes de mover la generacion -- el hash es de ESTE frame. */
   ayther_update_frame_hash(samples, frames);
   /* Se congela ANTES de incrementar: el bitmap pertenece al frame que acaba
      de correr, no al que viene. */
   ayther_delta_freeze(ayther_frame_generation);
   ayther_frame_generation++;
   ayther_snapshot_generation++;
}
#else
#define ayther_reset_session(content_loaded) ((void)(content_loaded))
#define ayther_invalidate_snapshot() ((void)0)
#define ayther_begin_frame() ((void)0)
#define ayther_end_frame(samples, frames) ((void)(samples), (void)(frames))
#endif

retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_cb;

enum RetroLightgunInputModes{RetroLightgun, RetroPointer};
static enum RetroLightgunInputModes retro_gun_mode = RetroLightgun;

/* Cheat Support */
#define MAX_CHEATS (150)

typedef struct
{
 bool enable;
 uint16_t data;
 uint16_t old;
 uint32_t address;
 uint8_t *prev;
} CHEATENTRY;

static int maxcheats = 0;
static int maxROMcheats = 0;
static int maxRAMcheats = 0;

static CHEATENTRY cheatlist[MAX_CHEATS];
static uint8_t cheatIndexes[MAX_CHEATS];

static char ggvalidchars[] = "ABCDEFGHJKLMNPRSTVWXYZ0123456789";

static char arvalidchars[] = "0123456789ABCDEF";

/* Some games appear to calibrate music playback speed for PAL/NTSC by
   actually counting CPU cycles per frame during startup, resulting in
   hilariously fast music.  Delay overclocking for a while as a
   workaround */
#ifdef HAVE_OVERCLOCK
static uint32_t overclock_delay;
#endif

static bool libretro_supports_option_categories = false;
static bool libretro_supports_bitmasks          = false;

#define SOUND_FREQUENCY 44100

/*EQ settings*/
#define HAVE_EQ

/* Frameskipping Support */

static unsigned frameskip_type             = 0;
static unsigned frameskip_threshold        = 0;
static uint16_t frameskip_counter          = 0;

static bool retro_audio_buff_active        = false;
static unsigned retro_audio_buff_occupancy = 0;
static bool retro_audio_buff_underrun      = false;
/* Maximum number of consecutive frames that
 * can be skipped */
#define FRAMESKIP_MAX 60

static unsigned audio_latency              = 0;
static bool update_audio_latency           = false;

#ifdef USE_PER_SOUND_CHANNELS_CONFIG
static bool show_advanced_av_settings      = true;
#endif

static void retro_audio_buff_status_cb(
      bool active, unsigned occupancy, bool underrun_likely)
{
   retro_audio_buff_active    = active;
   retro_audio_buff_occupancy = occupancy;
   retro_audio_buff_underrun  = underrun_likely;
}

/* LED interface */
static retro_set_led_state_t led_state_cb = NULL;
static unsigned int retro_led_state[2] = {0};

static void retro_led_interface(void)
{
   /* 0: Power
    * 1: CD */

   unsigned int led_state[2] = {0};
   unsigned int l            = 0;

   led_state[0] = (zstate) ? 1 : 0;
   led_state[1] = (scd.regs[0x06 >> 1].byte.h & 1) ? 1 : 0;

   for (l = 0; l < sizeof(led_state)/sizeof(led_state[0]); l++)
   {
      if (retro_led_state[l] != led_state[l])
      {
         retro_led_state[l] = led_state[l];
         led_state_cb(l, led_state[l]);
      }
   }
}

static void init_frameskip(void)
{
   if (frameskip_type > 0)
   {
      struct retro_audio_buffer_status_callback buf_status_cb;

      buf_status_cb.callback = retro_audio_buff_status_cb;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,
            &buf_status_cb))
      {
         if (log_cb)
            log_cb(RETRO_LOG_WARN, "Frameskip disabled - frontend does not support audio buffer status monitoring.\n");

         retro_audio_buff_active    = false;
         retro_audio_buff_occupancy = 0;
         retro_audio_buff_underrun  = false;
         audio_latency              = 0;
      }
      else
      {
         /* Frameskip is enabled - increase frontend
          * audio latency to minimise potential
          * buffer underruns */
         float frames_per_sec;
         float frame_time_msec;

         /* While system_clock and lines_per_frame will
          * always be valid when this function is called,
          * it is not guaranteed. Just add a fallback to
          * prevent any possible divide-by-zero errors */
         if ((system_clock <= 0) || (lines_per_frame <= 0))
            frames_per_sec = 60.0f;
         else
            frames_per_sec = (float)system_clock / (float)lines_per_frame / (float)MCYCLES_PER_LINE;

         frame_time_msec = 1000.0f / frames_per_sec;

         /* Set latency to 6x current frame time... */
         audio_latency = (unsigned)((6.0f * frame_time_msec) + 0.5f);

         /* ...then round up to nearest multiple of 32 */
         audio_latency = (audio_latency + 0x1F) & ~0x1F;
      }
   }
   else
   {
      environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, NULL);
      audio_latency = 0;
   }

   update_audio_latency = true;
}

/************************************
 * Genesis Plus GX implementation
 ************************************/
#undef  CHUNKSIZE
#define CHUNKSIZE   (0x10000)

void error(char * fmt, ...)
{
   char buffer[256];
   va_list ap;
   va_start(ap, fmt);
   vsprintf(buffer, fmt, ap);
   if (log_cb)
      log_cb(RETRO_LOG_ERROR, "%s\n", buffer);
   va_end(ap);
}

static void show_rom_size_error_msg(void)
{
   unsigned msg_interface_version = 0;
   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION,
         &msg_interface_version);

   if (msg_interface_version >= 1)
   {
      struct retro_message_ext msg = {
         "ROM size exceeds maximum permitted value",
         3000,
         3,
         RETRO_LOG_ERROR,
         RETRO_MESSAGE_TARGET_ALL,
         RETRO_MESSAGE_TYPE_NOTIFICATION,
         -1
      };
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
   }
   else
   {
      struct retro_message msg = {
         "ROM size exceeds maximum permitted value",
         180
      };
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
   }
}

int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension)
{
  int64_t left = 0;
  int64_t size = 0;
  RFILE *fd;

  /* Get filename extension */
  if (extension)
  {
    memcpy(extension, &filename[strlen(filename) - 3], 3);
    extension[3] = 0;
  }

  /* Check if this was called to load ROM file from the frontend (not BOOT ROM or Lock-On ROM files from the core) */
  if (maxsize >= 0x800000)
  {
    /* Check if loaded game is already in memory */
    if ((g_rom_data != NULL) && (g_rom_size > 0))
    {
      size = g_rom_size;
      if (size > maxsize)
      {
        /* ROM exceeds maximum allowed size
         * - Notify user and return an error */
        show_rom_size_error_msg();
        return 0;
      }
      memcpy(buffer, g_rom_data, size);
      return size;
    }
  }

  /* Open file */
  fd    = filestream_open(filename, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);

  if (!fd)
  {
    /* Master System & Game Gear BIOS are optional files */
    if (!strcmp(filename,MS_BIOS_US) || !strcmp(filename,MS_BIOS_EU) || !strcmp(filename,MS_BIOS_JP) || !strcmp(filename,GG_BIOS))
    {
      return 0;
    }
  
    /* Mega CD BIOS are required files */
    if (!strcmp(filename,CD_BIOS_US) || !strcmp(filename,CD_BIOS_EU) || !strcmp(filename,CD_BIOS_JP)) 
    {
       if (log_cb)
          log_cb(RETRO_LOG_ERROR, "Unable to open CD BIOS: \"%s\".\n", filename);
       return 0;
    }

    if (log_cb)
       log_cb(RETRO_LOG_ERROR, "Unable to open file.\n");
    return 0;
  }

  /* Get file size */
  filestream_seek(fd, 0, SEEK_END);
  size = filestream_tell(fd);

  /* size limit */
  if (size > MAXROMSIZE)
  {
    /* ROM exceeds maximum allowed size
     * - Notify user and return an error */
    filestream_close(fd);
    show_rom_size_error_msg();
    return 0;
  }
  else if (size > maxsize)
    size = maxsize;

  if (log_cb)
    log_cb(RETRO_LOG_INFO, "Loading %d bytes ...\n", size);

  /* Read into buffer */
  left = size;
  filestream_seek(fd, 0, SEEK_SET);
  while (left > CHUNKSIZE)
  {
    filestream_read(fd, buffer, CHUNKSIZE);
    buffer += CHUNKSIZE;
    left -= CHUNKSIZE;
  }

  /* Read remaining bytes */
  filestream_read(fd, buffer, left);

  /* Close file */
  filestream_close(fd);

  /* Return loaded ROM size */
  return size;
}

static void RAMCheatUpdate(void);

static void osd_input_update_internal_bitmasks(void)
{
   int i, player = 0;
   unsigned int temp;
   int16_t ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);

   for (i = 0; i < MAX_INPUTS; i++)
   {
      temp = 0;
      switch (input.dev[i])
      {
         case DEVICE_PAD6B:
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_L))
               temp |= INPUT_X;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_X))
               temp |= INPUT_Y;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_R))
               temp |= INPUT_Z;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
               temp |= INPUT_MODE;

         case DEVICE_PAD3B:
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
               temp |= INPUT_A;

         case DEVICE_PAD2B:
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_B;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_A))
               temp |= INPUT_C;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
               temp |= INPUT_UP;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
               temp |= INPUT_DOWN;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
               temp |= INPUT_LEFT;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
               temp |= INPUT_RIGHT;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_MOUSE:
            input.analog[i][0] = input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
            if (config.invert_mouse)
               input.analog[i][1] = input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
            else
               input.analog[i][1] = -input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

            if (input.analog[i][0] < -255)
               input.analog[i][0] = -255;
            else if (input.analog[i][0] > 255)
               input.analog[i][0] = 255;
            if (input.analog[i][1] < -255)
               input.analog[i][1] = -255;
            else if (input.analog[i][1] > 255)
               input.analog[i][1] = 255;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_MOUSE_LEFT;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_MOUSE_RIGHT;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
               temp |= INPUT_MOUSE_CENTER;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_START;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_LIGHTGUN:
            if ( retro_gun_mode == RetroPointer )
            {
               int touch_count;
               input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * bitmap.viewport.w) / 0xfffe;
               input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * bitmap.viewport.h) / 0xfffe;
               if (input_state_cb(player, RETRO_DEVICE_POINTER, 0,
                        RETRO_DEVICE_ID_POINTER_PRESSED))
                  temp |= INPUT_A;
               touch_count = input_state_cb(player,
                     RETRO_DEVICE_POINTER, 0,
                     RETRO_DEVICE_ID_POINTER_COUNT);

               if (touch_count == 2)
                  temp |= INPUT_B;
               else if (touch_count == 3)
                  temp |= INPUT_START;
               else if (touch_count == 4)
                  temp |= INPUT_C;
            }
            else
            {
               /* RetroLightgun is default */
               if ( input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) )
               {
                  input.analog[i][0] = -1000;
                  input.analog[i][1] = -1000;
               }
               else
               {
                  input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X) + 0x7fff) * bitmap.viewport.w) / 0xfffe;
                  input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y) + 0x7fff) * bitmap.viewport.h) / 0xfffe;
               }

               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER))
                  temp |= INPUT_A;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A))
                  temp |= INPUT_B;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_B))
                  temp |= INPUT_C;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START))
                  temp |= INPUT_START;
            }     

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_PADDLE:
            input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;

            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_BUTTON1;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_SPORTSPAD:
            input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;
            input.analog[i][1] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000) >> 8;

            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_BUTTON1;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_A))
               temp |= INPUT_BUTTON2;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_PICO:
            input.analog[i][0] = 0x03c + ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * (0x17c-0x03c)) / 0xfffe;
            input.analog[i][1] = 0x1fc + ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * (0x2f7-0x1fc)) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_PICO_PEN;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_PICO_RED;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
               pico_current = (pico_current - 1) & 7;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
               pico_current = (pico_current + 1) & 7;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
               temp |= INPUT_UP;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
               temp |= INPUT_DOWN;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
               temp |= INPUT_LEFT;
            if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
               temp |= INPUT_RIGHT;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_TEREBI:
            input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * 250) / 0xfffe;
            input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * 250) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_BUTTON1;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_START;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_GRAPHIC_BOARD:
            input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * 255) / 0xfffe;
            input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * 255) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_GRAPHIC_PEN;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_GRAPHIC_DO;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_GRAPHIC_MENU;

            player++;
            ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            break;

         case DEVICE_XE_1AP:
            {
               int rx = input.analog[i][0] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
               int ry = input.analog[i][1] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
               if (abs(rx) > abs(ry))
                  input.analog[i+1][0] = (rx + 0x8000) >> 8;
               else 
                  input.analog[i+1][0] = (0x7fff - ry) >> 8;
               input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;
               input.analog[i][1] = (input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000) >> 8;

               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_R))
                  temp |= INPUT_XE_A;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2))
                  temp |= INPUT_XE_B;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_L))
                  temp |= INPUT_XE_C;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2))
                  temp |= INPUT_XE_D;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
                  temp |= INPUT_XE_E1;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_B))
                  temp |= INPUT_XE_E2;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
                  temp |= INPUT_XE_SELECT;
               if (ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))
                  temp |= INPUT_XE_START;

               player++;
               ret = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
               break;
            }

         default:
            break;
      }

      input.pad[i] = temp;
   }
}

static void osd_input_update_internal(void)
{
   int i, player = 0;
   unsigned int temp;

   for (i = 0; i < MAX_INPUTS; i++)
   {
      temp = 0;
      switch (input.dev[i])
      {
         case DEVICE_PAD6B:
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
               temp |= INPUT_X;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
               temp |= INPUT_Y;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
               temp |= INPUT_Z;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
               temp |= INPUT_MODE;

         case DEVICE_PAD3B:
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
               temp |= INPUT_A;

         case DEVICE_PAD2B:
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_B;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
               temp |= INPUT_C;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
               temp |= INPUT_UP;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
               temp |= INPUT_DOWN;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
               temp |= INPUT_LEFT;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
               temp |= INPUT_RIGHT;

            player++;
            break;

         case DEVICE_MOUSE:
            input.analog[i][0] = input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
            if (config.invert_mouse)
               input.analog[i][1] = input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
            else
               input.analog[i][1] = -input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

            if (input.analog[i][0] < -255)
               input.analog[i][0] = -255;
            else if (input.analog[i][0] > 255)
               input.analog[i][0] = 255;
            if (input.analog[i][1] < -255)
               input.analog[i][1] = -255;
            else if (input.analog[i][1] > 255)
               input.analog[i][1] = 255;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_MOUSE_LEFT;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_MOUSE_RIGHT;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
               temp |= INPUT_MOUSE_CENTER;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_START;

            player++;
            break;

         case DEVICE_LIGHTGUN:
            if ( retro_gun_mode == RetroPointer )
            {
               input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * bitmap.viewport.w) / 0xfffe;
               input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * bitmap.viewport.h) / 0xfffe;
               if (input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED))
                  temp |= INPUT_A;
            }
            else
            {
               /* RetroLightgun is default */
               if ( input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) )
               {
                  input.analog[i][0] = -1000;
                  input.analog[i][1] = -1000;
               }
               else
               {
                  input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X) + 0x7fff) * bitmap.viewport.w) / 0xfffe;
                  input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y) + 0x7fff) * bitmap.viewport.h) / 0xfffe;
               }

               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER))
                  temp |= INPUT_A;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A))
                  temp |= INPUT_B;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_B))
                  temp |= INPUT_C;
               if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START))
                  temp |= INPUT_START;
            }

            player++;
            break;

         case DEVICE_PADDLE:
            input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;

            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_BUTTON1;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;

            player++;
            break;

         case DEVICE_SPORTSPAD:
            input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;
            input.analog[i][1] = (input_state_cb(player, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000) >> 8;

            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
               temp |= INPUT_BUTTON1;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
               temp |= INPUT_BUTTON2;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
               temp |= INPUT_START;

            player++;
            break;

         case DEVICE_PICO:
            input.analog[i][0] = 0x03c + ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * (0x17c-0x03c)) / 0xfffe;
            input.analog[i][1] = 0x1fc + ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * (0x2f7-0x1fc)) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_PICO_PEN;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_PICO_RED;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
               pico_current = (pico_current - 1) & 7;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
               pico_current = (pico_current + 1) & 7;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
               temp |= INPUT_UP;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
               temp |= INPUT_DOWN;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
               temp |= INPUT_LEFT;
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
               temp |= INPUT_RIGHT;

            player++;
            break;

         case DEVICE_TEREBI:
            input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * 250) / 0xfffe;
            input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * 250) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_BUTTON1;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_START;

            player++;
            break;

         case DEVICE_GRAPHIC_BOARD:
            input.analog[i][0] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 0x7fff) * 255) / 0xfffe;
            input.analog[i][1] = ((input_state_cb(player, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 0x7fff) * 255) / 0xfffe;

            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
               temp |= INPUT_GRAPHIC_PEN;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
               temp |= INPUT_GRAPHIC_DO;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
               temp |= INPUT_GRAPHIC_MENU;

            player++;
            break;

         case DEVICE_XE_1AP:
            {
               int rx = input.analog[i][0] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
               int ry = input.analog[i][1] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
               if (abs(rx) > abs(ry))
                  input.analog[i+1][0] = (rx + 0x8000) >> 8;
               else 
                  input.analog[i+1][0] = (0x7fff - ry) >> 8;
               input.analog[i][0] = (input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) + 0x8000) >> 8;
               input.analog[i][1] = (input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) + 0x8000) >> 8;

               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
                  temp |= INPUT_XE_A;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))
                  temp |= INPUT_XE_B;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
                  temp |= INPUT_XE_C;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2))
                  temp |= INPUT_XE_D;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
                  temp |= INPUT_XE_E1;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
                  temp |= INPUT_XE_E2;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
                  temp |= INPUT_XE_SELECT;
               if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
                  temp |= INPUT_XE_START;

               player++;
               break;
            }

         default:
            break;
      }

      input.pad[i] = temp;
   }
}

void osd_input_update(void)
{
  input_poll_cb();

  /* Update RAM patches */
  RAMCheatUpdate();

  if (libretro_supports_bitmasks)
     osd_input_update_internal_bitmasks();
  else
     osd_input_update_internal();
}

static void draw_cursor(int16_t x, int16_t y, uint16_t color)
{
   int i;

   /* crosshair center position */   
   uint16_t *ptr = (uint16_t *)bitmap.data + ((bitmap.viewport.y + y) * bitmap.width) + x + bitmap.viewport.x;

   /* default crosshair dimension */
   int x_start = x - 3;
   int x_end  = x + 3;
   int y_start = y - 3;
   int y_end = y + 3;

   /* off-screen? */
   if ( x < 0 && y < 0 )
      return;

   /* framebuffer limits */
   if (x_start < -bitmap.viewport.x) x_start = -bitmap.viewport.x;
   if (x_end >= (bitmap.viewport.w + bitmap.viewport.x)) x_end = bitmap.viewport.w + bitmap.viewport.x - 1;
   if (y_start < -bitmap.viewport.y) y_start = -bitmap.viewport.y;
   if (y_end >= (bitmap.viewport.h + bitmap.viewport.y)) y_end = bitmap.viewport.h + bitmap.viewport.y - 1;

   /* draw crosshair */
   for (i = (x_start - x); i <= (x_end - x); i++)
      ptr[i] = (i & 1) ? color : 0xffff;
   for (i = (y_start - y); i <= (y_end - y); i++)
      ptr[i * bitmap.width] = (i & 1) ? color : 0xffff;
}

static void init_bitmap(void)
{
   memset(&bitmap, 0, sizeof(bitmap));
   bitmap.width      = 720;
   bitmap.height     = 576;
   bitmap.pitch      = 720 * 2;
   bitmap.data       = (uint8_t *)bitmap_data_;
}

static void config_default(void)
{
   int i;
   
   /* sound options */
   config.psg_preamp     = 150;
   config.fm_preamp      = 100;
   config.cdda_volume    = 100;
   config.pcm_volume     = 100;
   config.hq_fm          = 1; /* high-quality FM resampling (slower) */
   config.hq_psg         = 1; /* high-quality PSG resampling (slower) */
   config.filter         = 1; /* no filter */
   config.lp_range       = 0x9999; /* 0.6 in 0.16 fixed point */
   config.low_freq       = 880;
   config.high_freq      = 5000;
   config.lg             = 100;
   config.mg             = 100;
   config.hg             = 100;
   config.ym2612         = YM2612_DISCRETE; 
   config.ym2413         = 2; /* AUTO */
   config.mono           = 0; /* STEREO output */
#ifdef USE_PER_SOUND_CHANNELS_CONFIG
   for (i = 0; i < 4; i++) config.psg_ch_volumes[i]    = 100;  /* individual channel volumes */
   for (i = 0; i < 6; i++) config.md_ch_volumes[i]     = 100;  /* individual channel volumes */
   for (i = 0; i < 9; i++) config.sms_fm_ch_volumes[i] = 100;  /* individual channel volumes */
#endif
#ifdef HAVE_YM3438_CORE
   config.ym3438         = 0;
#endif
#ifdef HAVE_OPLL_CORE
   config.opll           = 0;
#endif

   /* system options */
   config.system         = 0; /* AUTO */
   config.region_detect  = 0; /* AUTO */
   config.vdp_mode       = 0; /* AUTO */
   config.master_clock   = 0; /* AUTO */
   config.force_dtack    = 0;
   config.addr_error     = 1;
   config.bios           = 0;
   config.lock_on        = 0;
   config.add_on         = HW_ADDON_AUTO;
   config.lcd            = 0; /* 0.8 fixed point */
#ifdef HAVE_OVERCLOCK
   config.overclock      = 100;
#endif
   config.no_sprite_limit = 0;
   config.enhanced_vscroll = 0;
   config.enhanced_vscroll_limit = 8;

   /* video options */
   config.overscan = 0;
   config.aspect_ratio = 0;
   config.gg_extra = 0;
   config.ntsc     = 0;
   config.lcd      = 0;
   config.render   = 0;
   config.left_border = 0;

   /* input options */
   input.system[0] = SYSTEM_GAMEPAD;
   input.system[1] = SYSTEM_GAMEPAD;
   for (i=0; i<MAX_INPUTS; i++)
   {
     config.input[i].padtype = DEVICE_PAD2B | DEVICE_PAD3B | DEVICE_PAD6B;
   }
}

static void bram_load(void)
{
    RFILE *fp;

    /* automatically load internal backup RAM */
    switch (region_code)
    {
       case REGION_JAPAN_NTSC:
          fp = filestream_open(CD_BRAM_JP, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
          break;
       case REGION_EUROPE:
          fp = filestream_open(CD_BRAM_EU, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
          break;
       case REGION_USA:
          fp = filestream_open(CD_BRAM_US, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
          break;
       default:
          return;
    }

    if (fp != NULL)
    {
      filestream_read(fp, scd.bram, 0x2000);
      filestream_close(fp);

      /* update CRC */
      brm_crc[0] = crc32(0, scd.bram, 0x2000);
    }
    else
    {
      /* force internal backup RAM format (does not use previous region backup RAM) */
      scd.bram[0x1fff] = 0;
    }

    /* check if internal backup RAM is correctly formatted */
    if (memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
    {
      /* clear internal backup RAM */
      memset(scd.bram, 0x00, 0x2000 - 0x40);

      /* internal Backup RAM size fields */
      brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = 0x00;
      brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (sizeof(scd.bram) / 64) - 3;

      /* format internal backup RAM */
      memcpy(scd.bram + 0x2000 - 0x40, brm_format, 0x40);

      /* clear CRC to force file saving (in case previous region backup RAM was also formatted) */
      brm_crc[0] = 0;
    }

    /* automatically load cartridge backup RAM (if enabled) */
    if (scd.cartridge.id)
    {
      fp = filestream_open(CART_BRAM, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (fp != NULL)
      {
        int filesize = scd.cartridge.mask + 1;
        int done = 0;
        
        /* Read into buffer (2k blocks) */
        while (filesize > CHUNKSIZE)
        {
          filestream_read(fp, scd.cartridge.area + done, CHUNKSIZE);
          done += CHUNKSIZE;
          filesize -= CHUNKSIZE;
        }

        /* Read remaining bytes */
        if (filesize)
        {
          filestream_read(fp, scd.cartridge.area + done, filesize);
        }

        /* close file */
        filestream_close(fp);

        /* update CRC */
        brm_crc[1] = crc32(0, scd.cartridge.area, scd.cartridge.mask + 1);
      }

      /* check if cartridge backup RAM is correctly formatted */
      if (memcmp(scd.cartridge.area + scd.cartridge.mask + 1 - 0x20, brm_format + 0x20, 0x20))
      {
        /* clear cartridge backup RAM */
        memset(scd.cartridge.area, 0x00, scd.cartridge.mask + 1);

        /* Cartridge Backup RAM size fields */
        brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = (((scd.cartridge.mask + 1) / 64) - 3) >> 8;
        brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (((scd.cartridge.mask + 1) / 64) - 3) & 0xff;

        /* format cartridge backup RAM */
        memcpy(scd.cartridge.area + scd.cartridge.mask + 1 - 0x40, brm_format, 0x40);
      }
    }
}

static void bram_save(void)
{
    RFILE *fp;

    /* verify that internal backup RAM has been modified */
    if (crc32(0, scd.bram, 0x2000) != brm_crc[0])
    {
      /* check if it is correctly formatted before saving */
      if (!memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
      {
        switch (region_code)
        {
          case REGION_JAPAN_NTSC:
            fp = filestream_open(CD_BRAM_JP, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
            break;
          case REGION_EUROPE:
            fp = filestream_open(CD_BRAM_EU, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
            break;
          case REGION_USA:
            fp = filestream_open(CD_BRAM_US, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
            break;
          default:
            return;
        }

        if (fp != NULL)
        {
          filestream_write(fp, scd.bram, 0x2000);
          filestream_close(fp);

          /* update CRC */
          brm_crc[0] = crc32(0, scd.bram, 0x2000);
        }
      }
    }

    /* verify that cartridge backup RAM has been modified */
    if (scd.cartridge.id && (crc32(0, scd.cartridge.area, scd.cartridge.mask + 1) != brm_crc[1]))
    {
      /* check if it is correctly formatted before saving */
      if (!memcmp(scd.cartridge.area + scd.cartridge.mask + 1 - 0x20, brm_format + 0x20, 0x20))
      {
        fp = filestream_open(CART_BRAM, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
        if (fp != NULL)
        {
          int filesize = scd.cartridge.mask + 1;
          int done = 0;
        
          /* Write to file (2k blocks) */
          while (filesize > CHUNKSIZE)
          {
            filestream_write(fp, scd.cartridge.area + done, CHUNKSIZE);
            done += CHUNKSIZE;
            filesize -= CHUNKSIZE;
          }

          /* Write remaining bytes */
          if (filesize)
          {
            filestream_write(fp, scd.cartridge.area + done, filesize);
          }

          /* Close file */
          filestream_close(fp);

          /* update CRC */
          brm_crc[1] = crc32(0, scd.cartridge.area, scd.cartridge.mask + 1);
        }
      }
    }
}

static void extract_name(char *buf, const char *path, size_t size)
{
   char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');

   if (base)
   {
      strlcpy(buf, base, size);
      base = strrchr(buf, '.');
      if (base)
         *base = '\0';
   }
   else
      buf[0] = '\0';
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   char *base;
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   if (!(base = strrchr(buf, '/')))
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
      buf[0] = '\0';
}

static double calculate_display_aspect_ratio(void)
{
   double videosamplerate, dotrate;
   bool is_h40 = false;

   if (config.aspect_ratio == 0)
   {
      if ((system_hw == SYSTEM_GG || system_hw == SYSTEM_GGMS) && config.overscan == 0 && config.gg_extra == 0)
         return (6.0 / 5.0) * ((double)vwidth / (double)vheight);
   }

   is_h40  = bitmap.viewport.w == 320; /* Could be read directly from the register as well. */
   dotrate = system_clock / (is_h40 ? 8.0 : 10.0);

   if (config.aspect_ratio == 1) /* Force NTSC PAR */
      videosamplerate = 135000000.0 / 11.0;
   else if (config.aspect_ratio == 2) /* Force PAL PAR */
      videosamplerate = 14750000.0;
   else if (config.aspect_ratio == 3) /* Force 4:3 */
      return (4.0 / 3.0);
   else if (config.aspect_ratio == 4) /* Uncorrected */
      return (0.0);
   else
      videosamplerate = vdp_pal ? 14750000.0 : 135000000.0 / 11.0;

   return (videosamplerate / dotrate) * ((double)(vwidth - vwoffset) / ((double)vheight * 2.0));
}

static bool update_geometry(void)
{
   struct retro_system_av_info info;
   bool update_av_info = false;

   retro_get_system_av_info(&info);

   if (     info.geometry.max_width > max_width
         || info.geometry.max_height > max_height)
   {
      update_av_info = true;
      max_width  = info.geometry.max_width;
      max_height = info.geometry.max_height;
   }

   if (info.timing.fps != retro_fps)
   {
      update_av_info = true;
      retro_fps = info.timing.fps;
   }

   if (update_av_info)
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info);
}

static bool update_viewport(void)
{
   int ow = vwidth;
   int oh = vheight;
   double oar = vaspect_ratio;

   if ((system_hw == SYSTEM_GG) && !config.gg_extra)
      bitmap.viewport.x = (config.overscan & 2) ? 14 : -48;
   else
      bitmap.viewport.x = (config.overscan & 2) * 7;

   if (     (config.left_border != 0)
         && (reg[0] & 0x20)
         && ((system_hw == SYSTEM_MARKIII) || (system_hw & SYSTEM_SMS) || (system_hw == SYSTEM_PBC)))
   {
      bmdoffset = (16 + (config.ntsc ? 24 : 0));
      if (config.left_border == 1)
         vwoffset = (8 + (config.ntsc ? 12 : 0));
      else
         vwoffset = (16 + (config.ntsc ? 24 : 0));
   }
   else
      bmdoffset = vwoffset = 0;

   vwidth  = bitmap.viewport.w + (bitmap.viewport.x * 2);
   vheight = bitmap.viewport.h + (bitmap.viewport.y * 2);
   vaspect_ratio = calculate_display_aspect_ratio();

   if (config.ntsc)
   {
      if (reg[12] & 1)
         vwidth = MD_NTSC_OUT_WIDTH(vwidth);
      else
         vwidth = SMS_NTSC_OUT_WIDTH(vwidth);
   }

   if (config.render && interlaced)
   {
      vheight = vheight * 2;
   }

   return ((ow != vwidth) || (oh != vheight) || (oar != vaspect_ratio));
}

#ifdef HAVE_OVERCLOCK
static void update_overclock(void)
{
#ifdef M68K_OVERCLOCK_SHIFT
    m68k.cycle_ratio = 1 << M68K_OVERCLOCK_SHIFT;
#endif
#ifdef Z80_OVERCLOCK_SHIFT
    z80_cycle_ratio = 1 << Z80_OVERCLOCK_SHIFT;
#endif
    if (overclock_delay == 0)
    {
      /* Cycle ratios multiply per-instruction cycle counts, so use
         reciprocals */
#ifdef M68K_OVERCLOCK_SHIFT
      if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
        m68k.cycle_ratio = (100 << M68K_OVERCLOCK_SHIFT) / config.overclock;
#endif
#ifdef Z80_OVERCLOCK_SHIFT
      if ((system_hw & SYSTEM_PBC) != SYSTEM_MD)
        z80_cycle_ratio = (100 << Z80_OVERCLOCK_SHIFT) / config.overclock;
#endif
    }
}
#endif

static void check_variables(bool first_run)
{
  unsigned orig_value;
  struct retro_system_av_info info;
#ifdef USE_PER_SOUND_CHANNELS_CONFIG
  unsigned c;
  char md_fm_channel_volume_base_str[]  = "genesis_plus_gx_md_channel_0_volume";
  char sms_fm_channel_volume_base_str[] = "genesis_plus_gx_sms_fm_channel_0_volume";
  char psg_channel_volume_base_str[]    = "genesis_plus_gx_psg_channel_0_volume";
#endif
  bool update_viewports     = false;
  bool reinit               = false;
  bool update_frameskip     = false;
  struct retro_variable var = {0};

  if (first_run)
  {
    var.key = "genesis_plus_gx_system_bram";
    environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
    {
      if (!var.value || !strcmp(var.value, "per bios"))
      {
        fill_pathname_join(CD_BRAM_EU, save_dir, "scd_E.brm", sizeof(CD_BRAM_EU));
        fill_pathname_join(CD_BRAM_US, save_dir, "scd_U.brm", sizeof(CD_BRAM_US));
        fill_pathname_join(CD_BRAM_JP, save_dir, "scd_J.brm", sizeof(CD_BRAM_JP));
      }
      else
      {
        char newpath[4096];
        fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
        strlcat(newpath, ".brm", sizeof(newpath));
        strlcpy(CD_BRAM_EU, newpath, sizeof(CD_BRAM_EU));
        strlcpy(CD_BRAM_US, newpath, sizeof(CD_BRAM_US));
        strlcpy(CD_BRAM_JP, newpath, sizeof(CD_BRAM_JP));
      }
    }
  }

  if (first_run)
  {
    var.key = "genesis_plus_gx_cart_size";
    environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
    {
      if (var.value && !strcmp(var.value, "disabled"))
        cart_size = 0xff;
      else if (var.value && !strcmp(var.value, "128k"))
        cart_size = 1;
      else if (var.value && !strcmp(var.value, "256k"))
        cart_size = 2;
      else if (var.value && !strcmp(var.value, "512k"))
        cart_size = 3;
      else if (var.value && !strcmp(var.value, "1meg"))
        cart_size = 4;
      else if (var.value && !strcmp(var.value, "2meg"))
        cart_size = 5;
      else if (var.value && !strcmp(var.value, "4meg"))
        cart_size = 6;
    }
  }

  if (first_run)
  {
    var.key = "genesis_plus_gx_cart_bram";
    environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
    {
      if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 1)
         {
           fill_pathname_join(CART_BRAM, save_dir, "128Kbit_cart.brm", sizeof(CART_BRAM));
         }
      else if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 2)
         {
           fill_pathname_join(CART_BRAM, save_dir, "256Kbit_cart.brm", sizeof(CART_BRAM));
         }
      else if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 3)
         {
           fill_pathname_join(CART_BRAM, save_dir, "512Kbit_cart.brm", sizeof(CART_BRAM));
         }
      else if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 4)
         {
           fill_pathname_join(CART_BRAM, save_dir, "1Mbit_cart.brm", sizeof(CART_BRAM));
         }
      else if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 5)
         {
           fill_pathname_join(CART_BRAM, save_dir, "2Mbit_cart.brm", sizeof(CART_BRAM));
         }
      else if ((!var.value || !strcmp(var.value, "per cart")) && cart_size == 6)
         {
           fill_pathname_join(CART_BRAM, save_dir, "4Mbit_cart.brm", sizeof(CART_BRAM));
         }
      else
      {
      if (cart_size == 1)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_128Kbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      else if (cart_size == 2)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_256Kbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      else if (cart_size == 3)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_512Kbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      else if (cart_size == 4)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_1Mbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      else if (cart_size == 5)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_2Mbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      else if (cart_size == 6)
         { 
           char newpath[4096];
           fill_pathname_join(newpath, save_dir, g_rom_name, sizeof(newpath));
           strlcat(newpath, "_4Mbit_cart.brm", sizeof(newpath));
           strlcpy(CART_BRAM, newpath, sizeof(CART_BRAM));
         }
      }
     }
   }

  var.key = "genesis_plus_gx_system_hw";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.system;
    if (var.value && !strcmp(var.value, "sg-1000"))
      config.system = SYSTEM_SG;
    else if (var.value && !strcmp(var.value, "sg-1000 II"))
      config.system = SYSTEM_SGII;
    else if (var.value && !strcmp(var.value, "sg-1000 II + ram ext."))
      config.system = SYSTEM_SGII_RAM_EXT;
    else if (var.value && !strcmp(var.value, "mark-III"))
      config.system = SYSTEM_MARKIII;
    else if (var.value && !strcmp(var.value, "master system"))
      config.system = SYSTEM_SMS;
    else if (var.value && !strcmp(var.value, "master system II"))
      config.system = SYSTEM_SMS2;
    else if (var.value && !strcmp(var.value, "game gear"))
      config.system = SYSTEM_GG;
    else if (var.value && !strcmp(var.value, "mega drive / genesis"))
      config.system = SYSTEM_MD;
    else
      config.system = 0;

    if (orig_value != config.system)
    {
      if (system_hw)
      {
        switch (config.system)
        {
          case 0:
            system_hw = romtype; /* AUTO */
            break;

          case SYSTEM_MD:
            system_hw = (romtype & SYSTEM_MD) ? romtype : SYSTEM_PBC;
            break;

          case SYSTEM_GG:
            system_hw = (romtype == SYSTEM_GG) ? SYSTEM_GG : SYSTEM_GGMS;
            break;

          default:
            system_hw = config.system;
            break;
        }

        reinit = true;
      }
    }
  }

  var.key = "genesis_plus_gx_bios";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.bios;
    if (var.value && !strcmp(var.value, "enabled"))
      config.bios = 3;
    else
      config.bios = 0;

    if (orig_value != config.bios)
    {
      if (system_hw)
      {
        reinit = true;
      }
    }
  }

  var.key = "genesis_plus_gx_region_detect";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.region_detect;
    if (var.value && !strcmp(var.value, "ntsc-u"))
      config.region_detect = 1;
    else if (var.value && !strcmp(var.value, "pal"))
      config.region_detect = 2;
    else if (var.value && !strcmp(var.value, "ntsc-j"))
      config.region_detect = 3;
    else
      config.region_detect = 0;

    if (orig_value != config.region_detect)
    {
      if (system_hw)
      {
        get_region(NULL);
        
        if ((system_hw == SYSTEM_MCD) || ((system_hw & SYSTEM_SMS) && config.bios))
        {
          /* system with region BIOS should be reinitialized */
          reinit = true;
        }
        else
        {
          static const uint16 vc_table[4][2] = 
          {
            /* NTSC, PAL */
            {0xDA , 0xF2},  /* Mode 4 (192 lines) */
            {0xEA , 0x102}, /* Mode 5 (224 lines) */
            {0xDA , 0xF2},  /* Mode 4 (192 lines) */
            {0x106, 0x10A}  /* Mode 5 (240 lines) */
          };

          /* framerate might have changed, reinitialize audio timings */
          audio_set_rate(44100, 0);
          
          /* reinitialize I/O region register */
          if (system_hw == SYSTEM_MD)
          {
            io_reg[0x00] = 0x20 | region_code | (config.bios & 1);
          }
          else if (system_hw == SYSTEM_MCD)
          {
            io_reg[0x00] = region_code | (config.bios & 1);
          }
          else
          {
            io_reg[0x00] = 0x80 | (region_code >> 1);
          }

          /* reinitialize VDP timings */
          lines_per_frame = vdp_pal ? 313 : 262;
     
          /* reinitialize NTSC/PAL mode in VDP status */
          if (system_hw & SYSTEM_MD)
          {
            status = (status & ~1) | vdp_pal;
          }

          /* reinitialize VC max value */
          switch (bitmap.viewport.h)
          {
            case 192:
              vc_max = vc_table[0][vdp_pal];
              break;
            case 224:
              vc_max = vc_table[1][vdp_pal];
              break;
            case 240:
              vc_max = vc_table[3][vdp_pal];
              break;
          }

          update_viewports = true;
        }

        update_frameskip = true;
      }
    }
  }

  var.key = "genesis_plus_gx_vdp_mode";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.vdp_mode;
    if (var.value && !strcmp(var.value, "60hz"))
      config.vdp_mode = 1;
    else if (var.value && !strcmp(var.value, "50hz"))
      config.vdp_mode = 2;
    else
      config.vdp_mode = 0;

    if (orig_value != config.vdp_mode)
    {
      if (system_hw)
      {
        get_region(NULL);

        if ((system_hw == SYSTEM_MCD) || ((system_hw & SYSTEM_SMS) && config.bios))
        {
          /* system with region BIOS should be reinitialized */
          reinit = true;
        }
        else
        {
          static const uint16 vc_table[4][2] = 
          {
            /* NTSC, PAL */
            {0xDA , 0xF2},  /* Mode 4 (192 lines) */
            {0xEA , 0x102}, /* Mode 5 (224 lines) */
            {0xDA , 0xF2},  /* Mode 4 (192 lines) */
            {0x106, 0x10A}  /* Mode 5 (240 lines) */
          };

          /* framerate might have changed, reinitialize audio timings */
          audio_set_rate(44100, 0);

          /* reinitialize I/O region register */
          if (system_hw == SYSTEM_MD)
          {
            io_reg[0x00] = 0x20 | region_code | (config.bios & 1);
          }
          else if (system_hw == SYSTEM_MCD)
          {
            io_reg[0x00] = region_code | (config.bios & 1);
          }
          else
          {
            io_reg[0x00] = 0x80 | (region_code >> 1);
          }

          /* reinitialize VDP timings */
          lines_per_frame = vdp_pal ? 313 : 262;

          /* reinitialize NTSC/PAL mode in VDP status */
          if (system_hw & SYSTEM_MD)
          {
            status = (status & ~1) | vdp_pal;
          }

          /* reinitialize VC max value */
          switch (bitmap.viewport.h)
          {
            case 192:
              vc_max = vc_table[0][vdp_pal];
              break;
            case 224:
              vc_max = vc_table[1][vdp_pal];
              break;
            case 240:
              vc_max = vc_table[3][vdp_pal];
              break;
          }

          update_viewports = true;
        }

        update_frameskip = true;
      }
    }
  }

  var.key = "genesis_plus_gx_force_dtack";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "enabled"))
      config.force_dtack = 1;
    else
      config.force_dtack = 0;
  }

  var.key = "genesis_plus_gx_addr_error";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "enabled"))
      m68k.aerr_enabled = config.addr_error = 1;
    else
      m68k.aerr_enabled = config.addr_error = 0;
  }

  var.key = "genesis_plus_gx_cd_latency";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "enabled"))
      config.cd_latency = 1;
    else
      config.cd_latency = 0;
  }

  var.key = "genesis_plus_gx_cd_precache";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "disabled"))
      config.cd_precache = 0;
    else
      config.cd_precache = 1;
  }

  var.key = "genesis_plus_gx_add_on";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.add_on;
    if (var.value && !strcmp(var.value, "sega/mega cd"))
      config.add_on = HW_ADDON_MEGACD;
    else if (var.value && !strcmp(var.value, "megasd"))
      config.add_on = HW_ADDON_MEGASD;
    else if (var.value && !strcmp(var.value, "none"))
      config.add_on = HW_ADDON_NONE;
    else
      config.add_on = HW_ADDON_AUTO;
    /* note: game needs to be reloaded for change to take effect */
  }

  var.key = "genesis_plus_gx_lock_on";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.lock_on;
    if (var.value && !strcmp(var.value, "game genie"))
      config.lock_on = TYPE_GG;
    else if (var.value && !strcmp(var.value, "action replay (pro)"))
      config.lock_on = TYPE_AR;
    else if (var.value && !strcmp(var.value, "sonic & knuckles"))
      config.lock_on = TYPE_SK;
    else
      config.lock_on = 0;

    if (orig_value != config.lock_on)
    {
      if (system_hw == SYSTEM_MD)
        reinit = true;
    }
  }

  var.key = "genesis_plus_gx_ym2413";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.ym2413;
    if (var.value && !strcmp(var.value, "enabled"))
      config.ym2413 = 1;
    else if (var.value && !strcmp(var.value, "disabled"))
      config.ym2413 = 0;
    else
      config.ym2413 = 2;

    if (orig_value != config.ym2413)
    {
      if (system_hw && (config.ym2413 & 2) && ((system_hw & SYSTEM_PBC) != SYSTEM_MD))
      {
        memcpy(temp, sram.sram, sizeof(temp));
        sms_cart_init();
        memcpy(sram.sram, temp, sizeof(temp));
      }
    }
  }
  
#ifdef HAVE_OPLL_CORE
  var.key = "genesis_plus_gx_ym2413_core";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.opll;
    if (var.value && !strcmp(var.value, "nuked"))
    {
      config.opll = 1;
    }
    else
    {
      config.opll = 0;
    }

    if (((orig_value == 0) && (config.opll > 0)) || ((orig_value > 0) && (config.opll == 0)))
    {
      sound_init();
      sound_reset();
    }
  }
#endif

  var.key = "genesis_plus_gx_sound_output";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (var.value && !strcmp(var.value, "mono"))
      config.mono = 1;
    else if (!var.value || !strcmp(var.value, "stereo"))
      config.mono = 0; 
  }


  var.key = "genesis_plus_gx_psg_preamp";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.psg_preamp = (!var.value) ? 150: atoi(var.value);
    if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
    {
      psg_config(0, config.psg_preamp, 0xff);
    }
    else
    {
      psg_config(0, config.psg_preamp, io_reg[6]);
    }
  }

  var.key = "genesis_plus_gx_fm_preamp";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.fm_preamp = (!var.value) ? 100: atoi(var.value);
  }

  var.key = "genesis_plus_gx_cdda_volume";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.cdda_volume = (!var.value) ? 100: atoi(var.value);
  }

  var.key = "genesis_plus_gx_pcm_volume";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.pcm_volume = (!var.value) ? 100: atoi(var.value);
  }

  var.key = "genesis_plus_gx_audio_filter";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (var.value && !strcmp(var.value, "low-pass"))
      config.filter = 1;

#ifdef HAVE_EQ 
    else if (var.value && !strcmp(var.value, "EQ"))
      config.filter = 2;
#endif

    else
      config.filter = 0;
  }

  var.key = "genesis_plus_gx_lowpass_range";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.lp_range = (!var.value) ? 0x9999 : ((atoi(var.value) * 65536) / 100);
  }

#ifdef HAVE_EQ
  var.key = "genesis_plus_gx_audio_eq_low";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    uint8_t new_lg = (!var.value) ? 100 : atoi(var.value);
    if (new_lg != config.lg) restart_eq = true;
    config.lg = new_lg;
  }

  var.key = "genesis_plus_gx_audio_eq_mid";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    uint8_t new_mg = (!var.value) ? 100 : atoi(var.value);
    if (new_mg != config.mg) restart_eq = true;
    config.mg = new_mg;
  }

  var.key = "genesis_plus_gx_audio_eq_high";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    uint8_t new_hg = (!var.value) ? 100 : atoi(var.value);
    if (new_hg != config.hg) restart_eq = true;
    config.hg = new_hg;

  }
#endif

  var.key = "genesis_plus_gx_ym2612";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
#ifdef HAVE_YM3438_CORE
    orig_value = config.ym3438;
    if (var.value && !strcmp(var.value, "nuked (ym2612)"))
    {
      OPN2_SetChipType(ym3438_mode_ym2612);
      config.ym3438 = 1;
    }
    else if (var.value && !strcmp(var.value, "nuked (ym3438)"))
    {
      OPN2_SetChipType(ym3438_mode_readmode);
      config.ym3438 = 2;
    }
    else
    {
      config.ym3438 = 0;
    }

    if (((orig_value == 0) && (config.ym3438 > 0)) || ((orig_value > 0) && (config.ym3438 == 0)))
    {
      sound_init();
      sound_reset();
    }
#endif

    if (!var.value || !strcmp(var.value, "mame (ym2612)"))
    {
      config.ym2612 = YM2612_DISCRETE;
      YM2612Config(YM2612_DISCRETE);
    }
    else if (var.value && !strcmp(var.value, "mame (asic ym3438)"))
    {
      config.ym2612 = YM2612_INTEGRATED;
      YM2612Config(YM2612_INTEGRATED);
    }
    else
    {
      config.ym2612 = YM2612_ENHANCED;
      YM2612Config(YM2612_ENHANCED);
    }
  }

  var.key        = "genesis_plus_gx_frameskip";
  var.value      = NULL;
  orig_value     = frameskip_type;
  frameskip_type = 0;

  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
  {
    if (strcmp(var.value, "auto") == 0)
      frameskip_type = 1;
    else if (strcmp(var.value, "manual") == 0)
      frameskip_type = 2;
  }

  update_frameskip = update_frameskip || (frameskip_type != orig_value);

  var.key             = "genesis_plus_gx_frameskip_threshold";
  var.value           = NULL;
  frameskip_threshold = 33;

  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    frameskip_threshold = strtol(var.value, NULL, 10);

  var.key = "genesis_plus_gx_blargg_ntsc_filter";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.ntsc;

    if (!var.value || !strcmp(var.value, "disabled"))
      config.ntsc = 0;
    else if (var.value && !strcmp(var.value, "monochrome"))
    {
      config.ntsc = 1;
      sms_ntsc_init(sms_ntsc, &sms_ntsc_monochrome);
      md_ntsc_init(md_ntsc,   &md_ntsc_monochrome);
    }
    else if (var.value && !strcmp(var.value, "composite"))
    {
      config.ntsc = 1;
      sms_ntsc_init(sms_ntsc, &sms_ntsc_composite);
      md_ntsc_init(md_ntsc,   &md_ntsc_composite);
    }
    else if (var.value && !strcmp(var.value, "svideo"))
    {
      config.ntsc = 1;
      sms_ntsc_init(sms_ntsc, &sms_ntsc_svideo);
      md_ntsc_init(md_ntsc,   &md_ntsc_svideo);
    }
    else if (var.value && !strcmp(var.value, "rgb"))
    {
      config.ntsc = 1;
      sms_ntsc_init(sms_ntsc, &sms_ntsc_rgb);
      md_ntsc_init(md_ntsc,   &md_ntsc_rgb);
    }

    if (orig_value != config.ntsc)
      update_viewports = true;
  }

  var.key = "genesis_plus_gx_lcd_filter";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "disabled"))
      config.lcd = 0;
    else if (var.value && !strcmp(var.value, "enabled"))
      config.lcd = (uint8)(0.80 * 256);
  }

  var.key = "genesis_plus_gx_overscan";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.overscan;
    if (!var.value || !strcmp(var.value, "disabled"))
      config.overscan = 0;
    else if (var.value && !strcmp(var.value, "top/bottom"))
      config.overscan = 1;
    else if (var.value && !strcmp(var.value, "left/right"))
      config.overscan = 2;
    else if (var.value && !strcmp(var.value, "full"))
      config.overscan = 3;
    if (orig_value != config.overscan)
      update_viewports = true;
  }

  var.key = "genesis_plus_gx_gg_extra";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.gg_extra;
    if (!var.value || !strcmp(var.value, "disabled"))
      config.gg_extra = 0;
    else if (var.value && !strcmp(var.value, "enabled"))
      config.gg_extra = 1;
    if (orig_value != config.gg_extra)
      update_viewports = true;
  }

  var.key = "genesis_plus_gx_aspect_ratio";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.aspect_ratio;
    if (var.value && !strcmp(var.value, "NTSC PAR"))
      config.aspect_ratio = 1;
    else if (var.value && !strcmp(var.value, "PAL PAR"))
      config.aspect_ratio = 2;
    else if (var.value && !strcmp(var.value, "4:3"))
      config.aspect_ratio = 3;
    else if (var.value && !strcmp(var.value, "Uncorrected"))
      config.aspect_ratio = 4;
    else
      config.aspect_ratio = 0;
    if (orig_value != config.aspect_ratio)
      update_viewports = true;
  }

  var.key = "genesis_plus_gx_render";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.render;
    if (!var.value || !strcmp(var.value, "single field"))
      config.render = 0;
    else
      config.render = 1;
    if (orig_value != config.render)
      update_viewports = true;
  }

  var.key = "genesis_plus_gx_gun_cursor";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "disabled"))
      config.gun_cursor = 0;
    else
      config.gun_cursor = 1;
  }

  var.key = "genesis_plus_gx_gun_input";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "touchscreen"))
      retro_gun_mode = RetroPointer;
    else
      retro_gun_mode = RetroLightgun;
  }

  var.key = "genesis_plus_gx_invert_mouse";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "disabled"))
      config.invert_mouse = 0;
    else
      config.invert_mouse = 1;
  }

  var.key = "genesis_plus_gx_left_border";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    orig_value = config.left_border;
    if (!var.value || !strcmp(var.value, "disabled"))
      config.left_border = 0;
    else if (var.value && !strcmp(var.value, "left border"))
      config.left_border = 1;
    else if (var.value && !strcmp(var.value, "left & right borders"))
      config.left_border = 2;
    if (orig_value != config.left_border)
      update_viewports = true;
  }

#ifdef HAVE_OVERCLOCK
  var.key = "genesis_plus_gx_overclock";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    config.overclock = (!var.value) ? 100 : atoi(var.value);

    if (system_hw)
      update_overclock();
  }
#endif

  var.key = "genesis_plus_gx_no_sprite_limit";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
    if (!var.value || !strcmp(var.value, "disabled"))
      config.no_sprite_limit = 0;
    else
      config.no_sprite_limit = 1;
  }

  var.key = "genesis_plus_gx_enhanced_vscroll";
  environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
  {
      if (!var.value || !strcmp(var.value, "disabled"))
         config.enhanced_vscroll = 0;
      else
         config.enhanced_vscroll = 1;
  }

  var.key = "genesis_plus_gx_enhanced_vscroll_limit";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    config.enhanced_vscroll_limit = strtol(var.value, NULL, 10);

#ifdef USE_PER_SOUND_CHANNELS_CONFIG
  var.key = psg_channel_volume_base_str;
  for (c = 0; c < 4; c++)
  {
     psg_channel_volume_base_str[28] = c+'0';
     if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
     {
        config.psg_ch_volumes[c] = atoi(var.value);
        /* need to recall config to have the settings applied */
        if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
           psg_config(0, config.psg_preamp, 0xff);
        else
           psg_config(0, config.psg_preamp, io_reg[6]);
     }
  }
  
  var.key = md_fm_channel_volume_base_str;
  for (c = 0; c < 6; c++)
  {
    md_fm_channel_volume_base_str[27] = c+'0';
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
      config.md_ch_volumes[c] = atoi(var.value);
  }
  
  var.key = sms_fm_channel_volume_base_str;
  for (c = 0; c < 9; c++)
  {
     sms_fm_channel_volume_base_str[31] = c+'0';
     if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
        config.sms_fm_ch_volumes[c] = atoi(var.value);
  }

  var.key = "genesis_plus_gx_show_advanced_audio_settings";
  var.value = NULL;

  /* If frontend supports core option categories,
   * then genesis_plus_gx_show_advanced_audio_settings
   * is ignored and no options should be hidden */

  if (!libretro_supports_option_categories &&
      environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
  {
    bool show_advanced_av_settings_prev = show_advanced_av_settings;

    show_advanced_av_settings = true;
    if (strcmp(var.value, "disabled") == 0)
      show_advanced_av_settings = false;

    if (show_advanced_av_settings != show_advanced_av_settings_prev)
    {
      size_t i;
      struct retro_core_option_display option_display;
      char av_keys[19][40] = {
        "genesis_plus_gx_psg_channel_0_volume",
        "genesis_plus_gx_psg_channel_1_volume",
        "genesis_plus_gx_psg_channel_2_volume",
        "genesis_plus_gx_psg_channel_3_volume",
        "genesis_plus_gx_md_channel_0_volume",
        "genesis_plus_gx_md_channel_1_volume",
        "genesis_plus_gx_md_channel_2_volume",
        "genesis_plus_gx_md_channel_3_volume",
        "genesis_plus_gx_md_channel_4_volume",
        "genesis_plus_gx_md_channel_5_volume",
        "genesis_plus_gx_sms_fm_channel_0_volume",
        "genesis_plus_gx_sms_fm_channel_1_volume",
        "genesis_plus_gx_sms_fm_channel_2_volume",
        "genesis_plus_gx_sms_fm_channel_3_volume",
        "genesis_plus_gx_sms_fm_channel_4_volume",
        "genesis_plus_gx_sms_fm_channel_5_volume",
        "genesis_plus_gx_sms_fm_channel_6_volume",
        "genesis_plus_gx_sms_fm_channel_7_volume",
        "genesis_plus_gx_sms_fm_channel_8_volume"
      };

      option_display.visible = show_advanced_av_settings;

      for (i = 0; i < 19; i++)
      {
        option_display.key = av_keys[i];
        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      }
    }
  }
#endif

  if (reinit)
  {
#ifdef HAVE_OVERCLOCK
    overclock_delay = OVERCLOCK_FRAME_DELAY;
#endif
    audio_init(SOUND_FREQUENCY, 0);
    memcpy(temp, sram.sram, sizeof(temp));
    system_init();
    system_reset();
    memcpy(sram.sram, temp, sizeof(temp));
    update_viewports = true;
  }

  if (update_viewports)
    bitmap.viewport.changed = 11;

  /* Reinitialise frameskipping, if required */
  if ((update_frameskip || reinit) && !first_run)
    init_frameskip();
}

/* Cheat Support */
static uint32_t decode_cheat(char *string, int index)
{
   char *p;
   int i,n;
   uint32_t len = 0;
   uint32_t address = 0;
   uint16_t data = 0;
   uint8_t ref = 0;

   if ((system_hw & SYSTEM_PBC) == SYSTEM_MD){
      /*If system is Genesis-based*/

      /*Game-Genie*/
      if ((strlen(string) >= 9) && (string[4] == '-'))
      {
         for (i = 0; i < 8; i++)
         {
            if (i == 4) string++;
            p = strchr (ggvalidchars, *string++);
            if (!p)
               return 0;
            n = p - ggvalidchars;
            switch (i)
            {
               case 0:
                  data |= n << 3;
                  break;
               case 1:
                  data |= n >> 2;
                  address |= (n & 3) << 14;
                  break;
               case 2:
                  address |= n << 9;
                  break;
               case 3:
                  address |= (n & 0xF) << 20 | (n >> 4) << 8;
                  break;
               case 4:
                  data |= (n & 1) << 12;
                  address |= (n >> 1) << 16;
                  break;
               case 5:
                  data |= (n & 1) << 15 | (n >> 1) << 8;
                  break;
               case 6:
                  data |= (n >> 3) << 13;
                  address |= (n & 7) << 5;
                  break;
               case 7:
                  address |= n;
                  break;
            }
         }
         /* code length */
         len = 9;
      }

      /*Patch and PAR*/
      else if ((strlen(string) >=9) && (string[6] == ':'))
      {
         /* decode 24-bit address */
         for (i=0; i<6; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            address |= (n << ((5 - i) * 4));
         }
         /* decode 16-bit data */
         string++;
         for (i=0; i<4; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               break;
            n = (p - arvalidchars) & 0xF;
            data |= (n << ((3 - i) * 4));
         }
         /* code length */
         len = 11;
      }
   } else {
      /*If System is Master-based*/

      /*Game Genie*/
      if ((strlen(string) >=7) && (string[3] == '-'))
      {
         /* decode 8-bit data */
         for (i=0; i<2; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            data |= (n << ((1 - i) * 4));
         }

         /* decode 16-bit address (low 12-bits) */
         for (i=0; i<3; i++)
         {
            if (i==1) string++; /* skip separator */
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            address |= (n << ((2 - i) * 4));
         }
         /* decode 16-bit address (high 4-bits) */
         p = strchr (arvalidchars, *string++);
         if (!p)
            return 0;
         n = (p - arvalidchars) & 0xF;
         n ^= 0xF; /* bits inversion */
         address |= (n << 12);
         /* Optional: decode reference 8-bit data */
         if (*string=='-')
         {
            for (i=0; i<2; i++)
            {
               string++; /* skip separator and 2nd digit */
               p = strchr (arvalidchars, *string++);
               if (!p)
                  return 0;
               n = (p - arvalidchars) & 0xF;
               ref |= (n << ((1 - i) * 4));
            }
            ref = (ref >> 2) | ((ref & 0x03) << 6); /* 2-bit right rotation */
            ref ^= 0xBA; /* XOR */
            /* code length */
            len = 11;
         }
         else
         {
            /* code length */
            len = 7;
         }
      }

      /*Action Replay*/
      else if ((strlen(string) >=9) && (string[4] == '-')){
         string+=2;
         /* decode 16-bit address */
         for (i=0; i<4; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            address |= (n << ((3 - i) * 4));
            if (i==1) string++;
         }
         /* decode 8-bit data */
         for (i=0; i<2; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            data |= (n << ((1 - i) * 4));
         }
         /* code length */
         len = 9;
      }

      /*Fusion RAM*/
      else if ((strlen(string) >=7) && (string[4] == ':'))
      {
         /* decode 16-bit address */
         for (i=0; i<4; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            address |= (n << ((3 - i) * 4));
         }
         /* decode 8-bit data */
         string++;
         for (i=0; i<2; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            data |= (n << ((1 - i) * 4));
         }
         /* code length */
         len = 7;
      }

      /*Fusion ROM*/
      else if ((strlen(string) >=9) && (string[6] == ':'))
      {
         /* decode reference 8-bit data */
         for (i=0; i<2; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            ref |= (n << ((1 - i) * 4));
         }
         /* decode 16-bit address */
         for (i=0; i<4; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            address |= (n << ((3 - i) * 4));
         }
         /* decode 8-bit data */
         string++;
         for (i=0; i<2; i++)
         {
            p = strchr (arvalidchars, *string++);
            if (!p)
               return 0;
            n = (p - arvalidchars) & 0xF;
            data |= (n << ((1 - i) * 4));
         }
         /* code length */
         len = 9;
      }
      /* convert to 24-bit Work RAM address */
      if (address >= 0xC000)
         address = 0xFF0000 | (address & 0x1FFF);
   }
   /* Valid code found ? */
   if (len)
   {
      /* update cheat address & data values */
      cheatlist[index].address = address;
      cheatlist[index].data = data;
      cheatlist[index].old = ref;
   }
   /* return code length (0 = invalid) */
   return len;
}

static void apply_cheats(void)
{
   uint8_t *ptr;
   int i;
   /* clear ROM&RAM patches counter */
   maxROMcheats = maxRAMcheats = 0;

   for (i = 0; i < maxcheats; i++)
   {
      if (cheatlist[i].enable)
      {
         /* detect Work RAM patch */
         if (cheatlist[i].address >= 0xFF0000)
         {
            /* add RAM patch */
            cheatIndexes[maxRAMcheats++] = i;
         }

         /* check if Mega-CD game is running */
         else if ((system_hw == SYSTEM_MCD) && !scd.cartridge.boot)
         {
            /* detect PRG-RAM patch (Sub-CPU side) */
            if (cheatlist[i].address < 0x80000)
            {
               /* add RAM patch */
               cheatIndexes[maxRAMcheats++] = i;
            }

            /* detect Word-RAM patch (Main-CPU side)*/
            else if ((cheatlist[i].address >= 0x200000) && (cheatlist[i].address < 0x240000))
            {
               /* add RAM patch */
              cheatIndexes[maxRAMcheats++] = i;
            }
         }

         /* detect cartridge ROM patch */
         else if (cheatlist[i].address < cart.romsize)
         {
            if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
            {
               /* patch ROM data */
               cheatlist[i].old = *(uint16_t *)(cart.rom + (cheatlist[i].address & 0xFFFFFE));
               *(uint16_t *)(cart.rom + (cheatlist[i].address & 0xFFFFFE)) = cheatlist[i].data;
            }
            else
            {
               /* add ROM patch */
               maxROMcheats++;
               cheatIndexes[MAX_CHEATS - maxROMcheats] = i;
               /* get current banked ROM address */
               ptr = &z80_readmap[(cheatlist[i].address) >> 10][cheatlist[i].address & 0x03FF];
               /* check if reference matches original ROM data */
               if (((uint8_t)cheatlist[i].old) == *ptr)
               {
                  /* patch data */
                  *ptr = cheatlist[i].data;
                  /* save patched ROM address */
                  cheatlist[i].prev = ptr;
               }
               else
               {
                  /* no patched ROM address yet */
                  cheatlist[i].prev = NULL;
               }
            }
         }
      }
   }
}

static void clear_cheats(void)
{
   int i;

  /* no ROM patches with Mega-CD games */
   if ((system_hw == SYSTEM_MCD) && !scd.cartridge.boot)
      return;

   /* disable cheats in reversed order in case the same address is used by multiple ROM patches */
   i = maxcheats;
   while (i > 0)
   {
      if (cheatlist[i-1].enable)
      {
         if (cheatlist[i-1].address < cart.romsize)
         {
            if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
            {
               /* restore original ROM data */
               *(uint16_t *)(cart.rom + (cheatlist[i-1].address & 0xFFFFFE)) = cheatlist[i-1].old;
            }
            else
            {
               /* check if previous banked ROM address has been patched */
               if (cheatlist[i-1].prev != NULL)
               {
                  /* restore original data */
                  *cheatlist[i-1].prev = cheatlist[i-1].old;
                  /* no more patched ROM address */
                  cheatlist[i-1].prev = NULL;
               }
            }
         }
      }
      i--;
   }
}

/****************************************************************************
* RAMCheatUpdate
*
* Apply RAM patches (this should be called once per frame)
*
****************************************************************************/
static void RAMCheatUpdate(void)
{
   uint8_t *base;
   uint32_t mask;
   int index, cnt = maxRAMcheats;

   while (cnt)
   {
      /* get cheat index */
      index = cheatIndexes[--cnt];

      /* detect destination RAM */
      switch ((cheatlist[index].address >> 20) & 0xf)
      {
         case 0x0: /* Mega-CD PRG-RAM (512 KB) */
            base = scd.prg_ram;
            mask = 0x7fffe;
            break;

         case 0x2: /* Mega-CD 2M Word-RAM (256 KB) */
            base = scd.word_ram_2M;
            mask = 0x3fffe;
            break;

         default: /* Work-RAM (64 KB) */
            base = work_ram;
            mask = 0xfffe;
            break;
      }

      /* apply RAM patch */
      if (cheatlist[index].data & 0xFF00)
      {
         /* word patch */
         *(uint16_t *)(base + (cheatlist[index].address & mask)) = cheatlist[index].data;
      }
      else
      {
          /* byte patch */
          mask |= 1;
          base[cheatlist[index].address & mask] = cheatlist[index].data;
      }
   }
}

/****************************************************************************
 * ROMCheatUpdate
 *
 * Apply ROM patches (this should be called each time banking is changed)
 *
 ****************************************************************************/ 
void ROMCheatUpdate(void)
{
  int index, cnt = maxROMcheats;
  uint8_t *ptr;
  
  while (cnt)
  {
    /* get cheat index */
    index = cheatIndexes[MAX_CHEATS - cnt];

    /* check if previous banked ROM address was patched */
    if (cheatlist[index].prev != NULL)
    {
      /* restore original data */
      *cheatlist[index].prev = cheatlist[index].old;

      /* no more patched ROM address */
      cheatlist[index].prev = NULL;
    }

    /* get current banked ROM address */
    ptr = &z80_readmap[(cheatlist[index].address) >> 10][cheatlist[index].address & 0x03FF];

    /* check if reference exists and matches original ROM data */
    if (!cheatlist[index].old || ((uint8_t)cheatlist[index].old) == *ptr)
    {
      /* patch data */
      *ptr = cheatlist[index].data;

      /* save patched ROM address */
      cheatlist[index].prev = ptr;
    }

    /* next ROM patch */
    cnt--;
  }
}

static void set_memory_maps(void)
{
   if (system_hw == SYSTEM_MCD)
   {
      const size_t SCD_BIT = 1ULL << 31ULL;
      const uint64_t mem = RETRO_MEMDESC_SYSTEM_RAM;
      struct retro_memory_map mmaps;
      struct retro_memory_descriptor descs[] = {
         { mem, work_ram,        0,           0xFF0000, 0, 0, 0x10000, "68KRAM" },
         /* virtual address using SCD_BIT so all 512M of prg_ram can be accessed */
         /* at address $80020000 */
         { mem, scd.prg_ram,     0, SCD_BIT | 0x020000, 0, 0, 0x80000, "PRGRAM" },
         { mem, scd.word_ram_2M, 0,           0x200000, 0, 0, 0x40000, "WORDRAM" },
      };

      mmaps.descriptors = descs;
      mmaps.num_descriptors = sizeof(descs) / sizeof(descs[0]);
      environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
   }
}

/****************************************************************************
 * Disk control interface
 ****************************************************************************/ 
#define MAX_DISKS 4
static int disk_index;
static int disk_count;
static char *disk_info[MAX_DISKS];

static bool disk_set_eject_state(bool ejected)
{
  if (system_hw != SYSTEM_MCD)
    return false;

  if (ejected)
  {
    cdd.status = CD_OPEN;
    scd.regs[0x36>>1].byte.h = 0x01;
  }
  else if (cdd.status == CD_OPEN)
  {
    cdd.status = cdd.loaded ? CD_TOC : NO_DISC;
  }

  return true;
}

static bool disk_get_eject_state(void)
{
  if (system_hw != SYSTEM_MCD)
    return false;

  return (cdd.status == CD_OPEN);
}

static unsigned int disk_get_image_index(void)
{
  if ((system_hw != SYSTEM_MCD) || !cdd.loaded)
    return disk_count;

  return disk_index;
}

static bool disk_set_image_index(unsigned int index)
{
  char header[0x210];

  if (system_hw != SYSTEM_MCD)
    return false;

  if (index >= disk_count)
  {
    cdd.loaded = 0;
    return true;
  }

  if (disk_info[index] == NULL)
    return false;

  cdd_load(disk_info[index], header);

  if (!cdd.loaded)
    return false;

  disk_index = index;
  return true;
}

static unsigned int disk_get_num_images(void)
{
  return disk_count;
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
  if (system_hw != SYSTEM_MCD)
    return false;

  if (index >= disk_count)
    return false;

  if (disk_info[index] != NULL)
    free(disk_info[index]);

  disk_info[index] = NULL;

  if (info != NULL)
  {
    if (info->path == NULL)
      return false;

    disk_info[index] = strdup(info->path);

    if (index == disk_index)
      return disk_set_image_index(index);
  }
  else
  {
    int i = index;

    while (i < (disk_count-1))
    {
      disk_info[i]   = disk_info[i+1];
      disk_info[i+1] = NULL;
    }

    disk_count--;

    if (index < disk_index)
      disk_index--;
  }

  return true;
}

static bool disk_add_image_index(void)
{
  if (system_hw != SYSTEM_MCD)
    return false;

  if (disk_count >= MAX_DISKS)
    return false;

  disk_count++;
  return true;
}

static struct retro_disk_control_callback disk_ctrl =
{
  disk_set_eject_state,
  disk_get_eject_state,
  disk_get_image_index,
  disk_set_image_index,
  disk_get_num_images,
  disk_replace_image_index,
  disk_add_image_index
};

/************************************
 * libretro implementation
 ************************************/
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb)
{
   bool option_categories = false;
   struct retro_vfs_interface_info vfs_iface_info;
   struct retro_led_interface led_interface;

   static const struct retro_controller_description port_1[] = {
      { "Joypad Auto", RETRO_DEVICE_JOYPAD },
      { "Joypad Port Empty", RETRO_DEVICE_NONE },
      { "MD Joypad 3 Button", RETRO_DEVICE_MDPAD_3B },
      { "MD Joypad 6 Button", RETRO_DEVICE_MDPAD_6B },
      { "MS Joypad 2 Button", RETRO_DEVICE_MSPAD_2B },
      { "MD Joypad 3 Button + 4-WayPlay", RETRO_DEVICE_MDPAD_3B_WAYPLAY },
      { "MD Joypad 6 Button + 4-WayPlay", RETRO_DEVICE_MDPAD_6B_WAYPLAY },
      { "MD Joypad 3 Button + Teamplayer", RETRO_DEVICE_MDPAD_3B_TEAMPLAYER },
      { "MD Joypad 6 Button + Teamplayer", RETRO_DEVICE_MDPAD_6B_TEAMPLAYER },
      { "MS Joypad 2 Button + Master Tap", RETRO_DEVICE_MSPAD_2B_MASTERTAP },
      { "MS Light Phaser", RETRO_DEVICE_PHASER },
      { "MS Paddle Control", RETRO_DEVICE_PADDLE },
      { "MS Sports Pad", RETRO_DEVICE_SPORTSPAD },
      { "MS Graphic Board", RETRO_DEVICE_GRAPHIC_BOARD },
      { "MD XE-1AP", RETRO_DEVICE_XE_1AP },
      { "MD Mouse", RETRO_DEVICE_MOUSE },
   };

   static const struct retro_controller_description port_2[] = {
      { "Joypad Auto", RETRO_DEVICE_JOYPAD },
      { "Joypad Port Empty", RETRO_DEVICE_NONE },
      { "MD Joypad 3 Button", RETRO_DEVICE_MDPAD_3B },
      { "MD Joypad 6 Button", RETRO_DEVICE_MDPAD_6B },
      { "MS Joypad 2 Button", RETRO_DEVICE_MSPAD_2B },
      { "MD Joypad 3 Button + 4-WayPlay", RETRO_DEVICE_MDPAD_3B_WAYPLAY },
      { "MD Joypad 6 Button + 4-WayPlay", RETRO_DEVICE_MDPAD_6B_WAYPLAY },
      { "MD Joypad 3 Button + Teamplayer", RETRO_DEVICE_MDPAD_3B_TEAMPLAYER },
      { "MD Joypad 6 Button + Teamplayer", RETRO_DEVICE_MDPAD_6B_TEAMPLAYER },
      { "MS Joypad 2 Button + Master Tap", RETRO_DEVICE_MSPAD_2B_MASTERTAP },
      { "MD Menacer", RETRO_DEVICE_MENACER },
      { "MD Justifiers", RETRO_DEVICE_JUSTIFIERS },
      { "MS Light Phaser", RETRO_DEVICE_PHASER },
      { "MS Paddle Control", RETRO_DEVICE_PADDLE },
      { "MS Sports Pad", RETRO_DEVICE_SPORTSPAD },
      { "MS Graphic Board", RETRO_DEVICE_GRAPHIC_BOARD },
      { "MD XE-1AP", RETRO_DEVICE_XE_1AP },
      { "MD Mouse", RETRO_DEVICE_MOUSE },
  };

   static const struct retro_controller_info ports[] = {
      { port_1, 16 },
      { port_2, 18 },
      { 0 },
   };

   static const struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "C" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "A" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "X" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Mode" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 0 },
   };

   static const struct retro_system_content_info_override content_overrides[] = {
      {
         "mdx|md|bin|smd|gen|bms|sms|gg|sg|68k|sgd", /* extensions */
#if defined(LOW_MEMORY)
         true,                                   /* need_fullpath */
#else
         false,                                  /* need_fullpath */
#endif
         false                                   /* persistent_data */
      },
      { NULL, false, false }
   };

   environ_cb = cb;

   /* An annoyance: retro_set_environment() can be called
    * multiple times, and depending upon the current frontend
    * state various environment callbacks may be disabled.
    * This means the reported 'categories_supported' status
    * may change on subsequent iterations. We therefore have
    * to record whether 'categories_supported' is true on any
    * iteration, and latch the result */
   libretro_set_core_options(environ_cb, &option_categories);
   libretro_supports_option_categories |= option_categories;

   /* If frontend supports core option categories,
    * genesis_plus_gx_show_advanced_audio_settings
    * is unused and should be hidden */
   if (libretro_supports_option_categories)
   {
      struct retro_core_option_display option_display;

      option_display.visible = false;
      option_display.key     = "genesis_plus_gx_show_advanced_audio_settings";

      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
            &option_display);
   }

   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
   cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
   cb(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE, (void*)content_overrides);

   vfs_iface_info.required_interface_version = 2;
   vfs_iface_info.iface                      = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
	   filestream_vfs_init(&vfs_iface_info);

   if (environ_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led_interface)) {
      if (led_interface.set_led_state && !led_state_cb)
         led_state_cb = led_interface.set_led_state;
   }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }


void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "Genesis Plus GX";
   info->library_version = "v1.7.4" GIT_VERSION;
   info->valid_extensions = "m3u|mdx|md|smd|gen|bin|cue|iso|chd|bms|sms|gg|sg|68k|sgd";
   info->block_extract = false;
   info->need_fullpath = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   int max_border_width       = 14 * 2;
   info->geometry.base_width  = vwidth;
   info->geometry.base_height = vheight;

   /* Set maximum dimensions based upon emulated system/config */
   if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
   {
      /* 16 bit system */
      if (config.ntsc) {
         info->geometry.max_width = MD_NTSC_OUT_WIDTH(320 + max_border_width);
      } else {
         info->geometry.max_width = 320 + max_border_width;
      }
      if (config.render) {
         info->geometry.max_height = 480 + (vdp_pal * 96);
      } else {
         info->geometry.max_height = 240 + (vdp_pal * 48);
      }
   }
   else
   {
      /* 8 bit system */
      if (config.ntsc) {
         info->geometry.max_width = SMS_NTSC_OUT_WIDTH(256 + max_border_width);
      } else {
         info->geometry.max_width = 256 + max_border_width;
      }
      info->geometry.max_height = 240 + (vdp_pal * 48);
   }

   info->geometry.aspect_ratio  = vaspect_ratio;
   info->timing.fps             = (double)(system_clock) / (double)lines_per_frame / (double)MCYCLES_PER_LINE;
   info->timing.sample_rate     = SOUND_FREQUENCY;

   if (!retro_fps)
      retro_fps = info->timing.fps;
   if (!max_width)
      max_width = info->geometry.max_width;
   if (!max_height)
      max_height = info->geometry.max_height;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (port > 1)
     return;

   switch(device)
   {
      case RETRO_DEVICE_NONE:
         input.system[port] = NO_SYSTEM;
         break;
      case RETRO_DEVICE_MDPAD_3B:
      {
         if (port && (input.system[0] >= SYSTEM_MASTERTAP) && (input.system[0] <= SYSTEM_WAYPLAY))
            config.input[4].padtype = DEVICE_PAD3B;
         else
            config.input[port].padtype = DEVICE_PAD3B;
         input.system[port] = SYSTEM_GAMEPAD;
         break;
      }
      case RETRO_DEVICE_MDPAD_6B:
      {
         if (port && (input.system[0] >= SYSTEM_MASTERTAP) && (input.system[0] <= SYSTEM_WAYPLAY))
            config.input[4].padtype = DEVICE_PAD6B;
         else
            config.input[port].padtype = DEVICE_PAD6B;
         input.system[port] = SYSTEM_GAMEPAD;
         break;
      }
      case RETRO_DEVICE_MSPAD_2B:
      {
         if (port && (input.system[0] >= SYSTEM_MASTERTAP) && (input.system[0] <= SYSTEM_WAYPLAY))
            config.input[4].padtype = DEVICE_PAD2B;
         else
            config.input[port].padtype = DEVICE_PAD2B;
         input.system[port] = SYSTEM_GAMEPAD;
         break;
      }
      case RETRO_DEVICE_MDPAD_3B_WAYPLAY:
      {
         int i;
         for (i=0; i<4; i++)
         {
            config.input[i].padtype = DEVICE_PAD3B;
         }
         input.system[0] = input.system[1] = SYSTEM_WAYPLAY;
         break;
      }
      case RETRO_DEVICE_MDPAD_6B_WAYPLAY:
      {
         int i;
         for (i=0; i<4; i++)
         {
            config.input[i].padtype = DEVICE_PAD6B;
         }
         input.system[0] = input.system[1] = SYSTEM_WAYPLAY;
         break;
      }
      case RETRO_DEVICE_MDPAD_3B_TEAMPLAYER:
      {
         int i;
         for (i=0; i<4; i++)
         {
            config.input[port*4 + i].padtype = DEVICE_PAD3B;
         }
         input.system[port] = SYSTEM_TEAMPLAYER;
         break;
      }
      case RETRO_DEVICE_MDPAD_6B_TEAMPLAYER:
      {
         int i;
         for (i=0; i<4; i++)
         {
            config.input[port*4 + i].padtype = DEVICE_PAD6B;
         }
         input.system[port] = SYSTEM_TEAMPLAYER;
         break;
      }
      case RETRO_DEVICE_MSPAD_2B_MASTERTAP:
      {
         int i;
         for (i=0; i<4; i++)
         {
            config.input[port*4 + i].padtype = DEVICE_PAD2B;
         }
         input.system[port] = SYSTEM_MASTERTAP;
         break;
      }
      case RETRO_DEVICE_MENACER:
         input.system[1] = SYSTEM_MENACER;
         break;
      case RETRO_DEVICE_JUSTIFIERS:
         input.system[1] = SYSTEM_JUSTIFIER;
         break;
      case RETRO_DEVICE_PHASER:
         input.system[port] = SYSTEM_LIGHTPHASER;
         break;
      case RETRO_DEVICE_PADDLE:
         input.system[port] = SYSTEM_PADDLE;
         break;
      case RETRO_DEVICE_SPORTSPAD:
         input.system[port] = SYSTEM_SPORTSPAD;
         break;
      case RETRO_DEVICE_XE_1AP:
         input.system[port] = SYSTEM_XE_1AP;
         break;
      case RETRO_DEVICE_MOUSE:
         input.system[port] = SYSTEM_MOUSE;
         break;
      case RETRO_DEVICE_GRAPHIC_BOARD:
         input.system[port] = SYSTEM_GRAPHIC_BOARD;
         break;
      case RETRO_DEVICE_JOYPAD:
      default:
      {
         if (port && (input.system[0] >= SYSTEM_MASTERTAP) && (input.system[0] <= SYSTEM_WAYPLAY))
            config.input[4].padtype = DEVICE_PAD2B | DEVICE_PAD6B | DEVICE_PAD3B;
         else
            config.input[port].padtype = DEVICE_PAD2B | DEVICE_PAD6B | DEVICE_PAD3B;
         input.system[port] = SYSTEM_GAMEPAD;
         break;
      }
   }

   old_system[0] = input.system[0];
   old_system[1] = input.system[1];
   
   io_init();
   input_reset();
}

size_t retro_serialize_size(void) { return STATE_SIZE; }

/* AYTHER: guard de layout del savestate.
 *
 * El formato de upstream es un volcado crudo de memoria: `save_param` es un
 * memcpy, y varios structs se vuelcan enteros con sizeof. Z80_Regs mide 88
 * bytes en x64 y 76 en x86 porque lleva DOS PUNTEROS A FUNCION adentro
 * (`daisy`, `irq_callback`), asi que un estado escrito por un binario no se
 * puede leer con otro de distinto ABI: desde el primer struct con puntero en
 * adelante, todo sale corrido.
 *
 * Y hasta aca NADA lo detectaba. STATE_SIZE es 0xfd000 hardcodeado, identico
 * en las dos arquitecturas, y la firma "GENPLUS-GX 1.7.7" tambien: el estado
 * pasaba las dos validaciones y se cargaba mal EN SILENCIO. Ese es el peor
 * modo de falla posible; no ves un error, ves un emulador que se porta raro.
 *
 * El tag va en un offset FIJO contado desde el FINAL del buffer, no despues de
 * lo que escribio state_save. Es deliberado: si el estado viene de otro ABI, a
 * esa altura el offset de lectura ya esta corrido, y leeriamos el tag del
 * lugar equivocado. STATE_SIZE reserva 1.036.288 bytes y se usan ~144.000, o
 * sea que la cola esta libre.
 *
 * Un estado SIN tag se acepta: son los que escribieron las versiones previas,
 * todas x64. El build de 32 bits se publico recien, asi que en la practica no
 * hay estados x86 sin tag dando vueltas, y de aca en mas todo lo que
 * escribimos queda tageado. */
#define AYTHER_STATE_TAG_MAGIC  UINT32_C(0x53535941)  /* "AYSS" */
#define AYTHER_STATE_TAG_BYTES  16
#define AYTHER_STATE_TAG_OFFSET (STATE_SIZE - AYTHER_STATE_TAG_BYTES)

/* Huella del layout de ESTE binario. sizeof(void*) es la causa raiz; los
 * structs que se vuelcan enteros van tambien, asi que un cambio de packing o
 * de compilador se detecta igual. */
static uint32_t ayther_state_layout_id(void)
{
   const uint32_t parts[4] = {
      (uint32_t)sizeof(void *),
      (uint32_t)sizeof(Z80_Regs),
      (uint32_t)sizeof(m68ki_cpu_core),
      (uint32_t)sizeof(cart_hw_t)
   };
   const unsigned char *bytes = (const unsigned char *)parts;
   uint32_t h = UINT32_C(2166136261);
   size_t i;

   for (i = 0; i < sizeof(parts); i++)
   {
      h ^= (uint32_t)bytes[i];
      h *= UINT32_C(16777619);
   }

   return h;
}

static void ayther_state_tag_write(void *data)
{
   unsigned char *p = (unsigned char *)data + AYTHER_STATE_TAG_OFFSET;
   uint32_t magic  = AYTHER_STATE_TAG_MAGIC;
   uint32_t layout = ayther_state_layout_id();

   memset(p, 0, AYTHER_STATE_TAG_BYTES);
   memcpy(p, &magic, sizeof(magic));
   memcpy(p + sizeof(magic), &layout, sizeof(layout));
}

/* FALSE solo si HAY tag y no coincide. Sin tag se acepta, por lo de arriba. */
static bool ayther_state_tag_ok(const void *data)
{
   const unsigned char *p = (const unsigned char *)data + AYTHER_STATE_TAG_OFFSET;
   uint32_t magic  = 0;
   uint32_t layout = 0;

   memcpy(&magic, p, sizeof(magic));
   if (magic != AYTHER_STATE_TAG_MAGIC)
      return TRUE;

   memcpy(&layout, p + sizeof(magic), sizeof(layout));
   return layout == ayther_state_layout_id();
}

extern int8 fast_savestates;

bool get_fast_savestates(void)
{
   int result = -1;
   bool okay = false;
   okay = environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &result);
   if (okay)
   {
      return 0 != (result & 4);
   }
   else
   {
      return 0;
   }
}

bool retro_serialize(void *data, size_t size)
{ 
   fast_savestates = get_fast_savestates();
   if (size != STATE_SIZE)
      return FALSE;

   state_save(data);
   ayther_state_tag_write(data);
   if (fast_savestates) save_sound_buffer();

   return TRUE;
}

bool retro_unserialize(const void *data, size_t size)
{
   fast_savestates = get_fast_savestates();
   if (size != STATE_SIZE)
      return FALSE;

   /* Antes que state_load: si el layout no es el nuestro, lo que sigue seria
      una lectura corrida y silenciosa. */
   if (!ayther_state_tag_ok(data))
      return FALSE;

   if (!state_load((uint8_t*)data))
      return FALSE;

   if (fast_savestates) restore_sound_buffer();

#ifdef HAVE_OVERCLOCK
   update_overclock();
#endif

   ayther_invalidate_snapshot();

   return TRUE;
}

void retro_cheat_reset(void)
{
   /* clear existing ROM patches */
   clear_cheats();
   /* delete all cheats */
   maxcheats = maxROMcheats = maxRAMcheats = 0;
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	char codeCopy[256];
	char *buff;

	/* Avoid crashing when giving no input */
	if (code==NULL) return;

	/* clear existing ROM patches */
	clear_cheats();

	/* Detect and split multiline cheats */
	strncpy(codeCopy,code,255);
	codeCopy[255] = '\0';
	buff = strtok(codeCopy,"+");

	while (buff != NULL)
	{
      /* interpret code and check if this is a valid cheat code */
      if (decode_cheat((char *)buff, maxcheats))
      {
         int i;

         /* check if cheat code already exists */
         for (i=0; i<maxcheats; i++)
         {
            if ((cheatlist[i].address == cheatlist[maxcheats].address)
                   && (cheatlist[i].data == cheatlist[maxcheats].data))
               break;
         }

         /* cheat can be enabled or disabled */
         cheatlist[i].enable = enabled;

         /* if new cheat code, check current cheat count */
         if ((i == maxcheats) && (i < MAX_CHEATS))
         {
            /* increment cheat count */
            maxcheats++;
         }
      }
      buff = strtok(NULL,"+");
	}

	/* apply ROM patches */
	apply_cheats();
}

bool retro_load_game(const struct retro_game_info *info)
{
   int i;
   char *dir       = NULL;
#if defined(_WIN32)
   char slash      = '\\';
#else
   char slash      = '/';
#endif
   struct retro_game_info_ext *info_ext = NULL;
   char content_path[256];
   char content_ext[8];

   content_path[0] = '\0';
   content_ext[0]  = '\0';

   system_hw       = 0;
   g_rom_data      = NULL;
   g_rom_size      = 0;

   /* Attempt to fetch extended game info */
   if (environ_cb(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &info_ext))
   {
#if !defined(LOW_MEMORY)
      g_rom_data = (const void *)info_ext->data;
      g_rom_size = info_ext->size;
#endif
      strncpy(g_rom_dir, info_ext->dir, sizeof(g_rom_dir));
      g_rom_dir[sizeof(g_rom_dir) - 1] = '\0';

      strncpy(g_rom_name, info_ext->name, sizeof(g_rom_name));
      g_rom_name[sizeof(g_rom_name) - 1] = '\0';

      strncpy(content_ext, info_ext->ext, sizeof(content_ext));
      content_ext[sizeof(content_ext) - 1] = '\0';

      if (info_ext->file_in_archive)
      {
         /* We don't have a 'physical' file in this
          * case, but the core still needs a filename
          * in order to detect associated content
          * (Mega CD Mode 1/MegaSD MD+ mode). We therefore
          * fake it, using the content directory,
          * canonical content name, and content file
          * extension */
         snprintf(content_path, sizeof(content_path), "%s%c%s.%s",
               g_rom_dir, slash, g_rom_name, content_ext);
      }
      else
      {
         strncpy(content_path, info_ext->full_path, sizeof(content_path));
         content_path[sizeof(content_path) - 1] = '\0';
      }
   }
   else
   {
      const char *ext = NULL;

      if (!info || !info->path)
         goto error;

      extract_directory(g_rom_dir, info->path, sizeof(g_rom_dir));
      extract_name(g_rom_name, info->path, sizeof(g_rom_name));

      strncpy(content_path, info->path, sizeof(content_path));
      content_path[sizeof(content_path) - 1] = '\0';

      if (ext = strrchr(info->path, '.'))
      {
         strncpy(content_ext, ext + 1, sizeof(content_ext));
         content_ext[sizeof(content_ext) - 1] = '\0';
      }
   }

#ifdef FRONTEND_SUPPORTS_RGB565
   {
      unsigned rgb565 = RETRO_PIXEL_FORMAT_RGB565;
      if(environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565))
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
   }
#endif

   sms_ntsc = calloc(1, sizeof(sms_ntsc_t));
   md_ntsc  = calloc(1, sizeof(md_ntsc_t));

   init_bitmap();
   config_default();

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir)
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "[genplus]: Defaulting system directory to %s.\n", g_rom_dir);
      dir = g_rom_dir;
   }

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) || !save_dir)
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "[genplus]: Defaulting save directory to %s.\n", g_rom_dir);
      save_dir = g_rom_dir;
   }

   fill_pathname_join(GG_ROM,     dir, "ggenie.bin",    sizeof(GG_ROM));
   fill_pathname_join(AR_ROM,     dir, "areplay.bin",   sizeof(AR_ROM));
   fill_pathname_join(SK_ROM,     dir, "sk.bin",        sizeof(SK_ROM));
   fill_pathname_join(SK_UPMEM,   dir, "sk2chip.bin",   sizeof(SK_UPMEM));
   fill_pathname_join(MD_BIOS,    dir, "bios_MD.bin",   sizeof(MD_BIOS));
   fill_pathname_join(GG_BIOS,    dir, "bios.gg",       sizeof(GG_BIOS));
   fill_pathname_join(MS_BIOS_EU, dir, "bios_E.sms",    sizeof(MS_BIOS_EU));
   fill_pathname_join(MS_BIOS_US, dir, "bios_U.sms",    sizeof(MS_BIOS_US));
   fill_pathname_join(MS_BIOS_JP, dir, "bios_J.sms",    sizeof(MS_BIOS_JP));
   fill_pathname_join(CD_BIOS_EU, dir, "bios_CD_E.bin", sizeof(CD_BIOS_EU));
   fill_pathname_join(CD_BIOS_US, dir, "bios_CD_U.bin", sizeof(CD_BIOS_US));
   fill_pathname_join(CD_BIOS_JP, dir, "bios_CD_J.bin", sizeof(CD_BIOS_JP));
 
   check_variables(true);

   if (log_cb)
   {
      log_cb(RETRO_LOG_DEBUG, "Game Genie ROM should be located at: %s\n", GG_ROM);
      log_cb(RETRO_LOG_DEBUG, "Action Replay (Pro) ROM should be located at: %s\n", AR_ROM);
      log_cb(RETRO_LOG_DEBUG, "Sonic & Knuckles (2 MB) ROM should be located at: %s\n", SK_ROM);
      log_cb(RETRO_LOG_DEBUG, "Sonic & Knuckles UPMEM (256 KB) ROM should be located at: %s\n", SK_UPMEM);
      log_cb(RETRO_LOG_DEBUG, "Mega Drive TMSS BOOTROM should be located at: %s\n", MD_BIOS);
      log_cb(RETRO_LOG_DEBUG, "Game Gear TMSS BOOTROM should be located at: %s\n", GG_BIOS);
      log_cb(RETRO_LOG_DEBUG, "Master System (PAL) BOOTROM should be located at: %s\n", MS_BIOS_EU);
      log_cb(RETRO_LOG_DEBUG, "Master System (NTSC-U) BOOTROM should be located at: %s\n", MS_BIOS_US);
      log_cb(RETRO_LOG_DEBUG, "Master System (NTSC-J) BOOTROM should be located at: %s\n", MS_BIOS_JP);
      log_cb(RETRO_LOG_DEBUG, "Mega CD (PAL) BIOS should be located at: %s\n", CD_BIOS_EU);
      log_cb(RETRO_LOG_DEBUG, "Sega CD (NTSC-U) BIOS should be located at: %s\n", CD_BIOS_US);
      log_cb(RETRO_LOG_DEBUG, "Mega CD (NTSC-J) BIOS should be located at: %s\n", CD_BIOS_JP);
      log_cb(RETRO_LOG_DEBUG, "Mega CD (PAL) BRAM is located at: %s\n", CD_BRAM_EU);
      log_cb(RETRO_LOG_DEBUG, "Sega CD (NTSC-U) BRAM is located at: %s\n", CD_BRAM_US);
      log_cb(RETRO_LOG_DEBUG, "Mega CD (NTSC-J) BRAM is located at: %s\n", CD_BRAM_JP);
      log_cb(RETRO_LOG_DEBUG, "Sega/Mega CD RAM CART is located at: %s\n", CART_BRAM);
   }

   /* Clear disk interface (already done in retro_unload_game but better be safe) */
   disk_count = 0;
   disk_index = 0;
   for (i=0; i<MAX_DISKS; i++)
   {
      if (disk_info[i] != NULL)
      {
         free(disk_info[i]);
         disk_info[i] = NULL;
      }
   }

   /* M3U file list support */
   if (!strcmp(content_ext, "m3u"))
   {
      RFILE *fd = filestream_open(content_path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (fd)
      {
         int len;
         char linebuf[512];
         while ((filestream_gets(fd, linebuf, 512) != NULL) && (disk_count < MAX_DISKS))
         {
            /* skip commented lines */
            if (linebuf[0] != '#')
            {
               len = strlen(linebuf);
               if (len > 0)
               {
                  /* remove end-of-line character */
                  if (linebuf[len-1] == '\n')
                  {
                     linebuf[len-1] = 0;
                     len--;
                  }

                  /* remove carriage-return character */
                  if (len && (linebuf[len-1] == '\r'))
                  {
                     linebuf[len-1] = 0;
                     len--;
                  }

                  /* append file path to disk image file name */
                  disk_info[disk_count] = malloc(strlen(g_rom_dir) + len + 2);
                  if (disk_info[disk_count] != NULL)
                  {
                     /* add file to disk interface */
                     sprintf(disk_info[disk_count], "%s%c%s", g_rom_dir, slash, linebuf);
                     disk_count++;
                     if (log_cb)
                       log_cb(RETRO_LOG_INFO, "Disk #%d added from M3U file list: %s\n", disk_count, disk_info[disk_count-1]);
                  }
               }
            }
         }
      }

      /* automatically try to load first disk if at least one file was added to disk interface from M3U file list */
      if (disk_count > 0)
      {
         if (!load_rom(disk_info[0]))
         {
            if (log_cb)
               log_cb(RETRO_LOG_ERROR, "Could not load %s from M3U file list\n", disk_info[0]);

            /* clear initialized disk interface before returning an error */
            for (i=0; i<disk_count; i++)
            {
               if (disk_info[i] != NULL)
               {
                  free(disk_info[i]);
                  disk_info[i] = NULL;
               }
            }
            disk_count = 0;
            goto error;
         }
      }
      else
      {
         /* error adding disks from M3U */
         goto error;
      }
   }
   else
   {
      if (load_rom(content_path) <= 0)
         goto error;

      /* automatically add loaded CD to disk interface */
      if ((system_hw == SYSTEM_MCD) && cdd.loaded)
      {
         disk_count = 1;
         disk_info[0] = strdup(content_path);
      }
   }

   if ((config.bios & 1) && !(system_bios & SYSTEM_MD))
   {
      memset(boot_rom, 0xFF, 0x800);
      if (load_archive(MD_BIOS, boot_rom, 0x800, NULL) > 0)
      {
         if (!memcmp((char *)(boot_rom + 0x120),"GENESIS OS", 10))
         {
            system_bios |= SYSTEM_MD;
         }

#ifdef LSB_FIRST
         for (i=0; i<0x800; i+=2)
         {
            uint8 temp = boot_rom[i];
            boot_rom[i] = boot_rom[i+1];
            boot_rom[i+1] = temp;
         }
#endif
      }
   }

   audio_init(SOUND_FREQUENCY, 0);
   system_init();
   system_reset();
   is_running = false;

   if (system_hw == SYSTEM_MCD)
      bram_load();
   else
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, NULL);

   update_viewport();

#ifdef HAVE_OVERCLOCK
   overclock_delay = OVERCLOCK_FRAME_DELAY;
   update_overclock();
#endif

   set_memory_maps();

   init_frameskip();

   ayther_reset_session(true);

   return true;

error:
   if (sms_ntsc)
      free(sms_ntsc);
   sms_ntsc  = NULL;

   if (md_ntsc)
      free(md_ntsc);
   md_ntsc   = NULL;

   system_hw = 0;
   ayther_reset_session(false);

   return false;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   (void)game_type;
   (void)info;
   (void)num_info;
   return FALSE;
}

void retro_unload_game(void) 
{
	/* Clear disk interface */
   int i;
   disk_count = 0;
   disk_index = 0;
   for (i=0; i<MAX_DISKS; i++)
   {
      if (disk_info[i] != NULL)
      {
         free(disk_info[i]);
         disk_info[i] = NULL;
      }
   }

   if (system_hw == SYSTEM_MCD)
      bram_save();

   audio_shutdown();

   if (md_ntsc)
      free(md_ntsc);
   md_ntsc   = NULL;
   if (sms_ntsc)
      free(sms_ntsc);
   sms_ntsc  = NULL;

   system_hw = 0;
   is_running = false;
   ayther_reset_session(false);
}

unsigned retro_get_region(void) { return vdp_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC; }

#ifdef AYTHER_EXTENSIONS
typedef struct ayther_region_mapping
{
   void *data;
   uint32_t data_version;
   uint32_t element_size;
   uint32_t capacity;
   uint32_t byte_size;
   uint32_t access_flags;
   uint32_t legacy_memory_id;
} ayther_region_mapping;

static int32_t ayther_map_region(uint32_t region_id,
      ayther_region_mapping *mapping)
{
   memset(mapping, 0, sizeof(*mapping));
   mapping->data_version = AYTHER_LAYOUT_RAW_V1;
   mapping->access_flags = AYTHER_REGION_ACCESS_READ |
      AYTHER_REGION_DEPRECATED_LEGACY;

   switch (region_id)
   {
      case AYTHER_REGION_VRAM:
         mapping->data = vram;
         mapping->element_size = 1;
         mapping->capacity = sizeof(vram);
         mapping->byte_size = sizeof(vram);
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_VRAM;
#ifdef LSB_FIRST
         /* #32: la VRAM sale en el layout interno del emulador, no en orden
            logico: el byte `off` esta en `off ^ 1`. Declararlo en el descriptor
            es lo unico que le permite a un consumidor saberlo sin leer la
            documentacion en prosa. */
         mapping->access_flags |= AYTHER_REGION_WORD_SWAPPED_LE;
#endif
         break;
      case AYTHER_REGION_CRAM:
         mapping->data = cram;
         mapping->element_size = sizeof(cram[0]);
         mapping->capacity = sizeof(cram) / sizeof(cram[0]);
         mapping->byte_size = sizeof(cram);
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_CRAM;
         break;
      case AYTHER_REGION_VDP_REGS:
         mapping->data = reg;
         mapping->element_size = sizeof(reg[0]);
         mapping->capacity = sizeof(reg) / sizeof(reg[0]);
         mapping->byte_size = sizeof(reg);
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_VDP_REGS;
         break;
      case AYTHER_REGION_VSRAM:
         mapping->data = vsram;
         mapping->element_size = sizeof(vsram[0]);
         mapping->capacity = sizeof(vsram) / sizeof(vsram[0]);
         mapping->byte_size = sizeof(vsram);
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_VSRAM;
         break;
      case AYTHER_REGION_LAYER_MASK:
         mapping->data = &ayther_layer_mask;
         mapping->element_size = sizeof(ayther_layer_mask);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_layer_mask);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_LAYER_MASK;
         break;
      case AYTHER_REGION_SPRITE_SUPPRESS:
         mapping->data = ayther_sprite_suppress;
         mapping->element_size = sizeof(ayther_sprite_suppress[0]);
         mapping->capacity = sizeof(ayther_sprite_suppress);
         mapping->byte_size = sizeof(ayther_sprite_suppress);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_SPRITE_SUPPRESS;
         break;
      case AYTHER_REGION_TILE_SUPPRESS:
         mapping->data = ayther_tile_suppress;
         mapping->element_size = sizeof(ayther_tile_suppress[0]);
         mapping->capacity = sizeof(ayther_tile_suppress);
         mapping->byte_size = sizeof(ayther_tile_suppress);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_TILE_SUPPRESS;
         break;
      case AYTHER_REGION_PLANE_TILE_SUPPRESS:
         mapping->data = ayther_plane_tile_suppress;
         mapping->element_size = sizeof(ayther_plane_tile_suppress[0]);
         mapping->capacity = sizeof(ayther_plane_tile_suppress);
         mapping->byte_size = sizeof(ayther_plane_tile_suppress);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_PLANE_TILE_SUPPRESS;
         break;
      case AYTHER_REGION_PLANE_SUPPRESS_ACTIVE:
         mapping->data = &ayther_plane_suppress_active;
         mapping->element_size = sizeof(ayther_plane_suppress_active);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_plane_suppress_active);
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_PLANE_SUPPRESS_ACTIVE;
         break;
      case AYTHER_REGION_LAYER_DIM:
         mapping->data = &ayther_layer_dim;
         mapping->element_size = sizeof(ayther_layer_dim);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_layer_dim);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_LAYER_DIM;
         break;
      case AYTHER_REGION_AUDIO_WRITES:
         mapping->data = ayther_audio_writes;
         mapping->data_version = AYTHER_LAYOUT_AUDIO_WRITE_V1;
         mapping->element_size = sizeof(ayther_audio_writes[0]);
         mapping->capacity = sizeof(ayther_audio_writes) /
            sizeof(ayther_audio_writes[0]);
         mapping->byte_size = sizeof(ayther_audio_writes);
         mapping->access_flags |= AYTHER_REGION_FRAME_SCOPED;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_AUDIO_WRITES;
         break;
      case AYTHER_REGION_AUDIO_WRITE_COUNT:
         mapping->data = &ayther_audio_write_n;
         mapping->element_size = sizeof(ayther_audio_write_n);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_audio_write_n);
         mapping->access_flags |= AYTHER_REGION_FRAME_SCOPED;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_AUDIO_WRITE_COUNT;
         break;
      case AYTHER_REGION_PARSED_SPRITES:
         mapping->data = ayther_sprites;
         mapping->data_version = AYTHER_LAYOUT_SPRITE_V1;
         mapping->element_size = sizeof(ayther_sprites[0]);
         mapping->capacity = sizeof(ayther_sprites) / sizeof(ayther_sprites[0]);
         mapping->byte_size = sizeof(ayther_sprites);
         mapping->access_flags |= AYTHER_REGION_FRAME_SCOPED;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_PARSED_SPRITES;
         break;
      case AYTHER_REGION_PARSED_SPRITE_COUNT:
         mapping->data = &ayther_sprite_n;
         mapping->element_size = sizeof(ayther_sprite_n);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_sprite_n);
         mapping->access_flags |= AYTHER_REGION_FRAME_SCOPED;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_PARSED_SPRITE_COUNT;
         break;
      case AYTHER_REGION_AUDIO_MUTE:
         mapping->data = &ayther_audio_mute;
         mapping->element_size = sizeof(ayther_audio_mute);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_audio_mute);
         mapping->access_flags |= AYTHER_REGION_ACCESS_CONTROL_WRITE;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_AUDIO_MUTE;
         break;
      case AYTHER_REGION_ATTRIBUTION:
         /* #41: se expone del tamano REAL del frame emitido, no del maximo del
            buffer: leer 320x240 cuando el viewport es 256x224 devolveria filas
            de otro frame. Fuera de suscripcion la region existe pero
            read_region la rechaza con NOT_SUBSCRIBED. */
         mapping->data = ayther_attrib;
         mapping->element_size = 1;
         mapping->capacity = ayther_attrib_width * ayther_attrib_height;
         mapping->byte_size = ayther_attrib_width * ayther_attrib_height;
         mapping->data_version = AYTHER_LAYOUT_ATTRIBUTION_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_LINE_REGS:
         /* #42: se arma al leer, como el descriptor de sistema y por la misma
            razon: son hasta 7,5 KB que nadie mira en la mayoria de los frames. */
         ayther_fill_line_regs();
         /* #42/#55: `byte_size == element_size * capacity` es una promesa
            que la ABI publica para TODA region, y estas tres la rompian: el
            buffer es una cabecera de 24 B seguida de N entradas, y ningun par
            (tamanio, cantidad) describe eso. Un consumidor que dimensionara
            su buffer con la multiplicacion se quedaba 24 bytes corto, y sin
            suscripcion la region declaraba capacity=0 con byte_size=24.

            Se declaran como bytes crudos, igual que VRAM: el LAYOUT lo dice
            `data_version`, y `entry_size` y `lines` viajan DENTRO de la
            cabecera, que es donde el consumidor ya los tenia que leer. No se
            pierde informacion; se deja de mentir sobre la forma. */
         mapping->data = ayther_line_regs_buf;
         mapping->element_size = 1;
         mapping->capacity = ayther_line_regs_bytes;
         mapping->byte_size = ayther_line_regs_bytes;
         mapping->data_version = AYTHER_LAYOUT_LINE_REGS_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_LINE_CRAM:
         ayther_fill_line_cram();
         /* #42/#55: `byte_size == element_size * capacity` es una promesa
            que la ABI publica para TODA region, y estas tres la rompian: el
            buffer es una cabecera de 24 B seguida de N entradas, y ningun par
            (tamanio, cantidad) describe eso. Un consumidor que dimensionara
            su buffer con la multiplicacion se quedaba 24 bytes corto, y sin
            suscripcion la region declaraba capacity=0 con byte_size=24.

            Se declaran como bytes crudos, igual que VRAM: el LAYOUT lo dice
            `data_version`, y `entry_size` y `lines` viajan DENTRO de la
            cabecera, que es donde el consumidor ya los tenia que leer. No se
            pierde informacion; se deja de mentir sobre la forma. */
         mapping->data = ayther_line_cram_buf;
         mapping->element_size = 1;
         mapping->capacity = ayther_line_cram_bytes;
         mapping->byte_size = ayther_line_cram_bytes;
         mapping->data_version = AYTHER_LAYOUT_LINE_CRAM_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_LINE_CELLS:
         ayther_fill_line_cells();
         /* #42/#55: `byte_size == element_size * capacity` es una promesa
            que la ABI publica para TODA region, y estas tres la rompian: el
            buffer es una cabecera de 24 B seguida de N entradas, y ningun par
            (tamanio, cantidad) describe eso. Un consumidor que dimensionara
            su buffer con la multiplicacion se quedaba 24 bytes corto, y sin
            suscripcion la region declaraba capacity=0 con byte_size=24.

            Se declaran como bytes crudos, igual que VRAM: el LAYOUT lo dice
            `data_version`, y `entry_size` y `lines` viajan DENTRO de la
            cabecera, que es donde el consumidor ya los tenia que leer. No se
            pierde informacion; se deja de mentir sobre la forma. */
         mapping->data = ayther_line_cells_buf;
         mapping->element_size = 1;
         mapping->capacity = ayther_line_cells_bytes;
         mapping->byte_size = ayther_line_cells_bytes;
         mapping->data_version = AYTHER_LAYOUT_LINE_CELLS_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         /* `name_pair` es la carga de 32 bits tal cual la hizo el renderer
            sobre la VRAM, y en un host little-endian esa VRAM esta guardada
            con los bytes intercambiados. Es la misma convencion que la region
            VRAM ya declara con este flag: decirlo aca es lo que evita que un
            consumidor la lea al reves y no se entere. */
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_WORD_SWAPPED_LE;
         break;
      case AYTHER_REGION_SYSTEM:
         /* #39.B: descriptor de solo lectura. Se rellena aca, en la consulta,
            y no una vez por frame: ver la nota en ayther_fill_system. */
         ayther_fill_system();
         mapping->data = &ayther_system_desc;
         mapping->element_size = sizeof(ayther_system_desc);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_system_desc);
         mapping->data_version = AYTHER_LAYOUT_SYSTEM_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_RASTER_JOURNAL:
         /* #39.A: se copia aca, en la consulta. */
         ayther_fill_journal();
         mapping->data = &ayther_journal_desc;
         mapping->element_size = sizeof(ayther_journal_desc);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_journal_desc);
         mapping->data_version = AYTHER_LAYOUT_JOURNAL_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_FRAME_HASH:
         /* #39.D: ya calculado al cerrar el frame; aca solo se entrega. */
         mapping->data = &ayther_frame_hash_desc;
         mapping->element_size = sizeof(ayther_frame_hash_desc);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_frame_hash_desc);
         mapping->data_version = AYTHER_LAYOUT_FRAME_HASH_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_PALETTE:
         /* #39.E: la LUT del renderer, tal cual. El ancho de la entrada lo
            dice element_size porque depende del formato del build. */
         mapping->data = (void *)ayther_palette_data();
         mapping->element_size = ayther_palette_entry_size();
         mapping->capacity = ayther_palette_entries();
         mapping->byte_size = ayther_palette_entry_size() *
                              ayther_palette_entries();
         mapping->data_version = AYTHER_LAYOUT_PALETTE_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_SPRITE_OUTCOME:
         /* #39.C: un byte por slot de la SAT, con los bits acumulados del
            frame. El indice es el slot y no el orden de la cadena: el orden
            cambia entre frames, el slot no. */
         mapping->data = ayther_spr_outcome;
         mapping->element_size = 1;
         mapping->capacity = sizeof(ayther_spr_outcome);
         mapping->byte_size = sizeof(ayther_spr_outcome);
         mapping->data_version = AYTHER_LAYOUT_SPR_OUTCOME_V1;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_NONE;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_FRAME_SCOPED | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      /* #563 (ABI 1.9): la RAM del Z80, 8 KB. ESCRIBIBLE: el caso de uso es
         dejar un id de sonido donde el Z80 lo va a leer. No es frame-scoped --
         la RAM persiste entre frames como cualquier otra. */
      case AYTHER_REGION_Z80_RAM:
         mapping->data = zram;
         mapping->element_size = 1;
         mapping->capacity = sizeof(zram);
         mapping->byte_size = sizeof(zram);
         mapping->legacy_memory_id = 0x10F;
         mapping->access_flags = AYTHER_REGION_ACCESS_READ |
            AYTHER_REGION_ACCESS_CONTROL_WRITE | AYTHER_REGION_NATIVE_ENDIAN;
         break;
      case AYTHER_REGION_RASTER_FALLBACK_REASONS:
         mapping->data = &ayther_raster_dirty;
         mapping->element_size = sizeof(ayther_raster_dirty);
         mapping->capacity = 1;
         mapping->byte_size = sizeof(ayther_raster_dirty);
         mapping->access_flags |= AYTHER_REGION_FRAME_SCOPED;
         mapping->legacy_memory_id = AYTHER_LEGACY_MEMORY_RASTER_DIRTY;
         break;
      default:
         return AYTHER_STATUS_NOT_FOUND;
   }

   if (mapping->element_size > 1)
      mapping->access_flags |= AYTHER_REGION_NATIVE_ENDIAN;

   return AYTHER_STATUS_OK;
}

static uint32_t ayther_region_subscription(uint32_t region_id)
{
   switch (region_id)
   {
      case AYTHER_REGION_VRAM:
      case AYTHER_REGION_CRAM:
      case AYTHER_REGION_VDP_REGS:
      case AYTHER_REGION_VSRAM:
         return AYTHER_SUB_VDP_MEMORY;
      case AYTHER_REGION_AUDIO_WRITES:
      case AYTHER_REGION_AUDIO_WRITE_COUNT:
         return AYTHER_SUB_AUDIO_WRITES;
      case AYTHER_REGION_PARSED_SPRITES:
      case AYTHER_REGION_PARSED_SPRITE_COUNT:
      /* #39.C: misma suscripcion que los sprites parseados. Es la respuesta a
         "que le paso a ESTE sprite", y el que la pregunta ya pidio la lista. */
      case AYTHER_REGION_SPRITE_OUTCOME:
         return AYTHER_SUB_SPRITE_CAPTURE;
      case AYTHER_REGION_RASTER_FALLBACK_REASONS:
      /* #39.A: el journal EXISTE porque alguien se suscribio al tracking
         raster. Sin esa suscripcion no hay journal que devolver, y entregar
         el del ultimo frame observado seria devolver un fosil. */
      case AYTHER_REGION_RASTER_JOURNAL:
         return AYTHER_SUB_RASTER_TRACKING;
      case AYTHER_REGION_FRAME_HASH:
         return AYTHER_SUB_FRAME_HASH;
      /* #39.E: la paleta es estado del VDP resuelto; quien pregunta por
         colores esta preguntando por su memoria. */
      case AYTHER_REGION_PALETTE:
         return AYTHER_SUB_VDP_MEMORY;
      case AYTHER_REGION_ATTRIBUTION:
         return AYTHER_SUB_ATTRIBUTION;
      case AYTHER_REGION_LINE_REGS:
         return AYTHER_SUB_LINE_STATE;
      case AYTHER_REGION_LINE_CRAM:
         return AYTHER_SUB_LINE_CRAM;
      case AYTHER_REGION_LINE_CELLS:
         return AYTHER_SUB_LINE_CELLS;
      default:
         return 0;
   }
}

static int32_t AYTHER_CALL ayther_query_region_v1(uint32_t region_id,
      ayther_region_info_v1 *out, uint32_t out_size)
{
   ayther_region_mapping mapping;
   int32_t status;

   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;

   status = ayther_map_region(region_id, &mapping);
   if (status != AYTHER_STATUS_OK)
      return status;

   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->region_id = region_id;
   out->data_version = mapping.data_version;
   out->element_size = mapping.element_size;
   out->capacity = mapping.capacity;
   out->byte_size = mapping.byte_size;
   out->access_flags = mapping.access_flags;
   out->legacy_memory_id = mapping.legacy_memory_id;
   return AYTHER_STATUS_OK;
}

static int32_t AYTHER_CALL ayther_read_region_v1(uint32_t region_id,
      uint32_t offset, void *out, uint32_t byte_count,
      uint64_t expected_generation, uint64_t *actual_generation)
{
   ayther_region_mapping mapping;
   uint64_t generation = ayther_snapshot_generation;
   int32_t status;

   if (actual_generation)
      *actual_generation = generation;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;
   if (byte_count && !out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if ((expected_generation != AYTHER_GENERATION_ANY) &&
       (expected_generation != generation))
      return AYTHER_STATUS_STALE_GENERATION;

   status = ayther_map_region(region_id, &mapping);
   if (status != AYTHER_STATUS_OK)
      return status;
   if ((offset > mapping.byte_size) ||
       (byte_count > (mapping.byte_size - offset)))
      return AYTHER_STATUS_OUT_OF_BOUNDS;
   /* #36: una sola consulta. Eran dos por lectura, y la funcion es un switch
      sobre el id: barato, pero pagado por cada region leida de cada frame. */
   {
      uint32_t required = ayther_region_subscription(region_id);
      if (required && !AYTHER_SUBSCRIBED(required))
         return AYTHER_STATUS_NOT_SUBSCRIBED;
   }
   if (!byte_count)
      return AYTHER_STATUS_OK;

   if (byte_count)
      memcpy(out, (const uint8_t *)mapping.data + offset, byte_count);

   if (generation != ayther_snapshot_generation)
   {
      if (actual_generation)
         *actual_generation = ayther_snapshot_generation;
      return AYTHER_STATUS_STALE_GENERATION;
   }

   return AYTHER_STATUS_OK;
}

/* #40: el VDP esta en Mode 5. Es lo que decide si los controles de plano y de
   sprite pueden cumplir lo que prometen. */
/* #40/#55: los controles que solo existen en Mode 5 se rechazan en Mode 4.
   Pero "el VDP todavia no eligio modo" NO es Mode 4, y esto los confundia:
   con `reg[1]` en cero -- un core recien cargado, o un programa que todavia
   no escribio ese registro-- cualquier escritura de control se iba con
   UNSUPPORTED_MODE. Un frontend que arma su estado inicial antes de correr
   el primer frame recibia un "no podes" sobre algo que si va a poder.

   El criterio es el mismo que ya usa `ayther_fill_system` para contestar
   `vdp_mode`, y por la misma razon: en una maquina de la familia Mega Drive
   el bit apagado significa "todavia no", y decir 4 seria inventar. En Mode 4
   de verdad -- SMS, GG, Mark III-- el rechazo sigue igual, que es lo que #40
   vino a arreglar. */
static int ayther_mode5_controls_supported(void)
{
   if (reg[1] & 0x04)
      return 1;
   /* Sin contenido cargado no hay modo que declarar -- `system_hw` recien se
      fija al abrir la ROM-, asi que tampoco hay nada que rechazar. */
   if (!system_hw)
      return 1;
   return ((system_hw & SYSTEM_PBC) == SYSTEM_MD);
}

static int ayther_range_nonzero(const void *data, size_t n)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t i;
   for (i = 0; i < n; ++i)
      if (p[i])
         return 1;
   return 0;
}

static int ayther_plane_suppress_nonzero(void)
{
   size_t i;
   for (i = 0; i < sizeof(ayther_plane_tile_suppress); ++i)
      if (ayther_plane_tile_suppress[i])
         return 1;
   return 0;
}

static int32_t AYTHER_CALL ayther_write_control_v1(uint32_t region_id,
      uint32_t offset, const void *data, uint32_t byte_count,
      uint64_t expected_generation, uint64_t *new_generation)
{
   ayther_region_mapping mapping;
   uint64_t generation = ayther_snapshot_generation;
   int32_t status;

   if (new_generation)
      *new_generation = generation;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;
   if (byte_count && !data)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if ((expected_generation != AYTHER_GENERATION_ANY) &&
       (expected_generation != generation))
      return AYTHER_STATUS_STALE_GENERATION;

   status = ayther_map_region(region_id, &mapping);
   if (status != AYTHER_STATUS_OK)
      return status;
   if (!(mapping.access_flags & AYTHER_REGION_ACCESS_CONTROL_WRITE))
      return AYTHER_STATUS_READ_ONLY;
   if ((offset > mapping.byte_size) ||
       (byte_count > (mapping.byte_size - offset)))
      return AYTHER_STATUS_OUT_OF_BOUNDS;
   if (!byte_count)
      return AYTHER_STATUS_OK;

   if ((region_id == AYTHER_REGION_LAYER_MASK) ||
       (region_id == AYTHER_REGION_LAYER_DIM) ||
       (region_id == AYTHER_REGION_AUDIO_MUTE))
   {
      if ((offset != 0) || (byte_count != mapping.byte_size))
         return AYTHER_STATUS_INVALID_ARGUMENT;
   }

   /* #40: los controles que solo existen en Mode 5, pedidos en Mode 4.
      Hasta aca esto devolvia OK y no hacia nada: `parse_satb_m4` no tiene
      supresion, `render_bg_m4` no tiene hooks y `merge` no participa de ese
      path. Un exito que no hace nada es el peor contrato posible -- el
      frontend cree que oculto el sprite y dibuja su reemplazo encima del
      original-, asi que ahora se dice. */
   if (!ayther_mode5_controls_supported())
   {
      /* #40 fase 2: lo que YA funciona en Mode 4 deja de rechazarse. La lista
         de arriba era correcta en fase 1 -- contestar UNSUPPORTED_MODE es mejor
         que aceptar y no hacer nada-, pero hoy `parse_satb_m4` suprime por slot,
         `render_bg_m4` suprime por (patron, paleta) y respeta la capa de fondo,
         y la supresion por celda (0x104) revela el backdrop desde el hook de
         linea. Seguir rechazandolos seria el error simetrico del que fase 1
         arreglo: negar algo que si se puede cumplir.

         Lo que queda rechazado es lo que de verdad no tiene referente: apagar
         un plano que no existe. */
      switch (region_id)
      {
         case AYTHER_REGION_LAYER_MASK:
            /* En Mode 4 hay UN plano de fondo: el bit de "Plano A" se
               reinterpreta como ese fondo y los de B y Window no significan
               nada. Apagarlos no puede cumplirse, asi que se rechaza; apagar A
               o los sprites si. */
            if ((*(const uint8_t *)data & UINT8_C(0x06)) != UINT8_C(0x06))
            {
               ayther_raster_dirty |= AYTHER_RASTER_REASON_UNSUPPORTED_CONTROLS;
               return AYTHER_STATUS_UNSUPPORTED_MODE;
            }
            break;
         default:
            break;
      }
   }

   if ((region_id == AYTHER_REGION_LAYER_MASK) &&
       ((*(const uint8_t *)data & UINT8_C(0xF0)) != 0))
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if ((region_id == AYTHER_REGION_LAYER_DIM) &&
       (*(const uint8_t *)data > 1))
      return AYTHER_STATUS_INVALID_ARGUMENT;
   /* Bits reservados de la máscara de mute. Ahora son 4 bytes (FM 0-5, PSG 6-9,
      PCM 10-17) y lo que sobra —18 en adelante— sigue rechazándose: un bit puesto
      ahí es un frontend que cree tener canales que este core no tiene, y decirlo
      es mejor que ignorarlo en silencio. La comparación va contra el byte_size
      de la región y no contra un sizeof escrito a mano, para que ensanchar la
      máscara otra vez no deje este chequeo muerto sin que nadie lo note. */
   if ((region_id == AYTHER_REGION_AUDIO_MUTE) &&
       (byte_count == mapping.byte_size))
   {
      uint32_t mute = 0;
      memcpy(&mute, data, sizeof(mute));
      if ((mute & UINT32_C(0xFFFC0000)) != 0)
         return AYTHER_STATUS_INVALID_ARGUMENT;
   }

   if (byte_count)
      memcpy((uint8_t *)mapping.data + offset, data, byte_count);

   /* #36: el barrido de 3 KB corria en CADA escritura a la region. Poner un
      bit no necesita barrer nada -- si lo escrito trae algo distinto de cero,
      la respuesta ya es 1--; solo BORRAR obliga a revisar el resto. El caso
      caro queda para el caso raro. */
   if (region_id == AYTHER_REGION_PLANE_TILE_SUPPRESS)
   {
      if (!ayther_plane_suppress_active && ayther_range_nonzero(data, byte_count))
         ayther_plane_suppress_active = 1;
      else
         ayther_plane_suppress_active = ayther_plane_suppress_nonzero() ? 1 : 0;
      /* #37.4: y qué planos, no sólo si hay alguno. */
      ayther_psup_refresh();
   }
   /* #36: mismo criterio para la mascara de sprites, que hasta ahora no tenia
      ningun resumen: parse_satb_m5 tomaba el parser completo por estar
      suscrito a RENDER_CONTROLS aunque no hubiera un solo slot suprimido. */
   if (region_id == AYTHER_REGION_SPRITE_SUPPRESS)
   {
      if (!ayther_sprite_suppress_active && ayther_range_nonzero(data, byte_count))
         ayther_sprite_suppress_active = 1;
      else
         ayther_sprite_suppress_active =
            ayther_range_nonzero(ayther_sprite_suppress,
                                 sizeof(ayther_sprite_suppress)) ? 1 : 0;
   }

   ayther_snapshot_generation++;
   if (new_generation)
      *new_generation = ayther_snapshot_generation;
   return AYTHER_STATUS_OK;
}

static int32_t AYTHER_CALL ayther_capture_snapshot_v1(
      ayther_frame_snapshot_v1 *out, uint32_t out_size)
{
   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;

   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->snapshot_version = 1;
   out->snapshot_generation = ayther_snapshot_generation;
   out->frame_generation = ayther_frame_generation;
   if (ayther_content_loaded)
      out->flags |= AYTHER_SNAPSHOT_CONTENT_LOADED;
   if (ayther_frame_active)
      out->flags |= AYTHER_SNAPSHOT_FRAME_ACTIVE;
   if (ayther_sprite_overflow)
      out->overflow_flags |= AYTHER_OVERFLOW_PARSED_SPRITES;
   if (ayther_audio_write_overflow)
      out->overflow_flags |= AYTHER_OVERFLOW_AUDIO_WRITES;
   out->fallback_reasons = ayther_raster_dirty;
   out->parsed_sprite_count = ayther_sprite_n;
   out->audio_write_count = ayther_audio_write_n;
   return AYTHER_STATUS_OK;
}

/* El motivo del recompositor, traducido al rango propio de la ABI.
 *
 * Los `AYTHER_RC_ERR_*` internos van de -1 a -4 y ahi ya viven
 * INVALID_ARGUMENT, NOT_FOUND, BUFFER_TOO_SMALL y OUT_OF_BOUNDS: devolverlos
 * crudos haria que «no es modo 5» se leyera como «argumento invalido». De ahi
 * que el mapeo exista, en vez de simplemente dejar pasar el numero.
 *
 * Un motivo que este core todavia no conozca cae en UNSUPPORTED, que es lo que
 * se devolvia para todos hasta ahora. */
static int32_t ayther_rc_status(int rc)
{
   switch (rc)
   {
      case AYTHER_RC_ERR_NOT_MODE5:      return AYTHER_STATUS_RC_NOT_MODE5;
      case AYTHER_RC_ERR_INTERLACE2:     return AYTHER_STATUS_RC_INTERLACE2;
      case AYTHER_RC_ERR_NTSC_FILTER:    return AYTHER_STATUS_RC_NTSC_FILTER;
      case AYTHER_RC_ERR_INVALID_PARAMS: return AYTHER_STATUS_RC_INVALID_PARAMS;
      case AYTHER_RC_ERR_JOURNAL_OVERFLOW:
         return AYTHER_STATUS_RC_JOURNAL_OVERFLOW;
      default:                           return AYTHER_STATUS_UNSUPPORTED;
   }
}

static int32_t AYTHER_CALL ayther_recompose_frame_v1(uint16_t *out_pixels,
      uint32_t pixel_capacity, uint32_t flags, uint32_t *out_width,
      uint32_t *out_height)
{
   AytherSpr saved_sprites[128];
   uint8 saved_sprite_n;
   uint32 saved_sprite_overflow;
   int width;
   int height;
   int supported;

   if (!out_width || !out_height)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (pixel_capacity > INT_MAX)
      return AYTHER_STATUS_OUT_OF_BOUNDS;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;
   if (!AYTHER_SUBSCRIBED(AYTHER_SUB_RECOMPOSITION))
      return AYTHER_STATUS_NOT_SUBSCRIBED;

   /* #36: los 1280 B de sprites se salvan y restauran porque recomponer
      re-parsea la SAT y pisaria la captura del frame. Sin suscripcion a
      SPRITE_CAPTURE no hay captura que proteger, y la copia era 2560 B por
      llamada a cambio de nada. */
   {
      const int keep_sprites = AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE);
      if (keep_sprites)
      {
         memcpy(saved_sprites, ayther_sprites, sizeof(saved_sprites));
         saved_sprite_n = ayther_sprite_n;
         saved_sprite_overflow = ayther_sprite_overflow;
      }
      supported = ayther_recompose_frame(out_pixels, (int)pixel_capacity, flags,
            &width, &height);
      if (keep_sprites)
      {
         memcpy(ayther_sprites, saved_sprites, sizeof(saved_sprites));
         ayther_sprite_n = saved_sprite_n;
         ayther_sprite_overflow = saved_sprite_overflow;
      }
   }

   if (supported <= 0)
      return ayther_rc_status(supported);

   *out_width = (uint32_t)width;
   *out_height = (uint32_t)height;
   return AYTHER_STATUS_OK;
}

static int32_t AYTHER_CALL ayther_recompose_multilayer_v1(
    uint16_t *out_bg_a, uint16_t *out_bg_b, uint16_t *out_window,
    uint16_t *out_sprites, uint16_t *out_composite,
    uint32_t pixel_capacity, uint32_t flags,
    uint32_t *out_width, uint32_t *out_height)
{
   AytherSpr saved_sprites[128];
   uint8 saved_sprite_n;
   uint32 saved_sprite_overflow;
   int width;
   int height;
   int supported;

   if (!out_width || !out_height)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (pixel_capacity > INT_MAX)
      return AYTHER_STATUS_OUT_OF_BOUNDS;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;
   if (!AYTHER_SUBSCRIBED(AYTHER_SUB_RECOMPOSITION))
      return AYTHER_STATUS_NOT_SUBSCRIBED;

   memcpy(saved_sprites, ayther_sprites, sizeof(saved_sprites));
   saved_sprite_n = ayther_sprite_n;
   saved_sprite_overflow = ayther_sprite_overflow;
   supported = ayther_core_recompose_multilayer(
         out_bg_a, out_bg_b, out_window, out_sprites, out_composite,
         (int)pixel_capacity, flags, &width, &height);
   memcpy(ayther_sprites, saved_sprites, sizeof(saved_sprites));
   ayther_sprite_n = saved_sprite_n;
   ayther_sprite_overflow = saved_sprite_overflow;

   if (supported <= 0)
      return ayther_rc_status(supported);

   *out_width = (uint32_t)width;
   *out_height = (uint32_t)height;
   return AYTHER_STATUS_OK;
}

#ifdef AYTHER_LEGACY_PROFILE
/* #32: el simbolo suelto sobrevive SOLO en el perfil legacy. Desde ABI 1.2 la
   funcion vive en el descriptor; mantener las dos formas en el perfil estandar
   dejaba dos superficies publicas que versionar, y era la razon por la que el
   closure de exports decia una cosa en Windows y otra en Linux. */
AYTHER_API int32_t AYTHER_CALL ayther_recompose_multilayer(
    uint16_t *out_bg_a, uint16_t *out_bg_b, uint16_t *out_window,
    uint16_t *out_sprites, uint16_t *out_composite,
    uint32_t pixel_capacity, uint32_t flags,
    uint32_t *out_width, uint32_t *out_height)
{
   return ayther_recompose_multilayer_v1(out_bg_a, out_bg_b, out_window,
         out_sprites, out_composite, pixel_capacity, flags,
         out_width, out_height);
}
#endif

static int32_t AYTHER_CALL ayther_poll_audio_events_v1(
      ayther_audio_event_v1 *out, uint32_t event_capacity,
      uint32_t *out_event_count)
{
   if (!out_event_count)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   *out_event_count = 0;
   if (event_capacity && !out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (event_capacity > INT_MAX)
      return AYTHER_STATUS_OUT_OF_BOUNDS;
#ifndef SOUND_PROBE
   (void)out;
   return AYTHER_STATUS_UNSUPPORTED;
#else
   if (!AYTHER_SUBSCRIBED(AYTHER_SUB_AUDIO_EVENTS))
      return AYTHER_STATUS_NOT_SUBSCRIBED;
   *out_event_count = (uint32_t)audio_probe_poll(
         (ap_event_t *)out, (int)event_capacity);
   return AYTHER_STATUS_OK;
#endif
}

static int32_t AYTHER_CALL ayther_get_audio_transport_stats_v1(
      ayther_audio_transport_stats_v1 *out, uint32_t out_size)
{
   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;

#ifdef SOUND_PROBE
   audio_probe_get_transport_stats((ap_transport_stats_t *)out);
   return AYTHER_STATUS_OK;
#else
   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->transport_version = 1;
   out->event_size = sizeof(ayther_audio_event_v1);
   return AYTHER_STATUS_UNSUPPORTED;
#endif
}

static int32_t AYTHER_CALL ayther_get_subscriptions_v1(
      ayther_subscription_state_v1 *out, uint32_t out_size)
{
   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;

   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->state_version = 1;
   out->supported_mask = ayther_subscription_supported_mask();
   out->active_mask = ayther_subscription_active_mask;
   out->requested_mask = ayther_subscription_requested_mask;
   out->activation_frame = ayther_frame_generation +
      (out->active_mask != out->requested_mask);
   return AYTHER_STATUS_OK;
}

static int32_t AYTHER_CALL ayther_set_subscriptions_v1(
      uint32_t requested_mask)
{
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;
   if (!ayther_subscription_request(requested_mask))
      return AYTHER_STATUS_INVALID_ARGUMENT;
   ayther_snapshot_generation++;
   return AYTHER_STATUS_OK;
}

/* #55: este string decia 1.3 y el `abi_version` del descriptor 1.4, con el
   header en 1.7. La negociacion de version es EL mecanismo por el que un
   frontend descubre lo que el core sabe hacer: anunciar de menos es prometer
   de menos, y un consumidor que exigiera >= 1.5 para usar SYSTEM concluia
   que este core no la tiene, teniendola. Estuvo mal desde 1.5.

   El campo numerico -- el que se usa para negociar-- sale ahora de
   AYTHER_ABI_VERSION_LATEST y no de una constante copiada a mano, y
   `verify_ayther_api` compara el descriptor contra el header.

   #563: el string TAMBIEN sale del header ahora. Decia «se cambia aca
   tambien», y al agregar 1.9 no se cambio -- que es lo que pasa siempre con
   una regla que depende de acordarse. Derivado, no hay dos lugares que puedan
   discrepar. */
static const char ayther_build_id[] =
   "Genesis Plus GX AYTHER ABI " AYTHER_ABI_VERSION_LATEST_STR
   "; core v1.7.4" GIT_VERSION;

#if defined(LSB_FIRST) || defined(_WIN32) || defined(__LITTLE_ENDIAN__)
#define AYTHER_HOST_ENDIANNESS AYTHER_ENDIAN_LITTLE
#else
#define AYTHER_HOST_ENDIANNESS AYTHER_ENDIAN_BIG
#endif

#ifdef SOUND_PROBE
#define AYTHER_AUDIO_PROBE_CAPABILITY AYTHER_CAP_AUDIO_PROBE_V1
#else
#define AYTHER_AUDIO_PROBE_CAPABILITY UINT64_C(0)
#endif

static int32_t AYTHER_CALL ayther_poll_frame_delta_v1(
      ayther_frame_delta_v1 *out, uint32_t out_size)
{
   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;
   if (ayther_frame_active)
      return AYTHER_STATUS_BUSY;

   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->delta_version = 1;
   out->frame_generation = ayther_frame_generation;
   out->raster_event_count = ayther_raster_journal_count;
   out->raster_events_dropped = (uint32_t)ayther_raster_journal_dropped;
   out->parsed_sprite_count = ayther_sprite_n;
   out->audio_write_count = ayther_audio_write_n;
   /* AYTHER (tracker viejo 405): del espejo ACUMULATIVO, no de `bg_name_dirty`. Al original
      lo limpia `update_bg_pattern_cache()` línea por línea DENTRO del frame, y
      para cuando el frontend puede preguntar ya está vacío — medido desde el
      Engine: 0 patterns marcados en 240 frames contra 711 que habían cambiado.

      #30: ya NO se consume al leer. Devuelve el bitmap CONGELADO del ultimo
      frame terminado, sin tocarlo, asi que dos consumidores del mismo frame
      reciben lo mismo. Antes el segundo recibia cero. */
   memcpy(out->dirty_patterns, ayther_delta_frozen, sizeof(out->dirty_patterns));

   return AYTHER_STATUS_OK;
}

/* #30: todo lo ensuciado desde `generation_from` inclusive. */
static int32_t AYTHER_CALL ayther_frame_delta_since_v1(
      uint64_t generation_from, ayther_frame_delta_v1 *out, uint32_t out_size)
{
   size_t i;
   uint64_t oldest;
   int32_t status;

   status = ayther_poll_frame_delta_v1(out, out_size);
   if (status != AYTHER_STATUS_OK)
      return status;

   oldest = (ayther_frame_generation > (uint64_t)AYTHER_FRAME_DELTA_HISTORY)
      ? (ayther_frame_generation - (uint64_t)AYTHER_FRAME_DELTA_HISTORY)
      : 0u;

   if (generation_from < oldest)
   {
      /* Conservador a proposito: TODO sucio. Devolver el subconjunto que si
         esta en el ring seria peor que inutil -- el consumidor creeria que vio
         todo lo que cambio y dejaria assets viejos en pantalla. */
      memset(out->dirty_patterns, 0xFF, sizeof(out->dirty_patterns));
      return AYTHER_STATUS_DELTA_HISTORY_LOST;
   }

   for (i = 0; i < AYTHER_FRAME_DELTA_HISTORY; i++)
   {
      size_t b;
      if (!ayther_delta_ring_valid[i] || ayther_delta_ring_gen[i] < generation_from)
         continue;
      for (b = 0; b < sizeof(out->dirty_patterns); b++)
         out->dirty_patterns[b] |= ayther_delta_ring[i][b];
   }

   return AYTHER_STATUS_OK;
}

static int32_t AYTHER_CALL ayther_get_recompose_stats_v1(
      ayther_recompose_stats_v1 *out, uint32_t out_size)
{
   if (!out)
      return AYTHER_STATUS_INVALID_ARGUMENT;
   if (out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;

   memset(out, 0, sizeof(*out));
   out->struct_size = sizeof(*out);
   out->single_calls = ayther_rc_stat_single_calls;
   out->single_hits = ayther_rc_stat_single_hits;
   out->multilayer_calls = ayther_rc_stat_multi_calls;
   out->multilayer_hits = ayther_rc_stat_multi_hits;
   out->controls_fingerprint = ayther_controls_fingerprint();
   return AYTHER_STATUS_OK;
}

static const ayther_interface_v1 ayther_interface_1 =
{
   AYTHER_ABI_VERSION_LATEST,
   sizeof(ayther_interface_v1),
   AYTHER_CAP_LEGACY_MEMORY | AYTHER_CAP_REGION_QUERY |
      AYTHER_CAP_REGION_READ | AYTHER_CAP_CONTROL_WRITE |
      AYTHER_CAP_FRAME_SNAPSHOT | AYTHER_CAP_PARSED_SPRITES_V1 |
      AYTHER_CAP_AUDIO_WRITES_V1 | AYTHER_CAP_RASTER_FALLBACK_V1 |
      AYTHER_CAP_RECOMPOSE_V1 | AYTHER_CAP_SUBSCRIPTIONS_V1 |
      AYTHER_CAP_FRAME_DELTA_V1 | AYTHER_CAP_RECOMPOSE_STATS_V1 |
      AYTHER_CAP_ATTRIBUTION_V1 | AYTHER_CAP_FRAME_DELTA_SINCE_V1 |
      AYTHER_CAP_SYSTEM_V1 | AYTHER_CAP_LINE_STATE_V1 |
      AYTHER_CAP_OBSERVABILITY_V1 | AYTHER_CAP_SPRITE_OUTCOME_V1 |
      AYTHER_AUDIO_PROBE_CAPABILITY,
   AYTHER_HOST_ENDIANNESS,
   sizeof(void *),
   sizeof(ayther_region_info_v1),
   sizeof(ayther_frame_snapshot_v1),
   sizeof(ayther_sprite_v1),
   sizeof(ayther_audio_write_v1),
   ayther_build_id,
   sizeof(ayther_build_id) - 1,
   0,
   ayther_query_region_v1,
   ayther_read_region_v1,
   ayther_write_control_v1,
   ayther_capture_snapshot_v1,
   ayther_recompose_frame_v1,
   sizeof(ayther_audio_event_v1),
   sizeof(ayther_audio_transport_stats_v1),
   ayther_poll_audio_events_v1,
   ayther_get_audio_transport_stats_v1,
   sizeof(ayther_subscription_state_v1),
   0,
   ayther_get_subscriptions_v1,
   ayther_set_subscriptions_v1,
   sizeof(ayther_frame_delta_v1),
   ayther_poll_frame_delta_v1,
   sizeof(ayther_recompose_stats_v1),
   0,
   ayther_get_recompose_stats_v1,
   ayther_recompose_multilayer_v1,
   ayther_frame_delta_since_v1
};

AYTHER_API const ayther_interface_v1 *AYTHER_CALL ayther_get_interface(
      uint32_t requested_version)
{
   /* Un pedido de 1.0 recibe este mismo descriptor 1.1: los campos de 1.0 están
      en las mismas posiciones y `struct_size` le dice al cliente viejo dónde
      termina lo que él conoce. Rechazar el pedido de 1.0 rompería a los
      consumidores existentes sin ninguna ganancia. Lo que NO se acepta es un
      minor mayor al que este core implementa: ahí el cliente pide campos que no
      existen y devolver algo sería mentir. */
   if (requested_version == 0)
      return &ayther_interface_1;
   if ((AYTHER_ABI_VERSION_MAJOR(requested_version) ==
        AYTHER_ABI_VERSION_MAJOR(AYTHER_ABI_VERSION_LATEST)) &&
       (AYTHER_ABI_VERSION_MINOR(requested_version) <=
        AYTHER_ABI_VERSION_MINOR(AYTHER_ABI_VERSION_LATEST)))
      return &ayther_interface_1;
   return NULL;
}

#ifdef AYTHER_METRICS
/* #36: los contadores de instrumentacion. Existen SOLO en builds con
   -DAYTHER_METRICS, que no se publican: son para que el harness pueda afirmar
   "cero trabajo en idle" contando en vez de cronometrando. Un bench cuyo ruido
   es del mismo orden que lo que mide no puede distinguir "no hace nada" de
   "hace poco"; un contador en cero si. */
AYTHER_API int32_t AYTHER_CALL ayther_metrics_read(ayther_metrics_v1 *out,
      uint32_t out_size)
{
   if (!out || out_size < sizeof(*out))
      return AYTHER_STATUS_BUFFER_TOO_SMALL;
   *out = ayther_metrics;
   out->struct_size = (uint32_t)sizeof(*out);
   return AYTHER_STATUS_OK;
}

AYTHER_API void AYTHER_CALL ayther_metrics_reset(void)
{
   uint32_t size = ayther_metrics.struct_size;
   memset(&ayther_metrics, 0, sizeof(ayther_metrics));
   ayther_metrics.struct_size = size;
}
#endif /* AYTHER_METRICS */
#endif /* AYTHER_EXTENSIONS */

void *retro_get_memory_data(unsigned id)
{
   switch (id)
   {
      case RETRO_MEMORY_SAVE_RAM:
         return sram.on ? sram.sram : NULL;
      case RETRO_MEMORY_SYSTEM_RAM:
         return work_ram;
      /* AYTHER fork delta: expose VDP VRAM (64KB) so the HD sprite pipeline can
         read the Sprite Attribute Table. Upstream returns NULL for VIDEO_RAM.
         vram[] is the VDP's 64KB VRAM, declared in core/vdp_ctrl.c. */
      case RETRO_MEMORY_VIDEO_RAM:
         return vram;
#ifdef AYTHER_EXTENSIONS
      /* AYTHER fork delta: expose VDP CRAM (128 bytes = 64 x 9-bit colour
         entries, stored as host-endian uint16 words in GPX's internal
         0000BBB0GGG0RRR0 layout — core/vdp_ctrl.c). There is no standard
         libretro id for CRAM, so the AYTHER frontend uses a private one. */
      case 0x100 /* AYTHER_MEMORY_CRAM */:
         return cram;
      /* AYTHER fork delta: expose the 32 VDP registers (reg[0x20],
         core/vdp_ctrl.c) so the Lab can derive the plane name-table bases
         and plane size for the tilemap viewer. No standard libretro id. */
      case 0x101 /* AYTHER_MEMORY_VDP_REGS */:
         return reg;
      /* AYTHER fork delta: expose VSRAM (128 bytes = 64 x 11-bit vertical
         scroll entries, host-endian uint16 words — core/vdp_ctrl.c). Needed to
         resolve la posición en pantalla de un tile de plano (Fase 2c, overlay HD
         scroll-aware). hscroll vive en VRAM; vscroll vive acá. */
      case 0x107 /* AYTHER_MEMORY_VSRAM */:
         return vsram;
      /* AYTHER fork delta: máscara de capas visibles (1 byte, ESCRIBIBLE). El
         frontend escribe el bitmask (A/B/Window/Sprites) y el renderer la lee
         para aislar capas en el viewport. core/vdp_render.c. */
      case 0x102 /* AYTHER_MEMORY_LAYER_MASK */:
         return &ayther_layer_mask;
      /* AYTHER fork delta: bitmask de slots SAT suprimidos (16 bytes, ESCRIBIBLE).
         El frontend setea el bit de cada slot a ocultar; parse_satb lo saltea.
         core/vdp_render.c. */
      case 0x103 /* AYTHER_MEMORY_SPRITE_SUPPRESS */:
         return ayther_sprite_suppress;
      /* AYTHER fork delta: máscara de celdas de tile suprimidas (512 bytes,
         ESCRIBIBLE). El frontend setea el bit de cada celda (col=x>>3, fila=
         line>>3) a ocultar; render_line la pinta con el backdrop. vdp_render.c. */
      case 0x104 /* AYTHER_MEMORY_TILE_SUPPRESS */:
         return ayther_tile_suppress;
      /* AYTHER fork delta: máscara de tiles de PLANO suprimidos (3072 bytes,
         ESCRIBIBLE). 3 planos × bitmap de (patrón<<2 | paleta); render_bg_m5/_vs
         saltea esas celdas y revela el plano de atrás. vdp_render.c. */
      case 0x105 /* AYTHER_MEMORY_PLANE_TILE_SUPPRESS */:
         return ayther_plane_tile_suppress;
      /* AYTHER fork delta: flag (1 byte) "hay tiles de plano ocultos" — gatea el
         fast path de render_bg_m5/_vs (0 = DRAW_COLUMN sin costo). Lo setea el
         frontend al escribir 0x105. */
      case 0x106 /* AYTHER_MEMORY_PLANE_SUPPRESS_ACTIVE */:
         return &ayther_plane_suppress_active;
      /* AYTHER fork delta: flag (1 byte, ESCRIBIBLE) "atenuar capas no-sprite al
         25%" — dim mode del viewport de Animación. 0 = render normal. El frontend
         lo setea SÓLO en el frame visible (produce). core/vdp_render.c. */
      case 0x108 /* AYTHER_MEMORY_LAYER_DIM */:
         return &ayther_layer_dim;
      /* AYTHER fork delta: lista de sprites que parse_satb PARSEÓ este frame (10
         bytes c/u: yr/xr/attr u16 + w/h/sat_idx/chain_pos u8) + su contador
         (escribible = reset legacy). Es
         la fuente fiable de "qué sprites se dibujaron" aunque el juego reescriba el
         SAT a mitad de frame / cambie su base (el genio del logo Sega de Aladdin: el
         SAT a fin de frame muestra solo placeholders). La ABI v1 reinicia el
         contador; el reset manual 0x10C=0 sigue aceptado. vdp_render.c. */
      case 0x10B /* AYTHER_MEMORY_PARSED_SPRITES */:
         return ayther_sprites;
      case 0x10C /* AYTHER_MEMORY_PARSED_SPRITE_COUNT */:
         return &ayther_sprite_n;
      /* AYTHER (#5/#274): bitmask u32 de fallback por frame, automáticamente
         reiniciado por el core y todavía ESCRIBIBLE por compatibilidad.
         Bits AYTHER_RASTER_REASON_*; >0 conserva el booleano legacy. */
      case 0x10E /* AYTHER_MEMORY_RASTER_DIRTY */:
         return &ayther_raster_dirty;
      /* AYTHER fork delta: log de escrituras crudas a los chips de sonido (FM
         YM2612 + PSG SN76489) en orden temporal dentro del frame (AytherAudioWrite,
         8 bytes c/u: cycle u32 + addr u16 + data u8 + chip u8) + su contador u32
         (ESCRIBIBLE = reset). Identidad de audio por SECUENCIA DE COMANDOS al chip
         (estable en replay) vs hash del PCM de salida (no reproducible tras
         unserialize). La ABI v1 reinicia el contador por frame; el reset manual
         0x10A=0 sigue aceptado. core/sound/sound.c. */
      case 0x109 /* AYTHER_MEMORY_AUDIO_WRITES */:
         return ayther_audio_writes;
      case 0x10A /* AYTHER_MEMORY_AUDIO_WRITE_COUNT */:
         return &ayther_audio_write_n;
      /* AYTHER fork delta: máscara de mute por canal de audio (u16, ESCRIBIBLE).
         Bits 0-5 = FM ch 0-5; bits 6-9 = PSG ch 0-3. El canal se pone a 0 en el
         mixer de salida sin tocar el estado del chip (replay-safe). Primitivo de
         la sustitución por evento (C-A3). core/sound/sound.c. */
      case 0x10D /* AYTHER_MEMORY_AUDIO_MUTE */:
         return &ayther_audio_mute;
      /* AYTHER (#563): RAM del Z80 (8 KB, ESCRIBIBLE). Varios juegos dejan ahi
         el id del tema a tocar; sin esto no hay donde mirar cuando la casilla
         no esta en work RAM. core/genesis.c. */
      case 0x10F /* AYTHER_MEMORY_Z80_RAM */:
         return zram;
#endif
      default:
         return NULL;
   }
}

size_t retro_get_memory_size(unsigned id)
{
   int i;

   switch (id)
   {
      case RETRO_MEMORY_SAVE_RAM:
      {
         /* return 0 if SRAM is disabled */
         if (!sram.on)
            return 0;

         /* if emulation is not running, we assume the frontend is requesting SRAM size for loading so max supported size is returned */
         if (!is_running)
            return 0x10000;

         /* otherwise, we assume this is for saving and we return the size of SRAM data that has actually been modified */
         /* this is obviously not %100 safe since the frontend could still be trying to load SRAM while emulation is running */
         /* a better solution would be that the frontend itself checks if data has been modified before writing it to a file */
         for (i=0xffff; i>=0; i--)
            if (sram.sram[i] != 0xff)
               return (i+1);

         /* return 0 if SRAM is not modified */
         return 0;
      }

      case RETRO_MEMORY_SYSTEM_RAM:
      {
         /* 16-bit hardware */
         if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
            return 0x10000; /* 64KB internal RAM */

         /* get 8-bit cartrige on-board RAM size */
         i = sms_cart_ram_size();

         if (i > 0)
            return i + 0x2000; /* on-board RAM size + max 8KB internal RAM */
         else if (system_hw == SYSTEM_SGII)
            return 0x0800; /* 2KB internal RAM */
         else if (system_hw == SYSTEM_SG)
            return 0x0400; /* 1KB internal RAM */
         else
            return 0x2000; /* 8KB internal RAM */
      }

      /* AYTHER fork delta: VDP VRAM is a fixed 64KB array (core/vdp_ctrl.c). */
      case RETRO_MEMORY_VIDEO_RAM:
         return 0x10000;

#ifdef AYTHER_EXTENSIONS
      /* AYTHER fork delta: VDP CRAM is a fixed 128-byte array. */
      case 0x100 /* AYTHER_MEMORY_CRAM */:
         return 0x80;

      /* AYTHER fork delta: 32 VDP registers. */
      case 0x101 /* AYTHER_MEMORY_VDP_REGS */:
         return 0x20;

      /* AYTHER fork delta: VSRAM (128 bytes). */
      case 0x107 /* AYTHER_MEMORY_VSRAM */:
         return 0x80;

      /* AYTHER fork delta: máscara de capas visibles (1 byte). */
      case 0x102 /* AYTHER_MEMORY_LAYER_MASK */:
         return 1;

      /* AYTHER fork delta: bitmask de slots SAT suprimidos (16 bytes). */
      case 0x103 /* AYTHER_MEMORY_SPRITE_SUPPRESS */:
         return sizeof(ayther_sprite_suppress);

      /* AYTHER fork delta: máscara de celdas de tile suprimidas (512 bytes). */
      case 0x104 /* AYTHER_MEMORY_TILE_SUPPRESS */:
         return sizeof(ayther_tile_suppress);

      /* AYTHER fork delta: máscara de tiles de plano suprimidos (3072 bytes). */
      case 0x105 /* AYTHER_MEMORY_PLANE_TILE_SUPPRESS */:
         return sizeof(ayther_plane_tile_suppress);

      /* AYTHER fork delta: flag de actividad de la supresión por plano (1 byte). */
      case 0x106 /* AYTHER_MEMORY_PLANE_SUPPRESS_ACTIVE */:
         return 1;

      /* AYTHER fork delta: flag de dim de capas no-sprite (1 byte). */
      case 0x108 /* AYTHER_MEMORY_LAYER_DIM */:
         return 1;

      /* AYTHER fork delta: lista de sprites parseados (128 x 10 bytes) + contador. */
      case 0x10B /* AYTHER_MEMORY_PARSED_SPRITES */:
         return sizeof(ayther_sprites);
      case 0x10C /* AYTHER_MEMORY_PARSED_SPRITE_COUNT */:
         return 1;

      /* AYTHER (#5/#274): bitmask de motivos (u32, escribible = reset). */
      case 0x10E /* AYTHER_MEMORY_RASTER_DIRTY */:
         return sizeof(ayther_raster_dirty);

      /* AYTHER fork delta: log de escrituras a chips de sonido (8192 x 8 bytes) +
         contador (u32, escribible = reset). */
      case 0x109 /* AYTHER_MEMORY_AUDIO_WRITES */:
         return sizeof(ayther_audio_writes);
      case 0x10A /* AYTHER_MEMORY_AUDIO_WRITE_COUNT */:
         return sizeof(ayther_audio_write_n);

      /* AYTHER fork delta: máscara de mute por canal de audio (u16). */
      case 0x10D /* AYTHER_MEMORY_AUDIO_MUTE */:
         return sizeof(ayther_audio_mute);
      /* AYTHER (#563): RAM del Z80. */
      case 0x10F /* AYTHER_MEMORY_Z80_RAM */:
         return sizeof(zram);
#endif

      default:
        return 0;
   }
}

static void check_system_specs(void)
{
   unsigned level = 7;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
   struct retro_log_callback log;
   unsigned level                = 1;
   uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   check_system_specs();

   environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);
   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_ctrl);

   frameskip_type             = 0;
   frameskip_threshold        = 0;
   frameskip_counter          = 0;
   retro_audio_buff_active    = false;
   retro_audio_buff_occupancy = 0;
   retro_audio_buff_underrun  = false;
   audio_latency              = 0;
   update_audio_latency       = false;
   ayther_reset_session(false);
}

void retro_deinit(void)
{
   libretro_supports_option_categories = false;
   libretro_supports_bitmasks          = false;

   g_rom_data = NULL;
   g_rom_size = 0;
   is_running = false;
   ayther_reset_session(false);
#ifdef AYTHER_EXTENSIONS
   /* #36.7: los caches de recomposicion se piden al primer uso; lo que se pidio
      se suelta. Un core que nunca recompuso no llego a reservarlos. */
   ayther_recompose_release();
#endif
}

void retro_reset(void)
{
#ifdef HAVE_OVERCLOCK
   overclock_delay = OVERCLOCK_FRAME_DELAY;
   update_overclock();
#endif
   gen_reset(0);
   ayther_invalidate_snapshot();
}

extern int8 audio_hard_disable;

extern void sound_update_fm_function_pointers(void);

void retro_run(void)
{
   bool okay = false;
   int result = -1;
   int do_skip = 0;
   bool updated = false;
   int soundbuffer_size = 0;

   is_running = true;
   ayther_begin_frame();

#ifdef HAVE_OVERCLOCK
  /* update overclock delay */
  if (overclock_delay && --overclock_delay == 0)
      update_overclock();
#endif

   environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated);
   if (updated)
   {
      check_variables(false);
      if (restart_eq)
      {
         audio_set_equalizer();
         restart_eq = false;
      }
   }

   okay = environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &result);
   if (okay)
   {
      bool audioEnabled = 0 != (result & 2);
      bool videoEnabled = 0 != (result & 1);
      bool hardDisableAudio = 0 != (result & 8);
      do_skip = !videoEnabled;
      if (audio_hard_disable != hardDisableAudio)
      {
        audio_hard_disable = hardDisableAudio;
        sound_update_fm_function_pointers();
      }
   }
   else
   {
      do_skip = false;
      audio_hard_disable = false;
   }

  /* Check whether current frame should
  * be skipped */
  if ((frameskip_type > 0) &&
      retro_audio_buff_active &&
      !do_skip)
  {
    switch (frameskip_type)
    {
      case 1: /* auto */
        do_skip = retro_audio_buff_underrun ? 1 : 0;
        break;
      case 2: /* manual */
        do_skip = (retro_audio_buff_occupancy < frameskip_threshold) ? 1 : 0;
        break;
      default:
        do_skip = 0;
        break;
    }

    if (!do_skip || (frameskip_counter >= FRAMESKIP_MAX))
    {
      do_skip           = 0;
      frameskip_counter = 0;
    }
    else
      frameskip_counter++;
  }

  /* If frameskip settings have changed, update
  * frontend audio latency */
  if (update_audio_latency)
  {
    environ_cb(RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY,
        &audio_latency);
    update_audio_latency = false;
  }

   if (system_hw == SYSTEM_MCD)
   {
      system_frame_scd(do_skip);
   }
   else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
   {
      system_frame_gen(do_skip);
   }
   else
   {
      system_frame_sms(do_skip);
   }

   soundbuffer_size = audio_update(soundbuffer);
   ayther_end_frame(soundbuffer, (unsigned)soundbuffer_size);

   /* Force viewport update when SMS border changes after startup undetected */
   if (     ((system_hw == SYSTEM_MARKIII) || (system_hw & SYSTEM_SMS) || (system_hw == SYSTEM_PBC))
         && reg[0] != reg0_prev)
      bitmap.viewport.changed = 9;

   reg0_prev = reg[0];

   if (bitmap.viewport.changed & 9)
   {
      bool geometry_updated = update_viewport();
      bitmap.viewport.changed &= ~1;
      bitmap.viewport.changed &= ~8;
      if (geometry_updated)
         update_geometry();
   }

   if (config.gun_cursor)
   {
      if (input.system[0] == SYSTEM_LIGHTPHASER)
      {
         draw_cursor(input.analog[0][0], input.analog[0][1], 0x001f);
      }
      else if (input.dev[4] == DEVICE_LIGHTGUN)
      {
         draw_cursor(input.analog[4][0], input.analog[4][1], 0x001f);
      }

      if (input.system[1] == SYSTEM_LIGHTPHASER)
      {
         draw_cursor(input.analog[4][0], input.analog[4][1], 0xf800);
      }
      else if (input.dev[5] == DEVICE_LIGHTGUN)
      {
         draw_cursor(input.analog[5][0], input.analog[5][1], 0xf800);
      }
   }

   /* LED interface */
   if (led_state_cb)
      retro_led_interface();

   if (!do_skip)
      video_cb(bitmap.data + bmdoffset, vwidth - vwoffset, vheight, 720 * 2);
   else
      video_cb(NULL, vwidth - vwoffset, vheight, 720 * 2);

   audio_cb(soundbuffer, soundbuffer_size);
}

#undef  CHUNKSIZE
