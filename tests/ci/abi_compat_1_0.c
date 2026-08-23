/* Un cliente ABI 1.0 contra el core actual (#33).
 *
 * `test_ayther_api.c` congela los offsets del header ACTUAL, lo cual detecta un
 * reordenamiento pero no responde la pregunta que importa: un frontend ya
 * compilado, que no conoce 1.1 ni 1.2, ¿sigue funcionando? Eso solo se contesta
 * describiendo el contrato viejo por separado y usándolo.
 *
 * Por eso este archivo NO incluye ayther_api.h: declara el prefijo 1.0 con sus
 * propios tipos, como lo haría un consumidor de entonces. Si alguien reordena
 * el descriptor o cambia un tipo, este test falla aunque el header actual sea
 * internamente consistente — que es exactamente el punto ciego que deja
 * congelar offsets contra uno mismo.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE library_t;
#define OPEN_LIB(p)   LoadLibraryA(p)
#define SYM(l, n)     ((void *)(uintptr_t)GetProcAddress((l), (n)))
#define CLOSE_LIB(l)  FreeLibrary(l)
#define AYTHER_CALL_V1 __cdecl
#else
#include <dlfcn.h>
typedef void *library_t;
#define OPEN_LIB(p)   dlopen((p), RTLD_NOW)
#define SYM(l, n)     dlsym((l), (n))
#define CLOSE_LIB(l)  dlclose(l)
#define AYTHER_CALL_V1
#endif

static int passed, failed;
#define CHECK(cond, msg) do { \
  if (cond) { ++passed; } \
  else { ++failed; fprintf(stderr, "FAIL: %s\n", msg); } \
} while (0)

/* --- el contrato tal como existía en 1.0, transcrito a mano --- */

#define V1_0_VERSION UINT32_C(0x00010000)

#define V1_0_STATUS_OK 0
#define V1_0_STATUS_NOT_SUBSCRIBED (-9)
#define V1_0_GENERATION_ANY UINT64_MAX
#define V1_0_REGION_VRAM 1
#define V1_0_REGION_CRAM 2

#define V1_0_CAP_REGION_QUERY  (UINT64_C(1) << 1)
#define V1_0_CAP_REGION_READ   (UINT64_C(1) << 2)
#define V1_0_CAP_FRAME_SNAPSHOT (UINT64_C(1) << 4)
/* Un cliente 1.0 ya conocia las suscripciones: leer VRAM exige pedirlas. */
#define V1_0_SUB_ALL UINT32_C(0x7F)

typedef struct {
  uint32_t struct_size;
  uint32_t region_id;
  uint32_t data_version;
  uint32_t element_size;
  uint32_t capacity;
  uint32_t byte_size;
  uint32_t access_flags;
  uint32_t legacy_memory_id;
} v1_0_region_info;

typedef struct {
  uint32_t struct_size;
  uint32_t snapshot_version;
  uint64_t snapshot_generation;
  uint64_t frame_generation;
  uint32_t flags;
  uint32_t parsed_sprite_count;
  uint32_t audio_write_count;
  uint32_t overflow_flags;
  uint32_t fallback_reasons;
  uint32_t reserved0;
} v1_0_frame_snapshot;

typedef struct v1_0_interface {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t capabilities;
  uint32_t host_endianness;
  uint32_t pointer_size;
  uint32_t region_info_size;
  uint32_t frame_snapshot_size;
  uint32_t sprite_size;
  uint32_t audio_write_size;
  const char *build_id;
  uint32_t build_id_size;
  uint32_t reserved0;
  int32_t (AYTHER_CALL_V1 *query_region)(uint32_t, v1_0_region_info *, uint32_t);
  int32_t (AYTHER_CALL_V1 *read_region)(uint32_t, uint32_t, void *, uint32_t,
                                        uint64_t, uint64_t *);
  int32_t (AYTHER_CALL_V1 *write_control)(uint32_t, uint32_t, const void *,
                                          uint32_t, uint64_t, uint64_t *);
  int32_t (AYTHER_CALL_V1 *capture_snapshot)(v1_0_frame_snapshot *, uint32_t);
  int32_t (AYTHER_CALL_V1 *recompose_frame)(uint16_t *, uint32_t, uint32_t,
                                            uint32_t *, uint32_t *);
  uint32_t audio_event_size;
  uint32_t audio_transport_stats_size;
  void *poll_audio_events;
  void *get_audio_transport_stats;
  uint32_t subscription_state_size;
  uint32_t reserved1;
  void *get_subscriptions;
  int32_t (AYTHER_CALL_V1 *set_subscriptions)(uint32_t);
  uint32_t frame_delta_size;
  void *poll_frame_delta;
} v1_0_interface;

