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

#include "shared.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"

extern int8 reset_do_not_clear_buffers;

#ifndef HAVE_NO_SPRITE_LIMIT
#define MAX_SPRITES_PER_LINE 20
#define TMS_MAX_SPRITES_PER_LINE 4
#define MODE4_MAX_SPRITES_PER_LINE 8
#define MODE5_MAX_SPRITES_PER_LINE (bitmap.viewport.w >> 4)
#define MODE5_MAX_SPRITE_PIXELS max_sprite_pixels
#endif

/*** NTSC Filters ***/
extern md_ntsc_t *md_ntsc;
extern sms_ntsc_t *sms_ntsc;


/* Output pixels type*/
#if defined(USE_8BPP_RENDERING)
#define PIXEL_OUT_T uint8
#elif defined(USE_32BPP_RENDERING)
#define PIXEL_OUT_T uint32
#else
#define PIXEL_OUT_T uint16
#endif


/* Pixel priority look-up tables information */
#define LUT_MAX     (6)
#define LUT_SIZE    (0x10000)


#ifdef ALIGN_LONG
#undef READ_LONG
#undef WRITE_LONG

INLINE uint32 READ_LONG(void *address)
{
  if ((uint32)address & 3)
  {
#ifdef LSB_FIRST  /* little endian version */
    return ( *((uint8 *)address) +
        (*((uint8 *)address+1) << 8)  +
        (*((uint8 *)address+2) << 16) +
        (*((uint8 *)address+3) << 24) );
#else       /* big endian version */
    return ( *((uint8 *)address+3) +
        (*((uint8 *)address+2) << 8)  +
        (*((uint8 *)address+1) << 16) +
        (*((uint8 *)address)   << 24) );
#endif  /* LSB_FIRST */
  }
  else return *(uint32 *)address;
}

INLINE void WRITE_LONG(void *address, uint32 data)
{
  if ((uint32)address & 3)
  {
#ifdef LSB_FIRST
      *((uint8 *)address) =  data;
      *((uint8 *)address+1) = (data >> 8);
      *((uint8 *)address+2) = (data >> 16);
      *((uint8 *)address+3) = (data >> 24);
#else
      *((uint8 *)address+3) =  data;
      *((uint8 *)address+2) = (data >> 8);
      *((uint8 *)address+1) = (data >> 16);
      *((uint8 *)address)   = (data >> 24);
#endif /* LSB_FIRST */
    return;
  }
  else *(uint32 *)address = data;
}

#endif  /* ALIGN_LONG */


/* Draw 2-cell column (8-pixels high) */
/*
   Pattern cache base address: VHN NNNNNNNN NNYYYxxx
   with :
      x = Pattern Pixel (0-7)
      Y = Pattern Row (0-7)
      N = Pattern Number (0-2047) from pattern attribute
      H = Horizontal Flip bit from pattern attribute
      V = Vertical Flip bit from pattern attribute
*/
#define GET_LSB_TILE(ATTR, LINE) \
  atex = atex_table[(ATTR >> 13) & 7]; \
  src = (uint32 *)&bg_pattern_cache[(ATTR & 0x00001FFF) << 6 | (LINE)];
#define GET_MSB_TILE(ATTR, LINE) \
  atex = atex_table[(ATTR >> 29) & 7]; \
  src = (uint32 *)&bg_pattern_cache[(ATTR & 0x1FFF0000) >> 10 | (LINE)];

/* Draw 2-cell column (16 pixels high) */
/*
   Pattern cache base address: VHN NNNNNNNN NYYYYxxx
   with :
      x = Pattern Pixel (0-7)
      Y = Pattern Row (0-15)
      N = Pattern Number (0-1023)
      H = Horizontal Flip bit
      V = Vertical Flip bit
*/
#define GET_LSB_TILE_IM2(ATTR, LINE) \
  atex = atex_table[(ATTR >> 13) & 7]; \
  src = (uint32 *)&bg_pattern_cache[((ATTR & 0x000003FF) << 7 | (ATTR & 0x00001800) << 6 | (LINE)) ^ ((ATTR & 0x00001000) >> 6)];
#define GET_MSB_TILE_IM2(ATTR, LINE) \
  atex = atex_table[(ATTR >> 29) & 7]; \
  src = (uint32 *)&bg_pattern_cache[((ATTR & 0x03FF0000) >> 9 | (ATTR & 0x18000000) >> 10 | (LINE)) ^ ((ATTR & 0x10000000) >> 22)];

/*
   One column = 2 tiles
   Two pattern attributes are written in VRAM as two consecutives 16-bit words:

   P = priority bit
   C = color palette (2 bits)
   V = Vertical Flip bit
   H = Horizontal Flip bit
   N = Pattern Number (11 bits)

   (MSB) PCCVHNNN NNNNNNNN (LSB) (MSB) PCCVHNNN NNNNNNNN (LSB)
              PATTERN1                      PATTERN2

   Both pattern attributes are read from VRAM as one 32-bit word:

   LIT_ENDIAN: (MSB) PCCVHNNN NNNNNNNN PCCVHNNN NNNNNNNN (LSB)
                          PATTERN2          PATTERN1

   BIG_ENDIAN: (MSB) PCCVHNNN NNNNNNNN PCCVHNNN NNNNNNNN (LSB)
                          PATTERN1          PATTERN2


   In line buffers, one pixel = one byte: (msb) 0Pppcccc (lsb)
   with:
      P = priority bit  (from pattern attribute)
      p = color palette (from pattern attribute)
      c = color data (from pattern cache)

   One pattern = 8 pixels = 8 bytes = two 32-bit writes per pattern
*/

