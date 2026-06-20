/* Minimal stand-in for the emulator's shared.h, used ONLY to unit-test
   core/debug/audio_probe.c in isolation (no full emulator build).

   audio_probe.c includes "shared.h"; with -Itests/stub on the include path
   this file is picked up instead of core/shared.h. It declares just the few
   globals audio_probe.c references (in audio_probe_get_context) plus the
   psg_refresh_gain() hook. The test binary provides their definitions. */

#ifndef _SHARED_H_
#define _SHARED_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;

/* subset of ROMINFO sufficient for audio_probe_get_context() */
typedef struct {
  unsigned short checksum;
  unsigned short realchecksum;
} ROMINFO;

extern ROMINFO rominfo;
extern uint8   vdp_pal;
extern uint8   system_hw;
extern uint32  system_clock;
extern uint16  lines_per_frame;

#define MCYCLES_PER_LINE 3420

#ifdef SOUND_PROBE
#include "audio_probe.h"
extern void psg_refresh_gain(void);
#endif

#endif /* _SHARED_H_ */
