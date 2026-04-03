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
# App selection (mutually exclusive with tests and examples)
#   make APP=cli MCU=msp430fr5969   — build with CLI app
#   make MCU=msp430fr5969           — default (tests/examples as before)
# ---------------------------------------------------------------------------
APP ?=

# ---------------------------------------------------------------------------
# Shell service (kernel service — orthogonal to APP/tests/examples)
#   make TIKU_SHELL_ENABLE=1 MCU=msp430fr5969   — build with shell
#   make APP=cli MCU=msp430fr5969                — legacy alias (also enables shell)
# ---------------------------------------------------------------------------
TIKU_SHELL_ENABLE ?= 0
TIKU_INIT_ENABLE  ?= 0

# Legacy: APP=cli enables the kernel shell
ifeq ($(APP),cli)
TIKU_SHELL_ENABLE = 1
endif

# Init system requires the shell (for parser)
ifeq ($(TIKU_INIT_ENABLE),1)
TIKU_SHELL_ENABLE = 1
endif

# ---------------------------------------------------------------------------
# Optional components (auto-detected; override: make HAS_TESTS=0 etc.)
# When APP is set, tests and examples are excluded automatically.
# ---------------------------------------------------------------------------
ifneq ($(APP),)
HAS_APPS         = 1
HAS_TESTS        = 0
HAS_EXAMPLES     = 0
else
HAS_APPS         = 0
HAS_TESTS        ?= $(if $(wildcard $(PROJ_DIR)/tests/test_runner.c),1,0)
HAS_EXAMPLES     ?= $(if $(wildcard $(PROJ_DIR)/examples/tiku_example_config.h),1,0)
endif
HAS_TIKUKITS     ?= $(if $(wildcard $(PROJ_DIR)/tikukits),1,0)
HAS_PRESENTATION ?= $(if $(wildcard $(PROJ_DIR)/presentation/Makefile),1,0)

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
CFLAGS  = -mmcu=$(MCU) -Os -Wall -Wextra
CFLAGS += -D$(DEVICE_DEFINE)=1
CFLAGS += -DPLATFORM_MSP430=1
CFLAGS += -I$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
CFLAGS += -I$(MSP430_SUPPORT_DIR)
endif
CFLAGS += -I$(PROJ_DIR)
CFLAGS += -ffunction-sections -fdata-sections

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
endif
ifeq ($(HAS_EXAMPLES),1)
CFLAGS += -DHAS_EXAMPLES=1
endif
ifeq ($(HAS_TIKUKITS),1)
CFLAGS += -DHAS_TIKUKITS=1
endif

LDFLAGS  = -mmcu=$(MCU)
LDFLAGS += -L$(TOOLCHAIN_DIR)/include
ifneq ($(MSP430_SUPPORT_DIR),)
LDFLAGS += -L$(MSP430_SUPPORT_DIR)
endif
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-u,tiku_autostart_processes

# ---------------------------------------------------------------------------
# Source files — core OS (always compiled)
# ---------------------------------------------------------------------------
SRCS  = main.c
SRCS += arch/msp430/tiku_cpu_common.c
SRCS += arch/msp430/tiku_cpu_freq_boot_arch.c
SRCS += arch/msp430/tiku_cpu_watchdog_arch.c
SRCS += arch/msp430/tiku_htimer_arch.c
SRCS += arch/msp430/tiku_i2c_arch.c
SRCS += arch/msp430/tiku_adc_arch.c
SRCS += arch/msp430/tiku_onewire_arch.c
SRCS += arch/msp430/tiku_timer_arch.c
SRCS += arch/msp430/tiku_uart_arch.c
SRCS += arch/msp430/tiku_mem_arch.c
SRCS += arch/msp430/tiku_mpu_arch.c
SRCS += arch/msp430/tiku_region_arch.c
SRCS += boot/tiku_boot.c
SRCS += hal/tiku_cpu.c
SRCS += kernel/cpu/tiku_common.c
SRCS += kernel/cpu/tiku_watchdog.c
SRCS += kernel/timers/tiku_clock.c
SRCS += kernel/timers/tiku_htimer.c
SRCS += kernel/timers/tiku_timer.c
SRCS += interfaces/bus/tiku_i2c_bus.c
SRCS += interfaces/adc/tiku_adc.c
SRCS += interfaces/onewire/tiku_onewire.c
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
SRCS += kernel/scheduler/tiku_sched.c
SRCS += server/vfs/tiku_vfs.c
SRCS += server/vfs/tiku_vfs_tree.c