#ifdef ALIGN_LONG
#ifdef LSB_FIRST
#define DRAW_COLUMN(ATTR, LINE) \
  GET_LSB_TILE(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++; \
  GET_MSB_TILE(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++;
#define DRAW_COLUMN_IM2(ATTR, LINE) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++; \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++;
#else
#define DRAW_COLUMN(ATTR, LINE) \
  GET_MSB_TILE(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++; \
  GET_LSB_TILE(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++;
#define DRAW_COLUMN_IM2(ATTR, LINE) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++; \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  WRITE_LONG(dst, src[0] | atex); \
  dst++; \
  WRITE_LONG(dst, src[1] | atex); \
  dst++;
#endif
#else /* NOT ALIGNED */
#ifdef LSB_FIRST
#define DRAW_COLUMN(ATTR, LINE) \
  GET_LSB_TILE(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex); \
  GET_MSB_TILE(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex);
#define DRAW_COLUMN_IM2(ATTR, LINE) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex); \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex);
#else
#define DRAW_COLUMN(ATTR, LINE) \
  GET_MSB_TILE(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex); \
  GET_LSB_TILE(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex);
#define DRAW_COLUMN_IM2(ATTR, LINE) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex); \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  *dst++ = (src[0] | atex); \
  *dst++ = (src[1] | atex);
#endif
#endif /* ALIGN_LONG */

#ifdef ALT_RENDERER
/* Draw background tiles directly using priority look-up table */
/* SRC_A = layer A rendered pixel line (4 bytes = 4 pixels at once) */
/* SRC_B = layer B cached pixel line (4 bytes = 4 pixels at once) */
/* Note: cache address is always aligned so no need to use READ_LONG macro */
/* This might be faster or slower than original method, depending on  */
/* architecture (x86, PowerPC), cache size, memory access speed, etc...  */

#ifdef LSB_FIRST
#define DRAW_BG_TILE(SRC_A, SRC_B) \
  *lb++ = table[((SRC_B << 8) & 0xff00) | (SRC_A & 0xff)]; \
  *lb++ = table[(SRC_B & 0xff00) | ((SRC_A >> 8) & 0xff)]; \
  *lb++ = table[((SRC_B >> 8) & 0xff00) | ((SRC_A >> 16) & 0xff)]; \
  *lb++ = table[((SRC_B >> 16) & 0xff00) | ((SRC_A >> 24) & 0xff)];
#else
#define DRAW_BG_TILE(SRC_A, SRC_B) \
  *lb++ = table[((SRC_B >> 16) & 0xff00) | ((SRC_A >> 24) & 0xff)]; \
  *lb++ = table[((SRC_B >> 8) & 0xff00) | ((SRC_A >> 16) & 0xff)]; \
  *lb++ = table[(SRC_B & 0xff00) | ((SRC_A >> 8) & 0xff)]; \
  *lb++ = table[((SRC_B << 8) & 0xff00) | (SRC_A & 0xff)];
#endif

#ifdef ALIGN_LONG
#ifdef LSB_FIRST
#define DRAW_BG_COLUMN(ATTR, LINE, SRC_A, SRC_B) \
  GET_LSB_TILE(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_MSB_TILE(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#define DRAW_BG_COLUMN_IM2(ATTR, LINE, SRC_A, SRC_B) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#else
#define DRAW_BG_COLUMN(ATTR, LINE, SRC_A, SRC_B) \
  GET_MSB_TILE(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_LSB_TILE(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#define DRAW_BG_COLUMN_IM2(ATTR, LINE, SRC_A, SRC_B) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = READ_LONG((uint32 *)lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#endif
#else /* NOT ALIGNED */
#ifdef LSB_FIRST
#define DRAW_BG_COLUMN(ATTR, LINE, SRC_A, SRC_B) \
  GET_LSB_TILE(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_MSB_TILE(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#define DRAW_BG_COLUMN_IM2(ATTR, LINE, SRC_A, SRC_B) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#else
#define DRAW_BG_COLUMN(ATTR, LINE, SRC_A, SRC_B) \
  GET_MSB_TILE(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_LSB_TILE(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#define DRAW_BG_COLUMN_IM2(ATTR, LINE, SRC_A, SRC_B) \
  GET_MSB_TILE_IM2(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  GET_LSB_TILE_IM2(ATTR, LINE) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[0] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B) \
  SRC_A = *(uint32 *)(lb); \
  SRC_B = (src[1] | atex); \
  DRAW_BG_TILE(SRC_A, SRC_B)
#endif
#endif /* ALIGN_LONG */
#endif /* ALT_RENDERER */

#define DRAW_SPRITE_TILE(WIDTH,ATTR,TABLE)  \
  for (i=0;i<WIDTH;i++) \
  { \
    temp = *src++; \
    if (temp & 0x0f) \
    { \
      temp |= (lb[i] << 8); \
      lb[i] = TABLE[temp | ATTR]; \
      status |= ((temp & 0x8000) >> 10); \
    } \
  }

/* AYTHER (#31/#37/#41): DRAW_SPRITE_TILE con el bit de sprite EXACTO.

   `ayther_observed` es un parametro del clon, no una variable: en el clon
   rapido es la constante 0 y el compilador borra la rama entera, asi que el
   perfil sin subscribers no paga ni una comparacion por pixel.

   La regla es la misma que aplica la LUT: gana el sprite si tiene prioridad, o
   si lo que ya estaba no la tiene. Se replica en vez de comparar el resultado
   contra el valor anterior porque dos capas pueden dar el MISMO byte, y ahi
   comparar contesta cualquier cosa -- que es justo el defecto que esto arregla.

   El store es un OR y no una asignacion: los sprites se dibujan en orden, y un
   sprite posterior que PIERDE contra uno anterior no puede borrar la marca del
   que gano. */
#define AYTHER_DRAW_SPRITE_TILE(WIDTH,ATTR,TABLE)  \
  for (i=0;i<WIDTH;i++) \
  { \
    temp = *src++; \
    if (temp & 0x0f) \
    { \
      uint32 ayther_under = lb[i]; \
      temp |= (ayther_under << 8); \
      lb[i] = TABLE[temp | ATTR]; \
      status |= ((temp & 0x8000) >> 10); \
      if (ayther_observed) \
      { \
        uint32 ayther_sb = (ATTR) | (temp & 0x0F); \
        spx[i] |= (uint8)(AYTHER_SPRITE_WINS(ayther_sb, ayther_under) && \
                          !(ayther_sh && AYTHER_SPRITE_IS_OPERATOR(ayther_sb))); \
      } \
    } \
  }

#define DRAW_SPRITE_TILE_ACCURATE(WIDTH,ATTR,TABLE)  \
  for (i=0;i<WIDTH;i++) \
  { \
    temp = *src++; \
    if (temp & 0x0f) \
    { \
      temp |= (lb[i] << 8); \
      lb[i] = TABLE[temp | ATTR]; \
      if ((temp & 0x8000) && !(status & 0x20)) \
      { \
        spr_col = (v_counter << 8) | ((xpos + i + 13) >> 1); \
        status |= 0x20; \
      } \
    } \
  }

#define DRAW_SPRITE_TILE_ACCURATE_2X(WIDTH,ATTR,TABLE)  \
  for (i=0;i<WIDTH;i+=2) \
  { \
    temp = *src++; \
    if (temp & 0x0f) \
    { \
      temp |= (lb[i] << 8); \
      lb[i] = TABLE[temp | ATTR]; \
      if ((temp & 0x8000) && !(status & 0x20)) \
      { \
        spr_col = (v_counter << 8) | ((xpos + i + 13) >> 1); \
        status |= 0x20; \
      } \
      temp &= 0x00FF; \
      temp |= (lb[i+1] << 8); \
      lb[i+1] = TABLE[temp | ATTR]; \
      if ((temp & 0x8000) && !(status & 0x20)) \
      { \
        spr_col = (v_counter << 8) | ((xpos + i + 1 + 13) >> 1); \
        status |= 0x20; \
      } \
    } \
  }


/* Pixels conversion macro */
/* 4-bit color channels are either compressed to 2/3-bit or dithered to 5/6/8-bit equivalents */
/* 3:3:2 RGB */
#if defined(USE_8BPP_RENDERING)
#define MAKE_PIXEL(r,g,b)  (((r) >> 1) << 5 | ((g) >> 1) << 2 | (b) >> 2)

/* 5:5:5 RGB */
#elif defined(USE_15BPP_RENDERING)
#if defined(USE_ABGR)
#define MAKE_PIXEL(r,g,b) ((1 << 15) | (b) << 11 | ((b) >> 3) << 10 | (g) << 6 | ((g) >> 3) << 5 | (r) << 1 | (r) >> 3)
#else
#define MAKE_PIXEL(r,g,b) ((1 << 15) | (r) << 11 | ((r) >> 3) << 10 | (g) << 6 | ((g) >> 3) << 5 | (b) << 1 | (b) >> 3)
#endif
/* 5:6:5 RGB */
#elif defined(USE_16BPP_RENDERING)
#define MAKE_PIXEL(r,g,b) ((r) << 12 | ((r) >> 3) << 11 | (g) << 7 | ((g) >> 2) << 5 | (b) << 1 | (b) >> 3)

/* 8:8:8 RGB */
#elif defined(USE_32BPP_RENDERING)
#define MAKE_PIXEL(r,g,b) ((0xff << 24) | (r) << 20 | (r) << 16 | (g) << 12 | (g)  << 8 | (b) << 4 | (b))
#endif

/* Window & Plane A clipping */
static struct clip_t
{
  uint8 left;
  uint8 right;
  uint8 enable;
} clip[2];

/* Pattern attribute (priority + palette bits) expansion table */
static const uint32 atex_table[] =
{
  0x00000000,
  0x10101010,
  0x20202020,
  0x30303030,
  0x40404040,
  0x50505050,
  0x60606060,
  0x70707070
};

/* fixed Master System palette for Modes 0,1,2,3 */
static const uint8 tms_crom[16] =
{
  0x00, 0x00, 0x08, 0x0C,
  0x10, 0x30, 0x01, 0x3C,
  0x02, 0x03, 0x05, 0x0F,
  0x04, 0x33, 0x15, 0x3F
};

/* original SG-1000 palette */
#if defined(USE_8BPP_RENDERING)
static const uint8 tms_palette[16] =
{
  0x00, 0x00, 0x39, 0x79,
  0x4B, 0x6F, 0xC9, 0x5B,
  0xE9, 0xED, 0xD5, 0xD9,
  0x35, 0xCE, 0xDA, 0xFF
};

#elif defined(USE_15BPP_RENDERING)
static const uint16 tms_palette[16] =
{
  0x8000, 0x8000, 0x9308, 0xAF6F,
  0xA95D, 0xBDDF, 0xE949, 0xA3BE,
  0xFD4A, 0xFDEF, 0xEB0A, 0xF330,
  0x92A7, 0xE177, 0xE739, 0xFFFF
};

#elif defined(USE_16BPP_RENDERING)
static const uint16 tms_palette[16] =
{
  0x0000, 0x0000, 0x2648, 0x5ECF,
  0x52BD, 0x7BBE, 0xD289, 0x475E,
  0xF2AA, 0xFBCF, 0xD60A, 0xE670,
  0x2567, 0xC2F7, 0xCE59, 0xFFFF
};

#elif defined(USE_32BPP_RENDERING)
static const uint32 tms_palette[16] =
{
  0xFF000000, 0xFF000000, 0xFF21C842, 0xFF5EDC78,
  0xFF5455ED, 0xFF7D76FC, 0xFFD4524D, 0xFF42EBF5,
  0xFFFC5554, 0xFFFF7978, 0xFFD4C154, 0xFFE6CE80,
  0xFF21B03B, 0xFFC95BB4, 0xFFCCCCCC, 0xFFFFFFFF
};
#endif

/* AYTHER (#31): el cuarteo por canal vive en su propio header para que el
   test pueda verificar los valores sin levantar el core. */
#include "ayther/ayther_dim.h"
#include "ayther/ayther_sprite_px.h"

/* Cached and flipped patterns */
static uint8 ALIGNED_(4) bg_pattern_cache[0x80000];

/* Sprite pattern name offset look-up table (Mode 5) */
static uint8 name_lut[0x400];

/* Bitplane to packed pixel look-up table (Mode 4) */
static uint32 bp_lut[0x10000];

/* Layer priority pixel look-up tables */
static uint8 lut[LUT_MAX][LUT_SIZE];

/* Output pixel data look-up tables*/
static PIXEL_OUT_T pixel[0x100];
static PIXEL_OUT_T pixel_lut[3][0x200];
static PIXEL_OUT_T pixel_lut_m4[0x40];

/* Background & Sprite line buffers */
static uint8 linebuf[2][0x200];

/* Sprite limit flag */
static uint8 spr_ovr;

/* Sprite parsing lists */
typedef struct
{
  uint16 ypos;
  uint16 xpos;
  uint16 attr;
  uint16 size;
} object_info_t;

/* AYTHER (#270): 80 en vez de MAX_SPRITES_PER_LINE (20) — headroom para la
   recomposición con límite de sprites desactivado (ayther_rc_nolimit). El
   render normal sigue acotado por MODE5_MAX_SPRITES_PER_LINE (bounds por
   `max`, no por la capacidad del array): cero cambio de comportamiento. */
#ifdef AYTHER_EXTENSIONS
static object_info_t obj_info[2][80];
#else
static object_info_t obj_info[2][MAX_SPRITES_PER_LINE];
#endif

/* Sprite Counter */
static uint8 object_count[2];

/* Sprite Collision Info */
uint16 spr_col;

#ifdef AYTHER_EXTENSIONS

/* AYTHER (#28): ALT_RENDERER trae su propio juego de renderers Mode 5, sin
   ninguno de los gates de este fork. Sólo lo activan GameCube, Wii, GCW0, Vita
   y PSP2 —plataformas que este fork no compila: sus targets son win64, unix y
   osx—, así que la combinación no se despacha en ningún lado.
   Un #error es mejor que un fallback en tiempo de ejecución: la alternativa
   era que alguien portara el fork a una de esas plataformas y descubriera que
   todas las máscaras son no-op en silencio. Si algún día hace falta, lo que
   corresponde es gatear esos renderers, no borrar esta línea. */
#ifdef ALT_RENDERER
#error "AYTHER_EXTENSIONS y ALT_RENDERER son incompatibles: los renderers de ALT_RENDERER no tienen los gates de capa ni de supresion (ver #28)."
#endif

/* AYTHER fork delta: máscara de capas visibles (id de memoria privado 0x102).
   Bit set = visible. La leen render_bg_m5/_vs (planos) y render_line (sprites). */
uint8 ayther_layer_mask = 0xFF;
#define AYTHER_CONTROLS_ACTIVE \
  AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS)
/* #41: que render_line tome el clon OBSERVADO no es lo mismo que que los
   controles esten activos. La atribucion no controla nada -- solo mira-- pero
   se captura adentro de ese clon, asi que suscribirse SOLO a ATTRIBUTION
   dejaba la region vacia para siempre y sin ningun error de por medio: el
   frontend pedia el dato, el core contestaba OK, y devolvia cero bytes.

   Peor todavia con el clon rapido de render_bg, que usa `merge_fast` -- sin
   el hook--: ni siquiera las capas salian, y el frame entero decia
   "backdrop". */
/* La lista tiene que llevar TODA suscripción cuyo dato se capture adentro del
   clon observado. Olvidar una no da un error: da una región que contesta OK y
   viene vacía, que es como se descubrió esto en #41 y como volvió a pasar al
   agregar el estado por línea en #42. Si mañana entra otra región que se llene
   ahí adentro, su bit va en esta lista. */
#define AYTHER_OBSERVED_ACTIVE                    \
  AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS |  \
                    AYTHER_SUB_ATTRIBUTION |      \
                    AYTHER_SUB_LINE_STATE |       \
                    AYTHER_SUB_LINE_CRAM |        \
                    AYTHER_SUB_LINE_CELLS)
#define AYTHER_HIDE_A \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_A))
#define AYTHER_HIDE_B \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_B))
#define AYTHER_HIDE_W \
  (AYTHER_CONTROLS_ACTIVE && !(ayther_layer_mask & AYTHER_LAYER_W))
#define AYTHER_SHOW_OBJ \
  (!AYTHER_CONTROLS_ACTIVE || (ayther_layer_mask & AYTHER_LAYER_OBJ))

/* AYTHER fork delta: atenuar las capas NO-sprite (id de memoria 0x108, escribible).
   0 = render normal (bit-exact, sin costo). !=0 = "dim mode": los píxeles que NO
   son de sprite se emiten al 25% (preponderancia visual de los sprites — viewport
   de Animación del Lab). Implementación SIN tocar render_obj: render_line snapshotea
   linebuf[0] tras render_bg y lo difea tras render_obj (los píxeles que cambiaron
   son sprites → ayther_sprite_px); remap_line atenúa los demás. Sólo en el frame
   visible (produce); la re-sim bare corre con dim=0. Asume salida RGB565 (el build
   del fork: -DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565). */
uint8 ayther_layer_dim = 0;
static uint8 ayther_bg_snap[0x200];    /* linebuf[0] tras render_bg (fallback Mode 4) */
static uint8 ayther_sprite_px[0x200];  /* 1 = ese pixel es de sprite */

/* AYTHER (#31/#37/#41): "este pixel es de un sprite" se derivaba comparando
   linebuf[0] antes y despues de render_obj. Esa via da dos respuestas malas:

     - un pixel de sprite cuyo byte COINCIDE con el del fondo -- mismo indice,
       misma prioridad-- no aparece en el diff. El dim lo dejaba a brillo pleno
       y la atribucion no lo marcaba: un agujero adentro del sprite;
     - un pixel que solo cambio por el OPERADOR de shadow/highlight aparece, y
       se marcaba como sprite. No lo es: no pone color.

   Ahora se escribe donde se DECIDE, y ese lugar no es uno solo, porque las dos
   familias de renderers de sprites resuelven la prioridad en sitios distintos:

     render_obj_m5 / _im2      dibujan DIRECTO en linebuf[0], y la LUT resuelve
                               sprite-contra-fondo pixel a pixel. La decision
                               esta en el bucle, asi que el store va ahi.
     render_obj_m5_ste / _im2_ste  dibujan en linebuf[1] y mergean despues. La
                               decision esta en el merge, asi que el store va
                               ahi.

   Que sean dos no es duplicacion por gusto: es que la pregunta se contesta en
   dos lugares distintos, y contestarla en el que no decide es exactamente el
   error que el diff cometia. Ninguna de las dos cuesta nada en el perfil
   rapido: el store del bucle vive bajo un parametro de clon que el compilador
   pliega, y el del merge bajo una bandera que solo se enciende con dim o
   atribucion activos. */
static int ayther_obj_pass = 0;      /* 1 = corriendo render_obj              */
static int ayther_obj_px_exact = 0;  /* 1 = alguien lleno ayther_sprite_px    */

/* AYTHER fork delta: CAPTURA de los sprites realmente PARSEADOS por parse_satb
   durante el frame (ids 0x10B lista / 0x10C contador, reset legacy). Algunos
   intros reescriben el SAT in-place a MITAD de frame (el genio del logo Sega: el
   juego carga su SAT justo antes de sus scanlines y lo sobreescribe con placeholders
   después), así que NINGÚN estado del SAT a un instante fijo (fin de frame, línea 0)
   tiene todos los sprites. La única fuente fiable es lo que parse_satb parsea EN SUS
   LÍNEAS. Registramos cada sprite agregado (Y/X/attr ya resueltos + w/h), deduplicado
   por su identidad completa mediante hash fijo O(1) amortizado. La capa libretro
   avanza la generación de captura antes del frame;
   los resets manuales legacy siguen siendo válidos. Produce-only; no afecta render. Valores (no bytes) →
   sin problemas de endianness. */

/* AYTHER fork delta: bitmask de slots SAT suprimidos (id de memoria 0x103,
   escribible). Bit set = ese slot NO se parsea → su sprite no se dibuja (ocultar
   sprite por hash en el Lab). El frontend lo setea SÓLO para el frame visible
   (produce) y lo vacía para la re-simulación bare → status del VDP intacto. */
uint8 ayther_sprite_suppress[16] = {0};
/* #36: resumen de la mascara de arriba, mantenido en write_control. Sin esto,
   estar suscrito a RENDER_CONTROLS bastaba para que parse_satb_m5 tomara el
   parser completo linea por linea aunque no hubiera un solo slot suprimido --
   se pagaba la capacidad de suprimir, no la supresion-. */
uint8 ayther_sprite_suppress_active = 0;
#define AYTHER_SPR_SUPPRESSED_ACTIVE(active, slot) \
  ((active) && \
   (ayther_sprite_suppress[((slot) >> 3) & 0x0F] & (1 << ((slot) & 7))))

/* AYTHER (#270): toggles de la recomposición (ayther_recompose_frame). Sólo esa
   función los enciende, y los apaga antes de volver: el render normal corre
   SIEMPRE con ambos en 0 (cero cambio de comportamiento). */
static uint8 ayther_rc_nolimit = 0;  /* sin límite de sprites por línea (20/línea + presupuesto de px) */
static uint8 ayther_rc_nomask  = 0;  /* sin máscara de sprites (sprite en x=0 no tapa los siguientes) */

/* AYTHER fork delta: máscara de celdas de tile suprimidas (id de memoria 0x104,
   escribible). Grilla de 8x8 px en coordenadas del frame que ve el frontend
   (col = (x+viewport.x)>>3, fila = (line+viewport.y)>>3); bit set = "ocultar este
   tile". OCULTAR = PELAR UNA CAPA en el merge (no pintar backdrop): si el pixel
   visible vino del primer plano (A/Window) se revela el Plano B de atrás; si vino
   de B se revela el backdrop. Así "ver qué hay detrás" del elemento, no un agujero.
   El frontend mapea el hash de tile oculto → celdas del frame y setea la máscara
   SÓLO para el frame visible (produce); la re-sim bare corre con la máscara vacía.
   Stride fijo de 64 columnas (los modos de MD usan ≤40) → filas alineadas a byte
   (8 bytes/fila) para un descarte rápido por línea. 64x64 celdas = 512 bytes. */
#define AYTHER_TILE_COLS 64
#define AYTHER_TILE_ROWS 64
uint8 ayther_tile_suppress[(AYTHER_TILE_COLS * AYTHER_TILE_ROWS) / 8] = {0};
#define AYTHER_TILE_SUPPRESSED(row, col) \
  (ayther_tile_suppress[(((row) * AYTHER_TILE_COLS) + (col)) >> 3] \
   & (1 << ((((row) * AYTHER_TILE_COLS) + (col)) & 7)))

/* AYTHER fork delta: tiles de PLANO suprimidos por (plano, patrón, paleta) — id de
   memoria 0x105, escribible (Fase 2b del Lab: ocultar un tile de fondo). A diferencia
   de 0x104 (por celda de pantalla), esto oculta el GRÁFICO del tile dondequiera que
   aparezca en su plano, sin depender del scroll: render_bg_m5/_vs lo consultan por
   columna al leer la nametable. 3 planos (A=0, B=1, Window=2) × bitmap de
   (patrón<<2 | paleta) = 2048×4 = 8192 bits = 1024 bytes/plano = 3072 bytes. Identidad
   idéntica a collect_plane_tiles del frontend (patrón = word & 0x7FF, paleta = bits
   13-14). Ocultar un tile de A/Window deja su columna transparente → se ve el Plano B
   (o backdrop); ocultar uno de B → backdrop. Sólo en el frame visible (produce); la
   re-sim bare corre con la máscara vacía (active=0) → determinismo de video intacto. */
uint8 ayther_plane_tile_suppress[3 * 1024] = {0};
uint8 ayther_plane_suppress_active = 0;   /* id 0x106: lo setea el frontend (1 = hay algo oculto) */
#define AYTHER_PSUP(plane) \
  ((AYTHER_CONTROLS_ACTIVE && ayther_plane_suppress_active) \
    ? &ayther_plane_tile_suppress[(plane) * 1024] : (const uint8 *)0)
/* Clave e índice de bit de una celda de 16 bits (patrón 0x7FF | paleta bits 13-14). */
#define AYTHER_PTKEY(cell)     ((((uint32)(cell) & 0x7FFu) << 2) | (((uint32)(cell) >> 13) & 3u))
#define AYTHER_PTSUP(ps, cell) ((ps)[AYTHER_PTKEY(cell) >> 3] & (1u << (AYTHER_PTKEY(cell) & 7u)))

/* Estado del "peel" para la línea en curso (lo arma render_line antes de
   render_bg y lo apaga antes de los sprites → no pela los merges de sprites).
   Sólo se activa en líneas con alguna celda marcada → merge() conserva su fast
   path para todos los demás casos/juegos. */
static int ayther_peel_active = 0;   /* la línea actual tiene celdas marcadas */
static int ayther_peel_row    = 0;   /* fila de celda (frame, con borde) de la línea */
static int ayther_peel_vx     = 0;   /* desplazamiento del borde izquierdo (viewport.x) */

/* AYTHER (#41): atribución por píxel — quién pintó cada uno.
 *
 * El frontend deducía esto recomponiendo el frame varias veces con distintas
 * máscaras y diffeando: N pasadas de render para una respuesta que el VDP ya
 * conoce mientras dibuja. Y el atajo que existía —el diff de `layer_dim`— sólo
 * contesta "sprite sí/no", y mal: un píxel de sprite cuyo byte coincide con el
 * del fondo no aparece en el diff.
 *
 * Se captura dentro de `merge()`, que es el punto por el que pasan los CINCO
 * renderers Mode 5. Poner los stores en el bucle interno de cada uno habría
 * significado tocar el código más caliente del emulador cinco veces, y la
 * información que hace falta —qué capa ganó— recién existe en el merge.
 *
 * La capa se decide replicando la regla de prioridad de la LUT, no comparando
 * bytes: dos capas pueden producir el mismo valor y ahí comparar da una
 * respuesta arbitraria justo en los píxeles donde importa. */
/* AYTHER (#42): el estado de render POR SCANLINE.

   El raster journal lo aproxima desde el lado de la ESCRITURA: hay que adivinar
   en que ciclo de que linea cayo cada write y que efecto tuvo. Capturarlo donde
   el renderer lo CONSUME es exacto -- es literalmente el valor que uso-- y ademas
   mas barato: no hay nada que reconstruir.

   240 lineas es el maximo que este core emite (PAL con overscan). El buffer es
   estatico y del tamanio del peor caso: 240 x 32 B de registros mas 240 x 128 B
   de CRAM son 38 KB, del mismo orden que el buffer de atribucion que ya existe,
   y evita una asignacion en un camino que corre por linea. */
#define AYTHER_LINE_MAX 240u
ayther_line_regs_v1 ayther_line_regs[AYTHER_LINE_MAX];
ayther_line_cells_v1 ayther_line_cells[AYTHER_LINE_MAX];
/* #42.C: la fila que se esta capturando, y el plano. El renderer dibuja plano
   por plano, asi que un puntero al arreglo del plano en curso evita pasar el
   plano por cada columna. */
static uint32 *ayther_cell_dst;
static uint8 *ayther_cell_count;
uint8 ayther_line_cram[AYTHER_LINE_MAX][128];
uint32 ayther_line_count = 0;
uint32 ayther_line_flags = 0;
static uint8 ayther_line_cram_first[128];
static int ayther_line_cram_dirty = 0;

#define AYTHER_LINE_STATE_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_STATE)
#define AYTHER_LINE_CRAM_ACTIVE  AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_CRAM)
#define AYTHER_LINE_CELLS_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_LINE_CELLS)

/* #42.C: se guarda el par crudo, tal como el renderer lo consumio. Un store a
   un puntero que ya esta en un registro; sin lecturas extra de VRAM. */
#define AYTHER_CELL_RECORD(pair)                                          \
  do {                                                                    \
    if (ayther_observed && ayther_cell_dst &&                             \
        *ayther_cell_count < AYTHER_LINE_CELL_COLUMNS)                    \
      ayther_cell_dst[(*ayther_cell_count)++] = (uint32)(pair);           \
  } while (0)

/* Se llama al empezar el frame: lo que sigue pertenece a ESTE frame y no al
   anterior. `lines` queda en cero hasta que las lineas se dibujen, asi que un
   consumidor que lea a mitad de camino ve lo que hay y no basura vieja. */
void ayther_line_state_begin_frame(void)
{
  ayther_line_count = 0;
  ayther_line_flags = 0;
  ayther_line_cram_dirty = 0;
}

/* La CRAM se copia entera por linea solo si CAMBIO a mitad de frame. En un
   frame sin writes de paleta -- que es la mayoria- se guarda una sola copia y se
   marca CRAM_UNIFORM: 128 B en vez de 30 KB, y el consumidor sabe leerlo. */
static void ayther_line_capture_cram(uint32 line)
{
  if (!ayther_line_cram_dirty)
  {
    if (line == 0)
    {
      memcpy(ayther_line_cram_first, cram, sizeof(ayther_line_cram_first));
      memcpy(ayther_line_cram[0], cram, sizeof(ayther_line_cram[0]));
      return;
    }
    if (!memcmp(ayther_line_cram_first, cram, sizeof(ayther_line_cram_first)))
      return;                      /* sigue igual: no hay nada que guardar */
    /* Cambio: hay que rellenar hacia atras lo que se venia salteando. */
    {
      uint32 i;
      for (i = 1; i < line; ++i)
        memcpy(ayther_line_cram[i], ayther_line_cram_first,
               sizeof(ayther_line_cram[0]));
    }
    ayther_line_cram_dirty = 1;
  }
  memcpy(ayther_line_cram[line], cram, sizeof(ayther_line_cram[0]));
}

/* Capturado a la ENTRADA de render_bg_m5*, con los valores ya resueltos: el
   scroll de la linea, las bases de las tablas y los recortes de la ventana. */
static void ayther_line_capture(uint32 line, uint32 xscroll,
                                                uint32 yscroll)
{
  ayther_line_regs_v1 *r;

  if (line >= AYTHER_LINE_MAX)
  {
    ayther_line_flags |= AYTHER_LINES_OVERFLOW;
    return;
  }
  r = &ayther_line_regs[line];
#ifdef LSB_FIRST
  r->xscroll_a = (uint16)(xscroll & 0x3FF);
  r->xscroll_b = (uint16)((xscroll >> 16) & 0x3FF);
  r->yscroll_a = (uint16)(yscroll & 0x3FF);
  r->yscroll_b = (uint16)((yscroll >> 16) & 0x3FF);
#else
  r->xscroll_a = (uint16)((xscroll >> 16) & 0x3FF);
  r->xscroll_b = (uint16)(xscroll & 0x3FF);
  r->yscroll_a = (uint16)((yscroll >> 16) & 0x3FF);
  r->yscroll_b = (uint16)(yscroll & 0x3FF);
#endif
  r->ntab = ntab; r->ntbb = ntbb; r->ntwb = ntwb;
  r->hscb = hscb; r->satb = satb;
  r->reg1 = reg[1]; r->reg7 = reg[7]; r->reg11 = reg[11]; r->reg12 = reg[12];
  r->reg13 = reg[13]; r->reg16 = reg[16]; r->reg17 = reg[17]; r->reg18 = reg[18];
  r->clip_a_start = (uint8)clip[0].left;
  r->clip_a_end   = (uint8)clip[0].right;
  r->clip_w_start = (uint8)clip[1].left;
  r->clip_w_end   = (uint8)clip[1].right;
  r->flags = (uint8)((clip[1].enable ? AYTHER_LINE_WINDOW_ACTIVE : 0) |
                     ((reg[11] & 0x04) ? AYTHER_LINE_VSCROLL_COLUMN : 0));
  r->reserved0 = 0;

  if (AYTHER_LINE_CRAM_ACTIVE)
  {
    ayther_line_capture_cram(line);
    /* Mientras la CRAM no haya cambiado, la region entrega UNA entrada y este
       flag; el consumidor sabe leerlo. 128 B en vez de 30 KB. */
    if (ayther_line_cram_dirty)
      ayther_line_flags &= ~AYTHER_LINES_CRAM_UNIFORM;
    else
      ayther_line_flags |= AYTHER_LINES_CRAM_UNIFORM;
  }

  if (line + 1u > ayther_line_count)
    ayther_line_count = line + 1u;
}

/* #42.C: abre la captura de un plano para la linea en curso. `row` y `shift`
   son por linea y por plano -- el renderer los calcula una vez-, asi que van
   aca y no repetidos en cada columna. */
static void ayther_cells_open(uint32 line, int plane, uint32 row, uint32 shift)
{
  ayther_line_cells_v1 *c;
  if (line >= AYTHER_LINE_MAX) { ayther_cell_dst = 0; return; }
  c = &ayther_line_cells[line];
  switch (plane)
  {
    case 0: ayther_cell_dst = c->name_a; ayther_cell_count = &c->cols_a;
            c->row_a = (uint8)row; c->shift_a = (uint8)shift; break;
    case 1: ayther_cell_dst = c->name_b; ayther_cell_count = &c->cols_b;
            c->row_b = (uint8)row; c->shift_b = (uint8)shift; break;
    default: ayther_cell_dst = c->name_w; ayther_cell_count = &c->cols_w;
            c->row_w = (uint8)row; break;
  }
  *ayther_cell_count = 0;
}

static void ayther_cells_close(void)
{
  ayther_cell_dst = 0;
}

static void ayther_cells_begin_line(uint32 line)
{
  if (line < AYTHER_LINE_MAX)
    memset(&ayther_line_cells[line], 0, sizeof(ayther_line_cells[line]));
  ayther_cell_dst = 0;
}

uint8 ayther_attrib[320 * 240];
uint32 ayther_attrib_width = 0;
uint32 ayther_attrib_height = 0;
uint32 ayther_attrib_flags = 0;
/* Fila que se está dibujando y si hay que capturar. El flag lo enciende
   `render_line` y NO la recomposición: recomponer es una lectura, y dejar que
   pisara la atribución del frame haría que el resultado dependiera de si
   alguien miró. */
static int ayther_attrib_capture = 0;
static int ayther_attrib_row = 0;
static uint8 ayther_attrib_line[0x200];

#define AYTHER_ATTRIB_ACTIVE AYTHER_SUBSCRIBED(AYTHER_SUB_ATTRIBUTION)

#define AYTHER_LAYER_DIM_ACTIVE \
  (AYTHER_CONTROLS_ACTIVE && ayther_layer_dim)
#define AYTHER_RC_NOLIMIT_ACTIVE ayther_rc_nolimit
#define AYTHER_RC_NOMASK_ACTIVE ayther_rc_nomask

/* Huella de TODO lo que el frontend puede cambiar sin correr un frame (#26).
 *
 * El cache de recomposición se indexaba por (generación de frame, flags,
 * layer_mask). Las otras regiones de control —supresión de sprites, celdas,
 * tiles de plano, dim— también cambian los píxeles que produce el recompositor,
 * y no estaban en la clave: escribir una de ellas entre dos recomposiciones del
 * MISMO frame devolvía la imagen vieja. Silencioso, y peor en el Lab que en
 * ningún lado, porque ahí la secuencia normal es exactamente esa: el emulador
 * pausado y la UI cambiando máscaras sobre un frame fijo.
 *
 * Es una huella de CONTENIDO y no un contador porque los ids privados 0x102-0x108
 * siguen entregando punteros mutables por `retro_get_memory_data`: un consumidor
 * legacy escribe la máscara sin pasar por `write_control`, así que no hay hook
 * donde incrementar nada. Lo único que ve las dos rutas es el contenido.
 *
 * Entra también la máscara de suscripción, porque los controles sólo tienen
 * efecto cuando RENDER_CONTROLS está activa: suscribir o desuscribir cambia el
 * frame recompuesto sin tocar un solo byte de las máscaras.
 *
 * Costo: FNV-1a sobre ~3,6 KB. Es menos de lo que cuesta el memcpy de 150 KB que
 * este mismo cache hace en un acierto, así que no cambia el balance de tenerlo. */
uint64_t ayther_controls_fingerprint(void)
{
  uint64_t h = UINT64_C(0xCBF29CE484222325);
  size_t i;

#define AYTHER_FP_BYTE(b) \
  do { h ^= (uint64_t)(uint8)(b); h *= UINT64_C(0x100000001B3); } while (0)
#define AYTHER_FP_BUF(p, n) \
  do { for (i = 0; i < (size_t)(n); ++i) AYTHER_FP_BYTE(((const uint8 *)(p))[i]); } while (0)

  AYTHER_FP_BYTE(AYTHER_CONTROLS_ACTIVE ? 1 : 0);
  AYTHER_FP_BYTE(ayther_layer_mask);
  AYTHER_FP_BYTE(ayther_layer_dim);
  AYTHER_FP_BYTE(ayther_plane_suppress_active);
  AYTHER_FP_BUF(ayther_sprite_suppress, sizeof(ayther_sprite_suppress));
  AYTHER_FP_BUF(ayther_tile_suppress, sizeof(ayther_tile_suppress));
  AYTHER_FP_BUF(ayther_plane_tile_suppress, sizeof(ayther_plane_tile_suppress));

#undef AYTHER_FP_BUF
#undef AYTHER_FP_BYTE

  return h;
}

#else

#define AYTHER_CONTROLS_ACTIVE 0
#define AYTHER_OBSERVED_ACTIVE 0
#define AYTHER_HIDE_A 0
#define AYTHER_HIDE_B 0
#define AYTHER_HIDE_W 0
#define AYTHER_SHOW_OBJ 1
#define AYTHER_LAYER_DIM_ACTIVE 0
#define AYTHER_RC_NOLIMIT_ACTIVE 0
#define AYTHER_RC_NOMASK_ACTIVE 0
#define AYTHER_SPR_SUPPRESSED_ACTIVE(active, slot) ((void)(active), 0)
#define AYTHER_PSUP(plane) ((const uint8 *)0)
#define ayther_sprite_capture_record(yr, xr, attr, w, h, sat_idx, chain_pos) ((void)0)
#define AYTHER_CELL_RECORD(pair) ((void)0)

#endif /* AYTHER_EXTENSIONS */

/* Function pointers */
void (*render_bg)(int line);
void (*render_obj)(int line);
void (*parse_satb)(int line);
void (*update_bg_pattern_cache)(int index);


/*--------------------------------------------------------------------------*/
/* Sprite pattern name offset look-up table function (Mode 5)               */
/*--------------------------------------------------------------------------*/

static void make_name_lut(void)
{
  int vcol, vrow;
  int width, height;
  int flipx, flipy;
  int i;

  for (i = 0; i < 0x400; i += 1)
  {
    /* Sprite settings */
    vcol = i & 3;
    vrow = (i >> 2) & 3;
    height = (i >> 4) & 3;
    width  = (i >> 6) & 3;
    flipx  = (i >> 8) & 1;
    flipy  = (i >> 9) & 1;

    if ((vrow > height) || vcol > width)
    {
      /* Invalid settings (unused) */
      name_lut[i] = -1;
    }
    else
    {
      /* Adjust column & row index if sprite is flipped */
      if(flipx) vcol = (width - vcol);
      if(flipy) vrow = (height - vrow);

      /* Pattern offset (pattern order is up->down->left->right) */
      name_lut[i] = vrow + (vcol * (height + 1));
    }
  }
}


/*--------------------------------------------------------------------------*/
/* Bitplane to packed pixel look-up table function (Mode 4)                 */
/*--------------------------------------------------------------------------*/

static void make_bp_lut(void)
{
  int x,i,j;
  uint32 out;

  /* ---------------------- */
  /* Pattern color encoding */
  /* -------------------------------------------------------------------------*/
  /* 4 byteplanes are required to define one pattern line (8 pixels)          */
  /* A single pixel color is coded with 4 bits (c3 c2 c1 c0)                  */
  /* Each bit is coming from byteplane bits, as explained below:              */
  /* pixel 0: c3 = bp3 bit 7, c2 = bp2 bit 7, c1 = bp1 bit 7, c0 = bp0 bit 7  */
  /* pixel 1: c3 = bp3 bit 6, c2 = bp2 bit 6, c1 = bp1 bit 6, c0 = bp0 bit 6  */
  /* ...                                                                      */
  /* pixel 7: c3 = bp3 bit 0, c2 = bp2 bit 0, c1 = bp1 bit 0, c0 = bp0 bit 0  */
  /* -------------------------------------------------------------------------*/

  for(i = 0; i < 0x100; i++)
  for(j = 0; j < 0x100; j++)
  {
    out = 0;
    for(x = 0; x < 8; x++)
    {
      /* pixel line data = hh00gg00ff00ee00dd00cc00bb00aa00 (32-bit) */
      /* aa-hh = upper or lower 2-bit values of pixels 0-7 (shifted) */
      out |= (j & (0x80 >> x)) ? (uint32)(8 << (x << 2)) : 0;
      out |= (i & (0x80 >> x)) ? (uint32)(4 << (x << 2)) : 0;
    }

    /* i = low byte in VRAM  (bp0 or bp2) */
    /* j = high byte in VRAM (bp1 or bp3) */
 #ifdef LSB_FIRST
    bp_lut[(j << 8) | (i)] = out;
 #else
    bp_lut[(i << 8) | (j)] = out;
 #endif
   }
}


/*--------------------------------------------------------------------------*/
/* Layers priority pixel look-up tables functions                           */
/*--------------------------------------------------------------------------*/

/* Input (bx):  d5-d0=color, d6=priority, d7=unused */
/* Input (ax):  d5-d0=color, d6=priority, d7=unused */
/* Output:    d5-d0=color, d6=priority, d7=zero */
static uint32 make_lut_bg(uint32 bx, uint32 ax)
{
  int bf = (bx & 0x7F);
  int bp = (bx & 0x40);
  int b  = (bx & 0x0F);

  int af = (ax & 0x7F);
  int ap = (ax & 0x40);
  int a  = (ax & 0x0F);

  int c = (ap ? (a ? af : bf) : (bp ? (b ? bf : af) : (a ? af : bf)));

  /* Strip palette & priority bits from transparent pixels */
  if((c & 0x0F) == 0x00) c &= 0x80;

  return (c);
}

/* Input (bx):  d5-d0=color, d6=priority, d7=unused */
/* Input (sx):  d5-d0=color, d6=priority, d7=unused */
/* Output:    d5-d0=color, d6=priority, d7=intensity select (0=half/1=normal) */
static uint32 make_lut_bg_ste(uint32 bx, uint32 ax)
{
  int bf = (bx & 0x7F);
  int bp = (bx & 0x40);
  int b  = (bx & 0x0F);

  int af = (ax & 0x7F);
  int ap = (ax & 0x40);
  int a  = (ax & 0x0F);

  int c = (ap ? (a ? af : bf) : (bp ? (b ? bf : af) : (a ? af : bf)));

  /* Half intensity when both pixels are low priority */
  c |= ((ap | bp) << 1);

  /* Strip palette & priority bits from transparent pixels */
  if((c & 0x0F) == 0x00) c &= 0x80;

  return (c);
}

/* Input (bx):  d5-d0=color, d6=priority/1, d7=sprite pixel marker */
/* Input (sx):  d5-d0=color, d6=priority, d7=unused */
/* Output:    d5-d0=color, d6=priority, d7=sprite pixel marker */
static uint32 make_lut_obj(uint32 bx, uint32 sx)
{
  int c;

  int bf = (bx & 0x7F);
  int bs = (bx & 0x80);
  int sf = (sx & 0x7F);

  if((sx & 0x0F) == 0) return bx;

  c = (bs ? bf : sf);

  /* Strip palette bits from transparent pixels */
  if((c & 0x0F) == 0x00) c &= 0xC0;

  return (c | 0x80);
}


/* Input (bx):  d5-d0=color, d6=priority, d7=opaque sprite pixel marker */
/* Input (sx):  d5-d0=color, d6=priority, d7=unused */
/* Output:    d5-d0=color, d6=zero/priority, d7=opaque sprite pixel marker */
static uint32 make_lut_bgobj(uint32 bx, uint32 sx)
{
  int c;

  int bf = (bx & 0x3F);
  int bs = (bx & 0x80);
  int bp = (bx & 0x40);
  int b  = (bx & 0x0F);

  int sf = (sx & 0x3F);
  int sp = (sx & 0x40);
  int s  = (sx & 0x0F);

  if(s == 0) return bx;

  /* Previous sprite has higher priority */
  if(bs) return bx;

  c = (sp ? sf : (bp ? (b ? bf : sf) : sf));

  /* Strip palette & priority bits from transparent pixels */
  if((c & 0x0F) == 0x00) c &= 0x80;

  return (c | 0x80);
}

/* Input (bx):  d5-d0=color, d6=priority, d7=intensity (half/normal) */
/* Input (sx):  d5-d0=color, d6=priority, d7=sprite marker */
/* Output:    d5-d0=color, d6=intensity (half/normal), d7=(double/invalid) */
static uint32 make_lut_bgobj_ste(uint32 bx, uint32 sx)
{
  int c;

  int bf = (bx & 0x3F);
  int bp = (bx & 0x40);
  int b  = (bx & 0x0F);
  int bi = (bx & 0x80) >> 1;

  int sf = (sx & 0x3F);
  int sp = (sx & 0x40);
  int s  = (sx & 0x0F);
  int si = sp | bi;

  if(sp)
  {
    if(s)
    {
      if((sf & 0x3E) == 0x3E)
      {
        if(sf & 1)
        {
          c = (bf | 0x00);
        }
        else
        {
          c = (bx & 0x80) ? (bf | 0x80) : (bf | 0x40);
        }
      }
      else
      {
        if(sf == 0x0E || sf == 0x1E || sf == 0x2E)
        {
          c = (sf | 0x40);
        }
        else
        {
          c = (sf | si);
        }
      }
    }
    else
    {
      c = (bf | bi);
    }
  }
  else
  {
    if(bp)
    {
      if(b)
      {
        c = (bf | bi);
      }
      else
      {
        if(s)
        {
          if((sf & 0x3E) == 0x3E)
          {
            if(sf & 1)
            {
              c = (bf | 0x00);
            }
            else
            {
              c = (bx & 0x80) ? (bf | 0x80) : (bf | 0x40);
            }
          }
          else
          {
            if(sf == 0x0E || sf == 0x1E || sf == 0x2E)
            {
              c = (sf | 0x40);
            }
            else
            {
              c = (sf | si);
            }
          }
        }
        else
        {
          c = (bf | bi);
        }
      }
    }
    else
    {
      if(s)
      {
        if((sf & 0x3E) == 0x3E)
        {
          if(sf & 1)
          {
            c = (bf | 0x00);
          }
          else
          {
            c = (bx & 0x80) ? (bf | 0x80) : (bf | 0x40);
          }
        }
        else
        {
          if(sf == 0x0E || sf == 0x1E || sf == 0x2E)
          {
            c = (sf | 0x40);
          }
          else
          {
            c = (sf | si);
          }
        }
      }
      else
      {
        c = (bf | bi);
      }
    }
  }

  if((c & 0x0f) == 0x00) c &= 0xC0;

  return (c);
}

/* Input (bx):  d3-d0=color, d4=palette, d5=priority, d6=zero, d7=sprite pixel marker */
/* Input (sx):  d3-d0=color, d7-d4=zero */
/* Output:      d3-d0=color, d4=palette, d5=zero/priority, d6=zero, d7=sprite pixel marker */
static uint32 make_lut_bgobj_m4(uint32 bx, uint32 sx)
{
  int c;

  int bf = (bx & 0x3F);
  int bs = (bx & 0x80);
  int bp = (bx & 0x20);
  int b  = (bx & 0x0F);

  int s  = (sx & 0x0F);
  int sf = (s | 0x10); /* force palette bit */

  /* Transparent sprite pixel */
  if(s == 0) return bx;

  /* Previous sprite has higher priority */
  if(bs) return bx;

  /* note: priority bit is always 0 for Modes 0,1,2,3 */
  c = (bp ? (b ? bf : sf) : sf);

  return (c | 0x80);
}


/*--------------------------------------------------------------------------*/
/* Pixel layer merging function                                             */
/*--------------------------------------------------------------------------*/

/* AYTHER: merge que oculta los tiles marcados (id 0x104) revelando lo de atrás.
   Procesa de a una celda (tramo de 8 px alineado al frame) y decide por celda:
     · la celda tiene PRIMER PLANO (algún pixel de A/Window opaco) → es un elemento
       de adelante: se pela A/W (merge con A transparente, `table[(b<<8)|0]`) y se
       revela el PLANO B de atrás, uniforme y limpio (sin inversión en tiles con
       transparencias, p. ej. texto);
     · la celda es PLANO B puro (sin primer plano, un fondo) → se revela el
       BACKDROP (índice 0x40), porque no hay nada debajo de B.
   Así se oculta CUALQUIER tile (de adelante o de fondo) viendo lo que queda detrás.
   La decisión es por tramo de celda-fila (8 px), no por pixel → un fondo de Plano B
   puro (sin primer plano en ninguna fila) queda limpio en backdrop, y un elemento
   de primer plano revela B de forma uniforme. Función aparte (no inline) para no
   inflar el fast path de merge(), que sigue intacto. */
#ifdef AYTHER_EXTENSIONS
static void ayther_peel_merge(uint8 *srca, uint8 *srcb, uint8 *dst, uint8 *table, int width)
{
  int x = 0;
  while (width > 0)
  {
    int fx   = x + ayther_peel_vx;
    int seg  = 8 - (fx & 7);            /* px hasta el próximo borde de celda */
    int fcol = fx >> 3;
    int i;
    if (seg > width) seg = width;
    if (fcol < AYTHER_TILE_COLS && AYTHER_TILE_SUPPRESSED(ayther_peel_row, fcol))
    {
      /* #31: "hay primer plano en esta celda" es OPACIDAD, no "el byte no es
         cero". Un píxel de plano A transparente pero con el bit de prioridad
         puesto vale 0x40, y contaba como primer plano para las ocho columnas de
         la celda. Lo que decide es el índice de color. */
      int has_fg = 0;                   /* ¿algún pixel OPACO de A/W en la celda? */
      for (i = 0; i < seg; i++) if (srca[i] & 0x0F) { has_fg = 1; break; }
      for (i = 0; i < seg; i++)
        dst[i] = has_fg ? table[(srcb[i] << 8)]   /* pela A/W → revela Plano B */
                        : table[0];               /* fondo (B puro) → backdrop */
    }
    else
    {
      for (i = 0; i < seg; i++) dst[i] = table[(srcb[i] << 8) | srca[i]];   /* merge normal */
    }
    srca += seg; srcb += seg; dst += seg; x += seg; width -= seg;
  }
}
#endif

#ifdef AYTHER_EXTENSIONS
/* AYTHER (#41): atribución de una línea de fondo, con la MISMA regla que
   `make_lut_bg`: gana A si A tiene prioridad y es opaco; si no, gana B si B
   tiene prioridad y es opaco; si ninguno tiene prioridad, gana A si es opaco.
   Se replica la regla en vez de comparar el resultado contra las fuentes porque
   dos capas pueden dar el mismo byte, y ahí comparar responde cualquier cosa.

   A y Window comparten `linebuf[1]`, pero ocupan rangos de x DISJUNTOS en la
   línea (clip[0] para A, clip[1] para Window), así que distinguirlos es exacto
   y gratis: alcanza con mirar en qué rango cae la columna. */
static void ayther_attrib_bg(const uint8 *srca, const uint8 *srcb,
                             const uint8 *dst, int width, int shadow_mode)
{
  const int w_left  = clip[1].enable ? (clip[1].left  << 4) : 0;
  const int w_right = clip[1].enable ? (clip[1].right << 4) : 0;
  int x;

  for (x = 0; x < width; ++x)
  {
    const uint8 ax = srca[x], bx = srcb[x];
    const int a = ax & 0x0F, b = bx & 0x0F;
    const int ap = ax & 0x40, bp = bx & 0x40;
    const int a_wins = ap ? (a != 0) : (bp ? (b == 0) : (a != 0));
    const uint8 win = a_wins ? ax : bx;
    uint8 attr;

    if (!(win & 0x0F))
    {
      /* Ninguna capa puso color: se ve el backdrop. */
      ayther_attrib_line[x] = AYTHER_ATTRIB_LAYER_BACKDROP << AYTHER_ATTRIB_LAYER_SHIFT;
      continue;
    }

    if (a_wins)
      attr = (uint8)(((x >= w_left && x < w_right) ? AYTHER_ATTRIB_LAYER_WINDOW
                                                   : AYTHER_ATTRIB_LAYER_PLANE_A)
                     << AYTHER_ATTRIB_LAYER_SHIFT);
    else
      attr = (uint8)(AYTHER_ATTRIB_LAYER_PLANE_B << AYTHER_ATTRIB_LAYER_SHIFT);

    if (win & 0x40) attr |= AYTHER_ATTRIB_PRIORITY;
    attr |= (uint8)((((win >> 4) & 3) << AYTHER_ATTRIB_PALETTE_SHIFT) &
                    AYTHER_ATTRIB_PALETTE_MASK);
    /* En modo shadow/highlight el bit 7 del byte fusionado es "intensidad
       completa" (lo pone la LUT cuando alguna capa tenía prioridad); apagado
       significa sombra. El highlight lo introducen los operadores de sprite, en
       la etapa siguiente. */
    if (shadow_mode && !(dst[x] & 0x80))
      attr |= (uint8)(AYTHER_ATTRIB_SH_SHADOW << AYTHER_ATTRIB_SH_SHIFT);

    ayther_attrib_line[x] = attr;
  }
}
#endif

#ifdef AYTHER_EXTENSIONS
#if defined(__GNUC__) || defined(__clang__)
#define AYTHER_NOINLINE __attribute__((noinline))
#else
#define AYTHER_NOINLINE
#endif

/* Fuera de `merge` y sin inlinear a proposito: `merge` es INLINE y se expande en
   cada renderer, asi que meterle este cuerpo adentro engorda todos los sitios de
   llamada aunque la rama no se tome. */
static AYTHER_NOINLINE void ayther_merge_capture(uint8 *srca, uint8 *srcb,
                                                 uint8 *dst, uint8 *table,
                                                 int width)
{
  /* La atribución necesita las DOS fuentes, y el merge las consume in-place
     (dst == srcb en todos los renderers). Se copian antes; sólo la sombra se
     resuelve después, que es lo único que depende del resultado. */
  static uint8 snap_a[0x200], snap_b[0x200];
  const int shadow_mode = (reg[12] & 0x08) != 0;
  memcpy(snap_a, srca, width);
  memcpy(snap_b, srcb, width);
  if (ayther_peel_active) ayther_peel_merge(srca, srcb, dst, table, width);
  else { int i; for (i = 0; i < width; ++i) dst[i] = table[(snap_b[i] << 8) | snap_a[i]]; }
  ayther_attrib_bg(snap_a, snap_b, dst, width, shadow_mode);
}

/* #31/#37/#41: el merge de la capa de sprites (familia S/H) es quien decide si
   el sprite gana, asi que es el unico lugar donde la pregunta tiene respuesta
   exacta ahi. `srca` es la capa de sprites y `srcb` el fondo ya fusionado.

   Los operadores de shadow/highlight -- paleta 3, indices 14 y 15-- NO cuentan:
   no ponen color, modifican el brillo del pixel de abajo. Contarlos como
   sprite es el defecto 2 de #31, y por el diff entraban siempre. */
static AYTHER_NOINLINE void ayther_obj_capture(const uint8 *srca,
                                               const uint8 *srcb, int width)
{
  const int shadow_mode = (reg[12] & 0x08) != 0;
  uint8 *out = &ayther_sprite_px[0x20];
  int x;

  for (x = 0; x < width; ++x)
  {
    const uint8 sx = srca[x], bx = srcb[x];
    const int s = sx & 0x0F, sp = sx & 0x40;
    const int b = bx & 0x0F, bp = bx & 0x40;
    int is_sprite;

    (void)s; (void)sp; (void)b; (void)bp;
    if (shadow_mode && AYTHER_SPRITE_IS_OPERATOR(sx))
      is_sprite = 0;              /* operador S/H: brillo, no color */
    else
      is_sprite = AYTHER_SPRITE_WINS(sx, bx) ? 1 : 0;

    out[x] = (uint8)(is_sprite ? 1 : 0);
  }
  ayther_obj_px_exact = 1;
}
#endif

INLINE void merge(uint8 *srca, uint8 *srcb, uint8 *dst, uint8 *table, int width)
{
#ifdef AYTHER_EXTENSIONS
  /* Antes del merge: lo consume in-place (dst == srcb). */
  if (ayther_obj_pass)
    ayther_obj_capture(srca, srcb, width);
  if (ayther_attrib_capture)
  {
    ayther_merge_capture(srca, srcb, dst, table, width);
    return;
  }
  if (ayther_peel_active) { ayther_peel_merge(srca, srcb, dst, table, width); return; }
#endif
  do
  {
    *dst++ = table[(*srcb++ << 8) | (*srca++)];
  }
  while (--width);
}

/* Branch-free stock merge used by the compiled-idle Mode 5 renderer. */
INLINE void merge_fast(uint8 *srca, uint8 *srcb, uint8 *dst,
                       uint8 *table, int width)
{
  do
  {
    *dst++ = table[(*srcb++ << 8) | (*srca++)];
  }
  while (--width);
}


/*--------------------------------------------------------------------------*/
/* Pixel color lookup tables initialization                                 */
/*--------------------------------------------------------------------------*/

static void palette_init(void)
{
  int r, g, b, i;

  /************************************************/
  /* Each R,G,B color channel is 4-bit with a     */
  /* total of 15 different intensity levels.      */
  /*                                              */
  /* Color intensity depends on the mode:         */
  /*                                              */
  /*    normal   : xxx0     (0-14)                */
  /*    shadow   : 0xxx     (0-7)                 */
  /*    highlight: 1xxx - 1 (7-14)                */
  /*    mode4    : xxxx(*)  (0-15)                */
  /*    GG mode  : xxxx     (0-15)                */
  /*                                              */
  /* with x = original CRAM value (2, 3 or 4-bit) */
  /*  (*) 2-bit CRAM value is expanded to 4-bit   */
  /************************************************/

  /* Initialize Mode 5 pixel color look-up tables */
  for (i = 0; i < 0x200; i++)
  {
    /* CRAM 9-bit value (BBBGGGRRR) */
    r = (i >> 0) & 7;
    g = (i >> 3) & 7;
    b = (i >> 6) & 7;

    /* Convert to output pixel format */
    pixel_lut[0][i] = MAKE_PIXEL(r,g,b);
    pixel_lut[1][i] = MAKE_PIXEL(r<<1,g<<1,b<<1);
    pixel_lut[2][i] = MAKE_PIXEL(r+7,g+7,b+7);
  }

  /* Initialize Mode 4 pixel color look-up table */
  for (i = 0; i < 0x40; i++)
  {
    /* CRAM 6-bit value (000BBGGRR) */
    r = (i >> 0) & 3;
    g = (i >> 2) & 3;
    b = (i >> 4) & 3;

    /* Expand to full range & convert to output pixel format */
    pixel_lut_m4[i] = MAKE_PIXEL((r << 2) | r, (g << 2) | g, (b << 2) | b);
  }
}


/*--------------------------------------------------------------------------*/
/* Color palette update functions                                           */
/*--------------------------------------------------------------------------*/

void color_update_m4(int index, unsigned int data)
{
  switch (system_hw)
  {
    case SYSTEM_GG:
    {
      /* CRAM value (BBBBGGGGRRRR) */
      int r = (data >> 0) & 0x0F;
      int g = (data >> 4) & 0x0F;
      int b = (data >> 8) & 0x0F;

      /* Convert to output pixel */
      data = MAKE_PIXEL(r,g,b);
      break;
    }

    case SYSTEM_SG:
    case SYSTEM_SGII:
    case SYSTEM_SGII_RAM_EXT:
    {
      /* Fixed TMS99xx palette */
      if (index & 0x0F)
      {
        /* Colors 1-15 */
        data = tms_palette[index & 0x0F];
      }
      else
      {
        /* Backdrop color */
        data = tms_palette[reg[7] & 0x0F];
      }
      break;
    }

    default:
    {
      /* Test M4 bit */
      if (!(reg[0] & 0x04))
      {
        if (system_hw & SYSTEM_MD)
        {
          /* Invalid Mode (black screen) */
          data = 0x00;
        }
        else if (system_hw != SYSTEM_GGMS)
        {
          /* Fixed CRAM palette */
          if (index & 0x0F)
          {
            /* Colors 1-15 */
            data = tms_crom[index & 0x0F];
          }
          else
          {
            /* Backdrop color */
            data = tms_crom[reg[7] & 0x0F];
          }
        }
      }

      /* Mode 4 palette */
      data = pixel_lut_m4[data & 0x3F];
      break;
    }
  }


  /* Input pixel: x0xiiiii (normal) or 01000000 (backdrop) */
  if (reg[0] & 0x04)
  {
    /* Mode 4 */
    pixel[0x00 | index] = data;
    pixel[0x20 | index] = data;
    pixel[0x80 | index] = data;
    pixel[0xA0 | index] = data;
  }
  else
  {
    /* TMS99xx modes (palette bit forced to 1 because Game Gear uses CRAM palette #1) */
    if ((index == 0x40) || (index == (0x10 | (reg[7] & 0x0F))))
    {
      /* Update backdrop color */
      pixel[0x40] = data;

      /* Update transparent color */
      pixel[0x10] = data;
      pixel[0x30] = data;
      pixel[0x90] = data;
      pixel[0xB0] = data;
    }

    if (index & 0x0F)
    {
      /* update non-transparent colors */
      pixel[0x00 | index] = data;
      pixel[0x20 | index] = data;
      pixel[0x80 | index] = data;
      pixel[0xA0 | index] = data;
    }
  }
}

void color_update_m5(int index, unsigned int data)
{
  /* Palette Mode */
  if (!(reg[0] & 0x04))
  {
    /* Color value is limited to 00X00X00X */
    data &= 0x49;
  }

  if(reg[12] & 0x08)
  {
    /* Mode 5 (Shadow/Normal/Highlight) */
    pixel[0x00 | index] = pixel_lut[0][data];
    pixel[0x40 | index] = pixel_lut[1][data];
    pixel[0x80 | index] = pixel_lut[2][data];
  }
  else
  {
    /* Mode 5 (Normal) */
    data = pixel_lut[1][data];

    /* Input pixel: xxiiiiii */
    pixel[0x00 | index] = data;
    pixel[0x40 | index] = data;
    pixel[0x80 | index] = data;
  }
}


/*--------------------------------------------------------------------------*/
/* Background layers rendering functions                                    */
/*--------------------------------------------------------------------------*/

/* Graphics I */
void render_bg_m0(int line)
{
  uint8 color, name, pattern;

  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line & 0xF8) << 2)];
  uint8 *ct = &vram[((reg[3] <<  6) & 0x3FC0)];
  uint8 *pg = &vram[((reg[4] << 11) & 0x3800) + (line & 7)];

  /* 32 x 8 pixels */
  int width = 32;

  do
  {
    name = *nt++;
    color = ct[name >> 3];
    pattern = pg[name << 3];

    *lb++ = 0x10 | ((color >> (((pattern >> 7) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 6) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 5) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 4) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 3) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 2) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 1) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 0) & 1) << 2)) & 0x0F);
  }
  while (--width);
}

