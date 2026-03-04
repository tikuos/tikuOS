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

# ---------------------------------------------------------------------------
# Toolchain  (auto-detected from PATH; override with TOOLCHAIN_DIR=…)
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Debug / Flash tools  (auto-detected from PATH; override with MSPDEBUG=…)
# ---------------------------------------------------------------------------
MSPDEBUG ?= $(shell command -v mspdebug 2>/dev/null || echo mspdebug)
DEBUGGER  = tilib

# ---------------------------------------------------------------------------
# Target MCU  (override on command line: make MCU=msp430fr5969 or mcu=msp430fr5969)
# Accepts uppercase (MSP430FR5969) or lowercase (msp430fr5969) MCU names.
# ---------------------------------------------------------------------------
MCU ?= $(mcu)
ifeq ($(MCU),)
MCU = msp430fr2433
endif
MCU := $(shell echo $(MCU) | tr '[:upper:]' '[:lower:]')

# Derive device define from MCU: msp430fr2433 -> TIKU_DEVICE_MSP430FR2433
DEVICE_UPPER = $(shell echo $(MCU) | tr '[:lower:]' '[:upper:]')
DEVICE_DEFINE = TIKU_DEVICE_$(DEVICE_UPPER)

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
PROJ_DIR  = $(CURDIR)
BUILD_DIR = build/$(MCU)

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
CFLAGS  = -mmcu=$(MCU) -O2 -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -DPLATFORM_MSP430=1
CFLAGS += -I$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
CFLAGS += -I$(MSP430_SUPPORT_DIR)
endif
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections

LDFLAGS  = -mmcu=$(MCU)
LDFLAGS += -L$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
LDFLAGS += -L$(MSP430_SUPPORT_DIR)
endif
LDFLAGS += -Wl,--gc-sections

# ---------------------------------------------------------------------------
# Source files  (core OS — examples are excluded)
# ---------------------------------------------------------------------------
SRCS  = main.c
SRCS += arch/msp430/tiku_cpu_common.c
SRCS += arch/msp430/tiku_cpu_freq_boot_arch.c
SRCS += arch/msp430/tiku_cpu_watchdog_arch.c
SRCS += arch/msp430/tiku_htimer_arch.c
SRCS += arch/msp430/tiku_timer_arch.c
SRCS += arch/msp430/tiku_uart_arch.c
SRCS += arch/msp430/tiku_mem_arch.c
SRCS += boot/tiku_boot.c
SRCS += hal/tiku_cpu.c
SRCS += kernel/cpu/tiku_common.c
SRCS += kernel/cpu/tiku_watchdog.c
SRCS += kernel/timers/tiku_clock.c
SRCS += kernel/timers/tiku_htimer.c
SRCS += kernel/timers/tiku_timer.c
SRCS += kernel/memory/tiku_mem.c
SRCS += kernel/process/tiku_process.c
SRCS += kernel/scheduler/tiku_sched.c
SRCS += tests/test_cpuclock.c
SRCS += tests/test_process.c
SRCS += tests/test_runner.c
SRCS += tests/test_timer.c
SRCS += tests/test_tiku_mem.c
SRCS += tests/test_watchdog.c
SRCS += examples/01_blink/blink.c
SRCS += examples/02_dual_blink/dual_blink.c
SRCS += examples/03_button_led/button_led.c
SRCS += examples/04_multi_process/multi_process.c
SRCS += examples/05_state_machine/state_machine.c
SRCS += examples/06_callback_timer/callback_timer.c
SRCS += examples/07_broadcast/broadcast.c
SRCS += examples/08_timeout/timeout.c
SRCS += examples/09_channel/channel.c

# Object files in build directory
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
TARGET = main.elf

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all clean flash run debug erase size monitor docs docs-clean

all: $(TARGET) size

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

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
	@$(MAKE) -C presentation --no-print-directory

docs-clean:
	@$(MAKE) -C presentation distclean --no-print-directory

# ---------------------------------------------------------------------------
# Flash / Debug / Erase
# ---------------------------------------------------------------------------
flash: all
	$(MSPDEBUG) $(DEBUGGER) "prog $(TARGET)"

run: flash

debug: all
	$(MSPDEBUG) $(DEBUGGER) "gdb"

erase:
	$(MSPDEBUG) $(DEBUGGER) "erase"

# ---------------------------------------------------------------------------
# Serial Monitor  (auto-detects TI LaunchPad, picks picocom or screen)
# ---------------------------------------------------------------------------
BAUD ?= 9600

# Auto-detect: prefer /dev/ttyACM* with TI vendor ID (0451/2047),
# fall back to first /dev/ttyACM* present.
PORT ?= $(shell \
	for dev in /dev/ttyACM*; do \
		[ -e "$$dev" ] || continue; \
		vid=$$(cat "/sys/class/tty/$$(basename $$dev)/device/../idVendor" 2>/dev/null); \
		if [ "$$vid" = "0451" ] || [ "$$vid" = "2047" ]; then echo "$$dev"; exit 0; fi; \
	done; \
	ls /dev/ttyACM* 2>/dev/null | head -1)

monitor:
	@if [ -z "$(PORT)" ]; then \
		echo "Error: No serial port found (/dev/ttyACM*)"; \
		echo "  Is the LaunchPad plugged in?"; \
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
