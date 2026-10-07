# ===========================================================================
# TikuOS Makefile: builds the image for one MCU (MCU=) and board (BOARD=).
#
# Usage:
#   make MCU=msp430fr5994                      — build for FR5994
#   make flash MCU=msp430fr5994                — compile + flash
#   make flash MCU=msp430fr5994 DEBUGGER=tilib — explicit debugger
#   make debug MCU=msp430fr5994                — compile + start GDB server
#   make run MCU=msp430fr5994                  — alias for flash
#   make erase MCU=msp430fr5994                — erase chip
#   make monitor                               — serial console, port detected
#   make monitor PORT=/dev/ttyACM1 BAUD=9600   — explicit port/baud
#   make clean                                 — clean build artifacts
# ===========================================================================

# Makes `all` the default goal.  Rules such as the msp430 debug-stripped
# libnosys come before the `all:` rule; without this line the first of them is
# the default goal, and a bare `make` builds only that file.
.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Target MCU, set on the command line: MCU=msp430fr5994, MCU=rp2350, ...
# (mcu= also works).  Upper- and lowercase names are accepted.
# ---------------------------------------------------------------------------
MCU ?= $(mcu)
ifeq ($(MCU),)
MCU = msp430fr5994          # the target of a bare `make`
endif
MCU := $(shell echo $(MCU) | tr '[:upper:]' '[:lower:]')

# TIKU_PLATFORM: the port an MCU belongs to, which selects the toolchain,
# flags and sources below.  An MCU not named here is taken as msp430.
ifeq ($(MCU),rp2350)
TIKU_PLATFORM := rp2350
else ifeq ($(MCU),apollo510)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo4l)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo4p)
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),apollo510b)
# The Apollo510 Blue EVB part: the Apollo510 (Cortex-M55) die plus an EM9305
# BLE radio.  It takes apollo510's register map, linker script, J-Link device
# and arch backends through the apollo510 `else` branches below.
TIKU_PLATFORM := ambiq
else ifeq ($(MCU),nrf54l15)
TIKU_PLATFORM := nordic
else ifeq ($(MCU),nrf54lm20a)
TIKU_PLATFORM := nordic
else ifeq ($(MCU),nrf54lm20b)
TIKU_PLATFORM := nordic
else ifeq ($(MCU),stm32n6)
# STM32N657X0 (NUCLEO-N657X0-Q): Cortex-M55 with no internal flash.  The boot
# ROM loads one signed image into SRAM; `make flash` sends it over the ROM's
# DFU interface.
TIKU_PLATFORM := stm32n6
else ifeq ($(MCU),ra8p1)
# R7KA8P1KF (EK-RA8P1): Cortex-M85 at 1 GHz beside a Cortex-M33, with 1 MB of
# code MRAM and an Ethos-U55 NPU.  The image links into the code MRAM.
TIKU_PLATFORM := ra8p1
else ifeq ($(MCU),esp32c61)
# ESP32-C61 (ESP32-C61-DevKitC): one RISC-V core.  The ROM loads the image
# from flash into SRAM and runs it there; code runs from flash only where a
# build places it in the XIP window.
TIKU_PLATFORM := esp32c61
else
TIKU_PLATFORM := msp430
endif

# ---------------------------------------------------------------------------
# Console channel: uart (the default: the board's console UART) | usb (native
# USB CDC-ACM) | both (mirrored to UART and USB).  usb and both need rp2350,
# nrf54lm20a or nrf54lm20b; they compile the port's USB CDC stack and define
# TIKU_CONSOLE_USB, and both also defines TIKU_CONSOLE_BOTH.  The printf HAL
# (hal/tiku_printf_hal.h), the shell I/O backend, the console and boot read
# those macros.
# ---------------------------------------------------------------------------
TIKU_CONSOLE ?= uart
ifeq ($(filter uart usb both,$(TIKU_CONSOLE)),)
$(error TIKU_CONSOLE must be uart, usb, or both (got '$(TIKU_CONSOLE)'))
endif
ifneq ($(filter usb both,$(TIKU_CONSOLE)),)
ifeq ($(filter rp2350 nrf54lm20a nrf54lm20b,$(MCU)),)
$(error TIKU_CONSOLE=$(TIKU_CONSOLE) (USB CDC console) is supported on rp2350 \
and nrf54lm20a/b; this build is $(MCU). Use TIKU_CONSOLE=uart)
endif
endif

# ---------------------------------------------------------------------------
# Board selection (all platforms)
#
# BOARD names the PCB; MCU names the silicon on it:
#
#   MCU=apollo510b        the AP510NFB part (Apollo510 die + EM9305 BLE die
#                         in one package).
#   BOARD=apollo510b_evb  Ambiq's evaluation board carrying that part, with
#                         its eMMC (U11), PSRAM (U14), USB supply switches and
#                         J-Link console routing.
#
# Every MCU has a default board (DEFAULT_BOARD_<mcu>).  BOARD= builds the same
# silicon for another PCB of the same platform, such as a custom board.
# ---------------------------------------------------------------------------

# MCU -> its default board.
DEFAULT_BOARD_msp430fr2433  := fr2433_launchpad
DEFAULT_BOARD_msp430fr5969  := fr5969_launchpad
DEFAULT_BOARD_msp430fr5994  := fr5994_launchpad
DEFAULT_BOARD_msp430fr6989  := fr6989_launchpad
DEFAULT_BOARD_rp2350        := pico2w
DEFAULT_BOARD_apollo4l      := apollo4l_evb
# The Apollo4 Plus EVB is its own board, with its own header and its console
# on UART0, which the ambiq CFLAGS block selects by BOARD.
DEFAULT_BOARD_apollo4p      := apollo4p_evb
DEFAULT_BOARD_apollo510     := apollo510_evb
DEFAULT_BOARD_apollo510b    := apollo510b_evb
DEFAULT_BOARD_nrf54l15      := nrf54l15_dk
# Both LM20 variants default to the nRF54LM20-DK, which carries the LM20B; an
# nrf54lm20a image runs on it as well.
DEFAULT_BOARD_nrf54lm20a    := nrf54lm20_dk
DEFAULT_BOARD_nrf54lm20b    := nrf54lm20_dk
DEFAULT_BOARD_stm32n6       := nucleo_n657x0q
DEFAULT_BOARD_ra8p1         := ek_ra8p1
DEFAULT_BOARD_esp32c61      := esp32c61_devkitc

# BOARD -> the macro that selects its header, and the platform it belongs to.
# A new board goes in KNOWN_BOARDS, gets a row in each table here and in
# BOARD_CAPS_ below, and gets a board header.
KNOWN_BOARDS := fr2433_launchpad fr5969_launchpad fr5994_launchpad \
                fr6989_launchpad pico2 pico2w apollo4l_evb apollo4p_evb \
                apollo510_evb apollo510b_evb nrf54l15_dk nrf54lm20_dk \
                nucleo_n657x0q ek_ra8p1 esp32c61_devkitc tiku_bare

BOARD_DEFINE_fr2433_launchpad  := TIKU_BOARD_FR2433_LAUNCHPAD
BOARD_DEFINE_fr5969_launchpad  := TIKU_BOARD_FR5969_LAUNCHPAD
BOARD_DEFINE_fr5994_launchpad  := TIKU_BOARD_FR5994_LAUNCHPAD
BOARD_DEFINE_fr6989_launchpad  := TIKU_BOARD_FR6989_LAUNCHPAD
BOARD_DEFINE_pico2             := TIKU_BOARD_RPI_PICO2
BOARD_DEFINE_pico2w            := TIKU_BOARD_RPI_PICO2_W
BOARD_DEFINE_apollo4l_evb      := TIKU_BOARD_APOLLO4L_EVB
BOARD_DEFINE_apollo4p_evb      := TIKU_BOARD_APOLLO4P_EVB
BOARD_DEFINE_apollo510_evb     := TIKU_BOARD_APOLLO510_EVB
BOARD_DEFINE_apollo510b_evb    := TIKU_BOARD_APOLLO510B_EVB
# tiku_bare: a custom TikuOS board with Apollo510 silicon and none of the
# EVB's parts.  No MCU defaults to it; select it with BOARD=tiku_bare.
BOARD_DEFINE_tiku_bare         := TIKU_BOARD_TIKU_BARE
BOARD_DEFINE_nrf54l15_dk       := TIKU_BOARD_NRF54L15_DK
BOARD_DEFINE_nrf54lm20_dk      := TIKU_BOARD_NRF54LM20_DK
BOARD_DEFINE_nucleo_n657x0q    := TIKU_BOARD_NUCLEO_N657X0Q
BOARD_DEFINE_ek_ra8p1          := TIKU_BOARD_EK_RA8P1
BOARD_DEFINE_esp32c61_devkitc  := TIKU_BOARD_ESP32C61_DEVKITC

BOARD_PLATFORM_fr2433_launchpad  := msp430
BOARD_PLATFORM_fr5969_launchpad  := msp430
BOARD_PLATFORM_fr5994_launchpad  := msp430
BOARD_PLATFORM_fr6989_launchpad  := msp430
BOARD_PLATFORM_pico2             := rp2350
BOARD_PLATFORM_pico2w            := rp2350
BOARD_PLATFORM_apollo4l_evb      := ambiq
BOARD_PLATFORM_apollo4p_evb      := ambiq
BOARD_PLATFORM_apollo510_evb     := ambiq
BOARD_PLATFORM_apollo510b_evb    := ambiq
BOARD_PLATFORM_tiku_bare         := ambiq
BOARD_PLATFORM_nrf54l15_dk       := nordic
BOARD_PLATFORM_nrf54lm20_dk      := nordic
BOARD_PLATFORM_nucleo_n657x0q    := stm32n6
BOARD_PLATFORM_ek_ra8p1          := ra8p1
BOARD_PLATFORM_esp32c61_devkitc  := esp32c61

# ---------------------------------------------------------------------------
# Board capabilities: the parts fitted on the PCB
#
# A capability says a part is fitted and routed on this board, which the MCU
# does not say: the same die can sit on a board without the part.  Each cap
# becomes -DTIKU_BOARD_HAS_<CAP>=1 for the compiler, and the driver gates
# below accept a TIKU_DRV_*_ENABLE=1 only on a board that declares the part.
# A command-line -D reaches every translation unit whatever it includes; a
# macro defined in a board header reaches only the files that include that
# header first.
# ---------------------------------------------------------------------------

# Both Apollo510 EVBs carry the U11 eMMC (8 GB), the U14 PSRAM (64 MB) and
# switched USB rails; the U12 octal NOR is fitted on the green EVB only.  The
# Blue board's package adds the EM9305 BLE radio die (EM9305), whose 12 MHz
# EXTREFCLK is also its USB high-speed PHY reference (USBHS_CLK_EM9305); the
# green board takes that reference from its own crystal (USBHS_CLK_XTAL).
BOARD_CAPS_apollo510_evb       := EMMC PSRAM NOR USB_RAILS USBHS_CLK_XTAL
BOARD_CAPS_apollo510b_evb      := EMMC PSRAM USB_RAILS USBHS_CLK_EM9305 EM9305
BOARD_CAPS_pico2w              := CYW43

# No driver on these boards is gated on a capability.  Every board declares
# its row, empty or not (checked below).
BOARD_CAPS_pico2               :=
BOARD_CAPS_apollo4l_evb        :=
BOARD_CAPS_apollo4p_evb        :=
BOARD_CAPS_fr2433_launchpad    :=
BOARD_CAPS_fr5969_launchpad    :=
BOARD_CAPS_fr5994_launchpad    :=
# The FR6989 LaunchPad has a segment LCD wired to LCD_C.  Its LCD cap defines
# TIKU_BOARD_HAS_LCD=1, which the LCD driver and the shell's lcd command test.
BOARD_CAPS_fr6989_launchpad    := LCD
BOARD_CAPS_nrf54l15_dk         :=
BOARD_CAPS_nrf54lm20_dk        :=
BOARD_CAPS_nucleo_n657x0q      :=
# EK-RA8P1: USBHS is the USB high-speed connector (J7), which
# TIKU_DRV_USBHS_ENABLE requires.  The board's other parts (Ethernet PHY, OSPI
# NOR, microSD slot, camera and display connectors) are not declared as caps,
# and no driver gate checks the board for them.
BOARD_CAPS_ek_ra8p1            := USBHS
# ESP32-C61-DevKitC: the RGB LED and the console bridge are described in the
# board header; no driver on the board is gated on a cap.
BOARD_CAPS_esp32c61_devkitc    :=
# tiku_bare declares no parts, so the eMMC, PSRAM, NOR and USB gates below
# refuse it.
BOARD_CAPS_tiku_bare           :=

# $(call board_has,EMMC) -> non-empty when this board declares that cap.
board_has = $(filter $(1),$(BOARD_CAPS_$(BOARD)))

# Every board in KNOWN_BOARDS must declare a BOARD_CAPS_ row, even an empty
# one; the build stops here when one is missing.  A missing row would read as
# empty, and the driver gates would refuse parts the board has.  $(origin)
# tells a row declared empty from one never declared, which $(if) cannot.
$(foreach b,$(KNOWN_BOARDS),$(if $(filter undefined,$(origin BOARD_CAPS_$(b))),\
    $(error BOARD_CAPS_$(b) is not declared. Every board in KNOWN_BOARDS must \
declare its capabilities, even if the list is empty (BOARD_CAPS_$(b) := ).)))

# Every board in KNOWN_BOARDS also needs a BOARD_DEFINE_ and a
# BOARD_PLATFORM_ row; the build stops here, at parse time, when one is
# missing.  Without a BOARD_DEFINE_ row, BOARD=x fails below as unknown while
# the list of known boards names x.
$(foreach b,$(KNOWN_BOARDS),$(if $(BOARD_DEFINE_$(b)),,\
    $(error KNOWN_BOARDS lists '$(b)' but BOARD_DEFINE_$(b) is unset)))
$(foreach b,$(KNOWN_BOARDS),$(if $(BOARD_PLATFORM_$(b)),,\
    $(error KNOWN_BOARDS lists '$(b)' but BOARD_PLATFORM_$(b) is unset)))

# BOARD= (or board=) from the command line, else the MCU's default board;
# lowercased like MCU.
BOARD ?= $(board)
ifeq ($(BOARD),)
BOARD := $(DEFAULT_BOARD_$(MCU))
endif
BOARD := $(shell echo $(BOARD) | tr '[:upper:]' '[:lower:]')

ifeq ($(BOARD),)
$(error No default board is known for MCU=$(MCU). Pass BOARD=<name>. \
Known boards: $(KNOWN_BOARDS))
endif
TIKU_BOARD_DEFINE := $(BOARD_DEFINE_$(BOARD))
ifeq ($(TIKU_BOARD_DEFINE),)
$(error Unknown BOARD=$(BOARD) (MCU=$(MCU)). Known boards: $(KNOWN_BOARDS))
endif
# A board belongs to one platform, and a BOARD of another platform than the
# MCU's stops the build here.  The port's device selector does not know such
# a board: some stop with #error, and the rp2350, ambiq and nordic selectors
# fall back to their default board.
ifneq ($(BOARD_PLATFORM_$(BOARD)),$(TIKU_PLATFORM))
$(error BOARD=$(BOARD) is a $(BOARD_PLATFORM_$(BOARD)) board, but MCU=$(MCU) \
is $(TIKU_PLATFORM). Pick a $(TIKU_PLATFORM) board, or change MCU.)
endif

# An immediate (:=) assignment, so it must stay below the lines that resolve
# BOARD.  Above them, $(BOARD_CAPS_$(BOARD)) expands to nothing: no
# -DTIKU_BOARD_HAS_* reaches the compiler, while the board_has gates, which
# expand where they are used, still pass.
BOARD_CAP_DEFINES := $(foreach c,$(BOARD_CAPS_$(BOARD)),-DTIKU_BOARD_HAS_$(c)=1)

# A part the board declares (EMMC, PSRAM, NOR) gets its driver by default;
# TIKU_DRV_<PART>_ENABLE=0 leaves it out.  Set before the overlays are
# included, so an overlay sees the result.  A MINIMAL=1 build enables none.
ifneq ($(MINIMAL),1)
ifneq ($(call board_has,EMMC),)
TIKU_DRV_EMMC_ENABLE ?= 1
endif
ifneq ($(call board_has,PSRAM),)
TIKU_DRV_PSRAM_ENABLE ?= 1
endif
ifneq ($(call board_has,NOR),)
TIKU_DRV_NOR_ENABLE ?= 1
endif
endif

# ---------------------------------------------------------------------------
# Private experiment overlay
#
# A private repository cloned into experiment/ adds features when present.
# It is included here, after the board capabilities are known and before the
# capability refusals below, so the drivers a feature requests are checked
# against the board like any other request.  Each feature is opt-in (e.g.
# LLM=1); with none opted in, or the directory absent, the image is the same
# as one built without the overlay.
#
# The platform blocks below assign SRCS and CFLAGS with `=`, which drops
# anything added before them.  The overlay therefore sets only driver
# requests and per-feature variables here; its EXP_SRCS and EXP_CFLAGS are
# appended by the Apollo510 source block, after those assignments.  An
# overlay adds sources, include paths and -D macros; it does not patch
# mainline sources.
# ---------------------------------------------------------------------------
-include experiment/experiment.mk
# Applications overlay: the same arrangement for programs that run on TikuOS,
# each a process and a VFS client.  It exports APPL_SRCS and APPL_CFLAGS,
# which are appended once every platform block has run, on every platform.
-include applications/applications.mk

# ---------------------------------------------------------------------------
# Capability refusals
#
# Each driver request is checked against the board here, outside every
# platform block.  A check inside a platform block runs for that platform
# only: the same request on another platform builds an image without the
# driver and without an error.
# ---------------------------------------------------------------------------

ifeq ($(TIKU_DRV_USB_ENABLE),1)
ifeq ($(call board_has,USB_RAILS),)
$(error TIKU_DRV_USB_ENABLE=1 requires a board that switches the USB rails \
(currently BOARD=$(BOARD), MCU=$(MCU)). The device controller is powered from \
VDDUSB33 / VDDUSB0P9, which the Apollo510 EVBs gate from board pads; a board \
that does not route them enumerates nothing. Build with BOARD=apollo510_evb \
or BOARD=apollo510b_evb, or drop TIKU_DRV_USB_ENABLE.)
endif
endif

# MINIMAL=1 builds main_minimal.c, which drives the USB-HS device on RA8P1,
# so on a board with the USBHS cap a MINIMAL=1 build enables the driver by
# default.  This must come before the gate below reads the flag.  In the
# kernel build the driver is off unless requested.
ifeq ($(MINIMAL),1)
ifneq ($(call board_has,USBHS),)
TIKU_DRV_USBHS_ENABLE ?= 1
endif
endif

ifeq ($(TIKU_DRV_USBHS_ENABLE),1)
ifeq ($(call board_has,USBHS),)
$(error TIKU_DRV_USBHS_ENABLE=1 requires a board that routes the USB-HS \
connector (currently BOARD=$(BOARD), MCU=$(MCU)). J7 on the EK-RA8P1 is the \
only one; the controller has dedicated DP/DM pins, so a board that does not \
route them enumerates nothing. Build with BOARD=ek_ra8p1, or drop \
TIKU_DRV_USBHS_ENABLE.)
endif
endif

# The Ethos-U55 NPU, the 2D drawing engine and the GLCDC are on-chip
# peripherals of the RA8P1 only.
ifeq ($(TIKU_NPU_ENABLE),1)
ifneq ($(TIKU_PLATFORM),ra8p1)
$(error TIKU_NPU_ENABLE=1 requires a part with the Ethos-U55 (currently \
MCU=$(MCU)). RA8P1 is the only one in this tree; elsewhere the flag would be \
silently ignored and the build would claim an accelerator it has not got.)
endif
endif

ifeq ($(TIKU_DRV_DRW_ENABLE),1)
ifneq ($(TIKU_PLATFORM),ra8p1)
$(error TIKU_DRV_DRW_ENABLE=1 requires a part with the 2D drawing engine \
(currently MCU=$(MCU)). RA8P1 is the only one in this tree; elsewhere the flag \
would be silently ignored and the build would claim an accelerator it lacks.)
endif
endif

ifeq ($(TIKU_DRV_GLCDC_ENABLE),1)
ifneq ($(TIKU_PLATFORM),ra8p1)
$(error TIKU_DRV_GLCDC_ENABLE=1 requires a part with the GLCDC (currently \
MCU=$(MCU)). RA8P1 is the only one in this tree; elsewhere the flag would be \
silently ignored and the build would claim a display controller it lacks.)
endif
endif

ifeq ($(TIKU_DRV_EMMC_ENABLE),1)
ifeq ($(call board_has,EMMC),)
$(error TIKU_DRV_EMMC_ENABLE=1 requires a board with an eMMC fitted \
(currently BOARD=$(BOARD), MCU=$(MCU)). U11 is the 8 GB eMMC on the Apollo510 \
EVBs; no other supported board carries one, and the SDIO0 bus answers nothing \
without it. Build with BOARD=apollo510_evb or BOARD=apollo510b_evb, or drop \
TIKU_DRV_EMMC_ENABLE.)
endif
endif

ifeq ($(TIKU_DRV_NOR_ENABLE),1)
ifeq ($(call board_has,NOR),)
$(error TIKU_DRV_NOR_ENABLE=1 requires a board with the octal NOR fitted \
(currently BOARD=$(BOARD), MCU=$(MCU)). U12 is populated on the Apollo510 EVB \
(green, BOARD=apollo510_evb) and is NOT populated on the Apollo510B (Blue) -- \
its BSP defines no MSPI1 chip select. Build with BOARD=apollo510_evb, or drop \
TIKU_DRV_NOR_ENABLE. See kintsugi/mspi-nor-plan.md.)
endif
endif

ifeq ($(TIKU_DRV_PSRAM_ENABLE),1)
ifeq ($(call board_has,PSRAM),)
$(error TIKU_DRV_PSRAM_ENABLE=1 requires a board with PSRAM fitted \
(currently BOARD=$(BOARD), MCU=$(MCU)). U14 is the 64 MB APS25608N on the \
Apollo510 EVBs, reached over MSPI0; without it the tier has no backing store. \
Build with BOARD=apollo510_evb or BOARD=apollo510b_evb, or drop \
TIKU_DRV_PSRAM_ENABLE.)
endif
endif

ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
ifeq ($(call board_has,EM9305),)
$(error TIKU_DRV_BLE_EM9305_ENABLE=1 requires a board with the EM9305 radio \
(currently BOARD=$(BOARD), MCU=$(MCU)). The die sits in the AP510NFB \
package of the Apollo510B (Blue) EVB, on IOM6 SPI. Build with \
BOARD=apollo510b_evb, or drop TIKU_DRV_BLE_EM9305_ENABLE.)
endif
endif

# ---------------------------------------------------------------------------
# Driver-vs-MCU compatibility gates
#
# Some drivers use one silicon family's peripherals directly (the CYW43439
# driver uses RP2350 PIO).  These checks stop the build at parse time when
# such a driver is requested for another MCU, which would otherwise fail with
# compiler errors.  A driver bound to one platform gets its check here.
# ---------------------------------------------------------------------------

