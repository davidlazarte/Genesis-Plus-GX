/***************************************************************************************
 *  Genesis Plus
 *  PSG sound chip (SN76489A compatible)
 *
 *  Support for discrete chip & integrated (ASIC) clones
 *
 *  Noise implementation based on http://www.smspower.org/Development/SN76489#NoiseChannel
 *
 *  Copyright (C) 2016-2017 Eke-Eke (Genesis Plus GX)
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
#include "blip_buf.h"

extern int8 audio_hard_disable;

/* internal clock = input clock : 16 = (master clock : 15) : 16 */
#define PSG_MCYCLES_RATIO (15*16)

/* maximal channel output (roughly adjusted to match VA4 MD1 PSG/FM balance with 1.5x amplification of PSG output) */
#define PSG_MAX_VOLUME 2800

static const uint8 noiseShiftWidth[2] = {14,15};

static const uint8 noiseBitMask[2] = {0x6,0x9};

static const uint8 noiseFeedback[10] = {0,1,1,0,1,0,0,1,1,0};

static const uint16 chanVolume[16] = {
  PSG_MAX_VOLUME,               /*  MAX  */
  PSG_MAX_VOLUME * 0.794328234, /* -2dB  */
  PSG_MAX_VOLUME * 0.630957344, /* -4dB  */
  PSG_MAX_VOLUME * 0.501187233, /* -6dB  */
  PSG_MAX_VOLUME * 0.398107170, /* -8dB  */
  PSG_MAX_VOLUME * 0.316227766, /* -10dB */
  PSG_MAX_VOLUME * 0.251188643, /* -12dB */
  PSG_MAX_VOLUME * 0.199526231, /* -14dB */
  PSG_MAX_VOLUME * 0.158489319, /* -16dB */
  PSG_MAX_VOLUME * 0.125892541, /* -18dB */
  PSG_MAX_VOLUME * 0.1,         /* -20dB */
  PSG_MAX_VOLUME * 0.079432823, /* -22dB */
  PSG_MAX_VOLUME * 0.063095734, /* -24dB */
  PSG_MAX_VOLUME * 0.050118723, /* -26dB */
  PSG_MAX_VOLUME * 0.039810717, /* -28dB */
  0                             /*  OFF  */
};

static struct
{
  int clocks;
  int latch;
  int zeroFreqInc;
  int noiseShiftValue;
  int noiseShiftWidth;
  int noiseBitMask;
  int regs[8];
  int freqInc[4];
  int freqCounter[4];
  int polarity[4];
  int chanDelta[4][2];
  int chanOut[4][2];
  int chanAmp[4][2];
#ifdef SOUND_PROBE
  unsigned int preamp;   /* last preamp value  (for gain refresh) */
  unsigned int panning;  /* last panning value (for gain refresh) */
#endif
} psg;

/* AYTHER fork delta: estado de mute aplicado por canal (para detectar el flanco
   y corregir el nivel corriente sin dejar offset DC). NO se serializa: un
   savestate guarda siempre el audio SIN mutear (el mute es output-only), así que
   psg_context_load lo resetea a 0 — si no, tras un unserialize (replay/scrub) el
   shadow queda desincronizado y el mute no corrige el nivel (residual DC). */
#ifdef AYTHER_EXTENSIONS
static uint8 psg_ayther_muted[4] = {0};
#endif

static void psg_update(unsigned int clocks);

void psg_init(PSG_TYPE type)
{
  int i;

  /* Initialize stereo amplification (default) */
  for (i=0; i<4; i++)
  {
    psg.chanAmp[i][0] = 100;
    psg.chanAmp[i][1] = 100; 
  }

  /* Initialize Tone zero frequency increment value */
  psg.zeroFreqInc = ((type == PSG_DISCRETE) ? 0x400 : 0x1) * PSG_MCYCLES_RATIO;

  /* Initialize Noise LSFR type */
  psg.noiseShiftWidth = noiseShiftWidth[type];
  psg.noiseBitMask = noiseBitMask[type];
}