/* Text */
void render_bg_m1(int line)
{
  uint8 pattern;
  uint8 color = reg[7];

  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line >> 3) * 40)];
  uint8 *pg = &vram[((reg[4] << 11) & 0x3800) + (line & 7)];

  /* 40 x 6 pixels */
  int width = 40;

  /* Left border (8 pixels) */
  memset (lb, 0x40, 8);
  lb += 8;

  do
  {
    pattern = pg[*nt++ << 3];

    *lb++ = 0x10 | ((color >> (((pattern >> 7) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 6) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 5) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 4) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 3) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 2) & 1) << 2)) & 0x0F);
  }
  while (--width);

  /* Right borders (8 pixels) */
  memset(lb, 0x40, 8);
}

/* Text + extended PG */
void render_bg_m1x(int line)
{
  uint8 pattern;
  uint8 *pg;

  uint8 color = reg[7];

  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line >> 3) * 40)];

  uint16 pg_mask = ~0x3800 ^ (reg[4] << 11);

  /* 40 x 6 pixels */
  int width = 40;

  /* Unused bits used as a mask on TMS99xx & 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    pg_mask |= 0x1800;
  }

  pg = &vram[((0x2000 + ((line & 0xC0) << 5)) & pg_mask) + (line & 7)];

  /* Left border (8 pixels) */
  memset (lb, 0x40, 8);
  lb += 8;

  do
  {
    pattern = pg[*nt++ << 3];

    *lb++ = 0x10 | ((color >> (((pattern >> 7) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 6) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 5) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 4) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 3) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 2) & 1) << 2)) & 0x0F);
  }
  while (--width);

  /* Right borders (8 pixels) */
  memset(lb, 0x40, 8);
}

