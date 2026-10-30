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
ifeq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
ifeq ($(wildcard drivers/wifi/esp/c6/build.mk),)
$(error C6 Wi-Fi requires the drivers checkout with wifi/esp/c6/build.mk)
endif
ifneq ($(C6_PROFILE),firmware)
$(error C6 Wi-Fi requires C6_PROFILE=firmware)
endif
endif
ifeq ($(TIKU_KIT_NET_ENABLE),1)
ifneq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
$(error C6 IP networking requires TIKU_DRV_WIFI_ESP_ENABLE=1)
endif
endif
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
TIKU_SHELL_ENABLE ?= 1
ifneq ($(TIKU_SHELL_ENABLE),1)
$(error C6 BASIC requires TIKU_SHELL_ENABLE=1)
endif
endif
ifneq ($(C6_PROFILE),firmware)
ifneq ($(filter 1,$(TIKU_SHELL_ENABLE) $(TIKU_SHELL_BASIC_ENABLE)),)
$(error The C6 shell requires C6_PROFILE=firmware)
endif
endif
TIKU_THREADS_ENABLE ?= $(if $(filter firmware,$(C6_PROFILE)),1,0)
BOARD ?= firebeetle2_esp32p4_c6
ifeq ($(filter $(C6_PROFILE),resident cold firmware),)
$(error ESP32-C6 supports C6_PROFILE=resident, cold or firmware)
endif
ifneq ($(BOARD),firebeetle2_esp32p4_c6)
$(error ESP32-C6 resident profile requires BOARD=firebeetle2_esp32p4_c6)
endif
ROOT := $(CURDIR)
BUILD_DIR ?= build/esp32c6/$(C6_PROFILE)
C6_RAM_BASE ?= 0x4080a000
C6_BUILD ?= 1
C6_PERIPHERAL_TEST ?= 0
C6_WIFI_TEST ?= 0
ifeq ($(C6_WIFI_TEST),1)
ifneq ($(C6_PROFILE):$(TIKU_DRV_WIFI_ESP_ENABLE):$(TIKU_KIT_NET_ENABLE),firmware:1:1)
$(error C6_WIFI_TEST requires native Wi-Fi and IP networking)
endif
CFLAGS_C6_WIFI_TEST := -DTIKU_C6_WIFI_TEST=1 -DTIKU_SHELL_CMD_TRNG=1
endif
ifeq ($(C6_PERIPHERAL_TEST),1)
ifneq ($(C6_PROFILE),firmware)
$(error C6_PERIPHERAL_TEST requires C6_PROFILE=firmware)
endif
endif
C6_THREAD_TEST ?= 0
C6_STORAGE_TEST ?= 0
C6_STORAGE ?= 0
ifneq ($(filter $(C6_STORAGE),0 1),$(C6_STORAGE))
$(error C6_STORAGE must be 0 or 1)
endif
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
 -DTIKU_CLOCK_ARCH_SECOND=128UL -DTIKU_LC_PERSISTENT=0 -DTIKU_THREADS_ENABLE=$(TIKU_THREADS_ENABLE) \
 -include $(ROOT)/arch/esp32c6/include/tiku.h -DTIKU_C6_BUILD=$(C6_BUILD) -I$(ROOT)/arch/esp32c6/include -I$(ROOT)
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS += $(CFLAGS_C6_WIFI_TEST)
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
ifeq ($(C6_STORAGE),1)
ifneq ($(C6_PROFILE),firmware)
$(error C6_STORAGE requires C6_PROFILE=firmware)
endif
CFLAGS += -DTIKU_C6_STORAGE=1 -DTIKU_CELL_TABLE=1
C6_SRCS += arch/esp32c6/tiku_flash_arch.c arch/esp32c6/tiku_nvm_arch.c \
 arch/esp32c6/tiku_nvm_region_arch.c arch/esp32c6/tiku_mpu_arch.c \
 arch/esp32c6/tiku_trng_arch.c kernel/memory/tiku_mpu.c \
 kernel/memory/tiku_layout.c kernel/memory/tiku_persist.c \
 kernel/memory/tiku_persist_move.c kernel/fs/tiku_tfs.c
