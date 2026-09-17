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

# --------------------------------------------------------------------------
# Que SABOR de shell habla el shell que eligio make (#104).
#
# Esto era `$(findstring sh,$(notdir $(SHELL)))`, y tenia un falso positivo
# que no se ve leyendolo: "pwsh" y "powershell" CONTIENEN "sh". Con
# SHELL=pwsh.exe -- que es el shell con el que la CI corre los pasos de
# Windows-- la deteccion elegia la rama POSIX y le mandaba `test -n ... ||
# (echo ...; exit 2)` a PowerShell, que contesta "The term 'test' is not
# recognized" y "Missing file specification after redirection operator".
# Peor que el ruido: la receta terminaba en 1 en vez de 2, asi que el
# rechazo de un CORE ausente dejaba de ser distinguible de cualquier otra
# falla.
#
# Ahora se decide por LISTA BLANCA de nombres conocidos, no por subcadena.
# Una subcadena responde "se parece a"; lo que hace falta saber es "es".
#
# Y si el shell no esta en ninguna de las dos listas se FALLA, en vez de
# adivinar: las recetas de este archivo existen en dos idiomas y nada mas,
# asi que un shell que no hable ninguno no puede salir bien -- solo puede
# salir mal mas tarde y mas lejos, que es como se descubrio este bug.
# AYTHER_SHELL_FLAVOR=posix|cmd es la salida de emergencia para un shell
# compatible que no este en la lista.
# AYTHER_SHELL_PROBE existe para poder PROBAR esta deteccion
# (tests/ci/check_shell_detection.sh). No es un hook de conveniencia: en
# Windows, make DESCARTA un SHELL que no exista en disco y pone el suyo, asi
# que un test que preguntara por "pwsh.exe" recibia "sh.exe" y pasaba sin
# probar nada -- decia ok pasara lo que pasara. Con la costura, la tabla se
# prueba entera y aparte queda un caso que usa el $(SHELL) de verdad para
# verificar que el cableado sigue conectado.
AYTHER_SHELL_PROBE ?= $(SHELL)
SHELL_PATH := $(subst \,/,$(AYTHER_SHELL_PROBE))
# Ojo con $(notdir): parte por ESPACIOS, asi que una ruta como
# "C:/Program Files/Git/usr/bin/sh.exe" le sale "Program Files sh" y la lista
# blanca no matchea nada -- romperia justo los setups de Windows que hoy andan.
# Partir por / y quedarse con la ultima palabra si aguanta los espacios.
SHELL_NAME := $(basename $(lastword $(subst /, ,$(SHELL_PATH))))

AYTHER_POSIX_SHELLS := sh bash dash ash ksh ksh93 mksh zsh busybox
AYTHER_CMD_SHELLS   := cmd command CMD COMMAND Cmd

ifneq ($(AYTHER_SHELL_FLAVOR),)
SHELL_FLAVOR := $(AYTHER_SHELL_FLAVOR)
else ifneq ($(filter $(SHELL_NAME),$(AYTHER_POSIX_SHELLS)),)
SHELL_FLAVOR := posix
else ifneq ($(filter $(SHELL_NAME),$(AYTHER_CMD_SHELLS)),)
SHELL_FLAVOR := cmd
else
SHELL_FLAVOR := desconocido
endif

ifeq ($(SHELL_FLAVOR),desconocido)
$(error make eligio SHELL=$(SHELL) y no se de que sabor es. Las recetas de mk/shell.mk existen en POSIX sh y en cmd, y nada mas. Si $(SHELL_NAME) es compatible con alguno, decilo: make AYTHER_SHELL_FLAVOR=posix ... (o cmd). Ojo que pwsh y powershell NO son ninguno de los dos.)
endif

ifneq ($(filter-out posix cmd,$(SHELL_FLAVOR)),)
$(error AYTHER_SHELL_FLAVOR=$(SHELL_FLAVOR) no es un valor valido: posix o cmd)
endif

# --------------------------------------------------------------------------
# El nombre no alcanza: en Windows hay que PREGUNTARLE al shell (#114).
#
# Desde PowerShell -- con o sin Git Bash en el PATH-- GNU make 4.4.1 informa
# `$(SHELL) = sh.exe`, que es su default y no una eleccion, y despues ejecuta
# las recetas con cmd.exe. La lista blanca de arriba leia "sh" y elegia la
# rama POSIX, y `mkdir -p` le llegaba a cmd:
#
#   Ya existe el subdirectorio o el archivo -p.
#   make: *** [Makefile:96: .build/...] Error 1
#
# Con SHELL=cmd.exe en la linea de comandos pasaba, porque ahi el nombre y el
# shell real coincidian. #104 arreglo el caso contrario (pwsh explicito); este
# es el nombre diciendo sh y el shell siendo cmd.
#
# La unica fuente de verdad es el shell que make usa para $(shell ...), que
# es el mismo que para las recetas: cmd EXPANDE %COMSPEC% y un shell POSIX lo
# devuelve tal cual. Un proceso al parsear, solo en Windows, y solo cuando
# nadie forzo el sabor ni esta probando la tabla por AYTHER_SHELL_PROBE (esa
# costura existe para preguntar por shells que no estan en esta maquina, y
# preguntarle al shell real la volveria inutil). Con pwsh explicito no se
# llega aca: la lista blanca ya fallo antes.
ifeq ($(OS),Windows_NT)
ifeq ($(AYTHER_SHELL_FLAVOR),)
ifeq ($(AYTHER_SHELL_PROBE),$(SHELL))
AYTHER_SHELL_ECHO := $(shell echo %COMSPEC%)
ifeq ($(findstring %COMSPEC%,$(AYTHER_SHELL_ECHO)),)
SHELL_FLAVOR := cmd
else
SHELL_FLAVOR := posix
endif
endif
endif
endif

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
THREAD_FLAGS :=
else
EXE_EXT :=
THREAD_FLAGS := -pthread
endif

# `require_core` vive aca por la misma razon que lo demas: tests/ lo tenia y
# tests/fuzz/ lo necesita igual (#34). Copiarlo habria sido la tercera copia
# de un bloque que este archivo existe para no tener repetido.
ifeq ($(SHELL_FLAVOR),posix)
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

ifeq ($(SHELL_FLAVOR),posix)
define copy_file
	@cp -f "$1" "$2"
endef
else
define copy_file
	@copy /Y "$(subst /,\,$1)" "$(subst /,\,$2)" >NUL
endef
endif

ifeq ($(SHELL_FLAVOR),posix)
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
