# ===========================================================================
# TikuOS Makefile
#
# Usage:
#   make MCU=msp430fr5969                      — build for FR5969
#   make flash MCU=msp430fr5969                — compile + flash
#   make flash MCU=msp430fr5969 DEBUGGER=tilib — explicit debugger
#   make debug MCU=msp430fr5969                — compile + start GDB server
#   make run MCU=msp430fr5969                  — alias for flash
#   make erase MCU=msp430fr5969                — erase chip
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
# Target MCU  (override on command line: make MCU=msp430fr5969 / MCU=rp2350)
# Accepts uppercase (MSP430FR5969 / RP2350) or lowercase MCU names.
# ---------------------------------------------------------------------------
MCU ?= $(mcu)
ifeq ($(MCU),)
MCU = msp430fr2433
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
ifeq ($(MCU),apollo4l)
TIKU_BOARD_DEFINE := TIKU_BOARD_APOLLO4L_EVB
else
TIKU_BOARD_DEFINE := TIKU_BOARD_APOLLO510_EVB
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
# ---------------------------------------------------------------------------
ifneq (,$(filter $(TIKU_PLATFORM),rp2350 ambiq))

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
# J-Link device + MRAM load address differ per Ambiq part.
ifeq ($(MCU),apollo4l)
JLINK_DEVICE    ?= AMAP42KL-KBR
AMBIQ_LOAD_ADDR ?= 0x00018000
# The Apollo4 Lite secure SBL parks (PC stays inside the SBL) while a debugger
# is attached at reset. Detaching with the target left running (qc) drops the
# debugger so the SBL hands off to the app at 0x18000; the Sleep lets the SBL
# reach that debug-wait before we detach. (q halts, so the app never starts.)
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
#   make APP=cli MCU=msp430fr5969   — build with CLI app
#   make MCU=msp430fr5969           — default (tests/examples as before)
# ---------------------------------------------------------------------------
APP ?=

# ---------------------------------------------------------------------------
# Shell service (kernel service — orthogonal to APP/tests/examples)
#   make TIKU_SHELL_ENABLE=1 MCU=msp430fr5969   — build with shell
#   make APP=cli MCU=msp430fr5969                — legacy alias (also enables shell)
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
MEMORY_MODEL ?= small

# ---------------------------------------------------------------------------
# Build-time consistency guards (MSP430 only — RP2350 has no HIFRAM
# concept and the BASIC interpreter is platform-neutral C).
# ---------------------------------------------------------------------------
ifeq ($(TIKU_PLATFORM),msp430)
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

else ifeq ($(TIKU_PLATFORM),ambiq)

# CPU/FPU per Ambiq part: Cortex-M55 + Helium (Apollo510) or Cortex-M4F with a
# single-precision FPU (Apollo4 Lite). Derived from -mcpu; -Wno-psabi below.
ifeq ($(MCU),apollo4l)
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
# Memory tiers sized for Apollo510's 512 KB DTCM. The tiku_mem.h defaults are
# MSP430-era (128 B SRAM / 1 KB NVM) and assume large allocations spill to HIFRAM
# (FRAM > 64 KB), which this part lacks -- so AUTO allocations land in the 128 B
# SRAM tier and OOM on a half-MB-of-RAM MCU (e.g. BASIC's ~4 KB arena). The mem
# size type is 32-bit here (arch/ambiq/tiku_mem_arch.h), so >64 KB tiers are
# fine. NVM tier is volatile-RAM-backed until MRAM persistence lands.
CFLAGS += -DTIKU_TIER_SRAM_SIZE=131072   # 128 KB fast volatile tier in DTCM
CFLAGS += -DTIKU_TIER_NVM_SIZE=16384      # 16 KB NVM tier
# Part selectors that configure the vendored register map (apollo4l.h / apollo510.h).
ifeq ($(MCU),apollo4l)
CFLAGS += -DPART_apollo4l -DAM_PART_APOLLO4L -Dgcc
else
CFLAGS += -DPART_apollo510 -DAM_PART_APOLLO510 -DAM_PACKAGE_BGA -Dgcc
endif
CFLAGS += -I$(PROJ_DIR)
# CMSIS register headers, VENDORED in-tree (arch/ambiq/cmsis/) so the build is
# fully self-contained: it references nothing in temp/AmbiqSuite, only the MRAM
# bootrom blob. apollo510.h (the complete Apollo510 register map -- all 30
# peripherals) pulls core_cm55.h + system_apollo510.h from that same dir.
# Provenance + licenses: arch/ambiq/cmsis/PROVENANCE.md.
CFLAGS += -I$(PROJ_DIR)/arch/ambiq/cmsis
CFLAGS += -ffunction-sections -fdata-sections -fno-common

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

ifeq ($(MCU),apollo4l)
LDFLAGS  = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
else
LDFLAGS  = -mcpu=cortex-m55 -mthumb -mfpu=auto -mfloat-abi=hard
endif
# nano.specs -> libc_nano (small printf, no heavy stdio init); nosys.specs ->
# libnosys syscall stubs (_sbrk/_write/...), formerly supplied by AmbiqSuite.
LDFLAGS += --specs=nano.specs --specs=nosys.specs
LDFLAGS += -nostartfiles -static
ifeq ($(MCU),apollo4l)
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
ifeq ($(filter $(TIKU_PLATFORM),rp2350 ambiq),)
$(error MINIMAL=1 is only supported on MCU=rp2350 or MCU=apollo510)
endif

