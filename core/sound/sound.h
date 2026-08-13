/***************************************************************************************
 *  Genesis Plus
 *  Sound Hardware
 *
 *  Copyright (C) 1998-2003  Charles Mac Donald (original code)
 *  Copyright (C) 2007-2020  Eke-Eke (Genesis Plus GX)
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

#ifndef _SOUND_H_
#define _SOUND_H_

#ifdef AYTHER_EXTENSIONS
#include "../ayther/ayther_api.h"
#include "../ayther/ayther_runtime.h"
#endif

/* Function prototypes */
extern void sound_init(void);
extern void sound_reset(void);
extern int sound_context_save(uint8 *state);
extern int sound_context_load(uint8 *state);
extern int sound_update(unsigned int cycles);
extern void (*fm_reset)(unsigned int cycles);
extern void (*fm_write)(unsigned int cycles, unsigned int address, unsigned int data);
extern unsigned int (*fm_read)(unsigned int cycles, unsigned int address);
extern void save_sound_buffer();
extern void restore_sound_buffer();

/* ----------------------------------------------------------------------------
 * AYTHER fork delta: log de escrituras crudas a los chips de sonido (YM2612 FM +
 * SN76489 PSG), en orden temporal dentro del frame. El frontend lo lee por los
 * ids de memoria 0x109 (array) / 0x10A (contador, ESCRIBIBLE = reset) para
 * identificar eventos de audio por la SECUENCIA DE COMANDOS al chip — estable a
 * través del replay (la CPU/VDP son byte-deterministas) — en lugar de hashear el
 * PCM de salida, que NO es reproducible tras unserialize (la fase del FM diverge).
 * El core queda "tonto": sólo registra (cycle, addr, data, chip); toda la
 * interpretación (estado de canal, key-on/off, firma) vive en el host. La ABI
 * v1 hace que el core reinicie contadores/overflow por frame; la escritura de
 * reset por IDs legacy sigue aceptada durante la transición. sound.c.
 * -------------------------------------------------------------------------- */
#ifdef AYTHER_EXTENSIONS

#define AYTHER_AUDIO_CHIP_FM   0   /* YM2612 (FM)    */
#define AYTHER_AUDIO_CHIP_PSG  1   /* SN76489 (PSG)  */
#define AYTHER_AUDIO_WRITE_CAP 8192

typedef ayther_audio_write_v1 AytherAudioWrite;

extern AytherAudioWrite ayther_audio_writes[AYTHER_AUDIO_WRITE_CAP];
extern uint32 ayther_audio_write_n;
extern uint32 ayther_audio_write_overflow;


/* ----------------------------------------------------------------------------
 * AYTHER fork delta: máscara de SILENCIADO POR CANAL (4 bytes, ESCRIBIBLE desde
 * el frontend vía el id de memoria 0x10D):
 *
 *   bits  0-5   canales FM  (YM2612)   0-5
 *   bits  6-9   canales PSG (SN76489)  0-3
 *   bits 10-17  canales PCM (RF5C164 de Sega CD) 0-7
 *   bits 18-31  libres
 *
 * Bit set = ese canal se pone a CERO en el mixer de salida, SIN tocar el estado
 * de los registros del chip (el análogo de audio al sprite_suppress 0x103). Como
 * sólo afecta el PCM emitido y no el estado interno, es replay-safe: el chip
 * evoluciona idéntico aunque el mute esté puesto. Es el primitivo de la
 * sustitución por evento (C-A3): mutear los canales de un evento mientras suena
 * su asset HD.
 *
 * Eran 2 bytes hasta 2026-08-13. El chip PCM de Sega CD agrega ocho canales que
 * no entraban, y dejarlo afuera lo habría empujado al OTRO camino de silencio
 * que existe en este core —`audio_probe_set_channel_gain`, que sí lo soporta—.
 * Tener la decisión de mute escrita en dos lugares es exactamente el defecto que
 * el frontend corrigió en su issue #329: la voz del chip callaba por un camino y
 * el reemplazo seguía sonando por el otro. Un solo mecanismo, más ancho.
 * -------------------------------------------------------------------------- */
extern uint32 ayther_audio_mute;
#define AYTHER_FM_MUTED(ch) \
  (AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) && \
   (ayther_audio_mute & (1u << (ch))))
#define AYTHER_PSG_MUTED(ch) \
  (AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) && \
   (ayther_audio_mute & (1u << (6 + (ch)))))
#define AYTHER_PCM_MUTED(ch) \
  (AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS) && \
   (ayther_audio_mute & (1u << (10 + (ch)))))

#else


#define AYTHER_FM_MUTED(ch) 0
#define AYTHER_PSG_MUTED(ch) 0

#endif /* AYTHER_EXTENSIONS */

#endif /* _SOUND_H_ */
