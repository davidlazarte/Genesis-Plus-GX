/***************************************************************************************
 *  Genesis Plus
 *  Video Display Processor (pixel output rendering)
 *
 *  Support for all TMS99xx modes, Mode 4 & Mode 5 rendering
 *
 *  Copyright (C) 1998-2003  Charles Mac Donald (original code)
 *  Copyright (C) 2007-2025  Eke-Eke (Genesis Plus GX)
 *  Copyright (C) 2022  AlexKiri (enhanced vscroll mode rendering function)
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

#ifndef _RENDER_H_
#define _RENDER_H_

#ifdef AYTHER_EXTENSIONS
#include "ayther/ayther_api.h"
#include "ayther/ayther_runtime.h"
#include "ayther/ayther_metrics.h"
#ifdef AYTHER_EXTENSIONS
#include "ayther/ayther_sprite_capture.h"
#endif
#endif

/* 3:3:2 RGB */
#if defined(USE_8BPP_RENDERING)
#define PIXEL(r,g,b) (((r) << 5) | ((g) << 2) | (b))
#define GET_R(pixel) (((pixel) & 0xe0) >> 5)
#define GET_G(pixel) (((pixel) & 0x1c) >> 2)
#define GET_B(pixel) (((pixel) & 0x03) >> 0)

/* 5:5:5 RGB */
#elif defined(USE_15BPP_RENDERING)
#if defined(USE_ABGR)
#define PIXEL(r,g,b) ((1 << 15) | ((b) << 10) | ((g) << 5) | (r))
#define GET_B(pixel) (((pixel) & 0x7c00) >> 10)
#define GET_G(pixel) (((pixel) & 0x03e0) >> 5)
#define GET_R(pixel) (((pixel) & 0x001f) >> 0)
#else
#define PIXEL(r,g,b) ((1 << 15) | ((r) << 10) | ((g) << 5) | (b))
#define GET_R(pixel) (((pixel) & 0x7c00) >> 10)
#define GET_G(pixel) (((pixel) & 0x03e0) >> 5)
#define GET_B(pixel) (((pixel) & 0x001f) >> 0)
#endif

/* 5:6:5 RGB */
#elif defined(USE_16BPP_RENDERING)
#define PIXEL(r,g,b) (((r) << 11) | ((g) << 5) | (b))
#define GET_R(pixel) (((pixel) & 0xf800) >> 11)
#define GET_G(pixel) (((pixel) & 0x07e0) >> 5)
#define GET_B(pixel) (((pixel) & 0x001f) >> 0)

/* 8:8:8 RGB */
#elif defined(USE_32BPP_RENDERING)
#define PIXEL(r,g,b) ((0xff << 24) | ((r) << 16) | ((g) << 8) | (b))
#define GET_R(pixel) (((pixel) & 0xff0000) >> 16)
#define GET_G(pixel) (((pixel) & 0x00ff00) >> 8)
#define GET_B(pixel) (((pixel) & 0x0000ff) >> 0)
#endif

/* LCD image persistence (ghosting) filter */
/* Simulates (roughly) the slow decay response time of passive-matrix LCD */
/* Rate value is formatted as 0.8 fixed-point integer (between 0.0 and 0.99609375), a higher value meaning a slower decay */
/* Required for proper display of some effects in a few Game Gear games (James Pond 3, Power Drift, Super Monaco GP II,...) */
#define RENDER_PIXEL_LCD(in,out,table,rate) \
{ \
  PIXEL_OUT_T pixel_out = table[*in++]; \
  PIXEL_OUT_T pixel_old  = *out; \
  uint8 r = GET_R(pixel_out); \
  uint8 g = GET_G(pixel_out); \
  uint8 b = GET_B(pixel_out); \
  int r_decay = GET_R(pixel_old) - r; \
  int g_decay = GET_G(pixel_old) - g; \
  int b_decay = GET_B(pixel_old) - b; \
  if (r_decay > 0) r += (rate * r_decay) >> 8; \
  if (g_decay > 0) g += (rate * g_decay) >> 8; \
  if (b_decay > 0) b += (rate * b_decay) >> 8; \
  *out++ = PIXEL(r,g,b); \
}

/* Global variables */
extern uint16 spr_col;

