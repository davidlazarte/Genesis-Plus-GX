CORE_DIR     := ..
BUILD_DIR    ?= .build
ARTIFACT_DIR ?= artifacts
CC           ?= cc
CFLAGS       ?= -O2 -Wall -Wextra
CPPFLAGS     ?=
LDFLAGS      ?=

COMMON_CPPFLAGS := -DSOUND_PROBE -I$(CORE_DIR)/tests/stub -I$(CORE_DIR)/core/debug
PROBE_SRC       := $(CORE_DIR)/core/debug/audio_probe.c
PROBE_HEADERS   := $(CORE_DIR)/tests/stub/shared.h $(CORE_DIR)/core/debug/audio_probe.h

# EXE_EXT, make_dir y remove_build_dir viven en mk/shell.mk (#43). Este archivo
# arrastraba la tercera copia del bloque y se entero tarde de las correcciones.
include $(CORE_DIR)/mk/shell.mk

BENCH_BIN := $(BUILD_DIR)/bench_audio_probe$(EXE_EXT)
BENCH_RESULT := $(ARTIFACT_DIR)/audio_probe_benchmark.json
SPRITE_BENCH_BIN := $(BUILD_DIR)/bench_sprite_capture$(EXE_EXT)
SPRITE_BENCH_RESULT := $(ARTIFACT_DIR)/sprite_capture_benchmark.json
SPRITE_CAPTURE_SRC := $(CORE_DIR)/core/ayther/ayther_sprite_capture.c
SPRITE_CAPTURE_HEADERS := $(CORE_DIR)/core/ayther/ayther_sprite_capture.h \
	$(CORE_DIR)/core/ayther/ayther_api.h

.PHONY: all run clean

all: $(BENCH_BIN) $(SPRITE_BENCH_BIN)

$(BUILD_DIR):
	$(call make_dir,$@)

$(ARTIFACT_DIR):
	$(call make_dir,$@)

$(BENCH_BIN): bench_audio_probe.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ bench_audio_probe.c $(PROBE_SRC) $(LDFLAGS)

$(SPRITE_BENCH_BIN): bench_sprite_capture.c $(SPRITE_CAPTURE_SRC) \
		$(SPRITE_CAPTURE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DAYTHER_SPRITE_CAPTURE_METRICS \
		-I$(CORE_DIR)/core -o $@ bench_sprite_capture.c \
		$(SPRITE_CAPTURE_SRC) $(LDFLAGS)

run: $(BENCH_BIN) $(SPRITE_BENCH_BIN) | $(ARTIFACT_DIR)
	$(BENCH_BIN) $(BENCH_RESULT)
	$(SPRITE_BENCH_BIN) $(SPRITE_BENCH_RESULT)

clean:
	$(call remove_build_dir,$(BUILD_DIR))
	$(call remove_build_dir,$(ARTIFACT_DIR))