void psg_reset(void)
{
  int i;

  /* power-on state (verified on 315-5313A & 315-5660 integrated version only) */
  for (i=0; i<4; i++)
  {
    psg.regs[i*2]       = 0;
    psg.regs[i*2+1]     = 0;
    psg.freqInc[i]      = (i < 3) ? (psg.zeroFreqInc) : (16 * PSG_MCYCLES_RATIO);
    psg.freqCounter[i]  = 0;
    psg.polarity[i]     = -1;
    psg.chanDelta[i][0] = 0;
    psg.chanDelta[i][1] = 0;
    psg.chanOut[i][0]   = 0;
    psg.chanOut[i][1]   = 0;
  }

  /* tone #2 attenuation register is latched on power-on (verified on 315-5313A integrated version only) */
  psg.latch = 3;

  /* reset noise shift register */
  psg.noiseShiftValue = 1 << psg.noiseShiftWidth;

  /* reset internal M-cycles clock counter */
  psg.clocks = 0;

#ifdef SOUND_PROBE
  psg.preamp  = 0;
  psg.panning = 0;
#endif
}

int psg_context_save(uint8 *state)
{
  int bufferptr = 0;

  save_param(&psg.clocks,sizeof(psg.clocks));
  save_param(&psg.latch,sizeof(psg.latch));
  save_param(&psg.noiseShiftValue,sizeof(psg.noiseShiftValue));
  save_param(psg.regs,sizeof(psg.regs));
  save_param(psg.freqInc,sizeof(psg.freqInc));
  save_param(psg.freqCounter,sizeof(psg.freqCounter));
  save_param(psg.polarity,sizeof(psg.polarity));
  save_param(psg.chanOut,sizeof(psg.chanOut));

  return bufferptr;
}

int psg_context_load(uint8 *state)
{
  int delta[2];
  int i, bufferptr = 0;

  /* initialize delta with current noise channel output (AYTHER: si el canal
     estaba muteado su contribución efectiva era 0 → no la quitamos) */
  if ((psg.noiseShiftValue & 1)
#ifdef AYTHER_EXTENSIONS
      && !psg_ayther_muted[3]
#endif
     )
  {
    delta[0] = -psg.chanOut[3][0];
    delta[1] = -psg.chanOut[3][1];
  }
  else
  {
    delta[0] = 0;
    delta[1] = 0;
  }

  /* add current tone channels output (AYTHER: idem, muted-aware) */
  for (i=0; i<3; i++)
  {
    if ((psg.polarity[i] > 0)
#ifdef AYTHER_EXTENSIONS
        && !psg_ayther_muted[i]
#endif
       )
    {
      delta[0] -= psg.chanOut[i][0];
      delta[1] -= psg.chanOut[i][1];
    }
  }

  load_param(&psg.clocks,sizeof(psg.clocks));
  load_param(&psg.latch,sizeof(psg.latch));
  load_param(&psg.noiseShiftValue,sizeof(psg.noiseShiftValue));
  load_param(psg.regs,sizeof(psg.regs));
  load_param(psg.freqInc,sizeof(psg.freqInc));
  load_param(psg.freqCounter,sizeof(psg.freqCounter));
  load_param(psg.polarity,sizeof(psg.polarity));
  load_param(psg.chanOut,sizeof(psg.chanOut));

  /* AYTHER fork delta: el savestate guarda el audio SIN mutear (el mute es
     output-only, no toca el estado del chip), así que el estado restaurado está
     "desmuteado". Reseteamos el shadow a 0 → la próxima máscara del frontend
     re-dispara la corrección de nivel. Sin esto, tras un replay/scrub el mute no
     baja la energía (residual DC). El new-output de abajo usa chanOut pleno (OK). */
#ifdef AYTHER_EXTENSIONS
  for (i=0; i<4; i++) psg_ayther_muted[i] = 0;
#endif

  /* add noise channel output variation */
  if (psg.noiseShiftValue & 1)
  {
    delta[0] += psg.chanOut[3][0];
    delta[1] += psg.chanOut[3][1];
  }

  /* add tone channels output variation */
  for (i=0; i<3; i++)
  {
    if (psg.polarity[i] > 0)
    {
      delta[0] += psg.chanOut[i][0];
      delta[1] += psg.chanOut[i][1];
    }
  }

  /* update mixed channels output */
  if (config.hq_psg)
  {
    blip_add_delta(snd.blips[0], psg.clocks, delta[0], delta[1]);
  }
  else
  {
    blip_add_delta_fast(snd.blips[0], psg.clocks, delta[0], delta[1]);
  }

  return bufferptr;
}