#ifdef AYTHER_EXTENSIONS
/* AYTHER fork delta: máscara de capas visibles (id de memoria privado 0x102,
   escribible desde el frontend). Bit set = capa visible; default 0xFF. La leen
   render_bg_m5/_vs (planos A/B/Window) y render_line (sprites) para aislar
   capas en el viewport — autoría en el Lab de AYTHER. */
#define AYTHER_LAYER_A   0x01   /* Plano A (Scroll A)  */
#define AYTHER_LAYER_B   0x02   /* Plano B (Scroll B)  */
#define AYTHER_LAYER_W   0x04   /* Window              */
#define AYTHER_LAYER_OBJ 0x08   /* Sprites             */
extern uint8 ayther_layer_mask;
extern uint8 ayther_layer_dim;             /* atenuar capas no-sprite al 25% (id 0x108) */
/* Sprites realmente parseados por parse_satb en el frame (id 0x10B lista / 0x10C
   contador, escribible=reset legacy). La ABI v1 reinicia contador/overflow antes
   de cada frame. Captura los reescritos in-place a mitad de frame. */
/* 10 bytes. sat_idx = índice de ENTRADA del SAT (link>>2) — el MISMO espacio de
   índices que la máscara de supresión 0x103 (¡distinto del orden de la lista!).
   chain_pos = posición en la CADENA de links al parsear = prioridad real de dibujo
   del VDP entre sprites (menor = más al frente). */
extern uint8 ayther_sprite_suppress[16];   /* slots SAT suprimidos (id 0x103) */
extern uint8 ayther_sprite_suppress_active; /* #36: 1 = hay algun slot suprimido */

/* #42: estado de render por scanline. */
#define AYTHER_LINE_MAX 240u
extern ayther_line_regs_v1 ayther_line_regs[AYTHER_LINE_MAX];
extern uint8 ayther_line_cram[AYTHER_LINE_MAX][128];
extern uint32 ayther_line_count;
extern uint32 ayther_line_flags;
extern ayther_line_cells_v1 ayther_line_cells[AYTHER_LINE_MAX];
void ayther_line_state_begin_frame(void);
/* #36.7: suelta los caches de recomposicion pedidos al primer uso. */
void ayther_recompose_release(void);
#ifdef AYTHER_EXTENSIONS
/* #39.C: que le paso a cada sprite de la SAT en este frame (bits acumulados,
   se limpian en vdp_ayther_begin_frame). */
extern uint8 ayther_spr_outcome[AYTHER_SPRITE_SAT_SLOTS];
#endif

/* #39.E: la LUT de color resuelto (formato del build, S/H ya aplicado). */
const void *ayther_palette_data(void);
unsigned ayther_palette_entry_size(void);
unsigned ayther_palette_entries(void);
extern uint8 ayther_tile_suppress[512];    /* celdas de tile suprimidas (id 0x104, 64x64) */
extern uint8 ayther_plane_tile_suppress[3 * 1024]; /* tiles de plano suprimidos (id 0x105) */
extern uint8 ayther_plane_suppress_active;  /* id 0x106: 1 = hay algún tile de plano oculto */
/* #37.4: resumen por plano de la máscara 0x105 — un plano vacío conserva el
   fast path de DRAW_COLUMN aunque otro tenga tiles ocultos. */
extern uint8 ayther_psup_any[3];
void ayther_psup_refresh(void);

/* AYTHER (#270): recomposición del frame desde el estado FINAL del VDP, con el
   mismo renderer del core (spike de fidelidad del render propio). `flags` apaga
   comportamientos uno a uno para atribuir el error de no modelarlos. Escribe
   w*h píxeles RGB565 contiguos en `out` (cap en píxeles) y devuelve 1; 0 si no
   aplica (no-modo-5, interlace 2, NTSC, buffer chico). No perturba la emulación
   (todo estado mutado se salva y restaura). Export del DLL: el frontend lo
   resuelve por nombre (win64 auto-exporta los globals; ver Makefile.libretro). */
