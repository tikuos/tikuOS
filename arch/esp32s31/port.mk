# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# port.mk - Build native S31 kernel qualification in SRAM.
#
# SPDX-License-Identifier: Apache-2.0

BOARD ?= esp32s31_korvo1
ifneq ($(BOARD),esp32s31_korvo1)
$(error ESP32-S31 requires BOARD=esp32s31_korvo1)
endif
S31_PROFILE ?= kernel-test
ifeq ($(filter $(S31_PROFILE),kernel-test board-test shell watchdog-test flash-shell psram-test flash-psram-test),)
$(error S31_PROFILE must be kernel-test, board-test, shell, watchdog-test, flash-shell, psram-test or flash-psram-test)
endif
S31_ENABLED := $(strip $(foreach v,$(filter TIKU_%_ENABLE,$(.VARIABLES)),\
    $(if $(filter 1,$($(v))),$(v))))
ifneq ($(filter-out TIKU_THREADS_ENABLE,$(S31_ENABLED)),)
$(error S31 profiles select optional services through S31_PROFILE)
endif
ifdef TIKU_THREADS_ENABLE
ifneq ($(TIKU_THREADS_ENABLE),1)
$(error S31 profiles require TIKU_THREADS_ENABLE=1)
endif
endif
CROSS_COMPILE ?= riscv32-esp-elf-
PYTHON ?= python3
S31_CC := $(CROSS_COMPILE)gcc
S31_DIR := build/esp32s31/$(S31_PROFILE)
S31_ARCH := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
S31_FLAGS := $(S31_ARCH) -Os -g3 -Wall -Wextra -Werror \
    -Wno-missing-field-initializers -ffreestanding -fno-builtin \
    -ffunction-sections -fdata-sections -I. -DPLATFORM_ESP32S31=1 \
    -DTIKU_BOARD_ESP32S31_KORVO1=1 -DTIKU_THREADS_ENABLE=1 \
    -DTIKU_MEM_NVM_ENABLE=0
S31_SRCS := arch/esp32s31/tiku_runtime_arch.c arch/esp32s31/tiku_watchdog_arch.c \
    arch/esp32s31/tiku_i2c_arch.c interfaces/bus/tiku_i2c_bus.c \
    interfaces/gpio/tiku_gpio_owner.c \
    kernel/process/tiku_process.c kernel/scheduler/tiku_sched.c \
    kernel/timers/tiku_clock.c kernel/timers/tiku_timer.c \
    kernel/timers/tiku_htimer.c kernel/timers/tiku_crit.c \
    kernel/threads/tiku_thread.c kernel/vfs/tiku_vfs.c \
    kernel/cpu/tiku_hang.c
ifneq ($(filter shell flash-shell,$(S31_PROFILE)),)
S31_SHELL_COMMANDS := HELP PS TIMER QUEUE ECHO CALC HTIMER CLEAR
S31_SHELL_FLAGS := $(shell awk '$$1 == sprintf("%cifndef",35) && $$2 ~ /^TIKU_SHELL_CMD_/ {print $$2}' shell/tiku_shell_config.h)
S31_FLAGS += -DTIKU_SHELL_ENABLE=1 -DTIKU_SHELL_ALIASES_ENABLE=0 \
    $(foreach v,$(sort $(S31_SHELL_FLAGS)),-D$(v)=$(if $(filter $(patsubst TIKU_SHELL_CMD_%,%,$(v)),$(S31_SHELL_COMMANDS)),1,0))
S31_SRCS += kernel/cpu/tiku_watchdog.c kernel/memory/tiku_mem.c boot/tiku_s31_ram.c shell/tiku_shell.c shell/tiku_shell_io.c \
    shell/tiku_shell_parser.c shell/tiku_shell_cwd.c services/console/tiku_console.c \
    $(foreach c,ps timer queue echo calc clear,shell/commands/tiku_shell_cmd_$(c).c)
else ifneq ($(filter psram-test flash-psram-test,$(S31_PROFILE)),)
S31_FLAGS += -DTIKU_S31_PSRAM_TEST=1
S31_SRCS += arch/esp32s31/tiku_psram_arch.c TikuBench/tests/esp32s31/test_psram.c
else ifeq ($(S31_PROFILE),watchdog-test)
S31_SRCS += TikuBench/tests/esp32s31/test_watchdog.c
else
S31_SRCS += TikuBench/tests/esp32s31/test_kernel.c TikuBench/tests/esp32s31/test_i2c.c
endif
ifeq ($(S31_PROFILE),board-test)
ifeq ($(wildcard drivers/input/gt1151/tiku_drv_input_gt1151.c),)
$(error board-test requires the optional drivers/input/gt1151 checkout)
endif
S31_FLAGS += -DS31_TOUCH_TEST=1
S31_SRCS += drivers/input/gt1151/tiku_drv_input_gt1151.c
endif
S31_LINKER := arch/esp32s31/diagnostic.ld
ifneq ($(filter flash-shell flash-psram-test,$(S31_PROFILE)),)
S31_FLAGS += -DTIKU_S31_FLASH_BOOT=1
S31_LINKER := arch/esp32s31/flash.ld
S31_SRCS += arch/esp32s31/tiku_image_desc.c
endif
S31_ASM := arch/esp32s31/tiku_crt_arch.S
S31_OBJS := $(patsubst %.c,$(S31_DIR)/%.o,$(S31_SRCS)) \
    $(patsubst %.S,$(S31_DIR)/%.o,$(S31_ASM))
.PHONY: all ram-load clean FORCE
all: $(S31_DIR)/kernel.bin
$(S31_DIR)/.flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(S31_CC) $(S31_FLAGS)' > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new
$(S31_DIR)/%.o: %.c $(S31_DIR)/.flags
	@mkdir -p $(@D)
	$(S31_CC) $(S31_FLAGS) -MMD -MP -c $< -o $@
$(S31_DIR)/%.o: %.S $(S31_DIR)/.flags
	@mkdir -p $(@D)
	$(S31_CC) $(S31_FLAGS) -MMD -MP -c $< -o $@
$(S31_DIR)/kernel.elf: $(S31_OBJS) $(S31_LINKER) arch/esp32s31/port.mk
	$(S31_CC) $(S31_ARCH) -nostdlib -Wl,--gc-sections,--build-id=none \
	    -Wl,-Map=$(S31_DIR)/kernel.map -T$(S31_LINKER) \
	    $(S31_OBJS) -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group -o $@
$(S31_DIR)/kernel.bin: $(S31_DIR)/kernel.elf
	$(PYTHON) -m esptool --chip esp32s31 elf2image --flash-size 16MB -o $@ $<
ram-load: all
ifneq ($(filter flash-shell flash-psram-test,$(S31_PROFILE)),)
	$(error flash profiles require the installed second-stage bootloader)
endif
	$(PYTHON) tools/esp32s31_ram.py --image $(S31_DIR)/kernel.bin \
	    --port '$(PORT)' --serial '$(S31_SERIAL)' --$(if $(filter shell flash-shell,$(S31_PROFILE)),shell-test,$(if $(filter watchdog-test board-test psram-test,$(S31_PROFILE)),$(S31_PROFILE),kernel-test)) --seconds $(if $(filter psram-test,$(S31_PROFILE)),60,6) --log '$(LOG)'
clean:
	$(RM) $(S31_OBJS) $(S31_OBJS:.o=.d) $(S31_DIR)/kernel.elf \
	    $(S31_DIR)/kernel.bin $(S31_DIR)/kernel.map $(S31_DIR)/.flags
-include $(S31_OBJS:.o=.d)