void psg_write(unsigned int clocks, unsigned int data)
{
  int index;

  /* AYTHER fork delta: registrar la escritura cruda al PSG (byte ORIGINAL, antes
     de que el switch de abajo recalcule `data`). El protocolo de latch (bit 7 =
     latch de registro, si no dato al registro latcheado) lo replica el host. */
  /* El log lo produce audio_probe.c, no esta ruta: por eso este hook quedo
     vacio. Consecuencia todavia abierta (#29): con SOUND_PROBE=0 la region
     AUDIO_WRITES existe y siempre viene vacia, aunque la capability se
     anuncie igual. */

  /* PSG chip synchronization */
  if (clocks > psg.clocks)
  {
    /* run PSG chip until current timestamp */
    psg_update(clocks);

    /* update internal M-cycles clock counter */
    psg.clocks += ((clocks - psg.clocks + PSG_MCYCLES_RATIO - 1) / PSG_MCYCLES_RATIO) * PSG_MCYCLES_RATIO;
  }

#ifdef SOUND_PROBE
  /* report the raw PSG bus write (timestamped) */
  audio_probe_psg_raw(clocks, data);
#endif

  if (data & 0x80)
  {
    /* latch register index (1xxx----) */
    psg.latch = index = (data >> 4) & 0x07;
  }
  else
  {
    /* restore latched register index */
    index= psg.latch;
  }

  switch (index)
  {
    case 0:
    case 2:
    case 4: /* Tone channels frequency */
    {
      /* recalculate frequency register value */
      if (data & 0x80)
      {
        /* update 10-bit register LSB (1---xxxx)  */
        data = (psg.regs[index] & 0x3f0) | (data & 0x0f);
      }
      else
      {
        /* update 10-bit register MSB (0-xxxxxx)  */
        data = (psg.regs[index] & 0x00f) | ((data & 0x3f) << 4);
      }

      /* update channel M-cycle counter increment */
      if (data)
      {
        psg.freqInc[index>>1] = data * PSG_MCYCLES_RATIO;
      }
      else
      {
        /* zero value behaves the same as a value of 1 on integrated version (0x400 on discrete version) */
        psg.freqInc[index>>1] = psg.zeroFreqInc;
      }

      /* update noise channel counter increment if required */
      if ((index == 4) && ((psg.regs[6] & 0x03) == 0x03))
      {
        psg.freqInc[3] = psg.freqInc[2];
      }

      break;
    }

    case 6: /* Noise control */
    {
      /* noise signal generator frequency (-----?xx) */
      int noiseFreq = (data & 0x03);

      if (noiseFreq == 0x03)
      {
        /* noise generator is controlled by tone channel #3 generator */
        psg.freqInc[3] = psg.freqInc[2];
        psg.freqCounter[3] = psg.freqCounter[2];
      }
      else
      {
        /* noise generator is running at separate frequency */
        psg.freqInc[3] = (0x10 << noiseFreq) * PSG_MCYCLES_RATIO;
      }

      /* check current noise shift register output */
      if (psg.noiseShiftValue & 1)
      {
        /* high to low transition will be applied at next internal cycle update */
        psg.chanDelta[3][0] -= psg.chanOut[3][0];
        psg.chanDelta[3][1] -= psg.chanOut[3][1];
      }

      /* reset noise shift register value (noise channel output is forced low) */
      psg.noiseShiftValue = 1 << psg.noiseShiftWidth;;

      break;
    }

    case 7: /* Noise channel attenuation */
    {
      int chanOut[2];

      /* convert 4-bit attenuation value (----xxxx) to 16-bit volume value */
      data = chanVolume[data & 0x0f];

      /* channel pre-amplification */
      chanOut[0] = (data * psg.chanAmp[3][0]) / 100;
      chanOut[1] = (data * psg.chanAmp[3][1]) / 100;

      /* check noise shift register output (AYTHER: si está muteado, no generar
         delta audible — el nivel real se guarda abajo y se restaura al desmutear) */
      if ((psg.noiseShiftValue & 1) && !AYTHER_PSG_MUTED(3))
      {
        /* channel output is high, volume variation will be applied at next internal cycle update */
        psg.chanDelta[3][0] += (chanOut[0] - psg.chanOut[3][0]);
        psg.chanDelta[3][1] += (chanOut[1] - psg.chanOut[3][1]);
      }

      /* update channel volume */
      psg.chanOut[3][0] = chanOut[0];
      psg.chanOut[3][1] = chanOut[1];

      break;
    }

    default: /* Tone channels attenuation */
    {
      int chanOut[2];

      /* channel number (0-2) */
      int i = index >> 1;

      /* convert 4-bit attenuation value (----xxxx) to 16-bit volume value */
      data = chanVolume[data & 0x0f];

      /* channel pre-amplification */
      chanOut[0] = (data * psg.chanAmp[i][0]) / 100;
      chanOut[1] = (data * psg.chanAmp[i][1]) / 100;

      /* check tone generator polarity (AYTHER: si está muteado, no generar delta
         audible — el nivel real se guarda abajo y se restaura al desmutear) */
      if ((psg.polarity[i] > 0) && !AYTHER_PSG_MUTED(i))
      {
        /* channel output is high, volume variation will be applied at next internal cycle update */
        psg.chanDelta[i][0] += (chanOut[0] - psg.chanOut[i][0]);
        psg.chanDelta[i][1] += (chanOut[1] - psg.chanOut[i][1]);
      }

      /* update channel volume */
      psg.chanOut[i][0] = chanOut[0];
      psg.chanOut[i][1] = chanOut[1];

      break;
    }
  }

  /* save register value */
  psg.regs[index] = data;
}