endif
ifeq ($(C6_STORAGE_TEST),1)
ifneq ($(C6_PROFILE),firmware)
$(error C6_STORAGE_TEST requires C6_PROFILE=firmware)
endif
CFLAGS += -DTIKU_C6_STORAGE_TEST=1
C6_SRCS += TikuBench/tests/esp32c6/test_storage.c
C6_STORAGE_ASMS := TikuBench/tests/esp32c6/test_storage_regs.S
endif
ifeq ($(TIKU_THREADS_ENABLE),1)
ifneq ($(C6_PROFILE),firmware)
$(error C6 workers require C6_PROFILE=firmware)
endif
C6_SRCS += arch/esp32c6/tiku_thread_arch.c kernel/threads/tiku_thread.c
CFLAGS += -Wno-missing-field-initializers
endif
ifeq ($(C6_THREAD_TEST),1)
ifneq ($(TIKU_THREADS_ENABLE),1)
$(error C6_THREAD_TEST requires TIKU_THREADS_ENABLE=1)
endif
CFLAGS += -DTIKU_C6_THREAD_TEST=1
C6_SRCS += TikuBench/tests/esp32c6/test_threads.c
C6_TEST_ASMS := TikuBench/tests/esp32c6/test_thread_regs.S
endif
ifneq ($(filter $(C6_PROFILE),cold firmware),)
ifneq ($(C6_RAM_BASE),0x4080a000)
$(error The C6 cold profile requires C6_RAM_BASE=0x4080a000)
endif
C6_SDK_ENTRY ?= 0x40800896
C6_SRCS += arch/esp32c6/tiku_cold_arch.c
C6_ASMS := arch/esp32c6/tiku_cold_entry.S arch/esp32c6/tiku_irq_probe.S
CFLAGS += -DC6_SDK_ENTRY=$(C6_SDK_ENTRY) -Wl,--wrap=native_main,--wrap=tiku_c6_qualify,--entry=c6_cold_entry
endif
ifeq ($(C6_PROFILE),firmware)
include $(ROOT)/arch/esp32c6/firmware.mk
include $(wildcard $(ROOT)/drivers/wifi/esp/c6/build.mk)
ifeq ($(TIKU_C6_RADIO_SERVICE_ENABLE),1)
ifneq ($(TIKU_DRV_WIFI_ESP_ENABLE),1)
$(error C6 radio service requires the native Wi-Fi driver)
endif
ifeq ($(TIKU_KIT_NET_ENABLE),1)
$(error C6 radio service owns Ethernet; local C6 IP must be disabled)
endif
CFLAGS += -DTIKU_C6_RADIO_SERVICE_ENABLE=1 -DTIKU_IPC_BUFFERS_MAX=8
C6_SRCS += services/ipc/tiku_ipc_mux.c services/ipc/tiku_ipc_radio_codec.c \
 services/ipc/tiku_ipc_radio_frames.c services/ipc/tiku_ipc_radio_server.c
endif
ifeq ($(C6_WIFI_TEST),1)
C6_SRCS += TikuBench/tests/esp32c6/test_wifi.c
endif
ifeq ($(C6_PERIPHERAL_TEST),1)
ifneq ($(TIKU_SHELL_ENABLE)$(C6_STORAGE),11)
$(error C6_PERIPHERAL_TEST requires the shell and storage profile)
endif
CFLAGS += -DTIKU_C6_PERIPHERAL_TEST=1
C6_SRCS += TikuBench/tests/esp32c6/test_peripherals.c
endif
C6_TIER_MIN ?= 196608
CFLAGS += -DTIKU_C6_FIRMWARE=1 -DTIKU_C6_TIER_FLOOR=$(C6_TIER_MIN)
endif
C6_OBJS := $(addprefix $(BUILD_DIR)/,$(C6_SRCS:.c=.o) $(C6_ASMS:.S=.o) $(C6_TEST_ASMS:.S=.o) $(C6_STORAGE_ASMS:.S=.o))
.PHONY: all clean flash FORCE
FORCE:
$(BUILD_DIR)/.flags: FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(CFLAGS) base=$(C6_RAM_BASE)' > $@.new
	@if ! cmp -s $@ $@.new; then mv $@.new $@; else rm $@.new; fi
$(C6_OBJS) $(BUILD_DIR)/native.ld: $(BUILD_DIR)/.flags
all: $(BUILD_DIR)/main.bin
ifeq ($(C6_PROFILE),resident)
all: $(BUILD_DIR)/manifest.bin
endif
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -include $(ROOT)/arch/esp32c6/include/tiku.h,$(CFLAGS)) -MMD -MP -c $< -o $@
ifeq ($(C6_PROFILE),firmware)
$(BUILD_DIR)/native.ld: arch/esp32c6/esp32c6_firmware.ld.in
	@mkdir -p $(dir $@)
	sed -e 's/@BASE@/$(C6_RAM_BASE)/g' -e 's/@BUILD@/$(C6_BUILD)/g' \
	 -e 's|@RADIO_XIP@|$(if $(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE)),INCLUDE drivers/wifi/esp/c6/wifi_c6_xip.ld,)|g' \
	 -e 's/@XIP_BYTES@/$(if $(filter 1,$(TIKU_DRV_WIFI_ESP_ENABLE)),0xfffd0,0x40000)/g' \
	 -e 's/@SHELL@/$(if $(filter 1,$(TIKU_SHELL_ENABLE)),1,0)/g' -e 's/@TIER_FLOOR@/$(C6_TIER_MIN)/g' -e 's/@STORAGE@/$(C6_STORAGE)/g' $< > $@
