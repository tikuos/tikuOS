# ===========================================================================
# TikuOS Makefile
#
# Usage:
#   make MCU=msp430fr5994                      — build for FR5994
#   make flash MCU=msp430fr5994                — compile + flash
#   make flash MCU=msp430fr5994 DEBUGGER=tilib — explicit debugger
#   make debug MCU=msp430fr5994                — compile + start GDB server
#   make run MCU=msp430fr5994                  — alias for flash
#   make erase MCU=msp430fr5994                — erase chip
#   make monitor                               — open serial console (auto-detect)
#   make monitor PORT=/dev/ttyACM1 BAUD=9600   — explicit port/baud
#   make clean                                 — clean build artifacts
# ===========================================================================

# Pin the default goal to `all`. Some prerequisite rules (e.g. the msp430
# debug-stripped libnosys) are defined before the `all:` target; without this
# the first such rule would silently become the default goal, so a bare `make`
# would build only that helper and leave main.elf to the flash step.
.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Target MCU  (override on command line: make MCU=msp430fr5994 / MCU=rp2350)
# Accepts uppercase (MSP430FR5969 / RP2350) or lowercase MCU names.
# ---------------------------------------------------------------------------
MCU ?= $(mcu)
ifeq ($(MCU),)
MCU = msp430fr5994          # a bare `make` builds a supported target (FR2433 no longer fits)
endif
MCU := $(shell echo $(MCU) | tr '[:upper:]' '[:lower:]')

# Derive PLATFORM from MCU prefix.
#   msp430* -> PLATFORM_MSP430 (default historical target)
#   rp2350  -> PLATFORM_RP2350 (Raspberry Pi Pico 2 / Pico 2 W)
ifeq ($(MCU),rp2350)
TIKU_PLATFORM := rp2350
else ifeq ($(MCU),apollo510)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo4l)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo4p)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo510b)
# Apollo510 Blue EVB: the SAME Apollo510 (Cortex-M55) silicon as apollo510 --
# same register map, linker, J-Link device and arch backends (it inherits them
# all via the apollo510 `else` branches below). The board just adds an EM9305
# BLE radio (a later, SPI-gated effort); bring-up is identical to apollo510.
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),nrf54l15)
TIKU_PLATFORM := nordic
else ifeq ($(MCU),nrf54lm20a)
TIKU_PLATFORM := nordic
else ifeq ($(MCU),nrf54lm20b)
TIKU_PLATFORM := nordic
else
TIKU_PLATFORM := msp430
endif

# ---------------------------------------------------------------------------
# Console channel (RP2350 only): uart (default; debug/console over UART0 to an
# external FT232) | usb (native USB CDC-ACM on the programming connector) |
# both (mirror to UART + USB). The selection is consumed by hal/tiku_printf_hal.h,
# the shell I/O backend, and boot.  usb/both compile arch/arm-rp2350/
# tiku_usb_cdc_arch.c and define TIKU_CONSOLE_USB (+ TIKU_CONSOLE_BOTH).
# ---------------------------------------------------------------------------
TIKU_CONSOLE ?= uart
ifeq ($(filter uart usb both,$(TIKU_CONSOLE)),)
$(error TIKU_CONSOLE must be uart, usb, or both (got '$(TIKU_CONSOLE)'))
endif
ifneq ($(filter usb both,$(TIKU_CONSOLE)),)
ifneq ($(TIKU_PLATFORM),rp2350)
$(error TIKU_CONSOLE=$(TIKU_CONSOLE) (USB CDC console) is only supported on \
rp2350; this build is $(TIKU_PLATFORM). Use TIKU_CONSOLE=uart, or MCU=rp2350)
endif
endif

# ---------------------------------------------------------------------------
# Board selection (RP2350 only — MSP430 boards are 1:1 with MCU=)
#
# BOARD picks which board header arch/arm-rp2350/boards/tiku_board_*.h
# is pulled in by the device selector. The two RP2350 boards differ
# in:
#   - Pi Pico 2     (BOARD=pico2)   plain board, LED on GP25, no
#                                   CYW43 module — TIKU_DRV_WIFI_*
#                                   not available.
#   - Pi Pico 2 W   (BOARD=pico2w)  default — adds the CYW43439
#                                   wireless module on GP23..25 +
#                                   GP29; LED is behind the chip so
#                                   the board header reroutes LED1
#                                   to GP15 when the WiFi driver is
#                                   compiled in.
#
# Override on the make line, e.g.:  make MCU=rp2350 BOARD=pico2
# ---------------------------------------------------------------------------
BOARD ?= $(board)
ifeq ($(BOARD),)
BOARD = pico2w
endif
BOARD := $(shell echo $(BOARD) | tr '[:upper:]' '[:lower:]')

ifeq ($(TIKU_PLATFORM),rp2350)
ifeq ($(BOARD),pico2w)
TIKU_BOARD_DEFINE := TIKU_BOARD_RPI_PICO2_W
else ifeq ($(BOARD),pico2)
TIKU_BOARD_DEFINE := TIKU_BOARD_RPI_PICO2
else
$(error Unknown BOARD=$(BOARD) for MCU=rp2350. Valid: pico2, pico2w)
endif
endif

ifeq ($(TIKU_PLATFORM),ambiq)
ifeq ($(MCU),apollo510)
TIKU_BOARD_DEFINE := TIKU_BOARD_APOLLO510_EVB
else ifeq ($(MCU),apollo510b)
# Same Apollo510 silicon as apollo510, but the Blue EVB has its own pinout:
# console on UART1 (pads 12/14, funcsel 5), LEDs 11/19/83, buttons 46/29 --
# see tiku_board_apollo510b_evb.h + the TIKU_CONSOLE_UART1 gate below.
TIKU_BOARD_DEFINE := TIKU_BOARD_APOLLO510B_EVB
else
# Apollo4 Lite and Apollo4 Plus EVBs share the M4F board pinout (console UART,
# LEDs, buttons); apollo4p reuses the apollo4l board config for bring-up.
TIKU_BOARD_DEFINE := TIKU_BOARD_APOLLO4L_EVB
endif
endif

ifeq ($(TIKU_PLATFORM),nordic)
# One board per device: nrf54l15 -> nRF54L15-DK (PCA10156);
# nrf54lm20a/nrf54lm20b -> nRF54LM20-DK (PCA10184; the DK ships LM20B silicon,
# both images run on it).
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
TIKU_BOARD_DEFINE := TIKU_BOARD_NRF54LM20_DK
else
TIKU_BOARD_DEFINE := TIKU_BOARD_NRF54L15_DK
endif
endif

# ---------------------------------------------------------------------------
# Apollo510 register headers are VENDORED in-tree at arch/ambiq/cmsis/ (CMSIS
# device map + ARM CMSIS-Core). The build references no external AmbiqSuite
# tree -- only the MRAM bootrom blob. See arch/ambiq/cmsis/PROVENANCE.md.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Driver-vs-MCU compatibility gates
#
# Some drivers are bound to a specific silicon family because they use
# platform-specific peripherals at the C level (e.g. RP2350 PIO + SIO
# atomic aliases). Compiling them against the wrong MCU produces a
# confusing wall of compiler errors several minutes in; better to
# reject up-front with a clear message.
#
# Whenever a new driver gets a platform binding, add its check here.
# ---------------------------------------------------------------------------

# CYW43439 (Pi Pico 2 W) — driver uses PIO1 + RP2350 SIO atomic aliases
# + RP2350-specific register layouts AND the CYW43 module's pinout
# (GP23..25 + GP29). The plain Pi Pico 2 doesn't carry the module, so
# the driver requires BOTH the right silicon and the right PCB.
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
ifneq ($(TIKU_PLATFORM),rp2350)
$(error TIKU_DRV_WIFI_CYW43_ENABLE=1 requires MCU=rp2350 \
(currently MCU=$(MCU)). The CYW43439 driver depends on \
RP2350-specific PIO + SIO peripherals and only runs on Pi Pico \
2 W hardware. For other MCUs, omit TIKU_DRV_WIFI_CYW43_ENABLE.)
endif
ifneq ($(BOARD),pico2w)
$(error TIKU_DRV_WIFI_CYW43_ENABLE=1 requires BOARD=pico2w \
(currently BOARD=$(BOARD)). The CYW43439 module is only present on \
the Pi Pico 2 W; the plain Pi Pico 2 has no WiFi hardware. Either \
build with BOARD=pico2w, or drop TIKU_DRV_WIFI_CYW43_ENABLE.)
endif
endif

# CYW43439 BT extension — same chip + module, so it inherits the
# rp2350/pico2w platform requirements implicitly through
# TIKU_DRV_WIFI_CYW43_ENABLE, which it requires. Surfaced as its own
# flag so a user can opt into WiFi without paying for the BT
# bring-up + transport code path (~1-2 KB of program code; the BT
# firmware blob itself ships in firmware.S unconditionally).
ifeq ($(TIKU_DRV_WIFI_CYW43_BT_ENABLE),1)
ifneq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
$(error TIKU_DRV_WIFI_CYW43_BT_ENABLE=1 requires \
TIKU_DRV_WIFI_CYW43_ENABLE=1. BT bring-up reuses the WiFi driver's \
gSPI transport + backplane primitives, and the WLAN firmware must \
be running before the BT side can be powered up. Add \
TIKU_DRV_WIFI_CYW43_ENABLE=1 too, or drop TIKU_DRV_WIFI_CYW43_BT_ENABLE.)
endif
endif

# Derive device define from MCU: msp430fr2433 -> TIKU_DEVICE_MSP430FR2433,
# rp2350 -> TIKU_DEVICE_RP2350. The RP2350 board (Pico 2 W) is hard-wired
# for now; later boards can switch via TIKU_BOARD_*.
DEVICE_UPPER = $(shell echo $(MCU) | tr '[:lower:]' '[:upper:]')
DEVICE_DEFINE = TIKU_DEVICE_$(DEVICE_UPPER)

# ---------------------------------------------------------------------------
# Toolchain
#
# msp430:  msp430-elf-gcc auto-detected from PATH (or $(HOME)/tigcc)
# rp2350:  arm-none-eabi-gcc auto-detected from PATH
# apollo510: arm-none-eabi-gcc auto-detected from PATH
# nrf54l15:  arm-none-eabi-gcc auto-detected from PATH (Cortex-M33)
# ---------------------------------------------------------------------------
ifneq (,$(filter $(TIKU_PLATFORM),rp2350 ambiq nordic))

# ARM Embedded toolchain (apt: gcc-arm-none-eabi).
TOOLCHAIN_PREFIX ?= arm-none-eabi-
TOOLCHAIN_DIR    ?= $(shell \
	p=$$(command -v $(TOOLCHAIN_PREFIX)gcc 2>/dev/null) && dirname $$(dirname "$$p") \
	|| echo "/usr")
CC      = $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)gcc
OBJCOPY = $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)objcopy
SIZE    = $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)size
GDB     = $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)gdb
MSP430_SUPPORT_DIR :=

else

TOOLCHAIN_DIR ?= $(shell \
	p=$$(command -v msp430-elf-gcc 2>/dev/null) && dirname $$(dirname "$$p") \
	|| echo "$(HOME)/tigcc")

CC            = $(TOOLCHAIN_DIR)/bin/msp430-elf-gcc
OBJCOPY       = $(TOOLCHAIN_DIR)/bin/msp430-elf-objcopy
SIZE          = $(TOOLCHAIN_DIR)/bin/msp430-elf-size
GDB           = $(TOOLCHAIN_DIR)/bin/msp430-elf-gdb

# Auto-detect MSP430 GCC support files (msp430.h, linker scripts)
MSP430_SUPPORT_DIR ?= $(shell \
	find $(TOOLCHAIN_DIR)/include -type f -name "msp430.h" 2>/dev/null \
	| head -1 | xargs -r dirname)

endif

# ---------------------------------------------------------------------------
# Debug / Flash tools  (auto-detected from PATH; override with MSPDEBUG=…)
# ---------------------------------------------------------------------------
MSPDEBUG ?= $(shell command -v mspdebug 2>/dev/null || echo mspdebug)
DEBUGGER  = tilib

# picotool / openocd for the RP2350 path. picotool is preferred when
# available because it can use BOOTSEL or an installed Debug Probe; we
# fall back to "drag-and-drop the UF2 onto the RPI-RP2 mass storage".
PICOTOOL ?= $(shell command -v picotool 2>/dev/null || echo picotool)

# Apollo510 (Ambiq) SEGGER J-Link settings. Defaults match the AmbiqSuite
# hello_world flash.jlink: device AP510NFA-CBR, SWD @4 MHz, image loaded to
# MRAM 0x00410000. Override any of these on the make command line.
JLINK           ?= JLinkExe
JLINK_GDB       ?= JLinkGDBServer
JLINK_IF        ?= SWD
JLINK_SPEED     ?= 4000
# Select a specific J-Link probe by its serial number.  Every SEGGER J-Link
# reports the same USB VID (0x1366), so on a rig with several Ambiq EVBs the
# probe serial is the only thing that tells them apart -- pass
# `make flash MCU=apollo4l JLINK_SN=001160001290` to flash exactly that board.
# Empty (the default) lets JLinkExe pick the sole connected probe.
JLINK_SN        ?=
JLINK_SN_ARG    := $(if $(strip $(JLINK_SN)),-SelectEmuBySN $(strip $(JLINK_SN)),)
# J-Link device + MRAM load address differ per Ambiq part.
ifeq ($(MCU),apollo4l)
JLINK_DEVICE    ?= AMAP42KL-KBR
AMBIQ_LOAD_ADDR ?= 0x00018000
# The Apollo4 Lite secure SBL parks (PC stays inside the SBL) while a debugger
# is attached at reset. Detaching with the target left running (qc) drops the
# debugger so the SBL hands off to the app at 0x18000; the Sleep lets the SBL
# reach that debug-wait before we detach. (q halts, so the app never starts.)
JLINK_RUN_SEQ   ?= r\ng\nSleep 600\nqc
else ifeq ($(MCU),apollo4p)
# Apollo4 Plus (AMAP42KP-KBR): same M4F family + SBL hand-off as the Lite, just
# a different J-Link flash device (2 MB MRAM vs 1 MB).
JLINK_DEVICE    ?= AMAP42KP-KBR
AMBIQ_LOAD_ADDR ?= 0x00018000
JLINK_RUN_SEQ   ?= r\ng\nSleep 600\nqc
else
JLINK_DEVICE    ?= AP510NFA-CBR
AMBIQ_LOAD_ADDR ?= 0x00410000
# Apollo510 hands off cleanly: reset, go, quit.
JLINK_RUN_SEQ   ?= r\ng\nq
endif

# Whether this MCU has HIFRAM (FRAM > 64 KB).  MSP430-only concept;
# for the RP2350 it is meaningless.
ifeq ($(MCU),msp430fr5994)
DEVICE_HAS_HIFRAM := 1
else ifeq ($(MCU),msp430fr6989)
DEVICE_HAS_HIFRAM := 1
else
DEVICE_HAS_HIFRAM := 0
endif

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
PROJ_DIR  = $(CURDIR)
BUILD_DIR = build/$(MCU)

# ---------------------------------------------------------------------------
# App selection (mutually exclusive with tests and examples)
#   make APP=cli MCU=msp430fr5994   — build with CLI app
#   make MCU=msp430fr5994           — default (tests/examples as before)
# ---------------------------------------------------------------------------
APP ?=

# App firmware sources (formerly the in-tree apps/ dir) now live OUT of core
# tikuOS, in the TikuBench harness.  The build that drives `make APP=net` or
# `TIKU_TURBO_BENCH=1` passes their location in via TIKU_APP_DIR (TikuBench
# exports it from tikubench/__init__.py; it may also be given on the make
# line).  Empty by default, so a bare `make APP=net` with no harness fails
# loudly (see the guard in the Apps section) instead of silently missing the
# source file.
TIKU_APP_DIR ?=

# ---------------------------------------------------------------------------
# Shell service (kernel service — orthogonal to APP/tests/examples)
#   make TIKU_SHELL_ENABLE=1 MCU=msp430fr5994   — build with shell
#   make APP=cli MCU=msp430fr5994                — legacy alias (also enables shell)
#
# Optional shell add-ons (off by default; opt in alongside the shell):
#   TIKU_SHELL_BASIC_ENABLE=1   — Tiku BASIC interpreter REPL
#                                 (~3.5 KB code + ~1.3 KB arena)
#   TIKU_SHELL_COLOR=1          — ANSI color output
# ---------------------------------------------------------------------------
TIKU_SHELL_ENABLE ?= 1
TIKU_SHELL_COLOR  ?= 0
# BASIC defaults ON for Apollo510 (ample DTCM; its memory tiers are sized for it
# in the ambiq CFLAGS block), OFF elsewhere (MSP430 needs MEMORY_MODEL=large;
# RP2350 stays opt-in). Override on the make line as usual.
TIKU_SHELL_BASIC_ENABLE ?= $(if $(filter ambiq,$(TIKU_PLATFORM)),1,0)
TIKU_INIT_ENABLE  ?= 0
TIKU_INIT_TEST    ?= 0

# Init tests require the init system
ifeq ($(TIKU_INIT_TEST),1)
TIKU_INIT_ENABLE = 1
endif

# Legacy: APP=cli enables the kernel shell.
# `override` is required so this still wins when callers explicitly pass
# TIKU_SHELL_ENABLE=0 on the command line (e.g. TikuBench's slim test builds).
ifeq ($(APP),cli)
override TIKU_SHELL_ENABLE := 1
endif