# Use the minimal entry point and exactly the arch files it needs.
SRCS  = main_minimal.c
ifeq ($(TIKU_PLATFORM),ambiq)
ifeq ($(MCU),apollo4l)
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
SRCS += arch/arm-rp2350/tiku_gpio_arch.c
SRCS += arch/arm-rp2350/tiku_spi_arch.c
SRCS += arch/arm-rp2350/tiku_lcd_arch.c
SRCS += arch/arm-rp2350/tiku_pio_arch.c
SRCS += arch/arm-rp2350/tiku_pwm_arch.c
SRCS += arch/arm-rp2350/tiku_dma_arch.c
SRCS += arch/arm-rp2350/tiku_trng_arch.c

# Console backend: add the native USB CDC stack for TIKU_CONSOLE=usb|both.
ifeq ($(TIKU_CONSOLE),usb)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1
else ifeq ($(TIKU_CONSOLE),both)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1 -DTIKU_CONSOLE_BOTH=1
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
SRCS += arch/ambiq/tiku_lcd_arch.c
ifeq ($(MCU),apollo4l)
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
SRCS += arch/ambiq/tiku_gpio_apollo4l.c
SRCS += arch/ambiq/tiku_adc_apollo4l.c
else
# Apollo510 (Cortex-M55) device/CPU backends.
SRCS += arch/ambiq/tiku_adc_arch.c
SRCS += arch/ambiq/tiku_timer_arch.c
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

endif
SRCS += boot/tiku_boot.c
SRCS += hal/tiku_cpu.c
SRCS += kernel/cpu/tiku_common.c
SRCS += kernel/cpu/tiku_watchdog.c
SRCS += kernel/cpu/tiku_rtc.c

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
SRCS += kernel/shell/commands/tiku_shell_cmd_ps.c
SRCS += kernel/shell/commands/tiku_shell_cmd_info.c
SRCS += kernel/shell/commands/tiku_shell_cmd_timer.c
SRCS += kernel/shell/commands/tiku_shell_cmd_kill.c
SRCS += kernel/shell/commands/tiku_shell_cmd_resume.c
SRCS += kernel/shell/commands/tiku_shell_cmd_queue.c
SRCS += kernel/shell/commands/tiku_shell_cmd_reboot.c
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
CFLAGS += -DTIKU_APP_NET=1
SRCS += apps/net/tiku_app_net.c
SRCS += labs/coap/tiku_kits_net_coap.c
SRCS += labs/coap/tiku_kits_net_coap_process.c
endif

# Shell net-test mode: activate TCP and pull in the CoAP server so the shell
# firmware can answer the TikuBench net suite (gated; see TIKU_SHELL_NET_TEST).
ifeq ($(TIKU_SHELL_NET_TEST),1)
CFLAGS += -DTIKU_SHELL_NET_TEST=1 -DTIKU_KITS_NET_TCP_ENABLE=1
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
CFLAGS += -DTIKU_SHELL_TCP_ENABLE=1
SRCS += labs/coap/tiku_kits_net_coap.c
SRCS += labs/coap/tiku_kits_net_coap_process.c
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
CFLAGS += -DTIKU_TURBO_BENCH=1
TIKU_KIT_CRYPTO_ENABLE := 1
TIKU_KIT_MATHS_ENABLE  := 1
TIKU_KIT_ML_ENABLE     := 1
SRCS   += apps/turbo_bench/turbo_bench.c
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
else
SRCS   += $(wildcard tikukits/net/ipv4/*.c)
SRCS   += $(wildcard tikukits/net/http/*.c)
SRCS   += $(wildcard tikukits/net/mqtt/*.c)
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

ifeq ($(TIKU_KIT_CRYPTO_ENABLE),1)
CFLAGS += -DTIKU_KIT_CRYPTO_ENABLE=1
SRCS   += $(wildcard tikukits/crypto/sha256/*.c)
SRCS   += $(wildcard tikukits/crypto/base64/*.c)
SRCS   += $(wildcard tikukits/crypto/crc/*.c)
SRCS   += $(wildcard tikukits/crypto/gcm/*.c)
SRCS   += $(wildcard tikukits/crypto/aes128/*.c)
SRCS   += $(wildcard tikukits/crypto/hkdf/*.c)
SRCS   += $(wildcard tikukits/crypto/hmac/*.c)
# TLS pulls in additional code; gated separately on HAS_TLS=1
# because tiku_kits_crypto_tls requires the platform to provide
# TIKU_KITS_CRYPTO_TLS_RNG_FILL.
ifeq ($(HAS_TLS),1)
SRCS   += $(wildcard tikukits/crypto/tls/*.c)
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

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
TARGET = main.elf

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.SUFFIXES:
.PHONY: all clean flash run debug erase size monitor deploy docs docs-clean uf2

# UF2 is the RP2350 deliverable; ELF is enough on MSP430.
ifeq ($(TIKU_PLATFORM),rp2350)
TARGET_BIN = main.bin
TARGET_UF2 = main.uf2
all: $(TARGET) $(TARGET_BIN) $(TARGET_UF2) size
else ifeq ($(TIKU_PLATFORM),ambiq)
TARGET_BIN = main.bin
all: $(TARGET) $(TARGET_BIN) size
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
	$(CC) $(CFLAGS) -c -o $@ $<

# Assembly-source rule. Used by firmware-blob wrappers that pull in
# binary data via .incbin (see drivers/wifi/cyw43/firmware.S).
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

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
	$(CC) $(CFLAGS) -c -o $@ $<
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
	$(JLINK) -CommanderScript $(JLINK_FLASH_SCRIPT)

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
	$(JLINK) -CommanderScript $(JLINK_ERASE_SCRIPT)

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
