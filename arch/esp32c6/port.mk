# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# port.mk - ESP32-C6 native kernel in a resident-loader SRAM arena.
#
# SPDX-License-Identifier: Apache-2.0
C6_PROFILE ?= resident
BOARD ?= firebeetle2_esp32p4_c6
ifneq ($(C6_PROFILE),resident)
$(error ESP32-C6 supports C6_PROFILE=resident)
endif
ifneq ($(BOARD),firebeetle2_esp32p4_c6)
$(error ESP32-C6 resident profile requires BOARD=firebeetle2_esp32p4_c6)
endif
ROOT := $(CURDIR)
BUILD_DIR ?= build/esp32c6/resident
C6_RAM_BASE ?= 0x4080a000
C6_BUILD ?= 1
C6_PYTHON ?= python3
C6_BANK ?= 0
C6_UPDATE_LOG_DIR ?= $(BUILD_DIR)/update-$(shell date +%Y%m%dT%H%M%S)
TOOLCHAIN_DIR ?= $(ROOT)/temp/toolchains/xpack-riscv-none-elf-gcc-15.2.0-1
TOOLCHAIN_PREFIX ?= riscv-none-elf-
CC := $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)objcopy
SIZE := $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)size
CFLAGS := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -Os -g -Wall -Wextra -Werror \
 -ffreestanding -fno-builtin -fno-pic -ffunction-sections -fdata-sections \
 -DTIKU_CLOCK_ARCH_SECOND=128UL -DTIKU_LC_PERSISTENT=0 -DTIKU_THREADS_ENABLE=0 \
 -include $(ROOT)/arch/esp32c6/include/tiku.h -DTIKU_C6_BUILD=$(C6_BUILD) -I$(ROOT)/arch/esp32c6/include -I$(ROOT)
CFLAGS += $(EXTRA_CFLAGS)
C6_SRCS := arch/esp32c6/tiku_native_arch.c arch/esp32c6/tiku_runtime_arch.c \
 arch/esp32c6/tiku_main_arch.c arch/esp32c6/tiku_mem_arch.c \
 arch/esp32c6/tiku_console_arch.c arch/esp32c6/tiku_sdio_arch.c \
 arch/esp32c6/tiku_sdio_diag_arch.c \
 arch/esp32c6/tiku_sdio_meter_arch.c \
 kernel/process/tiku_process.c kernel/timers/tiku_timer.c kernel/timers/tiku_clock.c \
 kernel/scheduler/tiku_sched.c kernel/ipc/tiku_ipc.c kernel/ipc/tiku_ipc_stream.c \
 kernel/memory/tiku_mem.c kernel/memory/tiku_pool.c kernel/memory/tiku_tier.c \
 kernel/memory/tiku_region.c kernel/memory/tiku_ipc_buffer.c \
 kernel/memory/tiku_nvm_region.c \
 services/ipc/tiku_ipc_wire.c services/ipc/tiku_ipc_bus.c \
 hal/tiku_ipc_packet_hal.c services/link/tiku_link.c services/console/tiku_console.c
C6_ASMS := arch/esp32c6/tiku_crt_early.S arch/esp32c6/tiku_irq_probe.S
C6_OBJS := $(addprefix $(BUILD_DIR)/,$(C6_SRCS:.c=.o) $(C6_ASMS:.S=.o))
.PHONY: all clean flash FORCE
FORCE:
$(BUILD_DIR)/.flags: FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(CFLAGS) base=$(C6_RAM_BASE)' > $@.new
	@if ! cmp -s $@ $@.new; then mv $@.new $@; else rm $@.new; fi
$(C6_OBJS) $(BUILD_DIR)/native.ld: $(BUILD_DIR)/.flags
all: $(BUILD_DIR)/main.bin $(BUILD_DIR)/manifest.bin
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -include $(ROOT)/arch/esp32c6/include/tiku.h,$(CFLAGS)) -MMD -MP -c $< -o $@
$(BUILD_DIR)/native.ld: arch/esp32c6/esp32c6_resident.ld.in
	@mkdir -p $(dir $@)
	sed 's/@BASE@/$(C6_RAM_BASE)/g' $< > $@
$(BUILD_DIR)/main.elf: $(C6_OBJS) $(BUILD_DIR)/native.ld
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Wl,--gc-sections,--build-id=none,-Map=$(BUILD_DIR)/main.map \
	 -T$(BUILD_DIR)/native.ld -o $@ $(C6_OBJS) -lgcc
	$(SIZE) $@
$(BUILD_DIR)/main.bin: $(BUILD_DIR)/main.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD_DIR)/manifest.bin: $(BUILD_DIR)/main.bin tools/esp32c6_resident_image.py
	python3 tools/esp32c6_resident_image.py --elf $(BUILD_DIR)/main.elf \
	 --image $< --base $(C6_RAM_BASE) --build $(C6_BUILD) --output $@
flash: all
	@test -n '$(C6_P4_PORT)' || { echo 'Set C6_P4_PORT to the idle P4 USB serial path'; exit 1; }
	$(C6_PYTHON) tools/esp32c6_resident_update.py --port '$(C6_P4_PORT)' \
	 --elf $(BUILD_DIR)/main.elf --image $(BUILD_DIR)/main.bin \
	 --manifest $(BUILD_DIR)/manifest.bin --bank $(C6_BANK) --output '$(C6_UPDATE_LOG_DIR)'
clean:
	rm -rf $(BUILD_DIR)
-include $(C6_OBJS:.o=.d)