# Init system requires the shell (for parser).
# `override` ensures TIKU_INIT_TEST=1 categories get the shell even when the
# caller passes TIKU_SHELL_ENABLE=0 to slim down the default test build.
ifeq ($(TIKU_INIT_ENABLE),1)
override TIKU_SHELL_ENABLE := 1
endif

# ---------------------------------------------------------------------------
# Scratch demos (mutually exclusive with apps/tests/examples)
#   make DEMO=ping_a_host_to_pico MCU=rp2350 ...
# `demos/` is in .gitignore and excluded from public builds; this block
# is a no-op when the directory doesn't exist.
# ---------------------------------------------------------------------------
DEMO ?=

# ---------------------------------------------------------------------------
# Optional components (opt-in; override: make HAS_TESTS=1 HAS_EXAMPLES=1)
# Tests and examples are EXCLUDED by default — plain `make` builds only the
# core OS (plus any explicitly enabled services like the shell).
# When APP or DEMO is set, tests and examples are forced off.
# ---------------------------------------------------------------------------
ifneq ($(APP),)
HAS_APPS         = 1
HAS_TESTS        = 0
HAS_EXAMPLES     = 0
HAS_DEMOS        = 0
else ifneq ($(DEMO),)
HAS_APPS         = 0
HAS_TESTS        = 0
HAS_EXAMPLES     = 0
HAS_DEMOS        = 1
else
HAS_APPS         = 0
HAS_TESTS        ?= 0
HAS_EXAMPLES     ?= 0
HAS_DEMOS        = 0
endif
HAS_TIKUKITS     ?= $(if $(wildcard $(PROJ_DIR)/tikukits),1,0)
HAS_DRIVERS      ?= $(if $(wildcard $(PROJ_DIR)/drivers),1,0)
HAS_PRESENTATION ?= $(if $(wildcard $(PROJ_DIR)/presentation/Makefile),1,0)

# ---------------------------------------------------------------------------
# Per-kit enable flags
#
# Each kit under tikukits/ is opt-in: its sources are compiled only
# when its TIKU_KIT_<NAME>_ENABLE flag is 1. Default is 0, so a
# kernel-only build (e.g. `make APP=cli` or just `make` with no
# example flags) does not compile any kit code at all.
#
# Three ways the flags get set:
#   1. The user passes them on the command line, e.g.
#        make TIKU_KIT_DS_ENABLE=1
#   2. An app or example block below auto-enables them when its own
#      TIKU_EXAMPLE_* or APP= flag is set.
#   3. TIKU_KITS_ALL=1 enables every kit at once (compatibility
#      shim for the old "compile everything" behaviour).
# ---------------------------------------------------------------------------
TIKU_KIT_GFX_ENABLE              ?= 0
TIKU_KIT_UI_ENABLE               ?= 0
TIKU_KIT_EPAPER_ENABLE           ?= 0
TIKU_KIT_NET_ENABLE              ?= 0
TIKU_KIT_CRYPTO_ENABLE           ?= 0
TIKU_KIT_TIME_ENABLE             ?= 0
TIKU_KIT_CODEC_ENABLE            ?= 0
TIKU_KIT_MATHS_ENABLE            ?= 0
TIKU_KIT_DS_ENABLE               ?= 0
TIKU_KIT_ML_ENABLE               ?= 0
TIKU_KIT_SENSORS_ENABLE          ?= 0
TIKU_KIT_SIGFEATURES_ENABLE      ?= 0
TIKU_KIT_TEXTCOMPRESSION_ENABLE  ?= 0

# BASIC JSON$ wraps the json codec, so a BIG (Cortex-M) BASIC build pulls it in
# automatically -- offline JSON parsing plus API/LLM replies from HTTPGET$.
# MSP430/FRAM BASIC gates JSON$ off (config), so it stays codec-free there.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
ifneq ($(filter ambiq rp2350,$(TIKU_PLATFORM)),)
TIKU_KIT_CODEC_ENABLE            := 1
endif
endif

ifeq ($(TIKU_KITS_ALL),1)
TIKU_KIT_GFX_ENABLE              := 1
TIKU_KIT_UI_ENABLE               := 1
TIKU_KIT_EPAPER_ENABLE           := 1
TIKU_KIT_NET_ENABLE              := 1
TIKU_KIT_CRYPTO_ENABLE           := 1
TIKU_KIT_TIME_ENABLE             := 1
TIKU_KIT_CODEC_ENABLE            := 1
TIKU_KIT_MATHS_ENABLE            := 1
TIKU_KIT_DS_ENABLE               := 1
TIKU_KIT_ML_ENABLE               := 1
TIKU_KIT_SENSORS_ENABLE          := 1
TIKU_KIT_SIGFEATURES_ENABLE      := 1
TIKU_KIT_TEXTCOMPRESSION_ENABLE  := 1
endif

# UI depends on GFX -- enabling UI implies GFX.
ifeq ($(TIKU_KIT_UI_ENABLE),1)
TIKU_KIT_GFX_ENABLE              := 1
endif

# ---------------------------------------------------------------------------
# Auto-enable kits based on which example or app is active.
#
# Each example only pulls in the kits it actually uses, so a build
# of `make TIKU_EXAMPLE_GFX_DEMO=1` does NOT compile tikukits/ui,
# tikukits/net, etc.
# ---------------------------------------------------------------------------

# Sensors-only examples.
ifeq ($(TIKU_EXAMPLE_I2C_TEMP),1)
TIKU_KIT_SENSORS_ENABLE := 1
endif
ifeq ($(TIKU_EXAMPLE_DS18B20_TEMP),1)
TIKU_KIT_SENSORS_ENABLE := 1
endif

# Networking examples (12..18) all need NET; HTTPS also needs CRYPTO.
ifneq ($(filter 1, $(TIKU_EXAMPLE_UDP_SEND) $(TIKU_EXAMPLE_TCP_SEND) \
                  $(TIKU_EXAMPLE_DNS_RESOLVE) $(TIKU_EXAMPLE_HTTP_GET) \
                  $(TIKU_EXAMPLE_TCP_ECHO) $(TIKU_EXAMPLE_HTTP_FETCH) \
                  $(TIKU_EXAMPLE_HTTP_DIRECT)),)
TIKU_KIT_NET_ENABLE     := 1
endif
ifeq ($(TIKU_EXAMPLE_HTTPS_DIRECT),1)
TIKU_KIT_NET_ENABLE     := 1
TIKU_KIT_CRYPTO_ENABLE  := 1
endif

# Display-stack demos (24..28 + new kits_examples gfx/ui demos).
ifneq ($(filter 1, $(TIKU_EXAMPLE_EPAPER) $(TIKU_EXAMPLE_EPAPER_KIT)),)
TIKU_KIT_EPAPER_ENABLE  := 1
endif
ifneq ($(filter 1, $(TIKU_EXAMPLE_GFX_DEMO) $(TIKU_EXAMPLE_GFX_CURVES) \
                  $(TIKU_EXAMPLE_GFX_IMAGE) $(TIKU_EXAMPLE_GFX_PHASE0)),)
TIKU_KIT_GFX_ENABLE     := 1
TIKU_KIT_EPAPER_ENABLE  := 1
endif
ifneq ($(filter 1, $(TIKU_EXAMPLE_UI_DEMO) $(TIKU_EXAMPLE_UI_WIDGET_ZOO) \
                  $(TIKU_EXAMPLE_UI_DASHBOARD) $(TIKU_EXAMPLE_UI_MENU) \
                  $(TIKU_EXAMPLE_UI_LAYOUT) $(TIKU_EXAMPLE_UI_SETTINGS)),)
TIKU_KIT_UI_ENABLE      := 1
TIKU_KIT_GFX_ENABLE     := 1
TIKU_KIT_EPAPER_ENABLE  := 1
endif

# tikukits-library exercise demos (examples/kits/...).
ifneq ($(filter 1, $(TIKU_EXAMPLE_KITS_MATRIX) \
                  $(TIKU_EXAMPLE_KITS_STATISTICS) \
                  $(TIKU_EXAMPLE_KITS_DISTANCE)),)
TIKU_KIT_MATHS_ENABLE   := 1
endif
ifneq ($(filter 1, $(TIKU_EXAMPLE_KITS_DS_ARRAY) \
                  $(TIKU_EXAMPLE_KITS_DS_BITMAP) $(TIKU_EXAMPLE_KITS_DS_BTREE) \
                  $(TIKU_EXAMPLE_KITS_DS_HTABLE) $(TIKU_EXAMPLE_KITS_DS_LIST) \
                  $(TIKU_EXAMPLE_KITS_DS_PQUEUE) $(TIKU_EXAMPLE_KITS_DS_QUEUE) \
                  $(TIKU_EXAMPLE_KITS_DS_RINGBUF) $(TIKU_EXAMPLE_KITS_DS_SM) \
                  $(TIKU_EXAMPLE_KITS_DS_SORTARRAY) \
                  $(TIKU_EXAMPLE_KITS_DS_STACK)),)
TIKU_KIT_DS_ENABLE      := 1
endif
ifneq ($(filter 1, $(TIKU_EXAMPLE_KITS_ML_DTREE) $(TIKU_EXAMPLE_KITS_ML_KNN) \
                  $(TIKU_EXAMPLE_KITS_ML_LINREG) \
                  $(TIKU_EXAMPLE_KITS_ML_LINSVM) \
                  $(TIKU_EXAMPLE_KITS_ML_LOGREG) \
                  $(TIKU_EXAMPLE_KITS_ML_NBAYES) \
                  $(TIKU_EXAMPLE_KITS_ML_TNN)),)
TIKU_KIT_ML_ENABLE      := 1
TIKU_KIT_MATHS_ENABLE   := 1
endif
ifeq ($(TIKU_EXAMPLE_KITS_SENSORS),1)
TIKU_KIT_SENSORS_ENABLE := 1
endif
ifeq ($(TIKU_EXAMPLE_KITS_SIGFEATURES),1)
TIKU_KIT_SIGFEATURES_ENABLE := 1
TIKU_KIT_MATHS_ENABLE       := 1
endif
ifeq ($(TIKU_EXAMPLE_KITS_TEXTCOMPRESSION),1)
TIKU_KIT_TEXTCOMPRESSION_ENABLE := 1
endif
ifneq ($(filter 1, $(TIKU_EXAMPLE_KITS_NET_IPV4) \
                  $(TIKU_EXAMPLE_KITS_NET_UDP) \
                  $(TIKU_EXAMPLE_KITS_NET_TFTP) \
                  $(TIKU_EXAMPLE_KITS_NET_TCP) \
                  $(TIKU_EXAMPLE_KITS_NET_DNS) \
                  $(TIKU_EXAMPLE_KITS_NET_HTTP)),)
TIKU_KIT_NET_ENABLE     := 1
endif
ifeq ($(TIKU_EXAMPLE_KITS_NET_TLS),1)
TIKU_KIT_NET_ENABLE     := 1
TIKU_KIT_CRYPTO_ENABLE  := 1
endif

# Apps that pull in kits.
ifeq ($(APP),net)
TIKU_KIT_NET_ENABLE     := 1
TIKU_KIT_CRYPTO_ENABLE  := 1
endif

# Shell net-test mode (TikuBench net suite where there is no working APP=net,
# e.g. Ambiq): pull the net stack into the shell firmware so it hosts the
# UDP/TCP/CoAP test servers. Gated -- normal shell builds are unaffected.
ifeq ($(TIKU_SHELL_NET_TEST),1)
TIKU_KIT_NET_ENABLE     := 1
endif

# After all the cascade rules above settle, recompute the
# UI -> GFX implication (auto-enables may have flipped UI on).
ifeq ($(TIKU_KIT_UI_ENABLE),1)
TIKU_KIT_GFX_ENABLE     := 1
endif

# ---------------------------------------------------------------------------
# Memory model
#
# Default is the small 16-bit model: code and data live in the lower-FRAM
# region (0x4400-0xFF7F on FR5969/FR6989 — about 48 KB). This is the
# only sensible default for FR5969 and FR2433, where there is no HIFRAM
# bank to gain anything from large model.
#
# Passing MEMORY_MODEL=large enables -mlarge + -mcode-region=either +
# -mdata-region=either so the linker can spill text/rodata/bss into
# HIFRAM (0x10000+) on parts that have it (FR5994, FR6989). This is
# how you actually unlock the chip's full FRAM beyond ~48 KB.
#
# The ISR-vector hazard (vectors at 0xFF80 are 16-bit and cannot reach
# HIFRAM) is handled centrally: the TIKU_ISR macro in
# hal/tiku_compiler.h applies __attribute__((lower)) to every handler
# under GCC, pinning ISR entry points in lower FRAM regardless of
# -mcode-region. Calls from those entry points into HIFRAM helpers
# work fine — under -mlarge the compiler emits 20-bit CALLA. Any new
# ISR site MUST go through TIKU_ISR (not raw __attribute__((interrupt)))
# so the placement protection is automatic.
#
# Caveat: 20-bit pointers and CALLA/MOVA inflate text by ~15-20% and
# data by ~25%, so prefer small model unless you actually need HIFRAM.
# ---------------------------------------------------------------------------
# Memory model.  On MSP430 the part decides: large where there is HIFRAM to
# spill into (FR5994 / FR6989), small on the 64-KB-or-smaller parts (FR5969 /
# FR2433).  This removes the old footgun where `make MCU=msp430fr5994` silently
# defaulted to the SMALL model -- the very one that overflows -- so the big
# parts only linked if you remembered MEMORY_MODEL=large.  Now they just build.
# (ambiq / rp2350 keep the plain small default; they force large where needed,
# e.g. BASIC.)  An explicit MEMORY_MODEL=... on the make line still wins.
ifeq ($(TIKU_PLATFORM),msp430)
MEMORY_MODEL ?= $(if $(filter 1,$(DEVICE_HAS_HIFRAM)),large,small)
else
MEMORY_MODEL ?= small
endif

# ---------------------------------------------------------------------------
# Build-time consistency guards (MSP430 only — RP2350 has no HIFRAM
# concept and the BASIC interpreter is platform-neutral C).
# ---------------------------------------------------------------------------
ifeq ($(TIKU_PLATFORM),msp430)
# 0. Parts without HIFRAM (FR5969 64 KB, FR2433 16 KB, and smaller) are no
#    longer supported build targets.  TikuOS core is the kernel PLUS the VFS
#    namespace -- the VFS *is* TikuOS, not an add-on -- and that overruns the
#    ~48 KB a small-model image can address (a bare `make MCU=msp430fr5994`
#    overflows FRAM by ~25 KB).  Their arch code (device/board headers, linker
#    scripts) is kept in the tree for reference, but the build refuses them.
ifneq ($(DEVICE_HAS_HIFRAM),1)
$(error MCU=$(MCU) is not a supported TikuOS target: the core (kernel + VFS) \
does not fit a 64-KB-or-smaller MSP430.  Supported MSP430 parts are \
msp430fr5994 (256 KB) and msp430fr6989 (128 KB).  The FR5969/FR2433 arch code \
remains in-tree for reference)
endif

# 1. MEMORY_MODEL=large is only meaningful on parts with HIFRAM (FRAM >
#    64 KB).  On FR5969 / FR2433 the large model inflates code/data by
#    ~20-25 % with no upper-FRAM region to spill into.  Refuse the build
#    rather than silently producing a fatter binary that fits worse.
ifeq ($(MEMORY_MODEL),large)
ifneq ($(DEVICE_HAS_HIFRAM),1)
$(error MEMORY_MODEL=large is only supported on MCUs with HIFRAM \
(FR5994, FR6989). $(MCU) has only a single 64-KB-or-smaller FRAM \
region, so large model just inflates code by ~20% with nowhere to \
spill. Use MEMORY_MODEL=small (the default) on this part)
endif
endif
endif

# 2. BASIC adds ~3.5 KB of code, which pushes most shell-enabled
#    MSP430 builds past the 48 KB lower-FRAM cap. The RP2350 has 4 MB
#    of XIP flash so this guard does not apply there.
ifeq ($(TIKU_PLATFORM),msp430)
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
ifneq ($(MEMORY_MODEL),large)
ifneq ($(TIKU_SHELL_BASIC_ALLOW_SMALL),1)
$(error TIKU_SHELL_BASIC_ENABLE=1 requires MEMORY_MODEL=large \
(only available on FR5994 / FR6989).  BASIC adds ~3.5 KB of code \
which overflows the 48 KB lower-FRAM cap in small-model builds. \
Re-run with: 'make ... MCU=msp430fr5994 TIKU_SHELL_ENABLE=1 \
TIKU_SHELL_BASIC_ENABLE=1 MEMORY_MODEL=large'. To override (only if \
you know your part has lower-FRAM headroom), pass \
TIKU_SHELL_BASIC_ALLOW_SMALL=1)
endif
endif
endif
endif

# 3. BASIC_PROGRAM=foo.bas embeds a BASIC source file directly into
#    the firmware and runs it on boot. Implies TIKU_SHELL_BASIC_ENABLE=1
#    and (transitively) MEMORY_MODEL=large. The .bas file is converted
#    to a C string literal at build time via tools/bas_to_c.py.
ifneq ($(BASIC_PROGRAM),)
override TIKU_SHELL_BASIC_ENABLE := 1
override MEMORY_MODEL := large
endif