typedef const v1_0_interface *(AYTHER_CALL_V1 *v1_0_get_interface)(uint32_t);

int main(int argc, char **argv)
{
  library_t library;
  v1_0_get_interface get_interface;
  const v1_0_interface *api;
  v1_0_region_info info;
  static uint8_t vram[0x10000];

  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <core>\n", argv[0]);
    return 2;
  }

  library = OPEN_LIB(argv[1]);
  if (!library)
  {
    fprintf(stderr, "cannot load %s\n", argv[1]);
    return 1;
  }

  get_interface = (v1_0_get_interface)SYM(library, "ayther_get_interface");
  CHECK(get_interface != NULL, "a 1.0 client finds the discovery symbol");
  if (!get_interface) { CLOSE_LIB(library); return 1; }

  /* Pide 1.0 sin saber que existe algo mas nuevo. */
  api = get_interface(V1_0_VERSION);
  CHECK(api != NULL, "a 1.0 client negotiates successfully");
  if (!api) { CLOSE_LIB(library); return 1; }

  /* La regla del cliente viejo: mismo major, y `struct_size` cubre lo que
     conoce. Nunca "abi_version == la mia". */
  CHECK((api->abi_version >> 16) == 1,
        "the core reports ABI major 1");
  CHECK(api->struct_size >= sizeof(v1_0_interface),
        "the descriptor covers everything a 1.0 client knows");
  CHECK(api->pointer_size == sizeof(void *), "pointer size matches");
  CHECK(api->region_info_size == sizeof(v1_0_region_info),
        "the 1.0 region descriptor did not change size");
  CHECK(api->frame_snapshot_size == sizeof(v1_0_frame_snapshot),
        "the 1.0 snapshot did not change size");
  CHECK((api->capabilities &
         (V1_0_CAP_REGION_QUERY | V1_0_CAP_REGION_READ |
          V1_0_CAP_FRAME_SNAPSHOT)) ==
        (V1_0_CAP_REGION_QUERY | V1_0_CAP_REGION_READ |
         V1_0_CAP_FRAME_SNAPSHOT),
        "the 1.0 capabilities are still advertised");

  /* Y sobre todo: que las funciones que conoce sigan estando DONDE las conoce.
     Un reordenamiento del descriptor pasa desapercibido en un test que congela
     offsets contra el header actual, y acá llamaría al puntero equivocado.
     El flujo es el real de un consumidor: negociar, suscribir, leer. */
  CHECK(api->set_subscriptions &&
        api->set_subscriptions(V1_0_SUB_ALL) == V1_0_STATUS_OK,
        "a 1.0 client can still request every subscription it knew");

  memset(&info, 0, sizeof(info));
  CHECK(api->query_region &&
        api->query_region(V1_0_REGION_VRAM, &info, sizeof(info)) ==
          V1_0_STATUS_OK &&
        info.byte_size == UINT32_C(0x10000),
        "query_region still describes VRAM as 64 KiB");
  /* Sin correr un frame la suscripcion queda PEDIDA pero no activa -es el
     protocolo de activacion en el limite de frame, que 1.0 ya definia-, asi que
     lo correcto de afirmar aca no es que copie, sino que siga distinguiendo
     "no suscripto" de "no soportado". Ese distingo es contrato 1.0: un cliente
     que reciba UNSUPPORTED apaga la funcion para siempre, y con NOT_SUBSCRIBED
     vuelve a intentar despues del proximo frame. La copia efectiva la cubre
     full_core_replay, que si tiene ROM y corre frames. */
  CHECK(api->read_region != NULL, "read_region is still where 1.0 left it");
  {
    int32_t status = api->read_region(V1_0_REGION_VRAM, 0, vram, sizeof(vram),
                                      V1_0_GENERATION_ANY, NULL);
    CHECK(status == V1_0_STATUS_NOT_SUBSCRIBED || status == V1_0_STATUS_OK,
          "read_region reports not-subscribed rather than unsupported");
  }
  memset(&info, 0, sizeof(info));
  CHECK(api->query_region(V1_0_REGION_CRAM, &info, sizeof(info)) ==
          V1_0_STATUS_OK && info.byte_size == 0x80,
        "query_region still describes CRAM as 128 bytes");

  printf("ABI 1.0 compatibility: %d passed, %d failed; core reports %u.%u\n",
         passed, failed, api->abi_version >> 16,
         api->abi_version & 0xFFFFu);
  CLOSE_LIB(library);
  return failed ? 1 : 0;
}