/* Graphics II */
void render_bg_m2(int line)
{
  uint8 color, pattern;
  uint16 name;
  uint8 *ct, *pg;

  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line & 0xF8) << 2)];

  uint16 ct_mask = ~0x3FC0 ^ (reg[3] << 6);
  uint16 pg_mask = ~0x3800 ^ (reg[4] << 11);

  /* 32 x 8 pixels */
  int width = 32;

  /* Unused bits used as a mask on TMS99xx & 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    ct_mask |= 0x1FC0;
    pg_mask |= 0x1800;
  }

  ct = &vram[((0x2000 + ((line & 0xC0) << 5)) & ct_mask) + (line & 7)];
  pg = &vram[((0x2000 + ((line & 0xC0) << 5)) & pg_mask) + (line & 7)];

  do
  {
    name = *nt++ << 3 ;
    color = ct[name & ct_mask];
    pattern = pg[name];

    *lb++ = 0x10 | ((color >> (((pattern >> 7) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 6) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 5) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 4) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 3) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 2) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 1) & 1) << 2)) & 0x0F);
    *lb++ = 0x10 | ((color >> (((pattern >> 0) & 1) << 2)) & 0x0F);
  }
  while (--width);
}

/* Multicolor */
void render_bg_m3(int line)
{
  uint8 color;
  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line & 0xF8) << 2)];
  uint8 *pg = &vram[((reg[4] << 11) & 0x3800) + ((line >> 2) & 7)];

  /* 32 x 8 pixels */
  int width = 32;

  do
  {
    color = pg[*nt++ << 3];

    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
  }
  while (--width);
}

/* Multicolor + extended PG */
void render_bg_m3x(int line)
{
  uint8 color;
  uint8 *pg;

  uint8 *lb = &linebuf[0][0x20];
  uint8 *nt = &vram[((reg[2] << 10) & 0x3C00) + ((line & 0xF8) << 2)];

  uint16 pg_mask = ~0x3800 ^ (reg[4] << 11);

  /* 32 x 8 pixels */
  int width = 32;

  /* Unused bits used as a mask on TMS99xx & 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    pg_mask |= 0x1800;
  }

  pg = &vram[((0x2000 + ((line & 0xC0) << 5)) & pg_mask) + ((line >> 2) & 7)];

  do
  {
    color = pg[*nt++ << 3];

    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
  }
  while (--width);
}

/* Invalid (2+3/1+2+3) */
void render_bg_inv(int line)
{
  uint8 color = reg[7];

  uint8 *lb = &linebuf[0][0x20];

  /* 40 x 6 pixels */
  int width = 40;

  /* Left border (8 pixels) */
  memset (lb, 0x40, 8);
  lb += 8;

  do
  {
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 4) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
    *lb++ = 0x10 | ((color >> 0) & 0x0F);
  }
  while (--width);

  /* Right borders (8 pixels) */
  memset(lb, 0x40, 8);
}

/* Mode 4 */
void render_bg_m4(int line)
{
  int column;
  uint16 *nt;
  uint32 attr, atex;

  /* Horizontal scrolling */
  int index = ((reg[0] & 0x40) && (line < 0x10)) ? 0x100 : reg[0x08];
  int shift = index & 7;

  /* Background line buffer */
  uint32 *dst = (uint32 *)&linebuf[0][0x20 + shift];

  /* Vertical scrolling */
  int v_line = line + vscroll;

  /* Pattern name table mask */
  uint16 nt_mask = ~0x3C00 ^ (reg[2] << 10);

  /* Unused bits used as a mask on TMS99xx & 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    nt_mask |= 0x400;
  }

  /* Check extended height modes (Master System II & Game Gear VDP only) */
  if (bitmap.viewport.h > 192)
  {
    /* Vertical scroll mask */
    v_line = v_line % 256;

    /* Pattern name Table */
    nt = (uint16 *)&vram[(0x3700 & nt_mask) + ((v_line >> 3) << 6)];
  }
  else
  {
    /* Vertical scroll mask */
    v_line = v_line % 224;

    /* Pattern name Table */
    nt = (uint16 *)&vram[(0x3800 + ((v_line >> 3) << 6)) & nt_mask];
  }

  /* Pattern row index */
  v_line = (v_line & 7) << 3;

  /* Tile column index */
  index = (0x100 - index) >> 3;

  /* Clip left-most column if required */
  if (shift)
  {
    memset(&linebuf[0][0x20], 0, shift);
    index++;
  }

  /* Draw tiles (32 x 8 pixels) */
  for(column = 0; column < 32; column++, index++)
  {
    /* Stop vertical scrolling for rightmost eight tiles */
    if((column == 24) && (reg[0] & 0x80))
    {
      /* Clear Pattern name table start address */
      if (bitmap.viewport.h > 192)
      {
        nt = (uint16 *)&vram[(0x3700 & nt_mask) + ((line >> 3) << 6)];
      }
      else
      {
        nt = (uint16 *)&vram[(0x3800 + ((line >> 3) << 6)) & nt_mask];
      }

      /* Clear Pattern row index */
      v_line = (line & 7) << 3;
    }

    /* Read name table attribute word */
    attr = nt[index & 0x1F];
#ifndef LSB_FIRST
    attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif

    /* Expand priority and palette bits */
    atex = atex_table[(attr >> 11) & 3];

    /* On 315-5124 VDP only, Color Table Base Address (resp. Pattern Generator Table Base Address) register bits 7:0 (resp. bits 2:0) */
    /* are used as a mask on tile index upper bits when fetching bitplanes 0&1 (resp. bitplanes 2&3), which correspond to tile pixels */
    /* data bits 0:1 (resp. bits 2:3) */
    if (system_hw <= SYSTEM_SMS)
    {
      /* Cached pattern data lines (4 bytes = 4 pixels at once) for pixels data bits 0:1 and 2:3 */
      uint32 *src01 = (uint32 *)&bg_pattern_cache[((attr & (0x601 | (reg[3] << 1))) << 6) | v_line];
      uint32 *src23 = (uint32 *)&bg_pattern_cache[((attr & (0x63F | ((reg[4] & 0x07) << 6))) << 6) | v_line];

      /* Copy left & right half, retrieving each pixel data bits from appropriate source and adding the attribute bits in */
#ifdef ALIGN_LONG
      WRITE_LONG(dst, (src01[0] & 0x03030303) | (src23[0] & 0x0C0C0C0C) | atex);
      dst++;
      WRITE_LONG(dst, (src01[1] & 0x03030303) | (src23[1] & 0x0C0C0C0C) | atex);
      dst++;
#else
      *dst++ = (src01[0] & 0x03030303) | (src23[0] & 0x0C0C0C0C) | atex;
      *dst++ = (src01[1] & 0x03030303) | (src23[1] & 0x0C0C0C0C) | atex;
#endif
    }
    else
    {
      /* Cached pattern data line (4 bytes = 4 pixels at once) */
      uint32 *src = (uint32 *)&bg_pattern_cache[((attr & 0x7FF) << 6) | v_line];

      /* Copy left & right half, adding the attribute bits in */
#ifdef ALIGN_LONG
      WRITE_LONG(dst, src[0] | atex);
      dst++;
      WRITE_LONG(dst, src[1] | atex);
      dst++;
#else
      *dst++ = src[0] | atex;
      *dst++ = src[1] | atex;
#endif
    }
  }
}

/* AYTHER fork delta: dibuja una columna de 2 celdas igual que DRAW_COLUMN, pero
   saltea (deja transparente, color 0) las celdas cuyo patrón+paleta esté en la
   máscara de supresión por-plano `ps` (id 0x105). `ps == NULL` → camino idéntico a
   DRAW_COLUMN (sin costo para los demás juegos). Respeta el orden de dibujo
   (LSB/MSB) según el endianness, igual que DRAW_COLUMN. Devuelve el `dst` avanzado. */
#ifdef AYTHER_EXTENSIONS
#ifdef ALIGN_LONG
#define AYTHER_PUT0()  do { WRITE_LONG(dst, 0); dst++; WRITE_LONG(dst, 0); dst++; } while (0)
#define AYTHER_PUTS()  do { WRITE_LONG(dst, src[0] | atex); dst++; WRITE_LONG(dst, src[1] | atex); dst++; } while (0)
#else
#define AYTHER_PUT0()  do { *dst++ = 0; *dst++ = 0; } while (0)
#define AYTHER_PUTS()  do { *dst++ = (src[0] | atex); *dst++ = (src[1] | atex); } while (0)
#endif
INLINE uint32 *ayther_draw_col(uint32 *dst, uint32 atbuf, uint32 v_line, const uint8 *ps)
{
  uint32 atex, *src;
#ifdef LSB_FIRST
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
#else
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE(atbuf, v_line) AYTHER_PUTS(); }
#endif
  return dst;
}
/* Atajo: usa el fast path (DRAW_COLUMN) cuando no hay supresión en este plano. */
#define DRAW_COLUMN_AE(ATTR, LINE, PS) \
  do { if (PS) dst = ayther_draw_col(dst, (ATTR), (LINE), (PS)); else { DRAW_COLUMN((ATTR), (LINE)) } } while (0)

/* AYTHER (#28): el mismo dibujo de columna para interlace mode 2. Los
   renderers de im2 usan su propio par de getters -el patrón se indexa distinto
   porque cada tile guarda dos campos-, así que no alcanzaba con reutilizar
   `ayther_draw_col`: sin esta variante, la supresión de tiles de plano
   simplemente no existía en im2. La clave de supresión es la misma (patrón +
   paleta), para que el frontend no tenga que saber en qué modo está el VDP. */
INLINE uint32 *ayther_draw_col_im2(uint32 *dst, uint32 atbuf, uint32 v_line,
                                   const uint8 *ps)
{
  uint32 atex, *src;
#ifdef LSB_FIRST
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
#else
  if (AYTHER_PTSUP(ps, (atbuf >> 16) & 0xFFFFu)){ AYTHER_PUT0(); }
  else { GET_MSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
  if (AYTHER_PTSUP(ps, atbuf & 0xFFFFu))        { AYTHER_PUT0(); }
  else { GET_LSB_TILE_IM2(atbuf, v_line) AYTHER_PUTS(); }
#endif
  return dst;
}
#define DRAW_COLUMN_IM2_AE(ATTR, LINE, PS) \
  do { if (PS) dst = ayther_draw_col_im2(dst, (ATTR), (LINE), (PS)); \
       else { DRAW_COLUMN_IM2((ATTR), (LINE)) } } while (0)
#else
#define DRAW_COLUMN_AE(ATTR, LINE, PS) DRAW_COLUMN((ATTR), (LINE))
#define DRAW_COLUMN_IM2_AE(ATTR, LINE, PS) DRAW_COLUMN_IM2((ATTR), (LINE))
#endif

#if defined(AYTHER_EXTENSIONS) && defined(__GNUC__)
#define AYTHER_HOT_INLINE static inline __attribute__((always_inline))
#else
#define AYTHER_HOT_INLINE INLINE
#endif


/* Mode 5 */
#ifndef ALT_RENDERER

AYTHER_HOT_INLINE void render_bg_m5_impl(int line, int ayther_observed)
{
  int column;
  uint32 atex, atbuf, *src, *dst;

  /* Common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = *(uint32 *)&vsram[0];
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;

#ifdef AYTHER_EXTENSIONS
  /* #42: el estado de esta línea, con el scroll ya resuelto. Sólo en el clon
     observado y sólo bajo suscripción: sin subscribers no corre nada. */
  if (ayther_observed && AYTHER_LINE_STATE_ACTIVE)
    ayther_line_capture((uint32)line, xscroll, yscroll);
#endif

  /* AYTHER: máscaras de supresión por plano (id 0x105); NULL = fast path. */
  const uint8 *psupA = ayther_observed ? AYTHER_PSUP(0) : (const uint8 *)0;
  const uint8 *psupB = ayther_observed ? AYTHER_PSUP(1) : (const uint8 *)0;
  const uint8 *psupW = ayther_observed ? AYTHER_PSUP(2) : (const uint8 *)0;
#ifdef AYTHER_EXTENSIONS
  const int hide_a = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_A);
  const int hide_b = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_B);
  const int hide_w = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_W);
#else
  const int hide_a = 0;
  const int hide_b = 0;
  const int hide_w = 0;
#endif

  /* Window & Plane A */
  int a = (reg[18] & 0x1F) << 3;
  int w = (reg[18] >> 7) & 1;

  /* Plane B width */
  int start = 0;
  int end = bitmap.viewport.w >> 4;

  /* Plane B scroll */
#ifdef LSB_FIRST
  uint32 shift  = (xscroll >> 16) & 0x0F;
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
  uint32 v_line = (line + (yscroll >> 16)) & pf_row_mask;
#else
  uint32 shift  = (xscroll & 0x0F);
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
  uint32 v_line = (line + yscroll) & pf_row_mask;
#endif

  /* Plane B name table */
  uint32 *nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

  /* Pattern row index */
  v_line = (v_line & 7) << 3;

#ifdef AYTHER_EXTENSIONS
  /* #42.C: la fila y el desplazamiento fino son por LINEA y por PLANO -- el
     renderer los calcula una vez-, asi que se guardan aca y no repetidos en
     cada una de las 21 columnas. */
  if (ayther_observed && AYTHER_LINE_CELLS_ACTIVE)
  {
    ayther_cells_begin_line((uint32)line);
    ayther_cells_open((uint32)line, 1, v_line, shift);
  }
#endif
  if(shift)
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x10 + shift];

    atbuf = nt[(index - 1) & pf_col_mask];
    DRAW_COLUMN_AE(atbuf, v_line, psupB);
      AYTHER_CELL_RECORD(atbuf);
  }
  else
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x20];
  }

  for(column = 0; column < end; column++, index++)
  {
    atbuf = nt[index & pf_col_mask];
    DRAW_COLUMN_AE(atbuf, v_line, psupB);
      AYTHER_CELL_RECORD(atbuf);
  }
#ifdef AYTHER_EXTENSIONS
  if (ayther_observed) ayther_cells_close();
#endif

  /* AYTHER: ocultar Plano A o Window → limpiar su buffer compartido (linebuf[1])
     antes de dibujar; lo no dibujado queda transparente y se ve Plano B. */
  if (hide_a || hide_w)
    memset(&linebuf[1][0x20], 0, bitmap.viewport.w);

  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Plane A */
  if (a)
  {
    if (!hide_a)   /* AYTHER: gate Plano A (el rango de Window se fija igual abajo) */
    {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A scroll */
#ifdef LSB_FIRST
    shift   = (xscroll & 0x0F);
    index   = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
    v_line  = (line + yscroll) & pf_row_mask;
#else
    shift   = (xscroll >> 16) & 0x0F;
    index   = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
    v_line  = (line + (yscroll >> 16)) & pf_row_mask;
#endif

    /* Plane A name table */
    nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;
#ifdef AYTHER_EXTENSIONS
    if (ayther_observed && AYTHER_LINE_CELLS_ACTIVE)
      ayther_cells_open((uint32)line, 0, v_line, shift);
#endif

    if(shift)
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x10 + shift + (start << 4)];

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index - 1) & pf_col_mask];
      }

      DRAW_COLUMN_AE(atbuf, v_line, psupA);
      AYTHER_CELL_RECORD(atbuf);
    }
    else
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];
    }

    for(column = start; column < end; column++, index++)
    {
      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_AE(atbuf, v_line, psupA);
      AYTHER_CELL_RECORD(atbuf);
    }
#ifdef AYTHER_EXTENSIONS
    if (ayther_observed) ayther_cells_close();
#endif
    }   /* AYTHER: fin gate Plano A */

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }

  /* Window */
  if (w && !hide_w)   /* AYTHER: gate Window */
  {
    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;
#ifdef AYTHER_EXTENSIONS
    if (ayther_observed && AYTHER_LINE_CELLS_ACTIVE)
      ayther_cells_open((uint32)line, 2, v_line, 0);
#endif

    /* Plane A line buffer */
    dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_AE(atbuf, v_line, psupW);
      AYTHER_CELL_RECORD(atbuf);
    }
  }

  /* AYTHER: ocultar Plano B → limpiar su buffer antes del merge. */
  if (hide_b)
    memset(&linebuf[0][0x20], 0, bitmap.viewport.w);

  /* Merge background layers */
  if (ayther_observed)
    merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20],
          lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
  else
    merge_fast(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20],
               lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_NOINLINE void render_bg_m5_fast_path(int line)
{
  render_bg_m5_impl(line, 0);
}

static AYTHER_NOINLINE void render_bg_m5_observed_path(int line)
{
  render_bg_m5_impl(line, 1);
}
#endif

void render_bg_m5(int line)
{
#ifdef AYTHER_EXTENSIONS
  /* #41: OBSERVED, no CONTROLS: ver la nota en AYTHER_OBSERVED_ACTIVE. */
  if (AYTHER_OBSERVED_ACTIVE)
    render_bg_m5_observed_path(line);
  else
    render_bg_m5_fast_path(line);
#else
  render_bg_m5_impl(line, 0);
#endif
}

AYTHER_HOT_INLINE void render_bg_m5_vs_impl(int line, int ayther_observed)
{
  int column;
  uint32 atex, atbuf, *src, *dst;
  uint32 v_line, *nt;

  /* AYTHER: máscaras de supresión por plano (id 0x105); NULL = fast path. */
  const uint8 *psupA = ayther_observed ? AYTHER_PSUP(0) : (const uint8 *)0;
  const uint8 *psupB = ayther_observed ? AYTHER_PSUP(1) : (const uint8 *)0;
  const uint8 *psupW = ayther_observed ? AYTHER_PSUP(2) : (const uint8 *)0;
#ifdef AYTHER_EXTENSIONS
  const int hide_a = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_A);
  const int hide_b = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_B);
  const int hide_w = ayther_observed &&
    !(ayther_layer_mask & AYTHER_LAYER_W);
#else
  const int hide_a = 0;
  const int hide_b = 0;
  const int hide_w = 0;
#endif

  /* Common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Window & Plane A */
  int a = (reg[18] & 0x1F) << 3;
  int w = (reg[18] >> 7) & 1;

  /* Plane B width */
  int start = 0;
  int end = bitmap.viewport.w >> 4;

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  uint32 shift  = (xscroll >> 16) & 0x0F;
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  uint32 shift  = (xscroll & 0x0F);
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  if(shift)
  {
    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x10 + shift];

    atbuf = nt[(index - 1) & pf_col_mask];
    DRAW_COLUMN_AE(atbuf, v_line, psupB);
      AYTHER_CELL_RECORD(atbuf);
  }
  else
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x20];
  }

  for(column = 0; column < end; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
#endif

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[index & pf_col_mask];
    DRAW_COLUMN_AE(atbuf, v_line, psupB);
      AYTHER_CELL_RECORD(atbuf);
  }

  /* AYTHER: ocultar Plano A o Window → limpiar su buffer compartido (linebuf[1]). */
  if (hide_a || hide_w)
    memset(&linebuf[1][0x20], 0, bitmap.viewport.w);

  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Plane A */
  if (a)
  {
    if (!hide_a)   /* AYTHER: gate Plano A */
    {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    if(shift)
    {
      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x10 + shift + (start << 4)];

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index - 1) & pf_col_mask];
      }

      DRAW_COLUMN_AE(atbuf, v_line, psupA);
      AYTHER_CELL_RECORD(atbuf);
    }
    else
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#endif

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_AE(atbuf, v_line, psupA);
      AYTHER_CELL_RECORD(atbuf);
    }
    }   /* AYTHER: fin gate Plano A */

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }

  /* Window */
  if (w && !hide_w)   /* AYTHER: gate Window */
  {
    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;
#ifdef AYTHER_EXTENSIONS
    if (ayther_observed && AYTHER_LINE_CELLS_ACTIVE)
      ayther_cells_open((uint32)line, 2, v_line, 0);
#endif

    /* Plane A line buffer */
    dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_AE(atbuf, v_line, psupW);
      AYTHER_CELL_RECORD(atbuf);
    }
  }

  /* AYTHER: ocultar Plano B → limpiar su buffer antes del merge. */
  if (hide_b)
    memset(&linebuf[0][0x20], 0, bitmap.viewport.w);

  /* Merge background layers */
  if (ayther_observed)
    merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20],
          lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
  else
    merge_fast(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20],
               lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_NOINLINE void render_bg_m5_vs_fast_path(int line)
{
  render_bg_m5_vs_impl(line, 0);
}

static AYTHER_NOINLINE void render_bg_m5_vs_observed_path(int line)
{
  render_bg_m5_vs_impl(line, 1);
}
#endif

void render_bg_m5_vs(int line)
{
#ifdef AYTHER_EXTENSIONS
  /* #41: OBSERVED, no CONTROLS: ver la nota en AYTHER_OBSERVED_ACTIVE. */
  if (AYTHER_OBSERVED_ACTIVE)
    render_bg_m5_vs_observed_path(line);
  else
    render_bg_m5_vs_fast_path(line);
#else
  render_bg_m5_vs_impl(line, 0);
#endif
}