void psg_config(unsigned int clocks, unsigned int preamp, unsigned int panning)
{
  int i;

#ifdef SOUND_PROBE
  /* remember settings so audio_probe gain changes can be re-applied live */
  psg.preamp  = preamp;
  psg.panning = panning;
#endif

  /* PSG chip synchronization */
  if (clocks > psg.clocks)
  {
    /* run PSG chip until current timestamp */
    psg_update(clocks);

    /* update internal M-cycles clock counter */
    psg.clocks += ((clocks - psg.clocks + PSG_MCYCLES_RATIO - 1) / PSG_MCYCLES_RATIO) * PSG_MCYCLES_RATIO;
  }

  for (i=0; i<4; i++)
  {
    /* channel internal volume */
    int volume = psg.regs[i*2+1];
    
    /* update channel stereo amplification */
    #ifndef USE_PER_SOUND_CHANNELS_CONFIG
        psg.chanAmp[i][0] = preamp * ((panning >> (i + 4)) & 1);
        psg.chanAmp[i][1] = preamp * ((panning >> (i + 0)) & 1); 
    #else
        /* also apply user-set volume scaling */
        psg.chanAmp[i][0] = ((preamp * config.psg_ch_volumes[i]) / 100) * ((panning >> (i + 4)) & 1);
        psg.chanAmp[i][1] = ((preamp * config.psg_ch_volumes[i]) / 100) * ((panning >> (i + 0)) & 1);
    #endif

#ifdef SOUND_PROBE
    /* audio_probe per-channel gain (for HQ audio substitution) */
    if (AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS))
    {
      int pg = audio_probe_get_channel_gain(AP_SRC_PSG, i);
      if (pg < 100)
      {
        psg.chanAmp[i][0] = (psg.chanAmp[i][0] * pg) / 100;
        psg.chanAmp[i][1] = (psg.chanAmp[i][1] * pg) / 100;
      }
    }
#endif

    /* tone channels */
    if (i < 3)
    {
      /* check tone generator polarity */
      if (psg.polarity[i] > 0)
      {
        /* channel output is high, volume variation will be applied at next internal cycle update */
        psg.chanDelta[i][0] += (((volume * psg.chanAmp[i][0]) / 100) - psg.chanOut[i][0]);
        psg.chanDelta[i][1] += (((volume * psg.chanAmp[i][1]) / 100) - psg.chanOut[i][1]);
      }
    }
    
    /* noise channel */
    else
    {
      /* check noise shift register output */
      if (psg.noiseShiftValue & 1)
      {
        /* channel output is high, volume variation will be applied at next internal cycle update */
        psg.chanDelta[3][0] += (((volume * psg.chanAmp[3][0]) / 100) - psg.chanOut[3][0]);
        psg.chanDelta[3][1] += (((volume * psg.chanAmp[3][1]) / 100) - psg.chanOut[3][1]);
      }
    }

    /* update channel volume */
    psg.chanOut[i][0] = (volume * psg.chanAmp[i][0]) / 100;
    psg.chanOut[i][1] = (volume * psg.chanAmp[i][1]) / 100;
  }
}

