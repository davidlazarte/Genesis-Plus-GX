/***************************************************************************************
 *  Genesis Plus
 *  Video Display Processor (68k & Z80 CPU interface)
 *
 *  Support for SG-1000 (TMS99xx & 315-5066), Master System (315-5124 & 315-5246), Game Gear & Mega Drive VDP
 *
 *  Copyright (C) 1998-2003  Charles Mac Donald (original code)
 *  Copyright (C) 2007-2025  Eke-Eke (Genesis Plus GX)
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

#ifndef _VDP_H_
#define _VDP_H_

#ifdef AYTHER_EXTENSIONS
#include "ayther_raster.h"
#include "ayther/ayther_runtime.h"
#include "ayther/ayther_metrics.h"
#endif

/* VDP context */
/* Las cuatro definiciones de vdp_ctrl.c fuerzan ALIGNED_(4). Sin repetirlo
   ACA, un TU que solo vea el `extern` asume la alineacion preferida del tipo
   —16 para un array de este tamano— y el vectorizador emite `movaps`, que
   exige 16 de verdad. Eso es exactamente lo que hacia crashear a
   `ayther_core_recompose_multilayer` al copiar `cram` entero desde otro TU con
   -O2: `cram` cae en ...0228, alineado a 8, y el load alineado dispara un
   ACCESS_VIOLATION. En -O0 no se veia porque no habia vectorizacion.
   Declaracion y definicion tienen que decir LO MISMO. */
extern uint8 reg[0x20];
extern uint8 ALIGNED_(4) sat[0x400];
extern uint8 ALIGNED_(4) vram[0x10000];
extern uint8 ALIGNED_(4) cram[0x80];
extern uint8 ALIGNED_(4) vsram[0x80];
extern uint8 border;   /* AYTHER (#12C): indice de color del borde */
#ifdef AYTHER_EXTENSIONS
/* AYTHER (#27): tablas de layout del playfield/hscroll. Las usa el raster
   replay para recalcular el estado derivado de un cambio de registro con las
   MISMAS constantes que `vdp_reg_w`, en vez de una copia que pueda desviarse. */
extern const uint8 hscroll_mask_table[];
extern const uint8 shift_table[];
extern const uint8 col_mask_table[];
extern const uint16 row_mask_table[];
#endif
extern uint8 hint_pending;
extern uint8 vint_pending;
extern uint16 status;
extern uint32 dma_length;
extern uint32 dma_endCycles;
extern uint8 dma_type;

/* Global variables */
extern uint16 ntab;
extern uint16 ntbb;
extern uint16 ntwb;
extern uint16 satb;
extern uint16 hscb;
extern uint8 bg_name_dirty[0x800];
extern uint16 bg_name_list[0x800];
extern uint16 bg_list_index;
extern uint8 hscroll_mask;
#ifdef AYTHER_EXTENSIONS
/* AYTHER (#5/#274): bitmask transiente de motivos de fallback por frame.
   Se expone por el id privado 0x10E; `> 0` conserva el contrato booleano. */
extern uint32 ayther_raster_dirty;
/* AYTHER (#405): espejo acumulativo de `bg_name_dirty` para el frontend. El
   original lo limpia `update_bg_pattern_cache()` a mitad del frame, así que
   nunca llegaba nada al otro lado; este sólo lo limpia el poll del frontend
   (ayther_poll_frame_delta_v1). Ver el comentario largo en vdp_ctrl.c. */
extern uint8 ayther_vram_dirty[0x800];
#endif
extern uint8 playfield_shift;
extern uint8 playfield_col_mask;
extern uint16 playfield_row_mask;
extern uint8 odd_frame;
extern uint8 im2_flag;
extern uint8 interlaced;
extern uint8 vdp_pal;
extern uint8 h_counter;
extern uint16 v_counter;
extern uint16 vc_max;
extern uint16 vscroll;
extern uint16 lines_per_frame;
extern uint16 max_sprite_pixels;
extern uint32 fifo_cycles[4];
extern uint32 hvc_latch;
extern uint32 vint_cycle;
extern const uint8 *hctab;

/* Function pointers */
extern void (*vdp_68k_data_w)(unsigned int data);
extern void (*vdp_z80_data_w)(unsigned int data);
extern unsigned int (*vdp_68k_data_r)(void);
extern unsigned int (*vdp_z80_data_r)(void);

/* Function prototypes */
extern void vdp_init(void);
extern void vdp_reset(void);
#ifdef AYTHER_EXTENSIONS
extern void vdp_ayther_begin_frame(void);
#else
#define vdp_ayther_begin_frame() ((void)0)
#endif
extern int vdp_context_save(uint8 *state);
extern int vdp_context_load(uint8 *state);
extern void vdp_dma_update(unsigned int cycles);
extern void vdp_68k_ctrl_w(unsigned int data);
extern void vdp_z80_ctrl_w(unsigned int data);
extern void vdp_sms_ctrl_w(unsigned int data);
extern void vdp_tms_ctrl_w(unsigned int data);
extern unsigned int vdp_68k_ctrl_r(unsigned int cycles);
extern unsigned int vdp_z80_ctrl_r(unsigned int cycles);
extern unsigned int vdp_hvc_r(unsigned int cycles);
extern void vdp_test_w(unsigned int data);
extern int vdp_68k_irq_ack(int int_level);

#endif /* _VDP_H_ */
