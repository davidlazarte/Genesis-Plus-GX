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

# El nombre del binario depende del SISTEMA; las recetas, del SHELL. Ver la nota
# larga en tests/Makefile: en Windows, si hay un `sh` en el PATH, GNU make lo
# elige como SHELL y las recetas en sintaxis de cmd mueren con «syntax error
# near unexpected token». Este archivo tenia el mismo defecto que aquel.
ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
else
EXE_EXT :=
endif

ifeq ($(findstring sh,$(notdir $(SHELL))),sh)
define make_dir
	@mkdir -p "$1"
endef
define remove_build_dir
	@rm -rf -- "$1"
endef
else
define make_dir
	@if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
endef
define remove_build_dir
	@if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
endef
endif

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