# ---------------------------------------------------------------------------
# Shell (kernel service — compiled when TIKU_SHELL_ENABLE=1)
# ---------------------------------------------------------------------------
ifeq ($(TIKU_SHELL_ENABLE),1)
CFLAGS += -DTIKU_SHELL_ENABLE=1
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
SRCS += kernel/shell/commands/tiku_shell_cmd_history.c
SRCS += kernel/shell/commands/tiku_shell_cmd_ls.c
SRCS += kernel/shell/tiku_shell_cwd.c
SRCS += kernel/shell/commands/tiku_shell_cmd_cd.c
SRCS += kernel/shell/commands/tiku_shell_cmd_toggle.c
endif

# ---------------------------------------------------------------------------
# Init system (FRAM-backed configurable boot — requires shell)
# ---------------------------------------------------------------------------
ifeq ($(TIKU_INIT_ENABLE),1)
CFLAGS += -DTIKU_INIT_ENABLE=1
SRCS += kernel/memory/tiku_fram_map.c
SRCS += kernel/init/tiku_init.c
SRCS += kernel/shell/commands/tiku_shell_cmd_init.c
endif

# ---------------------------------------------------------------------------
# Tests (only if tests/ is present)
# ---------------------------------------------------------------------------
ifeq ($(HAS_TESTS),1)
SRCS += tests/test_runner.c
SRCS += tests/uart/test_uart_init.c
SRCS += tests/uart/test_uart_tx_binary.c
SRCS += tests/uart/test_uart_loopback.c
SRCS += tests/uart/test_uart_ringbuf.c
SRCS += tests/uart/test_uart_overrun.c
SRCS += tests/uart/test_uart_slip_bytes.c
SRCS += tests/uart/test_uart_stress.c
SRCS += tests/uart/test_uart_capacity.c
SRCS += tests/uart/test_uart_overrun_provoke.c
SRCS += tests/uart/test_uart_slip_frame.c
SRCS += tests/uart/test_uart_duplex.c
SRCS += tests/uart/test_uart_isr_contention.c
SRCS += tests/uart/test_uart_ezfet_challenge.c
SRCS += tests/cpuclock/test_cpuclock_basic.c
SRCS += tests/cpuclock/test_clock_edge.c
SRCS += tests/memory/test_mem_common.c
SRCS += tests/memory/test_mem_arena.c
SRCS += tests/memory/test_mem_persist.c
SRCS += tests/memory/test_mem_mpu.c
SRCS += tests/memory/test_mem_pool.c
SRCS += tests/memory/test_mem_region.c
SRCS += tests/memory/test_mem_edge.c
SRCS += tests/memory/test_mem_tier.c
SRCS += tests/memory/test_mem_cache.c
SRCS += tests/memory/test_mem_hibernate.c
SRCS += tests/memory/test_mem_proc_mem.c
SRCS += tests/process/test_process_lifecycle.c
SRCS += tests/process/test_process_events.c
SRCS += tests/process/test_process_yield.c
SRCS += tests/process/test_process_broadcast.c
SRCS += tests/process/test_process_poll.c
SRCS += tests/process/test_process_queue.c
SRCS += tests/process/test_process_local.c
SRCS += tests/process/test_process_broadcast_exit.c
SRCS += tests/process/test_process_graceful_exit.c
SRCS += tests/process/test_process_current_cleared.c
SRCS += tests/process/test_process_edge.c
SRCS += tests/process/test_process_channel.c
SRCS += tests/process/test_process_observe.c
SRCS += tests/scheduler/test_sched.c
SRCS += tests/timer/test_timer_event.c
SRCS += tests/timer/test_timer_callback.c
SRCS += tests/timer/test_timer_periodic.c
SRCS += tests/timer/test_timer_stop.c
SRCS += tests/timer/test_htimer_basic.c
SRCS += tests/timer/test_htimer_periodic.c
SRCS += tests/timer/test_timer_edge.c
SRCS += tests/timer/test_htimer_edge.c
SRCS += tests/watchdog/test_watchdog_basic.c
SRCS += tests/watchdog/test_watchdog_pause_resume.c
SRCS += tests/watchdog/test_watchdog_interval.c
SRCS += tests/watchdog/test_watchdog_timeout.c
SRCS += tests/uart/test_uart_edge.c
SRCS += tests/watchdog/test_watchdog_edge.c
SRCS += tests/server/vfs/test_vfs.c

