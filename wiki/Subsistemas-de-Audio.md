# Subsistemas de Audio

El fork AYTHER integra una sonda de audio profunda para la reconstrucción semántica y el aislamiento de sonido. Esto permite transmitir comandos MIDI-like o eventos musicales al motor AYTHER en lugar de enviar streams PCM pesados.

## Audio Probe v2

Se requiere compilar el núcleo con `SOUND_PROBE=1` para activar `AYTHER_CAP_AUDIO_PROBE_V1`.
*   Utiliza una cola de tipo SPSC (Single Producer, Single Consumer) libre de bloqueos.
*   Un hilo emulador produce datos y el hilo de herramientas de AYTHER (consumidor) realiza un drenaje concurrente mediante `poll_audio_events` extrayendo registros `ayther_audio_event_v1`.
*   Ofrece capacidades para monitorizar estadísticas de transporte (eventos descartados, capacidad efectiva, etc.) mediante `get_audio_transport_stats`.

## Eventos Semánticos (PSG y YM2612)

El Audio Probe captura eventos semánticos de síntesis en lugar de audio procesado:
*   Captura de escrituras crudas a los chips de sonido.
*   Agrupamiento lógico de eventos a través de anclajes como `NOTE_ON` y `DAC_START`.
*   El motor AYTHER puede entonces recrear la síntesis (FM o de onda cuadrada) o redirigir estos eventos de forma aislada a los clientes conectados.
*   Los eventos de tiempo (FRAME, RAW_WRITE, NOTE_OFF, DAC_STOP) no prolongan la ventana de coincidencia, manteniendo baja latencia.

## Mute y Control del Sega CD PCM

El sistema provee acceso de escritura controlado al estado de mute (`AUDIO_MUTE` a través del bitmask `0x03FF`).
*   Permite mutear canales individuales de FM (YM2612) o PSG bajo demanda.
*   Implementa control específico para apagar la reproducción PCM del Sega CD si el usuario o el backend determinan que ese canal no debe mezclarse.
