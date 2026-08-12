# Subsistemas Gráficos

El fork AYTHER expande radicalmente las capacidades de manipulación gráfica de Genesis-Plus-GX para permitir streaming diferencial, análisis y manipulación del renderizado.

## Frame Delta Stream

Para optimizar la transmisión de estado en escenarios multijugador, la arquitectura AYTHER implementa un "Frame Delta Stream". En lugar de enviar un framebuffer completo de 60fps por red, el sistema captura y aísla los cambios mínimos:
1.  **Identificación de deltas:** Se monitorizan escrituras y supresiones en VRAM y tablas de sprites.
2.  **Snapshot Consistente:** Al final de `retro_run`, se toma una captura segura (generacional) del estado de sprites parseados (`AYTHER_REGION_PARSED_SPRITES`) y deltas visuales.
3.  **Sincronización:** El frontend aplica solo la diferencia (sprites movidos, capas actualizadas), lo que reduce el ancho de banda drásticamente.

## Recomposición Multicapa (Multilayer Recomposition)

La interfaz expone punteros a funciones clave como `ayther_recompose_frame` y `ayther_core_recompose_multilayer`. Esto permite:
*   Ignorar máscaras de capa tradicionales o el límite de sprites nativo por scanline.
*   Renderizar capas aisladas de manera determinista (por ejemplo, extraer solo el plano A, o solo el fondo).
*   Actuar como un *oráculo* determinista sin alterar el estado emulado subyacente. Se pueden hacer pasadas de renderizado alternativas en el mismo frame sin causar desincronizaciones del core.

## Raster Replay

En combinación con la recomposición, el Raster Replay registra y reproduce el comportamiento del rasterizador clásico.
*   Soporta la lectura y manipulación de estados de supresión de tiles (`TILE_SUPPRESS`) y sprites (`SPRITE_SUPPRESS`).
*   Informa al frontend las razones por las cuales el motor tuvo que recurrir al rasterizador de software de reserva (`RASTER_FALLBACK_REASONS`), útil para análisis y telemetría de rendimiento y fallos.
