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

BENCH_BIN := $(BUILD_DIR)/bench_audio_probe$(EXE_EXT)
BENCH_RESULT := $(ARTIFACT_DIR)/audio_probe_benchmark.json

.PHONY: all run clean

all: $(BENCH_BIN)

$(BUILD_DIR):
	$(call make_dir,$@)

$(ARTIFACT_DIR):
	$(call make_dir,$@)

$(BENCH_BIN): bench_audio_probe.c $(PROBE_SRC) $(PROBE_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(COMMON_CPPFLAGS) -o $@ bench_audio_probe.c $(PROBE_SRC) $(LDFLAGS)

run: $(BENCH_BIN) | $(ARTIFACT_DIR)
	$(BENCH_BIN) $(BENCH_RESULT)

clean:
	$(call remove_build_dir,$(BUILD_DIR))
	$(call remove_build_dir,$(ARTIFACT_DIR))
