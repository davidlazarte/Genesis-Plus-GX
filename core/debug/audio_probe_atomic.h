/***************************************************************************************
 *  Internal portable 32-bit atomics for audio_probe.
 *
 *  GCC and Clang use their lock-free __atomic builtins. Native MSVC uses the
 *  Interlocked intrinsics, whose full barriers are stronger than the acquire /
 *  release operations required by the SPSC transport.
 ****************************************************************************************/

#ifndef AUDIO_PROBE_ATOMIC_H
#define AUDIO_PROBE_ATOMIC_H

#if defined(_MSC_VER) && !defined(__clang__)
#define AP_ATOMIC_INLINE static __inline
#else
#define AP_ATOMIC_INLINE static inline
#endif

#if defined(_MSC_VER) && !defined(__clang__)

#include <intrin.h>

#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)

typedef volatile long ap_atomic_u32;

AP_ATOMIC_INLINE unsigned int ap_atomic_load_relaxed(const ap_atomic_u32 *value)
{
  return (unsigned int)_InterlockedCompareExchange(
      (volatile long *)value, 0, 0);
}

AP_ATOMIC_INLINE unsigned int ap_atomic_load_acquire(const ap_atomic_u32 *value)
{
  return ap_atomic_load_relaxed(value);
}

AP_ATOMIC_INLINE void ap_atomic_store_relaxed(ap_atomic_u32 *value,
                                               unsigned int desired)
{
  _InterlockedExchange(value, (long)desired);
}

AP_ATOMIC_INLINE void ap_atomic_store_release(ap_atomic_u32 *value,
                                               unsigned int desired)
{
  ap_atomic_store_relaxed(value, desired);
}

AP_ATOMIC_INLINE int ap_atomic_compare_exchange_acquire(
    ap_atomic_u32 *value, unsigned int *expected, unsigned int desired)
{
  long observed = _InterlockedCompareExchange(
      value, (long)desired, (long)*expected);
  if ((unsigned int)observed == *expected)
    return 1;
  *expected = (unsigned int)observed;
  return 0;
}

AP_ATOMIC_INLINE int ap_atomic_compare_exchange_relaxed(
    ap_atomic_u32 *value, unsigned int *expected, unsigned int desired)
{
  return ap_atomic_compare_exchange_acquire(value, expected, desired);
}

#else

typedef unsigned int ap_atomic_u32;

AP_ATOMIC_INLINE unsigned int ap_atomic_load_relaxed(const ap_atomic_u32 *value)
{
  return __atomic_load_n(value, __ATOMIC_RELAXED);
}

AP_ATOMIC_INLINE unsigned int ap_atomic_load_acquire(const ap_atomic_u32 *value)
{
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

AP_ATOMIC_INLINE void ap_atomic_store_relaxed(ap_atomic_u32 *value,
                                               unsigned int desired)
{
  __atomic_store_n(value, desired, __ATOMIC_RELAXED);
}

AP_ATOMIC_INLINE void ap_atomic_store_release(ap_atomic_u32 *value,
                                               unsigned int desired)
{
  __atomic_store_n(value, desired, __ATOMIC_RELEASE);
}

AP_ATOMIC_INLINE int ap_atomic_compare_exchange_acquire(
    ap_atomic_u32 *value, unsigned int *expected, unsigned int desired)
{
  return __atomic_compare_exchange_n(value, expected, desired, 0,
      __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

AP_ATOMIC_INLINE int ap_atomic_compare_exchange_relaxed(
    ap_atomic_u32 *value, unsigned int *expected, unsigned int desired)
{
  return __atomic_compare_exchange_n(value, expected, desired, 0,
      __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

#endif

#undef AP_ATOMIC_INLINE

#endif /* AUDIO_PROBE_ATOMIC_H */
