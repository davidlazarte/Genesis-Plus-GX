# Standalone AYTHER tests. Both binaries compile audio_probe.c against the
# minimal tests/stub/shared.h, so a ROM or a full emulator build is not needed.

CORE_DIR    := ..
BUILD_DIR   ?= .build
ARTIFACT_DIR ?= artifacts
CC          ?= cc
CFLAGS      ?= -O2 -Wall -Wextra
CPPFLAGS    ?=
LDFLAGS     ?=

COMMON_CPPFLAGS := -DSOUND_PROBE -Istub -I$(CORE_DIR)/core/debug
PROBE_SRC       := $(CORE_DIR)/core/debug/audio_probe.c
PROBE_HEADERS   := stub/shared.h $(CORE_DIR)/core/debug/audio_probe.h \
	$(CORE_DIR)/core/debug/audio_probe_atomic.h \
	$(CORE_DIR)/core/ayther/ayther_api.h
SPRITE_CAPTURE_SRC := $(CORE_DIR)/core/ayther/ayther_sprite_capture.c
SPRITE_CAPTURE_HEADERS := $(CORE_DIR)/core/ayther/ayther_sprite_capture.h \
	$(CORE_DIR)/core/ayther/ayther_api.h

# EXE_EXT, make_dir y remove_build_dir viven en mk/shell.mk (#43): el bloque
# estaba copiado en tres Makefiles y se corrigio dos veces por separado.
include $(CORE_DIR)/mk/shell.mk

ifeq ($(findstring sh,$(notdir $(SHELL))),sh)
define require_core
	@test -n "$(CORE)" || (echo "CORE=/path/to/libretro core is required" >&2; exit 2)
endef
define require_profile_cores
	@test -n "$(PROFILE_OFF_CORE)" || (echo "PROFILE_OFF_CORE is required" >&2; exit 2)
	@test -n "$(PROFILE_IDLE_CORE)" || (echo "PROFILE_IDLE_CORE is required" >&2; exit 2)
endef
else
define require_core
	@if "$(CORE)"=="" (echo CORE=path-to-libretro-core is required 1>&2 & exit /B 2)
endef
define require_profile_cores
	@if "$(PROFILE_OFF_CORE)"=="" (echo PROFILE_OFF_CORE is required 1>&2 & exit /B 2)
	@if "$(PROFILE_IDLE_CORE)"=="" (echo PROFILE_IDLE_CORE is required 1>&2 & exit /B 2)
endef
endif

UNIT_BIN  := $(BUILD_DIR)/test_audio_probe$(EXE_EXT)
CONCURRENCY_BIN := $(BUILD_DIR)/test_audio_probe_concurrency$(EXE_EXT)
API_BIN := $(BUILD_DIR)/test_ayther_api$(EXE_EXT)
RASTER_BIN := $(BUILD_DIR)/test_raster_guard$(EXE_EXT)
DIM_BIN := $(BUILD_DIR)/test_dim_quarter$(EXE_EXT)
SPRITE_CAPTURE_BIN := $(BUILD_DIR)/test_sprite_capture$(EXE_EXT)
ROM_PROBE_BIN := $(BUILD_DIR)/raster_rom_probe$(EXE_EXT)
FULL_CORE_BIN := $(BUILD_DIR)/full_core_replay$(EXE_EXT)
STATE_GUARD_BIN := $(BUILD_DIR)/state_guard$(EXE_EXT)
STATE_LAYOUT_BIN := $(BUILD_DIR)/state_layout$(EXE_EXT)
AUDIO_CTRL_BIN := $(BUILD_DIR)/audio_controls$(EXE_EXT)
FRAME_DELTA_BIN := $(BUILD_DIR)/frame_delta$(EXE_EXT)
IDLE_METRICS_BIN := $(BUILD_DIR)/idle_metrics$(EXE_EXT)
ATTRIB_SCENE_BIN := $(BUILD_DIR)/attribution_scene$(EXE_EXT)
TRACE_BIN := $(BUILD_DIR)/audio_probe_trace$(EXE_EXT)
TRACE_GOLDEN := ayther/golden/audio_probe_trace.json
TRACE_ACTUAL := $(ARTIFACT_DIR)/audio_probe_trace.actual.json
# UN SOLO GOLDEN, y ya no uno por plataforma (#45).
#
# Linux y Windows daban hashes distintos de video, audio y estado con el mismo
# core, y se convivio con dos goldens sin saber por que. Eran DOS causas, las
# dos ajenas a la emulacion:
#
#   1. `gen_reset` sacaba de `rand()` el punto del frame en que arranca el 68k
#      tras un reset por boton. `rand()` es de la libc: glibc y MSVCRT no
#      coinciden ni en el generador ni en RAND_MAX. Windows arrancaba en otro
#      punto del frame y quedaba un frame entero por delante para siempre.
#      Arreglado en core/genesis.c con un xorshift propio.
#   2. El hash del estado incluia el tag de layout de AYTHER, que codifica los
#      tamanios de las structs serializadas y difiere entre plataformas A
#      PROPOSITO. Ahora queda fuera del hash (ver full_core_replay.c).
#
# Con las dos corregidas los cuatro hashes coinciden byte a byte entre Linux y
# Windows, asi que un golden por plataforma ya no describe nada real.
FULL_CORE_GOLDEN := ayther/golden/full_core_replay-x64.json
FULL_CORE_ACTUAL := $(ARTIFACT_DIR)/full_core_replay.actual.json
FULL_CORE_REPORT := $(ARTIFACT_DIR)/full_core_replay.frames.jsonl
FULL_CORE_BENCHMARK := $(ARTIFACT_DIR)/full_core_benchmark.json
PROFILE_COMPARISON := $(ARTIFACT_DIR)/feature_gate_profiles.json

