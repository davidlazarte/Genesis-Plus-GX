# Standalone unit tests for the audio_probe module.
#
#   make            # build the test binary
#   make check      # build and run
#   make clean
#
# audio_probe.c is compiled against tests/stub/shared.h (a minimal stand-in),
# so no full emulator build is needed.

CORE_DIR := ..
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
CPPFLAGS := -DSOUND_PROBE -Istub -I$(CORE_DIR)/core/debug

SRC := test_audio_probe.c $(CORE_DIR)/core/debug/audio_probe.c
BIN := test_audio_probe

$(BIN): $(SRC) stub/shared.h $(CORE_DIR)/core/debug/audio_probe.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ test_audio_probe.c $(CORE_DIR)/core/debug/audio_probe.c

.PHONY: check clean
check: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