/* Enhanced function that allows each cell to be vscrolled individually, instead of being limited to 2-cell */
void render_bg_m5_vs_enhanced(int line)
{
  int column;
  uint32 atex, atbuf, *src, *dst;
  uint32 v_line, next_v_line, *nt;

  /* AYTHER (#28): este renderer no tenia los gates de capa ni de supresion de
     tiles, asi que con la opcion de vscroll mejorado activada escribir la
     mascara 0x102 o 0x105 no hacia nada. El frontend creia haber ocultado un
     plano y no pasaba nada: sin error, sin motivo de fallback, sin sintoma
     salvo el resultado equivocado. Los macros ya incluyen el chequeo de
     suscripcion y se pliegan a 0 sin AYTHER_EXTENSIONS. */
  const uint8 *psupA = AYTHER_PSUP(0);
  const uint8 *psupB = AYTHER_PSUP(1);
  const uint8 *psupW = AYTHER_PSUP(2);
  const int hide_a = AYTHER_HIDE_A;
  const int hide_b = AYTHER_HIDE_B;
  const int hide_w = AYTHER_HIDE_W;
#ifdef AYTHER_EXTENSIONS
  /* La supresion de tiles de plano SI queda fuera de alcance aca: este
     renderer dibuja cada media columna con su propio v_line -para eso
     existe- y no pasa por el camino de columna donde vive el filtro. En vez
     de aplicarlo a medias se declara, para que el frontend pueda apagar la
     sustitucion en lugar de confiar en un resultado incompleto. */
  if ((psupA || psupB || psupW))
    ayther_raster_dirty |= AYTHER_RASTER_REASON_UNSUPPORTED_CONTROLS;
#endif

  /* Vertical scroll offset */
  int v_offset = 0;

  /* Common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Window & Plane A */
  int a = (reg[18] & 0x1F) << 3;
  int w = (reg[18] >> 7) & 1;

  /* Plane B width */
  int start = 0;
  int end = bitmap.viewport.w >> 4;

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  uint32 shift  = (xscroll >> 16) & 0x0F;
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  uint32 shift  = (xscroll & 0x0F);
  uint32 index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  if(shift)
  {
    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x10 + shift];

    atbuf = nt[(index - 1) & pf_col_mask];
    DRAW_COLUMN(atbuf, v_line)
  }
  else
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x20];
  }

  for(column = 0; column < end; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
    next_v_line = (line + (vs[column + 1] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
    next_v_line = (line + vs[column + 1]) & pf_row_mask;
#endif

    if (column != end - 1)
    {
      /* The offset of the intermediary cell is an average of the offsets of the current 2-cell and the next 2-cell. */
      /* For the last column, the previously calculated offset is used */
      v_offset = ((int)next_v_line - (int)v_line) / 2;
      v_offset = (abs(v_offset) >= config.enhanced_vscroll_limit) ? 0 : v_offset;
    }

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[index & pf_col_mask];
#ifdef LSB_FIRST
    GET_LSB_TILE(atbuf, v_line)
#else
    GET_MSB_TILE(atbuf, v_line)
#endif

#ifdef ALIGN_LONG
    WRITE_LONG(dst, src[0] | atex);
    dst++;
    WRITE_LONG(dst, src[1] | atex);
    dst++;
#else
    *dst++ = (src[0] | atex);
    *dst++ = (src[1] | atex);
#endif

#ifdef LSB_FIRST
    v_line = (line + v_offset + (vs[column] >> 16)) & pf_row_mask;
#else
    v_line = (line + v_offset + vs[column]) & pf_row_mask;
#endif

    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];
    v_line = (v_line & 7) << 3;
    atbuf = nt[index & pf_col_mask];

#ifdef LSB_FIRST
    GET_MSB_TILE(atbuf, v_line)
#else
    GET_LSB_TILE(atbuf, v_line)
#endif
#ifdef ALIGN_LONG
    WRITE_LONG(dst, src[0] | atex);
    dst++;
    WRITE_LONG(dst, src[1] | atex);
    dst++;
#else
    *dst++ = (src[0] | atex);
    *dst++ = (src[1] | atex);
#endif
  }

  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* AYTHER (#28): ocultar Plano A o Window -> limpiar su buffer compartido
     (linebuf[1]) antes de dibujar. */
  if (hide_a || hide_w)
    memset(&linebuf[1][0x20], 0, bitmap.viewport.w);

  /* Plane A */
  if (a)
  {
    if (!hide_a)   /* AYTHER: gate Plano A */
    {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    if(shift)
    {
      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x10 + shift + (start << 4)];

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index - 1) & pf_col_mask];
      }

      DRAW_COLUMN(atbuf, v_line)
    }
    else
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
      next_v_line = (line + vs[column + 1]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
      next_v_line = (line + (vs[column + 1] >> 16)) & pf_row_mask;
#endif

      if (column != end - 1)
      {
        v_offset = ((int)next_v_line - (int)v_line) / 2;
        v_offset = (abs(v_offset) >= config.enhanced_vscroll_limit) ? 0 : v_offset;
      }

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      atbuf = nt[index & pf_col_mask];
#ifdef LSB_FIRST
      GET_LSB_TILE(atbuf, v_line)
#else
      GET_MSB_TILE(atbuf, v_line)
#endif
#ifdef ALIGN_LONG
      WRITE_LONG(dst, src[0] | atex);
      dst++;
      WRITE_LONG(dst, src[1] | atex);
      dst++;
#else
      *dst++ = (src[0] | atex);
      *dst++ = (src[1] | atex);
#endif

#ifdef LSB_FIRST
      v_line = (line + v_offset + vs[column]) & pf_row_mask;
#else
      v_line = (line + v_offset + (vs[column] >> 16)) & pf_row_mask;
#endif

      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];
      v_line = (v_line & 7) << 3;
      atbuf = nt[index & pf_col_mask];

#ifdef LSB_FIRST
      GET_MSB_TILE(atbuf, v_line)
#else
      GET_LSB_TILE(atbuf, v_line)
#endif
#ifdef ALIGN_LONG
      WRITE_LONG(dst, src[0] | atex);
      dst++;
      WRITE_LONG(dst, src[1] | atex);
      dst++;
#else
      *dst++ = (src[0] | atex);
      *dst++ = (src[1] | atex);
#endif
    }

    }   /* AYTHER: fin gate Plano A */

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }

  /* Window */
  if (w && !hide_w)   /* AYTHER: gate Window */
  {
    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;

    /* Plane A line buffer */
    dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN(atbuf, v_line)
    }
  }

  /* Merge background layers */
  /* AYTHER: ocultar Plano B -> limpiar su buffer antes del merge. */
  if (hide_b)
    memset(&linebuf[0][0x20], 0, bitmap.viewport.w);

  merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
}

void render_bg_m5_im2(int line)
{
  int column, start, end, a, w;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;

  /* Common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = *(uint32 *)&vsram[0];
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = (playfield_row_mask << 1) | 1;
  uint32 pf_shift     = playfield_shift;

  /* AYTHER (#28): interlace mode 2 no tenia los gates de capa ni de supresion
     de tiles de plano. Escribir la mascara 0x102 o 0x105 con el VDP en este
     modo -Sonic 2 en dos jugadores, Combat Cars- no hacia absolutamente nada:
     ni efecto, ni error, ni motivo de fallback. El frontend creia haber
     ocultado un plano. Los macros ya incluyen el chequeo de suscripcion y se
     pliegan a 0 sin AYTHER_EXTENSIONS. */
  const uint8 *psupA = AYTHER_PSUP(0);
  const uint8 *psupB = AYTHER_PSUP(1);
  const uint8 *psupW = AYTHER_PSUP(2);
  const int hide_a = AYTHER_HIDE_A;
  const int hide_b = AYTHER_HIDE_B;
  const int hide_w = AYTHER_HIDE_W;

  /* Adjust line offset */
  line = (line << 1) + odd_frame;

  /* Plane B width */
  start = 0;
  end = bitmap.viewport.w >> 4;

  /* Plane B scroll */
#ifdef LSB_FIRST
  shift  = (xscroll >> 16) & 0x0F;
  index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
  v_line = (line + (yscroll >> 16)) & pf_row_mask;
#else
  shift  = (xscroll & 0x0F);
  index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
  v_line = (line + yscroll) & pf_row_mask;
#endif

  /* Plane B name table */
  nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

  /* Pattern row index */
  v_line = (v_line & 15) << 3;

  if(shift)
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x10 + shift];

    atbuf = nt[(index - 1) & pf_col_mask];
    DRAW_COLUMN_IM2_AE(atbuf, v_line, psupB);
  }
  else
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x20];
  }

  for(column = 0; column < end; column++, index++)
  {
    atbuf = nt[index & pf_col_mask];
    DRAW_COLUMN_IM2_AE(atbuf, v_line, psupB);
  }

  /* Window & Plane A */
  a = (reg[18] & 0x1F) << 4;
  w = (reg[18] >> 7) & 1;

  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* AYTHER: ocultar Plano A o Window -> limpiar su buffer compartido
     (linebuf[1]) antes de dibujar; lo no dibujado queda transparente y
     se ve el Plano B, que es lo que "ocultar una capa" significa. */
  if (hide_a || hide_w)
    memset(&linebuf[1][0x20], 0, bitmap.viewport.w);

  /* Plane A */
  if (a)
  {
    if (!hide_a)   /* AYTHER: gate Plano A */
    {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A scroll */
#ifdef LSB_FIRST
    shift   = (xscroll & 0x0F);
    index   = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
    v_line  = (line + yscroll) & pf_row_mask;
#else
    shift   = (xscroll >> 16) & 0x0F;
    index   = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
    v_line  = (line + (yscroll >> 16)) & pf_row_mask;
#endif

    /* Plane A name table */
    nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    if(shift)
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x10 + shift + (start << 4)];

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index - 1) & pf_col_mask];
      }

      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupA);
    }
    else
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];
    }

    for(column = start; column < end; column++, index++)
    {
      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupA);
    }

    }   /* AYTHER: fin gate Plano A */

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }

  /* Window */
  if (w && !hide_w)   /* AYTHER: gate Window */
  {
    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 4) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 15) << 3;

    /* Plane A line buffer */
    dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupW);
    }
  }

  /* AYTHER: ocultar Plano B -> limpiar su buffer antes del merge. */
  if (hide_b)
    memset(&linebuf[0][0x20], 0, bitmap.viewport.w);

  /* Merge background layers */
  merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
}

void render_bg_m5_im2_vs(int line)
{
  int column, start, end, a, w;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;

  /* Common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = (playfield_row_mask << 1) | 1;
  uint32 pf_shift     = playfield_shift;

  /* AYTHER (#28): interlace mode 2 no tenia los gates de capa ni de supresion
     de tiles de plano. Escribir la mascara 0x102 o 0x105 con el VDP en este
     modo -Sonic 2 en dos jugadores, Combat Cars- no hacia absolutamente nada:
     ni efecto, ni error, ni motivo de fallback. El frontend creia haber
     ocultado un plano. Los macros ya incluyen el chequeo de suscripcion y se
     pliegan a 0 sin AYTHER_EXTENSIONS. */
  const uint8 *psupA = AYTHER_PSUP(0);
  const uint8 *psupB = AYTHER_PSUP(1);
  const uint8 *psupW = AYTHER_PSUP(2);
  const int hide_a = AYTHER_HIDE_A;
  const int hide_b = AYTHER_HIDE_B;
  const int hide_w = AYTHER_HIDE_W;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Adjust line offset */
  line = (line << 1) + odd_frame;

  /* Plane B width */
  start = 0;
  end = bitmap.viewport.w >> 4;

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  shift = (xscroll >> 16) & 0x0F;
  index = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  shift = (xscroll & 0x0F);
  index = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  if(shift)
  {
    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x10 + shift];

    atbuf = nt[(index - 1) & pf_col_mask];
    DRAW_COLUMN_IM2_AE(atbuf, v_line, psupB);
  }
  else
  {
    /* Plane B line buffer */
    dst = (uint32 *)&linebuf[0][0x20];
  }

  for(column = 0; column < end; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
#endif

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    atbuf = nt[index & pf_col_mask];
    DRAW_COLUMN_IM2_AE(atbuf, v_line, psupB);
  }

  /* Window & Plane A */
  a = (reg[18] & 0x1F) << 4;
  w = (reg[18] >> 7) & 1;

  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* AYTHER: ocultar Plano A o Window -> limpiar su buffer compartido
     (linebuf[1]) antes de dibujar; lo no dibujado queda transparente y
     se ve el Plano B, que es lo que "ocultar una capa" significa. */
  if (hide_a || hide_w)
    memset(&linebuf[1][0x20], 0, bitmap.viewport.w);

  /* Plane A */
  if (a)
  {
    if (!hide_a)   /* AYTHER: gate Plano A */
    {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    if(shift)
    {
      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x10 + shift + (start << 4)];

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index - 1) & pf_col_mask];
      }

      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupA);
    }
    else
    {
      /* Plane A line buffer */
      dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#endif

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupA);
    }

    }   /* AYTHER: fin gate Plano A */

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }

  /* Window */
  if (w && !hide_w)   /* AYTHER: gate Window */
  {
    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 4) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 15) << 3;

    /* Plane A line buffer */
    dst = (uint32 *)&linebuf[1][0x20 + (start << 4)];

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_IM2_AE(atbuf, v_line, psupW);
    }
  }

  /* AYTHER: ocultar Plano B -> limpiar su buffer antes del merge. */
  if (hide_b)
    memset(&linebuf[0][0x20], 0, bitmap.viewport.w);

  /* Merge background layers */
  merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[(reg[12] & 0x08) >> 2], bitmap.viewport.w);
}

#else

void render_bg_m5(int line)
{
  int column, start, end;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;
  uint8 *lb;

  /* Scroll Planes common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = *(uint32 *)&vsram[0];
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;

  /* Number of columns to draw */
  int width = bitmap.viewport.w >> 4;

  /* Layer priority table */
  uint8 *table = lut[(reg[12] & 8) >> 2];

  /* Window vertical range (cell 0-31) */
  int a = (reg[18] & 0x1F) << 3;

  /* Window position (0=top, 1=bottom) */
  int w = (reg[18] >> 7) & 1;

  /* Test against current line */
  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Plane A */
  if (a)
  {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A scroll */
#ifdef LSB_FIRST
    shift  = (xscroll & 0x0F);
    index  = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
    v_line = (line + yscroll) & pf_row_mask;
#else
    shift  = (xscroll >> 16) & 0x0F;
    index  = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
    v_line = (line + (yscroll >> 16)) & pf_row_mask;
#endif

    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4) + shift];

    /* Plane A name table */
    nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;
#ifdef AYTHER_EXTENSIONS
    if (ayther_observed && AYTHER_LINE_CELLS_ACTIVE)
      ayther_cells_open((uint32)line, 0, v_line, shift);
#endif

    if(shift)
    {
      /* Left-most column is partially shown */
      dst -= 4;

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index-1) & pf_col_mask];
      }

      DRAW_COLUMN(atbuf, v_line)
    }

    for(column = start; column < end; column++, index++)
    {
      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN(atbuf, v_line)
    }

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }
  else
  {
    /* Window width */
    start = 0;
    end = width;
  }

  /* Window Plane */
  if (w)
  {
    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4)];

    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN(atbuf, v_line)
    }
  }

  /* Plane B scroll */
#ifdef LSB_FIRST
  shift  = (xscroll >> 16) & 0x0F;
  index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
  v_line = (line + (yscroll >> 16)) & pf_row_mask;
#else
  shift  = (xscroll & 0x0F);
  index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
  v_line = (line + yscroll) & pf_row_mask;
#endif

  /* Plane B name table */
  nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

  /* Pattern row index */
  v_line = (v_line & 7) << 3;

  /* Background line buffer */
  lb = &linebuf[0][0x20];

  if(shift)
  {
    /* Left-most column is partially shown */
    lb -= (0x10 - shift);

    atbuf = nt[(index-1) & pf_col_mask];
    DRAW_BG_COLUMN(atbuf, v_line, xscroll, yscroll)
  }

  for(column = 0; column < width; column++, index++)
  {
    atbuf = nt[index & pf_col_mask];
    DRAW_BG_COLUMN(atbuf, v_line, xscroll, yscroll)
  }
}

void render_bg_m5_vs(int line)
{
  int column, start, end;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;
  uint8 *lb;

  /* Scroll Planes common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Number of columns to draw */
  int width = bitmap.viewport.w >> 4;

  /* Layer priority table */
  uint8 *table = lut[(reg[12] & 8) >> 2];

  /* Window vertical range (cell 0-31) */
  int a = (reg[18] & 0x1F) << 3;

  /* Window position (0=top, 1=bottom) */
  int w = (reg[18] >> 7) & 1;

  /* Test against current line */
  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  /* Plane A*/
  if (a)
  {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4) + shift];

    if(shift)
    {
      /* Left-most column is partially shown */
      dst -= 4;

      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index-1) & pf_col_mask];
      }

      DRAW_COLUMN(atbuf, v_line)
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#endif

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN(atbuf, v_line)
    }

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }
  else
  {
    /* Window width */
    start = 0;
    end   = width;
  }

  /* Window Plane */
  if (w)
  {
    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4)];

    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN(atbuf, v_line)
    }
  }

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  shift = (xscroll >> 16) & 0x0F;
  index = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  shift = (xscroll & 0x0F);
  index = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Background line buffer */
  lb = &linebuf[0][0x20];

  if(shift)
  {
    /* Left-most column is partially shown */
    lb -= (0x10 - shift);

    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[(index-1) & pf_col_mask];
    DRAW_BG_COLUMN(atbuf, v_line, xscroll, yscroll)
  }

  for(column = 0; column < width; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
#endif

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[index & pf_col_mask];
    DRAW_BG_COLUMN(atbuf, v_line, xscroll, yscroll)
  }
}

void render_bg_m5_vs_enhanced(int line)
{
  int column, start, end;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, next_v_line, *nt;
  uint8 *lb;

  /* Vertical scroll offset */
  int v_offset = 0;

  /* Scroll Planes common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = playfield_row_mask;
  uint32 pf_shift     = playfield_shift;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Number of columns to draw */
  int width = bitmap.viewport.w >> 4;

  /* Layer priority table */
  uint8 *table = lut[(reg[12] & 8) >> 2];

  /* Window vertical range (cell 0-31) */
  int a = (reg[18] & 0x1F) << 3;

  /* Window position (0=top, 1=bottom) */
  int w = (reg[18] >> 7) & 1;

  /* Test against current line */
  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  /* Plane A*/
  if (a)
  {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4) + shift];

    if(shift)
    {
      /* Left-most column is partially shown */
      dst -= 4;

      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index-1) & pf_col_mask];
      }

      DRAW_COLUMN(atbuf, v_line)
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
      next_v_line = (line + vs[column + 1]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
      next_v_line = (line + (vs[column + 1] >> 16)) & pf_row_mask;
#endif

      if (column != end - 1)
      {
        v_offset = ((int)next_v_line - (int)v_line) / 2;
        v_offset = (abs(v_offset) >= config.enhanced_vscroll_limit) ? 0 : v_offset;
      }

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      atbuf = nt[index & pf_col_mask];
#ifdef LSB_FIRST
      GET_LSB_TILE(atbuf, v_line)
#else
      GET_MSB_TILE(atbuf, v_line)
#endif
#ifdef ALIGN_LONG
      WRITE_LONG(dst, src[0] | atex);
      dst++;
      WRITE_LONG(dst, src[1] | atex);
      dst++;
#else
      *dst++ = (src[0] | atex);
      *dst++ = (src[1] | atex);
#endif

#ifdef LSB_FIRST
      v_line = (line + v_offset + vs[column]) & pf_row_mask;
#else
      v_line = (line + v_offset + (vs[column] >> 16)) & pf_row_mask;
#endif

      nt = (uint32 *)&vram[ntab + (((v_line >> 3) << pf_shift) & 0x1FC0)];
      v_line = (v_line & 7) << 3;
      atbuf = nt[index & pf_col_mask];

#ifdef LSB_FIRST
      GET_MSB_TILE(atbuf, v_line)
#else
      GET_LSB_TILE(atbuf, v_line)
#endif
#ifdef ALIGN_LONG
      WRITE_LONG(dst, src[0] | atex);
      dst++;
      WRITE_LONG(dst, src[1] | atex);
      dst++;
#else
      *dst++ = (src[0] | atex);
      *dst++ = (src[1] | atex);
#endif
    }

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }
  else
  {
    /* Window width */
    start = 0;
    end   = width;
  }

  /* Window Plane */
  if (w)
  {
    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4)];

    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 3) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 7) << 3;

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN(atbuf, v_line)
    }
  }

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  shift = (xscroll >> 16) & 0x0F;
  index = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  shift = (xscroll & 0x0F);
  index = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Background line buffer */
  lb = &linebuf[0][0x20];

  if(shift)
  {
    /* Left-most column is partially shown */
    lb -= (0x10 - shift);

    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[(index-1) & pf_col_mask];
    DRAW_BG_COLUMN(atbuf, v_line, xscroll, yscroll)
  }

  for(column = 0; column < width; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
    next_v_line = (line + (vs[column + 1] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
    next_v_line = (line + vs[column + 1]) & pf_row_mask;
#endif

    if (column != width - 1)
    {
      v_offset = ((int)next_v_line - (int)v_line) / 2;
      v_offset = (abs(v_offset) >= config.enhanced_vscroll_limit) ? 0 : v_offset;
    }
    
    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 7) << 3;

    atbuf = nt[index & pf_col_mask];
#ifdef ALIGN_LONG
#ifdef LSB_FIRST
  GET_LSB_TILE(atbuf, v_line)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)

  v_line = (line + v_offset + (vs[column] >> 16)) & pf_row_mask;
  nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];
  v_line = (v_line & 7) << 3;
  atbuf = nt[index & pf_col_mask];
  
  GET_MSB_TILE(atbuf, v_line)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
#else
  GET_MSB_TILE(atbuf, v_line)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)

  v_line = (line + vs[column]) & pf_row_mask;
  nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];
  v_line = (v_line & 7) << 3;
  atbuf = nt[index & pf_col_mask];
 
  GET_LSB_TILE(atbuf, v_line)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = READ_LONG((uint32 *)lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
#endif
#else /* NOT ALIGNED */
#ifdef LSB_FIRST
  GET_LSB_TILE(atbuf, v_line)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)

  v_line = (line + v_offset + (vs[column] >> 16)) & pf_row_mask;
  nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];
  v_line = (v_line & 7) << 3;
  atbuf = nt[index & pf_col_mask];

  GET_MSB_TILE(atbuf, v_line)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
#else
  GET_MSB_TILE(atbuf, v_line)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)

  v_line = (line + vs[column]) & pf_row_mask;
  nt = (uint32 *)&vram[ntbb + (((v_line >> 3) << pf_shift) & 0x1FC0)];
  v_line = (v_line & 7) << 3;
  atbuf = nt[index & pf_col_mask];

  GET_LSB_TILE(atbuf, v_line)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[0] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
  xscroll = *(uint32 *)(lb);
  yscroll = (src[1] | atex);
  DRAW_BG_TILE(xscroll, yscroll)
#endif
#endif /* ALIGN_LONG */
  }
}

