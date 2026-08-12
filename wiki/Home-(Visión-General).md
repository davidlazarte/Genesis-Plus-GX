# Genesis-Plus-GX: AYTHER Fork

Bienvenido a la documentación del fork AYTHER de Genesis-Plus-GX. Este fork está diseñado y optimizado específicamente como un core libretro altamente especializado para el motor AYTHER. Su propósito central no es ejecutarse como un emulador tradicional, sino proporcionar un backend determinista con capacidades avanzadas de extracción de estado, recomposición multicapa y transmisión diferencial (Delta Stream).

## Características Principales

*   **ABI v1 Estricta:** Contratos definidos y versionados para la interoperabilidad con el frontend.
*   **Extracción de Estado Profunda:** Acceso determinista a la VRAM, CRAM, VSRAM y registros VDP.
*   **Recomposición Multicapa y Raster Replay:** Aislamiento de capas gráficas y sprites para renderizado sin interfaz o con superposiciones personalizadas.
*   **Audio Probe v2:** Sonda de eventos de audio de alta precisión con eventos semánticos (PSG, YM2612).
*   **Modelo de Suscripciones:** Activación de subsistemas (gráfico, audio, estado) en tiempo de ejecución para minimizar el coste de rendimiento cuando no se necesitan.

## Dependencias e Instalación

Para compilar y trabajar con este fork, necesitas un entorno de desarrollo compatible con C y las herramientas de compilación estándar (GCC/Clang, Make).

**Dependencias:**
*   `make`
*   Compilador C (GCC o Clang) compatible con C99.
*   (Opcional) Herramientas de análisis estático si se desea auditar el código.

**Compilación del Core:**
Para compilar el core libretro estándar con soporte AYTHER:
```bash
make -f Makefile.libretro
```

## Pruebas y Validación (Tests)

La calidad y la estabilidad de la ABI son fundamentales para el ecosistema AYTHER. Este fork incluye una suite de pruebas para verificar el cumplimiento del contrato ABI, la resolución de símbolos dinámicos y la lógica de descubrimiento.

Para ejecutar los tests, utiliza el siguiente comando desde la raíz del repositorio:
```bash
make -C tests check
```
Esto compilará los entornos de prueba e invocará la verificación estricta de la API (`verify_ayther_api.c`). Si alguna aserción falla (tamaños de layout, negociación de versiones, o adaptadores legacy), el comando devolverá un error.
