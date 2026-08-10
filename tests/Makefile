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
PROBE_HEADERS   := stub/shared.h $(CORE_DIR)/core/debug/audio_probe.h

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
define make_dir
	@if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
endef
define remove_build_dir
	@if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
endef
else
EXE_EXT :=
define make_dir
	@mkdir -p "$1"
endef
define remove_build_dir
	@rm -rf -- "$1"
endef
endif

UNIT_BIN  := $(BUILD_DIR)/test_audio_probe$(EXE_EXT)
API_BIN := $(BUILD_DIR)/test_ayther_api$(EXE_EXT)
RASTER_BIN := $(BUILD_DIR)/test_raster_guard$(EXE_EXT)
ROM_PROBE_BIN := $(BUILD_DIR)/raster_rom_probe$(EXE_EXT)
TRACE_BIN := $(BUILD_DIR)/audio_probe_trace$(EXE_EXT)
TRACE_GOLDEN := ayther/golden/audio_probe_trace.json
TRACE_ACTUAL := $(ARTIFACT_DIR)/audio_probe_trace.actual.json

.PHONY: all check check-unit check-determinism raster-rom-probe clean

all: $(UNIT_BIN) $(API_BIN) $(RASTER_BIN) $(TRACE_BIN)

$(BUILD_DIR):
	$(call make_dir,$@)

$(ARTIFACT_DIR):
	$(call make_dir,$@)

$(UNIT_BIN): test_audio_probe.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ test_audio_probe.c $(PROBE_SRC) $(LDFLAGS)

$(API_BIN): test_ayther_api.c $(CORE_DIR)/core/ayther/ayther_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -o $@ test_ayther_api.c $(LDFLAGS)

$(RASTER_BIN): test_raster_guard.c $(CORE_DIR)/core/ayther_raster.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -o $@ test_raster_guard.c $(LDFLAGS)

ifeq ($(OS),Windows_NT)
ROM_PROBE_LIBS :=
else
ROM_PROBE_LIBS := -ldl
endif

$(ROM_PROBE_BIN): ayther/raster_rom_probe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(CORE_DIR)/core -I$(CORE_DIR)/libretro/libretro-common/include -o $@ $< $(LDFLAGS) $(ROM_PROBE_LIBS)

raster-rom-probe: $(ROM_PROBE_BIN)

$(TRACE_BIN): ayther/audio_probe_trace.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ ayther/audio_probe_trace.c $(PROBE_SRC) $(LDFLAGS)

check: check-unit check-determinism

check-unit: $(UNIT_BIN) $(API_BIN) $(RASTER_BIN) $(ROM_PROBE_BIN)
	$(UNIT_BIN)
	$(API_BIN)
	$(RASTER_BIN)

check-determinism: $(TRACE_BIN) | $(ARTIFACT_DIR)
	$(TRACE_BIN) $(TRACE_GOLDEN) $(TRACE_ACTUAL)

clean:
	$(call remove_build_dir,$(BUILD_DIR))
	$(call remove_build_dir,$(ARTIFACT_DIR))