void render_bg_m5_im2(int line)
{
  int column, start, end, a, w;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;
  uint8 *lb;

  /* Scroll Planes common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = *(uint32 *)&vsram[0];
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = (playfield_row_mask << 1) | 1;
  uint32 pf_shift     = playfield_shift;

  /* Number of columns to draw */
  int width = bitmap.viewport.w >> 4;

  /* Layer priority table */
  uint8 *table = lut[(reg[12] & 8) >> 2];

  /* Adjust line offset */
  line = (line << 1) + odd_frame;

  /* Window vertical range (cell 0-31) */
  a = (reg[18] & 0x1F) << 4;

  /* Window position (0=top, 1=bottom) */
  w = (reg[18] >> 7) & 1;

  /* Test against current line */
  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Plane A */
  if (a)
  {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A scroll */
#ifdef LSB_FIRST
    shift  = (xscroll & 0x0F);
    index  = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
    v_line = (line + yscroll) & pf_row_mask;
#else
    shift  = (xscroll >> 16) & 0x0F;
    index  = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
    v_line = (line + (yscroll >> 16)) & pf_row_mask;
#endif

    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4) + shift];

    /* Plane A name table */
    nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    if(shift)
    {
      /* Left-most column is partially shown */
      dst -= 4;

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index-1) & pf_col_mask];
      }

      DRAW_COLUMN_IM2(atbuf, v_line)
    }

    for(column = start; column < end; column++, index++)
    {
      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_IM2(atbuf, v_line)
    }

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }
  else
  {
    /* Window width */
    start = 0;
    end   = width;
  }

  /* Window Plane */
  if (w)
  {
    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4)];

    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 4) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 15) << 3;

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_IM2(atbuf, v_line)
    }
  }

  /* Plane B scroll */
#ifdef LSB_FIRST
  shift  = (xscroll >> 16) & 0x0F;
  index  = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
  v_line = (line + (yscroll >> 16)) & pf_row_mask;
#else
  shift  = (xscroll & 0x0F);
  index  = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
  v_line = (line + yscroll) & pf_row_mask;
#endif

  /* Plane B name table */
  nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

  /* Pattern row index */
  v_line = (v_line & 15) << 3;

  /* Background line buffer */
  lb = &linebuf[0][0x20];

  if(shift)
  {
    /* Left-most column is partially shown */
    lb -= (0x10 - shift);

    atbuf = nt[(index-1) & pf_col_mask];
    DRAW_BG_COLUMN_IM2(atbuf, v_line, xscroll, yscroll)
  }

  for(column = 0; column < width; column++, index++)
  {
    atbuf = nt[index & pf_col_mask];
    DRAW_BG_COLUMN_IM2(atbuf, v_line, xscroll, yscroll)
  }
}

void render_bg_m5_im2_vs(int line)
{
  int column, start, end, a, w;
  uint32 atex, atbuf, *src, *dst;
  uint32 shift, index, v_line, *nt;
  uint8 *lb;

  /* common data */
  uint32 xscroll      = *(uint32 *)&vram[hscb + ((line & hscroll_mask) << 2)];
  uint32 yscroll      = 0;
  uint32 pf_col_mask  = playfield_col_mask;
  uint32 pf_row_mask  = (playfield_row_mask << 1) | 1;
  uint32 pf_shift     = playfield_shift;
  uint32 *vs          = (uint32 *)&vsram[0];

  /* Number of columns to draw */
  int width = bitmap.viewport.w >> 4;

  /* Layer priority table */
  uint8 *table = lut[(reg[12] & 8) >> 2];

  /* Adjust line offset */
  line = (line << 1) + odd_frame;

  /* Window vertical range (cell 0-31) */
  a = (reg[18] & 0x1F) << 4;

  /* Window position (0=top, 1=bottom) */
  w = (reg[18] >> 7) & 1;

  /* Test against current line */
  if (w == (line >= a))
  {
    /* Window takes up entire line */
    a = 0;
    w = 1;
  }
  else
  {
    /* Window and Plane A share the line */
    a = clip[0].enable;
    w = clip[1].enable;
  }

  /* Left-most column vertical scrolling when partially shown horizontally (verified on PAL MD2)  */
  /* TODO: check on Genesis 3 models since it apparently behaves differently  */
  /* In H32 mode, vertical scrolling is disabled, in H40 mode, same value is used for both planes */
  /* See Formula One / Kawasaki Superbike Challenge (H32) & Gynoug / Cutie Suzuki no Ringside Angel (H40) */
  if (reg[12] & 1)
  {
    yscroll = vs[19] & (vs[19] >> 16);
  }

  /* Plane A */
  if (a)
  {
    /* Plane A width */
    start = clip[0].left;
    end   = clip[0].right;

    /* Plane A horizontal scroll */
#ifdef LSB_FIRST
    shift = (xscroll & 0x0F);
    index = pf_col_mask + start + 1 - ((xscroll >> 4) & pf_col_mask);
#else
    shift = (xscroll >> 16) & 0x0F;
    index = pf_col_mask + start + 1 - ((xscroll >> 20) & pf_col_mask);
#endif

    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4) + shift];

    if(shift)
    {
      /* Left-most column is partially shown */
      dst -= 4;

      /* Plane A vertical scroll */
      v_line = (line + yscroll) & pf_row_mask;

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      /* Window bug */
      if (start)
      {
        atbuf = nt[index & pf_col_mask];
      }
      else
      {
        atbuf = nt[(index-1) & pf_col_mask];
      }

      DRAW_COLUMN_IM2(atbuf, v_line)
    }

    for(column = start; column < end; column++, index++)
    {
      /* Plane A vertical scroll */
#ifdef LSB_FIRST
      v_line = (line + vs[column]) & pf_row_mask;
#else
      v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#endif

      /* Plane A name table */
      nt = (uint32 *)&vram[ntab + (((v_line >> 4) << pf_shift) & 0x1FC0)];

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      atbuf = nt[index & pf_col_mask];
      DRAW_COLUMN_IM2(atbuf, v_line)
    }

    /* Window width */
    start = clip[1].left;
    end   = clip[1].right;
  }
  else
  {
    /* Window width */
    start = 0;
    end   = width;
  }

  /* Window Plane */
  if (w)
  {
    /* Background line buffer */
    dst = (uint32 *)&linebuf[0][0x20 + (start << 4)];

    /* Window name table */
    nt = (uint32 *)&vram[ntwb | ((line >> 4) << (6 + (reg[12] & 1)))];

    /* Pattern row index */
    v_line = (line & 15) << 3;

    for(column = start; column < end; column++)
    {
      atbuf = nt[column];
      DRAW_COLUMN_IM2(atbuf, v_line)
    }
  }

  /* Plane B horizontal scroll */
#ifdef LSB_FIRST
  shift = (xscroll >> 16) & 0x0F;
  index = pf_col_mask + 1 - ((xscroll >> 20) & pf_col_mask);
#else
  shift = (xscroll & 0x0F);
  index = pf_col_mask + 1 - ((xscroll >> 4) & pf_col_mask);
#endif

  /* Background line buffer */
  lb = &linebuf[0][0x20];

  if(shift)
  {
    /* Left-most column is partially shown */
    lb -= (0x10 - shift);

    /* Plane B vertical scroll */
    v_line = (line + yscroll) & pf_row_mask;

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    atbuf = nt[(index-1) & pf_col_mask];
    DRAW_BG_COLUMN_IM2(atbuf, v_line, xscroll, yscroll)
  }

  for(column = 0; column < width; column++, index++)
  {
    /* Plane B vertical scroll */
#ifdef LSB_FIRST
    v_line = (line + (vs[column] >> 16)) & pf_row_mask;
#else
    v_line = (line + vs[column]) & pf_row_mask;
#endif

    /* Plane B name table */
    nt = (uint32 *)&vram[ntbb + (((v_line >> 4) << pf_shift) & 0x1FC0)];

    /* Pattern row index */
    v_line = (v_line & 15) << 3;

    atbuf = nt[index & pf_col_mask];
    DRAW_BG_COLUMN_IM2(atbuf, v_line, xscroll, yscroll)
  }
}
#endif


/*--------------------------------------------------------------------------*/
/* Sprite layer rendering functions                                         */
/*--------------------------------------------------------------------------*/

void render_obj_tms(int line)
{
  int x, start, end;
  uint8 *lb, *sg;
  uint8 color, pattern[2];
  uint16 temp;

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Default sprite width (8 pixels) */
  int width = 8;

  /* Adjust width for 16x16 sprites */
  width <<= ((reg[1] & 0x02) >> 1);

  /* Adjust width for zoomed sprites */
  width <<= (reg[1] & 0x01);

  /* Latch SOVR flag from previous line to VDP status */
  status |= spr_ovr;

  /* Clear SOVR flag for current line */
  spr_ovr = 0;

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* Sprite X position */
    start = object_info->xpos;

    /* Sprite Color + Early Clock bit */
    color = object_info->size;

    /* X position shift (32 pixels) */
    start -= ((color & 0x80) >> 2);

    /* Pointer to line buffer */
    lb = &linebuf[0][0x20 + start];

    if ((start + width) > 256)
    {
      /* Clip sprites on right edge */
      end = 256 - start;
      start = 0;
    }
    else
    {
      end = width;

      if (start < 0)
      {
        /* Clip sprites on left edge */
        start = 0 - start;
      }
      else
      {
        start = 0;
      }
    }

    /* Sprite Color (0-15) */
    color &= 0x0F;

    /* Sprite Pattern Name */
    temp = object_info->attr;

    /* Mask two LSB for 16x16 sprites */
    temp &= ~((reg[1] & 0x02) >> 0);
    temp &= ~((reg[1] & 0x02) >> 1);

    /* Pointer to sprite generator table */
    sg = (uint8 *)&vram[((reg[6] << 11) & 0x3800) | (temp << 3) | object_info->ypos];

    /* Sprite Pattern data (2 x 8 pixels) */
    pattern[0] = sg[0x00];
    pattern[1] = sg[0x10];

    if (reg[1] & 0x01)
    {
      /* Zoomed sprites are rendered at half speed */
      for (x=start; x<end; x+=2)
      {
        temp = pattern[(x >> 4) & 1];
        temp = (temp >> (7 - ((x >> 1) & 7))) & 0x01;
        temp = temp * color;
        temp |= (lb[x] << 8);
        lb[x] = lut[5][temp];
        status |= ((temp & 0x8000) >> 10);
        temp &= 0x00FF;
        temp |= (lb[x+1] << 8);
        lb[x+1] = lut[5][temp];
        status |= ((temp & 0x8000) >> 10);
      }
    }
    else
    {
      /* Normal sprites */
      for (x=start; x<end; x++)
      {
        temp = pattern[(x >> 3) & 1];
        temp = (temp >> (7 - (x & 7))) & 0x01;
        temp = temp * color;
        temp |= (lb[x] << 8);
        lb[x] = lut[5][temp];
        status |= ((temp & 0x8000) >> 10);
      }
    }

    /* Next sprite entry */
    object_info++;
  }

  /* handle Game Gear reduced screen (160x144) */
  if ((system_hw == SYSTEM_GG) && !config.gg_extra && (v_counter < bitmap.viewport.h))
  {
    int line = v_counter - (bitmap.viewport.h - 144) / 2;
    if ((line < 0) || (line >= 144))
    {
      memset(&linebuf[0][0x20], 0x40, 256);
    }
    else
    {
      if (bitmap.viewport.x > 0)
      {
        memset(&linebuf[0][0x20], 0x40, 48);
        memset(&linebuf[0][0x20+48+160], 0x40, 48);
      }
    }
  }
}

void render_obj_m4(int line)
{
  int i, xpos, end;
  uint8 *src, *lb;
  uint16 temp;

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Default sprite width */
  int width = 8;

  /* Sprite Generator address mask */
  uint16 sg_mask = ~0x1C0 ^ (reg[6] << 6);

  /* Zoomed sprites (not working on Genesis VDP) */
  if (system_hw < SYSTEM_MD)
  {
    width <<= (reg[1] & 0x01);
  }

  /* Unused bits used as a mask on 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    sg_mask |= 0xC0;
  }

  /* Latch SOVR flag from previous line to VDP status */
  status |= spr_ovr;

  /* Clear SOVR flag for current line */
  spr_ovr = 0;

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* 315-5124 VDP specific */
    if (system_hw <= SYSTEM_SMS)
    {
      /* last 4 sprites can not be zoomed */
      if (count < 4)
      {
        /* force default width for remaining sprites */
        width = 8;
      }
    }

    /* Sprite pattern index */
    temp = (object_info->attr | 0x100) & sg_mask;

    /* Pointer to pattern cache line */
    src = (uint8 *)&bg_pattern_cache[(temp << 6) | (object_info->ypos << 3)];

    /* Sprite X position */
    xpos = object_info->xpos;

    /* X position shift */
    xpos -= (reg[0] & 0x08);

    if (xpos < 0)
    {
      /* Clip sprites on left edge */
      src = src - xpos;
      end = xpos + width;
      xpos = 0;
    }
    else if ((xpos + width) > 256)
    {
      /* Clip sprites on right edge */
      end = 256 - xpos;
    }
    else
    {
      /* Sprite maximal width */
      end = width;
    }

    /* Pointer to line buffer */
    lb = &linebuf[0][0x20 + xpos];

    if (width > 8)
    {
      /* Draw sprite pattern (zoomed sprites are rendered at half speed) */
      DRAW_SPRITE_TILE_ACCURATE_2X(end,0,lut[5])
    }
    else
    {
      /* Draw sprite pattern */
      DRAW_SPRITE_TILE_ACCURATE(end,0,lut[5])
    }

    /* Next sprite entry */
    object_info++;
  }

  /* handle Game Gear reduced screen (160x144) */
  if ((system_hw == SYSTEM_GG) && !config.gg_extra && (v_counter < bitmap.viewport.h))
  {
    int line = v_counter - (bitmap.viewport.h - 144) / 2;
    if ((line < 0) || (line >= 144))
    {
      memset(&linebuf[0][0x20], 0x40, 256);
    }
    else
    {
      if (bitmap.viewport.x > 0)
      {
        memset(&linebuf[0][0x20], 0x40, 48);
        memset(&linebuf[0][0x20+48+160], 0x40, 48);
      }
    }
  }
}

AYTHER_HOT_INLINE void render_obj_m5_impl(int line, int ayther_observed)
{
  int i, column;
  int xpos, width;
  int pixelcount = 0;
  int masked = 0;
  int max_pixels = AYTHER_RC_NOLIMIT_ACTIVE ? 0x7FFF   /* AYTHER (#270): sin presupuesto */
                                     : MODE5_MAX_SPRITE_PIXELS;
  /* #36 punto 6: el flag se leia del global una vez POR SPRITE. Es una carga
     barata, pero ademas le dice al compilador que ese global puede cambiar
     adentro del bucle, y eso le impide mantener cosas en registros. Se lee una
     vez por linea, que es cuando puede cambiar. */
  const int rc_nomask = AYTHER_RC_NOMASK_ACTIVE;

  uint8 *src, *s, *lb;
  uint32 temp, v_line;
  uint32 attr, name, atex;
#ifdef AYTHER_EXTENSIONS
  /* #31/#37/#41: paralelo a `lb`, con el mismo indice. En el clon rapido
     `ayther_observed` es 0 y todo esto desaparece en compilacion. */
  uint8 *spx = 0;
  const int ayther_sh = (reg[12] & 0x08) != 0;
  if (ayther_observed)
  {
    memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
    ayther_obj_px_exact = 1;
  }
#endif

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* Sprite X position */
    xpos = object_info->xpos;

    /* Sprite masking  */
    if (xpos)
    {
      /* Requires at least one sprite with xpos > 0 */
      spr_ovr = 1;
    }
    else if (spr_ovr && !rc_nomask)   /* AYTHER (#270): nomask la anula */
    {
      /* Remaining sprites are not drawn */
      masked = 1;
    }

    /* Display area offset */
    xpos = xpos - 0x80;

    /* Sprite size */
    temp = object_info->size;

    /* Sprite width */
    width = 8 + ((temp & 0x0C) << 1);

    /* Update pixel count (off-screen sprites are included) */
    pixelcount += width;

    /* Is sprite across visible area ? */
    if (((xpos + width) > 0) && (xpos < bitmap.viewport.w) && !masked)
    {
      /* Sprite attributes */
      attr = object_info->attr;

      /* Sprite vertical offset */
      v_line = object_info->ypos;

      /* Sprite priority + palette bits */
      atex = (attr >> 9) & 0x70;

      /* Pattern name base */
      name = attr & 0x07FF;

      /* Mask vflip/hflip */
      attr &= 0x1800;

      /* Pointer into pattern name offset look-up table */
      s = &name_lut[((attr >> 3) & 0x300) | (temp << 4) | ((v_line & 0x18) >> 1)];

      /* Pointer into line buffer */
      lb = &linebuf[0][0x20 + xpos];
#ifdef AYTHER_EXTENSIONS
      spx = &ayther_sprite_px[0x20 + xpos];
#endif

      /* Max. number of sprite pixels rendered per line */
      if (pixelcount > max_pixels)
      {
        /* Adjust number of pixels to draw */
        width -= (pixelcount - max_pixels);
      }

      /* Number of tiles to draw */
      width = width >> 3;

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Draw sprite patterns */
#ifdef AYTHER_EXTENSIONS
      for (column = 0; column < width; column++, lb+=8, spx+=8)
      {
        temp = attr | ((name + s[column]) & 0x07FF);
        src = &bg_pattern_cache[(temp << 6) | (v_line)];
        AYTHER_DRAW_SPRITE_TILE(8,atex,lut[1])
      }
#else
      for (column = 0; column < width; column++, lb+=8)
      {
        temp = attr | ((name + s[column]) & 0x07FF);
        src = &bg_pattern_cache[(temp << 6) | (v_line)];
        DRAW_SPRITE_TILE(8,atex,lut[1])
      }
#endif
    }

    /* Sprite limit */
    if (pixelcount >= max_pixels)
    {
      /* Sprite masking is effective on next line if max pixel width is reached */
      spr_ovr = (pixelcount >= bitmap.viewport.w);

      /* Stop sprite rendering */
      return;
    }

    /* Next sprite entry */
    object_info++;
  }

  /* Clear sprite masking for next line  */
  spr_ovr = 0;
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_NOINLINE void render_obj_m5_fast_path(int line)
{
  render_obj_m5_impl(line, 0);
}

static AYTHER_NOINLINE void render_obj_m5_observed_path(int line)
{
  render_obj_m5_impl(line, 1);
}
#endif

void render_obj_m5(int line)
{
#ifdef AYTHER_EXTENSIONS
  /* #31/#37/#41: el clon observado es el que escribe el bit de sprite exacto.
     Solo hace falta cuando alguien va a leerlo -- dim o atribucion-, y el
     predicado ya lo calculo render_line: viaja en `ayther_obj_pass`. */
  if (ayther_obj_pass)
    render_obj_m5_observed_path(line);
  else
    render_obj_m5_fast_path(line);
#else
  render_obj_m5_impl(line, 0);
#endif
}

void render_obj_m5_ste(int line)
{
  int i, column;
  int xpos, width;
  int pixelcount = 0;
  int masked = 0;
  int max_pixels = AYTHER_RC_NOLIMIT_ACTIVE ? 0x7FFF   /* AYTHER (#270): sin presupuesto */
                                     : MODE5_MAX_SPRITE_PIXELS;
  /* #36 punto 6: el flag se leia del global una vez POR SPRITE. Es una carga
     barata, pero ademas le dice al compilador que ese global puede cambiar
     adentro del bucle, y eso le impide mantener cosas en registros. Se lee una
     vez por linea, que es cuando puede cambiar. */
  const int rc_nomask = AYTHER_RC_NOMASK_ACTIVE;

  uint8 *src, *s, *lb;
  uint32 temp, v_line;
  uint32 attr, name, atex;

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Clear sprite line buffer */
  memset(&linebuf[1][0], 0, bitmap.viewport.w + 0x40);

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* Sprite X position */
    xpos = object_info->xpos;

    /* Sprite masking  */
    if (xpos)
    {
      /* Requires at least one sprite with xpos > 0 */
      spr_ovr = 1;
    }
    else if (spr_ovr && !rc_nomask)   /* AYTHER (#270): nomask la anula */
    {
      /* Remaining sprites are not drawn */
      masked = 1;
    }

    /* Display area offset */
    xpos = xpos - 0x80;

    /* Sprite size */
    temp = object_info->size;

    /* Sprite width */
    width = 8 + ((temp & 0x0C) << 1);

    /* Update pixel count (off-screen sprites are included) */
    pixelcount += width;

    /* Is sprite across visible area ? */
    if (((xpos + width) > 0) && (xpos < bitmap.viewport.w) && !masked)
    {
      /* Sprite attributes */
      attr = object_info->attr;

      /* Sprite vertical offset */
      v_line = object_info->ypos;

      /* Sprite priority + palette bits */
      atex = (attr >> 9) & 0x70;

      /* Pattern name base */
      name = attr & 0x07FF;

      /* Mask vflip/hflip */
      attr &= 0x1800;

      /* Pointer into pattern name offset look-up table */
      s = &name_lut[((attr >> 3) & 0x300) | (temp << 4) | ((v_line & 0x18) >> 1)];

      /* Pointer into line buffer */
      lb = &linebuf[1][0x20 + xpos];

      /* Adjust number of pixels to draw for sprite limit */
      if (pixelcount > max_pixels)
      {
        width -= (pixelcount - max_pixels);
      }

      /* Number of tiles to draw */
      width = width >> 3;

      /* Pattern row index */
      v_line = (v_line & 7) << 3;

      /* Draw sprite patterns */
      for (column = 0; column < width; column++, lb+=8)
      {
        temp = attr | ((name + s[column]) & 0x07FF);
        src = &bg_pattern_cache[(temp << 6) | (v_line)];
        DRAW_SPRITE_TILE(8,atex,lut[3])
      }
    }

    /* Sprite limit */
    if (pixelcount >= max_pixels)
    {
      /* Sprite masking is effective on next line if max pixel width is reached */
      spr_ovr = (pixelcount >= bitmap.viewport.w);

      /* Merge background & sprite layers */
      merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[4], bitmap.viewport.w);

      /* Stop sprite rendering */
      return;
    }

    /* Next sprite entry */
    object_info++;
  }

  /* Clear sprite masking for next line  */
  spr_ovr = 0;

  /* Merge background & sprite layers */
  merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[4], bitmap.viewport.w);
}