.PHONY: all check check-unit check-concurrency check-determinism \
	check-full-core check-profile-comparison raster-rom-probe \
	full-core-replay check-idle-metrics check-attribution-scene clean

all: $(UNIT_BIN) $(CONCURRENCY_BIN) $(API_BIN) $(RASTER_BIN) \
	$(SPRITE_CAPTURE_BIN) $(DIM_BIN) $(TRACE_BIN)

$(BUILD_DIR):
	$(call make_dir,$@)

$(ARTIFACT_DIR):
	$(call make_dir,$@)

$(UNIT_BIN): test_audio_probe.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ test_audio_probe.c $(PROBE_SRC) $(LDFLAGS)

$(CONCURRENCY_BIN): test_audio_probe_concurrency.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -DAUDIO_PROBE_RING_SIZE=64 \
		-o $@ test_audio_probe_concurrency.c $(PROBE_SRC) $(LDFLAGS) $(THREAD_FLAGS)

$(API_BIN): test_ayther_api.c $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -o $@ test_ayther_api.c $(LDFLAGS)

$(RASTER_BIN): test_raster_guard.c $(CORE_DIR)/core/ayther_raster.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -o $@ test_raster_guard.c $(LDFLAGS)

# #31: el atenuado del dim, en los tres formatos de pixel que el core compila.
# Aritmetica pura: no necesita el core, solo el mismo header.
$(DIM_BIN): test_dim_quarter.c $(CORE_DIR)/core/ayther/ayther_dim.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ test_dim_quarter.c $(LDFLAGS)

$(SPRITE_CAPTURE_BIN): test_sprite_capture.c $(SPRITE_CAPTURE_SRC) \
		$(SPRITE_CAPTURE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DAYTHER_SPRITE_CAPTURE_METRICS \
		-I$(CORE_DIR)/core -o $@ test_sprite_capture.c \
		$(SPRITE_CAPTURE_SRC) $(LDFLAGS)

ifeq ($(OS),Windows_NT)
ROM_PROBE_LIBS :=
else
ROM_PROBE_LIBS := -ldl
endif

$(ROM_PROBE_BIN): ayther/raster_rom_probe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -I$(CORE_DIR)/libretro/libretro-common/include -o $@ $< $(LDFLAGS) $(ROM_PROBE_LIBS)

raster-rom-probe: $(ROM_PROBE_BIN)

$(FULL_CORE_BIN): ayther/full_core_replay.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core \
		-I$(CORE_DIR)/libretro/libretro-common/include -Iayther \
		-o $@ ayther/full_core_replay.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

$(STATE_GUARD_BIN): ayther/state_guard.c ayther/generated_rom.c 		ayther/generated_rom.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/state_guard.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

# Necesita el core REAL: lo que se verifica es que retro_unserialize rechace un
# estado de otro layout, y eso solo lo puede contestar el binario.
check-state-guard: $(STATE_GUARD_BIN)
	$(call require_core)
	$(STATE_GUARD_BIN) "$(CORE)"

# #45: shared.h arrastra los headers del core, asi que necesita el mismo set de
# -I que el build real.
CORE_INCFLAGS := -I$(CORE_DIR)/core -I$(CORE_DIR)/core/z80 	-I$(CORE_DIR)/core/m68k -I$(CORE_DIR)/core/ntsc -I$(CORE_DIR)/core/sound 	-I$(CORE_DIR)/core/input_hw -I$(CORE_DIR)/core/cd_hw 	-I$(CORE_DIR)/core/cart_hw -I$(CORE_DIR)/core/cart_hw/svp 	-I$(CORE_DIR)/core/debug -I$(CORE_DIR)/core/ayther 	-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include

$(STATE_LAYOUT_BIN): ayther/state_layout.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_INCFLAGS) -o $@ ayther/state_layout.c $(LDFLAGS)