#ifdef SOUND_PROBE
/* Re-apply channel amplification using the last preamp/panning so an
   audio_probe gain change takes effect immediately (propagated through the
   same delta path psg_config uses). */
void psg_refresh_gain(void)
{
  psg_config(psg.clocks, psg.preamp, psg.panning);
}
#endif

void psg_end_frame(unsigned int clocks)
{
  int i;

  if (clocks > psg.clocks)
  {
    /* run PSG chip until current timestamp */
    psg_update(clocks);

    /* update internal M-cycles clock counter */
    psg.clocks += ((clocks - psg.clocks + PSG_MCYCLES_RATIO - 1) / PSG_MCYCLES_RATIO) * PSG_MCYCLES_RATIO;
  }

  /* adjust internal M-cycles clock counter for next frame */
  psg.clocks -= clocks;

  /* adjust channels time counters for next frame */
  for (i=0; i<4; ++i)
  {
    psg.freqCounter[i] -= clocks;
  }
}

#if defined(AYTHER_EXTENSIONS) && (defined(__GNUC__) || defined(__clang__))
#define AYTHER_PSG_INLINE static inline __attribute__((always_inline))
#define AYTHER_PSG_NOINLINE __attribute__((noinline))
#else
#define AYTHER_PSG_INLINE INLINE
#define AYTHER_PSG_NOINLINE
#endif