AYTHER_HOT_INLINE void render_obj_m5_im2_impl(int line, int ayther_observed)
{
  int i, column;
  int xpos, width;
  int pixelcount = 0;
  int masked = 0;
  int max_pixels = AYTHER_RC_NOLIMIT_ACTIVE ? 0x7FFF   /* AYTHER (#270): sin presupuesto */
                                     : MODE5_MAX_SPRITE_PIXELS;
  /* #36 punto 6: el flag se leia del global una vez POR SPRITE. Es una carga
     barata, pero ademas le dice al compilador que ese global puede cambiar
     adentro del bucle, y eso le impide mantener cosas en registros. Se lee una
     vez por linea, que es cuando puede cambiar. */
  const int rc_nomask = AYTHER_RC_NOMASK_ACTIVE;

  uint8 *src, *s, *lb;
  uint32 temp, v_line;
  uint32 attr, name, atex;
#ifdef AYTHER_EXTENSIONS
  /* #31/#37/#41: paralelo a `lb`, con el mismo indice. En el clon rapido
     `ayther_observed` es 0 y todo esto desaparece en compilacion. */
  uint8 *spx = 0;
  const int ayther_sh = (reg[12] & 0x08) != 0;
  if (ayther_observed)
  {
    memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
    ayther_obj_px_exact = 1;
  }
#endif

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* Sprite X position */
    xpos = object_info->xpos;

    /* Sprite masking  */
    if (xpos)
    {
      /* Requires at least one sprite with xpos > 0 */
      spr_ovr = 1;
    }
    else if (spr_ovr && !rc_nomask)   /* AYTHER (#270): nomask la anula */
    {
      /* Remaining sprites are not drawn */
      masked = 1;
    }

    /* Display area offset */
    xpos = xpos - 0x80;

    /* Sprite size */
    temp = object_info->size;

    /* Sprite width */
    width = 8 + ((temp & 0x0C) << 1);

    /* Update pixel count (off-screen sprites are included) */
    pixelcount += width;

    /* Is sprite across visible area ? */
    if (((xpos + width) > 0) && (xpos < bitmap.viewport.w) && !masked)
    {
      /* Sprite attributes */
      attr = object_info->attr;

      /* Sprite y offset */
      v_line = object_info->ypos;

      /* Sprite priority + palette bits */
      atex = (attr >> 9) & 0x70;

      /* Pattern name base */
      name = attr & 0x03FF;

      /* Mask vflip/hflip */
      attr &= 0x1800;

      /* Pattern name offset lookup table */
      s = &name_lut[((attr >> 3) & 0x300) | (temp << 4) | ((v_line & 0x30) >> 2)];

      /* Pointer into line buffer */
      lb = &linebuf[0][0x20 + xpos];
#ifdef AYTHER_EXTENSIONS
      spx = &ayther_sprite_px[0x20 + xpos];
#endif

      /* Adjust width for sprite limit */
      if (pixelcount > max_pixels)
      {
        width -= (pixelcount - max_pixels);
      }

      /* Number of tiles to draw */
      width = width >> 3;

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      /* Render sprite patterns */
#ifdef AYTHER_EXTENSIONS
      for(column = 0; column < width; column ++, lb+=8, spx+=8)
      {
        temp = attr | (((name + s[column]) & 0x3ff) << 1);
        src = &bg_pattern_cache[((temp << 6) | (v_line)) ^ ((attr & 0x1000) >> 6)];
        AYTHER_DRAW_SPRITE_TILE(8,atex,lut[1])
      }
#else
      for(column = 0; column < width; column ++, lb+=8)
      {
        temp = attr | (((name + s[column]) & 0x3ff) << 1);
        src = &bg_pattern_cache[((temp << 6) | (v_line)) ^ ((attr & 0x1000) >> 6)];
        DRAW_SPRITE_TILE(8,atex,lut[1])
      }
#endif
    }

    /* Sprite Limit */
    if (pixelcount >= max_pixels)
    {
      /* Sprite masking is effective on next line if max pixel width is reached */
      spr_ovr = (pixelcount >= bitmap.viewport.w);

      /* Stop sprite rendering */
      return;
    }

    /* Next sprite entry */
    object_info++;
  }

  /* Clear sprite masking for next line */
  spr_ovr = 0;
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_NOINLINE void render_obj_m5_im2_fast_path(int line)
{
  render_obj_m5_im2_impl(line, 0);
}

static AYTHER_NOINLINE void render_obj_m5_im2_observed_path(int line)
{
  render_obj_m5_im2_impl(line, 1);
}
#endif

void render_obj_m5_im2(int line)
{
#ifdef AYTHER_EXTENSIONS
  if (ayther_obj_pass)
    render_obj_m5_im2_observed_path(line);
  else
    render_obj_m5_im2_fast_path(line);
#else
  render_obj_m5_im2_impl(line, 0);
#endif
}

void render_obj_m5_im2_ste(int line)
{
  int i, column;
  int xpos, width;
  int pixelcount = 0;
  int masked = 0;
  int max_pixels = AYTHER_RC_NOLIMIT_ACTIVE ? 0x7FFF   /* AYTHER (#270): sin presupuesto */
                                     : MODE5_MAX_SPRITE_PIXELS;
  /* #36 punto 6: el flag se leia del global una vez POR SPRITE. Es una carga
     barata, pero ademas le dice al compilador que ese global puede cambiar
     adentro del bucle, y eso le impide mantener cosas en registros. Se lee una
     vez por linea, que es cuando puede cambiar. */
  const int rc_nomask = AYTHER_RC_NOMASK_ACTIVE;

  uint8 *src, *s, *lb;
  uint32 temp, v_line;
  uint32 attr, name, atex;

  /* Sprite list for current line */
  object_info_t *object_info = obj_info[line];
  int count = object_count[line];

  /* Clear sprite line buffer */
  memset(&linebuf[1][0], 0, bitmap.viewport.w + 0x40);

  /* Draw sprites in front-to-back order */
  while (count--)
  {
    /* Sprite X position */
    xpos = object_info->xpos;

    /* Sprite masking  */
    if (xpos)
    {
      /* Requires at least one sprite with xpos > 0 */
      spr_ovr = 1;
    }
    else if (spr_ovr && !rc_nomask)   /* AYTHER (#270): nomask la anula */
    {
      /* Remaining sprites are not drawn */
      masked = 1;
    }

    /* Display area offset */
    xpos = xpos - 0x80;

    /* Sprite size */
    temp = object_info->size;

    /* Sprite width */
    width = 8 + ((temp & 0x0C) << 1);

    /* Update pixel count (off-screen sprites are included) */
    pixelcount += width;

    /* Is sprite across visible area ? */
    if (((xpos + width) > 0) && (xpos < bitmap.viewport.w) && !masked)
    {
      /* Sprite attributes */
      attr = object_info->attr;

      /* Sprite y offset */
      v_line = object_info->ypos;

      /* Sprite priority + palette bits */
      atex = (attr >> 9) & 0x70;

      /* Pattern name base */
      name = attr & 0x03FF;

      /* Mask vflip/hflip */
      attr &= 0x1800;

      /* Pattern name offset lookup table */
      s = &name_lut[((attr >> 3) & 0x300) | (temp << 4) | ((v_line & 0x30) >> 2)];

      /* Pointer into line buffer */
      lb = &linebuf[1][0x20 + xpos];

      /* Adjust width for sprite limit */
      if (pixelcount > max_pixels)
      {
        width -= (pixelcount - max_pixels);
      }

      /* Number of tiles to draw */
      width = width >> 3;

      /* Pattern row index */
      v_line = (v_line & 15) << 3;

      /* Render sprite patterns */
      for(column = 0; column < width; column ++, lb+=8)
      {
        temp = attr | (((name + s[column]) & 0x3ff) << 1);
        src = &bg_pattern_cache[((temp << 6) | (v_line)) ^ ((attr & 0x1000) >> 6)];
        DRAW_SPRITE_TILE(8,atex,lut[3])
      }
    }

    /* Sprite Limit */
    if (pixelcount >= max_pixels)
    {
      /* Sprite masking is effective on next line if max pixel width is reached */
      spr_ovr = (pixelcount >= bitmap.viewport.w);

      /* Merge background & sprite layers */
      merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[4], bitmap.viewport.w);

      /* Stop sprite rendering */
      return;
    }

    /* Next sprite entry */
    object_info++;
  }

  /* Clear sprite masking for next line */
  spr_ovr = 0;

  /* Merge background & sprite layers */
  merge(&linebuf[1][0x20], &linebuf[0][0x20], &linebuf[0][0x20], lut[4], bitmap.viewport.w);
}


/*--------------------------------------------------------------------------*/
/* Sprites Parsing functions                                                */
/*--------------------------------------------------------------------------*/

void parse_satb_tms(int line)
{
  int i = 0;

  /* Sprite counter (4 max. per line) */
  int count = 0;

  /* no sprites in Text modes */
  if (!(reg[1] & 0x10))
  {
    /* Sprite Y position */
    int ypos;

    /* Sprite list for next line */
    object_info_t *object_info = obj_info[(line + 1) & 1];

    /* Pointer to sprite attribute table */
    uint8 *st = &vram[(reg[5] << 7) & 0x3F80];

    /* Sprite height (8 pixels by default) */
    int height = 8;

    /* Adjust height for 16x16 sprites */
    height <<= ((reg[1] & 0x02) >> 1);

    /* Parse Sprite Table (32 entries) */
    do
    {
      /* Read sprite Y position */
      ypos = st[i << 2];

      /* Check end of sprite list marker */
      if (ypos == 0xD0)
      {
        break;
      }

      /* Wrap Y coordinate for sprites > 256-32 */
      if (ypos >= 224)
      {
        ypos -= 256;
      }

      /* Y range */
      ypos = line - ypos;

      /* Adjust Y range for zoomed sprites */
      ypos >>= (reg[1] & 0x01);

      /* Sprite is visible on this line ? */
      if ((ypos >= 0) && (ypos < height))
      {
        /* Sprite overflow */
        if (count == TMS_MAX_SPRITES_PER_LINE)
        {
          /* Flag is set only during active area */
          if (line < bitmap.viewport.h)
          {
            spr_ovr = 0x40;
          }
          break;
        }

        /* Store sprite attributes for later processing */
        object_info->ypos = ypos;
        object_info->xpos = st[(i << 2) + 1];
        object_info->attr = st[(i << 2) + 2];
        object_info->size = st[(i << 2) + 3];

        /* Increment Sprite count */
        ++count;

        /* Next sprite entry */
        object_info++;
      }
    }
    while (++i < 32);
  }

  /* Update sprite count for next line */
  object_count[(line + 1) & 1] = count;

  /* Insert number of last sprite entry processed */
  status = (status & 0xE0) | (i & 0x1F);
}

void parse_satb_m4(int line)
{
  int i = 0;
  uint8 *st;

  /* Sprite counter (8 max. per line) */
  int count = 0;

  /* Sprite Y position */
  int ypos;

  /* Sprite list for next line */
  object_info_t *object_info = obj_info[(line + 1) & 1];

  /* Sprite height (8x8 or 8x16) */
  int height = 8 + ((reg[1] & 0x02) << 2);

  /* Sprite attribute table address mask */
  uint16 st_mask = ~0x3F80 ^ (reg[5] << 7);

  /* Unused bits used as a mask on 315-5124 VDP only */
  if (system_hw > SYSTEM_SMS)
  {
    st_mask |= 0x80;
  }

  /* Pointer to sprite attribute table */
  st = &vram[st_mask & 0x3F00];

  /* Parse Sprite Table (64 entries) */
  do
  {
    /* Read sprite Y position */
    ypos = st[i];

    /* Check end of sprite list marker (no effect in extended modes) */
    if ((ypos == 208) && (bitmap.viewport.h == 192))
    {
      break;
    }

    /* Wrap Y coordinate (NB: this is likely not 100% accurate and needs to be verified on real hardware) */
    if (ypos > (bitmap.viewport.h + 16))
    {
      ypos -= 256;
    }

    /* Y range */
    ypos = line - ypos;

    /* Adjust Y range for zoomed sprites (not working on Mega Drive VDP) */
    if (system_hw < SYSTEM_MD)
    {
      ypos >>= (reg[1] & 0x01);
    }

    /* Check if sprite is visible on this line */
    if ((ypos >= 0) && (ypos < height))
    {
      /* Sprite overflow */
      if (count == MODE4_MAX_SPRITES_PER_LINE)
      {
        /* Flag is set only during active area */
        if ((line >= 0) && (line < bitmap.viewport.h))
        {
          spr_ovr = 0x40;
        }
        break;
      }

      /* Store sprite attributes for later processing */
      object_info->ypos = ypos;
      object_info->xpos = st[(0x80 + (i << 1)) & st_mask];
      object_info->attr = st[(0x81 + (i << 1)) & st_mask];

      /* 8x16 sprites pattern index LSB is masked */
      if (reg[1] & 0x02)
      {
        object_info->attr &= 0xfe;
      }

      /* Increment Sprite count */
      ++count;

      /* Next sprite entry */
      object_info++;
    }
  }
  while (++i < 64);

  /* Update sprite count for next line */
  object_count[(line + 1) & 1] = count;
}

#ifdef AYTHER_EXTENSIONS
/* Stock Mode 5 parser selected once per scanline when sprite observation and
   render controls are idle. It avoids subscription/suppression branches in
   the SAT chain loop. */
static void parse_satb_m5_fast(int line, int im2)
{
  int ypos;
  int height;
  int size;
  int link = 0;
  int count = 0;
  int total = max_sprite_pixels >> 2;
  uint16 *p = (uint16 *)&vram[satb];
  uint16 *q = (uint16 *)&sat[0];
  object_info_t *object_info = obj_info[(line + 1) & 1];

  line = im2 ? (((line + 0x81) << 1) + odd_frame) : (line + 0x81);
  do
  {
    ypos = q[link] & (im2 ? 0x3FF : 0x1FF);
    if (line >= ypos)
    {
      size = q[link + 1] >> 8;
      height = (im2 ? 16 : 8) +
        ((size & 3) << (im2 ? 4 : 3));
      ypos = line - ypos;
      if (ypos < height)
      {
        if (count == MODE5_MAX_SPRITES_PER_LINE)
        {
          status |= 0x40;
          break;
        }
        object_info->attr = p[link + 2];
        object_info->xpos = p[link + 3] & 0x1ff;
        object_info->ypos = ypos;
        object_info->size = size & 0x0f;
        ++count;
        ++object_info;
      }
    }
    link = (q[link + 1] & 0x7F) << 2;
    if ((link == 0) || (link >= bitmap.viewport.w)) break;
  }
  while (--total);
  object_count[im2 ? ((line >> 1) & 1) : (line & 1)] = count;
}
#endif

void parse_satb_m5(int line)
{
#ifdef AYTHER_EXTENSIONS
  /* #36: la suscripcion habilita, pero lo que obliga al parser completo es que
     haya ALGO que hacer. SPRITE_CAPTURE si obliga -- el frontend pidio los
     sprites-. RENDER_CONTROLS no: sin un solo slot suprimido, el parser rapido
     produce exactamente lo mismo, y `test_satb_equiv` (#33) es lo que sostiene
     esa afirmacion. Antes se pagaba la CAPACIDAD de suprimir en cada linea de
     cada frame, estuviera o no suprimido algo. */
  if (!AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE) &&
      !(AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) &&
        ayther_sprite_suppress_active) &&
      !ayther_rc_nolimit)
  {
    parse_satb_m5_fast(line, 0);
    return;
  }
  AYTHER_METRIC_INC(satb_slow_path);
#endif
  /* Sprite Y position */
  int ypos;

  /* Sprite height */
  int height;

  /* Sprite size data */
  int size;

  /* Sprite link data */
  int link = 0;

  /* Sprite counter */
  int count = 0;

  /* max. number of rendered sprites (16 or 20 sprites per line by default) */
  int max = MODE5_MAX_SPRITES_PER_LINE;
  if (AYTHER_RC_NOLIMIT_ACTIVE) max = 80;   /* AYTHER (#270): recomposición sin límite */

  /* max. number of parsed sprites (64 or 80 sprites per line by default) */
  int total = max_sprite_pixels >> 2;
  int total0 = total;   /* AYTHER: para chain_pos = total0 - total */

  /* Runtime feature state is latched once per scanline, outside the SAT loop. */
#ifdef AYTHER_EXTENSIONS
  const int ayther_capture_active =
    AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE);
  const int ayther_suppression_active =
    AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS);
#else
  const int ayther_capture_active = 0;
  const int ayther_suppression_active = 0;
#endif

  /* Pointer to sprite attribute table */
  uint16 *p = (uint16 *) &vram[satb];

  /* Pointer to internal RAM */
  uint16 *q = (uint16 *) &sat[0];

  /* Sprite list for next line */
  object_info_t *object_info = obj_info[(line + 1) & 1];

  /* Adjust line offset */
  line += 0x81;

  do
  {
    /* Read sprite Y position from internal SAT cache (9 bits) */
    ypos = q[link] & 0x1FF;

    /* Check if sprite Y position has been reached */
    if (line >= ypos)
    {
      /* Read sprite size from internal SAT cache */
      size = q[link + 1] >> 8;

      /* Sprite height (8, 16, 24 or 32 pixels) */
      height = 8 + ((size & 3) << 3);

      /* Y range */
      ypos = line - ypos;

      /* Check if sprite is visible on current line */
      if (ypos < height)
      {
        /* AYTHER: ocultar sprite por hash — saltear el slot SAT suprimido (no se
           agrega → no se dibuja). Sólo el frame visible suprime; la re-sim bare
           corre con la máscara vacía → status del VDP intacto, replay sin drift. */
        if (!AYTHER_SPR_SUPPRESSED_ACTIVE(
              ayther_suppression_active, link >> 2))
        {
        /* Sprite overflow */
        if (count == max)
        {
          status |= 0x40;
          break;
        }

        /* Update sprite list (only name, attribute & xpos are parsed from VRAM) */
        object_info->attr  = p[link + 2];
        object_info->xpos  = p[link + 3] & 0x1ff;
        object_info->ypos  = ypos;
        object_info->size  = size & 0x0f;
        /* AYTHER: registrar el sprite parseado (ids 0x10B/0x10C). q[link] = Y cruda
           (la caché), p[link+3] = X, p[link+2] = attr; w/h desde `size` (q[link+1]>>8,
           h=bits1:0, w=bits3:2). sat_idx = link>>2 (espacio de la máscara 0x103);
           chain_pos = paso en la cadena (prioridad real de dibujo). Captura los
           sprites reescritos in-place a mitad de frame (genio del logo Sega),
           deduplicado por (Y,X,attr) entre scanlines. */
        if (ayther_capture_active)
          ayther_sprite_capture_record(
              (uint16)(q[link] & 0x1FF),
              (uint16)(p[link + 3] & 0x1ff), (uint16)p[link + 2],
              (uint8)(((size >> 2) & 3) + 1),
              (uint8)((size & 3) + 1),
              (uint8)(link >> 2), (uint8)(total0 - total));

        /* Increment Sprite count */
        ++count;

        /* Next sprite entry */
        object_info++;
        }
      }
    }

    /* Read link data from internal SAT cache */
    link = (q[link + 1] & 0x7F) << 2;

    /* Stop parsing if link data points to first entry (#0) or after the last entry (#64 in H32 mode, #80 in H40 mode) */
    if ((link == 0) || (link >= bitmap.viewport.w)) break;
  }
  while (--total);

  /* Update sprite count for next line (line value already incremented) */
  object_count[line & 1] = count;
}

void parse_satb_m5_im2(int line)
{
#ifdef AYTHER_EXTENSIONS
  /* #36: la suscripcion habilita, pero lo que obliga al parser completo es que
     haya ALGO que hacer. SPRITE_CAPTURE si obliga -- el frontend pidio los
     sprites-. RENDER_CONTROLS no: sin un solo slot suprimido, el parser rapido
     produce exactamente lo mismo, y `test_satb_equiv` (#33) es lo que sostiene
     esa afirmacion. Antes se pagaba la CAPACIDAD de suprimir en cada linea de
     cada frame, estuviera o no suprimido algo. */
  if (!AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE) &&
      !(AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) &&
        ayther_sprite_suppress_active) &&
      !ayther_rc_nolimit)
  {
    parse_satb_m5_fast(line, 1);
    return;
  }
  AYTHER_METRIC_INC(satb_slow_path);
#endif
  /* Sprite Y position */
  int ypos;

  /* Sprite height */
  int height;

  /* Sprite size data */
  int size;

  /* Sprite link data */
  int link = 0;

  /* Sprite counter */
  int count = 0;

  /* max. number of rendered sprites (16 or 20 sprites per line by default) */
  int max = MODE5_MAX_SPRITES_PER_LINE;
  if (AYTHER_RC_NOLIMIT_ACTIVE) max = 80;   /* AYTHER (#270): recomposición sin límite */

  /* max. number of parsed sprites (64 or 80 sprites per line by default) */
  int total = max_sprite_pixels >> 2;
  int total0 = total;   /* AYTHER: para chain_pos = total0 - total */

  /* Runtime feature state is latched once per scanline, outside the SAT loop. */
#ifdef AYTHER_EXTENSIONS
  const int ayther_capture_active =
    AYTHER_SUBSCRIBED(AYTHER_SUB_SPRITE_CAPTURE);
  const int ayther_suppression_active =
    AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS);
#else
  const int ayther_capture_active = 0;
  const int ayther_suppression_active = 0;
#endif

  /* Pointer to sprite attribute table */
  uint16 *p = (uint16 *) &vram[satb];

  /* Pointer to internal RAM */
  uint16 *q = (uint16 *) &sat[0];

  /* Sprite list for next line */
  object_info_t *object_info = obj_info[(line + 1) & 1];

  /* Adjust line offset */
  line = ((line + 0x81) << 1) + odd_frame;

  do
  {
    /* Read sprite Y position from internal SAT cache (10 bits) */
    ypos = q[link] & 0x3FF;

    /* Check if sprite Y position has been reached */
    if (line >= ypos)
    {
      /* Read sprite size from internal SAT cache */
      size = q[link + 1] >> 8;

      /* Sprite height (16, 32, 48 or 64 pixels) */
      height = 16 + ((size & 3) << 4);

      /* Y range */
      ypos = line - ypos;

      /* Check if sprite is visible on current line */
      if (ypos < height)
      {
        /* AYTHER: ocultar sprite por hash — saltear el slot SAT suprimido (no se
           agrega → no se dibuja). Sólo el frame visible suprime; la re-sim bare
           corre con la máscara vacía → status del VDP intacto, replay sin drift. */
        if (!AYTHER_SPR_SUPPRESSED_ACTIVE(
              ayther_suppression_active, link >> 2))
        {
        /* Sprite overflow */
        if (count == max)
        {
          status |= 0x40;
          break;
        }

        /* Update sprite list (only name, attribute & xpos are parsed from VRAM) */
        object_info->attr  = p[link + 2];
        object_info->xpos  = p[link + 3] & 0x1ff;
        object_info->ypos  = ypos;
        object_info->size  = size & 0x0f;
        /* AYTHER: registrar el sprite parseado (ids 0x10B/0x10C) — variante im2.
           sat_idx = link>>2 (espacio de la máscara 0x103); chain_pos = paso en la
           cadena (prioridad real de dibujo). */
        if (ayther_capture_active)
          ayther_sprite_capture_record(
              (uint16)(q[link] & 0x3FF),
              (uint16)(p[link + 3] & 0x1ff), (uint16)p[link + 2],
              (uint8)(((size >> 2) & 3) + 1),
              (uint8)((size & 3) + 1),
              (uint8)(link >> 2), (uint8)(total0 - total));

        /* Increment Sprite count */
        ++count;

        /* Next sprite entry */
        object_info++;
        }
      }
    }

    /* Read link data from internal SAT cache */
    link = (q[link + 1] & 0x7F) << 2;

    /* Stop parsing if link data points to first entry (#0) or after the last entry (#64 in H32 mode, #80 in H40 mode) */
    if ((link == 0) || (link >= bitmap.viewport.w)) break;
  }
  while (--total);

  /* Update sprite count for next line (line value already incremented) */
  object_count[(line >> 1) & 1] = count;
}


