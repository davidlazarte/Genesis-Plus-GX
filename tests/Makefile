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

UNIT_BIN  := $(BUILD_DIR)/test_audio_probe$(EXE_EXT)
CONCURRENCY_BIN := $(BUILD_DIR)/test_audio_probe_concurrency$(EXE_EXT)
API_BIN := $(BUILD_DIR)/test_ayther_api$(EXE_EXT)
RASTER_BIN := $(BUILD_DIR)/test_raster_guard$(EXE_EXT)
DIM_BIN := $(BUILD_DIR)/test_dim_quarter$(EXE_EXT)
SPRITE_RULE_BIN := $(BUILD_DIR)/test_sprite_px$(EXE_EXT)
SPRITE_CAPTURE_BIN := $(BUILD_DIR)/test_sprite_capture$(EXE_EXT)
ROM_PROBE_BIN := $(BUILD_DIR)/raster_rom_probe$(EXE_EXT)
FULL_CORE_BIN := $(BUILD_DIR)/full_core_replay$(EXE_EXT)
STATE_GUARD_BIN := $(BUILD_DIR)/state_guard$(EXE_EXT)
STATE_LAYOUT_BIN := $(BUILD_DIR)/state_layout$(EXE_EXT)
AUDIO_CTRL_BIN := $(BUILD_DIR)/audio_controls$(EXE_EXT)
FRAME_DELTA_BIN := $(BUILD_DIR)/frame_delta$(EXE_EXT)
IDLE_METRICS_BIN := $(BUILD_DIR)/idle_metrics$(EXE_EXT)
ATTRIB_SCENE_BIN := $(BUILD_DIR)/attribution_scene$(EXE_EXT)
PLANE_SUP_BIN := $(BUILD_DIR)/plane_suppress$(EXE_EXT)
MULTILAYER_BIN := $(BUILD_DIR)/multilayer$(EXE_EXT)
OBSERVABILITY_BIN := $(BUILD_DIR)/observability$(EXE_EXT)
SPR_OUTCOME_BIN := $(BUILD_DIR)/sprite_outcome$(EXE_EXT)
ABI_DYNAMIC_BIN := $(BUILD_DIR)/verify_ayther_api$(EXE_EXT)
SCENES_BIN := $(BUILD_DIR)/scenes$(EXE_EXT)
LINE_STATE_BIN := $(BUILD_DIR)/line_state$(EXE_EXT)
SCENES_GOLDEN := ayther/golden/scenes.txt
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
	full-core-replay check-idle-metrics check-attribution-scene check-plane-suppress check-multilayer \
	check-observability check-abi-dynamic check-fuzz check-fuzz-ring \
	check-sprite-outcome \
	regen-multilayer check-scenes regen-scenes check-line-state clean

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

# #31/#37/#41: que la regla de "el sprite gana" sea la misma que aplica la LUT.
# Aritmetica pura sobre las combinaciones posibles: no necesita el core.
$(SPRITE_RULE_BIN): test_sprite_px.c $(CORE_DIR)/core/ayther/ayther_sprite_px.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ test_sprite_px.c $(LDFLAGS)

