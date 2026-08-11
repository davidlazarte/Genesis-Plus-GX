/* Internal AYTHER compile gate and frame-boundary subscription state. */

#ifndef AYTHER_RUNTIME_H
#define AYTHER_RUNTIME_H

#include <stdint.h>

#include "ayther_api.h"

#ifdef AYTHER_EXTENSIONS

extern uint32_t ayther_subscription_active_mask;
extern uint32_t ayther_subscription_requested_mask;

uint32_t ayther_subscription_supported_mask(void);
void ayther_subscription_reset(void);
int ayther_subscription_request(uint32_t requested_mask);
int ayther_subscription_begin_frame(void);

#if defined(__GNUC__) || defined(__clang__)
/* Standard builds are idle by default. This hint keeps observed variants out
   of the fall-through instruction path while preserving runtime activation. */
#define AYTHER_SUBSCRIBED(mask) \
  __builtin_expect( \
    (ayther_subscription_active_mask & (uint32_t)(mask)) != 0, 0)
#else
#define AYTHER_SUBSCRIBED(mask) \
  ((ayther_subscription_active_mask & (uint32_t)(mask)) != 0)
#endif

#else

#define AYTHER_SUBSCRIBED(mask) 0

#endif

#endif /* AYTHER_RUNTIME_H */