/*--------------------------------------------------------------------------*/
/* Pattern cache update function                                            */
/*--------------------------------------------------------------------------*/

void update_bg_pattern_cache_m4(int index)
{
  int i;
  uint8 x, y, c;
  uint8 *dst;
  uint16 name, bp01, bp23;
  uint32 bp;

  for(i = 0; i < index; i++)
  {
    /* Get modified pattern name index */
    name = bg_name_list[i];

    /* Pattern cache base address */
    dst = &bg_pattern_cache[name << 6];

    /* Check modified lines */
    for(y = 0; y < 8; y++)
    {
      if(bg_name_dirty[name] & (1 << y))
      {
        /* Byteplane data */
        bp01 = *(uint16 *)&vram[(name << 5) | (y << 2) | (0)];
        bp23 = *(uint16 *)&vram[(name << 5) | (y << 2) | (2)];

        /* Convert to pixel line data (4 bytes = 8 pixels)*/
        /* (msb) p7p6 p5p4 p3p2 p1p0 (lsb) */
        bp = (bp_lut[bp01] >> 2) | (bp_lut[bp23]);

        /* Update cached line (8 pixels = 8 bytes) */
        for(x = 0; x < 8; x++)
        {
          /* Extract pixel data */
          c = bp & 0x0F;

          /* Pattern cache data (one pattern = 8 bytes) */
          /* byte0 <-> p0 p1 p2 p3 p4 p5 p6 p7 <-> byte7 (hflip = 0) */
          /* byte0 <-> p7 p6 p5 p4 p3 p2 p1 p0 <-> byte7 (hflip = 1) */
          dst[0x00000 | (y << 3) | (x)] = (c);            /* vflip=0 & hflip=0 */
          dst[0x08000 | (y << 3) | (x ^ 7)] = (c);        /* vflip=0 & hflip=1 */
          dst[0x10000 | ((y ^ 7) << 3) | (x)] = (c);      /* vflip=1 & hflip=0 */
          dst[0x18000 | ((y ^ 7) << 3) | (x ^ 7)] = (c);  /* vflip=1 & hflip=1 */

          /* Next pixel */
          bp = bp >> 4;
        }
      }
    }

    /* Clear modified pattern flag */
    bg_name_dirty[name] = 0;
  }
}

void update_bg_pattern_cache_m5(int index)
{
  int i;
  uint8 x, y, c;
  uint8 *dst;
  uint16 name;
  uint32 bp;

  for(i = 0; i < index; i++)
  {
    /* Get modified pattern name index */
    name = bg_name_list[i];

    /* Pattern cache base address */
    dst = &bg_pattern_cache[name << 6];

    /* Check modified lines */
    for(y = 0; y < 8; y ++)
    {
      if(bg_name_dirty[name] & (1 << y))
      {
        /* Byteplane data (one pattern = 4 bytes) */
        /* LIT_ENDIAN: byte0 (lsb) p2p3 p0p1 p6p7 p4p5 (msb) byte3 */
        /* BIG_ENDIAN: byte0 (msb) p0p1 p2p3 p4p5 p6p7 (lsb) byte3 */
        bp = *(uint32 *)&vram[(name << 5) | (y << 2)];

        /* Update cached line (8 pixels = 8 bytes) */
        for(x = 0; x < 8; x ++)
        {
          /* Extract pixel data */
          c = bp & 0x0F;

          /* Pattern cache data (one pattern = 8 bytes) */
          /* byte0 <-> p0 p1 p2 p3 p4 p5 p6 p7 <-> byte7 (hflip = 0) */
          /* byte0 <-> p7 p6 p5 p4 p3 p2 p1 p0 <-> byte7 (hflip = 1) */
#ifdef LSB_FIRST
          /* Byteplane data = (msb) p4p5 p6p7 p0p1 p2p3 (lsb) */
          dst[0x00000 | (y << 3) | (x ^ 3)] = (c);        /* vflip=0, hflip=0 */
          dst[0x20000 | (y << 3) | (x ^ 4)] = (c);        /* vflip=0, hflip=1 */
          dst[0x40000 | ((y ^ 7) << 3) | (x ^ 3)] = (c);  /* vflip=1, hflip=0 */
          dst[0x60000 | ((y ^ 7) << 3) | (x ^ 4)] = (c);  /* vflip=1, hflip=1 */
#else
          /* Byteplane data = (msb) p0p1 p2p3 p4p5 p6p7 (lsb) */
          dst[0x00000 | (y << 3) | (x ^ 7)] = (c);        /* vflip=0, hflip=0 */
          dst[0x20000 | (y << 3) | (x)] = (c);            /* vflip=0, hflip=1 */
          dst[0x40000 | ((y ^ 7) << 3) | (x ^ 7)] = (c);  /* vflip=1, hflip=0 */
          dst[0x60000 | ((y ^ 7) << 3) | (x)] = (c);      /* vflip=1, hflip=1 */
#endif
          /* Next pixel */
          bp = bp >> 4;
        }
      }
    }

    /* Clear modified pattern flag */
    bg_name_dirty[name] = 0;
  }
}


/*--------------------------------------------------------------------------*/
/* Window & Plane A clipping update function (Mode 5)                       */
/*--------------------------------------------------------------------------*/

void window_clip(unsigned int data, unsigned int sw)
{
  /* Window size and invert flags */
  int hp = (data & 0x1f);
  int hf = (data >> 7) & 1;

  /* Perform horizontal clipping; the results are applied in reverse
     if the horizontal inversion flag is set
   */
  int a = hf;
  int w = hf ^ 1;

  /* Display width (16 or 20 columns) */
  sw = 16 + (sw << 2);

  if(hp)
  {
    if(hp > sw)
    {
      /* Plane W takes up entire line */
      clip[w].left = 0;
      clip[w].right = sw;
      clip[w].enable = 1;
      clip[a].enable = 0;
    }
    else
    {
      /* Plane W takes left side, Plane A takes right side */
      clip[w].left = 0;
      clip[a].right = sw;
      clip[a].left = clip[w].right = hp;
      clip[0].enable = clip[1].enable = 1;
    }
  }
  else
  {
    /* Plane A takes up entire line */
    clip[a].left = 0;
    clip[a].right = sw;
    clip[a].enable = 1;
    clip[w].enable = 0;
  }
}


/*--------------------------------------------------------------------------*/
/* Init, reset routines                                                     */
/*--------------------------------------------------------------------------*/

void render_init(void)
{
  int bx, ax;

  /* Initialize layers priority pixel look-up tables */
  uint16 index;
  for (bx = 0; bx < 0x100; bx++)
  {
    for (ax = 0; ax < 0x100; ax++)
    {
      index = (bx << 8) | (ax);

      lut[0][index] = make_lut_bg(bx, ax);
      lut[1][index] = make_lut_bgobj(bx, ax);
      lut[2][index] = make_lut_bg_ste(bx, ax);
      lut[3][index] = make_lut_obj(bx, ax);
      lut[4][index] = make_lut_bgobj_ste(bx, ax);
      lut[5][index] = make_lut_bgobj_m4(bx,ax);
    }
  }

  /* Initialize pixel color look-up tables */
  palette_init();

  /* Make sprite pattern name index look-up table (Mode 5) */
  make_name_lut();

  /* Make bitplane to pixel look-up table (Mode 4) */
  make_bp_lut();
}

void render_reset(void)
{
  if (!reset_do_not_clear_buffers)
  {
    /* Clear display bitmap */
    memset(bitmap.data, 0, bitmap.pitch * bitmap.height);

    /* Clear line buffers */
    memset(linebuf, 0, sizeof(linebuf));

    /* Clear color palettes */
    memset(pixel, 0, sizeof(pixel));

    /* Clear pattern cache */
    memset((char *)bg_pattern_cache, 0, sizeof(bg_pattern_cache));
  }

  /* Reset Sprite infos */
  spr_ovr = spr_col = object_count[0] = object_count[1] = 0;
}


/*--------------------------------------------------------------------------*/
/* Line rendering functions                                                 */
/*--------------------------------------------------------------------------*/

AYTHER_HOT_INLINE void render_line_impl(int line, int ayther_observed)
{
#ifdef AYTHER_EXTENSIONS
  const int ayther_dim_active = ayther_observed && ayther_layer_dim;
  const int ayther_show_obj = !ayther_observed ||
    (ayther_layer_mask & AYTHER_LAYER_OBJ);
  /* #41: se decide una vez por línea. `ayther_attrib_capture` se apaga apenas
     termina render_bg —para que la recomposición no pise la atribución— así que
     la etapa de sprites necesita su propia copia del predicado. */
  const int ayther_attrib_capture_pending = ayther_observed &&
    AYTHER_ATTRIB_ACTIVE && bitmap.viewport.w <= 320 &&
    bitmap.viewport.h <= 240;
#else
  const int ayther_dim_active = 0;
  const int ayther_show_obj = 1;
  const int ayther_attrib_capture_pending = 0;
#endif
  /* Check display status */
  if (reg[1] & 0x40)
  {
    /* Update pattern cache */
    if (bg_list_index)
    {
      update_bg_pattern_cache(bg_list_index);
      bg_list_index = 0;
    }

#ifdef AYTHER_EXTENSIONS
    /* AYTHER: ocultar tile por celda (id 0x104) → "pela una capa" en el merge de
       render_bg, revelando el plano de atrás (ver ayther_peel_merge). Se activa
       sólo si esta línea tiene alguna celda marcada (descarte rápido: 8 bytes de
       la fila), en coords del frame que ve el frontend (+ viewport.x/y; 0 con
       overscan off, el caso normal de MD). Se apaga antes de los sprites para no
       pelar sus merges (un sprite sobre un tile oculto sigue visible). */
    ayther_peel_active = 0;
    if (ayther_observed)
    {
      /* AYTHER (#28): la fila de celda va en coordenadas del frame EMITIDO, y
         con salida entrelazada `remap_line` duplica la fila de salida
         (`line * 2 + odd_frame`). Sin replicar ese ajuste aca, la mascara caia
         en la mitad de la fila que el frontend habia marcado: ocultaba la celda
         equivocada, que es peor que no ocultar nada. En interlace mode 2 sin
         `config.render` la salida NO se dobla y `line` ya es la fila correcta. */
      int emitted = line + bitmap.viewport.y;
      if (interlaced && config.render)
        emitted = (emitted * 2) + odd_frame;
      {
      const int frow = emitted >> 3;
      if (frow >= 0 && frow < AYTHER_TILE_ROWS)
      {
        const uint8 *rb = &ayther_tile_suppress[(frow * AYTHER_TILE_COLS) >> 3];
        if (rb[0]|rb[1]|rb[2]|rb[3]|rb[4]|rb[5]|rb[6]|rb[7])
        {
          ayther_peel_active = 1;
          ayther_peel_row    = frow;
          ayther_peel_vx     = bitmap.viewport.x;
        }
      }
      }
    }
#endif

#ifdef AYTHER_EXTENSIONS
    /* AYTHER (#41): capturar la atribución de fondo dentro de este render_bg.
       El flag envuelve SÓLO esta llamada: la recomposición usa los mismos
       renderers y, si quedara encendido, una lectura pisaría la atribución del
       frame — el resultado dependería de si alguien miró. */
    /* Los stores globales van DENTRO del guard: en el perfil compilado-idle
       `pending` es 0 y el objetivo es que no quede ni una escritura de mas por
       linea. Escribir siempre costaba dos stores a globales por linea, y ademas
       le dice al compilador que esos globales pueden cambiar, lo cual le impide
       mantener cosas en registros dentro del renderer. */
    if (ayther_attrib_capture_pending)
    {
      ayther_attrib_capture = 1;
      ayther_attrib_row = line;
      /* Las dimensiones son las del frame emitido y se refrescan por línea: el
         viewport puede cambiar entre frames y el consumidor tiene que poder
         interpretar el buffer sin adivinarlas. */
      ayther_attrib_width  = (uint32)bitmap.viewport.w;
      ayther_attrib_height = (uint32)bitmap.viewport.h;
      ayther_attrib_flags  = (interlaced && config.render) ? 1u : 0u;
    }
#endif

    /* Render BG layer(s) */
    render_bg(line);

#ifdef AYTHER_EXTENSIONS
    /* Condicional por lo mismo: si nunca se encendio, no hace falta apagarlo. */
    if (ayther_attrib_capture_pending)
      ayther_attrib_capture = 0;
    /* AYTHER: el peel sólo aplica a los merges de BG, no a los de sprites. */
    ayther_peel_active = 0;

    /* Solo para el fallback de Mode 4; en Mode 5 el bit de sprite es exacto y
       este snapshot no se usa. */
    if ((ayther_dim_active || ayther_attrib_capture_pending) && !(reg[1] & 0x04))
      memcpy(ayther_bg_snap, linebuf[0], sizeof(ayther_bg_snap));

    /* Render sprite layer (AYTHER: ocultable vía máscara de capas, id 0x102).
       AYTHER dim (id 0x108): snapshot de linebuf[0] tras render_bg + diff tras
       render_obj → los píxeles que cambió render_obj son sprites (los demás son
       fondo, que remap_line atenúa). No toca las internas de render_obj. */
    if (ayther_dim_active || ayther_attrib_capture_pending)
    {
      /* #31/#37/#41: el bit de sprite lo escribe QUIEN DECIDE la prioridad --
         el bucle de render_obj en la familia sin S/H, el merge en la familia
         con S/H-. Antes salia de comparar linebuf[0] antes y despues, y esa
         via perdia los pixeles de sprite iguales al fondo y marcaba como
         sprite los operadores de brillo. */
      ayther_obj_px_exact = 0;
      if (ayther_show_obj)
      {
        ayther_obj_pass = 1;
        render_obj(line & 1);
        ayther_obj_pass = 0;
      }
      if (!ayther_obj_px_exact)
      {
        /* Mode 4 y TMS no pasan por ninguno de los dos: ahi sigue el diff, con
           su defecto conocido, hasta que #40 fase 2 meta esos modos en alcance.
           El snapshot se toma SOLO en esos modos, asi que fuera de ellos la
           respuesta correcta es "ningun pixel es de sprite" y no un diff contra
           un buffer que nadie lleno. */
        int i;
        if (ayther_show_obj && !(reg[1] & 0x04))
          for (i = 0; i < 0x200; i++)
            ayther_sprite_px[i] = (linebuf[0][i] != ayther_bg_snap[i]);
        else
          memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
      }
    }
    else if (ayther_show_obj)
      render_obj(line & 1);

#ifdef AYTHER_EXTENSIONS
    /* AYTHER (#41): volcar la fila al buffer del frame.
       El bit de sprite ya NO sale de un diff contra el fondo: lo escribe quien
       decide la prioridad. La limitación conocida —un píxel de sprite cuyo byte
       coincide con el del fondo quedaba sin marcar— queda cerrada, y de paso
       dejan de marcarse como sprite los operadores de shadow/highlight, que no
       lo son (#31 defecto 2). */
    if (ayther_attrib_capture_pending)
    {
      const uint32 w = (uint32)bitmap.viewport.w;
      const int row = ayther_attrib_row + bitmap.viewport.y;
      if (row >= 0 && (uint32)row < ayther_attrib_height && w <= 320)
      {
        uint8 *out = &ayther_attrib[(size_t)row * ayther_attrib_width];
        uint32 x;
        for (x = 0; x < w && x < ayther_attrib_width; ++x)
        {
          uint8 attr = ayther_attrib_line[x];
          if (ayther_sprite_px[0x20 + x])
            attr |= AYTHER_ATTRIB_SPRITE;
          out[x] = attr;
        }
      }
    }
#endif
#else
    render_obj(line & 1);
#endif

    /* Left-most column blanking */
    if (reg[0] & 0x20)
    {
      if (system_hw >= SYSTEM_MARKIII)
      {
        memset(&linebuf[0][0x20], 0x40, 8);
      }
    }

    /* Parse sprites for next line */
    if (line < (bitmap.viewport.h - 1))
    {
      parse_satb(line);
    }

    /* Horizontal borders */
    if (bitmap.viewport.x > 0)
    {
      memset(&linebuf[0][0x20 - bitmap.viewport.x], 0x40, bitmap.viewport.x);
      memset(&linebuf[0][0x20 + bitmap.viewport.w], 0x40, bitmap.viewport.x);
    }
  }
  else
  {
    /* Master System & Game Gear VDP specific */
    if (system_hw < SYSTEM_MD)
    {
      /* Update SOVR flag */
      status |= spr_ovr;
      spr_ovr = 0;

      /* Sprites are still parsed when display is disabled */
      parse_satb(line);
    }

    /* Blanked line */
    memset(&linebuf[0][0x20 - bitmap.viewport.x], 0x40, bitmap.viewport.w + 2*bitmap.viewport.x);

#ifdef AYTHER_EXTENSIONS
    /* AYTHER dim: línea en blanco → ningún pixel es sprite (todo se atenúa). */
    if (ayther_dim_active)
      memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
#endif
  }

  /* Pixel color remapping */
  remap_line(line);
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_NOINLINE void render_line_fast_path(int line)
{
  render_line_impl(line, 0);
}

static AYTHER_NOINLINE void render_line_observed_path(int line)
{
  render_line_impl(line, 1);
}
#endif

void render_line(int line)
{
#ifdef AYTHER_EXTENSIONS
  if (AYTHER_OBSERVED_ACTIVE)
    render_line_observed_path(line);
  else
    render_line_fast_path(line);
#else
  render_line_impl(line, 0);
#endif
}

void blank_line(int line, int offset, int width)
{
  memset(&linebuf[0][0x20 + offset], 0x40, width);
#ifdef AYTHER_EXTENSIONS
  /* AYTHER dim: línea en blanco → ningún pixel es sprite (todo se atenúa). */
  if (AYTHER_LAYER_DIM_ACTIVE)
    memset(ayther_sprite_px, 0, sizeof(ayther_sprite_px));
#endif
  remap_line(line);
}

void remap_line(int line)
{
  /* Line width */
  int width = bitmap.viewport.w + 2*bitmap.viewport.x;

  /* Pixel line buffer */
  uint8 *src = &linebuf[0][0x20 - bitmap.viewport.x];

  /* Adjust line offset in framebuffer */
  line = (line + bitmap.viewport.y) % lines_per_frame;

  /* Take care of Game Gear reduced screen when overscan is disabled */
  if (line < 0) return;

  /* Adjust for interlaced output */
  if (interlaced && config.render)
  {
    line = (line * 2) + odd_frame;
  }

#if defined(USE_15BPP_RENDERING) || defined(USE_16BPP_RENDERING)
  /* NTSC Filter (only supported for 15 or 16-bit pixels rendering) */
  if (config.ntsc)
  {
    if (reg[12] & 0x01)
    {
      md_ntsc_blit(md_ntsc, ( MD_NTSC_IN_T const * )pixel, src, width, line);
    }
    else
    {
      sms_ntsc_blit(sms_ntsc, ( SMS_NTSC_IN_T const * )pixel, src, width, line);
    }
  }
  else
#endif
  {
#ifdef CUSTOM_BLITTER
    CUSTOM_BLITTER(line, width, pixel, src)
#else
    /* Convert VDP pixel data to output pixel format */
    PIXEL_OUT_T *dst = ((PIXEL_OUT_T *)&bitmap.data[(line * bitmap.pitch)]);
    if (config.lcd)
    {
      do
      {
        RENDER_PIXEL_LCD(src,dst,pixel,config.lcd);
      }
      while (--width);
    }
#ifdef AYTHER_EXTENSIONS
    else if (AYTHER_LAYER_DIM_ACTIVE)
    {
      /* AYTHER dim (id 0x108): los píxeles que NO son sprite se emiten al 25%.
         `ayther_sprite_px` es paralelo a linebuf[0], con el mismo offset que src.

         #31: el cuarteo estaba escrito con máscaras RGB565 a mano. Ése es el
         formato del build del fork, pero no el único que el core compila: bajo
         USE_15BPP_RENDERING los canales viven en otros bits, y esas máscaras no
         daban "más oscuro" sino OTRO color. Ahora sale de AYTHER_DIM_QUARTER,
         que tiene una definición por formato. */
      uint8 *spx = &ayther_sprite_px[0x20 - bitmap.viewport.x];
      do
      {
        PIXEL_OUT_T p = pixel[*src++];
        *dst++ = *spx++ ? p : AYTHER_DIM_QUARTER(p);
      }
      while (--width);
    }
#endif
    else
    {
      do
      {
        *dst++ = pixel[*src++];
      }
      while (--width);
    }
 #endif
  }
}


/*--------------------------------------------------------------------------*/
/* AYTHER (#270): recomposición del frame desde el estado FINAL del VDP     */
/*--------------------------------------------------------------------------*/
/* Re-renderiza el frame recién emulado con ESTE MISMO renderer, pero con el
   estado del VDP congelado a fin de frame (VRAM/CRAM/VSRAM/regs/SAT como
   quedaron). La pantalla real se dibujó línea a línea con el estado VIGENTE
   en cada línea (efectos raster: scroll/CRAM/regs a media pantalla); la
   diferencia contra el framebuffer real ES la medida de cuánto pierde una
   recomposición single-state — el dato que decide la épica del render propio.

   `flags` permite además APAGAR comportamientos del VDP uno a uno para
   atribuirles su parte del error (¿cuánto cuesta NO modelar shadow/highlight?
   ¿el límite de sprites? ¿el window?): cada flag renderiza como si ese
   comportamiento no existiera.

   No perturba la emulación: todo estado mutado (status, spr_ovr/spr_col,
   obj_info/object_count, clip, regs tocados, bitmap.*, pixel[] si NO_SH) se
   salva y restaura. Sólo modo 5 no entrelazado, sin filtro NTSC, 16bpp.
   Devuelve 1 y escribe w*h píxeles RGB565 contiguos en `out` (cap = capacidad
   en píxeles); 0 si no aplica. */

/**
 * @brief Re-renders the emulated frame synchronously with the VDP's final state.
 *
 * This function MUST be executed EXCLUSIVELY and synchronously on the emulator's
 * main thread (core thread). It temporarily clobbers internal VDP globals
 * (like linebuf, obj_info, etc.) and restores them before returning.
 * It is safe to call immediately after a frame has finished emulating, and
 * guarantees that the VDP state is left completely identical to how it was found.
 */
#include "ayther/ayther_core.c"

