# Detección de shell y sufijo de ejecutable, compartida por tests/, bench/ y el
# Makefile del core (#43).
#
# EL NOMBRE DEL BINARIO DEPENDE DEL SISTEMA; LAS RECETAS, DEL SHELL. No es la
# misma pregunta, y confundirlas es lo que rompía `make check` en el runner de
# Windows: si hay un `sh` en el PATH —Git for Windows lo instala y los runners
# de GitHub lo traen— GNU make lo elige como SHELL, y entonces cada receta
# escrita en sintaxis de cmd muere con «syntax error near unexpected token».
# Decidir ambas cosas por $(OS) hacía que el Makefile hablara cmd a un shell
# POSIX.
#
# El bloque estaba copiado en tres Makefiles y se corrigió dos veces por
# separado; la tercera copia se enteró tarde. Por eso vive acá.
#
# Uso:
#   include $(dir $(lastword $(MAKEFILE_LIST)))../mk/shell.mk    (desde tests/)
# y después $(EXE_EXT), $(call make_dir,DIR), $(call remove_build_dir,DIR).

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
THREAD_FLAGS :=
else
EXE_EXT :=
THREAD_FLAGS := -pthread
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
