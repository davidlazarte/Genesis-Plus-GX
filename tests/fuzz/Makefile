# Targets de fuzzing del fork. (#34)
#
# DOS builds del mismo codigo, y las dos hacen falta:
#
#   fuzz-<name>     con libFuzzer (clang). Busca entradas nuevas. Corre en el
#                   job nocturno; necesita clang, que no esta en todas las
#                   maquinas de desarrollo.
#   replay-<name>   sin fuzzer, con cualquier compilador de C. Reproduce el
#                   corpus y `regressions/`, y es lo que entra en `make check`.
#
# Si solo existiera el primero, un target roto se descubriria en el job nocturno
# y nadie sabria si se rompio el target o el core. Si solo existiera el segundo,
# no se encontraria nada nuevo.

CORE_DIR    := ../..
BUILD_DIR   ?= .build
CC          ?= cc
FUZZ_CC     ?= clang
CFLAGS      ?= -O1 -g -Wall -Wextra
CPPFLAGS    ?=
LDFLAGS     ?=

include $(CORE_DIR)/mk/shell.mk

INCLUDES := -I$(CORE_DIR)/core/ayther -I$(CORE_DIR)/libretro \
	-I$(CORE_DIR)/libretro/libretro-common/include -I$(CORE_DIR)/tests/ayther

ifeq ($(OS),Windows_NT)
DL_LIBS :=
else ifeq ($(shell uname -s),Darwin)
DL_LIBS :=
else
DL_LIBS := -ldl
endif

# El ROM sintetico es la base de casi todos los targets: la entrada del fuzzer
# es una MUTACION sobre algo valido, no bytes al azar desde cero. Empezar del
# vacio gasta el presupuesto en llegar a "es un cartucho".
COMMON_SRC := $(CORE_DIR)/tests/ayther/generated_rom.c
COMMON_DEP := fuzz_common.h $(CORE_DIR)/tests/ayther/generated_rom.h \
	$(CORE_DIR)/core/ayther/ayther_api.h

# `audio_ring` es el unico que no carga el core: compila audio_probe.c contra el
# stub de tests/, igual que test_audio_probe_concurrency.c. Lo que prueba es el
# protocolo del ring, no la emulacion, y meterle un core de por medio solo
# agregaria ruido entre el fuzzer y lo que se quiere romper.
SRC_audio_ring := $(CORE_DIR)/core/debug/audio_probe.c
INC_audio_ring := -DSOUND_PROBE -I$(CORE_DIR)/tests/stub \
	-I$(CORE_DIR)/core/debug -I$(CORE_DIR)/core/ayther
DEP_audio_ring := $(CORE_DIR)/core/debug/audio_probe.h

SRC_write_control  := $(COMMON_SRC)
SRC_unserialize    := $(COMMON_SRC)
SRC_recompose      := $(COMMON_SRC)
SRC_generated_rom  := $(COMMON_SRC)
DEP_write_control  := $(COMMON_DEP)
DEP_unserialize    := $(COMMON_DEP)
DEP_recompose      := $(COMMON_DEP)
DEP_generated_rom  := $(COMMON_DEP)
INC_write_control  := $(INCLUDES)
INC_unserialize    := $(INCLUDES)
INC_recompose      := $(INCLUDES)
INC_generated_rom  := $(INCLUDES)

# audio_ring no necesita el core, asi que tampoco necesita CORE=.
NEEDS_CORE_audio_ring := 0

TARGETS := write_control unserialize recompose generated_rom audio_ring

# Sanitizers tambien en el build de replay: reproducir un crash sin ASan es
# reproducir el sintoma y no la causa.
#
# En Windows quedan afuera porque el toolchain llvm-mingw que usa este fork no
# trae el runtime de ASan (no existe libclang_rt.asan_dynamic para el target
# x86_64-w64-windows-gnu). El replay sigue valiendo ahi -- corre el corpus y
# caza abortos y crashes-, pero sin la lupa. La lupa la pone Linux, que es
# donde corre el job nocturno.
ifeq ($(OS),Windows_NT)
REPLAY_SAN ?=
else
REPLAY_SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
endif

.PHONY: all fuzz-all replay-all check clean $(addprefix fuzz-,$(TARGETS)) \
	$(addprefix replay-,$(TARGETS))

all: replay-all

$(BUILD_DIR):
	$(call make_dir,$(BUILD_DIR))

# --- build con libFuzzer (clang) ------------------------------------------
# `-shared-libsan` no es un detalle de gusto: sin el, clang linkea el runtime de
# ASan ESTATICO dentro del ejecutable, y cuatro de los cinco targets hacen
# `dlopen` de un core que el job nocturno compila con `-shared-libsan`. Las dos
# copias del runtime no conviven y el proceso muere antes del primer caso, con
# "Your application is linked against incompatible ASan runtimes" -- que no es
# un hallazgo del fuzzer sino el target roto, y se veia igual de rojo.
#
# El workflow ya exportaba `LD_LIBRARY_PATH=$(clang -print-runtime-dir)` para
# encontrar ese runtime al ejecutar; faltaba la mitad que lo pide. `audio_ring`
# no carga el core y pasaba: por eso el job fallaba 4 de 5 y no 5 de 5, que es
# la pista de que el problema estaba en el `dlopen` y no en el codigo.
define FUZZ_RULE
$(BUILD_DIR)/fuzz_$(1)$(EXE_EXT): fuzz_$(1).c $$(SRC_$(1)) $$(DEP_$(1)) | $(BUILD_DIR)
	$$(FUZZ_CC) -O1 -g -Wall -Wextra $$(CPPFLAGS) $$(INC_$(1)) \
		-fsanitize=fuzzer,address,undefined -shared-libsan \
		-fno-omit-frame-pointer \
		-o $$@ fuzz_$(1).c $$(SRC_$(1)) $$(DL_LIBS)

fuzz-$(1): $$(BUILD_DIR)/fuzz_$(1)$$(EXE_EXT)
	AYTHER_FUZZ_CORE="$$(CORE)" $$(BUILD_DIR)/fuzz_$(1)$$(EXE_EXT) \
		corpus/$(1) -max_total_time=$$(FUZZ_SECONDS) -print_final_stats=1
endef
$(foreach t,$(TARGETS),$(eval $(call FUZZ_RULE,$(t))))

FUZZ_SECONDS ?= 60

# --- build de replay (cualquier compilador) -------------------------------
define REPLAY_RULE
$(BUILD_DIR)/replay_$(1)$(EXE_EXT): fuzz_$(1).c fuzz_replay.c $$(SRC_$(1)) $$(DEP_$(1)) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) $$(REPLAY_SAN) $$(CPPFLAGS) $$(INC_$(1)) \
		-o $$@ fuzz_$(1).c fuzz_replay.c $$(SRC_$(1)) \
		$$(LDFLAGS) $$(REPLAY_SAN) $$(DL_LIBS)

replay-$(1): $$(BUILD_DIR)/replay_$(1)$$(EXE_EXT)
	@echo "== $(1) =="
	$$(BUILD_DIR)/replay_$(1)$$(EXE_EXT) "$$(CORE)" corpus/$(1) regressions/$(1)
endef
$(foreach t,$(TARGETS),$(eval $(call REPLAY_RULE,$(t))))

fuzz-all: $(addprefix fuzz-,$(TARGETS))
replay-all: $(addprefix replay-,$(TARGETS))

# Lo que corre en `make -C tests check`.
check: replay-all

clean:
	$(call remove_build_dir,$(BUILD_DIR))
