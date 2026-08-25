/* Runtime subscription state shared by the core and libretro adapter. */

#include "ayther_runtime.h"
#include "ayther_metrics.h"

#ifdef AYTHER_METRICS
/* `= {0}`, y no un inicializador campo por campo: `ayther_metrics_read`
   escribe struct_size desde sizeof en cada lectura, asi que enumerarlos aca
   no aporta nada y hacia que agregar un contador rompiera el build con
   -Wmissing-field-initializers. */
ayther_metrics_v1 ayther_metrics = {0};
#endif

#ifdef SOUND_PROBE
#include "audio_probe.h"
#endif

#ifdef AYTHER_LEGACY_PROFILE
#ifdef SOUND_PROBE
#define AYTHER_INITIAL_SUBSCRIPTIONS AYTHER_SUB_ALL
#else
#define AYTHER_INITIAL_SUBSCRIPTIONS \
  (AYTHER_SUB_ALL & ~AYTHER_SUB_AUDIO_EVENTS)
#endif
#else
#define AYTHER_INITIAL_SUBSCRIPTIONS 0
#endif

/* Keep the profile contract valid before retro_init() as well as after every
   content transition. Some native hosts negotiate the ABI immediately after
   loading the shared library. */
uint32_t ayther_subscription_active_mask = AYTHER_INITIAL_SUBSCRIPTIONS;
uint32_t ayther_subscription_requested_mask = AYTHER_INITIAL_SUBSCRIPTIONS;

uint32_t ayther_subscription_supported_mask(void)
{
  uint32_t mask = AYTHER_SUB_ALL;
#ifndef SOUND_PROBE
  mask &= ~AYTHER_SUB_AUDIO_EVENTS;
#endif
  return mask;
}

void ayther_subscription_reset(void)
{
  ayther_subscription_active_mask = AYTHER_INITIAL_SUBSCRIPTIONS;
  ayther_subscription_requested_mask = ayther_subscription_active_mask;
#ifdef SOUND_PROBE
  audio_probe_set_enabled(
      AYTHER_SUBSCRIBED(AYTHER_SUB_AUDIO_EVENTS));
#endif
}

int ayther_subscription_request(uint32_t requested_mask)
{
  if (requested_mask & ~ayther_subscription_supported_mask())
    return 0;
  ayther_subscription_requested_mask = requested_mask;
  return 1;
}

int ayther_subscription_begin_frame(void)
{
  int changed =
      ayther_subscription_active_mask != ayther_subscription_requested_mask;
  ayther_subscription_active_mask = ayther_subscription_requested_mask;
#ifdef SOUND_PROBE
  audio_probe_set_enabled(
      AYTHER_SUBSCRIBED(AYTHER_SUB_AUDIO_EVENTS));
#endif
  return changed;
}