# ---------------------------------------------------------------------------
# LEA peripheral on MSP430FR5994
# ---------------------------------------------------------------------------
#
# FR5994 has 8 KB of physical SRAM, but the toolchain's stock linker
# script reserves the upper 4 KB for the LEA (Low-Energy Accelerator)
# peripheral as LEARAM (~3.7 KB) + LEASTACK (312 B), leaving only 4 KB
# of general-purpose RAM for .bss and stack.
#
# TikuOS does not use LEA today, so by default we pre-include
# arch/msp430/devices/msp430fr5994_8k_ram.ld which redefines the
# RAM region to swallow LEARAM/LEASTACK and gives the kernel the full
# 8 KB. Set LEA_ENABLE=1 to fall back to the stock 4 KB layout if you
# bring up an LEA-using driver later.
# ---------------------------------------------------------------------------
LEA_ENABLE ?= 0

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
ifeq ($(TIKU_PLATFORM),rp2350)

# Cortex-M33 (mainline ARM core on Raspberry Pi RP2350). Single-precision
# FPU is present but tikuOS uses softfp ABI to keep the toolchain
# selection simple — none of the kernel code uses floats.
CFLAGS  = -mcpu=cortex-m33 -mthumb
CFLAGS += -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_RP2350=1
# Use newlib-nano (smaller integer-only printf, lightweight reentrancy)
# layered on top of the nosys syscall stubs. The full newlib stdio path
# hangs on bare-metal RP2350 because its global_stdio_init walks file
# structures that nosys can't satisfy; nano sidesteps that entirely and
# is also what the Pico SDK uses by default.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections -fno-common

# Memory tiers. tiku_mem.h defaults to the MSP430-era 128 B SRAM (AUTO) tier,
# which can't hold a real allocation. BASIC's program arena (~98 KB for the
# 1024-line BIG tier RP2350 selects) then fails to fit SRAM and resolve_tier()
# falls back to the 1 KB NVM tier -- which on RP2350 is QSPI flash (program-op,
# not byte-writable), so the first arena store faults and `basic` wedged the
# board at entry. Size the SRAM (AUTO) tier to hold the arena in the part's
# 520 KB SRAM. Gated on BASIC so non-BASIC builds keep the lean default.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
ifeq ($(HAS_TLS),1)
# HTTPS (HAS_TLS) adds the cert-TLS client's static buffers to .bss; trim the
# BASIC tier to 128 KB so the cyw43 bring-up + stack keep their SRAM (the
# ~98 KB 1024-line arena still fits).
CFLAGS += -DTIKU_TIER_SRAM_SIZE=131072    # 128 KB: arena + TLS .bss + radio
# TLS server flights are multi-KB; the lean 512 B TCP receive window turns
# each one into fragile 512-byte stop-and-wait (a lost window-update ACK
# stalls the handshake).  Widen the window so a flight streams in a couple of
# round-trips.  HTTPGET$ uses a single connection, so 2 conns is ample and
# keeps the SRAM bump small (4 KB x 2 = 8 KB vs 512 B x 4).
CFLAGS += -DTIKU_KITS_NET_TCP_RX_BUF_SIZE=4096
CFLAGS += -DTIKU_KITS_NET_TCP_MAX_CONNS=2
else
CFLAGS += -DTIKU_TIER_SRAM_SIZE=163840    # 160 KB: fits the 1024-line BASIC arena
endif
endif

else ifeq ($(TIKU_PLATFORM),ambiq)

# CPU/FPU per Ambiq part: Cortex-M55 + Helium (Apollo510) or Cortex-M4F with a
# single-precision FPU (Apollo4 Lite). Derived from -mcpu; -Wno-psabi below.
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
CFLAGS  = -mcpu=cortex-m4 -mthumb
CFLAGS += -mfpu=fpv4-sp-d16 -mfloat-abi=hard
else
CFLAGS  = -mcpu=cortex-m55 -mthumb
CFLAGS += -mfpu=auto -mfloat-abi=hard
endif
CFLAGS += -Os -Wall -Wextra -Wno-psabi
# newlib-nano (small integer printf) + nosys syscall stubs. AmbiqSuite used to
# supply _sbrk/_write/_read/etc; with the SDK gone, libnosys provides them.
# Same config as the rp2350 block above; nano also avoids the heavy stdio init.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_AMBIQ=1
# Memory tiers. The tiku_mem.h defaults are MSP430-era (128 B SRAM / 1 KB NVM)
# and assume large allocations spill to HIFRAM (FRAM > 64 KB), which these parts
# lack -- so without an override AUTO allocations land in the tiny SRAM tier and
# OOM. The SRAM (AUTO) tier lives in the multi-MB SSRAM (.ssram, powered + zeroed
# in tiku_crt_early.c -- mem port B), NOT the 512 KB DTCM, so size it to the
# part's SSRAM rather than the old DTCM-era 128 KB cap (which was left stale when
# the tier moved out of DTCM). This is the ceiling on every AUTO-tier arena,
# including BASIC's program arena (a 2048-line program needs ~195 KB). The mem
# size type is 32-bit here (arch/ambiq/tiku_mem_arch.h), so multi-hundred-KB
# tiers are fine.
ifeq ($(MCU),apollo4p)
CFLAGS += -DTIKU_TIER_SRAM_SIZE=1703936   # 1.625 MB tier in the Plus's 2 MB SSRAM (4 MB MPU window; +~182 KB other .ssram still fits, ~206 KB spare)
else ifeq ($(MCU),apollo4l)
CFLAGS += -DTIKU_TIER_SRAM_SIZE=524288    # 512 KB of the Lite's 1 MB mapped SSRAM
else
CFLAGS += -DTIKU_TIER_SRAM_SIZE=1048576   # 1 MB of the 3 MB SSRAM (Apollo510)
endif
CFLAGS += -DTIKU_TIER_NVM_SIZE=16384      # 16 KB NVM tier
# HTTPS over TCP on Ambiq -- two coupled fixes:
# (1) BUF_PERSIST=0: put the RX ring + TX pool in regular ZERO-INITIALISED .bss,
#     not .persistent. On Cortex-M ".persistent" is plain SRAM (lost on a power
#     cycle, so the persistence is moot) and the linker marks it NOLOAD/skipped
#     by zero-init -- a large *uninitialised* RX ring there behaved
#     non-deterministically (varying RST/stall/empty-body across identical
#     builds on Apollo510). Zeroed .bss is deterministic.
# (2) RX_BUF=4096: a TLS-1.3 server's post-handshake NewSessionTickets (google's
#     GFE sends ~1 KB right after the handshake) sit UNREAD in the ring
#     (read_record stops at the server Finished), so a small ring fills with
#     tickets and refuses the HTTP response (google => empty body; cloudflare/
#     nginx send less post-handshake data, so they fit a small ring). 4 KB holds
#     tickets + response together. DTCM has ~410 KB free, so 4 KB x 2 in .bss is
#     trivial.
ifeq ($(HAS_TLS),1)
CFLAGS += -DTIKU_KITS_NET_TCP_BUF_PERSIST=0
CFLAGS += -DTIKU_KITS_NET_TCP_RX_BUF_SIZE=4096
CFLAGS += -DTIKU_KITS_NET_TCP_MAX_CONNS=2
endif
# Part selectors that configure the vendored register map (apollo4l.h / apollo510.h).
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
# apollo4p reuses the apollo4l register map (apollo4l.h) -- the Apollo4 family is
# register-compatible for the peripherals tikuOS uses (UART2/GPIO/PWRCTRL/STIMER/
# MRAM), so the M4F arch backends are shared.
CFLAGS += -DPART_apollo4l -DAM_PART_APOLLO4L -Dgcc
else
CFLAGS += -DPART_apollo510 -DAM_PART_APOLLO510 -DAM_PACKAGE_BGA -Dgcc
endif
# The Apollo4 Plus EVB routes its J-Link VCOM to UART0 (pads 60/47); the Lite
# uses UART2 (pads 54/11). The shared UART driver + crt vector key off this.
ifeq ($(MCU),apollo4p)
CFLAGS += -DTIKU_CONSOLE_UART0
endif
# The Apollo510 Blue EVB routes its J-Link VCOM to UART1 (pads 12/14, funcsel 5);
# the base Apollo510 EVB uses UART0 (pads 30/55). The shared M55 UART driver, the
# crt vector table and the wake source all key off TIKU_CONSOLE_UART1.
ifeq ($(MCU),apollo510b)
CFLAGS += -DTIKU_CONSOLE_UART1
endif
# BLE radio (EM9305 on IOM6 SPI) -- opt-in, apollo510b only. Building it turns
# the IOM SPI master on (TIKU_SPI_IOM_ENABLE flips tiku_spi_arch.c from stub to
# the real driver) and compiles the bare-metal EM9305 SPI-HCI transport. The
# pinout lives in the apollo510b board header, so reject the flag elsewhere up
# front rather than fail deep in the compile. The `ble` shell command that
# drives the first-contact probe are added in the arch + shell source blocks
# below (SRCS is (re)initialised further down, so it cannot be extended here).
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
ifneq ($(MCU),apollo510b)
$(error TIKU_DRV_BLE_EM9305_ENABLE=1 requires MCU=apollo510b (the only board \
with the EM9305 radio); currently MCU=$(MCU))
endif
CFLAGS += -DTIKU_DRV_BLE_EM9305_ENABLE=1 -DTIKU_SPI_IOM_ENABLE=1
# Map the concrete radio driver to the GENERIC BLE capability. Consumers (the
# BLE-serial facade, the BASIC BLE words) gate on TIKU_HAS_BLE, not on any one
# chip -- a future BLE backend just sets this too.
CFLAGS += -DTIKU_HAS_BLE=1
endif
CFLAGS += -I$(PROJ_DIR)
# CMSIS register headers, VENDORED in-tree (arch/ambiq/cmsis/) so the build is
# fully self-contained: it references nothing in temp/AmbiqSuite, only the MRAM
# bootrom blob. apollo510.h (the complete Apollo510 register map -- all 30
# peripherals) pulls core_cm55.h + system_apollo510.h from that same dir.
# Provenance + licenses: arch/ambiq/cmsis/PROVENANCE.md.
CFLAGS += -I$(PROJ_DIR)/arch/ambiq/cmsis
CFLAGS += -ffunction-sections -fdata-sections -fno-common

else ifeq ($(TIKU_PLATFORM),nordic)

# Cortex-M33 (Nordic nRF54L15). Single-precision FPU is present but tikuOS
# uses the softfp ABI (no float in the kernel), matching the rp2350 M33 config.
CFLAGS  = -mcpu=cortex-m33 -mthumb
CFLAGS += -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_NORDIC=1
# newlib-nano (small integer printf) + nosys syscall stubs -- same self-
# contained libc config as the rp2350 / ambiq ARM ports.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections -fno-common

# BASIC needs a real SRAM (AUTO) tier for its program arena (~98 KB for the
# 1024-line BIG tier); the tiku_mem.h default is 128 B, so `basic` OOMs at
# entry without this.  The nRF54L15 has 256 KB SRAM, so a 160 KB tier fits the
# arena with ample room for .bss + stack.  Gated on BASIC so non-BASIC builds
# keep the lean default.  (Same fix the rp2350 block applies for its part.)
# 32 KB, not rp2350's 160 KB: nordic runs the FRAM BASIC tier (96 lines,
# 2 KB heap -- observed AUTO-tier demand ~10 KB), and the https build must
# leave room for BOTH TLS clients' RFC-max record buffers in 256 KB SRAM.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
# Threaded HTTPS also carries the worker scheduler state in the same 240 KB
# application SRAM window.  The Nordic FRAM-tier BASIC profile has measured
# AUTO-tier demand of only ~10 KB, so keep 20 KB for threaded builds and
# recover 12 KB of static headroom; non-threaded BASIC retains the original
# 32 KB arena.  This lets the canonical TikuBench/TikuConsole HTTPS-offload
# profile link while remaining comfortably above observed BASIC demand.
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
# The LM20's tier arena lives in RAM2 (the upper 256 KB SRAM bank, linker
# section .ram2) and does not compete with the primary bank's .bss/stack at
# all -- so give BASIC a roomy arena regardless of threads.  192 KB of the
# 255 KB usable bank (top 1 KB of RAM2 is unbacked on this silicon).
CFLAGS += -DTIKU_TIER_SRAM_SIZE=196608    # 192 KB tier arena in RAM2
else ifeq ($(TIKU_THREADS_ENABLE),1)
CFLAGS += -DTIKU_TIER_SRAM_SIZE=20480     # 20 KB: BASIC + thread headroom
else
CFLAGS += -DTIKU_TIER_SRAM_SIZE=32768     # 32 KB: FRAM-tier BASIC arena
endif
endif

else

CFLAGS  = -mmcu=$(MCU) -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -DPLATFORM_MSP430=1
ifeq ($(MEMORY_MODEL),large)
# -mlarge:               20-bit pointers, CALLA/MOVA for full FRAM reach
# -mcode-region=either:  text can spill into HIFRAM (frees lower-FRAM space)
# -mdata-region=either:  data can also spill into .upper.bss/.upper.data
#                        in HIFRAM. The kernel MPU is configured to grant
#                        R+W+X on segment 3 when the device has HIFRAM
#                        (see TIKU_DEVICE_HAS_HIFRAM in the device header
#                        and the default-protection logic in
#                        arch/msp430/tiku_mpu_arch.c) so writes to the
#                        UART RX ring, process queue, shell tables, etc.
#                        succeed. Forcing data lower overflows SRAM by
#                        ~60 bytes once 20-bit pointer inflation kicks in.
CFLAGS += -mlarge -mcode-region=either -mdata-region=either
# Expose memory-model selection to C source so the kernel can gate
# HIFRAM-backed sections (e.g., the HIFRAM tier pool in
# kernel/memory/tiku_tier.c) on both HAS_HIFRAM and large-mode
# builds. Without this gate, declaring TIKU_HIFRAM_BSS-tagged
# arrays in small-mode builds links-fails because the .upper.bss
# output section doesn't exist.
CFLAGS += -DTIKU_MEMORY_MODEL_LARGE=1
endif
CFLAGS += -I$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
CFLAGS += -I$(MSP430_SUPPORT_DIR)
endif
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections

endif

# VFS node tables (and other growable static tables) use positional
# initializers that intentionally leave optional trailing fields
# (e.g. tiku_vfs_node_t.desc) zero/NULL -- the zero-default IS the
# back-compat contract.  -Wmissing-field-initializers (pulled in by
# -Wextra) flags every such entry; disable just this one sub-warning
# across all platforms rather than churn ~150 initializers on each
# future field addition.  -Wextra otherwise stays on.
CFLAGS += -Wno-missing-field-initializers