AYTHER_PSG_INLINE void psg_update_impl(unsigned int clocks,
                                      int ayther_audio_controls)
{
  int i, timestamp, polarity;
#ifndef AYTHER_EXTENSIONS
  (void)ayther_audio_controls;
#endif
  if (audio_hard_disable) return;

  for (i=0; i<4; i++)
  {
#ifdef AYTHER_EXTENSIONS
    /* AYTHER: mute por canal a nivel de salida. eo0/eo1 = salida efectiva (0 si
       muteado). Al CAMBIAR el mute, corrige el nivel corriente vía chanDelta —
       como un cambio de volumen a/desde silencio — para no dejar DC ni click. */
    int aeth_mute = ayther_audio_controls &&
      (ayther_audio_mute & (1u << (6 + i))) ? 1 : 0;
    if (aeth_mute != psg_ayther_muted[i])
    {
      int high = (i < 3) ? (psg.polarity[i] > 0) : (psg.noiseShiftValue & 1);
      if (high)
      {
        int f = aeth_mute ? -1 : 1;   /* mute: -chanOut (a 0); unmute: +chanOut */
        psg.chanDelta[i][0] += f * psg.chanOut[i][0];
        psg.chanDelta[i][1] += f * psg.chanOut[i][1];
      }
      psg_ayther_muted[i] = aeth_mute;
    }
    int eo0 = aeth_mute ? 0 : psg.chanOut[i][0];
    int eo1 = aeth_mute ? 0 : psg.chanOut[i][1];
#else
    int eo0 = psg.chanOut[i][0];
    int eo1 = psg.chanOut[i][1];
#endif

    /* apply any pending channel volume variations */
    if (psg.chanDelta[i][0] | psg.chanDelta[i][1])
    {
      /* update channel output */
      if (config.hq_psg)
      {
        blip_add_delta(snd.blips[0], psg.clocks, psg.chanDelta[i][0], psg.chanDelta[i][1]);
      }
      else
      {
        blip_add_delta_fast(snd.blips[0], psg.clocks, psg.chanDelta[i][0], psg.chanDelta[i][1]);
      }

      /* clear pending channel volume variations */
      psg.chanDelta[i][0] = 0;
      psg.chanDelta[i][1] = 0;
    }

    /* timestamp of next transition */
    timestamp = psg.freqCounter[i];

    /* current channel generator polarity */
    polarity = psg.polarity[i];

    /* Tone channels */
    if (i < 3)
    {
      /* process all transitions occurring until current clock timestamp */
      while (timestamp < clocks)
      {
        /* invert tone generator polarity */
        polarity = -polarity;

        /* update channel output (eo0/eo1 = 0 si el canal está muteado) */
        if (config.hq_psg)
        {
          blip_add_delta(snd.blips[0], timestamp, polarity*eo0, polarity*eo1);
        }
        else
        {
          blip_add_delta_fast(snd.blips[0], timestamp, polarity*eo0, polarity*eo1);
        }

        /* timestamp of next transition */
        timestamp += psg.freqInc[i];
      }
    }

    /* Noise channel */
    else
    {
      /* current noise shift register value */
      int shiftValue = psg.noiseShiftValue;

      /* process all transitions occurring until current clock timestamp */
      while (timestamp < clocks)
      {
        /* invert noise generator polarity */
        polarity = -polarity;

        /* noise register is shifted on positive edge only */
        if (polarity > 0)
        {
          /* current shift register output */
          int shiftOutput = shiftValue & 0x01;

          /* White noise (-----1xx) */
          if (psg.regs[6] & 0x04)
          {
            /* shift and apply XOR feedback network */
            shiftValue = (shiftValue >> 1) | (noiseFeedback[shiftValue & psg.noiseBitMask] << psg.noiseShiftWidth);
          }

          /* Periodic noise (-----0xx) */
          else
          {
            /* shift and feedback current output */
            shiftValue = (shiftValue >> 1) | (shiftOutput << psg.noiseShiftWidth);
          }

          /* shift register output variation */
          shiftOutput = (shiftValue & 0x1) - shiftOutput;

          /* update noise channel output (eo0/eo1 = 0 si el canal está muteado) */
          if (config.hq_psg)
          {
            blip_add_delta(snd.blips[0], timestamp, shiftOutput*eo0, shiftOutput*eo1);
          }
          else
          {
            blip_add_delta_fast(snd.blips[0], timestamp, shiftOutput*eo0, shiftOutput*eo1);
          }
        }

        /* timestamp of next transition */
        timestamp += psg.freqInc[3];
      }

      /* save shift register value */
      psg.noiseShiftValue = shiftValue;
    }

    /* save timestamp of next transition */
    psg.freqCounter[i] = timestamp;

    /* save channel generator polarity */
    psg.polarity[i] = polarity;
  }
}

#ifdef AYTHER_EXTENSIONS
static AYTHER_PSG_NOINLINE void psg_update_fast_path(unsigned int clocks)
{
  psg_update_impl(clocks, 0);
}

static AYTHER_PSG_NOINLINE void psg_update_observed_path(unsigned int clocks)
{
  psg_update_impl(clocks, 1);
}
#endif

static void psg_update(unsigned int clocks)
{
#ifdef AYTHER_EXTENSIONS
  if (AYTHER_SUBSCRIBED(AYTHER_SUB_RENDER_CONTROLS))
    psg_update_observed_path(clocks);
  else
    psg_update_fast_path(clocks);
#else
  psg_update_impl(clocks, 0);
#endif
}

#ifdef SOUND_PROBE
/* Snapshot the resolved voice of PSG channel 'ch' (0-3): tone period in
   block_fnum, attenuation (0-15, 15=off) in op_tl[0], and the noise control
   nibble (channel 3) in algorithm. */
void PSG_GetVoice(int ch, ap_voice_t *out)
{
  if (!out) return;
  memset(out, 0, sizeof(*out));
  if (ch < 0 || ch > 3) return;
  out->op_tl[0]   = (unsigned char)(psg.regs[ch * 2 + 1] & 0x0f);
  out->block_fnum = (unsigned int)(psg.regs[ch * 2] & 0x3ff);
  if (ch == 3)
    out->algorithm = (unsigned char)(psg.regs[6] & 0x07);
}
#endif /* SOUND_PROBE */
