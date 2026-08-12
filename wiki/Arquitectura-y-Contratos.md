# Arquitectura y Contratos

El núcleo de la integración con el ecosistema AYTHER se basa en un contrato ABI estricto (ABI v1) que reemplaza la dependencia histórica de libretro sobre IDs de memoria privados y asunciones implícitas de layout.

## ABI v1 (`ayther_api.h`)

La comunicación entre el core y el frontend (AYTHER Engine) está gobernada por la interfaz versionada `ayther_interface_v1`. El único símbolo dinámico exportado para descubrimiento es:

```c
const ayther_interface_v1 *ayther_get_interface(uint32_t requested_version);
```

### Proceso de Descubrimiento Seguro
1.  **Resolución dinámica:** El frontend resuelve `ayther_get_interface`. Si no está presente, se asume un core legacy sin capacidades AYTHER.
2.  **Negociación:** Se invoca la función con `AYTHER_ABI_VERSION_1_0` (o `0` para descubrir la última versión).
3.  **Validación:** Se valida el `abi_version`, `struct_size` y los bits de capacidad requeridos antes de acceder a la API.

## Layouts de Datos

Los layouts (estructuras de memoria compartidas) están estrictamente controlados y congelados en tamaño para evitar fallos de memoria durante las actualizaciones del core:
*   `ayther_sprite_v1`: 10 bytes
*   `ayther_audio_write_v1`: 8 bytes
*   `ayther_audio_voice_v1`: 40 bytes
*   `ayther_audio_event_v1`: 80 bytes
*   `ayther_audio_transport_stats_v1`: 32 bytes
*   `ayther_subscription_state_v1`: 32 bytes

Las macros y aserciones en tiempo de compilación (`static_assert`) garantizan que el compilador respete estos tamaños sin relleno (padding) inesperado. El endianness para metadatos, sprites y audio obedece a la arquitectura del host (`host_endianness`), mientras que los datos puros (VRAM) conservan el endianness interno emulado.

## Modelo de Suscripciones (Runtime Subscriptions)

El modelo de suscripciones (`AYTHER_CAP_SUBSCRIPTIONS_V1`) permite separar la compilación de características de su uso en tiempo de ejecución. 

*   Por defecto, el core inicia con las capacidades apagadas (`active_mask = requested_mask = 0`), lo que permite que se comporte como un emulador estándar sin sobrecarga.
*   El frontend puede invocar `set_subscriptions` entre frames (nunca durante) para solicitar la activación de subsistemas específicos (ej. controles de render, sondas de audio).
*   La máscara de suscripciones solicitada entra en vigor al comienzo del siguiente ciclo `retro_run`.
*   Cualquier intento de acceder a características inactivas devolverá `AYTHER_STATUS_NOT_SUBSCRIBED`.