# CYW43439 (Pico 2 W): the driver uses RP2350 PIO1 and register layouts and
# the module's pins (GP23-GP25, GP29).  It needs MCU=rp2350 and a board that
# carries the module (the CYW43 cap: pico2w); the plain Pico 2 has none.
ifeq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
# The CYW43439 firmware is linked into .rodata with .incbin (firmware.S), so
# it is charged against the code window; the warning below says so on every
# such build.  Do not raise the code window to make room for firmware.
$(warning TIKU_DRV_WIFI_CYW43_ENABLE=1: the CYW43439 firmware is compiled into \
.rodata and charged against the code window (~233 KB of it). This links at 384 \
KB but is NOT the shipping shape -- P3a moves the firmware to /data. Do NOT \
raise the window to make room for a blob.)
ifneq ($(TIKU_PLATFORM),rp2350)
$(error TIKU_DRV_WIFI_CYW43_ENABLE=1 requires MCU=rp2350 \
(currently MCU=$(MCU)). The CYW43439 driver depends on \
RP2350-specific PIO + SIO peripherals and only runs on Pi Pico \
2 W hardware. For other MCUs, omit TIKU_DRV_WIFI_CYW43_ENABLE.)
endif
# The module is a board part: only a board with the CYW43 cap carries it.
ifeq ($(call board_has,CYW43),)
$(error TIKU_DRV_WIFI_CYW43_ENABLE=1 requires BOARD=pico2w \
(currently BOARD=$(BOARD)). The CYW43439 module is only present on \
the Pi Pico 2 W; the plain Pi Pico 2 has no WiFi hardware. Either \
build with BOARD=pico2w, or drop TIKU_DRV_WIFI_CYW43_ENABLE.)
endif
endif

