/* Concurrent SPSC and callback-reconfiguration stress tests for audio_probe. */

#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

#include "shared.h"
#include "audio_probe_atomic.h"

ROMINFO rominfo;
uint8 vdp_pal;
uint8 system_hw;
uint32 system_clock;
uint16 lines_per_frame;
void psg_refresh_gain(void) { }

#define SPSC_EVENT_COUNT UINT32_C(2000000)
#define CALLBACK_EVENT_COUNT UINT32_C(1000000)

static int passed;
static int failed;
static ap_atomic_u32 callback_null_errors;

#define CHECK(condition, message) do { \
  if (condition) { ++passed; } \
  else { ++failed; fprintf(stderr, "FAIL: %s\n", message); } \
} while (0)

typedef struct producer_args
{
  uint32 count;
  ap_atomic_u32 *start;
} producer_args;

typedef struct callback_state
{
  uint32 magic;
  ap_atomic_u32 calls;
  ap_atomic_u32 errors;
} callback_state;

static void yield_consumer(void);

static void atomic_increment(ap_atomic_u32 *value)
{
  unsigned int current = ap_atomic_load_relaxed(value);
  while (!ap_atomic_compare_exchange_relaxed(value, &current, current + 1))
  {
  }
}

static void produce_events(producer_args *args)
{
  uint32 i;
  while (args->start && !ap_atomic_load_acquire(args->start))
    yield_consumer();
  for (i = 0; i < args->count; ++i)
    audio_probe_fm_raw(i, i ^ UINT32_C(0xA5A55A5A));
}

#if defined(_WIN32)
typedef HANDLE test_thread;

static DWORD WINAPI producer_thread(LPVOID opaque)
{
  produce_events((producer_args *)opaque);
  return 0;
}

static int start_thread(test_thread *thread, producer_args *args)
{
  *thread = CreateThread(NULL, 0, producer_thread, args, 0, NULL);
  return *thread != NULL;
}

static void join_thread(test_thread thread)
{
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
}

static void delay_consumer(void) { Sleep(10); }
static void yield_consumer(void) { Sleep(0); }
#else
typedef pthread_t test_thread;

static void *producer_thread(void *opaque)
{
  produce_events((producer_args *)opaque);
  return NULL;
}

static int start_thread(test_thread *thread, producer_args *args)
{
  return pthread_create(thread, NULL, producer_thread, args) == 0;
}

static void join_thread(test_thread thread)
{
  pthread_join(thread, NULL);
}

static void delay_consumer(void)
{
  struct timespec duration;
  duration.tv_sec = 0;
  duration.tv_nsec = 10000000;
  nanosleep(&duration, NULL);
}

static void yield_consumer(void) { sched_yield(); }
#endif

static void callback_a(const ap_event_t *event, void *user)
{
  callback_state *state = (callback_state *)user;
  if (!state)
  {
    atomic_increment(&callback_null_errors);
    return;
  }
  if (state->magic != UINT32_C(0xA11CE001) ||
      event->data != (event->reg ^ UINT32_C(0xA5A55A5A)))
    atomic_increment(&state->errors);
  atomic_increment(&state->calls);
}

static void callback_b(const ap_event_t *event, void *user)
{
  callback_state *state = (callback_state *)user;
  if (!state)
  {
    atomic_increment(&callback_null_errors);
    return;
  }
  if (state->magic != UINT32_C(0xB11CE002) ||
      event->data != (event->reg ^ UINT32_C(0xA5A55A5A)))
    atomic_increment(&state->errors);
  atomic_increment(&state->calls);
}