$(SPRITE_CAPTURE_BIN): test_sprite_capture.c $(SPRITE_CAPTURE_SRC) \
		$(SPRITE_CAPTURE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DAYTHER_SPRITE_CAPTURE_METRICS \
		-I$(CORE_DIR)/core -o $@ test_sprite_capture.c \
		$(SPRITE_CAPTURE_SRC) $(LDFLAGS)

# #35.2: en macOS `dlopen` vive en libSystem y no hay una libdl que
# linkear; en Windows se usa LoadLibrary. Solo Linux (y los Unix con
# libdl separada) necesitan el -ldl.
ifeq ($(OS),Windows_NT)
ROM_PROBE_LIBS :=
else ifeq ($(shell uname -s),Darwin)
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
		-I$(CORE_DIR)/libretro/libretro-common/include -Iayther -Ici \
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

$(PLANE_SUP_BIN): ayther/plane_suppress.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/plane_suppress.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

$(MULTILAYER_BIN): ayther/multilayer.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/multilayer.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

$(OBSERVABILITY_BIN): ayther/observability.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/observability.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #55: el contrato dinamico de la ABI contra el core REAL. Vivia solo en el
# workflow -- se compilaba a mano dentro del job de exports-, asi que no habia
# forma de correrlo antes de pushear. Siete afirmaciones rotas viajaron hasta
# CI por eso: las tres regiones de #42 declaraban byte_size != element_size *
# capacity, y el rechazo de Mode 4 de #40 contestaba UNSUPPORTED_MODE con el
# VDP todavia sin elegir modo. Un test que solo corre despues del push no es
# una red, es un aviso tardio.
$(ABI_DYNAMIC_BIN): $(CORE_DIR)/tests/ci/verify_ayther_api.c \
		$(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core \
		-o $@ ci/verify_ayther_api.c $(LDFLAGS) $(ROM_PROBE_LIBS)

check-abi-dynamic: $(ABI_DYNAMIC_BIN)
	$(call require_core)
	$(ABI_DYNAMIC_BIN) "$(CORE)"

$(SPR_OUTCOME_BIN): ayther/sprite_outcome.c ayther/generated_rom.c \
		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther \
		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include \
		-Iayther -o $@ ayther/sprite_outcome.c ayther/generated_rom.c \
		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #39.C: por que cada sprite se dibujo o no.
check-sprite-outcome: $(SPR_OUTCOME_BIN)
	$(call require_core)
	$(SPR_OUTCOME_BIN) "$(CORE)"

# #39 A/D/E: journal raster, hashes del frame y paleta resuelta.
check-observability: $(OBSERVABILITY_BIN)
	$(call require_core)
	$(OBSERVABILITY_BIN) "$(CORE)"

# #37.6: las cinco capas de recompose_multilayer, pixel por pixel.
check-multilayer: $(MULTILAYER_BIN)
	$(call require_core)
	$(MULTILAYER_BIN) "$(CORE)" ayther/golden/multilayer.txt

regen-multilayer: $(MULTILAYER_BIN)
	$(call require_core)
	$(MULTILAYER_BIN) "$(CORE)" ayther/golden/multilayer.txt --regen

# #37.4: la supresion de tiles por plano, plano por plano.
check-plane-suppress: $(PLANE_SUP_BIN)
	$(call require_core)
	$(PLANE_SUP_BIN) "$(CORE)"

# #41: la region de atribucion con una suscripcion que pide SOLO atribucion.
check-attribution-scene: $(ATTRIB_SCENE_BIN)
	$(call require_core)
	$(ATTRIB_SCENE_BIN) "$(CORE)"

$(SCENES_BIN): ayther/scenes.c ayther/generated_rom.c 		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/scenes.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

$(LINE_STATE_BIN): ayther/line_state.c ayther/generated_rom.c 		ayther/generated_rom.h $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core/ayther 		-I$(CORE_DIR)/libretro -I$(CORE_DIR)/libretro/libretro-common/include 		-Iayther -o $@ ayther/line_state.c ayther/generated_rom.c 		$(LDFLAGS) $(ROM_PROBE_LIBS)

# #42: el estado de render por scanline, contra la tabla de hscroll del ROM.
check-line-state: $(LINE_STATE_BIN)
	$(call require_core)
	$(LINE_STATE_BIN) "$(CORE)"

# #35: un hash por modo de video. El golden es por escena y no agregado: eso es
# lo que convierte "algo se rompio" en "se rompio el interlace 2".
check-scenes: $(SCENES_BIN)
	$(call require_core)
	$(SCENES_BIN) "$(CORE)" $(SCENES_GOLDEN)

# El golden se regenera con un comando y no a mano. Un golden que solo se puede
# actualizar editandolo termina editado sin mirar, que es como deja de valer.
regen-scenes: $(SCENES_BIN)
	$(call require_core)
	$(SCENES_BIN) "$(CORE)" $(SCENES_GOLDEN) --regen

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

# #34: `check-fuzz-ring` entra en el `check` pelado porque es el unico target
# de fuzzing que no necesita un core cargado. Los otros cuatro van en
# `check-fuzz`, que si lo pide.
check: check-unit check-determinism check-fuzz-ring

# #34: reproducir el corpus y las regresiones de los targets de fuzzing.
# Esto NO fuzzea -- no necesita clang-: replaya entradas conocidas bajo
# ASan/UBSan, que es lo que evita que un crash arreglado vuelva sin que
# nadie se entere.
check-fuzz:
	$(call require_core)
	$(MAKE) -C fuzz replay-all CC=$(CC) CORE="$(abspath $(CORE))"

check-fuzz-ring:
	$(MAKE) -C fuzz replay-audio_ring CC=$(CC) CORE=no-hace-falta

check-unit: $(UNIT_BIN) $(CONCURRENCY_BIN) $(API_BIN) $(RASTER_BIN) \
		$(SPRITE_CAPTURE_BIN) $(DIM_BIN) $(SPRITE_RULE_BIN) $(ROM_PROBE_BIN)
	$(UNIT_BIN)
	$(CONCURRENCY_BIN)
	$(API_BIN)
	$(RASTER_BIN)
	$(SPRITE_CAPTURE_BIN)
	$(DIM_BIN)
	$(SPRITE_RULE_BIN)

check-concurrency: $(CONCURRENCY_BIN)
	$(CONCURRENCY_BIN)

check-determinism: $(TRACE_BIN) | $(ARTIFACT_DIR)
	$(TRACE_BIN) $(TRACE_GOLDEN) $(TRACE_ACTUAL)

clean:
	$(call remove_build_dir,$(BUILD_DIR))
	$(call remove_build_dir,$(ARTIFACT_DIR))
