/* Dynamically load a core and validate the AYTHER ABI v1 contract. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ayther/ayther_api.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE library_t;
static library_t open_library(const char *path) { return LoadLibraryA(path); }
static void *load_symbol(library_t lib, const char *name)
{
  return (void *)(uintptr_t)GetProcAddress(lib, name);
}
static void close_library(library_t lib) { FreeLibrary(lib); }
static void print_load_error(const char *path)
{
  fprintf(stderr, "cannot load %s (Windows error %lu)\n", path,
          (unsigned long)GetLastError());
}
#else
#include <dlfcn.h>
typedef void *library_t;
static library_t open_library(const char *path)
{
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static void *load_symbol(library_t lib, const char *name)
{
  return dlsym(lib, name);
}
static void close_library(library_t lib) { dlclose(lib); }
static void print_load_error(const char *path)
{
  const char *error = dlerror();
  fprintf(stderr, "cannot load %s: %s\n", path, error ? error : "unknown");
}
#endif

typedef void *(*retro_get_memory_data_fn)(unsigned id);
typedef size_t (*retro_get_memory_size_fn)(unsigned id);

static int passed;
static int failed;

#define CHECK(condition, message) do { \
  if (condition) { ++passed; } \
  else { ++failed; fprintf(stderr, "FAIL: %s\n", message); } \
} while (0)

static int host_endianness(void)
{
  const uint16_t value = UINT16_C(0x0102);
  return (*(const uint8_t *)&value == UINT8_C(0x02))
      ? AYTHER_ENDIAN_LITTLE : AYTHER_ENDIAN_BIG;
}

int main(int argc, char **argv)
{
  library_t library;
  ayther_get_interface_fn get_interface;
  retro_get_memory_data_fn get_memory_data;
  retro_get_memory_size_fn get_memory_size;
  const ayther_interface_v1 *api;
  const ayther_interface_v1 *latest;
  ayther_frame_snapshot_v1 snapshot;
  uint64_t generation;
  uint64_t new_generation;
  uint64_t required_caps;
  uint8_t layer_mask;
  uint8_t invalid_layer_mask;
  uint8_t plane_bit;
  uint16_t invalid_audio_mute;
  ayther_audio_transport_stats_v1 audio_stats;
  ayther_subscription_state_v1 subscriptions;
  uint32_t audio_event_count;
  int has_legacy_audio_probe;
  int require_abi;
  int legacy_profile;
  uint32_t region_id;
  int argument;

  if (argc < 2 || argc > 4)
  {
    fprintf(stderr, "usage: %s LIBRARY [--require] [--legacy]\n", argv[0]);
    return 2;
  }
  require_abi = 0;
  legacy_profile = 0;
  for (argument = 2; argument < argc; ++argument)
  {
    if (strcmp(argv[argument], "--require") == 0)
      require_abi = 1;
    else if (strcmp(argv[argument], "--legacy") == 0)
      legacy_profile = 1;
    else
    {
      fprintf(stderr, "unknown option: %s\n", argv[argument]);
      return 2;
    }
  }

  library = open_library(argv[1]);
  if (!library)
  {
    print_load_error(argv[1]);
    return 1;
  }

  get_interface = (ayther_get_interface_fn)(uintptr_t)
      load_symbol(library, "ayther_get_interface");
  if (!get_interface)
  {
    printf("AYTHER ABI unavailable: stock or legacy core detected safely\n");
    close_library(library);
    return require_abi ? 1 : 0;
  }

  get_memory_data = (retro_get_memory_data_fn)(uintptr_t)
      load_symbol(library, "retro_get_memory_data");
  get_memory_size = (retro_get_memory_size_fn)(uintptr_t)
      load_symbol(library, "retro_get_memory_size");
  has_legacy_audio_probe =
      load_symbol(library, "audio_probe_poll") != NULL;
  CHECK(get_memory_data != NULL, "legacy data entry point is exported");
  CHECK(get_memory_size != NULL, "legacy size entry point is exported");
  if (!get_memory_data || !get_memory_size)
  {
    close_library(library);
    return 1;
  }

  latest = get_interface(0);
  api = get_interface(AYTHER_ABI_VERSION_1_0);
  CHECK(api != NULL, "explicit ABI v1 negotiation succeeds");
  CHECK(latest == api, "version zero discovers the latest interface");
  CHECK(get_interface(UINT32_C(0x00020000)) == NULL,
        "unsupported major version returns NULL");
  if (!api)
  {
    close_library(library);
    return 1;
  }

  required_caps = AYTHER_CAP_LEGACY_MEMORY | AYTHER_CAP_REGION_QUERY |
      AYTHER_CAP_REGION_READ | AYTHER_CAP_CONTROL_WRITE |
      AYTHER_CAP_FRAME_SNAPSHOT | AYTHER_CAP_PARSED_SPRITES_V1 |
      AYTHER_CAP_AUDIO_WRITES_V1 | AYTHER_CAP_RASTER_FALLBACK_V1 |
      AYTHER_CAP_RECOMPOSE_V1 | AYTHER_CAP_SUBSCRIPTIONS_V1;
  /* La regla es "mismo major, minor suficiente", no "version igual a la mia":
     un core que crece en minor le sigue sirviendo a este cliente porque los
     campos que conoce no se movieron y `struct_size` marca hasta donde leer.
     Comprobar igualdad exacta convertiria cada bump aditivo en una rotura
     ficticia. */
  CHECK(AYTHER_ABI_VERSION_MAJOR(api->abi_version) ==
        AYTHER_ABI_VERSION_MAJOR(AYTHER_ABI_VERSION_1_0),
        "descriptor reports the ABI major this client was built against");
  /* No se compara el minor contra el de 1.0 -que es 0, y daria una tautologia
     que gcc marca con -Wtype-limits-. Lo que hay que exigir es que el
     descriptor cubra los campos que este cliente conoce, y eso lo dice
     `struct_size`, no la version. */
  CHECK(api->struct_size >= offsetof(ayther_interface_v1, poll_frame_delta) +
        sizeof(api->poll_frame_delta),
        "the descriptor covers every field this client knows");
  /* Un cliente 1.0 pide 1.0 y tiene que recibir un descriptor usable, aunque el
     core ya sea 1.1: esto es el test de compatibilidad hacia atras. */
  CHECK(get_interface(AYTHER_ABI_VERSION_1_0) != NULL &&
        get_interface(AYTHER_ABI_VERSION_1_0)->query_region != NULL,
        "a 1.0 client still negotiates successfully against this core");
  CHECK(get_interface(UINT32_C(0x0001FFFF)) == NULL,
        "a minor newer than the core provides is refused");
  CHECK(api->struct_size >= offsetof(ayther_interface_v1,
        set_subscriptions) + sizeof(api->set_subscriptions),
        "descriptor exposes every v1 function");
  CHECK((api->capabilities & required_caps) == required_caps,
        "descriptor advertises every required v1 capability");
  CHECK(api->host_endianness == (uint32_t)host_endianness(),
        "descriptor reports host endianness");
  CHECK(api->pointer_size == sizeof(void *), "descriptor reports pointer size");
  CHECK(api->region_info_size == sizeof(ayther_region_info_v1),
        "region descriptor size matches the header");
  CHECK(api->frame_snapshot_size == sizeof(ayther_frame_snapshot_v1),
        "snapshot size matches the header");
  CHECK(api->sprite_size == 10, "sprite layout is negotiated as 10 bytes");
  CHECK(api->audio_write_size == 8,
        "audio write layout is negotiated as 8 bytes");
  CHECK(api->audio_event_size == sizeof(ayther_audio_event_v1),
        "audio event layout size is negotiated");
  CHECK(api->audio_transport_stats_size ==
        sizeof(ayther_audio_transport_stats_v1),
        "audio transport stats size is negotiated");
  CHECK(api->subscription_state_size == sizeof(ayther_subscription_state_v1),
        "subscription state size is negotiated");
  CHECK(api->build_id && api->build_id_size == strlen(api->build_id) &&
        api->build_id_size > 0, "build identifier has explicit length");
  CHECK(api->query_region && api->read_region && api->write_control &&
        api->capture_snapshot && api->recompose_frame &&
        api->poll_audio_events && api->get_audio_transport_stats &&
        api->get_subscriptions && api->set_subscriptions,
        "all v1 function pointers are present");
  CHECK(((api->capabilities & AYTHER_CAP_AUDIO_PROBE_V1) != 0) ==
        has_legacy_audio_probe,
        "audio probe capability follows the build-time feature gate");

  memset(&subscriptions, 0, sizeof(subscriptions));
  subscriptions.struct_size = sizeof(subscriptions);
  CHECK(api->get_subscriptions(&subscriptions, sizeof(subscriptions)) ==
        AYTHER_STATUS_OK, "subscription state can be queried");
  CHECK(subscriptions.struct_size == sizeof(subscriptions) &&
        subscriptions.state_version == 1,
        "subscription state is self-describing");
  CHECK(subscriptions.active_mask ==
        (legacy_profile ? subscriptions.supported_mask : 0) &&
        subscriptions.requested_mask == subscriptions.active_mask,
        "build profile exposes its documented initial subscription state");
  CHECK((subscriptions.supported_mask & AYTHER_SUB_ALL) ==
        (has_legacy_audio_probe ? AYTHER_SUB_ALL :
         (AYTHER_SUB_ALL & ~AYTHER_SUB_AUDIO_EVENTS)),
        "supported subscriptions follow compile-time features");
  CHECK(api->set_subscriptions(UINT32_C(0x80000000)) ==
        AYTHER_STATUS_INVALID_ARGUMENT,
        "unknown subscription bits are rejected");
  {
    uint32_t initial_active = subscriptions.active_mask;
    uint32_t pending_mask = legacy_profile ? 0 : subscriptions.supported_mask;
  CHECK(api->set_subscriptions(pending_mask) ==
        AYTHER_STATUS_OK, "supported subscriptions can be requested");
  CHECK(api->get_subscriptions(&subscriptions, sizeof(subscriptions)) ==
        AYTHER_STATUS_OK && subscriptions.active_mask == initial_active &&
        subscriptions.requested_mask == pending_mask,
        "requested subscriptions remain pending until a frame boundary");
  }

  audio_event_count = UINT32_C(0xFFFFFFFF);
  CHECK(api->poll_audio_events(NULL, 0, &audio_event_count) ==
        (has_legacy_audio_probe ?
         (legacy_profile ? AYTHER_STATUS_OK : AYTHER_STATUS_NOT_SUBSCRIBED) :
         AYTHER_STATUS_UNSUPPORTED),
        "audio event polling distinguishes availability from subscription");
  CHECK(audio_event_count == 0,
        "zero-capacity audio polling returns no events");
  memset(&audio_stats, 0xA5, sizeof(audio_stats));
  CHECK(api->get_audio_transport_stats(&audio_stats, sizeof(audio_stats)) ==
        (has_legacy_audio_probe ? AYTHER_STATUS_OK : AYTHER_STATUS_UNSUPPORTED),
        "audio transport stats report feature availability");
  CHECK(audio_stats.struct_size == sizeof(audio_stats) &&
        audio_stats.transport_version == 1 &&
        audio_stats.event_size == sizeof(ayther_audio_event_v1),
        "audio transport stats are self-describing");

  for (region_id = 1; region_id < AYTHER_REGION_COUNT; ++region_id)
  {
    ayther_region_info_v1 info;
    int32_t status = api->query_region(region_id, &info, sizeof(info));
    CHECK(status == AYTHER_STATUS_OK, "v1 region can be queried");
    if (status != AYTHER_STATUS_OK)
      continue;
    CHECK(info.struct_size == sizeof(info), "region descriptor size is explicit");
    CHECK(info.region_id == region_id, "region descriptor echoes its ID");
    CHECK(info.element_size > 0 &&
          info.byte_size == info.element_size * info.capacity,
          "region dimensions are self-consistent");
    /* #41: una region de frame puede estar VACIA antes del primer frame -sus
       dimensiones son las del frame emitido, que todavia no existe-. Exigir
       capacity > 0 para todas era una regla de las regiones de v1, que son de
       tamano fijo. */
    if (!(info.access_flags & AYTHER_REGION_FRAME_SCOPED))
      CHECK(info.capacity > 0, "a fixed-size region is never empty");

    /* Sin id legacy no hay nada que comparar contra el adaptador viejo, y es lo
       correcto para lo que se agregue de ahora en mas: los punteros mutables
       legacy estan deprecados y no se le da uno a cada region nueva. */
    if (info.legacy_memory_id == AYTHER_LEGACY_MEMORY_NONE)
    {
      ++passed;   /* region moderna: no participa del adaptador legacy */
      continue;
    }
    if (get_memory_size(info.legacy_memory_id) != info.byte_size)
      fprintf(stderr, "region %lu: ABI size=%lu legacy 0x%lX size=%lu\n",
              (unsigned long)region_id, (unsigned long)info.byte_size,
              (unsigned long)info.legacy_memory_id,
              (unsigned long)get_memory_size(info.legacy_memory_id));
    CHECK(get_memory_size(info.legacy_memory_id) == info.byte_size,
          "legacy region size remains compatible");
    CHECK(get_memory_data(info.legacy_memory_id) != NULL,
          "legacy region pointer remains available");
  }

  CHECK(api->query_region(AYTHER_REGION_COUNT, NULL, 0) ==
        AYTHER_STATUS_INVALID_ARGUMENT, "NULL query output is rejected");
  CHECK(api->capture_snapshot(&snapshot, sizeof(snapshot) - 1) ==
        AYTHER_STATUS_BUFFER_TOO_SMALL, "short snapshot buffers are rejected");
  CHECK(api->capture_snapshot(&snapshot, sizeof(snapshot)) == AYTHER_STATUS_OK,
        "frame snapshot can be captured before content is loaded");
  CHECK(snapshot.struct_size == sizeof(snapshot) &&
        snapshot.snapshot_version == 1, "snapshot header is versioned");
  generation = snapshot.snapshot_generation;

  CHECK(api->read_region(AYTHER_REGION_LAYER_MASK, 0, &layer_mask,
        sizeof(layer_mask), generation, NULL) == AYTHER_STATUS_OK,
        "control state is read against a snapshot generation");
  layer_mask = UINT8_C(0x0F);
  CHECK(api->write_control(AYTHER_REGION_LAYER_MASK, 0, &layer_mask,
        sizeof(layer_mask), generation, &new_generation) == AYTHER_STATUS_OK,
        "validated layer control write succeeds");
  CHECK(new_generation != generation,
        "successful control write advances snapshot generation");
  CHECK(*(const uint8_t *)get_memory_data(AYTHER_LEGACY_MEMORY_LAYER_MASK) ==
        UINT8_C(0x0F), "controlled write is visible through the legacy adapter");
  CHECK(api->read_region(AYTHER_REGION_LAYER_MASK, 0, &layer_mask,
        sizeof(layer_mask), generation, NULL) ==
        AYTHER_STATUS_STALE_GENERATION,
        "stale snapshot generation is rejected predictably");

  invalid_layer_mask = UINT8_C(0xF0);
  CHECK(api->write_control(AYTHER_REGION_LAYER_MASK, 0, &invalid_layer_mask,
        sizeof(invalid_layer_mask), new_generation, NULL) ==
        AYTHER_STATUS_INVALID_ARGUMENT, "invalid layer bits are rejected");
  CHECK(api->write_control(AYTHER_REGION_CRAM, 0, &layer_mask,
        sizeof(layer_mask), new_generation, NULL) == AYTHER_STATUS_READ_ONLY,
        "emulated memory is read-only through the public ABI");
  CHECK(api->write_control(AYTHER_REGION_SPRITE_SUPPRESS, 0, NULL, 0,
        new_generation, &generation) == AYTHER_STATUS_OK &&
        generation == new_generation,
        "zero-length control write is a generation-preserving no-op");
  plane_bit = 1;
  CHECK(api->write_control(AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, &plane_bit,
        1, new_generation, &generation) == AYTHER_STATUS_OK,
        "partial plane suppression write succeeds");
  CHECK(*(const uint8_t *)get_memory_data(
        AYTHER_LEGACY_MEMORY_PLANE_SUPPRESS_ACTIVE) == 1,
        "plane suppression activity is derived after a controlled write");
  plane_bit = 0;
  CHECK(api->write_control(AYTHER_REGION_PLANE_TILE_SUPPRESS, 0, &plane_bit,
        1, generation, &new_generation) == AYTHER_STATUS_OK,
        "plane suppression bit can be cleared");
  CHECK(*(const uint8_t *)get_memory_data(
        AYTHER_LEGACY_MEMORY_PLANE_SUPPRESS_ACTIVE) == 0,
        "derived plane suppression activity clears with the bitmap");
  invalid_audio_mute = UINT16_C(0x8000);
  CHECK(api->write_control(AYTHER_REGION_AUDIO_MUTE, 0, &invalid_audio_mute,
        sizeof(invalid_audio_mute), new_generation, NULL) ==
        AYTHER_STATUS_INVALID_ARGUMENT, "invalid audio mute bits are rejected");
  CHECK(api->read_region(AYTHER_REGION_VDP_REGS, UINT32_C(0x20), &layer_mask,
        1, AYTHER_GENERATION_ANY, NULL) == AYTHER_STATUS_OUT_OF_BOUNDS,
        "out-of-bounds reads are rejected");
  CHECK(api->recompose_frame(NULL, 0, 0, NULL, NULL) ==
        AYTHER_STATUS_INVALID_ARGUMENT,
        "recomposition validates output dimensions before use");

  /* ABI 1.1 (#26). Se consulta por `struct_size` y no por la version: es la
     comprobacion que un cliente real tiene que hacer, asi que el verificador la
     ejerce igual que la haria el frontend. */
  if (AYTHER_IFACE_HAS(api, get_recompose_stats))
  {
    ayther_recompose_stats_v1 stats;
    CHECK((api->capabilities & AYTHER_CAP_RECOMPOSE_STATS_V1) != 0,
          "a core exposing recompose stats advertises the capability");
    CHECK(api->recompose_stats_size == sizeof(ayther_recompose_stats_v1),
          "recompose stats size matches the header");
    memset(&stats, 0, sizeof(stats));
    CHECK(api->get_recompose_stats(&stats, sizeof(stats)) ==
          AYTHER_STATUS_OK && stats.struct_size == sizeof(stats),
          "recompose stats are readable and self-describing");
    CHECK(api->get_recompose_stats(NULL, sizeof(stats)) ==
          AYTHER_STATUS_INVALID_ARGUMENT,
          "recompose stats reject a null destination");
    CHECK(api->get_recompose_stats(&stats, 1) ==
          AYTHER_STATUS_BUFFER_TOO_SMALL,
          "recompose stats reject an undersized destination");
  }

  /* ABI 1.2 (#32): la recomposicion multicapa se negocia por el descriptor. El
     simbolo suelto era la unica superficie AYTHER fuera de
     `ayther_get_interface`, y por eso el closure de exports decia una cosa en
     Windows y otra en Linux. */
  if (AYTHER_IFACE_HAS(api, recompose_multilayer))
  {
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(api->recompose_multilayer != NULL,
          "the descriptor exposes multilayer recomposition");
    CHECK(api->recompose_multilayer(NULL, NULL, NULL, NULL, NULL, 0, 0,
          NULL, NULL) == AYTHER_STATUS_INVALID_ARGUMENT,
          "multilayer recomposition validates its output dimensions");
    CHECK(api->recompose_multilayer(NULL, NULL, NULL, NULL, NULL,
          UINT32_MAX, 0, &width, &height) == AYTHER_STATUS_OUT_OF_BOUNDS,
          "multilayer recomposition rejects an out-of-range capacity");
  }

  /* #32: la VRAM se entrega word-swapped en hosts little-endian y hasta ahora
     eso solo vivia en la documentacion en prosa. Un consumidor que lee el
     descriptor tiene que poder enterarse por el descriptor. */
  {
    ayther_region_info_v1 vram_info;
    memset(&vram_info, 0, sizeof(vram_info));
    if (api->query_region(AYTHER_REGION_VRAM, &vram_info,
          sizeof(vram_info)) == AYTHER_STATUS_OK)
    {
      CHECK(vram_info.byte_size == UINT32_C(0x10000),
            "VRAM is exposed as 64 KiB");
      if (api->host_endianness == AYTHER_ENDIAN_LITTLE)
        CHECK((vram_info.access_flags & AYTHER_REGION_WORD_SWAPPED_LE) != 0,
              "VRAM declares its word-swapped layout on little-endian hosts");
    }
  }

  printf("AYTHER ABI dynamic tests: %d passed, %d failed; build=%.*s\n",
         passed, failed, (int)api->build_id_size, api->build_id);
  close_library(library);
  return failed ? 1 : 0;
}