# CYW43439 Bluetooth: needs TIKU_DRV_WIFI_CYW43_ENABLE=1, and through it
# MCU=rp2350 and the CYW43 board cap.  Without this flag a WiFi build leaves
# out the BT bring-up, its transport (bt_transport.c) and the BT firmware
# blob (firmware.S).
ifeq ($(TIKU_DRV_WIFI_CYW43_BT_ENABLE),1)
ifneq ($(TIKU_DRV_WIFI_CYW43_ENABLE),1)
$(error TIKU_DRV_WIFI_CYW43_BT_ENABLE=1 requires \
TIKU_DRV_WIFI_CYW43_ENABLE=1. BT bring-up reuses the WiFi driver's \
gSPI transport + backplane primitives, and the WLAN firmware must \
be running before the BT side can be powered up. Add \
TIKU_DRV_WIFI_CYW43_ENABLE=1 too, or drop TIKU_DRV_WIFI_CYW43_BT_ENABLE.)
endif
endif

# The device define is TIKU_DEVICE_ plus the MCU name in capitals:
# msp430fr5994 gives TIKU_DEVICE_MSP430FR5994, rp2350 TIKU_DEVICE_RP2350.
DEVICE_UPPER = $(shell echo $(MCU) | tr '[:lower:]' '[:upper:]')
DEVICE_DEFINE = TIKU_DEVICE_$(DEVICE_UPPER)

# ---------------------------------------------------------------------------
# Toolchain
#
# rp2350, ambiq, nordic, stm32n6, ra8p1:
#            arm-none-eabi-gcc from PATH, else under /usr
# esp32c61:  riscv32-esp-elf-gcc from PATH, else the newest version where
#            ESP-IDF's installer puts it
#            (~/.espressif/tools/riscv32-esp-elf/<ver>/riscv32-esp-elf), else
#            under /usr
# msp430:    msp430-elf-gcc from PATH, else under $(HOME)/tigcc
#
# TOOLCHAIN_DIR= on the make line sets the toolchain root and skips the search.
# ---------------------------------------------------------------------------
ifneq (,$(filter $(TIKU_PLATFORM),rp2350 ambiq nordic stm32n6 ra8p1))

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

else ifeq ($(TIKU_PLATFORM),esp32c61)

# A RISC-V newlib GCC for the rv32imac multilib the C61 runs; the kernel needs
# only newlib's libc, libm and libgcc.  TOOLCHAIN_PREFIX is the first of
# riscv-none-elf-, riscv32-unknown-elf- and riscv32-esp-elf- whose gcc is on
# PATH, else riscv32-esp-elf-.  TOOLCHAIN_DIR is the directory above that gcc,
# else the newest Espressif install under ~/.espressif, else /usr.  Either can
# be set on the command line.
TOOLCHAIN_PREFIX ?= $(shell \
	for p in riscv-none-elf- riscv32-unknown-elf- riscv32-esp-elf-; do \
	  command -v $${p}gcc > /dev/null 2>&1 && { echo $$p; exit 0; }; \
	done; echo riscv32-esp-elf-)
TOOLCHAIN_DIR    ?= $(shell \
	p=$$(command -v $(TOOLCHAIN_PREFIX)gcc 2>/dev/null) \
	  && { dirname $$(dirname "$$p"); exit 0; }; \
	d=$$(ls -d $(HOME)/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf \
	  2>/dev/null | sort | tail -1); \
	[ -n "$$d" ] && echo "$$d" || echo "/usr")
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

# MSP430 GCC support files (msp430.h, the device linker scripts): the
# directory of the first msp430.h under the toolchain's include/.
MSP430_SUPPORT_DIR ?= $(shell \
	find $(TOOLCHAIN_DIR)/include -type f -name "msp430.h" 2>/dev/null \
	| head -1 | xargs -r dirname)

endif

# ---------------------------------------------------------------------------
# Debug / Flash tools  (auto-detected from PATH; override with MSPDEBUG=…)
# ---------------------------------------------------------------------------
MSPDEBUG ?= $(shell command -v mspdebug 2>/dev/null || echo mspdebug)
# The mspdebug driver for the LaunchPad's eZ-FET (TI's MSP430 library).
DEBUGGER  = tilib

# picotool for RP2350: `make flash` loads the UF2 with it when it is
# installed, else copies the UF2 to a mounted RP2350/RP2 volume.
PICOTOOL ?= $(shell command -v picotool 2>/dev/null || echo picotool)

# SEGGER J-Link settings for the Ambiq, Nordic and RA8P1 rules: SWD at 4 MHz.
# Any of these can be overridden on the make command line.
JLINK           ?= JLinkExe
JLINK_GDB       ?= JLinkGDBServer
JLINK_IF        ?= SWD
JLINK_SPEED     ?= 4000
# JLINK_SN selects one J-Link probe by serial number.  Every J-Link has the
# same USB vendor ID (0x1366), so with several boards attached the serial is
# what picks one: `make flash MCU=apollo4l JLINK_SN=001160001290`.  Empty (the
# default) lets JLinkExe use the only probe connected.
JLINK_SN        ?=
JLINK_SN_ARG    := $(if $(strip $(JLINK_SN)),-SelectEmuBySN $(strip $(JLINK_SN)),)
# J-Link device name, MRAM load address and run sequence per Ambiq part.
# Every other MCU gets the Apollo510 values, which only the Ambiq rules read.
ifeq ($(MCU),apollo4l)
JLINK_DEVICE    ?= AMAP42KL-KBR
AMBIQ_LOAD_ADDR ?= 0x00018000
# The Apollo4 secure SBL waits, PC inside the SBL, while a debugger is
# attached at reset.  `qc` closes the J-Link connection with the target
# running, so the SBL hands off to the image at 0x18000; `Sleep 600` lets the
# SBL reach that wait first.  `q` leaves the target halted and the image
# does not start.
JLINK_RUN_SEQ   ?= r\ng\nSleep 600\nqc
else ifeq ($(MCU),apollo4p)
# Apollo4 Plus (AMAP42KP-KBR): the Lite's load address and SBL hand-off under
# its own J-Link device name.
JLINK_DEVICE    ?= AMAP42KP-KBR
AMBIQ_LOAD_ADDR ?= 0x00018000
JLINK_RUN_SEQ   ?= r\ng\nSleep 600\nqc
else
JLINK_DEVICE    ?= AP510NFA-CBR
AMBIQ_LOAD_ADDR ?= 0x00410000
# Apollo510 has no SBL wait: reset, go, quit.
JLINK_RUN_SEQ   ?= r\ng\nq
endif

# DEVICE_HAS_HIFRAM: 1 on the MSP430 parts with FRAM above 64 KB (FR5994,
# FR6989), 0 on every other MCU.
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
# Flag-change guard
#
# An object is rebuilt when its source or a header it includes changes (the
# .d files), but not when the flags or this Makefile change.  The guard
# fingerprints the command-line overrides ($(MAKEOVERRIDES)) and the checksum
# of this Makefile per build dir.  When the fingerprint differs from the last
# build of that dir, it deletes the dir's objects and main.elf/main.hex, so
# everything recompiles.  Runs at parse time.
#
# Without it, a build links objects compiled under other flags: a command
# goes missing from the shell table, or `undefined reference to tiku_basic_*`
# appears when a BASIC-on object meets a BASIC-off link.  Variables set in the
# environment and edits to the included .mk files are not fingerprinted.
# ---------------------------------------------------------------------------
# Both guards delete build outputs, so neither runs for `make options`, which
# builds nothing.
ifneq ($(MAKECMDGOALS),options)
BUILD_FLAGS_STAMP := $(BUILD_DIR)/.buildflags
_FLAG_GUARD := $(shell mkdir -p $(BUILD_DIR); \
    { printf '%s\n' "$(MAKEOVERRIDES)"; cksum $(firstword $(MAKEFILE_LIST)); } \
        > $(BUILD_DIR)/.buildflags.new; \
    if ! cmp -s $(BUILD_DIR)/.buildflags.new $(BUILD_FLAGS_STAMP) 2>/dev/null; \
    then \
        find $(BUILD_DIR) -name '*.o' -delete 2>/dev/null; \
        rm -f main.elf main.hex; \
        mv $(BUILD_DIR)/.buildflags.new $(BUILD_FLAGS_STAMP); \
        echo wiped; \
    else rm -f $(BUILD_DIR)/.buildflags.new; fi)
ifeq ($(_FLAG_GUARD),wiped)
$(info [flags changed -> $(BUILD_DIR) objects wiped for a clean rebuild])
endif

# main.elf is one path for every MCU, while the guard above is per build dir.
# .main-elf-mcu records the MCU of the last run; when the MCU changes,
# main.elf and main.hex are deleted.  Without this, the previous MCU's
# main.elf can be newer than every object of the new one: make skips the
# link, and that image is what gets sized and flashed.
_ELF_MCU_GUARD := $(shell \
    if [ -f main.elf ] && [ "`cat .main-elf-mcu 2>/dev/null`" != "$(MCU)" ]; \
    then rm -f main.elf main.hex; echo relink; fi; \
    echo "$(MCU)" > .main-elf-mcu)
ifeq ($(_ELF_MCU_GUARD),relink)
$(info [MCU changed -> stale main.elf dropped, will relink for $(MCU)])
endif
endif

# ---------------------------------------------------------------------------
# App selection: APP= turns tests, examples and demos off.
#   make APP=cli MCU=msp430fr5994   — the shell (APP=cli enables it)
#   make MCU=msp430fr5994           — no app
# ---------------------------------------------------------------------------
APP ?=

# TIKU_APP_DIR: the directory of the app firmware sources, which live in the
# TikuBench harness.  TikuBench exports it (tikubench/__init__.py); it may
# also be given on the make line.  APP=net and TIKU_TURBO_BENCH=1 need it,
# and with it empty (the default) those builds stop with an error naming it.
TIKU_APP_DIR ?=

# ---------------------------------------------------------------------------
# Shell service (a kernel service, independent of APP, tests and examples)
#   make MCU=msp430fr5994                       — shell on (the default)
#   make TIKU_SHELL_ENABLE=0 MCU=msp430fr5994   — no shell
#   make APP=cli MCU=msp430fr5994               — shell forced on
#
# Shell add-ons:
#   TIKU_SHELL_BASIC_ENABLE=1   — Tiku BASIC interpreter
#   TIKU_SHELL_COLOR=1          — ANSI colour output (default 0)
# ---------------------------------------------------------------------------
TIKU_SHELL_ENABLE ?= 1
TIKU_SHELL_COLOR  ?= 0
# BASIC is on by default on the Ambiq parts, whose SRAM tier floor (ambiq
# CFLAGS block) covers its arena, and off elsewhere.  On MSP430 it needs
# MEMORY_MODEL=large.
TIKU_SHELL_BASIC_ENABLE ?= $(if $(filter ambiq,$(TIKU_PLATFORM)),1,0)
# TIKU_INIT_ENABLE=1 builds the init system (boot configuration kept in NVM).
# TikuBench's init test categories pass TIKU_INIT_TEST=1, which turns it on.
TIKU_INIT_ENABLE  ?= 0
TIKU_INIT_TEST    ?= 0

# Init tests require the init system
ifeq ($(TIKU_INIT_TEST),1)
TIKU_INIT_ENABLE = 1
endif

# APP=cli enables the shell.  `override` makes it win over a
# TIKU_SHELL_ENABLE=0 on the command line, as TikuBench's slim test builds
# pass.
ifeq ($(APP),cli)
override TIKU_SHELL_ENABLE := 1
endif

# The init system uses the shell's parser, so it enables the shell.
# `override` gives TIKU_INIT_TEST=1 builds the shell even when the caller
# passes TIKU_SHELL_ENABLE=0.
ifeq ($(TIKU_INIT_ENABLE),1)
override TIKU_SHELL_ENABLE := 1
endif

# ---------------------------------------------------------------------------
# Scratch demos (exclusive with apps, tests and examples)
#   make DEMO=ping_a_host_to_pico MCU=rp2350 ...
# demos/ is gitignored; with the directory absent, DEMO= compiles no demo
# source.
# ---------------------------------------------------------------------------
DEMO ?=

# ---------------------------------------------------------------------------
# Optional components: HAS_TESTS=1 and HAS_EXAMPLES=1 opt in, and both are 0
# by default, so a plain `make` builds the core OS and the enabled services
# (the shell is on by default).  APP or DEMO forces tests and examples off.
# HAS_TIKUKITS and HAS_DRIVERS are 1 when tikukits/ and drivers/ are present,
# HAS_PRESENTATION when presentation/Makefile is.
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

# Bluetooth.  tikuOS's BLE host stack (tikukits/net/bluetooth) runs over the
# build's HCI controller: the CYW43439's BTSDIO, up with the Wi-Fi at boot,
# or the ESP32-C61's controller or the EM9305 die, which `bt on` powers on
# demand.  A board with the EM9305 gets its driver by default, as a declared
# part does, wherever the kits are present.
ifneq ($(MINIMAL),1)
ifneq ($(call board_has,EM9305),)
ifeq ($(HAS_TIKUKITS),1)
TIKU_DRV_BLE_EM9305_ENABLE ?= 1
endif
endif
endif
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
ifneq ($(HAS_TIKUKITS),1)
$(error TIKU_DRV_BLE_EM9305_ENABLE=1 needs tikukits/: the BLE host stack \
over the radio is tikukits/net/bluetooth. Check the kits out, or drop \
TIKU_DRV_BLE_EM9305_ENABLE)
endif
endif
TIKU_BT_HOST := $(if $(filter 1,$(TIKU_DRV_WIFI_CYW43_BT_ENABLE) \
                  $(TIKU_DRV_BLE_ESP_ENABLE) $(TIKU_DRV_BLE_EM9305_ENABLE)),1,0)
TIKU_BT_ON_DEMAND := $(if $(filter 1,$(TIKU_DRV_BLE_ESP_ENABLE) \
                       $(TIKU_DRV_BLE_EM9305_ENABLE)),1,0)
# Pairing's AES and P-256 in software, for a controller without the LE crypto
# commands; the CYW43439, the ESP32-C61 and the EM9305 all answer them, so it
# is off unless a build asks for it.
TIKU_BT_SW_CRYPTO ?= 0

# ---------------------------------------------------------------------------
# Per-kit enable flags
#
# A kit under tikukits/ is compiled only when its TIKU_KIT_<NAME>_ENABLE flag
# is 1.  Every flag defaults to 0 and is set by:
#   1. the command line, e.g. make TIKU_KIT_DS_ENABLE=1;
#   2. a block below that needs the kit: an app (APP=), an example
#      (TIKU_EXAMPLE_*), BASIC on ambiq and rp2350 (codec), RA8P1 (crypto);
#   3. TIKU_KITS_ALL=1, which enables every kit.
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

# BASIC's JSON$ needs the codec kit, so a BASIC build on ambiq or rp2350
# enables it.  On the other 32-bit ports JSON$ is compiled only when
# TIKU_KIT_CODEC_ENABLE=1 is passed; the MSP430 BASIC has no JSON$.
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

# UI needs GFX, so UI turns GFX on.
ifeq ($(TIKU_KIT_UI_ENABLE),1)
TIKU_KIT_GFX_ENABLE              := 1
endif

# ---------------------------------------------------------------------------
# Kits enabled by the active example or app
#
# Each example enables only the kits it uses: TIKU_EXAMPLE_GFX_DEMO=1
# compiles the gfx and epaper kits and no other.
# ---------------------------------------------------------------------------

# Sensor examples.
ifeq ($(TIKU_EXAMPLE_I2C_TEMP),1)
TIKU_KIT_SENSORS_ENABLE := 1
endif
ifeq ($(TIKU_EXAMPLE_DS18B20_TEMP),1)
TIKU_KIT_SENSORS_ENABLE := 1
endif

# Networking examples all need NET; HTTPS also needs CRYPTO.
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

# Display examples: e-paper, gfx and ui demos.
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

# TIKU_SHELL_NET_TEST=1 compiles the net kit into the shell firmware, so the
# shell hosts the UDP, TCP and CoAP servers of TikuBench's net suite; a block
# further down adds TCP, MQTT and CoAP.
ifeq ($(TIKU_SHELL_NET_TEST),1)
TIKU_KIT_NET_ENABLE     := 1
endif

# The rules above can turn UI on, so the UI -> GFX rule is applied again.
ifeq ($(TIKU_KIT_UI_ENABLE),1)
TIKU_KIT_GFX_ENABLE     := 1
endif

# ---------------------------------------------------------------------------
# Memory model (MSP430)
#
# MEMORY_MODEL=small uses 16-bit pointers, so code and data stay below 64 KB:
# code in the lower FRAM below the vectors at 0xFF80 (about 48 KB).
# MEMORY_MODEL=large adds -mlarge -mcode-region=either -mdata-region=either:
# 20-bit pointers, and the linker may place text, rodata and bss in HIFRAM
# (0x10000 and up) on the parts that have it (FR5994, FR6989).  The large
# model's 20-bit pointers and CALLA/MOVA make code and data larger.
#
# The vectors hold 16-bit addresses, so every interrupt handler must sit in
# lower FRAM.  TIKU_ISR (hal/tiku_compiler.h) applies __attribute__((lower))
# to each handler under GCC; a handler declared with a raw
# __attribute__((interrupt)) can land in HIFRAM, out of the vector's reach.
# A handler's calls into HIFRAM use 20-bit CALLA.
# ---------------------------------------------------------------------------
# The default follows the part: large on the parts with HIFRAM, small on the
# others.  Other platforms default to small, which only the MSP430 flags
# read.  MEMORY_MODEL= on the make line overrides the default.
ifeq ($(TIKU_PLATFORM),msp430)
MEMORY_MODEL ?= $(if $(filter 1,$(DEVICE_HAS_HIFRAM)),large,small)
else
MEMORY_MODEL ?= small
endif

# ---------------------------------------------------------------------------
# MSP430 build guards
# ---------------------------------------------------------------------------
ifeq ($(TIKU_PLATFORM),msp430)
# 0. Parts without HIFRAM (FR5969, FR2433 and smaller) are refused: the
#    kernel and the VFS do not fit the lower FRAM a small-model image can
#    address.  Their device headers, board headers and linker scripts stay in
#    the tree.
ifneq ($(DEVICE_HAS_HIFRAM),1)
$(error MCU=$(MCU) is not a supported TikuOS target: the core (kernel + VFS) \
does not fit a 64-KB-or-smaller MSP430.  Supported MSP430 parts are \
msp430fr5994 (256 KB) and msp430fr6989 (128 KB).  The FR5969/FR2433 arch code \
remains in-tree for reference)
endif

# 1. MEMORY_MODEL=large needs HIFRAM to place code and data in.  Guard 0
#    refuses every part without HIFRAM first, so this check takes effect
#    only if guard 0 is removed.
ifeq ($(MEMORY_MODEL),large)
ifneq ($(DEVICE_HAS_HIFRAM),1)
$(error MEMORY_MODEL=large is only supported on MCUs with HIFRAM \
(FR5994, FR6989). $(MCU) has only a single 64-KB-or-smaller FRAM \
region, so large model just inflates code by ~20% with nowhere to \
spill. Use MEMORY_MODEL=small (the default) on this part)
endif
endif
endif

# 2. BASIC on MSP430 needs MEMORY_MODEL=large: a small-model shell image
#    with BASIC overflows the lower FRAM.  TIKU_SHELL_BASIC_ALLOW_SMALL=1
#    skips the check.
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

# 3. BASIC_PROGRAM=foo.bas compiles a BASIC program into the image
#    (tools/bas_to_c.py turns it into a C string) and main.c runs it at
#    boot.  It forces TIKU_SHELL_BASIC_ENABLE=1 and MEMORY_MODEL=large.
ifneq ($(BASIC_PROGRAM),)
override TIKU_SHELL_BASIC_ENABLE := 1
override MEMORY_MODEL := large
endif

# ---------------------------------------------------------------------------
# LEA peripheral on MSP430FR5994
# ---------------------------------------------------------------------------
#
# The FR5994 has 8 KB of SRAM.  The toolchain's linker script reserves the
# upper 4 KB for the LEA (Low-Energy Accelerator), as LEARAM (~3.7 KB) and
# LEASTACK (312 B), leaving 4 KB for .bss and the stack.  TikuOS does not use
# the LEA: with LEA_ENABLE=0 (the default) the build links with
# arch/msp430/devices/msp430fr5994_8k_ram.ld, which gives all 8 KB to RAM.
# Any other value is refused; LEA_ENABLE=1 needs a LEA-aware linker script
# that keeps the same FRAM reservations.
# ---------------------------------------------------------------------------
LEA_ENABLE ?= 0
ifeq ($(MCU),msp430fr5994)
ifeq ($(LEA_ENABLE),1)
$(error LEA_ENABLE=1 on FR5994 needs a LEA-aware custom linker script preserving the pinned NVM region, module slot and high-BSS bounds)
else ifneq ($(LEA_ENABLE),0)
$(error LEA_ENABLE='$(LEA_ENABLE)' on FR5994: the only supported value is 0)
endif
endif

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
ifeq ($(TIKU_PLATFORM),rp2350)

# Cortex-M33 with a single-precision FPU, built for the softfp ABI:
# floating-point arguments travel in integer registers.
CFLAGS  = -mcpu=cortex-m33 -mthumb
CFLAGS += -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_RP2350=1
# newlib-nano (integer-only printf, small reentrancy state) over the nosys
# syscall stubs.  The full newlib stdio hangs on RP2350: its
# global_stdio_init walks file structures the nosys stubs do not provide.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections -fno-common

# SRAM (AUTO) tier: rp2350.ld carves it from the SRAM left after .bss, so the
# TLS and CYW43 buffers, which are statics, are placed first.
# TIKU_TIER_SRAM_MIN is the guaranteed minimum: it must cover
# BASIC_ARENA_BYTES at the configured TIKU_BASIC_PROGRAM_LINES (512 here),
# which kernel/shell/basic/tiku_basic_arena.inl asserts at build time.
TIKU_TIER_SRAM_MIN ?= 262144
CFLAGS += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS += -DTIKU_TIER_SRAM_DERIVED=1
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
ifeq ($(HAS_TLS),1)
# BASIC with TLS: a 4 KB TCP receive buffer and 2 connections.  A TLS server
# flight is several KB; with the default 512-byte buffer it arrives in
# 512-byte stop-and-wait steps, and one lost window-update ACK stalls the
# handshake.  HTTPGET$ uses one connection.
CFLAGS += -DTIKU_KITS_NET_TCP_RX_BUF_SIZE=4096
CFLAGS += -DTIKU_KITS_NET_TCP_MAX_CONNS=2
endif
endif

else ifeq ($(TIKU_PLATFORM),ambiq)

# CPU and FPU per Ambiq part: Cortex-M4F with a single-precision FPU (Apollo4
# Lite and Plus) or Cortex-M55 with Helium (Apollo510, 510B), where
# -mfpu=auto takes the FPU from -mcpu.
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
CFLAGS  = -mcpu=cortex-m4 -mthumb
CFLAGS += -mfpu=fpv4-sp-d16 -mfloat-abi=hard
else
CFLAGS  = -mcpu=cortex-m55 -mthumb
CFLAGS += -mfpu=auto -mfloat-abi=hard
endif
CFLAGS += -Os -Wall -Wextra -Wno-psabi
# newlib-nano (integer printf) and the nosys syscall stubs (_sbrk, _write,
# _read, ...), as on rp2350.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_AMBIQ=1
# SRAM (AUTO) tier: the linker script carves it from the shared SRAM (SSRAM)
# above the .ssram statics; the crt powers the SSRAM before any use.  The mem
# size type is 32-bit here (arch/ambiq/tiku_mem_arch.h), so the tier can
# exceed 64 KB.  TIKU_TIER_SRAM_MIN is the guaranteed minimum, not the size:
# it must cover BASIC_ARENA_BYTES, and the Apollo510 parts' BASIC has more
# program lines than the Apollo4 parts'.
ifeq ($(MCU),apollo510)
TIKU_TIER_SRAM_MIN ?= 327680
else ifeq ($(MCU),apollo510b)
TIKU_TIER_SRAM_MIN ?= 327680
else
TIKU_TIER_SRAM_MIN ?= 262144
endif
CFLAGS  += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS  += -DTIKU_TIER_SRAM_DERIVED=1
CFLAGS  += -DTIKU_TIER_SRAM_EXTRA=1
CFLAGS += -DTIKU_TIER_NVM_SIZE=16384      # 16 KB NVM tier
# HTTPS on Ambiq (HAS_TLS=1):
# - TIKU_KITS_NET_TCP_BUF_PERSIST=0 puts the TCP RX ring and TX pool in .bss,
#   which startup zeroes; .persistent is not zeroed at startup.
# - A 4 KB receive ring per connection holds a TLS 1.3 server's
#   post-handshake NewSessionTicket records, which stay in the ring until the
#   first application read, together with the HTTP response.  A smaller ring
#   fills with the tickets before the response arrives.
# - 2 connections; HTTPGET$ uses one.
ifeq ($(HAS_TLS),1)
CFLAGS += -DTIKU_KITS_NET_TCP_BUF_PERSIST=0
CFLAGS += -DTIKU_KITS_NET_TCP_RX_BUF_SIZE=4096
CFLAGS += -DTIKU_KITS_NET_TCP_MAX_CONNS=2
endif
# Part selectors for the vendored register maps (apollo4l.h, apollo510.h).
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
# apollo4p builds against the apollo4l register map (apollo4l.h): the two
# parts match for the peripherals the M4F backends use (UART, GPIO, PWRCTRL,
# STIMER, MRAM), so those backends serve both.
CFLAGS += -DPART_apollo4l -DAM_PART_APOLLO4L -Dgcc
else
CFLAGS += -DPART_apollo510 -DAM_PART_APOLLO510 -DAM_PACKAGE_BGA -Dgcc
endif
# Console routing is a board property: which UART the on-board J-Link exposes
# as its VCOM is a PCB trace.  The Apollo4 Plus EVB routes it to UART0 (pads
# 60/47); the Lite uses UART2 (pads 54/11), the default.
ifeq ($(BOARD),apollo4p_evb)
CFLAGS += -DTIKU_CONSOLE_UART0
endif
# The Apollo510 Blue EVB routes its VCOM to UART1 (pads 12/14, funcsel 5); the
# base Apollo510 EVB uses UART0 (pads 30/55).  The shared M55 UART driver, the
# crt vector table (the IRQ slot moves with the UART) and the wake source all
# key off TIKU_CONSOLE_UART1.
ifeq ($(BOARD),apollo510b_evb)
CFLAGS += -DTIKU_CONSOLE_UART1
endif
# EM9305 BLE radio on IOM6 SPI (the Blue EVB, on by default there).  It
# defines TIKU_SPI_IOM_ENABLE, which builds tiku_spi_arch.c as the IOM SPI
# master driver in place of its stub.  The EM9305 SPI-HCI transport and the
# `ble` shell command (a first-contact probe) are added in the source blocks
# below, after SRCS is assigned.  The capability refusals near the top have
# already checked the board.
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
CFLAGS += -DTIKU_DRV_BLE_EM9305_ENABLE=1 -DTIKU_SPI_IOM_ENABLE=1
endif
CFLAGS += -I$(PROJ_DIR)
# CMSIS register headers, vendored in arch/ambiq/cmsis/: apollo510.h and
# apollo4l.h with their system headers and the Arm CMSIS-Core headers they
# include.  Provenance and licences: arch/ambiq/cmsis/PROVENANCE.md.
CFLAGS += -I$(PROJ_DIR)/arch/ambiq/cmsis
CFLAGS += -ffunction-sections -fdata-sections -fno-common

else ifeq ($(TIKU_PLATFORM),nordic)

# Cortex-M33 (nRF54L15, nRF54LM20A/B) with a single-precision FPU, built for
# the softfp ABI as on rp2350.
CFLAGS  = -mcpu=cortex-m33 -mthumb
CFLAGS += -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS += -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_NORDIC=1
# newlib-nano (integer printf) and the nosys syscall stubs, as on rp2350 and
# ambiq.
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections -fno-common

# SRAM (AUTO) tier, carved by the linker: on the LM20 the rest of RAM2 after
# the AXON statics, on the L15 the primary bank between .bss and the stack
# guard.  TIKU_TIER_SRAM_MIN is the guaranteed minimum.  It must cover
# BASIC_ARENA_BYTES (kernel/shell/basic/tiku_basic_arena.inl), which grows by
# 148 bytes per program line; the LM20 has 1400 lines, the L15 256.  The
# floor does not change with TIKU_THREADS_ENABLE: thread stacks and TLS state
# are .bss and stack, not tier allocations.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
TIKU_TIER_SRAM_MIN ?= 253952
else
# L15: 92 KB.  The primary bank also holds the reservation table
# (TIKU_MEM_MAX_RESERVATIONS records, a static), and a 96 KB floor leaves no
# room for it.
TIKU_TIER_SRAM_MIN ?= 94208
endif
endif
TIKU_TIER_SRAM_MIN ?= 4096
CFLAGS += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS += -DTIKU_TIER_SRAM_DERIVED=1
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
CFLAGS += -DTIKU_TIER_SRAM_EXTRA=1
endif

else ifeq ($(TIKU_PLATFORM),stm32n6)

# STM32N657: Cortex-M55 with Helium.  The image runs from the 255 KB SRAM
# window the boot ROM loads it into: code, data and stack share it, with no
# flash and no XIP.
CFLAGS  = -mcpu=cortex-m55 -mthumb
CFLAGS += -mfpu=auto -mfloat-abi=hard
CFLAGS += -Os -Wall -Wextra -Wno-psabi
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_STM32N6=1
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections
# The AXI SRAM above the ROM's image window (0x341C0000 to 0x343C0000,
# 2 MB) holds the .axisram statics, which tiku_crt_early.c zeroes after the
# banks are powered, and above them the SRAM tier: the rest of the bank less
# a 32 KB reserve (stm32n657.ld).  An access above 0x343C0000 hangs the bus.
# The tier is linker-derived, so TIKU_TIER_SRAM_MIN is its guaranteed
# minimum, not its size.
TIKU_TIER_SRAM_MIN ?= 262144
CFLAGS += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS += -DTIKU_TIER_SRAM_DERIVED=1

else ifeq ($(TIKU_PLATFORM),ra8p1)

# R7KA8P1KF: Cortex-M85 with Helium.  The image links into the 1 MB code
# MRAM, which is byte-writable in place, and a reset starts it.
CFLAGS  = -mcpu=cortex-m85 -mthumb
CFLAGS += -mfpu=auto -mfloat-abi=hard
CFLAGS += -Os -Wall -Wextra -Wno-psabi
CFLAGS += --specs=nano.specs --specs=nosys.specs
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_RA8P1=1
# The software entropy source (tiku_trng_arch.c) conditions its samples with
# the crypto kit's SHA-256, so this platform always enables the kit.  The
# part's hardware generator is reachable only through a vendor library.
TIKU_KIT_CRYPTO_ENABLE := 1
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections
# r7ka8p1kf.ld carves the SRAM tier from the SRAM left after .bss, so this
# line sets no size, only the floor: the BASIC arena is asserted against it,
# and the linker asserts the carved span is not below it.
TIKU_TIER_SRAM_MIN ?= 262144
CFLAGS  += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS  += -DTIKU_TIER_SRAM_DERIVED=1

else ifeq ($(TIKU_PLATFORM),esp32c61)

# ESP32-C61: one RV32IMAC core.  Code, data, stack and the tier share the
# 320 KB of HP SRAM the ROM loads the image into, apart from code a build
# places in the flash XIP window.
ESP32C61_ARCH := -march=rv32imac_zicsr_zifencei -mabi=ilp32
CFLAGS  = $(ESP32C61_ARCH)
CFLAGS += -Os -Wall -Wextra
CFLAGS += --specs=nano.specs
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_ESP32C61=1
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections
# The tier is the SRAM left past the image, so its floor is small.  BASIC's
# arena is in PSRAM on this part, so the floor does not have to cover it.
TIKU_TIER_SRAM_MIN ?= 32768
CFLAGS += -DTIKU_TIER_SRAM_MIN=$(TIKU_TIER_SRAM_MIN)
CFLAGS += -DTIKU_TIER_SRAM_DERIVED=1

else

CFLAGS  = -mmcu=$(MCU) -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -D$(TIKU_BOARD_DEFINE)=1
CFLAGS += -DPLATFORM_MSP430=1
ifeq ($(MEMORY_MODEL),large)
# -mlarge:               20-bit pointers, CALLA/MOVA for full FRAM reach
# -mcode-region=either:  text may be placed in HIFRAM, freeing lower FRAM
# -mdata-region=either:  data may also go to .upper.bss/.upper.data in
#                        HIFRAM.  The kernel MPU grants R+W+X on segment 3
#                        when the device has HIFRAM (TIKU_DEVICE_HAS_HIFRAM
#                        in the device header; the default protection in
#                        arch/msp430/tiku_mpu_arch.c), so writes to data
#                        there (the UART RX ring, the process queue, the
#                        shell tables) succeed.  With 20-bit pointers, data
#                        kept in lower memory overflows the SRAM.
CFLAGS += -mlarge -mcode-region=either -mdata-region=either
# TIKU_MEMORY_MODEL_LARGE tells C code the build is large-model.  The HIFRAM
# tier pool (kernel/memory/tiku_tier.c), BASIC's HIFRAM capacity profile and
# the `free` command's HIFRAM figures exist only in large-model builds on a
# part with HIFRAM: a small-model build has no .upper.bss output section, and
# its 16-bit relocations cannot reach HIFRAM.
CFLAGS += -DTIKU_MEMORY_MODEL_LARGE=1
endif
CFLAGS += -I$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
CFLAGS += -I$(MSP430_SUPPORT_DIR)
endif
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections

endif

# The VFS node tables and other static tables use positional initializers
# that leave optional trailing fields (such as tiku_vfs_node_t.desc) zero or
# NULL.  -Wextra turns on -Wmissing-field-initializers, which flags each such
# entry; this one warning is turned off on every platform.
CFLAGS += -Wno-missing-field-initializers

# Preemptive worker threads (opt-in; not on MSP430).  Thread 0 is the
# cooperative kernel; workers are stackful compute threads limited to the
# ISR-safe primitives (kernel/threads/tiku_thread.h).  The Cortex-M parts
# share one switcher (kernel/threads/tiku_thread_cortexm.inl) behind a
# per-platform PendSV shim; esp32c61 has its own.  This block must stay below
# the per-platform blocks, which assign CFLAGS with `=`: inside one of them
# the define reaches that platform only, and on the others the thread code
# compiles to empty stubs.
ifeq ($(TIKU_THREADS_ENABLE),1)
ifeq ($(TIKU_PLATFORM),msp430)
$(error TIKU_THREADS_ENABLE=1 requires a Cortex-M part; MSP430 \
stays cooperative -- 2 KB of SRAM has no room for per-thread stacks)
endif
ifeq ($(filter apollo510 apollo510b apollo4l apollo4p rp2350 nrf54l15 nrf54lm20a nrf54lm20b ra8p1 esp32c61,$(MCU)),)
$(error TIKU_THREADS_ENABLE=1 needs a supported part -- \
apollo510/apollo510b (M55), apollo4l/apollo4p (M4F), rp2350 or \
nrf54l15/nrf54lm20a (M33), ra8p1 (M85), esp32c61 (RISC-V); $(MCU) has no \
thread backend. The Cortex-M switcher is generic asm \
(kernel/threads/tiku_thread_cortexm.inl); \
adding a part = a two-line shim that names its PendSV vector symbol (plus a \
custom cycle source if the part's DWT freezes standalone), and proving the \
torture suite)
endif
CFLAGS += -DTIKU_THREADS_ENABLE=1
endif

# UART_BAUD= sets the console baud rate (TIKU_BOARD_UART_BAUD); empty keeps
# the board header's default.
UART_BAUD ?=
ifneq ($(UART_BAUD),)
CFLAGS += -DTIKU_BOARD_UART_BAUD=$(UART_BAUD)
endif

# EXTRA_CFLAGS= on the make line is added to CFLAGS; the flag-change guard
# fingerprints it with the other command-line variables.
CFLAGS += $(EXTRA_CFLAGS)

ifeq ($(HAS_APPS),1)
CFLAGS += -DHAS_APPS=1
endif
ifeq ($(HAS_TESTS),1)
CFLAGS += -DHAS_TESTS=1
# Ambiq test builds keep -Wimplicit-function-declaration a warning: some
# TikuBench category dispatchers (e.g. tier) call void(void) test functions
# whose prototype header sits behind another TEST_* gate, and GCC 14 and
# newer make an implicit declaration an error.
ifeq ($(TIKU_PLATFORM),ambiq)
CFLAGS += -Wno-error=implicit-function-declaration
endif
endif
# Board capabilities -> -DTIKU_BOARD_HAS_*.  Must come after the per-platform
# `CFLAGS = ...` assignments above, which would drop them.
CFLAGS += $(BOARD_CAP_DEFINES)

ifeq ($(HAS_EXAMPLES),1)
CFLAGS += -DHAS_EXAMPLES=1
endif
ifeq ($(HAS_DEMOS),1)
CFLAGS += -DHAS_DEMOS=1
# The demo sources are added to SRCS further down, after SRCS is assigned.
endif
ifeq ($(HAS_TIKUKITS),1)
CFLAGS += -DHAS_TIKUKITS=1
endif
ifeq ($(HAS_DRIVERS),1)
CFLAGS += -DHAS_DRIVERS=1
endif

ifeq ($(TIKU_PLATFORM),rp2350)

LDFLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
# nano.specs swaps in libc_nano (integer-only printf, without the stdio init
# that hangs on bare metal).  nano comes first and nosys second, so libnosys
# provides the syscall stubs.
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
# nano.specs: libc_nano (small printf, no stdio init); nosys.specs: the
# libnosys syscall stubs (_sbrk, _write, ...).
LDFLAGS += --specs=nano.specs --specs=nosys.specs
LDFLAGS += -nostartfiles -static
ifeq ($(MCU),apollo4p)
# Apollo4 Plus: the Lite's map with a 2 MB shared-SRAM window (both SSRAM
# banks, powered in the crt) in place of the Lite's 1 MB, for a larger SRAM
# tier.  The rest of the Plus's SRAM is not mapped (see apollo4p.ld).
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
# System libraries only (libm, libc, libgcc); libnosys comes through
# nosys.specs above.
LDLIBS  = -Wl,--start-group
LDLIBS += -lm -lc -lgcc
LDLIBS += -Wl,--end-group

else ifeq ($(TIKU_PLATFORM),nordic)

LDFLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
# The LM20B's memory map is the LM20A's, so both link with nrf54lm20a.ld.
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

else ifeq ($(TIKU_PLATFORM),stm32n6)

LDFLAGS  = -mcpu=cortex-m55 -mthumb -mfpu=auto -mfloat-abi=hard
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
LDFLAGS += -Tarch/stm32n6/devices/stm32n657.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
# The vector table is the image's first bytes and nothing references it;
# without -u, --gc-sections drops it and the boot ROM finds no initial SP or
# entry point.
LDFLAGS += -Wl,-u,tiku_stm32n6_vectors
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
LDLIBS   = -Wl,--start-group
LDLIBS  += -lm -lc -lgcc
LDLIBS  += -Wl,--end-group

else ifeq ($(TIKU_PLATFORM),ra8p1)

LDFLAGS  = -mcpu=cortex-m85 -mthumb -mfpu=auto -mfloat-abi=hard
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
LDFLAGS += -Tarch/ra8p1/devices/r7ka8p1kf.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
# Nothing in C references the vector table; without -u, --gc-sections drops it
# and a reset never reaches the reset handler.
LDFLAGS += -Wl,-u,tiku_ra8p1_vectors
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
LDLIBS   = -Wl,--start-group
LDLIBS  += -lm -lc -lgcc
LDLIBS  += -Wl,--end-group

else ifeq ($(TIKU_PLATFORM),esp32c61)

LDFLAGS  = $(ESP32C61_ARCH)
LDFLAGS += --specs=nano.specs --specs=nosys.specs -nostartfiles
# Code and data share one SRAM segment, which is therefore RWX; this flag
# silences ld's warning about it.
LDFLAGS += -Wl,--no-warn-rwx-segments
LDFLAGS += -Tarch/esp32c61/devices/esp32c61.ld
# Where the arch script finds tiku_xip.ld, generated below the driver includes.
LDFLAGS += -L$(BUILD_DIR)
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
LDLIBS   = -Wl,--start-group
LDLIBS  += -lm -lc -lgcc
LDLIBS  += -Wl,--end-group

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
# Link map in build/<mcu>/main.map, as on the ARM ports.
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/main.map
# --- msp430-elf ld DWARF workaround -------------------------------------
# libnosys.a, which msp430-elf-gcc links by default (-lnosys in its link
# spec), carries a malformed .debug_line unit that the 9.3.1 toolchain's ld
# mishandles under --gc-sections on larger images: the link fails with "line
# info data is bigger than the space remaining in the section" (e.g. the
# init-boot and init-fram test builds, with the shell and init system).
# TikuOS compiles without -g, so the build links a debug-stripped copy of
# libnosys from $(BUILD_DIR), which the -L below puts ahead of the
# toolchain's.
NOSYS_FIXED := $(BUILD_DIR)/libnosys.a
# The copy comes from the multilib the link uses, found by -print-file-name
# with the model flags: lib/libnosys.a for the small model,
# large/full-memory-range/libnosys.a for -mlarge -mcode-region=either
# -mdata-region=either.  A mismatched model fails the link with "assumes
# data is exclusively in lower memory".
NOSYS_ORIG  := $(shell $(CC) -mmcu=$(MCU) $(if $(filter large,$(MEMORY_MODEL)),-mlarge -mcode-region=either -mdata-region=either) -print-file-name=libnosys.a)
LDFLAGS    += -L$(BUILD_DIR)
LDFLAGS += -Wl,-u,tiku_autostart_processes

# Build the debug-stripped libnosys (defined only in this msp430 branch).
$(NOSYS_FIXED): $(NOSYS_ORIG)
	@mkdir -p $(BUILD_DIR)
	@cp $(NOSYS_ORIG) $@ && $(OBJCOPY) --strip-debug $@

endif

ifeq ($(TIKU_PLATFORM),msp430)

# FR5994: msp430fr5994_8k_ram.ld takes the place of the toolchain's default
# script.  It includes the stock msp430fr5994.ld, then redeclares RAM as the
# whole 8 KB (LEARAM and LEASTACK get length 0), holds the top of HIFRAM back
# for the pinned NVM backend and the module slot, corrects the high-.bss
# clear size and defines __hifram_end.  TIKU_FR5994_LEA_DISABLED makes the
# device header report 8 KB of RAM.
ifeq ($(MCU),msp430fr5994)
ifeq ($(LEA_ENABLE),0)
LDFLAGS += -Tarch/msp430/devices/msp430fr5994_8k_ram.ld
CFLAGS  += -DTIKU_FR5994_LEA_DISABLED=1
endif
endif

# FR6989: msp430fr6989_hifram.ld fixes the .persistent origin, holds the top
# of HIFRAM back for the pinned NVM backend and the module slot, corrects the
# high-.bss clear size and defines __hifram_end.  Persistent data another
# image left on a board reads correctly only if that image used this layout.
ifeq ($(MCU),msp430fr6989)
LDFLAGS += -Tarch/msp430/devices/msp430fr6989_hifram.ld
endif

endif # TIKU_PLATFORM == msp430

# Every linker script but MSP430's collects the persist-cell table
# (.tiku_cells), so boot can carry cells by key across an update that moves
# them (kernel/memory/tiku_mem.h).
ifneq ($(TIKU_PLATFORM),msp430)
CFLAGS += -DTIKU_CELL_TABLE=1
endif

# TIKU_TIER_SRAM_MIN reaches the linker as __tier_sram_floor, where
# arch/common/tiku_sram_layout.ld asserts the carved SRAM tier is at least
# that large; tiku_basic_arena.inl asserts BASIC's arena against the same
# value.  A link outside make uses the linker script's default floor.
ifneq (,$(findstring TIKU_TIER_SRAM_DERIVED=1,$(CFLAGS)))
LDFLAGS += -Wl,--defsym=__tier_sram_floor=$(TIKU_TIER_SRAM_MIN)
endif

# ---------------------------------------------------------------------------
# Source files — core OS
#
# MINIMAL=1 (every platform but MSP430) builds main_minimal.c in place of the
# core OS: a bare-metal smoke test with no kernel, scheduler, processes, VFS
# or shell.  It brings up the clocks and the console, then prints "TikuOS
# minimal: hello #N" and toggles an LED in a loop.  If it prints nothing, the
# fault is in boot, clock or console setup.
# ---------------------------------------------------------------------------
MINIMAL ?= 0

ifeq ($(MINIMAL),1)
ifeq ($(filter $(TIKU_PLATFORM),rp2350 ambiq nordic stm32n6 ra8p1 esp32c61),)
$(error MINIMAL=1 is only supported on MCU=rp2350, MCU=apollo510, MCU=nrf54l15, MCU=nrf54lm20a, MCU=stm32n6, MCU=ra8p1, or MCU=esp32c61)
endif

# The minimal entry point and the arch files it needs.
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
else ifeq ($(TIKU_PLATFORM),nordic)
SRCS += arch/nordic/tiku_crt_early.c
SRCS += arch/nordic/tiku_cpu_freq_boot_arch.c
SRCS += arch/nordic/tiku_cpu_settings_arch.c
SRCS += arch/nordic/tiku_cpu_common.c
SRCS += arch/nordic/tiku_power_arch.c
SRCS += arch/nordic/tiku_uart_arch.c
SRCS += arch/nordic/tiku_gpio_arch.c
else ifeq ($(TIKU_PLATFORM),stm32n6)
SRCS += arch/stm32n6/tiku_crt_early.c
SRCS += arch/stm32n6/tiku_cpu_freq_boot_arch.c
SRCS += arch/stm32n6/tiku_cpu_common.c
SRCS += arch/stm32n6/tiku_uart_arch.c
SRCS += arch/stm32n6/tiku_gpio_arch.c
else ifeq ($(TIKU_PLATFORM),ra8p1)
SRCS += arch/ra8p1/tiku_crt_early.c
SRCS += arch/ra8p1/tiku_cpu_freq_boot_arch.c
SRCS += arch/ra8p1/tiku_cpu_common.c
SRCS += arch/ra8p1/tiku_uart_arch.c
SRCS += arch/ra8p1/tiku_trng_arch.c
SRCS += arch/ra8p1/tiku_gpio_arch.c
# main_minimal.c paces its output with the tick, so the minimal build
# includes it: the 128 Hz rate can be checked against a wall clock with
# nothing else running.
SRCS += arch/ra8p1/tiku_timer_arch.c
SRCS += arch/ra8p1/tiku_cache_arch.c
SRCS += arch/ra8p1/tiku_mram_arch.c
SRCS += arch/ra8p1/tiku_nvm_region_ra8p1.c
SRCS += arch/ra8p1/tiku_fault_arch.c
SRCS += arch/ra8p1/tiku_dma_arch.c
SRCS += arch/ra8p1/tiku_sdram_arch.c
SRCS += arch/ra8p1/tiku_xflash_arch.c
SRCS += arch/ra8p1/tiku_npu_arch.c
# main_minimal.c exercises the USB-HS disk and the model store, which need the
# sources below.  They sit behind the kernel build's gate, so a board without
# the USB-HS connector builds without them.
ifeq ($(TIKU_DRV_USBHS_ENABLE),1)
SRCS += arch/ra8p1/tiku_usbhs_arch.c
SRCS += arch/ra8p1/tiku_store_arch.c
SRCS += kernel/usb/tiku_usbd_msc.c
SRCS += kernel/fs/tiku_bigblob.c
CFLAGS += -DTIKU_DRV_USBHS_ENABLE=1
endif
# main_minimal.c checks that a critical section, which masks the NVIC, leaves
# the tick running: SysTick is a core exception the NVIC mask does not reach.
SRCS += arch/ra8p1/tiku_crit_arch.c
else ifeq ($(TIKU_PLATFORM),esp32c61)
SRCS += arch/esp32c61/tiku_crt_early.c
SRCS += arch/esp32c61/tiku_cpu_freq_boot_arch.c
SRCS += arch/esp32c61/tiku_cpu_common.c
SRCS += arch/esp32c61/tiku_uart_arch.c
SRCS += arch/esp32c61/tiku_gpio_arch.c
# The interrupt layer, the timers, critical sections and the flash driver
# are in the minimal build so main_minimal.c can check the tick's rate,
# masking and catch-up, the htimer alarm and the flash layer (on its scratch
# sector) with nothing else running.
SRCS += arch/esp32c61/tiku_irq_arch.c
SRCS += arch/esp32c61/tiku_timer_arch.c
SRCS += arch/esp32c61/tiku_htimer_arch.c
SRCS += arch/esp32c61/tiku_crit_arch.c
SRCS += arch/esp32c61/tiku_flash_arch.c
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
# Cortex-M33 workers (core 0): the generic switcher through the RP2350 shim,
# whose strong tiku_rp2350_pendsv_handler overrides the crt's weak alias.
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/arm-rp2350/tiku_thread_arch.c
endif

# Console backend: the native USB CDC stack for TIKU_CONSOLE=usb or both.
ifeq ($(TIKU_CONSOLE),usb)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1
else ifeq ($(TIKU_CONSOLE),both)
SRCS   += arch/arm-rp2350/tiku_usb_cdc_arch.c
CFLAGS += -DTIKU_CONSOLE_USB=1 -DTIKU_CONSOLE_BOTH=1
endif

else ifeq ($(TIKU_PLATFORM),nordic)

# Nordic nRF54L arch (Cortex-M33).
SRCS += arch/nordic/tiku_cpu_common.c
# Console backend: the nRF54LM20's own USB port for TIKU_CONSOLE=usb or both.
ifneq ($(filter usb both,$(TIKU_CONSOLE)),)
SRCS   += arch/nordic/tiku_usbhs_arch.c
SRCS   += arch/nordic/tiku_usbhs_dev.c
SRCS   += arch/nordic/tiku_usb_cdc_arch.c
SRCS   += kernel/usb/tiku_usbd_ctrl.c
CFLAGS += -DTIKU_CONSOLE_USB=1
ifeq ($(TIKU_CONSOLE),both)
CFLAGS += -DTIKU_CONSOLE_BOTH=1
endif
endif
SRCS += arch/nordic/tiku_crt_early.c
SRCS += arch/nordic/tiku_cpu_freq_boot_arch.c
SRCS += arch/nordic/tiku_cpu_settings_arch.c
SRCS += arch/nordic/tiku_power_arch.c
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
SRCS += arch/nordic/tiku_ble_ccm_arch.c
SRCS += arch/nordic/tiku_fault_arch.c
# The on-die 2.4 GHz RADIO backs the generic broadcast-BLE capability: the
# tiku_ble_adv facade, the BASIC BLEBEACON/BLESCAN$ words and /sys/radio test
# TIKU_HAS_BLE_ADV, not the chip.  TIKU_CAP_BLE_ADV lets the bleadv and
# rftest shell commands build further down when they are requested.
SRCS += interfaces/bluetooth/tiku_ble_adv.c
CFLAGS += -DTIKU_HAS_BLE_ADV=1
TIKU_CAP_BLE_ADV := 1
# LE Secure Connections (SMP) pairing, crypto and state machine.  Both roles
# use it, the FLPR-backed peripheral (responder) and the RADIO-driven central
# test peer (initiator), so it builds with the broadcast capability, outside
# the FLPR block.  AES-CMAC and f4/f5/f6 run on the CRACEN AES-ECB; P-256
# ECDH comes from tikukits/crypto/p256.  --gc-sections drops the code from a
# build that does not pair.
SRCS += interfaces/bluetooth/tiku_ble_smp.c
SRCS += interfaces/bluetooth/tiku_ble_smp_pair.c
SRCS += interfaces/bluetooth/tiku_ble_bond.c
SRCS += $(wildcard tikukits/crypto/p256/*.c)
# IEEE 802.15.4 on the same on-die RADIO: the PHY (tiku_ieee154_arch.c), the
# frame layer and the MAC (tiku_154.c).  Code tests the TIKU_HAS_154
# capability, not the chip; TIKU_CAP_154 lets the radio154 shell command
# build when it is requested.
SRCS += arch/nordic/tiku_ieee154_arch.c
SRCS += interfaces/radio/tiku_154_frame.c
SRCS += interfaces/radio/tiku_154.c
CFLAGS += -DTIKU_HAS_154=1
TIKU_CAP_154 := 1
# FLPR (the VPR RISC-V coprocessor), opt-in: builds the FLPR firmware
# (arch/nordic/flpr/) with the xPack riscv-none-elf toolchain under the
# gitignored temp/toolchains/ (RISCV_PREFIX), embeds the flat binary in this
# image, and compiles the loader (tiku_flpr_arch.c) and /sys/flpr.
ifeq ($(TIKU_FLPR_ENABLE),1)
# The nRF54L15 and nRF54LM20A/B have the same VPR00 (FLPR) core: base
# 0x5004C000, IRQ 76, MPC00 at 0x50041000, the same SPU10/SPU20 slots.  Its
# memory is the top 16 KB of the lower SRAM bank (0x2003C000-0x2003FFFF) on
# every nordic part, so tiku_flpr_ipc.h and tiku_flpr.ld serve all three; the
# LM20's RAM2 tier is outside it.  Each part's app linker script reserves
# those 16 KB in every build, so the layout does not change with
# TIKU_FLPR_ENABLE.
SRCS += arch/nordic/tiku_flpr_arch.c
CFLAGS += -DTIKU_FLPR_ENABLE=1
# The FLPR firmware is the on-die BLE controller; the BLE serial facade backs
# the BASIC BLE words over its mailbox.  SRCS is de-duplicated, so the host
# stack's block adding the same file is harmless.
SRCS += interfaces/bluetooth/tiku_ble_serial.c
# The ATT/GATT host for the FLPR controller runs on the M33; the FLPR
# forwards L2CAP frames over the mailbox.
SRCS += interfaces/bluetooth/tiku_ble_host.c
# The coprocessor interface (interfaces/coproc) backed by the FLPR, and
# /sys/coproc.  TIKU_COPROC_MSG_CAP must equal the FLPR mailbox's message
# cap; a _Static_assert in tiku_coproc_arch.c checks it.
SRCS += arch/nordic/tiku_coproc_arch.c           # interfaces/coproc backend
SRCS += kernel/vfs/tree/tiku_vfs_tree_coproc.c   # /sys/coproc
CFLAGS += -DTIKU_HAS_COPROC=1 -DTIKU_COPROC_MSG_CAP=240u
RISCV_PREFIX ?= temp/toolchains/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
RISCV_CC      = $(RISCV_PREFIX)gcc
FLPR_BUILD    = $(BUILD_DIR)/flpr
# The FLPR is RV32E (16 registers) with M and C; Zicsr is for its CSR
# accesses (mtvec, the VPR and VIO CSRs).
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

# Ambiq arch: Apollo4 Lite and Plus (Cortex-M4F), Apollo510 and 510B
# (Cortex-M55).  The common blocks further down add the SPI, LCD and GPIO
# arch files for MSP430 only, so this block adds the Ambiq ones.  The
# backends here serve every Ambiq part (I2C and 1-Wire are stubs); the
# per-part ones follow in the split below.
SRCS += arch/ambiq/tiku_i2c_arch.c
SRCS += arch/ambiq/tiku_onewire_arch.c
SRCS += arch/ambiq/tiku_wake_arch.c
SRCS += arch/ambiq/tiku_spi_arch.c
# EM9305 BLE radio over the IOM SPI master above (apollo510b): its SPI-HCI
# transport carries the host stack (tikukits/net/bluetooth).
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
SRCS += arch/ambiq/tiku_em9305.c
endif
SRCS += arch/ambiq/tiku_lcd_arch.c
# CryptoCell-312 TRNG (shared across apollo4l/4p/510) -- backs the cert-TLS
# handshake RNG (TIKU_KITS_CRYPTO_TLS_RNG_FILL).
SRCS += arch/ambiq/tiku_trng_arch.c
ifneq (,$(filter apollo4l apollo4p,$(MCU)))
# Apollo4 Lite and Plus (Cortex-M4F) device and CPU backends.  The kernel
# tick runs from the always-on STIMER, as on Apollo510: SysTick stops in WFI
# sleep.
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
# Apollo510 and 510B (Cortex-M55) device and CPU backends.
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
# Power-measurement instruments (tiku_power_ambiq.c; the nRF counterpart is
# arch/nordic/tiku_power_arch.c), Apollo510 only: they use the M55's L1
# caches and the apollo510.h register map, which Apollo4 lacks.
# TIKU_AMBIQ_POWER_PROBE is a -D capability macro, so the shell command gates
# on it whatever the include order (kernel/shell/tiku_shell_config.h).
# TIKU_AMBIQ_POWER_PROBE=0 leaves the instruments out, with the read-target
# arrays they place in the code window.
TIKU_AMBIQ_POWER_PROBE ?= 1
ifeq ($(TIKU_AMBIQ_POWER_PROBE),1)
SRCS += arch/ambiq/tiku_power_ambiq.c
CFLAGS += -DTIKU_AMBIQ_POWER_PROBE=1
endif
# TIKU_AMBIQ_POWER_PROBE_SIMD=1: Helium-versus-scalar energy measurement.
# tiku_simd_scalar.c compiles hal/tiku_simd.c a second time with the vector
# backend off and its symbols renamed, so one image holds both backends.
ifeq ($(TIKU_AMBIQ_POWER_PROBE_SIMD),1)
SRCS += arch/ambiq/tiku_simd_power.c
SRCS += arch/ambiq/tiku_simd_scalar.c
CFLAGS += -DTIKU_AMBIQ_POWER_PROBE_SIMD=1
endif
SRCS += arch/ambiq/tiku_crit_arch.c
SRCS += arch/ambiq/tiku_gpio_irq_arch.c
SRCS += arch/ambiq/tiku_uart_arch.c
SRCS += arch/ambiq/tiku_mem_arch.c
SRCS += arch/ambiq/tiku_mpu_arch.c
SRCS += arch/ambiq/tiku_region_arch.c
SRCS += arch/ambiq/tiku_nvm_region_apollo510.c
SRCS += arch/ambiq/tiku_gpio_arch.c
# Board-fitted parts.  Each driver below defines its TIKU_DRV_*_ENABLE as a
# -D capability macro, so its shell command and VFS nodes gate on it whatever
# the include order (kernel/shell/tiku_shell_config.h).  The capability
# refusals near the top have already checked the board.
#
# USB device controller, with the mass-storage class and /sys/usb.
ifeq ($(TIKU_DRV_USB_ENABLE),1)
SRCS += arch/ambiq/tiku_usb_arch.c
SRCS += kernel/usb/tiku_usbd_msc.c              # BOT + SCSI (host-tested)
SRCS += kernel/vfs/tree/tiku_vfs_tree_usb.c     # /sys/usb (no /sys/store here)
CFLAGS += -DTIKU_DRV_USB_ENABLE=1
endif
# On-board eMMC on SDIO0 (EVB U11, 8 GB), on both Apollo510 EVBs, with the
# FAT32 reader and the `fat` shell command.
ifeq ($(TIKU_DRV_EMMC_ENABLE),1)
SRCS += arch/ambiq/tiku_emmc_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_emmc.c    # /sys/emmc lifecycle nodes
SRCS += kernel/fs/tiku_fat.c                    # FAT32 reader (host-tested)
SRCS += kernel/shell/commands/tiku_shell_cmd_fat.c
CFLAGS += -DTIKU_DRV_EMMC_ENABLE=1
endif
# External octal NOR flash on MSPI1 (EVB U12, 8 MB), fitted on the green
# Apollo510 EVB only; the Blue board has no MSPI1 chip select for it.
ifeq ($(TIKU_DRV_NOR_ENABLE),1)
SRCS += arch/ambiq/tiku_nor_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_flash.c   # /sys/flash status nodes
CFLAGS += -DTIKU_DRV_NOR_ENABLE=1
endif
# External octal-DDR PSRAM on MSPI0 (EVB U14, 64 MB), on both Apollo510 EVBs.
ifeq ($(TIKU_DRV_PSRAM_ENABLE),1)
SRCS += arch/ambiq/tiku_psram_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_psram.c   # /sys/psram lifecycle nodes
CFLAGS += -DTIKU_DRV_PSRAM_ENABLE=1
endif
# Overlay sources, appended after the platform blocks have assigned SRCS and
# CFLAGS, so the overlay's additions survive.  Empty unless a feature was
# opted in (see the overlay include above).  Only Apollo510 and 510B builds
# reach these lines.
SRCS   += $(EXP_SRCS)
CFLAGS += $(EXP_CFLAGS)

ifeq ($(TIKU_DRV_GPU_ENABLE),1)
SRCS += arch/ambiq/tiku_gpu_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_gpu.c   # /sys/gpu status nodes
# TIKU_AMBIQ_POWER_PROBE_GPU=1: GPU power-measurement instruments.  They need
# the GPU driver, so they sit inside this block; their -D lets the shell gate
# on them whatever the include order.
ifeq ($(TIKU_AMBIQ_POWER_PROBE_GPU),1)
SRCS += arch/ambiq/tiku_gpu_power.c
CFLAGS += -DTIKU_AMBIQ_POWER_PROBE_GPU=1
endif
endif
ifeq ($(TIKU_DRV_DC_ENABLE),1)
SRCS += arch/ambiq/tiku_dc_arch.c
SRCS += arch/ambiq/tiku_display_arch.c       # interfaces/display backend
SRCS += interfaces/display/tiku_display.c    # portable damage tracking
CFLAGS += -DTIKU_HAS_DISPLAY=1
endif
endif

else ifeq ($(TIKU_PLATFORM),stm32n6)

# STM32N6 port sources.  A kernel interface whose arch backend is not listed
# here has no definition on this port: a build that calls it fails to link,
# naming the missing symbol.
SRCS += arch/stm32n6/tiku_crt_early.c
SRCS += arch/stm32n6/tiku_cpu_common.c
SRCS += arch/stm32n6/tiku_cpu_freq_boot_arch.c
SRCS += arch/stm32n6/tiku_uart_arch.c
SRCS += arch/stm32n6/tiku_gpio_arch.c
SRCS += arch/stm32n6/tiku_crit_arch.c
SRCS += arch/stm32n6/tiku_timer_arch.c
SRCS += arch/stm32n6/tiku_mem_arch.c
SRCS += arch/stm32n6/tiku_mpu_arch.c
SRCS += arch/stm32n6/tiku_cpu_watchdog_arch.c
SRCS += arch/stm32n6/tiku_region_arch.c
SRCS += arch/stm32n6/tiku_wake_arch.c
SRCS += arch/stm32n6/tiku_htimer_arch.c
SRCS += arch/stm32n6/tiku_gpio_irq_arch.c
SRCS += arch/stm32n6/tiku_trng_arch.c
SRCS += arch/stm32n6/tiku_lcd_arch.c
SRCS += arch/stm32n6/tiku_dma_arch.c
SRCS += arch/stm32n6/tiku_pwm_arch.c
SRCS += arch/stm32n6/tiku_xspi_arch.c
SRCS += arch/stm32n6/tiku_sram_arch.c
SRCS += arch/stm32n6/tiku_cache_arch.c
SRCS += arch/stm32n6/tiku_fault_arch.c
SRCS += arch/stm32n6/tiku_nvm_region_stm32n6.c
# TIKU_N6_NVM_DEBUG=1 prints the NVM region driver's trace on the UART.
ifeq ($(TIKU_N6_NVM_DEBUG),1)
CFLAGS += -DTIKU_N6_NVM_DEBUG=1
endif
ifeq ($(TIKU_N6_SRAM_PROBE),1)
# TIKU_N6_SRAM_PROBE=1: at boot, a destructive walk of every SRAM bank, one
# write and read-back per 64 KB, each result printed on the console.  Off
# unless set.
CFLAGS += -DTIKU_N6_SRAM_PROBE=1
endif
ifeq ($(TIKU_N6_OTP_TOOL),1)
# TIKU_N6_OTP_TOOL=1 adds the `xflash otpburn` subcommand (tiku_otp_tool.c),
# which programs the VDDIO3_HSLV fuse.  An OTP bit cannot be cleared; off
# unless set.
SRCS += arch/stm32n6/tiku_otp_tool.c
CFLAGS += -DTIKU_N6_OTP_TOOL=1
endif
SRCS += kernel/shell/commands/tiku_shell_cmd_xflash.c
SRCS += kernel/shell/commands/tiku_shell_cmd_cache.c
SRCS += kernel/shell/commands/tiku_shell_cmd_diag.c
# Stubs: this port has no ADC, I2C, SPI or 1-Wire driver.  What each
# tiku_<bus>_arch_* call does:
#   adc      init, channel_init and read return TIKU_ADC_ERR_PARAM; read
#            also stores 0 in *value
#   i2c      init, read, write, write_read and probe return
#            TIKU_I2C_ERR_PARAM
#   spi      init, read, write and write_read return TIKU_SPI_ERR_PARAM;
#            transfer returns 0xFF
#   onewire  init returns TIKU_OW_ERR_PARAM, reset TIKU_OW_ERR_NO_DEVICE,
#            read_bit 1 and read_byte 0xFF; the writes do nothing
# close does nothing on all four.
SRCS += arch/stm32n6/tiku_adc_arch.c
SRCS += arch/stm32n6/tiku_i2c_arch.c
SRCS += arch/stm32n6/tiku_spi_arch.c
SRCS += arch/stm32n6/tiku_onewire_arch.c

else ifeq ($(TIKU_PLATFORM),ra8p1)

# RA8P1 port sources.  A kernel interface whose arch backend is not listed
# here has no definition on this port: a build that calls it fails to link,
# naming the missing symbol.
SRCS += arch/ra8p1/tiku_crt_early.c
SRCS += arch/ra8p1/tiku_cpu_common.c
SRCS += arch/ra8p1/tiku_cpu_freq_boot_arch.c
SRCS += arch/ra8p1/tiku_uart_arch.c
SRCS += arch/ra8p1/tiku_trng_arch.c
SRCS += arch/ra8p1/tiku_gpio_arch.c
SRCS += arch/ra8p1/tiku_crit_arch.c
SRCS += arch/ra8p1/tiku_timer_arch.c
SRCS += arch/ra8p1/tiku_cpu_watchdog_arch.c
ifeq ($(TIKU_THREADS_ENABLE),1)
# Worker threads use the generic Cortex-M switcher.  tiku_thread_arch.c
# defines tiku_ra8p1_pendsv_handler, replacing the weak alias in
# tiku_crt_early.c.
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/ra8p1/tiku_thread_arch.c
endif
SRCS += arch/ra8p1/tiku_mem_arch.c
SRCS += arch/ra8p1/tiku_cache_arch.c
SRCS += arch/ra8p1/tiku_mram_arch.c
SRCS += arch/ra8p1/tiku_nvm_region_ra8p1.c
SRCS += arch/ra8p1/tiku_fault_arch.c
SRCS += arch/ra8p1/tiku_mpu_arch.c
SRCS += arch/ra8p1/tiku_dma_arch.c
SRCS += arch/ra8p1/tiku_region_arch.c
SRCS += arch/ra8p1/tiku_sdram_arch.c
SRCS += arch/ra8p1/tiku_xflash_arch.c
SRCS += arch/ra8p1/tiku_i2c_arch.c               # IIC1: camera + touch bus

# TIKU_DRV_CAM_ENABLE=1 compiles the OV5640 camera driver (sensor registers
# over the IIC1 bus above), the MIPI CSI-2 capture into memory
# (tiku_vin_arch.c) and, with the shell, the cam command.
ifeq ($(TIKU_DRV_CAM_ENABLE),1)
SRCS += arch/ra8p1/tiku_camera_arch.c
SRCS += arch/ra8p1/tiku_vin_arch.c
CFLAGS += -DTIKU_HAS_CAM=1
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_cam.c
endif
endif

# TIKU_DRV_DRW_ENABLE=1 compiles the 2D drawing engine driver.  It renders
# into memory and needs no panel; the display backend below depends on it.
ifeq ($(TIKU_DRV_DRW_ENABLE),1)
SRCS += arch/ra8p1/tiku_drw_arch.c
CFLAGS += -DTIKU_HAS_DRW=1
endif

ifeq ($(TIKU_DRV_GLCDC_ENABLE),1)
SRCS += arch/ra8p1/tiku_glcdc_arch.c
CFLAGS += -DTIKU_HAS_GLCDC=1
# The interfaces/display backend draws with the 2D engine and the GLCDC
# scans the framebuffer out, so it is compiled only when TIKU_DRV_DRW_ENABLE
# is 1 as well.
ifeq ($(TIKU_DRV_DRW_ENABLE),1)
SRCS += arch/ra8p1/tiku_display_arch.c
SRCS += interfaces/display/tiku_display.c
CFLAGS += -DTIKU_HAS_DISPLAY=1
endif
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_panel.c
endif
endif

# TIKU_NPU_ENABLE=1 compiles the Ethos-U55 NPU driver, its interfaces/npu
# backend, /sys/npu and, with the shell, the npu command.  The driver's
# arena, command and weight buffers are static .bss, sized by
# TIKU_NPU_ARENA_MAX, TIKU_NPU_CMS_MAX and TIKU_NPU_WTS_MAX.
ifeq ($(TIKU_NPU_ENABLE),1)
SRCS += arch/ra8p1/tiku_npu_arch.c
SRCS += arch/ra8p1/tiku_npu_iface.c              # interfaces/npu backend
SRCS += kernel/vfs/tree/tiku_vfs_tree_npu.c      # /sys/npu
# Presence is a -D global so every translation unit resolves it identically.
CFLAGS += -DTIKU_HAS_NPU=1
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_npu.c
endif
endif
ifeq ($(TIKU_DRV_USBHS_ENABLE),1)
SRCS += arch/ra8p1/tiku_usbhs_arch.c
SRCS += arch/ra8p1/tiku_store_arch.c            # staged-over-USB model store
SRCS += kernel/usb/tiku_usbd_msc.c              # BOT + SCSI (host-tested)
SRCS += kernel/fs/tiku_bigblob.c                # model-sized objects on flash
SRCS += kernel/vfs/tree/tiku_vfs_tree_usb.c     # /sys/usb + /sys/store
SRCS += kernel/shell/commands/tiku_shell_cmd_usbhs.c
CFLAGS += -DTIKU_DRV_USBHS_ENABLE=1
endif
ifeq ($(TIKU_DRV_CPU1_ENABLE),1)
SRCS += arch/ra8p1/tiku_cpu1_arch.c              # Cortex-M33 lifecycle
SRCS += kernel/shell/commands/tiku_shell_cmd_cpu1.c
SRCS += arch/ra8p1/tiku_coproc_arch.c            # interfaces/coproc backend
SRCS += kernel/vfs/tree/tiku_vfs_tree_coproc.c   # /sys/coproc
SRCS += arch/ra8p1/cpu1/tiku_cpu1_sha256.c       # A/B kernel, M85 side
SRCS += tikukits/crypto/p256/tiku_kits_crypto_p256.c  # verify baseline
CFLAGS += -DTIKU_DRV_CPU1_ENABLE=1
# Presence and capacity are -D globals so every translation unit resolves
# them the same way; the backend asserts the cap against its own mailbox.
CFLAGS += -DTIKU_HAS_COPROC=1 -DTIKU_COPROC_MSG_CAP=192u
# The Cortex-M33 payload is a separate link with the same toolchain and its
# own flags.  -mfloat-abi=soft: CPACR is zero out of reset, so the FPU is off
# and an FP instruction faults.  -Os: code and .bss must end below offset
# 0x1800, where the HardFault handler sits, and tiku_cpu1.ld asserts it.
CPU1_BUILD    = $(BUILD_DIR)/cpu1
CPU1_CFLAGS   = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -Wall -Wextra \
                -Werror -ffreestanding -fno-builtin -fno-common -nostdlib \
                -nostartfiles -ffunction-sections -fdata-sections -MMD -MP \
                -I$(PROJ_DIR)
CPU1_OBJS     = $(CPU1_BUILD)/tiku_cpu1_payload.o \
                $(CPU1_BUILD)/tiku_cpu1_sha256.o \
                $(CPU1_BUILD)/tiku_cpu1_libc.o \
                $(CPU1_BUILD)/tiku_kits_crypto_p256.o
TIKU_CPU1_IMG_O = $(CPU1_BUILD)/tiku_cpu1_img.o
endif
SRCS += arch/ra8p1/tiku_wake_arch.c
SRCS += arch/ra8p1/tiku_htimer_arch.c
SRCS += arch/ra8p1/tiku_gpio_irq_arch.c
# tiku_i2c_arch.c is the IIC1 driver, listed a second time here; sorting
# SRCS before OBJS drops the repeat.  The other three are stubs: this port
# has no ADC, SPI or 1-Wire driver.  What each tiku_<bus>_arch_* call does:
#   adc      init, channel_init and read return TIKU_ADC_ERR_PARAM; read
#            also stores 0 in *value
#   spi      init, read, write and write_read return TIKU_SPI_ERR_PARAM;
#            transfer returns 0xFF
#   onewire  init returns TIKU_OW_ERR_PARAM, reset TIKU_OW_ERR_NO_DEVICE,
#            read_bit 1 and read_byte 0xFF; the writes do nothing
# close does nothing on all three.
SRCS += arch/ra8p1/tiku_adc_arch.c
SRCS += arch/ra8p1/tiku_i2c_arch.c
SRCS += arch/ra8p1/tiku_spi_arch.c
SRCS += arch/ra8p1/tiku_onewire_arch.c
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_diag.c
SRCS += kernel/shell/commands/tiku_shell_cmd_sdram.c
endif

else ifeq ($(TIKU_PLATFORM),esp32c61)

# ESP32-C61 port sources.  A kernel interface whose arch backend is not
# listed here has no definition on this port: a build that calls it fails to
# link, naming the missing symbol.
SRCS += arch/esp32c61/tiku_crt_early.c
SRCS += arch/esp32c61/tiku_cpu_freq_boot_arch.c
SRCS += arch/esp32c61/tiku_cpu_common.c
SRCS += arch/esp32c61/tiku_uart_arch.c
SRCS += arch/esp32c61/tiku_gpio_arch.c
SRCS += arch/esp32c61/tiku_irq_arch.c
SRCS += arch/esp32c61/tiku_timer_arch.c
SRCS += arch/esp32c61/tiku_htimer_arch.c
SRCS += arch/esp32c61/tiku_crit_arch.c
SRCS += arch/esp32c61/tiku_wake_arch.c
SRCS += arch/esp32c61/tiku_cpu_watchdog_arch.c
SRCS += arch/esp32c61/tiku_mem_arch.c
SRCS += arch/esp32c61/tiku_mpu_arch.c
SRCS += arch/esp32c61/tiku_region_arch.c
SRCS += arch/esp32c61/tiku_gpio_irq_arch.c
# Stubs: this port has no ADC, I2C, SPI or 1-Wire driver.  What each
# tiku_<bus>_arch_* call does:
#   adc      init, channel_init and read return TIKU_ADC_ERR_PARAM; read
#            also stores 0 in *value
#   i2c      init, read, write, write_read and probe return
#            TIKU_I2C_ERR_PARAM
#   spi      init, read, write and write_read return TIKU_SPI_ERR_PARAM;
#            transfer returns 0xFF
#   onewire  init returns TIKU_OW_ERR_PARAM, reset TIKU_OW_ERR_NO_DEVICE,
#            read_bit 1 and read_byte 0xFF; the writes do nothing
# close does nothing on all four.
SRCS += arch/esp32c61/tiku_adc_arch.c
SRCS += arch/esp32c61/tiku_i2c_arch.c
SRCS += arch/esp32c61/tiku_spi_arch.c
SRCS += arch/esp32c61/tiku_onewire_arch.c
SRCS += arch/esp32c61/tiku_flash_arch.c
SRCS += arch/esp32c61/tiku_nvm_region_esp32c61.c
SRCS += arch/esp32c61/tiku_trng_arch.c
SRCS += arch/esp32c61/tiku_fault_arch.c
SRCS += arch/esp32c61/tiku_sleep_arch.c
SRCS += arch/esp32c61/tiku_psram_arch.c
SRCS += arch/esp32c61/tiku_dma_arch.c
SRCS += arch/esp32c61/tiku_xip_arch.c
# TIKU_ESP32C61_XIP_CODE=1 runs the code and constants of BASIC, the shell
# commands, crypto, TLS, the IPv4, HTTP, MQTT and Wi-Fi kits and the BLE host
# from flash through the XIP window (xip_code.ld).
# TIKU_ESP32C61_PSRAM_DATA=1 puts the TLS and crypto .bss and BASIC's large
# buffers in PSRAM (psram_data.ld).  Both free SRAM for large profiles such
# as HTTPS, and both are off unless set.
ifeq ($(TIKU_ESP32C61_XIP_CODE),1)
TIKU_XIP_LDS += arch/esp32c61/xip_code.ld
endif
ifeq ($(TIKU_ESP32C61_PSRAM_DATA),1)
TIKU_PSRAM_LDS += arch/esp32c61/psram_data.ld
endif
ifeq ($(TIKU_THREADS_ENABLE),1)
SRCS += kernel/threads/tiku_thread.c
SRCS += arch/esp32c61/tiku_thread_arch.c
endif
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_diag.c
endif

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
SRCS += kernel/cpu/tiku_cpu_settings.c
# Portable u8 vector kernels: Helium (MVE) code when the -mcpu has it
# (Cortex-M55, Cortex-M85), a bit-identical scalar path elsewhere.
# --gc-sections drops the kernels nothing calls.
SRCS += hal/tiku_simd.c
SRCS += kernel/cpu/tiku_common.c
SRCS += kernel/cpu/tiku_watchdog.c
SRCS += kernel/cpu/tiku_hang.c
SRCS += kernel/cpu/tiku_stack.c
SRCS += kernel/cpu/tiku_rtc.c
SRCS += kernel/cpu/tiku_bench.c

# Driver registry: always built, so the kernel has tiku_drv_init_all().  The
# descriptor table comes from the drivers/ repository when it is present
# (HAS_DRIVERS=1), else from the empty table below.  See drivers.md.
SRCS += kernel/drivers/tiku_drv_registry.c
ifeq ($(HAS_DRIVERS),1)
SRCS    += drivers/tiku_drv_table.c
# Each driver adds itself through its own build.mk.  The two wildcards take
# drivers/*/*/build.mk and drivers/*/*/*/build.mk, for example
# drivers/wifi/cyw43/build.mk.
include $(wildcard $(PROJ_DIR)/drivers/*/*/build.mk)
include $(wildcard $(PROJ_DIR)/drivers/*/*/*/build.mk)
else
SRCS    += kernel/drivers/tiku_drv_empty_table.c
endif
SRCS += kernel/timers/tiku_clock.c
SRCS += kernel/timers/tiku_htimer.c
SRCS += kernel/timers/tiku_timer.c
SRCS += kernel/timers/tiku_crit.c

# TIKU_BITBANG_ENABLE=1 compiles the bit-bang transmitter (backscatter,
# software UART, IR).  Default 0; TIKU_EXAMPLE_BITBANG=1 and
# TIKU_EXAMPLE_CRIT_DEFER=1, the examples that use it, turn it on.
TIKU_BITBANG_ENABLE ?= 0
ifeq ($(TIKU_EXAMPLE_BITBANG),1)
override TIKU_BITBANG_ENABLE := 1
endif
ifeq ($(TIKU_EXAMPLE_CRIT_DEFER),1)
override TIKU_BITBANG_ENABLE := 1
endif
# TEST_BITBANG=1 turns it on for the bit-bang C tests.
ifeq ($(TEST_BITBANG),1)
override TIKU_BITBANG_ENABLE := 1
endif
ifeq ($(TIKU_BITBANG_ENABLE),1)
CFLAGS += -DTIKU_BITBANG_ENABLE=1
SRCS += kernel/timers/tiku_bitbang.c
endif

# Apollo510 GPU (Think Silicon, Nema-class 2.5D): a register-level driver
# that links no vendor library.  Opt-in, apollo510 and apollo510b only; its
# SRCS entry is in the Ambiq arch block.  TEST_GPU=1 and TEST_GPU_COMPUTE=1
# turn it on for the GPU C tests.
TIKU_DRV_GPU_ENABLE ?= 0
ifeq ($(TEST_GPU),1)
override TIKU_DRV_GPU_ENABLE := 1
endif
ifeq ($(TEST_GPU_COMPUTE),1)
override TIKU_DRV_GPU_ENABLE := 1
endif
ifeq ($(TIKU_DRV_GPU_ENABLE),1)
ifeq ($(filter apollo510 apollo510b,$(MCU)),)
$(error TIKU_DRV_GPU_ENABLE=1 requires MCU=apollo510 or apollo510b (the GPU is \
Apollo510-only); currently MCU=$(MCU))
endif
CFLAGS += -DTIKU_DRV_GPU_ENABLE=1
endif

# Apollo510 display path (NemaDC, DSI host, CO5300 round AMOLED): a
# register-level driver that links no vendor library.  Opt-in; the panel kit
# is on the base apollo510 EVB.  TEST_GPU_DISPLAY=1 turns it on for the GPU
# display tests.
TIKU_DRV_DC_ENABLE ?= 0
ifeq ($(TEST_GPU_DISPLAY),1)
override TIKU_DRV_DC_ENABLE := 1
endif
ifeq ($(TIKU_DRV_DC_ENABLE),1)
ifeq ($(filter apollo510 apollo510b,$(MCU)),)
$(error TIKU_DRV_DC_ENABLE=1 requires MCU=apollo510 or apollo510b (NemaDC is \
Apollo510-only); currently MCU=$(MCU))
endif
CFLAGS += -DTIKU_DRV_DC_ENABLE=1
endif

SRCS += interfaces/led/tiku_led.c
SRCS += interfaces/gpio/tiku_gpio_owner.c
SRCS += interfaces/bus/tiku_i2c_bus.c
SRCS += interfaces/bus/tiku_spi_bus.c
ifeq ($(TIKU_PLATFORM),msp430)
SRCS += arch/msp430/tiku_spi_arch.c
endif
SRCS += interfaces/adc/tiku_adc.c
SRCS += interfaces/onewire/tiku_onewire.c
# Segment-LCD interface: tiku_lcd.c is always compiled, and its calls do
# nothing when TIKU_BOARD_HAS_LCD is 0.  The MSP430 driver below compiles to
# an empty translation unit unless the device has an LCD_C controller
# (TIKU_DEVICE_HAS_LCD_C) and the board a panel (TIKU_BOARD_HAS_LCD).  Other
# ports list their own tiku_lcd_arch.c in their arch block.
SRCS += interfaces/lcd/tiku_lcd.c
ifeq ($(TIKU_PLATFORM),msp430)
SRCS += arch/msp430/tiku_lcd_arch.c
endif
SRCS += kernel/memory/tiku_mem.c
SRCS += kernel/memory/tiku_pool.c
SRCS += kernel/memory/tiku_mpu.c
SRCS += kernel/memory/tiku_persist.c
SRCS += kernel/memory/tiku_persist_move.c
SRCS += kernel/memory/tiku_region.c
SRCS += kernel/memory/tiku_tier.c
# TIKU_MEM_RECLAIM_ENABLE=1 compiles tiku_reclaim.c, which rebuilds owned
# memory backing in bounded, cooperative steps, and, with the shell, the
# reclaim command.  Default 0; an MSP430 build stops with an error.
TIKU_MEM_RECLAIM_ENABLE ?= 0
ifeq ($(TIKU_MEM_RECLAIM_ENABLE),1)
ifeq ($(TIKU_PLATFORM),msp430)
$(error TIKU_MEM_RECLAIM_ENABLE is not qualified for MSP430; the ordinary reclaimable allocator remains available)
endif
CFLAGS += -DTIKU_MEM_RECLAIM_ENABLE=1
SRCS += kernel/memory/tiku_reclaim.c
endif
SRCS += kernel/memory/tiku_layout.c
SRCS += kernel/memory/tiku_noheap.c
SRCS += kernel/memory/tiku_nvm_region.c
SRCS += kernel/memory/tiku_cache.c
SRCS += kernel/memory/tiku_hibernate.c
SRCS += kernel/memory/tiku_proc_mem.c
SRCS += kernel/process/tiku_process.c
SRCS += kernel/process/tiku_proc_vfs.c
SRCS += kernel/process/tiku_lc_persist.c
SRCS += kernel/scheduler/tiku_sched.c
# The console line: one SLIP decoder dispatching whole frames by channel
# (the IP stack, a desktop's window session) beside the shell's text.
SRCS += kernel/console/tiku_console.c
# The link: whole messages over any medium.  tiku_link_console.c is its
# console backend, on one marked console channel.  A build with no link user
# references neither file, and --gc-sections drops both.
SRCS += kernel/link/tiku_link.c
SRCS += kernel/link/tiku_link_console.c
# TIKU_LINK_BLE_ENABLE=1 compiles the BLE link: a board's window session over
# the BLE serial facade, the Nordic UART Service byte pipe.  Only the
# applications overlay registers it (TIKU_APPL_GUI_BLE).  The facade needs a
# radio: the host stack's (TIKU_BT_HOST, the EM9305 by default on the
# Apollo510B) or TIKU_FLPR_ENABLE=1 on Nordic; without one the build stops
# with an error.
ifeq ($(TIKU_LINK_BLE_ENABLE),1)
ifeq ($(filter 1,$(TIKU_FLPR_ENABLE) $(TIKU_BT_HOST)),)
$(error TIKU_LINK_BLE_ENABLE=1 needs the BLE serial facade under it; on \
nordic add TIKU_FLPR_ENABLE=1)
endif
CFLAGS += -DTIKU_LINK_BLE_ENABLE=1
SRCS   += kernel/link/tiku_link_ble.c
endif
SRCS += kernel/vfs/tiku_vfs.c
# TIKU_VFS_CONFIG=1 compiles the two-bank configuration journal behind
# /sys/config and sets the shell line to 256 bytes.  The banks are durable
# data written in place on MSP430 (FRAM) and Nordic (RRAM); on other ports
# /sys/config/status reads "v1:unsupported:-".  Default 0.
TIKU_VFS_CONFIG ?= 0
ifeq ($(TIKU_VFS_CONFIG),1)
CFLAGS += -DTIKU_VFS_CONFIG_ENABLE=1 -DTIKU_SHELL_LINE_SIZE=256
SRCS += kernel/vfs/tiku_vfs_config.c
endif
SRCS += kernel/vfs/tiku_vfs_cache.c
SRCS += kernel/vfs/tiku_vfs_tree.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_sys.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_boot.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_timer.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_watchdog.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_power.c
SRCS += kernel/cpu/tiku_power_policy.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_net.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_wifi.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_persist.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_layout.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_watch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_dev.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_gpio.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_sensor.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_inittab.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_data.c

# Tiku File Store: the store behind /data (tiku_vfs_tree_data.c) and the NVM
# layout code (tiku_layout.c).
SRCS += kernel/fs/tiku_tfs.c
# tiku_blob.c keeps large objects (network weights, radio firmware, module
# images) as chunked files in the store, through TFS calls only.
# tiku_model.c validates a packed model file and patches its relocation
# sites.  --gc-sections drops the entry points nothing calls.
SRCS += kernel/fs/tiku_blob.c
SRCS += kernel/fs/tiku_model.c

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
SRCS += kernel/shell/commands/tiku_shell_cmd_console.c
SRCS += kernel/shell/commands/tiku_shell_cmd_timer.c
SRCS += kernel/shell/commands/tiku_shell_cmd_kill.c
SRCS += kernel/shell/commands/tiku_shell_cmd_resume.c
SRCS += kernel/shell/commands/tiku_shell_cmd_queue.c
SRCS += kernel/shell/commands/tiku_shell_cmd_reboot.c
SRCS += kernel/shell/commands/tiku_shell_cmd_trng.c
# The mrambench command times the Ambiq boot-ROM MRAM programmer and is
# compiled on Ambiq only; tiku_shell_config.h drops its table entry
# elsewhere.
ifeq ($(TIKU_PLATFORM),ambiq)
SRCS += kernel/shell/commands/tiku_shell_cmd_mrambench.c
endif
# The ble command (EM9305 probe, beacon and a shell over BLE) is compiled
# only with the EM9305 driver (apollo510b).  The driver and its -D flags are
# set in the BLE block next to the Ambiq part selectors.
ifeq ($(TIKU_DRV_BLE_EM9305_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_ble.c
endif
ifeq (,$(findstring TIKU_SHELL_CMD_HISTORY=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_history.c
endif
# The wifi command is compiled only with a Wi-Fi driver (CYW43439 or the
# ESP32-C61's own); tiku_shell_config.h drops its table entry otherwise.
ifneq ($(filter 1,$(TIKU_DRV_WIFI_CYW43_ENABLE) $(TIKU_DRV_WIFI_ESP_ENABLE)),)
SRCS += kernel/shell/commands/tiku_shell_cmd_wifi.c
endif
# The bt command drives the BLE host stack, so it builds with any controller
# under it (TIKU_BT_HOST, above).  bt bonds prints a SHA-256 fingerprint of
# each key; SRCS is de-duplicated, so a build that also lists the kit's
# SHA-256 compiles it once.
ifeq ($(TIKU_BT_HOST),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_bt.c
SRCS += tikukits/crypto/sha256/tiku_kits_crypto_sha256.c
endif
ifeq ($(TIKU_DRV_SDR_ESP_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_sdr.c
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
# The slip, ping and ip commands are compiled only with the net kit.  The
# shell stays interactive until `slip` starts the net process and SLIP/IP.
ifeq ($(TIKU_KIT_NET_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_slip.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ping.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ip.c
# ntp command (SNTP client): on by default with net.  It needs the time kit,
# so it sets TIKU_KIT_TIME_ENABLE (see the time-kit block below).
# EXTRA_CFLAGS="-DTIKU_SHELL_CMD_NTP=0" leaves out both.
ifeq (,$(findstring TIKU_SHELL_CMD_NTP=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_ntp.c
TIKU_KIT_TIME_ENABLE := 1
endif
# dns command (A-record lookup): on by default with net.  It and the ntp
# command call the DNS stub resolver, which the full net kit compiles with
# ipv4/; a TIKU_KIT_NET_MIN build has it only with TIKU_KITS_NET_DNS_ENABLE=1.
ifeq (,$(findstring TIKU_SHELL_CMD_DNS=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_dns.c
endif
# syslog command (RFC 3164 remote log): on by default with net.  The full
# net kit compiles the syslog client with ipv4/; in a TIKU_KIT_NET_MIN build
# tiku_shell_config.h drops the command.
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
SRCS += kernel/shell/basic/tiku_basic_module.c   # module loader (self-gates)
SRCS += kernel/shell/commands/tiku_shell_cmd_basic.c
endif

# Loadable native module: kernel/shell/basic/modules/mod_demo.c is compiled
# and linked on its own at the module slot's address, flattened to a binary
# and embedded in the firmware.  tiku_basic_module.c installs a module from
# its /data file, and writes that file from the embedded image when it is
# missing.  The slot address, install method and CPU per device:
#   nrf54lm20a/b + nrf54l15: RRAM slot 0x58000, Cortex-M33 (byte-writable
#                         XIP); one address for the whole Nordic family
#   apollo510/apollo510b: no slot; the image is copied from /data into an
#                         ITCM window at 0x1000 at every activate (Cortex-M55)
#   apollo4l/apollo4p:    MRAM slot 0x70000,   Cortex-M4  (bootrom-programmed)
#   rp2350:               flash slot 0x10058000, Cortex-M33 (boot-ROM sectors)
#   msp430fr5994/fr6989:  FRAM slot 0x43000/0x23000 (HIFRAM top, MPU-unlocked)
#   esp32c61:             no slot; the image is copied from /data into a PSRAM
#                         window at 0x42800000 at every activate (RISC-V)
# An ARM slot is the top 32 KB of the 384 KB code window (order: code |
# module | region | persist); the MSP430 slot is the 4 KB at the top of
# HIFRAM.  Any other MCU has no module slot in its linker script, and the
# build stops with an error.
ifeq ($(TIKU_BASIC_MODULE_ENABLE),1)
CFLAGS        += -DTIKU_BASIC_MODULE_ENABLE=1
MOD_BUILD      = $(BUILD_DIR)/module
# Object format of the wrapped image.  It links into the firmware, so it
# must match the firmware's object format.
MOD_WRAP_OUT   = elf32-littlearm
MOD_WRAP_ARCH  = arm
# MOD_EMBED=objcopy wraps the binary as an ELF object.  msp430-elf-ld
# rejects such an object (no .MSP430.attributes: "unknown code model"), so
# MSP430 and the ESP32-C61 set MOD_EMBED=carray: tools/mod_embed.py turns
# the image into a C array compiled with the firmware's own CFLAGS.
MOD_EMBED      = objcopy
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
MOD_CPU_FLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
MOD_LDS        = kernel/shell/basic/modules/mod_demo.ld
else ifeq ($(MCU),nrf54l15)
# Same slot VMA as the LM20 (shared 384 KB Nordic code window), so the LM20
# module script serves both parts and a module image is family-portable.
MOD_CPU_FLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -DTIKU_DEVICE_NRF54L15
MOD_LDS        = kernel/shell/basic/modules/mod_demo.ld
else ifeq ($(MCU),rp2350)
MOD_CPU_FLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -DPLATFORM_RP2350
MOD_LDS        = kernel/shell/basic/modules/mod_demo_rp2350.ld
else ifneq (,$(filter apollo510 apollo510b,$(MCU)))
MOD_CPU_FLAGS  = -mcpu=cortex-m55 -mthumb -mfloat-abi=soft -DAM_PART_APOLLO510
MOD_LDS        = kernel/shell/basic/modules/mod_demo_apollo510.ld
else ifneq (,$(filter apollo4l apollo4p,$(MCU)))
MOD_CPU_FLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -DAM_PART_APOLLO4L
MOD_LDS        = kernel/shell/basic/modules/mod_demo_apollo4l.ld
else ifeq ($(MCU),msp430fr5994)
# MSP430 module: same compiler/memory-model as the firmware (-mlarge, CALLA
# calling convention through the syscall table); no Thumb, no ARM wrap.
MOD_CPU_FLAGS  = -mmcu=msp430fr5994 -mlarge
MOD_LDS        = kernel/shell/basic/modules/mod_demo_msp430fr5994.ld
MOD_LDFLAGS    =
MOD_EMBED      = carray
else ifeq ($(MCU),msp430fr6989)
MOD_CPU_FLAGS  = -mmcu=msp430fr6989 -mlarge
MOD_LDS        = kernel/shell/basic/modules/mod_demo_msp430fr6989.ld
MOD_LDFLAGS    =
MOD_EMBED      = carray
else ifeq ($(MCU),esp32c61)
# RISC-V, run from a PSRAM window.  No small-data section: the module cannot
# use the firmware's gp, so every global it touches is addressed in full.
MOD_CPU_FLAGS  = -march=rv32imac_zicsr_zifencei -mabi=ilp32 \
                 -msmall-data-limit=0 -DPLATFORM_ESP32C61
MOD_LDS        = kernel/shell/basic/modules/mod_demo_esp32c61.ld
MOD_EMBED      = carray
else
$(error TIKU_BASIC_MODULE_ENABLE=1: no module slot for MCU=$(MCU) \
        (supported: nrf54lm20a nrf54lm20b nrf54l15 rp2350 apollo510 \
        apollo510b apollo4l apollo4p msp430fr5994 msp430fr6989 esp32c61))
endif
# The slot is the top 32 KB of the code window, reserved at link time only
# when this loader is in the build.  apollo510 executes modules from the
# ITCM, the ESP32-C61 from its PSRAM, and MSP430 keeps its own HIFRAM scheme,
# so none of them reserves anything.
ifeq (,$(filter apollo510 apollo510b msp430fr5994 msp430fr6989 esp32c61,$(MCU)))
LDFLAGS += -Wl,--defsym=__tiku_module_reserve=0x8000
endif
MOD_CFLAGS     = $(MOD_CPU_FLAGS) -Os -ffreestanding \
                 -fno-builtin -fno-jump-tables -DTIKU_MODULE_BUILD=1 \
                 -I kernel/shell/basic
# Modules link with -nostdlib.  MSP430 sets MOD_LDFLAGS empty above and keeps
# the toolchain's libraries, so the hardware-multiply helper library
# (libmul_f5) resolves; the link rule's -nostartfiles still leaves out crt0.
MOD_LDFLAGS   ?= -nostdlib
TIKU_MOD_IMG_O = $(MOD_BUILD)/mod_demo_img.o
endif

# Embedded BASIC: tools/bas_to_c.py turns BASIC_PROGRAM=foo.bas into a C
# string literal in the firmware, and main.c runs it at boot when
# TIKU_BASIC_EMBEDDED is set.
#
# The generated .c lives inside $(BUILD_DIR), where the pattern rule
# `$(BUILD_DIR)/%.o: %.c` cannot find its source, so it has its own
# generate and compile rules, and its object is appended to OBJS directly
# after OBJS is derived from SRCS.
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
SRCS += kernel/shell/commands/tiku_shell_cmd_layout.c
SRCS += kernel/shell/commands/tiku_shell_cmd_sleep.c
SRCS += kernel/shell/commands/tiku_shell_cmd_wake.c
SRCS += kernel/shell/commands/tiku_shell_cmd_freq.c
SRCS += kernel/shell/commands/tiku_shell_cmd_power.c
# The emmc, psram, usb and nor commands compile to empty translation units
# unless their driver flag is set (TIKU_DRV_EMMC_ENABLE,
# TIKU_DRV_PSRAM_ENABLE, TIKU_DRV_USB_ENABLE, TIKU_DRV_NOR_ENABLE), so they
# are listed unconditionally.
SRCS += kernel/shell/commands/tiku_shell_cmd_emmc.c
SRCS += kernel/shell/commands/tiku_shell_cmd_psram.c
SRCS += kernel/shell/commands/tiku_shell_cmd_usb.c
SRCS += kernel/shell/commands/tiku_shell_cmd_nor.c
SRCS += kernel/shell/commands/tiku_shell_cmd_name.c
ifeq (,$(findstring TIKU_SHELL_CMD_IF=0,$(EXTRA_CFLAGS)))
SRCS += kernel/shell/commands/tiku_shell_cmd_if.c
endif
SRCS += kernel/shell/commands/tiku_shell_cmd_irq.c
SRCS += kernel/shell/commands/tiku_shell_cmd_tree.c
SRCS += kernel/shell/commands/tiku_shell_cmd_clear.c
SRCS += kernel/shell/commands/tiku_shell_cmd_echo.c
ifeq ($(TIKU_MEM_RECLAIM_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_reclaim.c
endif
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
ifeq ($(TIKU_CRACEN_PK_ENABLE),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_cryptoprobe.c
else
$(warning cryptoprobe: needs TIKU_CRACEN_PK_ENABLE=1 (CRACEN PK) -- skipped)
endif
endif
# TIKU_USBHS_MSC=1: USB mass storage on the nRF54LM20A/B DWC2 core.  It
# cannot be built with the USB CDC console (TIKU_CONSOLE=usb or both): each
# defines the USB device interrupt handler.
ifeq ($(TIKU_USBHS_MSC),1)
ifeq ($(filter nrf54lm20a nrf54lm20b,$(MCU)),)
$(error TIKU_USBHS_MSC=1 needs MCU=nrf54lm20a or nrf54lm20b)
endif
ifneq ($(filter usb both,$(TIKU_CONSOLE)),)
$(error TIKU_USBHS_MSC=1 and TIKU_CONSOLE=$(TIKU_CONSOLE) both claim the USB device; build mass storage with TIKU_CONSOLE=uart)
endif
SRCS   += arch/nordic/tiku_usbhs_arch.c
SRCS   += arch/nordic/tiku_usbhs_msc.c
SRCS   += kernel/usb/tiku_usbd_msc.c
SRCS   += kernel/usb/tiku_usbd_ctrl.c
SRCS   += kernel/shell/commands/tiku_shell_cmd_usbmsc.c
CFLAGS += -DTIKU_USBHS_MSC=1
endif

# The usbprobe command (-DTIKU_SHELL_CMD_USBPROBE=1 in EXTRA_CFLAGS,
# nRF54LM20A/B only) powers the USB block and reports what the DWC2 core
# says about itself.  It adds the USB driver unless the USB console already
# compiles it.
ifneq (,$(findstring TIKU_SHELL_CMD_USBPROBE=1,$(EXTRA_CFLAGS)))
ifneq (,$(filter nrf54lm20a nrf54lm20b,$(MCU)))
SRCS += kernel/shell/commands/tiku_shell_cmd_usbprobe.c
ifeq ($(filter usb both,$(TIKU_CONSOLE)),)
SRCS += arch/nordic/tiku_usbhs_arch.c
SRCS += arch/nordic/tiku_usbhs_dev.c
SRCS += kernel/usb/tiku_usbd_ctrl.c
endif
else
$(warning usbprobe: the USB block exists only on nrf54lm20a/b -- skipped)
endif
endif

ifneq (,$(findstring TIKU_SHELL_CMD_AXONSPROBE=1,$(EXTRA_CFLAGS)))
ifeq ($(MCU),nrf54lm20b)
SRCS += kernel/shell/commands/tiku_shell_cmd_axonsprobe.c
else
$(warning axonsprobe: the Axon NPU exists only on nrf54lm20b -- skipped)
endif
endif

# TIKU_AXON_ENABLE=1 (nRF54LM20B only) links Nordic's Axon driver library
# from a local checkout of
# github.com/nordicsemi-neuton/nrf54lm20b-axon-audio-models at AXON_SDK.
# The library is LicenseRef-Nordic-5-Clause and is not in this repository.
# arch/nordic/tiku_axon_platform.c provides the nrf_axon_platform_*
# functions the library calls.
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

# Neural-network models on the Axon NPU, in one of two configurations:
#
#   TIKU_AXON_MODEL=<name>          compile that model (tinyml_kws,
#                                   tinyml_vww, tinyml_ic or tinyml_ad from
#                                   the checkout), its weights and its
#                                   known-answer test (KAT) vectors into
#                                   .rodata, with Nordic's test app, which
#                                   `axonsprobe model` runs; the store path
#                                   runs beside it for comparison
#   TIKU_AXON_MODEL_FROM_STORE=1    compile no model; `axonsprobe modelstore`
#                                   reads every model from a file in /data
#
# Setting both stops the build with an error.  TIKU_AXON_ILB sizes the
# interlayer working buffer in RAM2 (default 140000 bytes); a model that
# needs more fails its init and prints the size it needs.
ifeq ($(TIKU_AXON_MODEL_FROM_STORE),1)
ifneq ($(strip $(TIKU_AXON_MODEL)),)
$(error TIKU_AXON_MODEL_FROM_STORE=1 and TIKU_AXON_MODEL=$(TIKU_AXON_MODEL) are \
mutually exclusive -- from-store means no model is compiled in. Drop \
TIKU_AXON_MODEL to build the model-free image.)
endif
# Nordic's inference code is compiled and no model is: the image holds no
# weights, command buffer or KAT vectors.  tiku_shell_cmd_axonsprobe.c
# defines the globals a compiled model would, and every model, descriptor
# included, is read from /data at run time.  A model larger than the 384 KB
# code window, such as tinyml_vww, runs only this way.
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer_test.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_op_extensions.c
SRCS += $(AXON_SDK)/lib/axon/platform/src/nrf_axon_logging.c
SRCS += $(AXON_SDK)/lib/axon/platform/src/nrf_axon_vector_compare.c
CFLAGS += -DPRId64='"lld"' -DPRIx64='"llx"'
CFLAGS += -DTIKU_AXON_MODEL_FROM_STORE=1
CFLAGS += -I$(AXON_SDK)/tests/axon/compiled_models
TIKU_AXON_ILB ?= 140000
CFLAGS += -DNRF_AXON_INTERLAYER_BUFFER_SIZE=$(TIKU_AXON_ILB)
CFLAGS += -DNRF_AXON_PSUM_BUFFER_SIZE=$(TIKU_AXON_PSUM)
TIKU_AXON_PSUM ?= 0
else ifneq ($(strip $(TIKU_AXON_MODEL)),)
# Nordic's compiled models are C arrays, so the weights and the KAT vectors
# go into .rodata and count against the 384 KB code window.
# AXON_OVERSIZE_MODELS lists the models whose arrays do not fit the window:
# for those the warning says the link fails and points to
# TIKU_AXON_MODEL_FROM_STORE=1; for the others it says the build links.
AXON_OVERSIZE_MODELS := tinyml_vww
ifneq (,$(filter $(TIKU_AXON_MODEL),$(AXON_OVERSIZE_MODELS)))
$(warning TIKU_AXON_MODEL=$(TIKU_AXON_MODEL): this model's weights + KAT \
vectors in .rodata exceed the 384 KB code window, so the link WILL fail. \
Build TIKU_AXON_MODEL_FROM_STORE=1 instead and provision the model to /data. \
Do NOT raise the window -- weights are DATA, and sizing the OS's memory \
contract around a vendor test harness is the design P3/P4 undid.)
else
$(warning TIKU_AXON_MODEL=$(TIKU_AXON_MODEL): links today, but the weights are \
still compiled into .rodata and charged against the code window. This build is \
a development convenience and the reference the store path is checked against; \
TIKU_AXON_MODEL_FROM_STORE=1 is the shipping shape.)
endif
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_infer_test.c
SRCS += $(AXON_SDK)/drivers/axon/nrf_axon_nn_op_extensions.c
SRCS += $(AXON_SDK)/lib/axon/platform/src/nrf_axon_logging.c
# newlib-nano's inttypes.h in this toolchain does not define PRId64 or
# PRIx64, which the vendor logging code uses when it dumps mismatched
# vectors; these -D values supply them.
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
# Radio test commands (bleadv, radio154, rftest), requested with
# -DTIKU_SHELL_CMD_<NAME>=1 in EXTRA_CFLAGS, compile only where the platform
# sets the radio capability (TIKU_CAP_BLE_ADV, TIKU_CAP_154).  Elsewhere a
# warning names the missing radio, and tiku_shell_config.h drops the command
# from the table.
ifneq (,$(findstring TIKU_SHELL_CMD_BLEADV=1,$(EXTRA_CFLAGS)))
ifeq ($(TIKU_CAP_BLE_ADV),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_bleadv.c
else
$(warning bleadv: no broadcast-BLE radio on $(MCU) -- command skipped)
endif
endif
ifneq (,$(findstring TIKU_SHELL_CMD_RADIO154=1,$(EXTRA_CFLAGS)))
ifeq ($(TIKU_CAP_154),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_radio154.c
else
$(warning radio154: no 802.15.4 PHY on $(MCU) -- command skipped)
endif
endif
ifneq (,$(findstring TIKU_SHELL_CMD_RFTEST=1,$(EXTRA_CFLAGS)))
ifeq ($(TIKU_CAP_BLE_ADV),1)
SRCS += kernel/shell/commands/tiku_shell_cmd_rftest.c
else
$(warning rftest: no test-capable 2.4 GHz radio on $(MCU) -- command skipped)
endif
endif
endif
# The VFS tree calls the GPIO driver on every port.  MSP430's is added here;
# every other port lists its own in its arch block above.
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
# TikuBench/tests/tests.mk adds the firmware test sources when HAS_TESTS=1,
# and the -I that resolves <tests/...> includes.  The leading `-` on the
# include lets a checkout without TikuBench build.
-include $(PROJ_DIR)/TikuBench/tests/tests.mk

# tikukits/gfx visual test runner (TEST_KITS_GFX_VISUAL=1): an autostart
# process that owns the UART, takes single-character commands and renders
# gfx scenes on the e-paper panel, driven from the host by
# TikuBench/tikubench/gfx_test.py.  It cannot run with the shell, which
# also reads the UART.
ifeq ($(TEST_KITS_GFX_VISUAL),1)
SRCS   += TikuBench/tests/kits/gfx/test_kits_gfx_visual.c
CFLAGS += -DTEST_KITS_GFX_VISUAL=1
TIKU_KIT_GFX_ENABLE    := 1
TIKU_KIT_EPAPER_ENABLE := 1
endif

# tikukits/ui visual test runner (TEST_KITS_UI_VISUAL=1): an autostart
# process that takes single-character commands on the UART and renders UI
# widget compositions, driven from the host by TikuBench/tikubench/ui_test.py.
# Build it with TIKU_SHELL_ENABLE=0: the shell also reads the UART.
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

# Examples 20 and up are compiled only when their TIKU_EXAMPLE_<NAME>=1 flag
# is set, so an object left by another example's build is not linked.  Each
# example defines the autostart process list (TIKU_AUTOSTART_PROCESSES), and
# two in one link fail with a duplicate definition.
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

# kits_examples: gfx and ui demos for the tikukits/gfx and tikukits/ui kits.
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

# gfx_phase0 (clip stack, alignment, wrapping, font fallback), ui_layout
# (vbox, hbox and grid containers) and ui_settings (a multi-screen demo).
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

# TikuKits examples (needs examples/ and tikukits/).  Each examples/kits/
# folder is compiled only when its kit is enabled: `make HAS_EXAMPLES=1
# TIKU_EXAMPLE_KITS_MATRIX=1` enables the maths kit and compiles
# examples/kits/maths/*.c and none of the other folders.
ifeq ($(HAS_TIKUKITS),1)

# The dispatcher is compiled whenever HAS_TIKUKITS and HAS_EXAMPLES are both
# 1, so main.c's call to example_kits_run() always links.  It runs only the
# demos whose TIKU_EXAMPLE_KITS_* flag is set.
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
# The shell comes from kernel/shell/; APP=cli forces TIKU_SHELL_ENABLE=1.
endif

ifeq ($(APP),net)
# APP=net compiles net/tiku_app_net.c from TIKU_APP_DIR, which the TikuBench
# harness provides (see TIKU_APP_DIR above).  An empty TIKU_APP_DIR stops the
# build with an error, except for `make clean`, which compiles nothing.
ifeq ($(strip $(TIKU_APP_DIR)),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error APP=net needs TIKU_APP_DIR=<dir containing net/tiku_app_net.c>; the app firmware lives in the TikuBench harness now, not core tikuOS)
endif
endif
CFLAGS += -DTIKU_APP_NET=1
SRCS += $(TIKU_APP_DIR)/net/tiku_app_net.c
# The CoAP client and server (tikukits/net/coap/, library and demo process)
# are compiled in, and TIKU_KITS_NET_COAP=1 makes the net app start the
# CoAP server process.
SRCS   += $(wildcard tikukits/net/coap/*.c)
CFLAGS += -DTIKU_KITS_NET_COAP=1
endif

# TIKU_SHELL_NET_TEST=1 builds the shell firmware the TikuBench net suite
# talks to: TCP, the MQTT kit and the mqtt command, the CoAP server, and the
# shell over TCP (tiku_shell_io_tcp.c).
ifeq ($(TIKU_SHELL_NET_TEST),1)
CFLAGS += -DTIKU_SHELL_NET_TEST=1 -DTIKU_KITS_NET_TCP_ENABLE=1
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
CFLAGS += -DTIKU_SHELL_TCP_ENABLE=1
SRCS   += $(wildcard tikukits/net/coap/*.c)
CFLAGS += -DTIKU_KITS_NET_COAP=1
SRCS += kernel/shell/commands/tiku_shell_cmd_mqtt.c
SRCS += kernel/shell/tiku_shell_io_tcp.c
endif

# prop/Makefile.inc, when present, adds local sources and build rules.  git
# ignores prop/, and the leading `-` skips the include when the file is
# absent.
-include prop/Makefile.inc

# ---------------------------------------------------------------------------
# TikuKits (per-kit gated; default 0 unless an app/example needs it)
#
# A kit's sources compile only when its TIKU_KIT_<NAME>_ENABLE flag is 1.
# Apps, examples, tests and some platform and driver blocks set these flags
# or add single kit files (RA8P1 always enables the crypto kit).
# ---------------------------------------------------------------------------

# TIKU_TURBO_BENCH=1 builds the turbo benchmark: TikuKits workloads (SHA-256,
# AES-128, Euclidean distance, a small neural net) run at 96 MHz (LP) and
# 192 MHz (HP) between serial markers, and the host times each run.  It
# enables the crypto, maths and ml kits; main.c calls turbo_bench_run() and
# then halts.
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
# IP=a.b.c.d sets the device IPv4 address, for example `make ... IP=10.0.0.5`.
# tiku_kits_net.h defaults TIKU_KITS_NET_IP_ADDR to {172,16,7,2} behind an
# #ifndef; the dotted quad becomes that brace list, quoted so the shell does
# not brace-expand it.  An IP= given on the make line is part of the
# flag-change guard's fingerprint, so changing it rebuilds every object.
ifdef IP
comma := ,
CFLAGS += -DTIKU_KITS_NET_IP_ADDR="{$(subst .,$(comma),$(IP))}"
endif
SRCS   += $(wildcard tikukits/net/slip/*.c)
ifeq ($(TIKU_KITS_NET_DHCP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_DHCP_ENABLE=1
endif
ifeq ($(TIKU_KITS_NET_DNS_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_DNS_ENABLE=1
endif
# TIKU_KIT_NET_MIN=1 compiles the IPv4 base set (ipv4, icmp, udp) and leaves
# out the other protocol modules and their static buffers; the flags below
# add DHCP, DNS, TCP, MQTT and HTTP back one at a time.
ifeq ($(TIKU_KIT_NET_MIN),1)
# The C side sees TIKU_KIT_NET_MIN too, so a shell command whose kit only the
# full set compiles, such as syslog, leaves itself out.
CFLAGS += -DTIKU_KIT_NET_MIN=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_ipv4.c
SRCS   += tikukits/net/ipv4/tiku_kits_net_icmp.c
SRCS   += tikukits/net/ipv4/tiku_kits_net_udp.c
# TIKU_KITS_NET_DHCP_ENABLE=1 adds the DHCP client, so the board takes its
# address from a DHCP server.
ifeq ($(TIKU_KITS_NET_DHCP_ENABLE),1)
SRCS   += tikukits/net/ipv4/tiku_kits_net_dhcp.c
endif
# TIKU_KITS_NET_DNS_ENABLE=1 adds the DNS stub resolver, which the ntp and
# dns shell commands call; the full set compiles it with the rest of ipv4/.
ifeq ($(TIKU_KITS_NET_DNS_ENABLE),1)
SRCS   += tikukits/net/ipv4/tiku_kits_net_dns.c
endif
# TIKU_KITS_NET_MQTT_ENABLE=1 or TIKU_KITS_NET_HTTP_ENABLE=1 adds TCP, the
# transport both use (for example BASIC MQTTPUB or HTTPGET$ on a lean Wi-Fi
# profile), and each adds its own kit below.  The http kit runs over TLS
# only, so it needs TIKU_KIT_CRYPTO_ENABLE=1 HAS_TLS=1 as well.
ifneq ($(filter 1,$(TIKU_KITS_NET_MQTT_ENABLE) $(TIKU_KITS_NET_HTTP_ENABLE)),)
CFLAGS += -DTIKU_KITS_NET_TCP_ENABLE=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_tcp.c
endif
# Without MQTT, HTTP, the IP link or the net test, TCP is not compiled, and
# TIKU_KITS_NET_TCP_ENABLE=0 removes IPv4's call into it; tiku_kits_net.h
# defaults the flag to 1, which would leave tiku_kits_net_tcp_input()
# undefined at link time.
ifeq ($(filter 1,$(TIKU_KITS_NET_MQTT_ENABLE) $(TIKU_KITS_NET_HTTP_ENABLE) \
                 $(TIKU_KITS_NET_LINK_IP_ENABLE) $(TIKU_SHELL_NET_TEST)),)
CFLAGS += -DTIKU_KITS_NET_TCP_ENABLE=0
endif
ifeq ($(TIKU_KITS_NET_MQTT_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
# tiku_shell_config.h turns the mqtt shell command on with the kit flag, but
# only the TIKU_SHELL_NET_TEST block compiles tiku_shell_cmd_mqtt.c, so
# TIKU_SHELL_CMD_MQTT=0 keeps the command out of the table and the link.
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
# The wildcards above compile the MQTT and HTTP clients.  The BASIC words
# test TIKU_KITS_NET_MQTT_ENABLE (MQTTPUB, MQTTWAIT$) and
# TIKU_KITS_NET_HTTP_ENABLE (HTTPGET$), so the make flags of those names pass
# the -D here as well; without it the words are compiled out.
ifeq ($(TIKU_KITS_NET_MQTT_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_MQTT_ENABLE=1
# tiku_shell_config.h turns the mqtt shell command on with the kit flag, but
# only the TIKU_SHELL_NET_TEST block compiles tiku_shell_cmd_mqtt.c, so
# TIKU_SHELL_CMD_MQTT=0 keeps the command out of the table and the link.  The
# BASIC MQTT words need only the kit flag.
CFLAGS += -DTIKU_SHELL_CMD_MQTT=0
endif
ifeq ($(TIKU_KITS_NET_HTTP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_HTTP_ENABLE=1
endif
endif
# WiFi link backend: the net kit over tiku_wireless, with either radio driver
# (the CYW43439 or the ESP32-C61's own).
ifneq ($(filter 1,$(TIKU_DRV_WIFI_CYW43_ENABLE) $(TIKU_DRV_WIFI_ESP_ENABLE)),)
ifeq ($(TIKU_KITS_NET_WIFI_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_WIFI_ENABLE=1
SRCS   += $(wildcard tikukits/net/wifi/*.c)
endif
endif
# TIKU_KITS_NET_LINK_IP_ENABLE=1 compiles the IP link: a board's window
# session over a TCP connection it dials to a desktop.  It enables TCP, and
# only the applications overlay registers it (TIKU_APPL_GUI_IP).  Without
# TIKU_KIT_NET_ENABLE=1 the build stops with an error.
ifeq ($(TIKU_KITS_NET_LINK_IP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_LINK_IP_ENABLE=1 -DTIKU_KITS_NET_TCP_ENABLE=1
SRCS   += tikukits/net/ipv4/tiku_kits_net_tcp.c
SRCS   += tikukits/net/link/tiku_kits_net_link_ip.c
endif
endif
ifneq ($(TIKU_KIT_NET_ENABLE),1)
ifeq ($(TIKU_KITS_NET_LINK_IP_ENABLE),1)
$(error TIKU_KITS_NET_LINK_IP_ENABLE=1 needs the net kit under it; add \
TIKU_KIT_NET_ENABLE=1)
endif
endif

# Bluetooth Low Energy protocol stack: driver-agnostic.  Pulled in whenever a
# driver gives it a transport (TIKU_BT_HOST, set beside HAS_TIKUKITS).  Code
# tests TIKU_BT_HOST for the stack and TIKU_BT_ON_DEMAND for a controller
# that `bt on` powers, not the chip.
ifeq ($(TIKU_BT_HOST),1)
CFLAGS += -DTIKU_BT_HOST=1
ifeq ($(TIKU_BT_ON_DEMAND),1)
CFLAGS += -DTIKU_BT_ON_DEMAND=1
endif
ifeq ($(TIKU_BT_SW_CRYPTO),1)
CFLAGS += -DTIKU_BT_SW_CRYPTO=1
endif
# TIKU_HAS_BLE is the generic connectable-BLE capability: the BLE serial
# facade over the stack (the Nordic UART Service byte pipe) and the BASIC BLE
# words test it, not the radio chip.
CFLAGS += -DTIKU_HAS_BLE=1
SRCS += interfaces/bluetooth/tiku_ble_serial.c
# TIKU_HAS_BLE_ADV is the broadcast one: the beacon, scans and the observer of
# the tiku_ble_adv facade, its backend over the stack here, behind /sys/radio
# and BLEBEACON/BLESCAN$.  On for the radios powered on demand; the Pico 2 W's
# image, which carries the CYW43439 firmware, has no room (TIKU_BT_ADV=1).
TIKU_BT_ADV ?= $(TIKU_BT_ON_DEMAND)
ifeq ($(TIKU_BT_ADV),1)
CFLAGS += -DTIKU_HAS_BLE_ADV=1
SRCS += interfaces/bluetooth/tiku_ble_adv_hci.c
endif
include $(wildcard $(PROJ_DIR)/tikukits/net/bluetooth/build.mk)
endif

# Scratch demos: DEMO=<dir> on the make line compiles demos/<dir>/*.c.  This
# block sits after `SRCS = main.c`, which resets SRCS.
ifeq ($(HAS_DEMOS),1)
SRCS   += $(wildcard demos/$(DEMO)/*.c)
endif

# TIKU_CRACEN_PK_ENABLE=1 compiles the CRACEN BA414EP ECDSA-verify calls
# (P-256, P-384) in arch/nordic/tiku_crypto_arch.c, which the cryptoprobe
# command uses; the TLS path verifies in software.  The engine runs
# Nordic-proprietary microcode that this repository does not contain: the
# build also needs a licensed cracen_pk_microcode.h next to
# tiku_crypto_arch.c, and without it every verify returns an error.  Default
# off.  The CryptoMaster SHA and AES-GCM offload needs none of this and is
# compiled into every Nordic build.
ifeq ($(TIKU_CRACEN_PK_ENABLE),1)
CFLAGS += -DTIKU_CRACEN_PK_ENABLE=1
endif

ifeq ($(TIKU_KIT_CRYPTO_ENABLE),1)
CFLAGS += -DTIKU_KIT_CRYPTO_ENABLE=1
SRCS   += $(wildcard tikukits/crypto/sha1/*.c)
SRCS   += $(wildcard tikukits/crypto/sha256/*.c)
SRCS   += $(wildcard tikukits/crypto/sha384/*.c)
SRCS   += $(wildcard tikukits/crypto/base64/*.c)
SRCS   += $(wildcard tikukits/crypto/crc/*.c)
SRCS   += $(wildcard tikukits/crypto/gcm/*.c)
SRCS   += $(wildcard tikukits/crypto/aes128/*.c)
SRCS   += $(wildcard tikukits/crypto/hkdf/*.c)
SRCS   += $(wildcard tikukits/crypto/hmac/*.c)
SRCS   += $(wildcard tikukits/crypto/pbkdf2/*.c)
SRCS   += $(wildcard tikukits/crypto/aeskw/*.c)
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
# HAS_TLS=1 adds the TLS clients (PSK, TLS 1.3, TLS 1.2).  They need
# TIKU_KITS_CRYPTO_TLS_RNG_FILL, which tiku_kits_crypto_tls_config.h maps to
# the TRNG on RP2350, Ambiq, MSP430, Nordic and the ESP32-C61; on any other
# platform the build stops with an #error unless EXTRA_CFLAGS defines it.
ifeq ($(HAS_TLS),1)
SRCS   += $(wildcard tikukits/net/tls/psk/*.c)
SRCS   += $(wildcard tikukits/net/tls/tls13/*.c)
SRCS   += $(wildcard tikukits/net/tls/tls12/*.c)
# With the cert clients linked, TIKU_KITS_NET_HTTP_CERT_ENABLE=1 lets the
# http kit's http_get() and http_post() use them (the CERT trust model) as
# well as the PSK client.  Without HAS_TLS the http kit is PSK-only and
# references neither tls13 nor tls12.
ifeq ($(TIKU_KITS_NET_HTTP_ENABLE),1)
CFLAGS += -DTIKU_KITS_NET_HTTP_CERT_ENABLE=1
endif
endif
endif

# BASE64$, SHA256$ and HMAC$ BASIC builtins: on in every BASIC build unless
# TIKU_BASIC_CRYPTO=0.  Their three kit sources are added here only when the
# crypto kit block above has not compiled them already.
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
TIKU_BASIC_CRYPTO ?= 1
ifeq ($(TIKU_BASIC_CRYPTO),1)
CFLAGS += -DTIKU_BASIC_CRYPTO_ENABLE=1
ifneq ($(TIKU_KIT_CRYPTO_ENABLE),1)
SRCS   += tikukits/crypto/sha256/tiku_kits_crypto_sha256.c
SRCS   += tikukits/crypto/base64/tiku_kits_crypto_base64.c
SRCS   += tikukits/crypto/hmac/tiku_kits_crypto_hmac.c
endif
else
# Passed explicitly: tiku_basic_config.h otherwise turns the words on when
# TIKU_BASIC_TIER_BIG and TIKU_KIT_CRYPTO_ENABLE are both set.
CFLAGS += -DTIKU_BASIC_CRYPTO_ENABLE=0
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

# Object files in the build directory.  ASM_SRCS holds .S files that
# build.mk fragments add (for example .incbin wrappers for firmware blobs);
# they compile with CFLAGS, since .S is preprocessed and then assembled and
# needs only the include paths and -D macros.
# Apps overlay sources (APPL_SRCS and APPL_CFLAGS from
# applications/applications.mk), appended after every platform block, so
# they build on every platform.
SRCS   += $(APPL_SRCS)
CFLAGS += $(APPL_CFLAGS)
# $(sort) also removes duplicates.  Some files are added by two blocks
# (tikukits/crypto/p256 by the Nordic BLE-SMP block for LE Secure
# Connections and by the crypto kit; tiku_i2c_arch.c twice on RA8P1), and an
# object twice in the link fails with "multiple definition".  The order of
# explicit objects does not change the link: the libraries come after every
# object, and the linker scripts place every section by pattern.
SRCS := $(sort $(SRCS))
ASM_SRCS := $(sort $(ASM_SRCS))

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS)) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))

ifneq ($(BASIC_PROGRAM),)
# The embedded-BASIC object, appended directly; its rules sit below `all:`,
# so `all` stays the default goal.
OBJS += $(TIKU_BASIC_EMBEDDED_O)
endif

ifeq ($(TIKU_FLPR_ENABLE),1)
# The embedded FLPR coprocessor image; its rules sit below `all:`, so `all`
# stays the default goal.
OBJS += $(TIKU_FLPR_IMG_O)
endif

ifeq ($(TIKU_DRV_CPU1_ENABLE),1)
# The embedded Cortex-M33 payload; its rules sit below `all:`, so `all`
# stays the default goal.
OBJS += $(TIKU_CPU1_IMG_O)
endif

ifeq ($(TIKU_BASIC_MODULE_ENABLE),1)
# The embedded loadable-module image; its rules sit below `all:`, so `all`
# stays the default goal.
OBJS += $(TIKU_MOD_IMG_O)
endif

# Build identity, read back as /sys/device/build and /sys/device/board.  The
# source is generated on every build and rewritten only when it changes
# (recipes in tools/firmware_options.mk).
TIKU_BUILD_ID_C := $(BUILD_DIR)/generated/tiku_build_id.c
TIKU_BUILD_ID_O := $(BUILD_DIR)/generated/tiku_build_id.o
OBJS += $(TIKU_BUILD_ID_O)

# Header-dependency tracking.  Each compile writes a .d file beside its .o
# (-MMD -MP in the compile rules below) listing every header it read; they
# are included here, so editing a header rebuilds the objects that include
# it.  -MP adds a phony target per header, so deleting a header does not
# break the build.  The .d files do not exist before the first build, and
# the leading `-` skips them.
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

# make lint runs tools/check_durable_placement.sh (no raw .persistent or
# .uninit section attribute outside the placement macros of
# kernel/memory/tiku_mem.h); with a host C compiler, the tools/usbmsc checks
# and the cpu_clock and cpu_settings host tests; and
# tools/check_comment_style.py --strict (comment-style.md) over the tracked
# tree.  Every check runs, and the target fails if any one fails.
lint:
	@rc=0; \
	 ./tools/check_durable_placement.sh || rc=1; \
	 if command -v cc >/dev/null 2>&1; then \
	   $(MAKE) -s -C tools/usbmsc check >/dev/null 2>&1 \
	     && echo "usb host checks: OK (tools/usbmsc: BOT/SCSI, control, diff)" \
	     || { echo "usb host checks: FAILED -- cd tools/usbmsc && make check"; rc=1; }; \
	   for t in cpu_clock cpu_settings; do \
	     cc -Wall -Wextra -I. -o /tmp/tiku_$${t}_test tools/$${t}_test.c \
	       >/dev/null 2>&1 && /tmp/tiku_$${t}_test >/dev/null 2>&1 \
	       && echo "$${t} host test: OK" \
	       || { echo "$${t} host test: FAILED -- tools/$${t}_test.c"; rc=1; }; \
	     rm -f /tmp/tiku_$${t}_test; \
	   done; \
	 else \
	   echo "usb host checks: SKIPPED -- no host C compiler"; \
	 fi; \
	 ./tools/check_comment_style.py --strict || rc=1; \
	 exit $$rc

# What `all` builds besides main.elf: main.bin and main.uf2 on RP2350,
# main.bin on Ambiq and the ESP32-C61, main.hex on Nordic, main.bin and the
# signed main.signed.bin on STM32N6, and nothing more on MSP430 and RA8P1.
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
else ifeq ($(TIKU_PLATFORM),stm32n6)
# The N6 deliverable is a signed image: the boot ROM reads a header for the
# entry point and refuses a bare binary.
TARGET_BIN    = main.bin
TARGET_SIGNED = main.signed.bin
all: $(TARGET) $(TARGET_BIN) $(TARGET_SIGNED) size
else ifeq ($(TIKU_PLATFORM),esp32c61)
# The ROM loads Espressif's image format, segments and an entry point, not a
# bare binary; esptool writes it from the ELF.
TARGET_BIN = main.bin
all: $(TARGET) $(TARGET_BIN) size
else
all: $(TARGET) size
endif

# main.elf in the project root is shared by every platform's build.
# build/.platform-stamp, outside every build/<mcu>/ directory, holds the
# platform of the last build and is a prerequisite of main.elf: a build for
# another platform rewrites it, and its newer timestamp forces a relink.
PLATFORM_STAMP = build/.platform-stamp

# A `make options` query builds nothing, so it leaves the stamp alone.
ifneq ($(MAKECMDGOALS),options)
ifneq ($(TIKU_PLATFORM),$(shell cat $(PLATFORM_STAMP) 2>/dev/null))
$(shell mkdir -p build && echo $(TIKU_PLATFORM) > $(PLATFORM_STAMP))
endif
endif

$(PLATFORM_STAMP):
	@mkdir -p build
	@echo $(TIKU_PLATFORM) > $@

# EXTRA_LDFLAGS is appended to the final link, the link-side counterpart of
# EXTRA_CFLAGS.  Given on the make line it is part of the flag-change guard's
# fingerprint, so changing it drops this build dir's objects.  It can carry
# the packing-only code-cap override that nrf54lm20a.ld describes; a shipped
# image is linked without it.
# TIKU_LDSCRIPTS lists the -T linker scripts in LDFLAGS, which are
# prerequisites of main.elf: an edit to a carve, a region or an ASSERT
# relinks the image.  A script missing from the list leaves main.elf newer
# than every object, so the link is skipped and the old image stays.
TIKU_LDSCRIPTS := $(patsubst -T%,%,$(filter -T%,$(LDFLAGS)))

ifeq ($(TIKU_PLATFORM),esp32c61)
# A build with an XIP fragment puts a header first in the XIP window
# (xip_head.ld, tiku_xip_header.c); at boot the kernel checks it against the
# running image and halts on an xip.bin from another build.
ifneq ($(strip $(TIKU_XIP_LDS)),)
TIKU_XIP_LDS := arch/esp32c61/xip_head.ld $(TIKU_XIP_LDS)
SRCS += arch/esp32c61/tiku_xip_header.c
endif
# What the arch script INCLUDEs: one line per XIP or PSRAM fragment the
# build registered, none otherwise.  Rewritten only when its text changes, so
# an unchanged build does not relink.
TIKU_XIP_LD := $(BUILD_DIR)/tiku_xip.ld
$(shell mkdir -p $(BUILD_DIR) && \
    printf '%s\n' '/* generated: the XIP and PSRAM fragments registered */' \
        $(foreach f,$(TIKU_XIP_LDS) $(TIKU_PSRAM_LDS),'INCLUDE $(f)') \
        > $(TIKU_XIP_LD).new && \
    { cmp -s $(TIKU_XIP_LD).new $(TIKU_XIP_LD) || \
      mv $(TIKU_XIP_LD).new $(TIKU_XIP_LD); })
TIKU_LDSCRIPTS += $(TIKU_XIP_LD) $(TIKU_XIP_LDS) $(TIKU_PSRAM_LDS)
endif

$(TARGET): $(OBJS) $(PLATFORM_STAMP) $(NOSYS_FIXED) $(TIKU_LDSCRIPTS)
	$(CC) $(LDFLAGS) $(EXTRA_LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

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

# Ambiq: raw binary that J-Link loads into MRAM at AMBIQ_LOAD_ADDR (0x18000
# on Apollo4, 0x410000 on Apollo510).
ifeq ($(TIKU_PLATFORM),ambiq)
$(TARGET_BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@
endif

# STM32N6: .elf -> .bin -> signed image the boot ROM will accept.
ifeq ($(TIKU_PLATFORM),stm32n6)
#
# CubeProgrammer is not on PATH by default.  STM32N6_CUBE is the first
# directory in STM32N6_CUBE_CANDIDATES that holds STM32_SigningTool_CLI;
# setting STM32N6_CUBE on the make line skips the search.
#
STM32N6_CUBE_CANDIDATES := \
    /Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin \
    /opt/st/stm32cubeprog/bin \
    /opt/STM32CubeProgrammer/bin \
    $(HOME)/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin \
    $(HOME)/st/stm32cubeprog/bin \
    /usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin
STM32N6_CUBE   ?= $(firstword $(foreach d,$(STM32N6_CUBE_CANDIDATES),\
                    $(if $(wildcard $(d)/STM32_SigningTool_CLI),$(d))))
STM32N6_SIGN   ?= $(STM32N6_CUBE)/STM32_SigningTool_CLI
STM32N6_PROG   ?= $(STM32N6_CUBE)/STM32_Programmer_CLI
# FSBL partition ID advertised by the ROM's DFU interface.
STM32N6_PART   ?= 0x01

# Recipe text that prints where CubeProgrammer was looked for and how to set
# STM32N6_CUBE, then fails.  Only the sign, flash and dfu-reset recipes call
# it, so main.elf and main.bin build on a host without CubeProgrammer.
define STM32N6_NEED_CUBE
	echo "stm32n6: CubeProgrammer not found -- cannot $(1)."; \
	 echo "  looked in:"; \
	 $(foreach d,$(STM32N6_CUBE_CANDIDATES),echo "    $(d)";) \
	 echo "  install it, or point at it:"; \
	 echo "    make MCU=stm32n6 STM32N6_CUBE=/path/to/bin $(1)"; \
	 exit 1
endef

$(TARGET_BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@

# No -la/-ep: the tool derives the entry point from vector word 1, which
# carries the Thumb bit. An even entry point locks the core up before the
# first instruction. The tool writes its output read-only and prompts before
# overwriting, so the stale file goes first and stdin is closed.
$(TARGET_SIGNED): $(TARGET_BIN)
	@test -x "$(STM32N6_SIGN)" || { $(call STM32N6_NEED_CUBE,sign); }
	@rm -f $@
	@$(STM32N6_SIGN) -bin $< -nk -of 0x80000000 -t fsbl -hv 2.3 -align -s -o $@ \
	    < /dev/null > /dev/null
	@echo "  [sign]  $< -> $@"
endif

# ESP32-C61: esptool turns the ELF into the image format the ROM loader
# accepts; without esptool the recipe stops and says how to install it.
# Sections a driver placed in the XIP window (.xip*) are removed from the
# boot image and written raw to xip.bin, which goes to flash at 1 MB.
# objcopy warns that the XIP segment is empty in the SRAM copy; the recipe
# keeps that output in xip-split.log and prints it only when objcopy fails.
ifeq ($(TIKU_PLATFORM),esp32c61)
ESPTOOL ?= esptool
TARGET_XIP := xip.bin
XIP_FLASH_OFFSET := 0x100000
$(TARGET_BIN): $(TARGET)
	@command -v $(ESPTOOL) > /dev/null || { \
	    echo "esp32c61: esptool not found -- brew install esptool, or"; \
	    echo "  pip install esptool, or point at it: ESPTOOL=/path/esptool"; \
	    exit 1; }
	@if $(SIZE) -A $< | grep -q '^\.xip'; then \
	    $(OBJCOPY) -R '.xip*' $< $(BUILD_DIR)/main.sram.elf \
	        2> $(BUILD_DIR)/xip-split.log || \
	        { cat $(BUILD_DIR)/xip-split.log; exit 1; }; \
	    $(ESPTOOL) --chip esp32c61 elf2image --flash-mode dio \
	        --flash-size 8MB -o $@ $(BUILD_DIR)/main.sram.elf > /dev/null && \
	    $(OBJCOPY) -O binary -j '.xip*' $< $(TARGET_XIP) && \
	    echo "  [image] $< -> $@ + $(TARGET_XIP) (flash $(XIP_FLASH_OFFSET))"; \
	else \
	    $(ESPTOOL) --chip esp32c61 elf2image --flash-mode dio \
	        --flash-size 8MB -o $@ $< > /dev/null && \
	    echo "  [image] $< -> $@"; \
	fi
endif

# Nordic: Intel HEX that nrfutil or J-Link programs into RRAM.
ifeq ($(TIKU_PLATFORM),nordic)
$(TARGET_HEX): $(TARGET)
	$(OBJCOPY) -O ihex $< $@
	@echo "  [hex]   $(TARGET) -> $(TARGET_HEX)"

hex: $(TARGET_HEX)
endif

# Embedded BASIC: the generated .c lives inside $(BUILD_DIR), where the
# pattern rule above cannot find its source, so it has explicit generate and
# compile rules.  They sit after `all:`, so the embedded .c file does not
# become the default goal.
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
# toolchain, link at the SRAM carve the image runs in (tiku_flpr.ld),
# flatten to a binary, then wrap that binary as an ARM object (in .rodata,
# so in RRAM) whose _binary_tiku_flpr_bin_* symbols tiku_flpr_arch.c copies
# from.  objcopy runs from inside the build dir, so the symbol names come
# from the bare file name and not the build path.
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

# ---------------------------------------------------------------------------
# Cortex-M33 payload sub-build: same toolchain as the M85 image but its own
# flags, linker script and link, flattened to a binary and wrapped as an ARM
# object whose _binary_tiku_cpu1_bin_* symbols tiku_cpu1_arch.c copies from.
# The wrap runs from inside the build dir, so the symbol names come from the
# bare file name and not from a path that holds the MCU name.
# ---------------------------------------------------------------------------
ifeq ($(TIKU_DRV_CPU1_ENABLE),1)
$(CPU1_BUILD)/%.o: arch/ra8p1/cpu1/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPU1_CFLAGS) -c -o $@ $<

$(CPU1_BUILD)/tiku_kits_crypto_p256.o: tikukits/crypto/p256/tiku_kits_crypto_p256.c
	@mkdir -p $(dir $@)
	$(CC) $(CPU1_CFLAGS) -c -o $@ $<

$(CPU1_BUILD)/tiku_cpu1.elf: $(CPU1_OBJS) arch/ra8p1/cpu1/tiku_cpu1.ld
	$(CC) $(CPU1_CFLAGS) -T arch/ra8p1/cpu1/tiku_cpu1.ld \
	    -Wl,--gc-sections -o $@ $(CPU1_OBJS)

$(CPU1_BUILD)/tiku_cpu1.bin: $(CPU1_BUILD)/tiku_cpu1.elf
	$(OBJCOPY) -O binary --gap-fill 0 $< $@
	@echo "  [cpu1]  $$(stat -c%s $@) bytes"

$(TIKU_CPU1_IMG_O): $(CPU1_BUILD)/tiku_cpu1.bin
	cd $(CPU1_BUILD) && $(OBJCOPY) -I binary -O elf32-littlearm -B arm \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    tiku_cpu1.bin tiku_cpu1_img.o

-include $(CPU1_OBJS:.o=.d)
endif

# Loadable-module sub-build: mod_demo.c compiled and linked on its own at the
# address $(MOD_LDS) gives, flattened to mod_demo.bin, then embedded with
# _binary_mod_demo_bin_start and _end symbols: by objcopy, or for
# MOD_EMBED=carray by tools/mod_embed.py as a C array.  tiku_basic_module.c
# reads the image through those symbols.
ifeq ($(TIKU_BASIC_MODULE_ENABLE),1)
$(MOD_BUILD)/mod_demo.elf: kernel/shell/basic/modules/mod_demo.c \
                           $(MOD_LDS)
	@mkdir -p $(dir $@)
	$(CC) $(MOD_CFLAGS) $(MOD_LDFLAGS) -T $(MOD_LDS) \
	    -Wl,--gc-sections -nostartfiles -o $@ $< -lgcc

$(MOD_BUILD)/mod_demo.bin: $(MOD_BUILD)/mod_demo.elf
	$(OBJCOPY) -O binary $< $@
	@echo "  [module]  $$(wc -c < $@ | tr -d ' ') bytes"

ifeq ($(MOD_EMBED),carray)
$(TIKU_MOD_IMG_O): $(MOD_BUILD)/mod_demo.bin
	python3 tools/mod_embed.py $(MOD_BUILD)/mod_demo.bin $(MOD_BUILD)/mod_demo_img.c
	$(CC) $(CFLAGS) -c -o $@ $(MOD_BUILD)/mod_demo_img.c
else
$(TIKU_MOD_IMG_O): $(MOD_BUILD)/mod_demo.bin
	cd $(MOD_BUILD) && $(OBJCOPY) -I binary -O $(MOD_WRAP_OUT) -B $(MOD_WRAP_ARCH) \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    mod_demo.bin mod_demo_img.o
endif
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

# Nordic flashing tool: NRFUTIL is nrfutil from PATH, else temp/nrfutil in
# this tree (temp/ is not tracked).  JLINK_SN or NRF_SN (JLINK_SN first)
# selects one J-Link probe by serial number on a multi-DK rig; TikuBench
# passes NRF_SN=<serial> per board.
#
# nrfutil finds its `device` subcommand under $NRFUTIL_HOME (default
# $HOME/.nrfutil).  Under sudo HOME is /root, which has no plugins, and the
# flash fails with "Subcommand nrfutil-device not found".  NRFUTIL_ENV sets
# NRFUTIL_HOME to the invoking user's ~/.nrfutil when make runs as root with
# SUDO_USER set, and is empty otherwise: a make that `sudo -u <user>` starts
# from a root shell has SUDO_USER=root, and /root/.nrfutil is not readable
# by that user.
NRFUTIL ?= $(shell command -v nrfutil 2>/dev/null || echo $(CURDIR)/temp/nrfutil)
NRFUTIL_ENV = $(if $(and $(SUDO_USER),$(filter 0,$(shell id -u))),NRFUTIL_HOME=$(shell getent passwd $(SUDO_USER) | cut -d: -f6)/.nrfutil,)
NRF_SN  ?=
_NRF_SN    := $(strip $(if $(strip $(JLINK_SN)),$(JLINK_SN),$(NRF_SN)))
NRF_SN_ARG := $(if $(_NRF_SN),--serial-number $(_NRF_SN),)

ifeq ($(TIKU_PLATFORM),rp2350)

# Pico 2 / Pico 2 W: flash runs `picotool load -fx main.uf2` when picotool
# is found, else copies main.uf2 to the board's mass-storage mount (BOOTSEL
# mode): the first of the RP2350 or RP2 mount points below that exists.
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

# Ambiq boards over a SEGGER J-Link: flash writes a J-Link Commander script
# that loads main.bin into MRAM at AMBIQ_LOAD_ADDR, then runs JLINK_RUN_SEQ
# (reset and go).  JLINK_DEVICE, JLINK_SPEED and JLINK_IF can be set on the
# make line for another probe or part.
JLINK_FLASH_SCRIPT = $(BUILD_DIR)/flash.jlink
JLINK_ERASE_SCRIPT = $(BUILD_DIR)/erase.jlink

# The flash recipe:
#   - sends `sleep off` to PORT when PORT is set and writable, so the shell
#     stops idling in a low-power state the debug unit cannot reach;
#   - resets and halts the CPU (`r`, `h`) before `loadbin`: J-Link saves
#     target RAM before it programs MRAM, and cannot while the CPU runs;
#     without the halt it fails, or programs and then fails verification
#     with old and new code mixed on the part;
#   - fails when the log names a known failure (Verification failed, Failed
#     to prepare, Could not connect, Error while programming), since JLinkExe
#     exits 0 whatever happens;
#   - fails when the log has no "Flash download: Total" or "Flash download:
#     Program" line, which means nothing was written.  "Failed to halt CPU"
#     alone is not a failure: it also appears in a good log, when the reset
#     after a first failed halt succeeds.
# On apollo510, `connect` to firmware that has been running can fail to halt
# it ("Failed to halt CPU"), and the reset that follows can leave the part
# with no console and no SWD until a power cycle; `sleep off` does not
# prevent this.  The reliable order is a power cycle, then the flash, then
# PSRAM or a model.
flash: all
	@mkdir -p $(BUILD_DIR)
	@if [ -n "$(PORT)" ] && [ -w "$(PORT)" ]; then \
	    printf 'sleep off\r\n' > "$(PORT)" 2>/dev/null || true; \
	    sleep 1; \
	 fi
	@printf 'device %s\nif %s\nspeed %s\nconnect\nr\nh\nloadbin %s %s\n$(JLINK_RUN_SEQ)\n' "$(JLINK_DEVICE)" "$(JLINK_IF)" "$(JLINK_SPEED)" "$(TARGET_BIN)" "$(AMBIQ_LOAD_ADDR)" > $(JLINK_FLASH_SCRIPT)
	@echo "Flashing $(TARGET_BIN) -> MRAM $(AMBIQ_LOAD_ADDR) via $(JLINK) ($(JLINK_DEVICE))..."
	@$(JLINK) $(JLINK_SN_ARG) -CommanderScript $(JLINK_FLASH_SCRIPT) \
	    2>&1 | tee $(BUILD_DIR)/flash.log; \
	 if grep -qiE 'Verification failed|Failed to prepare|Could not connect|Error while programming' $(BUILD_DIR)/flash.log; then \
	     echo "*** FLASH FAILED -- the part still holds the PREVIOUS image."; \
	     echo "*** Anything tested now is testing stale firmware."; \
	     grep -iE 'Verification failed|Failed to prepare|Could not connect|Failed to halt|Error while programming' $(BUILD_DIR)/flash.log | head -4; \
	     echo "*** 'Failed to halt' -- J-Link could not stop the running"; \
	     echo "*** firmware.  Power cycle and flash before bringing"; \
	     echo "*** PSRAM or a model up; that is the only reliable order"; \
	     echo "*** found so far.  The part is probably wedged now."; \
	     exit 1; \
	 fi; \
	 if ! grep -qE 'Flash download: (Total|Program)' $(BUILD_DIR)/flash.log; then \
	     echo "*** FLASH FAILED -- no download happened at all."; \
	     echo "*** The part still holds the PREVIOUS image; anything tested"; \
	     echo "*** now is testing stale firmware."; \
	     grep -iE 'Failed to halt|Cannot connect|Timeout|Error' $(BUILD_DIR)/flash.log | head -4; \
	     exit 1; \
	 fi

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

# Nordic DK flash: two backends, both through the on-board SEGGER J-Link.
#   NRF_FLASH=jlink    JLinkExe `loadfile main.hex` into RRAM; any J-Link
#                      works, with J-Link software 8.10f or later
#   NRF_FLASH=nrfutil  nrfutil `device program` with a full chip erase; on a
#                      failure it runs `device recover` and programs again
#   NRF_FLASH=auto     [default] nrfutil when it is on PATH or at
#                      temp/nrfutil, else jlink
# Probe serial: JLINK_SN or NRF_SN (JLINK_SN first) picks one DK on a
# multi-probe rig.  JLINK_DEVICE_NORDIC is read only on the jlink path;
# nrfutil targets --core Application and needs no device name.
ifeq ($(MCU),nrf54lm20a)
JLINK_DEVICE_NORDIC ?= nRF54LM20A_M33
else ifeq ($(MCU),nrf54lm20b)
JLINK_DEVICE_NORDIC ?= nRF54LM20B_M33
else
JLINK_DEVICE_NORDIC ?= nRF54L15_M33
endif
JLINK_FLASH_SCRIPT   = $(BUILD_DIR)/flash.jlink
JLINK_ERASE_SCRIPT   = $(BUILD_DIR)/erase.jlink
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
	@# On the LM20-DK, AP-protect can re-latch: the debug port stops
	@# answering ("Setting the debug port SELECT register failed while
	@# powering up sys and debug regions") until `nrfutil device recover`
	@# erases the part.  When the first program attempt fails, the recipe
	@# runs a recover and programs again.
	@#
	@# A recover erases the whole chip, /data, the persist cells and the
	@# boot counter included, and the recipe prints a warning before it.
	@$(NRFUTIL_ENV) $(NRFUTIL) device program --firmware $(TARGET_HEX) \
		--core Application \
		--options chip_erase_mode=ERASE_ALL,reset=RESET_SYSTEM $(NRF_SN_ARG) \
	  || ( echo ""; \
	       echo "*** flash failed -- assuming AP-protect re-latched."; \
	       echo "*** RUNNING RECOVER: this ERASES the part (/data, persist"; \
	       echo "*** cells and the boot counter are lost), then reflashing."; \
	       echo ""; \
	       $(NRFUTIL_ENV) $(NRFUTIL) device recover $(NRF_SN_ARG); \
	       $(NRFUTIL_ENV) $(NRFUTIL) device program --firmware $(TARGET_HEX) \
	         --core Application \
	         --options chip_erase_mode=ERASE_ALL,reset=RESET_SYSTEM $(NRF_SN_ARG) )
	@# RESET_PIN after programming: the device stays in Debug Interface
	@# mode until a debug session ends and a pin reset follows (datasheet
	@# section 9.3), and that mode raises the idle current, so a power figure
	@# taken after a RESET_SYSTEM-only flash reads high.  A pin reset also
	@# resets the debug domain.
	$(NRFUTIL_ENV) $(NRFUTIL) device reset --reset-kind RESET_PIN $(NRF_SN_ARG)
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

else ifeq ($(TIKU_PLATFORM),stm32n6)

# Load over the ROM's DFU interface, which needs development boot (BOOT1 in
# position 2-3) and both USB-C ports connected.  -g starts the image, the
# ROM's DFU interface goes away, and the programmer reports a reconnect
# timeout.  The leading `-` makes make ignore the programmer's exit status,
# so a failed write also reports success.
flash: all
	@test -x "$(STM32N6_PROG)" || { $(call STM32N6_NEED_CUBE,flash); }
	-@$(STM32N6_PROG) -c port=usb1 -w $(TARGET_SIGNED) $(STM32N6_PART) \
	    -g $(STM32N6_PART)

run: flash

# dfu-reset hard-resets the part over SWD.  The image lives in SRAM and does
# not survive the reset, so the ROM comes back up with DFU ready for the
# next load.
dfu-reset:
	@test -x "$(STM32N6_PROG)" || { $(call STM32N6_NEED_CUBE,dfu-reset); }
	@$(STM32N6_PROG) -c port=SWD mode=UR -hardRst

erase:
	@echo "stm32n6: nothing to erase -- the image lives in SRAM, so a reset"
	@echo "  (make dfu-reset) already clears it."

else ifeq ($(TIKU_PLATFORM),ra8p1)

# EK-RA8P1 over its on-board J-Link OB.  The image links into MRAM at
# 0x02000000 (r7ka8p1kf.ld), where the CPU boots from out of reset.
JLINK_DEVICE_RA8P1 ?= R7KA8P1KF
RA8P1_JLINK_SCRIPT  = $(BUILD_DIR)/flash.jlink

# flash resets and halts the CPU (`r`, `h`) so no byte lands under running
# code, programs main.elf into MRAM with `loadfile`, then resets into it
# (`r`, `go`).  JLinkExe exits 0 whatever happens, so the recipe fails when
# the log names a known failure or shows no download ("Flash download",
# "O.K." or "Download ... complete").
flash: all
	@mkdir -p $(BUILD_DIR)
	@printf 'device %s\nif %s\nspeed %s\nconnect\nr\nh\nloadfile %s\nr\ngo\nqc\n' \
	    "$(JLINK_DEVICE_RA8P1)" "$(JLINK_IF)" "$(JLINK_SPEED)" "$(TARGET)" \
	    > $(RA8P1_JLINK_SCRIPT)
	@echo "Programming $(TARGET) -> MRAM via $(JLINK) ($(JLINK_DEVICE_RA8P1))..."
	@$(JLINK) $(JLINK_SN_ARG) -CommanderScript $(RA8P1_JLINK_SCRIPT) \
	    2>&1 | tee $(BUILD_DIR)/flash.log; \
	 if grep -qiE 'Verification failed|Failed to prepare|Could not connect|Error while programming' $(BUILD_DIR)/flash.log; then \
	     echo "*** FLASH FAILED -- the part still holds the PREVIOUS image."; \
	     echo "*** Anything tested now is testing stale firmware."; \
	     grep -iE 'Verification failed|Failed to prepare|Could not connect|Failed to halt|Error while programming' $(BUILD_DIR)/flash.log | head -4; \
	     exit 1; \
	 fi; \
	 if ! grep -qiE 'Flash download: (Total|Program)|O\.K\.|Download.*complete' $(BUILD_DIR)/flash.log; then \
	     echo "*** LOAD FAILED -- no positive evidence any bytes moved."; \
	     echo "*** JLinkExe exits 0 whatever happens, so this check, not"; \
	     echo "*** its status, is what says the image is on the part."; \
	     grep -iE 'Cannot connect|Failed to halt|Error|Timeout' $(BUILD_DIR)/flash.log | head -4; \
	     exit 1; \
	 fi

run: flash

debug: all
	@echo "Debug: start a GDB server against the kit's J-Link OB with"
	@echo "  $(JLINK_GDB) -device $(JLINK_DEVICE_RA8P1) -if $(JLINK_IF) -speed $(JLINK_SPEED)"

erase:
	@echo "ra8p1: nothing to erase -- the image lives in SRAM, so a power"
	@echo "  cycle already clears it and the factory MRAM image boots."

else ifeq ($(TIKU_PLATFORM),esp32c61)

# flash writes the image at flash offset 0, which the ROM loads into SRAM at
# every reset as it would a bootloader: the board then boots tikuOS on its
# own, and whatever was at offset 0 is replaced.  RAM=1 loads the image into
# SRAM through the ROM loader and runs it, leaving offset 0 alone; an image
# with XIP sections still writes xip.bin to flash at 1 MB first.  ESP_PORT
# names the port: by default the chip's own USB-Serial/JTAG (303a:1001),
# else the CP2102N console bridge (10c4:ea60), whose RTS and DTR esptool
# drives.
ESP_PORT ?= $(firstword \
    $(shell python3 -m serial.tools.list_ports -q 303A:1001 2>/dev/null) \
    $(shell python3 -m serial.tools.list_ports -q 10C4:EA60 2>/dev/null))
flash: all
	@test -n "$(ESP_PORT)" || { \
	    echo "esp32c61: no Espressif USB-Serial/JTAG or CP2102N port found;"; \
	    echo "  plug in either connector, or name it: make flash ESP_PORT=..."; \
	    exit 1; }
	@xip=""; if $(SIZE) -A $(TARGET) | grep -q '^\.xip'; then \
	    xip="$(XIP_FLASH_OFFSET) $(TARGET_XIP)"; fi; \
	if [ "$(RAM)" = "1" ]; then \
	    if [ -n "$$xip" ]; then \
	        $(ESPTOOL) --chip esp32c61 -p $(ESP_PORT) write-flash \
	            --flash-mode dio --flash-size 8MB $$xip || exit 1; \
	    fi; \
	    $(ESPTOOL) --chip esp32c61 -p $(ESP_PORT) --no-stub load-ram \
	        $(TARGET_BIN); \
	else \
	    $(ESPTOOL) --chip esp32c61 -p $(ESP_PORT) write-flash \
	        --flash-mode dio --flash-size 8MB 0x0 $(TARGET_BIN) $$xip; \
	fi

run: flash

erase:
	@echo "esp32c61: not erased -- write-flash replaces the boot image, and"
	@echo "  erasing /data or the durable mirror is not a build step."

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
# Serial Monitor  (auto-detects the port, picks picocom or screen)
# ---------------------------------------------------------------------------
# Monitor baud: UART_BAUD when set, else 9600 on MSP430 and 115200 on every
# other platform.
ifeq ($(TIKU_PLATFORM),msp430)
BAUD ?= $(if $(UART_BAUD),$(UART_BAUD),9600)
else
BAUD ?= $(if $(UART_BAUD),$(UART_BAUD),115200)
endif

# Serial port search, first match wins:
#   1. /dev/ttyUSB* or macOS /dev/cu.usbserial*: an external FTDI or CP2102
#      adapter, which SLIP networking uses;
#   2. /dev/ttyACM* with a TI USB vendor ID (0451 or 2047): the eZ-FET
#      backchannel, which resets the target each time the port is opened;
#   3. any /dev/ttyACM* (the SEGGER J-Link VCOM, vendor 1366, among them)
#      or macOS /dev/cu.usbmodem*.
# Each glob is tested with [ -e ]: an unmatched glob stays literal and
# fails the test, on Linux and macOS alike.
PORT ?= $(shell \
	for p in /dev/ttyUSB* /dev/cu.usbserial*; do \
		[ -e "$$p" ] && { echo "$$p"; exit 0; }; \
	done; \
	for dev in /dev/ttyACM*; do \
		[ -e "$$dev" ] || continue; \
		vid=$$(cat "/sys/class/tty/$$(basename $$dev)/device/../idVendor" 2>/dev/null); \
		if [ "$$vid" = "0451" ] || [ "$$vid" = "2047" ]; then echo "$$dev"; exit 0; fi; \
	done; \
	for p in /dev/ttyACM* /dev/cu.usbmodem*; do \
		[ -e "$$p" ] && { echo "$$p"; exit 0; }; \
	done)
# macOS: /dev/cu.* not /dev/tty.*.  A tty.* node blocks on open until
# carrier detect, which a USB CDC console never asserts, so a write to it
# (the Ambiq flash rule's `sleep off`) hangs; a cu.* node opens at once.
# With two boards connected the search returns the first port it finds;
# PORT= names the port of the board meant.

monitor:
	@if [ -z "$(PORT)" ]; then \
		echo "Error: No serial port found (/dev/ttyUSB* or /dev/ttyACM*)"; \
		echo "  Plug in the USB-serial adapter / LaunchPad, or the board's"; \
		echo "  J-Link VCOM (shows up as /dev/ttyACM*)."; \
		echo "  Or point it at a specific port: make monitor PORT=/dev/ttyACM0"; \
		exit 1; \
	fi; \
	echo "Baud $(BAUD), from MCU=$(MCU) -- monitor needs MCU= like every"; \
	echo "  other target, and forgetting it is silent: a bare \`make\` picks"; \
	echo "  msp430fr5994 (9600) and a 115200 board then prints mojibake that"; \
	echo "  reads like broken hardware.  Garbled? \`make monitor MCU=<yours>\`"; \
	echo "  or override BAUD= directly."; \
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

# ---------------------------------------------------------------------------
# `make options`: the build knobs a tool may offer, resolved for this
# configuration (tools/firmware_options.mk).  Included last, after every
# default above is set.
# ---------------------------------------------------------------------------
include tools/firmware_options.mk