# TikuKits tests (requires both test framework and tikukits library)
ifeq ($(HAS_TIKUKITS),1)
SRCS += $(wildcard tests/kits/maths/*.c)
SRCS += $(wildcard tests/kits/sensors/*.c)
SRCS += $(wildcard tests/kits/sigfeatures/*.c)
SRCS += $(wildcard tests/kits/textcompression/*.c)
SRCS += $(wildcard tests/kits/ml/*.c)
SRCS += $(wildcard tests/kits/ds/*.c)
SRCS += $(wildcard tests/kits/net/*.c)
SRCS += $(wildcard tests/kits/codec/*.c)
SRCS += $(filter-out tests/kits/crypto/test_kits_crypto_tls.c, \
          $(wildcard tests/kits/crypto/*.c))
ifeq ($(HAS_TLS),1)
SRCS += tests/kits/crypto/test_kits_crypto_tls.c
endif
endif
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

# TikuKits examples (requires both examples/ and tikukits/)
ifeq ($(HAS_TIKUKITS),1)
SRCS += examples/kits/example_kits_runner.c
SRCS += $(wildcard examples/kits/maths/*.c)
SRCS += $(wildcard examples/kits/ds/*.c)
SRCS += $(wildcard examples/kits/ml/*.c)
SRCS += $(wildcard examples/kits/sensors/*.c)
SRCS += $(wildcard examples/kits/sigfeatures/*.c)
SRCS += $(wildcard examples/kits/textcompression/*.c)
SRCS += $(filter-out examples/kits/net/example_net_tls.c, \
          $(wildcard examples/kits/net/*.c))
ifeq ($(HAS_TLS),1)
SRCS += examples/kits/net/example_net_tls.c
endif
endif
endif

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

# ---------------------------------------------------------------------------
# TikuKits (only if tikukits/ is present)
# ---------------------------------------------------------------------------
ifeq ($(HAS_TIKUKITS),1)
SRCS += $(wildcard tikukits/maths/linear_algebra/*.c)
SRCS += $(wildcard tikukits/maths/statistics/*.c)
SRCS += $(wildcard tikukits/maths/distance/*.c)
SRCS += $(wildcard tikukits/sensors/temperature/*.c)
SRCS += $(wildcard tikukits/sigfeatures/peak/*.c)
SRCS += $(wildcard tikukits/sigfeatures/zcr/*.c)
SRCS += $(wildcard tikukits/sigfeatures/histogram/*.c)
SRCS += $(wildcard tikukits/sigfeatures/delta/*.c)
SRCS += $(wildcard tikukits/sigfeatures/goertzel/*.c)
SRCS += $(wildcard tikukits/sigfeatures/zscore/*.c)
SRCS += $(wildcard tikukits/sigfeatures/scale/*.c)
SRCS += $(wildcard tikukits/sigfeatures/ema/*.c)
SRCS += $(wildcard tikukits/sigfeatures/median/*.c)
SRCS += $(wildcard tikukits/sigfeatures/skip/*.c)
SRCS += $(wildcard tikukits/textcompression/rle/*.c)
SRCS += $(wildcard tikukits/textcompression/bpe/*.c)
SRCS += $(wildcard tikukits/textcompression/heatshrink/*.c)
SRCS += $(wildcard tikukits/ml/regression/*.c)
SRCS += $(wildcard tikukits/ml/classification/*.c)
SRCS += $(wildcard tikukits/ds/array/*.c)
SRCS += $(wildcard tikukits/ds/queue/*.c)
SRCS += $(wildcard tikukits/ds/pqueue/*.c)
SRCS += $(wildcard tikukits/ds/stack/*.c)
SRCS += $(wildcard tikukits/ds/ringbuf/*.c)
SRCS += $(wildcard tikukits/ds/bitmap/*.c)
SRCS += $(wildcard tikukits/ds/list/*.c)
SRCS += $(wildcard tikukits/ds/btree/*.c)
SRCS += $(wildcard tikukits/ds/sortarray/*.c)
SRCS += $(wildcard tikukits/ds/htable/*.c)
SRCS += $(wildcard tikukits/ds/sm/*.c)
SRCS += $(wildcard tikukits/ds/bloom/*.c)
SRCS += $(wildcard tikukits/ds/circlog/*.c)
SRCS += $(wildcard tikukits/ds/deque/*.c)
SRCS += $(wildcard tikukits/ds/trie/*.c)
SRCS += $(wildcard tikukits/ds/timerwheel/*.c)
SRCS += $(wildcard tikukits/net/slip/*.c)
SRCS += $(wildcard tikukits/net/ipv4/*.c)
SRCS += $(wildcard tikukits/net/http/*.c)
SRCS += $(wildcard tikukits/net/mqtt/*.c)
SRCS += $(wildcard tikukits/time/*.c)
SRCS += $(wildcard tikukits/time/ntp/*.c)
SRCS += $(wildcard tikukits/codec/cbor/*.c)
SRCS += $(wildcard tikukits/codec/json/*.c)
SRCS += $(wildcard tikukits/codec/protobuf/*.c)
SRCS += $(wildcard tikukits/codec/hex/*.c)
SRCS += $(wildcard tikukits/crypto/aes128/*.c)
SRCS += $(wildcard tikukits/crypto/sha256/*.c)
SRCS += $(wildcard tikukits/crypto/hmac/*.c)
SRCS += $(wildcard tikukits/crypto/crc/*.c)
SRCS += $(wildcard tikukits/crypto/base64/*.c)
SRCS += $(wildcard tikukits/crypto/hkdf/*.c)
SRCS += $(wildcard tikukits/crypto/gcm/*.c)
# TLS requires TIKU_KITS_CRYPTO_TLS_RNG_FILL; compile only with HAS_TLS=1
ifeq ($(HAS_TLS),1)
SRCS += $(wildcard tikukits/crypto/tls/*.c)
endif

endif

# Object files in build directory
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
TARGET = main.elf

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.SUFFIXES:
.PHONY: all clean flash run debug erase size monitor deploy docs docs-clean

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
flash: all
	$(MSPDEBUG) $(DEBUGGER) "prog $(TARGET)"

run: flash

debug: all
	$(MSPDEBUG) $(DEBUGGER) "gdb"

erase:
	$(MSPDEBUG) $(DEBUGGER) "erase"

deploy: clean flash monitor

# ---------------------------------------------------------------------------
# Serial Monitor  (auto-detects TI LaunchPad, picks picocom or screen)
# ---------------------------------------------------------------------------
BAUD ?= $(if $(UART_BAUD),$(UART_BAUD),9600)

# Auto-detect serial port.
# Prefer /dev/ttyUSB* (FTDI/CP2102 external adapter) over ttyACM*
# (eZ-FET backchannel) since external adapters are used for SLIP
# networking and avoid the eZ-FET DTR-reset bug.
PORT ?= $(shell \
	ls /dev/ttyUSB* 2>/dev/null | head -1 || \
	(for dev in /dev/ttyACM*; do \
		[ -e "$$dev" ] || continue; \
		vid=$$(cat "/sys/class/tty/$$(basename $$dev)/device/../idVendor" 2>/dev/null); \
		if [ "$$vid" = "0451" ] || [ "$$vid" = "2047" ]; then echo "$$dev"; exit 0; fi; \
	done; \
	ls /dev/ttyACM* 2>/dev/null | head -1))

monitor:
	@if [ -z "$(PORT)" ]; then \
		echo "Error: No serial port found (/dev/ttyUSB* or /dev/ttyACM*)"; \
		echo "  Is the FTDI adapter or LaunchPad plugged in?"; \
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