static void test_spsc_transport(void)
{
  producer_args args;
  test_thread producer;
  ap_event_t events[31];
  ap_transport_stats_t stats;
  uint32 received = 0;
  uint32 previous = 0;
  int have_previous = 0;
  int content_errors = 0;

  args.count = SPSC_EVENT_COUNT;
  args.start = NULL;
  audio_probe_reset();
  audio_probe_set_callback(NULL, NULL);
  audio_probe_reset_transport_stats();
  if (!start_thread(&producer, &args))
  {
    CHECK(0, "SPSC producer thread starts");
    return;
  }
  CHECK(1, "SPSC producer thread starts");
  delay_consumer(); /* deterministically force wrap-around and overflow */

  for (;;)
  {
    int i;
    int count = audio_probe_poll(events,
        (int)(sizeof(events) / sizeof(events[0])));
    for (i = 0; i < count; ++i)
    {
      if (events[i].type != AP_EV_RAW_WRITE ||
          events[i].schema != AUDIO_PROBE_SCHEMA ||
          events[i].data !=
              (events[i].reg ^ UINT32_C(0xA5A55A5A)) ||
          (have_previous && events[i].reg <= previous))
        ++content_errors;
      previous = events[i].reg;
      have_previous = 1;
    }
    received += (uint32)count;
    audio_probe_get_transport_stats(&stats);
    if (received + stats.dropped_events == SPSC_EVENT_COUNT)
      break;
    if (count == 0)
      yield_consumer();
  }

  join_thread(producer);
  audio_probe_get_transport_stats(&stats);
  CHECK(content_errors == 0,
        "consumer never observes partial, corrupt, or reordered events");
  CHECK(received + stats.dropped_events == SPSC_EVENT_COUNT,
        "every produced event is either consumed or reported dropped");
  CHECK(stats.pending == 0, "stress test drains the SPSC ring");
  CHECK(stats.dropped_events > 0, "forced overflow is observable");
  CHECK(stats.high_water_mark == stats.capacity,
        "stress reaches and reports effective capacity");
  CHECK(stats.capacity == AUDIO_PROBE_RING_SIZE - 1,
        "stress uses the negotiated effective capacity");
}

static void test_callback_reconfiguration(void)
{
  producer_args args;
  test_thread producer;
  callback_state a;
  callback_state b;
  ap_event_t discard[64];
  ap_atomic_u32 start;
  unsigned int i;

  args.count = CALLBACK_EVENT_COUNT;
  args.start = &start;
  a.magic = UINT32_C(0xA11CE001);
  b.magic = UINT32_C(0xB11CE002);
  ap_atomic_store_relaxed(&a.calls, 0);
  ap_atomic_store_relaxed(&a.errors, 0);
  ap_atomic_store_relaxed(&b.calls, 0);
  ap_atomic_store_relaxed(&b.errors, 0);
  ap_atomic_store_relaxed(&callback_null_errors, 0);
  ap_atomic_store_relaxed(&start, 0);

  audio_probe_reset_transport_stats();
  audio_probe_set_callback(callback_a, &a);
  if (!start_thread(&producer, &args))
  {
    CHECK(0, "callback producer thread starts");
    audio_probe_set_callback(NULL, NULL);
    return;
  }
  CHECK(1, "callback producer thread starts");
  ap_atomic_store_release(&start, 1);
  for (i = 0; i < 100000 &&
       ap_atomic_load_acquire(&a.calls) == 0; ++i)
    yield_consumer();
  for (i = 0; i < 20000; ++i)
  {
    if ((i % 3) == 0)
      audio_probe_set_callback(callback_a, &a);
    else if ((i % 3) == 1)
      audio_probe_set_callback(callback_b, &b);
    else
      audio_probe_set_callback(NULL, NULL);
  }
  audio_probe_set_callback(NULL, NULL);
  join_thread(producer);

  while (audio_probe_poll(discard,
         (int)(sizeof(discard) / sizeof(discard[0]))) != 0)
  {
  }

  CHECK(ap_atomic_load_acquire(&a.calls) +
        ap_atomic_load_acquire(&b.calls) > 0,
        "callbacks run while registration changes concurrently");
  CHECK(ap_atomic_load_acquire(&a.errors) == 0 &&
        ap_atomic_load_acquire(&b.errors) == 0 &&
        ap_atomic_load_acquire(&callback_null_errors) == 0,
        "callback and user-data pairs remain coherent");
}

int main(void)
{
  test_spsc_transport();
  test_callback_reconfiguration();
  printf("audio_probe concurrency tests: %d passed, %d failed; events=%lu\n",
         passed, failed,
         (unsigned long)(SPSC_EVENT_COUNT + CALLBACK_EVENT_COUNT));
  return failed ? 1 : 0;
}