# Diagnostico, no gate: imprime y sale 0. Lo que aporta es dejar los tamanios
# en el log de CADA plataforma, para que comparar sea leer dos logs.
state-layout: $(STATE_LAYOUT_BIN)
	$(STATE_LAYOUT_BIN)

$(AUDIO_CTRL_BIN): ayther/audio_controls.c ayther/generated_rom.c 		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/audio_controls.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #29: necesita el core REAL. Lo que se verifica es que la mascara de mute
# llegue al chip de FM seleccionado, y eso solo lo contesta el binario.
check-audio-controls: $(AUDIO_CTRL_BIN)
	$(call require_core)
	$(AUDIO_CTRL_BIN) "$(CORE)"

$(FRAME_DELTA_BIN): ayther/frame_delta.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/frame_delta.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #30: necesita el core real. Lo que se verifica es que dos lecturas del
# mismo frame devuelvan lo mismo, y eso solo lo contesta el binario.
check-frame-delta: $(FRAME_DELTA_BIN)
	$(call require_core)
	$(FRAME_DELTA_BIN) "$(CORE)"

$(IDLE_METRICS_BIN): ayther/idle_metrics.c ayther/generated_rom.c 		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h 		$(CORE_DIR)/core/ayther/ayther_metrics.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DAYTHER_METRICS -I$(CORE_DIR)/core/ayther 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/idle_metrics.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #36: necesita un core compilado con AYTHER_METRICS=1. Con cualquier otro el
# test se saltea en vez de fallar -- es un build de diagnostico, no publicado.
check-idle-metrics: $(IDLE_METRICS_BIN)
	$(call require_core)
	$(IDLE_METRICS_BIN) "$(CORE)"

$(ATTRIB_SCENE_BIN): ayther/attribution_scene.c ayther/generated_rom.c 		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/attribution_scene.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #41: la region de atribucion con una suscripcion que pide SOLO atribucion.
check-attribution-scene: $(ATTRIB_SCENE_BIN)
	$(call require_core)
	$(ATTRIB_SCENE_BIN) "$(CORE)"

$(FRAME_DELTA_BIN): ayther/frame_delta.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/frame_delta.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #30: necesita el core real. Lo que se verifica es que dos lecturas del
# mismo frame devuelvan lo mismo, y eso solo lo contesta el binario.
check-frame-delta: $(FRAME_DELTA_BIN)
	$(call require_core)
	$(FRAME_DELTA_BIN) "$(CORE)"

full-core-replay: $(FULL_CORE_BIN)

check-full-core: $(FULL_CORE_BIN) | $(ARTIFACT_DIR)
	$(call require_core)
	$(FULL_CORE_BIN) "$(CORE)" $(FULL_CORE_GOLDEN) $(FULL_CORE_ACTUAL) \
		$(FULL_CORE_REPORT) $(FULL_CORE_BENCHMARK)

check-profile-comparison: $(FULL_CORE_BIN) | $(ARTIFACT_DIR)
	$(call require_profile_cores)
	$(FULL_CORE_BIN) --compare-profiles "$(PROFILE_OFF_CORE)" \
		"$(PROFILE_IDLE_CORE)" $(PROFILE_COMPARISON)

$(TRACE_BIN): ayther/audio_probe_trace.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ ayther/audio_probe_trace.c $(PROBE_SRC) $(LDFLAGS)

check: check-unit check-determinism

check-unit: $(UNIT_BIN) $(CONCURRENCY_BIN) $(API_BIN) $(RASTER_BIN) \
		$(SPRITE_CAPTURE_BIN) $(DIM_BIN) $(ROM_PROBE_BIN)
	$(UNIT_BIN)
	$(CONCURRENCY_BIN)
	$(API_BIN)
	$(RASTER_BIN)
	$(SPRITE_CAPTURE_BIN)
	$(DIM_BIN)

check-concurrency: $(CONCURRENCY_BIN)
	$(CONCURRENCY_BIN)

check-determinism: $(TRACE_BIN) | $(ARTIFACT_DIR)
	$(TRACE_BIN) $(TRACE_GOLDEN) $(TRACE_ACTUAL)

clean:
	$(call remove_build_dir,$(BUILD_DIR))
	$(call remove_build_dir,$(ARTIFACT_DIR))
