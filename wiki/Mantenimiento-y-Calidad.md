# Mantenimiento y Calidad

Para mantener este fork sostenible, determinista y seguro a lo largo del tiempo, seguimos una serie de prácticas estrictas tanto para la integración con la rama principal (upstream) como para el mantenimiento general.

## Runbook de Sincronización con Upstream (Sync Runbook)

La base de código original de Genesis-Plus-GX (upstream) recibe actualizaciones regulares que pueden mejorar la compatibilidad. Para traer estos cambios al fork AYTHER sin romper la ABI v1, sigue este procedimiento:

1.  **Revisión del Changelog de Upstream:**
    Identifica si se realizaron cambios sustanciales en `vdp_ctrl.c`, `vdp_render.c`, `sound.c`, o `system.c`.
2.  **Preparación de Rama de Merge:**
    Crea una nueva rama (`git checkout -b sync/upstream-fecha`).
3.  **Merge Estratégico:**
    Haz un merge (o rebase) de los cambios upstream.
4.  **Resolución de Conflictos Priorizando AYTHER:**
    *   *Renderizado:* Si un cambio en `vdp_render.c` altera la iteración de la tabla de sprites, aségurate de que la generación de deltas visuales no se corrompa.
    *   *Audio:* Cualquier cambio en el PSG/YM2612 debe preservar los hooks del Audio Probe v2 (`SOUND_PROBE=1`).
    *   *Estado:* El estado guardado y la VRAM no deben cambiar su modelo de inmutabilidad (los punteros `AYTHER_REGION_*` no deben apuntar a datos efímeros).
5.  **Ejecutar los Tests de la ABI:**
    Compila y verifica obligatoriamente los contratos:
    ```bash
    make -C tests check
    ```
6.  **Validación Manual:**
    Carga el core en AYTHER Engine y valida un flujo completo de suscripciones encendiendo gráficas y audio (Frame Delta y Audio Probe).
7.  **Revisión Final y Commit:**
    Finaliza el PR y anota los cambios estructurales. No modifiques la ABI v1 a menos que se haya consensuado (y debe negociarse un cambio mayor en la API).

## Calidad del Código (QA)
*   **Aserciones Estáticas:** Se exige el uso exhaustivo de aserciones en tiempo de compilación para todos los tamaños de layouts compartidos con el frontend.
*   **Determinismo:** Evita agregar números aleatorios no pseudo-sembrados o lectura de contadores de alto rendimiento dentro de la emulación.