# Preemptive worker threads -- opt-in, Cortex-M only. Thread 0 is the whole
# existing cooperative kernel; workers are stackful compute threads confined
# to the ISR-safe primitives (see kernel/threads/tiku_thread.h). Per-thread
# stacks are impossible on a 2 KB MSP430, which stays cooperative AND
# byte-identical (flag off = none of this compiles). One generic Cortex-M
# switcher (kernel/threads/tiku_thread_cortexm.inl) serves every part via a
# per-platform PendSV shim.
# COMMON SCOPE on purpose: this must sit AFTER the per-platform CFLAGS
# blocks above (each starts with `CFLAGS = ...`), or the define only
# reaches whichever branch hosts it -- it lived inside the Ambiq branch
# once, and RP2350 builds silently compiled threads (and their tests)
# to empty stubs: firmware booted, reported total=0, nothing ran.
ifeq ($(TIKU_THREADS_ENABLE),1)
ifeq ($(TIKU_PLATFORM),msp430)
$(error TIKU_THREADS_ENABLE=1 requires a Cortex-M part; MSP430 \
stays cooperative -- 2 KB of SRAM has no room for per-thread stacks)
endif
ifeq ($(filter apollo510 apollo510b apollo4l apollo4p rp2350 nrf54l15 nrf54lm20a nrf54lm20b,$(MCU)),)
$(error TIKU_THREADS_ENABLE=1 needs a supported Cortex-M part -- \
apollo510/apollo510b (M55), apollo4l/apollo4p (M4F), rp2350 or \
nrf54l15/nrf54lm20a (M33); $(MCU) has no thread backend. The switcher is generic \
Cortex-M asm (kernel/threads/tiku_thread_cortexm.inl); adding a part = a \
two-line shim that names its PendSV vector symbol (plus a custom cycle \
source if the part's DWT freezes standalone), and proving the torture suite)
endif
CFLAGS += -DTIKU_THREADS_ENABLE=1
endif

# UART baud rate (default 9600; override: make UART_BAUD=115200)
UART_BAUD ?=
ifneq ($(UART_BAUD),)
CFLAGS += -DTIKU_BOARD_UART_BAUD=$(UART_BAUD)
endif

CFLAGS += $(EXTRA_CFLAGS)

ifeq ($(HAS_APPS),1)
CFLAGS += -DHAS_APPS=1
endif
ifeq ($(HAS_TESTS),1)
CFLAGS += -DHAS_TESTS=1
# Apollo510's arm-none-eabi-gcc 15 promotes -Wimplicit-function-declaration to a
# hard error. The shared TikuBench test tree was authored against MSP430's older,
# lenient gcc and has a few category dispatchers (e.g. tier) that call test fns
# whose prototype header sits behind a different TEST_* gate. Downgrade it to a
# warning for the Apollo510 TEST build only -- matches the MSP430/RP2350
# toolchains, and every such fn is a void(void) so the call is safe.
ifeq ($(TIKU_PLATFORM),ambiq)
CFLAGS += -Wno-error=implicit-function-declaration
endif
endif
ifeq ($(HAS_EXAMPLES),1)
CFLAGS += -DHAS_EXAMPLES=1
endif
ifeq ($(HAS_DEMOS),1)
CFLAGS += -DHAS_DEMOS=1
# Note: actual SRCS += for demos/$(DEMO)/*.c is below, after the
# SRCS=main.c reset.
endif
ifeq ($(HAS_TIKUKITS),1)
CFLAGS += -DHAS_TIKUKITS=1
endif
ifeq ($(HAS_DRIVERS),1)
CFLAGS += -DHAS_DRIVERS=1
endif

ifeq ($(TIKU_PLATFORM),rp2350)

LDFLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
# nano.specs swaps in libc_nano (small integer-only printf without the
# heavy stdio init that hangs on bare metal). Order: nano first, nosys
# second so libnosys still provides the syscall stubs.
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
LDFLAGS += -Tarch/arm-rp2350/devices/rp2350.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
LDFLAGS += -Wl,-u,tiku_rp2350_vectors
LDFLAGS += -Wl,-u,tiku_rp2350_image_def
LDFLAGS += -Wl,-u,tiku_rp2350_boot2_marker
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map

else ifeq ($(TIKU_PLATFORM),ambiq)

ifneq (,$(filter apollo4l apollo4p,$(MCU)))
LDFLAGS  = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
else
LDFLAGS  = -mcpu=cortex-m55 -mthumb -mfpu=auto -mfloat-abi=hard
endif
# nano.specs -> libc_nano (small printf, no heavy stdio init); nosys.specs ->
# libnosys syscall stubs (_sbrk/_write/...), formerly supplied by AmbiqSuite.
LDFLAGS += --specs=nano.specs --specs=nosys.specs
LDFLAGS += -nostartfiles -static
ifeq ($(MCU),apollo4p)
# Apollo4 Plus: same map as the Lite but the shared-SRAM window grows from 1 MB
# to the full 2 MB the two SSRAM banks expose (both already powered in the crt),
# backing a larger SRAM tier. (The Plus's remaining ~0.75 MB needs its own bank
# definitions -- see apollo4p.ld.)
LDFLAGS += -Tarch/ambiq/devices/apollo4p.ld
else ifeq ($(MCU),apollo4l)
LDFLAGS += -Tarch/ambiq/devices/apollo4l.ld
else
LDFLAGS += -Tarch/ambiq/devices/apollo510.ld
endif
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
LDFLAGS += -Wl,-u,tiku_ambiq_vectors
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
# System libraries only. The AmbiqSuite HAL/BSP archives (libam_hal.a /
# libam_bsp.a) are gone -- de-SDK complete, zero am_hal/am_bsp calls remain --
# and libnosys (via nosys.specs above) provides the syscall stubs.
LDLIBS  = -Wl,--start-group
LDLIBS += -lm -lc -lgcc
LDLIBS += -Wl,--end-group

else ifeq ($(TIKU_PLATFORM),nordic)

LDFLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
# The LM20B's memory map is identical to the A's (diff-proven); one script.
LDFLAGS += -Tarch/nordic/devices/nrf54lm20a.ld
else
LDFLAGS += -Tarch/nordic/devices/nrf54l15.ld
endif
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
LDFLAGS += -Wl,-u,tiku_nordic_vectors
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
LDLIBS  = -Wl,--start-group
# Axon NPU driver core (empty unless TIKU_AXON_ENABLE=1 defines it below;
# inside the group so its memcpy/memset resolve against libc).
LDLIBS += $(LDLIBS_AXON)
LDLIBS += -lm -lc -lgcc
LDLIBS += -Wl,--end-group

else

LDFLAGS  = -mmcu=$(MCU)
ifeq ($(MEMORY_MODEL),large)
LDFLAGS += -mlarge -mcode-region=either -mdata-region=either
endif
LDFLAGS += -L$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
LDFLAGS += -L$(MSP430_SUPPORT_DIR)
endif
LDFLAGS += -Wl,--gc-sections
# --- msp430-elf ld DWARF workaround -------------------------------------
# libnosys.a (pulled in by --specs=nosys.specs) ships a malformed
# .debug_line unit that ld 9.3.1 mis-handles under --gc-sections on larger
# images, failing the link with "line info data is bigger than the space
# remaining in the section" (e.g. the init-boot / init-fram test builds
# that drag in the shell + init system). TikuOS compiles with no -g, so we
# link a debug-stripped copy of libnosys -- lossless, we flash no debug
# info. -print-file-name with the build's multilib flags picks the correct
# libnosys variant (small vs -mlarge).
NOSYS_FIXED := $(BUILD_DIR)/libnosys.a
# Select the SAME multilib the link picks: small uses lib/libnosys.a; large
# (-mlarge -mcode-region=either -mdata-region=either) uses
# large/full-memory-range/libnosys.a. Mismatched models fail the link with
# "assumes data is exclusively in lower memory".
NOSYS_ORIG  := $(shell $(CC) -mmcu=$(MCU) $(if $(filter large,$(MEMORY_MODEL)),-mlarge -mcode-region=either -mdata-region=either) -print-file-name=libnosys.a)
LDFLAGS    += -L$(BUILD_DIR)
LDFLAGS += -Wl,-u,tiku_autostart_processes

# Build the debug-stripped libnosys (defined only in this msp430 branch).
$(NOSYS_FIXED): $(NOSYS_ORIG)
	@mkdir -p $(BUILD_DIR)
	@cp $(NOSYS_ORIG) $@ && $(OBJCOPY) --strip-debug $@

endif

ifeq ($(TIKU_PLATFORM),msp430)

# FR5994: merge LEARAM/LEASTACK into RAM unless LEA is requested.
# The override LD redeclares the RAM region with LENGTH=0x2000 and
# zero-sizes LEARAM/LEASTACK; ld(1) takes the last MEMORY declaration
# of a given name, so this wins over the spec-default script that
# `-mmcu=msp430fr5994` auto-includes. Also exposes the chosen layout
# to the C side via TIKU_FR5994_LEA_DISABLED so the device header can
# report the right TIKU_DEVICE_RAM_SIZE. The same override also adds
# the .upper_end_marker section so __hifram_end resolves on FR5994.
ifeq ($(MCU),msp430fr5994)
ifeq ($(LEA_ENABLE),0)
LDFLAGS += -Tarch/msp430/devices/msp430fr5994_8k_ram.ld
CFLAGS  += -DTIKU_FR5994_LEA_DISABLED=1
endif
endif

# FR6989: HIFRAM end-marker only (no SRAM merging needed — FR6989
# has 2 KB of SRAM and no LEA peripheral, so the stock RAM region
# is already optimal). The override adds .upper_end_marker so
# __hifram_end resolves and `free` can report HIFRAM in-use bytes.
ifeq ($(MCU),msp430fr6989)
LDFLAGS += -Tarch/msp430/devices/msp430fr6989_hifram.ld
endif

endif # TIKU_PLATFORM == msp430

# ---------------------------------------------------------------------------
# Source files — core OS (always compiled)
#
# MINIMAL=1 (RP2350 only): build a bare-metal smoke test that prints
# "TikuOS minimal: hello #N" forever on UART0 + toggles GP25. No
# kernel, scheduler, processes, VFS, or shell — useful for isolating
# whether bring-up failures are in the boot/clock/UART layer or
# higher up the stack.
# ---------------------------------------------------------------------------
MINIMAL ?= 0

ifeq ($(MINIMAL),1)
ifeq ($(filter $(TIKU_PLATFORM),rp2350 ambiq nordic),)
$(error MINIMAL=1 is only supported on MCU=rp2350, MCU=apollo510, MCU=nrf54l15, or MCU=nrf54lm20a)
endif

# Use the minimal entry point and exactly the arch files it needs.
SRCS  = main_minimal.c
ifeq ($(TIKU_PLATFORM),ambiq)
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
SRCS += arch/ambiq/tiku_crt_early_apollo4l.c
SRCS += arch/ambiq/tiku_cpu_freq_boot_apollo4l.c
SRCS += arch/ambiq/tiku_cpu_common_apollo4l.c
SRCS += arch/ambiq/tiku_uart_apollo4l.c
SRCS += arch/ambiq/tiku_gpio_apollo4l.c
else
SRCS += arch/ambiq/tiku_crt_early.c
SRCS += arch/ambiq/tiku_cpu_freq_boot_arch.c
SRCS += arch/ambiq/tiku_cpu_common.c
SRCS += arch/ambiq/tiku_uart_arch.c
SRCS += arch/ambiq/tiku_gpio_arch.c
endif
# No AmbiqSuite sources compiled in (de-SDK complete): system_apollo510.c,
# am_util_delay.c, am_util_stdio.c and am_resources.c are all dropped -- tikuOS
# uses its own printf and never references the HAL resource tables.
else ifeq ($(TIKU_PLATFORM),nordic)
SRCS += arch/nordic/tiku_crt_early.c
SRCS += arch/nordic/tiku_cpu_freq_boot_arch.c
SRCS += arch/nordic/tiku_cpu_common.c
SRCS += arch/nordic/tiku_uart_arch.c
SRCS += arch/nordic/tiku_gpio_arch.c
else
SRCS += arch/arm-rp2350/tiku_crt_early.c
SRCS += arch/arm-rp2350/tiku_cpu_freq_boot_arch.c
SRCS += arch/arm-rp2350/tiku_cpu_common.c
SRCS += arch/arm-rp2350/tiku_uart_arch.c
SRCS += arch/arm-rp2350/tiku_gpio_arch.c
endif
CFLAGS += -DTIKU_MINIMAL=1

else

SRCS  = main.c

ifeq ($(TIKU_PLATFORM),rp2350)

# RP2350 arch
SRCS += arch/arm-rp2350/tiku_cpu_common.c
SRCS += arch/arm-rp2350/tiku_crt_early.c
SRCS += arch/arm-rp2350/tiku_cpu_freq_boot_arch.c
SRCS += arch/arm-rp2350/tiku_cpu_watchdog_arch.c
SRCS += arch/arm-rp2350/tiku_htimer_arch.c
SRCS += arch/arm-rp2350/tiku_i2c_arch.c
SRCS += arch/arm-rp2350/tiku_adc_arch.c
SRCS += arch/arm-rp2350/tiku_onewire_arch.c
SRCS += arch/arm-rp2350/tiku_timer_arch.c
SRCS += arch/arm-rp2350/tiku_crit_arch.c
SRCS += arch/arm-rp2350/tiku_wake_arch.c
SRCS += arch/arm-rp2350/tiku_gpio_irq_arch.c
SRCS += arch/arm-rp2350/tiku_uart_arch.c
SRCS += arch/arm-rp2350/tiku_mem_arch.c
SRCS += arch/arm-rp2350/tiku_mpu_arch.c
SRCS += arch/arm-rp2350/tiku_region_arch.c
SRCS += arch/arm-rp2350/tiku_nvm_region_rp2350.c
SRCS += arch/arm-rp2350/tiku_gpio_arch.c
SRCS += arch/arm-rp2350/tiku_spi_arch.c
SRCS += arch/arm-rp2350/tiku_lcd_arch.c
SRCS += arch/arm-rp2350/tiku_pio_arch.c
SRCS += arch/arm-rp2350/tiku_pwm_arch.c
SRCS += arch/arm-rp2350/tiku_dma_arch.c
SRCS += arch/arm-rp2350/tiku_trng_arch.c
ifeq ($(TIKU_THREADS_ENABLE),1)
# Cortex-M33 workers (core 0): the generic switcher via the RP2350 shim
# (its strong tiku_rp2350_pendsv_handler overrides the crt weak alias).
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/arm-rp2350/tiku_thread_arch.c
endif

# Console backend: add the native USB CDC stack for TIKU_CONSOLE=usb|both.
ifeq ($(TIKU_CONSOLE),usb)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1
else ifeq ($(TIKU_CONSOLE),both)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1 -DTIKU_CONSOLE_BOTH=1
endif

else ifeq ($(TIKU_PLATFORM),nordic)

# Nordic nRF54L arch (Cortex-M33). Boot + tick + console are proven; the
# remaining HAL files (crit/wake/mem/mpu/region/watchdog + driver stubs) are
# added below as the kernel needs them.
SRCS += arch/nordic/tiku_cpu_common.c
SRCS += arch/nordic/tiku_crt_early.c
SRCS += arch/nordic/tiku_cpu_freq_boot_arch.c
SRCS += arch/nordic/tiku_timer_arch.c
SRCS += arch/nordic/tiku_gpio_arch.c
SRCS += arch/nordic/tiku_uart_arch.c
SRCS += arch/nordic/tiku_crit_arch.c
SRCS += arch/nordic/tiku_wake_arch.c
SRCS += arch/nordic/tiku_mem_arch.c
SRCS += arch/nordic/tiku_mpu_arch.c
SRCS += arch/nordic/tiku_region_arch.c
SRCS += arch/nordic/tiku_nvm_region_nordic.c
SRCS += arch/nordic/tiku_cpu_watchdog_arch.c
SRCS += arch/nordic/tiku_htimer_arch.c
SRCS += arch/nordic/tiku_gpio_irq_arch.c
SRCS += arch/nordic/tiku_adc_arch.c
SRCS += arch/nordic/tiku_i2c_arch.c
SRCS += arch/nordic/tiku_spi_arch.c
SRCS += arch/nordic/tiku_onewire_arch.c
SRCS += arch/nordic/tiku_trng_arch.c
SRCS += arch/nordic/tiku_crypto_arch.c
SRCS += arch/nordic/tiku_radio_arch.c
SRCS += arch/nordic/tiku_fault_arch.c
# On-die 2.4 GHz RADIO backs the GENERIC broadcast-BLE capability: the
# tiku_ble_adv facade, the BASIC BLEBEACON/BLESCAN$ words and /sys/radio all
# gate on TIKU_HAS_BLE_ADV, never on the chip (same pattern as TIKU_HAS_BLE).
SRCS += interfaces/bluetooth/tiku_ble_adv.c
CFLAGS += -DTIKU_HAS_BLE_ADV=1
# FLPR (VPR RISC-V coprocessor) -- opt-in.  Builds the tiny RISC-V firmware
# (arch/nordic/flpr/) with the xPack riscv-none-elf toolchain (unpacked under
# gitignored temp/toolchains/ -- see kintsugi/flpr_plan.md F0), embeds the
# flat binary into this image, and compiles the app-side loader + /sys/flpr.
ifeq ($(TIKU_FLPR_ENABLE),1)
# nRF54L15 and nRF54LM20A/B all carry the same VPR00 ("FLPR") RISC-V core at
# the same base (0x5004C000), IRQ 76, MPC00 (0x50041000) and SPU10/SPU20 slots
# -- diff-proven identical.  The FLPR carve is the top 16 KB of the LOWER SRAM
# bank (0x2003C000..0x2003FFFF) on every nordic part, so tiku_flpr_ipc.h and
# tiku_flpr.ld are shared verbatim; the LM20's RAM2 tier arena is untouched.
# Only the app linker reserves the carve (per-device .ld, always-on for a
# stable layout).
SRCS += arch/nordic/tiku_flpr_arch.c
CFLAGS += -DTIKU_FLPR_ENABLE=1
RISCV_PREFIX ?= temp/toolchains/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
RISCV_CC      = $(RISCV_PREFIX)gcc
FLPR_BUILD    = $(BUILD_DIR)/flpr
# The FLPR is RV32E (16 GPRs) + M + C; Zicsr for the CLIC CSRs later.
FLPR_CFLAGS   = -march=rv32emc_zicsr -mabi=ilp32e -Os -Wall -Wextra \
                -ffreestanding -nostdlib -nostartfiles \
                -ffunction-sections -fdata-sections -I$(PROJ_DIR) -MMD -MP
FLPR_OBJS     = $(FLPR_BUILD)/tiku_flpr_crt0.o $(FLPR_BUILD)/tiku_flpr_main.o
TIKU_FLPR_IMG_O = $(FLPR_BUILD)/tiku_flpr_img.o
ifeq ($(wildcard $(RISCV_CC)),)
$(error TIKU_FLPR_ENABLE=1 needs the RISC-V toolchain at $(RISCV_CC) -- \
kintsugi/flpr_plan.md F0 documents the xPack download)
endif
endif
ifeq ($(TIKU_THREADS_ENABLE),1)
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/nordic/tiku_thread_arch.c
endif

else ifeq ($(TIKU_PLATFORM),ambiq)

# Apollo510 arch (Cortex-M55). GPIO/SPI/LCD are bundled here (like RP2350)
# so they aren't double-added by the MSP430-guarded blocks further down.
# Device-agnostic ambiq backends (stubs + WFI) -- shared by both Ambiq parts.
# (ADC is part-specific: apollo4l has a real SAR-ADC backend, apollo510 keeps
# the stub for now -- so it is added per-part in the split below, not here.)
SRCS += arch/ambiq/tiku_i2c_arch.c
SRCS += arch/ambiq/tiku_onewire_arch.c
SRCS += arch/ambiq/tiku_wake_arch.c
SRCS += arch/ambiq/tiku_spi_arch.c
# EM9305 BLE radio transport rides the IOM SPI master above (apollo510b only).
# tiku_ble_uart.c layers the connectable GATT/NUS host stack on that transport.
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
SRCS += arch/ambiq/tiku_em9305.c
SRCS += arch/ambiq/tiku_ble_uart.c
# Portable "serial over BLE" facade on top of the host stack -- backs the BASIC
# BLE words and any app; EM9305 is just its first backend.
SRCS += interfaces/bluetooth/tiku_ble_serial.c
endif
SRCS += arch/ambiq/tiku_lcd_arch.c
# CryptoCell-312 TRNG (shared across apollo4l/4p/510) -- backs the cert-TLS
# handshake RNG (TIKU_KITS_CRYPTO_TLS_RNG_FILL).
SRCS += arch/ambiq/tiku_trng_arch.c
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
# Apollo4 Lite (Cortex-M4F) device/CPU backends.
# Apollo4 Lite drives the kernel tick from the always-on STIMER (not SysTick,
# which freezes in WFI sleep); apollo510 keeps the shared SysTick timer below.
SRCS += arch/ambiq/tiku_timer_apollo4l.c
SRCS += arch/ambiq/tiku_cpu_common_apollo4l.c
SRCS += arch/ambiq/tiku_crt_early_apollo4l.c
SRCS += arch/ambiq/tiku_cpu_freq_boot_apollo4l.c
SRCS += arch/ambiq/tiku_cpu_watchdog_apollo4l.c
SRCS += arch/ambiq/tiku_htimer_apollo4l.c
SRCS += arch/ambiq/tiku_crit_apollo4l.c
SRCS += arch/ambiq/tiku_gpio_irq_apollo4l.c
SRCS += arch/ambiq/tiku_uart_apollo4l.c
SRCS += arch/ambiq/tiku_mem_apollo4l.c
SRCS += arch/ambiq/tiku_mpu_apollo4l.c
SRCS += arch/ambiq/tiku_region_apollo4l.c
SRCS += arch/ambiq/tiku_nvm_region_apollo4l.c
SRCS += arch/ambiq/tiku_gpio_apollo4l.c
SRCS += arch/ambiq/tiku_adc_apollo4l.c
ifeq ($(TIKU_THREADS_ENABLE),1)
# Cortex-M4F workers: the generic switcher via the shared Ambiq shim
# (its strong tiku_ambiq_pendsv_handler overrides the crt weak alias).
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/ambiq/tiku_thread_arch.c
endif
else
# Apollo510 (Cortex-M55) device/CPU backends.
SRCS += arch/ambiq/tiku_adc_arch.c
SRCS += arch/ambiq/tiku_timer_arch.c
ifeq ($(TIKU_THREADS_ENABLE),1)
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/ambiq/tiku_thread_arch.c
endif
SRCS += arch/ambiq/tiku_cpu_common.c
SRCS += arch/ambiq/tiku_crt_early.c
SRCS += arch/ambiq/tiku_cpu_freq_boot_arch.c
SRCS += arch/ambiq/tiku_cpu_watchdog_arch.c
SRCS += arch/ambiq/tiku_htimer_arch.c
SRCS += arch/ambiq/tiku_crit_arch.c
SRCS += arch/ambiq/tiku_gpio_irq_arch.c
SRCS += arch/ambiq/tiku_uart_arch.c
SRCS += arch/ambiq/tiku_mem_arch.c
SRCS += arch/ambiq/tiku_mpu_arch.c
SRCS += arch/ambiq/tiku_region_arch.c
SRCS += arch/ambiq/tiku_nvm_region_apollo510.c
SRCS += arch/ambiq/tiku_gpio_arch.c
endif
# No AmbiqSuite sources compiled in (de-SDK complete): system_apollo510.c,
# am_util_delay.c, am_util_stdio.c, am_resources.c all dropped.

else

# MSP430 arch (default)
SRCS += arch/msp430/tiku_cpu_common.c
SRCS += arch/msp430/tiku_crt_early.c
SRCS += arch/msp430/tiku_cpu_freq_boot_arch.c
SRCS += arch/msp430/tiku_cpu_watchdog_arch.c
SRCS += arch/msp430/tiku_htimer_arch.c
SRCS += arch/msp430/tiku_i2c_arch.c
SRCS += arch/msp430/tiku_adc_arch.c
SRCS += arch/msp430/tiku_onewire_arch.c
SRCS += arch/msp430/tiku_timer_arch.c
SRCS += arch/msp430/tiku_crit_arch.c
SRCS += arch/msp430/tiku_wake_arch.c
SRCS += arch/msp430/tiku_gpio_irq_arch.c
SRCS += arch/msp430/tiku_uart_arch.c
SRCS += arch/msp430/tiku_mem_arch.c
SRCS += arch/msp430/tiku_mpu_arch.c
SRCS += arch/msp430/tiku_region_arch.c
SRCS += arch/msp430/tiku_nvm_region_msp430.c

endif
SRCS += boot/tiku_boot.c
SRCS += hal/tiku_cpu.c
SRCS += kernel/cpu/tiku_common.c
SRCS += kernel/cpu/tiku_watchdog.c
SRCS += kernel/cpu/tiku_hang.c
SRCS += kernel/cpu/tiku_stack.c
SRCS += kernel/cpu/tiku_rtc.c
SRCS += kernel/cpu/tiku_bench.c

# Driver-registry layer. Always built so the kernel exposes
# tiku_drv_init_all(); the descriptor table itself comes from
# drivers/ submodule when present, else from the empty fallback
# below. See drivers.md.
SRCS += kernel/drivers/tiku_drv_registry.c
ifeq ($(HAS_DRIVERS),1)
SRCS    += drivers/tiku_drv_table.c
# Each driver self-includes via its own build.mk fragment. The
# globbing covers 2- and 3-level driver paths (wifi/cyw43/build.mk
# AND sensors/temperature/mcp9808/build.mk).
include $(wildcard $(PROJ_DIR)/drivers/*/*/build.mk)
include $(wildcard $(PROJ_DIR)/drivers/*/*/*/build.mk)
else
SRCS    += kernel/drivers/tiku_drv_empty_table.c
endif
SRCS += kernel/timers/tiku_clock.c
SRCS += kernel/timers/tiku_htimer.c
SRCS += kernel/timers/tiku_timer.c
SRCS += kernel/timers/tiku_crit.c

# Bit-bang transmitter (opt-in: backscatter / software-UART / IR).
# Disabled by default so existing builds carry no extra code.
# Auto-enabled when example 20 is selected so callers don't have to
# pass both TIKU_EXAMPLE_BITBANG=1 and TIKU_BITBANG_ENABLE=1.
TIKU_BITBANG_ENABLE ?= 0
ifeq ($(TIKU_EXAMPLE_BITBANG),1)
override TIKU_BITBANG_ENABLE := 1
endif
ifeq ($(TIKU_EXAMPLE_CRIT_DEFER),1)
override TIKU_BITBANG_ENABLE := 1
endif
# Bit-bang C tests need the engine compiled in too.
ifeq ($(TEST_BITBANG),1)
override TIKU_BITBANG_ENABLE := 1
endif
ifeq ($(TIKU_BITBANG_ENABLE),1)
CFLAGS += -DTIKU_BITBANG_ENABLE=1
SRCS += kernel/timers/tiku_bitbang.c
endif
SRCS += interfaces/led/tiku_led.c
SRCS += interfaces/bus/tiku_i2c_bus.c
SRCS += interfaces/bus/tiku_spi_bus.c
ifeq ($(TIKU_PLATFORM),msp430)
SRCS += arch/msp430/tiku_spi_arch.c
endif
SRCS += interfaces/adc/tiku_adc.c
SRCS += interfaces/onewire/tiku_onewire.c
# Segment-LCD interface — generic glue is always compiled (it
# becomes a set of no-ops when TIKU_BOARD_HAS_LCD == 0). The arch
# driver self-gates on TIKU_DEVICE_HAS_LCD_C + TIKU_BOARD_HAS_LCD,
# so it's safe to include unconditionally too: parts/boards
# without an LCD compile it to an empty translation unit.
SRCS += interfaces/lcd/tiku_lcd.c
ifeq ($(TIKU_PLATFORM),msp430)
SRCS += arch/msp430/tiku_lcd_arch.c
endif
SRCS += kernel/memory/tiku_mem.c
SRCS += kernel/memory/tiku_pool.c
SRCS += kernel/memory/tiku_mpu.c
SRCS += kernel/memory/tiku_persist.c
SRCS += kernel/memory/tiku_region.c
SRCS += kernel/memory/tiku_tier.c
SRCS += kernel/memory/tiku_nvm_region.c
SRCS += kernel/memory/tiku_cache.c
SRCS += kernel/memory/tiku_hibernate.c
SRCS += kernel/memory/tiku_proc_mem.c
SRCS += kernel/process/tiku_process.c
SRCS += kernel/process/tiku_proc_vfs.c
SRCS += kernel/process/tiku_lc_persist.c
SRCS += kernel/scheduler/tiku_sched.c
SRCS += kernel/vfs/tiku_vfs.c
SRCS += kernel/vfs/tiku_vfs_cache.c
SRCS += kernel/vfs/tiku_vfs_tree.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_sys.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_boot.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_timer.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_watchdog.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_power.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_persist.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_watch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_dev.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_gpio.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_inittab.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_data.c

# File store backing the dynamic /data directory (self-gated; the data tree
# module above references it only when the shell is built).
SRCS += kernel/fs/tiku_tfs.c

# ---------------------------------------------------------------------------
# Shell (kernel service — compiled when TIKU_SHELL_ENABLE=1)
# ---------------------------------------------------------------------------
ifeq ($(TIKU_SHELL_ENABLE),1)
CFLAGS += -DTIKU_SHELL_ENABLE=1
ifeq ($(TIKU_SHELL_COLOR),1)
CFLAGS += -DTIKU_SHELL_COLOR=1
endif
SRCS += kernel/shell/tiku_shell_io.c
SRCS += kernel/shell/tiku_shell_parser.c
SRCS += kernel/shell/tiku_shell.c
SRCS += kernel/shell/tiku_shell_pump.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ps.c
SRCS += kernel/shell/commands/tiku_shell_cmd_info.c
SRCS += kernel/shell/commands/tiku_shell_cmd_timer.c
SRCS += kernel/shell/commands/tiku_shell_cmd_kill.c
SRCS += kernel/shell/commands/tiku_shell_cmd_resume.c
SRCS += kernel/shell/commands/tiku_shell_cmd_queue.c
SRCS += kernel/shell/commands/tiku_shell_cmd_reboot.c
SRCS += kernel/shell/commands/tiku_shell_cmd_trng.c
# The mrambench command benches the Ambiq bootrom MRAM programmer — only
# compile it on Ambiq (the shell config gates the table entry the same way).
ifeq ($(TIKU_PLATFORM),ambiq)
SRCS += kernel/shell/commands/tiku_shell_cmd_mrambench.c
endif
# The ble command runs the EM9305 radio first-contact probe; only compiled with
# the BLE driver (apollo510b). The driver + -D flags live in the BLE block near
# the Ambiq part selectors.
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_ble.c
endif
ifeq (,$(findstring TIKU_SHELL_CMD_HISTORY=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_history.c
endif
# The wifi shell command needs the CYW43 driver — only compile it
# when the driver is enabled. (The shell config gates the table
# entry the same way.)
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_wifi.c
endif
ifeq ($(TIKU_DRV_WIFI_CYW43_BT_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_bt.c
endif
SRCS += kernel/shell/commands/tiku_shell_cmd_ls.c
SRCS += kernel/shell/tiku_shell_cwd.c
SRCS += kernel/shell/commands/tiku_shell_cmd_cd.c
SRCS += kernel/shell/commands/tiku_shell_cmd_toggle.c
SRCS += kernel/shell/commands/tiku_shell_cmd_start.c
SRCS += kernel/shell/commands/tiku_shell_cmd_write.c
SRCS += kernel/shell/commands/tiku_shell_cmd_read.c
SRCS += kernel/shell/commands/tiku_shell_cmd_fs.c
SRCS += kernel/shell/commands/tiku_shell_cmd_watch.c
# slip command: only when the net stack is compiled in (it starts the net
# process). Keeps one OS image: interactive shell by default, SLIP/IP on
# demand via the `slip` command.
ifeq ($(TIKU_KIT_NET_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_slip.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ping.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ip.c
# ntp command (SNTP client): on by default with net.  It needs the time kit,
# so enabling it flips TIKU_KIT_TIME_ENABLE (see the time-kit block below).
# Drop both with EXTRA_CFLAGS="-DTIKU_SHELL_CMD_NTP=0".
ifeq (,$(findstring TIKU_SHELL_CMD_NTP=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_ntp.c
TIKU_KIT_TIME_ENABLE := 1
endif
# dns command (A-record lookup): on by default with net.  The DNS stub
# resolver is already compiled via the net-kit wildcard, so no extra kit.
ifeq (,$(findstring TIKU_SHELL_CMD_DNS=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_dns.c
endif
# syslog command (RFC 3164 remote log): on by default with net.  The syslog
# client is already compiled via the net-kit wildcard.
ifeq (,$(findstring TIKU_SHELL_CMD_SYSLOG=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_syslog.c
endif
endif
ifeq (,$(findstring TIKU_SHELL_CMD_CALC=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_calc.c
endif
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
CFLAGS += -DTIKU_SHELL_CMD_BASIC=1
SRCS += kernel/shell/basic/tiku_basic.c
SRCS += kernel/shell/commands/tiku_shell_cmd_basic.c
endif

# Embedded BASIC: BASIC_PROGRAM=foo.bas turns into a C string literal
# baked into the firmware. main.c picks it up at boot when
# TIKU_BASIC_EMBEDDED is set.
#
# The generated .c lives inside $(BUILD_DIR), which the standard
# pattern rule `$(BUILD_DIR)/%.o: %.c` can't reach (the stem would
# end up referring back into $(BUILD_DIR)). So we ship explicit
# generation + compile rules for it and append directly to OBJS
# below (after OBJS is normally derived from SRCS).
ifneq ($(BASIC_PROGRAM),)
TIKU_BASIC_EMBEDDED_C := $(BUILD_DIR)/embedded_bas.c
TIKU_BASIC_EMBEDDED_O := $(BUILD_DIR)/embedded_bas.o
CFLAGS += -DTIKU_BASIC_EMBEDDED=1
endif
SRCS += kernel/shell/tiku_shell_jobs.c
SRCS += kernel/shell/commands/tiku_shell_cmd_every.c
SRCS += kernel/shell/commands/tiku_shell_cmd_once.c
SRCS += kernel/shell/commands/tiku_shell_cmd_jobs.c
SRCS += kernel/shell/tiku_shell_rules.c
SRCS += kernel/shell/commands/tiku_shell_cmd_on.c
SRCS += kernel/shell/commands/tiku_shell_cmd_rules.c
SRCS += kernel/shell/commands/tiku_shell_cmd_changed.c
SRCS += kernel/shell/commands/tiku_shell_cmd_gpio.c
SRCS += kernel/shell/commands/tiku_shell_cmd_adc.c
SRCS += kernel/shell/commands/tiku_shell_cmd_free.c
ifeq (,$(findstring TIKU_SHELL_CMD_DF=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_df.c
endif
SRCS += kernel/shell/commands/tiku_shell_cmd_sleep.c
SRCS += kernel/shell/commands/tiku_shell_cmd_wake.c
SRCS += kernel/shell/commands/tiku_shell_cmd_freq.c
SRCS += kernel/shell/commands/tiku_shell_cmd_name.c
ifeq (,$(findstring TIKU_SHELL_CMD_IF=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_if.c
endif
SRCS += kernel/shell/commands/tiku_shell_cmd_irq.c
SRCS += kernel/shell/commands/tiku_shell_cmd_tree.c
SRCS += kernel/shell/commands/tiku_shell_cmd_clear.c
SRCS += kernel/shell/commands/tiku_shell_cmd_echo.c
SRCS += kernel/shell/commands/tiku_shell_cmd_lcd.c
SRCS += kernel/shell/tiku_shell_alias.c
SRCS += kernel/shell/commands/tiku_shell_cmd_alias.c
SRCS += kernel/shell/commands/tiku_shell_cmd_unalias.c
ifneq (,$(findstring TIKU_SHELL_CMD_I2C=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_i2c.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_DELAY=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_delay.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_REPEAT=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_repeat.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_PEEK=1,$(EXTRA_CFLAGS))$(findstring TIKU_SHELL_CMD_POKE=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_mem.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_NVMPROBE=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_nvmprobe.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_CRYPTOPROBE=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_cryptoprobe.c
endif
ifneq (,$(findstring TIKU_SHELL_CMD_AXONSPROBE=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_axonsprobe.c
endif

# Axon NPU (nRF54LM20B) -- opt-in.  Links Nordic's Axon driver core from the
# LOCAL, GITIGNORED checkout of github.com/nordicsemi-neuton/
# nrf54lm20b-axon-audio-models (LicenseRef-Nordic-5-Clause: linked at build
# time, never vendored -- the CRACEN PK microcode policy).  The TikuOS-side
# platform layer (arch/nordic/tiku_axon_platform.c) provides the ~11
# nrf_axon_platform_* functions the blob expects.
ifeq ($(TIKU_AXON_ENABLE),1)
ifeq ($(filter nrf54lm20b,$(MCU)),)
$(error TIKU_AXON_ENABLE=1 needs MCU=nrf54lm20b -- the Axon NPU exists only \
on the nRF54LM20B (the LM20A lacks the block))
endif
AXON_SDK ?= temp/axon-models/lib/axon
ifeq ($(wildcard $(AXON_SDK)/lib/axon/bin/arm/libnrf-axon-driver-internal.a),)
$(error TIKU_AXON_ENABLE=1 needs the Axon checkout at $(AXON_SDK) -- \
git clone https://github.com/nordicsemi-neuton/nrf54lm20b-axon-audio-models \
temp/axon-models)
endif
SRCS   += arch/nordic/tiku_axon_platform.c
CFLAGS += -DTIKU_AXON_ENABLE=1
CFLAGS += -I$(AXON_SDK)/include
LDLIBS_AXON = $(AXON_SDK)/lib/axon/bin/arm/libnrf-axon-driver-internal.a

# Optional compiled-model inference KAT: TIKU_AXON_MODEL=tinyml_kws (or
# tinyml_vww / tinyml_ic / tinyml_ad).  Compiles Nordic's public nn-infer
# sources + their portable test app (base_inference_main, invoked via
# `axonsprobe model`) against the model/test-vector headers shipped in the
# checkout.  The interlayer working buffer lives in RAM2 (size per model;
# 140000 covers the shipped tinyml set -- the model init verifies and
# reports the exact need on mismatch).
ifneq ($(strip $(TIKU_AXON_MODEL)),)
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer_test.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_op_extensions.c
SRCS += $(AXON_SDK)/lib/axon/platform/src/nrf_axon_logging.c
# newlib-nano's inttypes.h omits the 64-bit PRI macros this toolchain-wide;
# the vendor logging code uses PRId64/PRIx64 in failure-path vector dumps.
# Defining them here is conflict-free (the header genuinely lacks them).
CFLAGS += -DPRId64='"lld"' -DPRIx64='"llx"'
SRCS += $(AXON_SDK)/lib/axon/platform/src/nrf_axon_vector_compare.c
SRCS += $(AXON_SDK)/tests/axon/inference/src/nrf_axon_app_test_nn_inference.c
CFLAGS += -DNRF_AXON_MODEL_NAME=$(TIKU_AXON_MODEL)
CFLAGS += -DTIKU_AXON_MODEL_TEST=1
CFLAGS += -I$(AXON_SDK)/tests/axon/compiled_models
TIKU_AXON_ILB ?= 140000
CFLAGS += -DNRF_AXON_INTERLAYER_BUFFER_SIZE=$(TIKU_AXON_ILB)
CFLAGS += -DNRF_AXON_PSUM_BUFFER_SIZE=$(TIKU_AXON_PSUM)
TIKU_AXON_PSUM ?= 0
else
CFLAGS += -DNRF_AXON_INTERLAYER_BUFFER_SIZE=0 -DNRF_AXON_PSUM_BUFFER_SIZE=0
endif
endif
ifneq (,$(findstring TIKU_SHELL_CMD_BLEADV=1,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_bleadv.c
endif
endif
# GPIO arch is always needed (VFS tree references GPIO read/write/dir).
# RP2350 GPIO is bundled with the rp2350 arch sources at the top of the
# SRCS block so it doesn't get added a second time here.
ifeq ($(TIKU_PLATFORM),msp430)
SRCS += arch/msp430/tiku_gpio_arch.c
endif

# ---------------------------------------------------------------------------
# Init system (NVM-backed configurable boot — requires shell)
# ---------------------------------------------------------------------------
ifeq ($(TIKU_INIT_ENABLE),1)
CFLAGS += -DTIKU_INIT_ENABLE=1
SRCS += kernel/memory/tiku_nvm_map.c
SRCS += kernel/init/tiku_init.c
SRCS += kernel/shell/commands/tiku_shell_cmd_init.c
endif

# ---------------------------------------------------------------------------
# Tests (firmware test sources live in the TikuBench repo)
# ---------------------------------------------------------------------------
# The TikuOS test tree was moved to TikuBench/tests/ so the test system
# (host harness + firmware sources + the marker/flag contract) is
# self-contained in one repo.  This fragment lists the test SRCS and adds
# the -I so <tests/...> includes resolve.  Pulled in only for the test
# build; the leading-dash -include means a tikuOS checkout WITHOUT
# TikuBench still builds (production: HAS_TESTS=0, nothing here triggers).
-include $(PROJ_DIR)/TikuBench/tests/tests.mk

# tikukits/gfx visual test runner
# An on-target autostart process that owns UART, listens for single-
# character commands, and renders gfx scenes on the e-paper panel.
# Driven from the host by TikuBench/tikubench/gfx_test.py. Opt-in
# only -- conflicts with the shell because both want UART input.
ifeq ($(TEST_KITS_GFX_VISUAL),1)
SRCS   += TikuBench/tests/kits/gfx/test_kits_gfx_visual.c
CFLAGS += -DTEST_KITS_GFX_VISUAL=1
TIKU_KIT_GFX_ENABLE    := 1
TIKU_KIT_EPAPER_ENABLE := 1
endif

# tikukits/ui visual test runner: same UART-command pattern as the gfx
# variant but renders UI widget compositions. Driven by
# TikuBench/tikubench/ui_test.py. Conflicts with the shell -- both
# want UART input -- so set TIKU_SHELL_ENABLE=0.
ifeq ($(TEST_KITS_UI_VISUAL),1)
SRCS   += TikuBench/tests/kits/ui/test_kits_ui_visual.c
CFLAGS += -DTEST_KITS_UI_VISUAL=1
TIKU_KIT_UI_ENABLE     := 1
TIKU_KIT_GFX_ENABLE    := 1
TIKU_KIT_EPAPER_ENABLE := 1
endif

# ---------------------------------------------------------------------------
# Examples (only if examples/ is present)
# ---------------------------------------------------------------------------
ifeq ($(HAS_EXAMPLES),1)
SRCS += examples/01_blink/blink.c
SRCS += examples/02_dual_blink/dual_blink.c
SRCS += examples/03_button_led/button_led.c
SRCS += examples/04_multi_process/multi_process.c
SRCS += examples/05_state_machine/state_machine.c
SRCS += examples/06_callback_timer/callback_timer.c
SRCS += examples/07_broadcast/broadcast.c
SRCS += examples/08_timeout/timeout.c
SRCS += examples/09_channel/channel.c
SRCS += examples/10_i2c_temp/i2c_temp.c
SRCS += examples/11_ds18b20_temp/ds18b20_temp.c
SRCS += examples/12_udp_send/udp_send.c
SRCS += examples/13_tcp_send/tcp_send.c
SRCS += examples/14_dns_resolve/dns_resolve.c
SRCS += examples/15_http_get/http_get.c
SRCS += examples/16_tcp_echo/tcp_echo.c
SRCS += examples/17_http_fetch/http_fetch.c
SRCS += examples/18_http_direct/http_direct.c
SRCS += examples/19_https_direct/https_direct.c

# New examples (20+): gated by their TIKU_EXAMPLE_<NAME>=1 flag so
# only the active one is compiled. This avoids stale .o files from
# previous builds defining duplicate tiku_autostart_processes when
# the user switches between examples without `make clean`.
ifeq ($(TIKU_EXAMPLE_BITBANG),1)
SRCS += examples/20_bitbang/bitbang.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_BITBANG=1
endif
ifeq ($(TIKU_EXAMPLE_CRIT_DEFER),1)
SRCS += examples/21_crit_defer/crit_defer.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_CRIT_DEFER=1
endif
ifeq ($(TIKU_EXAMPLE_CLOCK_FAULT),1)
SRCS += examples/22_clock_fault/clock_fault.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_CLOCK_FAULT=1
endif
ifeq ($(TIKU_EXAMPLE_LCD_DEMO),1)
SRCS += examples/23_lcd_demo/lcd_demo.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_LCD_DEMO=1
endif
ifeq ($(TIKU_EXAMPLE_EPAPER),1)
SRCS += examples/kits_examples/epaper_demo/epaper_demo.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_EPAPER=1
endif
ifeq ($(TIKU_EXAMPLE_EPAPER_KIT),1)
SRCS += examples/kits_examples/epaper_kit/epaper_kit.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_EPAPER_KIT=1
endif
ifeq ($(TIKU_EXAMPLE_GFX_DEMO),1)
SRCS += examples/kits_examples/gfx_demo/gfx_demo.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_GFX_DEMO=1
endif
ifeq ($(TIKU_EXAMPLE_UI_DEMO),1)
SRCS += examples/kits_examples/ui_demo/ui_demo.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_DEMO=1
endif

# kits_examples: new gfx + ui demos that exercise the expanded
# tikukits/gfx and tikukits/ui kits.
ifeq ($(TIKU_EXAMPLE_GFX_CURVES),1)
SRCS += examples/kits_examples/gfx_curves/gfx_curves.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_GFX_CURVES=1
endif
ifeq ($(TIKU_EXAMPLE_GFX_IMAGE),1)
SRCS += examples/kits_examples/gfx_image/gfx_image.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_GFX_IMAGE=1
endif
ifeq ($(TIKU_EXAMPLE_UI_WIDGET_ZOO),1)
SRCS += examples/kits_examples/ui_widget_zoo/ui_widget_zoo.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_WIDGET_ZOO=1
endif
ifeq ($(TIKU_EXAMPLE_UI_DASHBOARD),1)
SRCS += examples/kits_examples/ui_dashboard/ui_dashboard.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_DASHBOARD=1
endif
ifeq ($(TIKU_EXAMPLE_UI_MENU),1)
SRCS += examples/kits_examples/ui_menu/ui_menu.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_MENU=1
endif

# Phase 0 + Phase 3 demos.
ifeq ($(TIKU_EXAMPLE_GFX_PHASE0),1)
SRCS += examples/kits_examples/gfx_phase0/gfx_phase0.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_GFX_PHASE0=1
endif
ifeq ($(TIKU_EXAMPLE_UI_LAYOUT),1)
SRCS += examples/kits_examples/ui_layout/ui_layout.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_LAYOUT=1
endif
ifeq ($(TIKU_EXAMPLE_UI_SETTINGS),1)
SRCS += examples/kits_examples/ui_settings/ui_settings.c
CFLAGS += -DTIKU_EXAMPLES_ENABLE=1 -DTIKU_EXAMPLE_UI_SETTINGS=1
endif

# TikuKits examples (requires examples/ + tikukits/). Each per-domain
# example folder is gated on its corresponding kit-enable flag, so a
# build of (say) `make HAS_EXAMPLES=1 TIKU_EXAMPLE_KITS_MATRIX=1`
# compiles ONLY examples/kits/maths/*.c -- nothing from ds/ ml/
# sensors/ etc. The `*_runner.c` dispatcher is always compiled when
# any kits-example is in scope; its body is #if'd out when none of
# the per-example flags are set.
ifeq ($(HAS_TIKUKITS),1)

# Always compile the dispatcher when HAS_TIKUKITS + HAS_EXAMPLES are
# both set. Its body is gated per-demo via TIKU_EXAMPLE_KITS_* flags
# inside the .c file, so an empty config compiles to a small no-op
# stub. This keeps main.c's call to example_kits_run() resolvable
# without forcing every kit-using app to manually enable a kit.
SRCS += examples/kits/example_kits_runner.c

ifeq ($(TIKU_KIT_MATHS_ENABLE),1)
SRCS += $(wildcard examples/kits/maths/*.c)
endif
ifeq ($(TIKU_KIT_DS_ENABLE),1)
SRCS += $(wildcard examples/kits/ds/*.c)
endif
ifeq ($(TIKU_KIT_ML_ENABLE),1)
SRCS += $(wildcard examples/kits/ml/*.c)
endif
ifeq ($(TIKU_KIT_SENSORS_ENABLE),1)
SRCS += $(wildcard examples/kits/sensors/*.c)
endif
ifeq ($(TIKU_KIT_SIGFEATURES_ENABLE),1)
SRCS += $(wildcard examples/kits/sigfeatures/*.c)
endif
ifeq ($(TIKU_KIT_TEXTCOMPRESSION_ENABLE),1)
SRCS += $(wildcard examples/kits/textcompression/*.c)
endif
ifeq ($(TIKU_KIT_NET_ENABLE),1)
SRCS += $(filter-out examples/kits/net/example_net_tls.c, \
          $(wildcard examples/kits/net/*.c))
ifeq ($(HAS_TLS),1)
SRCS += examples/kits/net/example_net_tls.c
endif
endif

endif # HAS_TIKUKITS (examples)
endif # HAS_EXAMPLES

# ---------------------------------------------------------------------------
# Apps (only when APP=<name> is specified)
# ---------------------------------------------------------------------------
ifeq ($(APP),cli)
CFLAGS += -DTIKU_APP_CLI=1
# Shell sources now come from kernel/shell/ (see TIKU_SHELL_ENABLE above)
endif

ifeq ($(APP),net)
# The net app source lives in the TikuBench harness (see TIKU_APP_DIR above).
# Guard against a missing path so the failure is legible; exempt `clean`,
# which parses this block but compiles nothing.
ifeq ($(strip $(TIKU_APP_DIR)),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error APP=net needs TIKU_APP_DIR=<dir containing net/tiku_app_net.c>; the app firmware lives in the TikuBench harness now, not core tikuOS)
endif
endif
CFLAGS += -DTIKU_APP_NET=1
SRCS += $(TIKU_APP_DIR)/net/tiku_app_net.c
# CoAP client/server (library + demo process) lives in tikukits/net/coap/.
# Pull it in and define TIKU_KITS_NET_COAP so the net app starts the CoAP
# server process; the C side #if's its use on the same flag.
SRCS   += $(wildcard tikukits/net/coap/*.c)
CFLAGS += -DTIKU_KITS_NET_COAP=1
endif

# Shell net-test mode: activate TCP and pull in the CoAP server so the shell
# firmware can answer the TikuBench net suite (gated; see TIKU_SHELL_NET_TEST).
ifeq ($(TIKU_SHELL_NET_TEST),1)
CFLAGS += -DTIKU_SHELL_NET_TEST=1 -DTIKU_KITS_NET_TCP_ENABLE=1
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
CFLAGS += -DTIKU_SHELL_TCP_ENABLE=1
SRCS   += $(wildcard tikukits/net/coap/*.c)
CFLAGS += -DTIKU_KITS_NET_COAP=1
SRCS += kernel/shell/commands/tiku_shell_cmd_mqtt.c
SRCS += kernel/shell/tiku_shell_io_tcp.c
endif

# Optional out-of-tree overlay hook.  Silently included if present
# (the file lives outside the source tree and is not part of the
# public repo).  Lets local builds add extra sources / build rules
# without touching this Makefile.
-include prop/Makefile.inc

# ---------------------------------------------------------------------------
# TikuKits (per-kit gated; default 0 unless an app/example needs it)
#
# Only enabled kits compile their sources. A kernel-only build
# (no apps, no examples) compiles ZERO files from tikukits/.
# ---------------------------------------------------------------------------

# Turbo benchmark (TIKU_TURBO_BENCH=1): an app-layer firmware that runs heavy
# TikuKits workloads at 96 MHz (LP) and 192 MHz (HP) and emits serial markers
# so the host times the wall-clock speedup. Enable the crypto/maths/ml kits and
# add the benchmark source (after the SRCS=main.c reset, so it sticks); main.c
# calls turbo_bench_run() then halts. kernel/ is untouched.
ifeq ($(TIKU_TURBO_BENCH),1)
# The benchmark firmware source lives in the TikuBench harness (TIKU_APP_DIR).
ifeq ($(strip $(TIKU_APP_DIR)),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error TIKU_TURBO_BENCH=1 needs TIKU_APP_DIR=<dir containing turbo_bench/turbo_bench.c>; the benchmark firmware lives in the TikuBench harness now)
endif
endif
CFLAGS += -DTIKU_TURBO_BENCH=1
TIKU_KIT_CRYPTO_ENABLE := 1
TIKU_KIT_MATHS_ENABLE  := 1
TIKU_KIT_ML_ENABLE     := 1
SRCS   += $(TIKU_APP_DIR)/turbo_bench/turbo_bench.c
endif

ifeq ($(HAS_TIKUKITS),1)

ifeq ($(TIKU_KIT_GFX_ENABLE),1)
CFLAGS += -DTIKU_KIT_GFX_ENABLE=1
SRCS   += $(wildcard tikukits/gfx/*.c)
SRCS   += $(wildcard tikukits/gfx/fonts/*.c)
SRCS   += $(wildcard tikukits/gfx/icons/*.c)
endif

ifeq ($(TIKU_KIT_UI_ENABLE),1)
CFLAGS += -DTIKU_KIT_UI_ENABLE=1
SRCS   += $(wildcard tikukits/ui/*.c)
SRCS   += $(wildcard tikukits/ui/widgets/*.c)
endif

ifeq ($(TIKU_KIT_EPAPER_ENABLE),1)
CFLAGS += -DTIKU_KIT_EPAPER_ENABLE=1
SRCS   += $(wildcard tikukits/epaper/*.c)
SRCS   += $(wildcard tikukits/epaper/pervasive_itc/*.c)
endif

ifeq ($(TIKU_KIT_NET_ENABLE),1)
CFLAGS += -DTIKU_KIT_NET_ENABLE=1
# Override the device IPv4 address at build time, e.g. `make ... IP=10.0.0.5`.
# tiku_kits_net.h defaults TIKU_KITS_NET_IP_ADDR to {172,16,7,2} behind an
# #ifndef; turn a dotted quad into that brace-list (quoted so the shell does
# not brace-expand it).  NOTE: CFLAGS changes are not dependency-tracked, so
# `make clean` when you change IP.
ifdef IP
comma := ,
CFLAGS += -DTIKU_KITS_NET_IP_ADDR="{$(subst .,$(comma),$(IP))}"
endif
SRCS   += $(wildcard tikukits/net/slip/*.c)
# IPv4 base set. Drops the heavy protocol modules when their per-flag
# is off — each declares static buffers via __attribute__((section(
# ".persistent"))) regardless of compile-time gates, and the RP2350
# .persistent backup sector is 4 KB hard cap. Most demos need only
# ipv4 + icmp + udp.
ifeq ($(TIKU_KIT_NET_MIN),1)
# Expose MIN to the C preprocessor so shell commands whose kits live only
# in the non-MIN wildcard (e.g. syslog) can gate themselves off.
CFLAGS += -DTIKU_KIT_NET_MIN=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_ipv4.c
SRCS   += tikukits/net/ipv4/tiku_kits_net_icmp.c
SRCS   += tikukits/net/ipv4/tiku_kits_net_udp.c
# Opt-in DHCP for MIN builds. dhcp.c has no .persistent buffers so
# it fits under the 4 KB cap. Demo A (host-pings-Pico) uses it to
# acquire an IP automatically instead of hardcoding.
ifeq ($(TIKU_KITS_NET_DHCP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_DHCP_ENABLE=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_dhcp.c
endif
# Opt-in DNS stub resolver for MIN builds.  The non-MIN path already pulls
# the whole ipv4/ directory; MIN omits it, but the ntp/dns shell commands
# reference the resolver symbols, so opt it in when those are wanted.  dns.c
# is a tiku_kits_net_*.o so its working buffer is relocated out of the .uninit
# backup window by the linker script -- no 4 KB-cap impact.
ifeq ($(TIKU_KITS_NET_DNS_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_DNS_ENABLE=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_dns.c
endif
# Opt-in TCP + MQTT/HTTP for MIN builds (e.g. BASIC MQTTPUB / HTTPGET$ on a
# lean WiFi profile).  TCP is the shared transport; MQTT and HTTP each add
# their kit on top.  These keep their working buffers in tiku_kits_net_*.o
# sections, relocated out of the .uninit backup window -- no 4 KB-cap impact.
# http is HTTPS-only, so pair TIKU_KITS_NET_HTTP_ENABLE=1 with
# TIKU_KIT_CRYPTO_ENABLE=1 HAS_TLS=1 (+ a TRNG-backed RNG_FILL, which the TLS
# config header defaults for PLATFORM_RP2350).
ifneq ($(filter 1,$(TIKU_KITS_NET_MQTT_ENABLE) $(TIKU_KITS_NET_HTTP_ENABLE)),)
CFLAGS += -DTIKU_KITS_NET_TCP_ENABLE=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_tcp.c
endif
ifeq ($(TIKU_KITS_NET_MQTT_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
# Same guard as the full-net branch below: the kit flag auto-enables the
# shell `mqtt` command, whose .c (+ SLIP command + TCP shell-io backend)
# only the TIKU_SHELL_NET_TEST block compiles. Keep it off here too, or
# any lean shell+MQTT build dies with undefined references.
CFLAGS += -DTIKU_SHELL_CMD_MQTT=0
SRCS   += tikukits/net/mqtt/tiku_kits_net_mqtt.c
endif
ifeq ($(TIKU_KITS_NET_HTTP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_HTTP_ENABLE=1
SRCS   += $(wildcard tikukits/net/http/*.c)
endif
else
SRCS   += $(wildcard tikukits/net/ipv4/*.c)
SRCS   += $(wildcard tikukits/net/http/*.c)
SRCS   += $(wildcard tikukits/net/mqtt/*.c)
# The wildcards above compile the MQTT/HTTP clients, but the enable -D that
# the BASIC/shell builtins gate on -- MQTTPUB / MQTTWAIT$ via
# `#if (TIKU_KITS_NET_MQTT_ENABLE + 0)`, HTTPGET$ via TIKU_KITS_NET_HTTP_ENABLE
# -- is only set in the MIN branch above. Without it those words compile out
# even though their client is linked in (dead code). Propagate the flags into
# the full-net profile too, opt-in, so requesting the client actually exposes
# the language/shell surface for it.
ifeq ($(TIKU_KITS_NET_MQTT_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
# The kit flag auto-enables the shell `mqtt` command (tiku_shell_config.h),
# but that command pulls in the SLIP command + TCP shell-io backend which the
# NET_TEST block compiles alongside it and this full-net profile does not. The
# BASIC MQTT words (MQTTWAIT$/MQTTPUB) are gated on the kit flag alone, so keep
# the shell command off here to avoid an undefined-reference link error.
CFLAGS += -DTIKU_SHELL_CMD_MQTT=0
endif
ifeq ($(TIKU_KITS_NET_HTTP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_HTTP_ENABLE=1
endif
endif
# WiFi link backend: requires both the CYW43 driver and the net kit.
# Compiled only when the build wires both submodules together.
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
ifeq ($(TIKU_KITS_NET_WIFI_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_WIFI_ENABLE=1
SRCS   += $(wildcard tikukits/net/wifi/*.c)
endif
endif
endif

# Bluetooth Low Energy protocol stack: driver-agnostic. Pulled in
# whenever a Bluetooth-capable driver is enabled. Today that's only
# TIKU_DRV_WIFI_CYW43_BT_ENABLE (CYW43439 BTSDIO transport); a future
# UART-HCI driver for Nordic / ESP32 / TI parts would set its own
# enable flag and we'd OR it in here.
ifeq ($(TIKU_DRV_WIFI_CYW43_BT_ENABLE),1)
include $(wildcard $(PROJ_DIR)/tikukits/net/bluetooth/build.mk)
endif

# Scratch demos: pulled in only when DEMO=<dir> is on the make line.
# Located after the SRCS=main.c reset above so it actually sticks.
ifeq ($(HAS_DEMOS),1)
SRCS   += $(wildcard demos/$(DEMO)/*.c)
endif

# CRACEN hardware public-key offload (nRF54L15 only, opt-in).  Enables the
# BA414EP ECDSA-verify path (481x over software) behind the runtime mode knob,
# but the engine is microcoded and TikuOS ships NO microcode (Nordic-
# proprietary): a build must ALSO drop its own licensed cracen_pk_microcode.h
# next to arch/nordic/tiku_crypto_arch.c.  Without it, every hardware verify
# fails safe to software.  Default off; the SHA/AES-GCM CryptoMaster offload
# needs none of this and is always on for nordic.
ifeq ($(TIKU_CRACEN_PK_ENABLE),1)
CFLAGS += -DTIKU_CRACEN_PK_ENABLE=1
endif

ifeq ($(TIKU_KIT_CRYPTO_ENABLE),1)
CFLAGS += -DTIKU_KIT_CRYPTO_ENABLE=1
SRCS   += $(wildcard tikukits/crypto/sha256/*.c)
SRCS   += $(wildcard tikukits/crypto/sha384/*.c)
SRCS   += $(wildcard tikukits/crypto/base64/*.c)
SRCS   += $(wildcard tikukits/crypto/crc/*.c)
SRCS   += $(wildcard tikukits/crypto/gcm/*.c)
SRCS   += $(wildcard tikukits/crypto/aes128/*.c)
SRCS   += $(wildcard tikukits/crypto/hkdf/*.c)
SRCS   += $(wildcard tikukits/crypto/hmac/*.c)
SRCS   += $(wildcard tikukits/crypto/x25519/*.c)
SRCS   += $(wildcard tikukits/crypto/p256/*.c)
SRCS   += $(wildcard tikukits/crypto/p384/*.c)
SRCS   += $(wildcard tikukits/crypto/rsa/*.c)
# MSP430 has no hardware TRNG: its SHA-256-conditioned software entropy
# source lives in the arch layer and is only linkable with the crypto kit.
ifeq ($(TIKU_PLATFORM),msp430)
SRCS   += arch/msp430/tiku_trng_arch.c
endif
SRCS   += $(wildcard tikukits/net/tls/x509/*.c)
# TLS pulls in additional code; gated separately on HAS_TLS=1
# because tiku_kits_crypto_tls requires the platform to provide
# TIKU_KITS_CRYPTO_TLS_RNG_FILL.
ifeq ($(HAS_TLS),1)
SRCS   += $(wildcard tikukits/net/tls/psk/*.c)
SRCS   += $(wildcard tikukits/net/tls/tls13/*.c)
SRCS   += $(wildcard tikukits/net/tls/tls12/*.c)
# The cert clients are now linked, so let the http kit route http_get()/
# http_post() over them (TIKU_KITS_NET_HTTP_CERT trust model) as well as the
# PSK client.  Off when HAS_TLS is unset -> the kit stays PSK-only and doesn't
# reference tls13/tls12.
ifeq ($(TIKU_KITS_NET_HTTP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_HTTP_CERT_ENABLE=1
endif
endif
endif

# BASE64$/SHA256$/HMAC$ BASIC builtins.  On by default whenever BASIC is
# built; TIKU_BASIC_CRYPTO=0 drops them (~3 KB) on the tightest parts.
# When the full crypto kit is already compiled the sources come from the
# block above, so we only add the -D and skip the (duplicate) source lines.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
TIKU_BASIC_CRYPTO ?= 1
ifeq ($(TIKU_BASIC_CRYPTO),1)
CFLAGS += -DTIKU_BASIC_CRYPTO_ENABLE=1
ifneq ($(TIKU_KIT_CRYPTO_ENABLE),1)
SRCS   += tikukits/crypto/sha256/tiku_kits_crypto_sha256.c
SRCS   += tikukits/crypto/base64/tiku_kits_crypto_base64.c
SRCS   += tikukits/crypto/hmac/tiku_kits_crypto_hmac.c
endif
endif
endif

ifeq ($(TIKU_KIT_TIME_ENABLE),1)
CFLAGS += -DTIKU_KIT_TIME_ENABLE=1
SRCS   += tikukits/time/tiku_kits_time.c
SRCS   += $(wildcard tikukits/time/ntp/*.c)
endif

ifeq ($(TIKU_KIT_CODEC_ENABLE),1)
CFLAGS += -DTIKU_KIT_CODEC_ENABLE=1
SRCS   += $(wildcard tikukits/codec/cbor/*.c)
SRCS   += $(wildcard tikukits/codec/json/*.c)
SRCS   += $(wildcard tikukits/codec/protobuf/*.c)
SRCS   += $(wildcard tikukits/codec/hex/*.c)
endif

ifeq ($(TIKU_KIT_MATHS_ENABLE),1)
CFLAGS += -DTIKU_KIT_MATHS_ENABLE=1
SRCS   += $(wildcard tikukits/maths/linear_algebra/*.c)
SRCS   += $(wildcard tikukits/maths/statistics/*.c)
SRCS   += $(wildcard tikukits/maths/distance/*.c)
endif

ifeq ($(TIKU_KIT_DS_ENABLE),1)
CFLAGS += -DTIKU_KIT_DS_ENABLE=1
SRCS   += $(wildcard tikukits/ds/array/*.c)
SRCS   += $(wildcard tikukits/ds/queue/*.c)
SRCS   += $(wildcard tikukits/ds/pqueue/*.c)
SRCS   += $(wildcard tikukits/ds/stack/*.c)
SRCS   += $(wildcard tikukits/ds/ringbuf/*.c)
SRCS   += $(wildcard tikukits/ds/bitmap/*.c)
SRCS   += $(wildcard tikukits/ds/list/*.c)
SRCS   += $(wildcard tikukits/ds/btree/*.c)
SRCS   += $(wildcard tikukits/ds/sortarray/*.c)
SRCS   += $(wildcard tikukits/ds/htable/*.c)
SRCS   += $(wildcard tikukits/ds/sm/*.c)
SRCS   += $(wildcard tikukits/ds/bloom/*.c)
SRCS   += $(wildcard tikukits/ds/circlog/*.c)
SRCS   += $(wildcard tikukits/ds/deque/*.c)
SRCS   += $(wildcard tikukits/ds/trie/*.c)
SRCS   += $(wildcard tikukits/ds/timerwheel/*.c)
endif

ifeq ($(TIKU_KIT_ML_ENABLE),1)
CFLAGS += -DTIKU_KIT_ML_ENABLE=1
SRCS   += $(wildcard tikukits/ml/regression/*.c)
SRCS   += $(wildcard tikukits/ml/classification/*.c)
endif

ifeq ($(TIKU_KIT_SENSORS_ENABLE),1)
CFLAGS += -DTIKU_KIT_SENSORS_ENABLE=1
SRCS   += $(wildcard tikukits/sensors/temperature/*.c)
endif

ifeq ($(TIKU_KIT_SIGFEATURES_ENABLE),1)
CFLAGS += -DTIKU_KIT_SIGFEATURES_ENABLE=1
SRCS   += $(wildcard tikukits/sigfeatures/peak/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/zcr/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/histogram/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/delta/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/goertzel/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/zscore/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/scale/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/ema/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/median/*.c)
SRCS   += $(wildcard tikukits/sigfeatures/skip/*.c)
endif

ifeq ($(TIKU_KIT_TEXTCOMPRESSION_ENABLE),1)
CFLAGS += -DTIKU_KIT_TEXTCOMPRESSION_ENABLE=1
SRCS   += $(wildcard tikukits/textcompression/rle/*.c)
SRCS   += $(wildcard tikukits/textcompression/bpe/*.c)
SRCS   += $(wildcard tikukits/textcompression/heatshrink/*.c)
endif

endif # HAS_TIKUKITS

endif # MINIMAL=1 / else

# Object files in build directory. ASM_SRCS is for .S files pulled in
# by build.mk fragments (e.g. firmware-blob .incbin wrappers); same
# CFLAGS as C, since the toolchain treats .S as preprocessed-and-then-
# assembled and we only need the include-path / -D macros.
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS)) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))

ifneq ($(BASIC_PROGRAM),)
# Append the embedded-BASIC object directly (the recipe lives below
# `all:` so it doesn't accidentally become the default goal).
OBJS += $(TIKU_BASIC_EMBEDDED_O)
endif

ifeq ($(TIKU_FLPR_ENABLE),1)
# Embedded FLPR coprocessor image (recipes below `all:`, same reason).
OBJS += $(TIKU_FLPR_IMG_O)
endif

# Header-dependency tracking.  Each compile emits a .d next to its .o (via
# -MMD -MP in the rules above) listing every header it pulled in; pull those
# back in so editing a header rebuilds exactly the objects that include it --
# no more stale objects (and no more `make clean` after a header edit).  -MP
# adds phony targets for each header so a later-deleted header can't break the
# build.  Silently absent on the first build, which is fine.
-include $(OBJS:.o=.d)

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
TARGET = main.elf

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.SUFFIXES:
.PHONY: all clean flash run debug erase size monitor deploy docs docs-clean uf2 lint

# Static placement lint: raw section(".persistent") outside the grade macros
# is the audit's silent-volatile bug class (kintsugi/memoryfix.md Phase A).
lint:
	@./tools/check_durable_placement.sh

# UF2 is the RP2350 deliverable; ELF is enough on MSP430.
ifeq ($(TIKU_PLATFORM),rp2350)
TARGET_BIN = main.bin
TARGET_UF2 = main.uf2
all: $(TARGET) $(TARGET_BIN) $(TARGET_UF2) size
else ifeq ($(TIKU_PLATFORM),ambiq)
TARGET_BIN = main.bin
all: $(TARGET) $(TARGET_BIN) size
else ifeq ($(TIKU_PLATFORM),nordic)
TARGET_HEX = main.hex
all: $(TARGET) $(TARGET_HEX) size
else
all: $(TARGET) size
endif

# main.elf in the project root is shared between MSP430 and RP2350
# builds. The .platform-stamp file (kept under build/, NOT
# build/$(MCU)/, so both platforms see the same one) is rewritten
# whenever the previous build was for a different platform — its
# timestamp advances and forces a relink.
PLATFORM_STAMP = build/.platform-stamp

ifneq ($(TIKU_PLATFORM),$(shell cat $(PLATFORM_STAMP) 2>/dev/null))
$(shell mkdir -p build && echo $(TIKU_PLATFORM) > $(PLATFORM_STAMP))
endif

$(PLATFORM_STAMP):
	@mkdir -p build
	@echo $(TIKU_PLATFORM) > $@

$(TARGET): $(OBJS) $(PLATFORM_STAMP) $(NOSYS_FIXED)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Assembly-source rule. Used by firmware-blob wrappers that pull in
# binary data via .incbin (see drivers/wifi/cyw43/firmware.S).
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# RP2350 outputs: .elf -> .bin -> .uf2 (the .uf2 is what the BOOTSEL
# mass-storage device wants).
ifeq ($(TIKU_PLATFORM),rp2350)
$(TARGET_BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@

$(TARGET_UF2): $(TARGET_BIN) tools/elf2uf2.py
	@python3 tools/elf2uf2.py $(TARGET_BIN) $(TARGET_UF2)
	@echo "  [uf2]   $(TARGET) -> $(TARGET_UF2)"

uf2: $(TARGET_UF2)
endif

# Apollo510: raw binary for J-Link load to MRAM 0x410000.
ifeq ($(TIKU_PLATFORM),ambiq)
$(TARGET_BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@
endif

# nRF54L15: Intel HEX for nrfutil to program into RRAM.
ifeq ($(TIKU_PLATFORM),nordic)
$(TARGET_HEX): $(TARGET)
	$(OBJCOPY) -O ihex $< $@
	@echo "  [hex]   $(TARGET) -> $(TARGET_HEX)"

hex: $(TARGET_HEX)
endif

# Embedded BASIC: the generated .c lives inside $(BUILD_DIR), so the
# pattern rule above can't reach it (it would loop on the prefix).
# Use explicit rules for both generation and compilation. These are
# defined AFTER `all:` so the embedded .c file does not steal the
# default goal.
ifneq ($(BASIC_PROGRAM),)
$(TIKU_BASIC_EMBEDDED_C): $(BASIC_PROGRAM) tools/bas_to_c.py
	@mkdir -p $(dir $@)
	@echo "  [bas]   $< -> $@"
	@python3 tools/bas_to_c.py $< $@

$(TIKU_BASIC_EMBEDDED_O): $(TIKU_BASIC_EMBEDDED_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<
endif

# ---------------------------------------------------------------------------
# FLPR (VPR RISC-V) coprocessor sub-build: compile with the RISC-V
# toolchain, link against the carve-resident script, flatten to a binary,
# then wrap that binary as an ARM object (blob in .rodata, RRAM) whose
# _binary_tiku_flpr_bin_* symbols the app-side loader memcpys from.  The
# wrap runs objcopy FROM INSIDE the build dir so the symbol names derive
# from the bare file name, not the build path.
# ---------------------------------------------------------------------------
ifeq ($(TIKU_FLPR_ENABLE),1)
$(FLPR_BUILD)/%.o: arch/nordic/flpr/%.S
	@mkdir -p $(dir $@)
	$(RISCV_CC) $(FLPR_CFLAGS) -c -o $@ $<

$(FLPR_BUILD)/%.o: arch/nordic/flpr/%.c
	@mkdir -p $(dir $@)
	$(RISCV_CC) $(FLPR_CFLAGS) -c -o $@ $<

$(FLPR_BUILD)/tiku_flpr.elf: $(FLPR_OBJS) arch/nordic/flpr/tiku_flpr.ld
	$(RISCV_CC) $(FLPR_CFLAGS) -T arch/nordic/flpr/tiku_flpr.ld \
	    -Wl,--gc-sections -o $@ $(FLPR_OBJS)

$(FLPR_BUILD)/tiku_flpr.bin: $(FLPR_BUILD)/tiku_flpr.elf
	$(RISCV_PREFIX)objcopy -O binary $< $@
	@echo "  [flpr]  $$(stat -c%s $@) bytes"

$(TIKU_FLPR_IMG_O): $(FLPR_BUILD)/tiku_flpr.bin
	cd $(FLPR_BUILD) && $(OBJCOPY) -I binary -O elf32-littlearm -B arm \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    tiku_flpr.bin tiku_flpr_img.o

-include $(FLPR_OBJS:.o=.d)
endif

size: $(TARGET)
	@echo ""
	@echo "===== Build Summary ($(MCU)) ====="
	@$(SIZE) $<
	@echo "=================================="

clean:
	rm -rf build/ $(TARGET)

# ---------------------------------------------------------------------------
# Presentations  (delegates to presentation/Makefile)
# ---------------------------------------------------------------------------
docs:
ifeq ($(HAS_PRESENTATION),1)
	@$(MAKE) -C presentation --no-print-directory
else
	@echo "Skipped: presentation/ directory not found"
endif

docs-clean:
ifeq ($(HAS_PRESENTATION),1)
	@$(MAKE) -C presentation distclean --no-print-directory
else
	@echo "Skipped: presentation/ directory not found"
endif

# ---------------------------------------------------------------------------
# Flash / Debug / Erase
# ---------------------------------------------------------------------------

# nRF54L15-DK flashing tool (Nordic's nrfutil): prefer a PATH `nrfutil`, else
# the vendored ./temp/nrfutil.  NRF_SN selects one J-Link probe by serial on a
# multi-DK rig (TikuBench passes it per board via NRF_SN=<serial>).
#
# nrfutil resolves its `device` subcommand from $NRFUTIL_HOME (default
# $HOME/.nrfutil) -- under sudo HOME=/root has no plugins and the flash dies
# with "Subcommand nrfutil-device not found".  When make itself runs AS ROOT
# with SUDO_USER set, point NRFUTIL_HOME back at the invoking user's plugin
# dir.  The euid gate matters: TikuBench's root runs demote make to the user
# via `sudo -u <user>`, and that inner sudo RE-SETS SUDO_USER=root -- an
# unconditional prefix would then aim at /root/.nrfutil, unreadable by the
# user (EACCES).  Demoted make has the right HOME already; leave it alone.
NRFUTIL ?= $(shell command -v nrfutil 2>/dev/null || echo $(CURDIR)/temp/nrfutil)
NRFUTIL_ENV = $(if $(and $(SUDO_USER),$(filter 0,$(shell id -u))),NRFUTIL_HOME=$(shell getent passwd $(SUDO_USER) | cut -d: -f6)/.nrfutil,)
NRF_SN  ?=
NRF_SN_ARG = $(if $(strip $(NRF_SN)),--serial-number $(strip $(NRF_SN)),)

ifeq ($(TIKU_PLATFORM),rp2350)

# Pi Pico 2 W has two reasonable flash paths:
#   1. picotool load -fx <uf2>     — works over BOOTSEL or Debug Probe
#   2. cp <uf2> /media/$USER/RPI-RP2  — drag-and-drop on Linux/macOS
# We try picotool first, then look for the mass-storage mountpoint.
PICO_MOUNT_GUESS = $(shell \
	for d in /run/media/$(USER)/RP2350 \
	         /run/media/$(USER)/RP2  \
	         /media/$(USER)/RP2350    \
	         /media/$(USER)/RP2  \
	         /Volumes/RP2350 \
	         /Volumes/RP2; do \
		[ -d $$d ] && echo $$d && break; \
	done)

flash: all
	@if command -v $(PICOTOOL) >/dev/null 2>&1; then \
		echo "Flashing via picotool..."; \
		$(PICOTOOL) load -fx $(TARGET_UF2); \
	elif [ -n "$(PICO_MOUNT_GUESS)" ]; then \
		echo "Copying $(TARGET_UF2) to $(PICO_MOUNT_GUESS)/"; \
		cp $(TARGET_UF2) $(PICO_MOUNT_GUESS)/; \
	else \
		echo "Error: no picotool and no RP2 mass-storage mountpoint."; \
		echo "  - Install picotool, OR"; \
		echo "  - Hold BOOTSEL while plugging in the Pico 2 W, then re-run."; \
		exit 1; \
	fi

run: flash

debug:
	@echo "Debug: connect a Debug Probe and run e.g.:"
	@echo "  openocd -s tcl -f interface/cmsis-dap.cfg -c 'adapter speed 5000' \\"
	@echo "    -f target/rp2350.cfg -c 'program $(TARGET) verify reset exit'"

erase:
	@echo "Erase: hold BOOTSEL, mount RPI-RP2, copy a flash_nuke.uf2 image."

else ifeq ($(TIKU_PLATFORM),ambiq)

# Apollo510 EVB via SEGGER J-Link — same recipe as the AmbiqSuite example:
# generate a Commander script that loads the raw binary to MRAM, resets, and
# runs. Override JLINK_DEVICE / JLINK_SPEED / JLINK_IF on the make line if
# your probe or part differs.
JLINK_FLASH_SCRIPT = $(BUILD_DIR)/flash.jlink
JLINK_ERASE_SCRIPT = $(BUILD_DIR)/erase.jlink

flash: all
	@mkdir -p $(BUILD_DIR)
	@printf 'device %s\nif %s\nspeed %s\nconnect\nloadbin %s %s\n$(JLINK_RUN_SEQ)\n' "$(JLINK_DEVICE)" "$(JLINK_IF)" "$(JLINK_SPEED)" "$(TARGET_BIN)" "$(AMBIQ_LOAD_ADDR)" > $(JLINK_FLASH_SCRIPT)
	@echo "Flashing $(TARGET_BIN) -> MRAM $(AMBIQ_LOAD_ADDR) via $(JLINK) ($(JLINK_DEVICE))..."
	$(JLINK) $(JLINK_SN_ARG) -CommanderScript $(JLINK_FLASH_SCRIPT)

run: flash

debug: all
	@echo "Apollo510 debug — start the GDB server in one terminal:"
	@echo "  $(JLINK_GDB) -device $(JLINK_DEVICE) -if $(JLINK_IF) -speed $(JLINK_SPEED)"
	@echo "then connect from another:"
	@echo "  $(GDB) main.elf -ex 'target remote :2331' -ex load -ex 'monitor reset' -ex continue"

erase:
	@mkdir -p $(BUILD_DIR)
	@printf 'device %s\nif %s\nspeed %s\nconnect\nerase\nr\nq\n' "$(JLINK_DEVICE)" "$(JLINK_IF)" "$(JLINK_SPEED)" > $(JLINK_ERASE_SCRIPT)
	@echo "Erasing MRAM via $(JLINK) ($(JLINK_DEVICE))..."
	$(JLINK) $(JLINK_SN_ARG) -CommanderScript $(JLINK_ERASE_SCRIPT)

else ifeq ($(TIKU_PLATFORM),nordic)

# nRF54L15-DK flash: two backends, both driving the on-board SEGGER J-Link.
#   NRF_FLASH=jlink    JLinkExe `loadfile main.hex` into RRAM -- universal
#                      (any J-Link; needs J-Link SW >= 8.10f), no extra tooling,
#                      the same recipe the Ambiq EVBs use.
#   NRF_FLASH=nrfutil  Nordic's nrfutil -- adds the Nordic-only extras
#                      (APPROTECT --recover, UICR/FICR, DFU/MCUboot packaging).
#   NRF_FLASH=auto     [default] nrfutil when it is installed, else J-Link, so
#                      `make flash MCU=nrf54l15` just works on any host.
# Probe serial: JLINK_SN or NRF_SN (either) picks one DK on a multi-probe rig.
# J-Link device string is only consulted on the NRF_FLASH=jlink path; the
# default auto/nrfutil path targets --core Application and needs no device name.
ifeq ($(MCU),nrf54lm20a)
JLINK_DEVICE_NORDIC ?= nRF54LM20A_M33
else ifeq ($(MCU),nrf54lm20b)
JLINK_DEVICE_NORDIC ?= nRF54LM20B_M33
else
JLINK_DEVICE_NORDIC ?= nRF54L15_M33
endif
JLINK_FLASH_SCRIPT   = $(BUILD_DIR)/flash.jlink
JLINK_ERASE_SCRIPT   = $(BUILD_DIR)/erase.jlink
_NRF_SN          := $(strip $(if $(strip $(JLINK_SN)),$(JLINK_SN),$(NRF_SN)))
NRF_JLINK_SN_ARG := $(if $(_NRF_SN),-SelectEmuBySN $(_NRF_SN),)
NRF_FLASH ?= auto
ifeq ($(NRF_FLASH),auto)
NRF_HAVE_NRFUTIL   := $(shell { command -v nrfutil >/dev/null 2>&1 || [ -x "$(CURDIR)/temp/nrfutil" ]; } && echo 1)
NRF_FLASH_RESOLVED := $(if $(NRF_HAVE_NRFUTIL),nrfutil,jlink)
else
NRF_FLASH_RESOLVED := $(NRF_FLASH)
endif

flash: all
	@echo "nRF54L15 flash backend: $(NRF_FLASH_RESOLVED)  (override with NRF_FLASH=jlink|nrfutil)"
ifeq ($(NRF_FLASH_RESOLVED),jlink)
	@mkdir -p $(BUILD_DIR)
	@printf 'device %s\nif %s\nspeed %s\nconnect\nloadfile %s\nr\ng\nqc\n' "$(JLINK_DEVICE_NORDIC)" "$(JLINK_IF)" "$(JLINK_SPEED)" "$(TARGET_HEX)" > $(JLINK_FLASH_SCRIPT)
	@echo "Flashing $(TARGET_HEX) -> RRAM via $(JLINK) ($(JLINK_DEVICE_NORDIC))..."
	$(JLINK) $(NRF_JLINK_SN_ARG) -CommanderScript $(JLINK_FLASH_SCRIPT)
else
	@echo "Flashing $(TARGET_HEX) via nrfutil ($(NRFUTIL))..."
	$(NRFUTIL_ENV) $(NRFUTIL) device program --firmware $(TARGET_HEX) --core Application \
		--options chip_erase_mode=ERASE_ALL,reset=RESET_SYSTEM $(NRF_SN_ARG)
endif

run: flash

debug: all
	@echo "nRF54L15 debug -- pick a backend:"
	@echo "  [J-Link]  $(JLINK_GDB) -device $(JLINK_DEVICE_NORDIC) -if $(JLINK_IF) -speed $(JLINK_SPEED)"
	@echo "            $(GDB) main.elf -ex 'target remote :2331' -ex load -ex 'monitor reset' -ex continue"
	@echo "  [nrfutil] $(NRFUTIL) device cpu-register-read --register PC $(NRF_SN_ARG)"

erase:
ifeq ($(NRF_FLASH_RESOLVED),jlink)
	@mkdir -p $(BUILD_DIR)
	@printf 'device %s\nif %s\nspeed %s\nconnect\nerase\nr\nqc\n' "$(JLINK_DEVICE_NORDIC)" "$(JLINK_IF)" "$(JLINK_SPEED)" > $(JLINK_ERASE_SCRIPT)
	@echo "Erasing RRAM via $(JLINK) ($(JLINK_DEVICE_NORDIC))..."
	$(JLINK) $(NRF_JLINK_SN_ARG) -CommanderScript $(JLINK_ERASE_SCRIPT)
else
	$(NRFUTIL_ENV) $(NRFUTIL) device erase --core Application $(NRF_SN_ARG)
endif

else

flash: all
	$(MSPDEBUG) $(DEBUGGER) "prog $(TARGET)"

run: flash

debug: all
	$(MSPDEBUG) $(DEBUGGER) "gdb"

erase:
	$(MSPDEBUG) $(DEBUGGER) "erase"

endif

deploy: clean flash monitor

# ---------------------------------------------------------------------------
# Serial Monitor  (auto-detects TI LaunchPad, picks picocom or screen)
# ---------------------------------------------------------------------------
# RP2350 + Apollo510 default to 115200; MSP430 to 9600.
ifeq ($(TIKU_PLATFORM),msp430)
BAUD ?= $(if $(UART_BAUD),$(UART_BAUD),9600)
else
BAUD ?= $(if $(UART_BAUD),$(UART_BAUD),115200)
endif

# Auto-detect serial port.
# Prefer /dev/ttyUSB* (FTDI/CP2102 external adapter) over ttyACM*
# (eZ-FET backchannel) since external adapters are used for SLIP
# networking and avoid the eZ-FET DTR-reset bug.
# NOTE: do NOT use "ls GLOB | head -1" here -- a pipeline's exit status is
# head's, which is 0 even when the glob matched nothing, so a "|| fallback"
# chain short-circuits on the first (empty) clause and never reaches the
# Linux device names.  Loop with [ -e ] instead: a non-matching glob stays
# literal and fails the test, so it is skipped cleanly (works on Linux +
# macOS).  Priority: external USB-serial -> TI/2047 ACM backchannel ->
# any ACM (incl. the SEGGER J-Link VCOM, VID 1366) / macOS usbmodem.
PORT ?= $(shell \
	for p in /dev/ttyUSB* /dev/tty.usbserial*; do \
		[ -e "$$p" ] && { echo "$$p"; exit 0; }; \
	done; \
	for dev in /dev/ttyACM*; do \
		[ -e "$$dev" ] || continue; \
		vid=$$(cat "/sys/class/tty/$$(basename $$dev)/device/../idVendor" 2>/dev/null); \
		if [ "$$vid" = "0451" ] || [ "$$vid" = "2047" ]; then echo "$$dev"; exit 0; fi; \
	done; \
	for p in /dev/ttyACM* /dev/tty.usbmodem*; do \
		[ -e "$$p" ] && { echo "$$p"; exit 0; }; \
	done)

monitor:
	@if [ -z "$(PORT)" ]; then \
		echo "Error: No serial port found (/dev/ttyUSB* or /dev/ttyACM*)"; \
		echo "  Plug in the USB-serial adapter / LaunchPad, or the board's"; \
		echo "  J-Link VCOM (shows up as /dev/ttyACM*)."; \
		echo "  Or point it at a specific port: make monitor PORT=/dev/ttyACM0"; \
		exit 1; \
	fi; \
	if command -v picocom >/dev/null 2>&1; then \
		echo "Connecting to $(PORT) at $(BAUD) baud  (Ctrl-A Ctrl-X to exit)"; \
		picocom -b $(BAUD) $(PORT); \
	elif command -v screen >/dev/null 2>&1; then \
		echo "Connecting to $(PORT) at $(BAUD) baud  (Ctrl-A k to exit)"; \
		screen $(PORT) $(BAUD); \
	else \
		echo "Error: Neither picocom nor screen found"; \
		echo "  sudo apt install picocom"; \
		exit 1; \
	fi