#define AYTHER_RC_NO_SH        0x01u  /* sin shadow/highlight (como reg12.3=0)   */
#define AYTHER_RC_NO_SPR_LIMIT 0x02u  /* sin límite de sprites/línea ni de px    */
#define AYTHER_RC_NO_SPR_MASK  0x04u  /* sin máscara de sprites (x=0)            */
#define AYTHER_RC_NO_WINDOW    0x08u  /* sin plano Window (A ocupa toda la línea)*/
#define AYTHER_RC_FLAT_HS      0x10u  /* hscroll de la línea 0 para todas        */
#define AYTHER_RC_FLAT_VS      0x20u  /* vscroll de la columna 0 para todas      */
/* R-2: byte alto = override de ayther_layer_mask durante la recomposición
   (bits AYTHER_LAYER_*; 0 = mantener la máscara vigente). Recomponer una capa
   sola = el oráculo CPU del pipeline indexado. */
#define AYTHER_RC_LAYER_MASK(m) (((unsigned int)(m) & 0xFFu) << 24)
/* Función interna. La ABI pública la expone mediante el function pointer
   recompose_frame; no se agrega un segundo símbolo AYTHER al DLL/so. */
extern int ayther_recompose_frame(uint16 *out, int cap, unsigned int flags,
                                  int *out_w, int *out_h);
extern int ayther_core_recompose_multilayer(
    uint16 *out_bg_a, uint16 *out_bg_b, uint16 *out_window,
    uint16 *out_sprites, uint16 *out_composite,
    int cap, unsigned int flags,
    int *out_w, int *out_h);

/* #26: huella de contenido de TODAS las regiones de control + el estado de la
   suscripción RENDER_CONTROLS. Es la clave que faltaba en los caches de
   recomposición; ver el comentario largo en vdp_render.c. */
extern uint64_t ayther_controls_fingerprint(void);

/* #26: aciertos/llamadas de cada cache, para que el test afirme sobre el
   mecanismo en vez de cronometrar. */
extern uint64_t ayther_rc_stat_single_calls;
extern uint64_t ayther_rc_stat_single_hits;
extern uint64_t ayther_rc_stat_multi_calls;
extern uint64_t ayther_rc_stat_multi_hits;

/* #41: atribucion por pixel del frame emitido, un byte por pixel. Layout y
   constantes en core/ayther/ayther_api.h (AYTHER_ATTRIB_*). Solo se llena con
   la suscripcion AYTHER_SUB_ATTRIBUTION activa. */
extern uint8 ayther_attrib[320 * 240];
extern uint32 ayther_attrib_width;
extern uint32 ayther_attrib_height;
extern uint32 ayther_attrib_flags;
#endif /* AYTHER_EXTENSIONS */

/* Function prototypes */
extern void render_init(void);
extern void render_reset(void);
extern void render_line(int line);
extern void blank_line(int line, int offset, int width);
extern void remap_line(int line);
extern void window_clip(unsigned int data, unsigned int sw);
extern void render_bg_m0(int line);
extern void render_bg_m1(int line);
extern void render_bg_m1x(int line);
extern void render_bg_m2(int line);
extern void render_bg_m3(int line);
extern void render_bg_m3x(int line);
extern void render_bg_inv(int line);
extern void render_bg_m4(int line);
extern void render_bg_m5(int line);
extern void render_bg_m5_vs(int line);
extern void render_bg_m5_vs_enhanced(int line);
extern void render_bg_m5_im2(int line);
extern void render_bg_m5_im2_vs(int line);
extern void render_obj_tms(int line);
extern void render_obj_m4(int line);
extern void render_obj_m5(int line);
extern void render_obj_m5_ste(int line);
extern void render_obj_m5_im2(int line);
extern void render_obj_m5_im2_ste(int line);
extern void parse_satb_tms(int line);
extern void parse_satb_m4(int line);
extern void parse_satb_m5(int line);
extern void parse_satb_m5_im2(int line);
extern void update_bg_pattern_cache_m4(int index);
extern void update_bg_pattern_cache_m5(int index);
extern void color_update_m4(int index, unsigned int data);
extern void color_update_m5(int index, unsigned int data);

/* Function pointers */
extern void (*render_bg)(int line);
extern void (*render_obj)(int line);
extern void (*parse_satb)(int line);
extern void (*update_bg_pattern_cache)(int index);

#endif /* _RENDER_H_ */