else
$(BUILD_DIR)/native.ld: arch/esp32c6/esp32c6_resident.ld.in
	@mkdir -p $(dir $@)
	sed 's/@BASE@/$(C6_RAM_BASE)/g' $< > $@
ifeq ($(C6_PROFILE),cold)
	printf '%s\n' 'ASSERT(__native_stack_top <= 0x40819000, "C6 cold-entry stack overlap")' >> $@
endif
endif
$(BUILD_DIR)/main.elf: $(C6_OBJS) $(BUILD_DIR)/native.ld $(C6_LINK_DEPS)
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Wl,--gc-sections,--build-id=none,-Map=$(BUILD_DIR)/main.map \
	 -T$(BUILD_DIR)/native.ld -o $@ $(C6_OBJS) $(C6_LDFLAGS) $(C6_LIBS) -lgcc
	$(SIZE) $@
$(BUILD_DIR)/main.bin: $(BUILD_DIR)/main.elf
	$(OBJCOPY) -O binary --remove-section=.xip $< $@
$(BUILD_DIR)/manifest.bin: $(BUILD_DIR)/main.bin tools/esp32c6_resident_image.py
	python3 tools/esp32c6_resident_image.py --elf $(BUILD_DIR)/main.elf \
	 --image $< --base $(C6_RAM_BASE) --build $(C6_BUILD) --output $@
ifneq ($(filter $(C6_PROFILE),cold firmware),)
.PHONY: cold-image
ifeq ($(C6_PROFILE),firmware)
cold-image: firmware-image
else
cold-image: all
	@test -n '$(C6_RECOVERY_IMAGE)' || { echo 'Set C6_RECOVERY_IMAGE to the preserved recovery application'; exit 1; }
	$(C6_PYTHON) tools/esp32c6_cold_image.py --recovery '$(C6_RECOVERY_IMAGE)' \
	 --native $(BUILD_DIR)/main.bin --elf $(BUILD_DIR)/main.elf \
	 --output $(BUILD_DIR)/app.bin
endif
ifeq ($(C6_PROFILE),firmware)
.PHONY: firmware-image
$(BUILD_DIR)/xip.bin: $(BUILD_DIR)/main.elf
	$(OBJCOPY) -O binary --only-section=.xip $< $@
firmware-image: all $(BUILD_DIR)/xip.bin
	@test -n '$(C6_RECOVERY_IMAGE)' || { echo 'Set C6_RECOVERY_IMAGE to the preserved recovery application'; exit 1; }
	$(C6_PYTHON) tools/esp32c6_firmware_image.py --recovery '$(C6_RECOVERY_IMAGE)' \
	 --native $(BUILD_DIR)/main.bin --elf $(BUILD_DIR)/main.elf --xip $(BUILD_DIR)/xip.bin \
	 --output $(BUILD_DIR)/app.bin --metadata $(BUILD_DIR)/firmware.json
flash: firmware-image
	@test -n '$(C6_P4_PORT)' -a -n '$(C6_P4_SERIAL)' -a -n '$(C6_PEER_MAC)' || \
	 { echo 'Set C6_P4_PORT, C6_P4_SERIAL and C6_PEER_MAC for the identified board'; exit 1; }
	$(C6_PYTHON) -B tools/esp32c6_install.py --port '$(C6_P4_PORT)' \
	 --p4-serial '$(C6_P4_SERIAL)' --peer-mac '$(C6_PEER_MAC)' \
	 --image $(BUILD_DIR)/app.bin --elf $(BUILD_DIR)/main.elf \
	 --metadata $(BUILD_DIR)/firmware.json --output '$(C6_UPDATE_LOG_DIR)'
else
flash:
	@echo 'The cold profile is an application-slot image; resident-bank flash is refused'
	@exit 1
endif
else
flash: all
	@test -n '$(C6_P4_PORT)' || { echo 'Set C6_P4_PORT to the idle P4 USB serial path'; exit 1; }
	$(C6_PYTHON) tools/esp32c6_resident_update.py --port '$(C6_P4_PORT)' \
	 --elf $(BUILD_DIR)/main.elf --image $(BUILD_DIR)/main.bin \
	 --manifest $(BUILD_DIR)/manifest.bin --bank $(C6_BANK) --output '$(C6_UPDATE_LOG_DIR)'
endif
clean:
	rm -rf $(BUILD_DIR)
-include $(C6_OBJS:.o=.d)
