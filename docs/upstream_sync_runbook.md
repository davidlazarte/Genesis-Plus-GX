# Upstream Sync Runbook (AYTHER)

Este runbook define las políticas y pasos estrictos para realizar la sincronización con el repositorio original (upstream) de Genesis-Plus-GX.

## 1. Políticas Generales
*   **Rebases pequeños**: Siempre preferir rebases incrementales sobre merges gigantes para mantener un historial limpio y localizable.
*   **Normalización previa**: Antes de hacer rebase, asegúrate de que tu rama local esté normalizada (CRLF vs LF) con `.gitattributes`.
*   **Aislamiento**: Todo código de AYTHER que no requiera intimidad con el VDP (helpers, inicialización de estructuras, etc.) debe habitar en `core/ayther/`.

## 2. Procedimiento de Fetch y Rebase
1.  Obtener los últimos cambios de upstream:
    ```bash
    git remote add upstream https://github.com/ekeeke/Genesis-Plus-GX.git
    git fetch upstream
    ```
2.  Iniciar el rebase interactivo si aplica:
    ```bash
    git rebase upstream/master
    ```

## 3. Resolución de Conflictos Críticos
Presta atención meticulosa a los siguientes archivos críticos donde se asientan nuestros _hooks_:
*   `libretro/libretro.c`: (Retro_get_memory_data, inicialización).
*   `core/vdp_render.c`: (Render_line, parse_satb_m5).
*   `core/sound/sound.c` y `ym2612.c`: (Mixer loop y shadow registers).

## 4. Suite Obligatoria de Verificación
Si superaste el rebase, antes de crear el commit de sync final, DEBES correr localmente:
```bash
make -C tests check
```
Si los tests fallan (en particular el chequeo de determinismo `audio_probe_trace`), evalúa inmediatamente la regresión en accuracy y ajusta el golden trace **solo si** el cambio de upstream está fundamentado (ej: corrección de un bug en emulación).
