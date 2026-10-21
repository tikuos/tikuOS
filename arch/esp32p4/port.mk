# port.mk -- the ESP32-P4 RAM profiles and the rules that build them.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# SPDX-License-Identifier: Apache-2.0

# Native ESP32-P4 RAM profiles; scratch writes require explicit qualification.
TIKU_PLATFORM := esp32p4
BOARD ?= olimex_esp32p4_pc
P4_BOARD_DEFINE_olimex_esp32p4_pc := TIKU_BOARD_OLIMEX_ESP32P4_PC
P4_BOARD_DEFINE_firebeetle2_esp32p4 := TIKU_BOARD_FIREBEETLE2_ESP32P4
P4_BOARD_DEFINE := $(P4_BOARD_DEFINE_$(BOARD))
ifeq ($(P4_BOARD_DEFINE),)
$(error The P4 RAM profiles require BOARD=olimex_esp32p4_pc or firebeetle2_esp32p4)
endif
P4_PROFILE ?= diagnostic
ifeq ($(P4_PROFILE),diagnostic)
ifneq ($(MINIMAL),1)
$(error Select MINIMAL=1, P4_PROFILE=kernel-test, or P4_PROFILE=firmware)
endif
else ifeq ($(P4_PROFILE),kernel-test)
ifneq ($(filter-out 0,$(MINIMAL)),)
$(error Use P4_PROFILE=kernel-test without MINIMAL)
endif
else
$(error P4_PROFILE must be diagnostic or kernel-test)
endif
TIKU_CONSOLE ?= usb
ifneq ($(TIKU_CONSOLE),usb)
$(error ESP32-P4 requires TIKU_CONSOLE=usb)
endif
P4_REQUESTED_FEATURES := $(strip $(foreach v,$(filter TIKU_%_ENABLE,$(.VARIABLES)),\
    $(if $(filter 1,$($(v))),$(v))))
ifneq ($(strip $(P4_REQUESTED_FEATURES) $(APP) $(BASIC_PROGRAM)),)
$(error P4 RAM profiles do not include optional features or application payloads)
endif
ifneq ($(filter-out 0,$(TEST_ENABLE) $(TIKU_ESP32P4_XIP_CODE)),)
$(error P4 RAM profiles require TEST_ENABLE=0 and have no XIP code)
endif

P4_FLASH_WRITE_TEST ?= 0
ifneq ($(P4_FLASH_WRITE_TEST),0)
ifneq ($(P4_FLASH_WRITE_TEST),1)
$(error P4_FLASH_WRITE_TEST must be 0 or 1)
endif
endif
ifeq ($(P4_FLASH_WRITE_TEST),1)
ifneq ($(P4_PROFILE),kernel-test)
$(error Scratch-write qualification requires P4_PROFILE=kernel-test)
endif
endif
P4_XIP_TEST ?= 0
ifneq ($(P4_XIP_TEST),0)
ifneq ($(P4_XIP_TEST),1)
$(error P4_XIP_TEST must be 0 or 1)
endif
endif
ifeq ($(P4_XIP_TEST),1)
ifneq ($(P4_PROFILE):$(P4_FLASH_WRITE_TEST),kernel-test:0)
$(error XIP qualification requires the non-writing kernel-test profile)
endif
endif
P4_PSRAM_TEST ?= 0
ifneq ($(P4_PSRAM_TEST),0)
ifneq ($(P4_PSRAM_TEST),1)
$(error P4_PSRAM_TEST must be 0 or 1)
endif
endif
ifeq ($(P4_PSRAM_TEST),1)
ifneq ($(P4_PROFILE):$(P4_FLASH_WRITE_TEST):$(P4_XIP_TEST),kernel-test:0:0)
$(error PSRAM qualification requires a non-writing, SRAM-only kernel-test profile)
endif
endif
P4_STORAGE_TEST ?= 0
ifneq ($(P4_STORAGE_TEST),0)
ifneq ($(P4_STORAGE_TEST),1)
$(error P4_STORAGE_TEST must be 0 or 1)
endif
endif
ifeq ($(P4_STORAGE_TEST),1)
ifneq ($(P4_PROFILE):$(P4_FLASH_WRITE_TEST):$(P4_XIP_TEST):$(P4_PSRAM_TEST),kernel-test:0:0:0)
$(error Storage qualification requires an exclusive SRAM-only kernel-test profile)
endif
endif
BUILD_DIR ?= build/esp32p4/$(P4_PROFILE)$(if $(filter 1,$(P4_FLASH_WRITE_TEST)),-write)$(if $(filter 1,$(P4_XIP_TEST)),-xip)$(if $(filter 1,$(P4_PSRAM_TEST)),-psram)$(if $(filter 1,$(P4_STORAGE_TEST)),-storage)
TOOLCHAIN_PREFIX ?= riscv-none-elf-
P4_TOOL := $(if $(TOOLCHAIN_DIR),$(TOOLCHAIN_DIR)/bin/)$(TOOLCHAIN_PREFIX)
P4_CC := $(P4_TOOL)gcc
P4_OBJCOPY := $(P4_TOOL)objcopy
P4_SIZE := $(P4_TOOL)size
PYTHON ?= python3
ESPTOOL ?= esptool
# P4_ARCH comes from the Makefile, which sets it before including this file.
P4_FLAGS := $(P4_ARCH) -Os -g -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -ffunction-sections -fdata-sections -I. \
    -DPLATFORM_ESP32P4=1 \
    -D$(P4_BOARD_DEFINE)=1
P4_SRCS := arch/esp32p4/tiku_boot_diagnostic.c \
    arch/esp32p4/tiku_boot_arch.c arch/esp32p4/tiku_irq_arch.c \
    arch/esp32p4/tiku_trap_arch.c arch/esp32p4/tiku_systimer_arch.c \
    arch/esp32p4/tiku_clock_arch.c arch/esp32p4/tiku_clock_pll.c \
    arch/esp32p4/tiku_pmu_arch.c arch/esp32p4/tiku_alarm_arch.c \
    arch/esp32p4/tiku_watchdog_arch.c arch/esp32p4/tiku_fault_arch.c \
    arch/esp32p4/tiku_fault_probe.c arch/esp32p4/tiku_flash_arch.c \
    TikuBench/tests/esp32p4/test_flash.c \
    services/console/tiku_usb_serial_jtag.c
P4_ASM := arch/esp32p4/tiku_crt_early.S arch/esp32p4/tiku_irq_probe.S
ifeq ($(P4_PROFILE),diagnostic)
P4_FLAGS += -DTIKU_MINIMAL=1
P4_LIBS := -lgcc
P4_LINKER := arch/esp32p4/devices/esp32p4_diagnostic.ld
else
P4_FLAGS += -DTIKU_P4_KERNEL_TEST=1 -DTIKU_THREADS_ENABLE=1 \
    -DTIKU_MEM_RECLAIM_ENABLE=1 -Wno-missing-field-initializers
P4_SRCS := $(filter-out arch/esp32p4/tiku_boot_diagnostic.c,$(P4_SRCS)) \
    arch/esp32p4/tiku_cpu_arch.c arch/esp32p4/tiku_timer_arch.c \
    arch/esp32p4/tiku_thread_arch.c arch/esp32p4/tiku_libc_arch.c \
    kernel/process/tiku_process.c kernel/scheduler/tiku_sched.c \
    kernel/timers/tiku_clock.c kernel/timers/tiku_htimer.c \
    kernel/timers/tiku_timer.c kernel/timers/tiku_crit.c \
    kernel/threads/tiku_thread.c kernel/vfs/tiku_vfs.c \
    kernel/cpu/tiku_hang.c TikuBench/tests/esp32p4/test_kernel.c \
    TikuBench/tests/esp32p4/test_memory.c \
    arch/esp32p4/tiku_cache_arch.c arch/esp32p4/tiku_mmap_arch.c \
    arch/esp32p4/tiku_mem_arch.c arch/esp32p4/tiku_region_arch.c \
    arch/esp32p4/tiku_mpu_arch.c kernel/memory/tiku_mem.c \
    kernel/memory/tiku_pool.c kernel/memory/tiku_tier.c \
    kernel/memory/tiku_region.c kernel/memory/tiku_mpu.c \
    kernel/memory/tiku_reclaim.c kernel/memory/tiku_nvm_region.c
P4_LIBS := -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group
P4_CAPTURE_ARGS := --kernel-test
P4_LINKER := arch/esp32p4/devices/esp32p4_kernel_ram.ld
endif
ifeq ($(P4_FLASH_WRITE_TEST),1)
P4_FLAGS += -DTIKU_P4_FLASH_WRITE_TEST=1 -DTIKU_P4_MSPI_TEST=1
P4_SRCS += arch/esp32p4/tiku_mspi_arch.c \
    arch/esp32p4/tiku_psram_arch.c TikuBench/tests/esp32p4/test_mspi.c \
    TikuBench/tests/esp32p4/test_flash_write.c
endif
ifeq ($(P4_XIP_TEST),1)
P4_FLAGS += -DTIKU_P4_XIP_TEST=1
P4_SRCS += arch/esp32p4/tiku_xip_arch.c TikuBench/tests/esp32p4/test_xip.c
endif
ifeq ($(P4_PSRAM_TEST),1)
P4_FLAGS += -DTIKU_P4_PSRAM_TEST=1
P4_SRCS += arch/esp32p4/tiku_psram_arch.c TikuBench/tests/esp32p4/test_psram.c
endif
ifeq ($(P4_STORAGE_TEST),1)
P4_FLAGS += -DTIKU_P4_STORAGE_TEST=1
P4_LDFLAGS += -Wl,--defsym=__p4_storage_enabled=1
P4_SRCS += arch/esp32p4/tiku_mspi_arch.c arch/esp32p4/tiku_nvm_arch.c \
    arch/esp32p4/tiku_nvm_region_arch.c TikuBench/tests/esp32p4/test_storage.c
endif
P4_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(P4_SRCS)) \
    $(patsubst %.S,$(BUILD_DIR)/%.o,$(P4_ASM))
P4_ELF := $(BUILD_DIR)/diagnostic.elf
P4_BIN := $(BUILD_DIR)/diagnostic.bin
P4_LOADER := tools/esp32p4_ram.py
ifeq ($(P4_FLASH_WRITE_TEST),1)
P4_LOADER := tools/esp32p4_flash_test.py
endif
ifeq ($(P4_STORAGE_TEST),1)
P4_LOADER := tools/esp32p4_storage_test.py
endif

.PHONY: all ram-load check-image flash erase clean lint FORCE
all: $(P4_BIN) check-image
$(BUILD_DIR)/.flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(P4_CC) $(P4_FLAGS)' > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new
$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)/.flags
	@mkdir -p $(@D)
	$(P4_CC) $(P4_FLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/%.o: %.S $(BUILD_DIR)/.flags
	@mkdir -p $(@D)
	$(P4_CC) $(P4_FLAGS) -MMD -MP -c $< -o $@
$(P4_ELF): $(P4_OBJS) $(P4_LINKER) arch/common/tiku_sram_layout.ld arch/esp32p4/port.mk
	$(P4_CC) $(P4_ARCH) -nostdlib -Wl,--gc-sections \
	    -Wl,-Map=$(BUILD_DIR)/diagnostic.map \
	    -T$(P4_LINKER) $(P4_LDFLAGS) $(P4_OBJS) $(P4_LIBS) -o $@
	$(P4_SIZE) $@
ifeq ($(P4_XIP_TEST),1)
$(P4_BIN) $(BUILD_DIR)/xip.bin $(BUILD_DIR)/diagnostic.paired.elf &: $(P4_ELF) \
    tools/esp32p4_xip.py tools/esp32p4_ram.py tools/esp32p4_layout.py arch/esp32p4/flash_layout.def
	$(PYTHON) tools/esp32p4_xip.py --elf $< --boot $(P4_BIN) --xip $(BUILD_DIR)/xip.bin \
	    --objcopy $(P4_OBJCOPY) --esptool $(ESPTOOL)
else
$(P4_BIN): $(P4_ELF)
	$(ESPTOOL) --chip esp32p4 elf2image --flash-mode dio \
	    --flash-freq 40m --flash-size 16MB --min-rev-full 103 \
	    --max-rev-full 103 -o $@ $<
endif
check-image: $(P4_BIN)
ifeq ($(P4_XIP_TEST),1)
check-image: $(BUILD_DIR)/xip.bin $(BUILD_DIR)/diagnostic.paired.elf
	$(PYTHON) tools/esp32p4_flash.py --image $(P4_BIN) --elf $(BUILD_DIR)/diagnostic.paired.elf \
	    --xip $(BUILD_DIR)/xip.bin --check-only
else
	$(PYTHON) $(P4_LOADER) --image $< --elf $(P4_ELF) $(P4_CAPTURE_ARGS) --check-only
endif
ram-load: all
ifeq ($(P4_XIP_TEST),1)
	@echo 'XIP requires an installed matching companion; use the explicit flash qualification tool.'
	@exit 1
else
	$(PYTHON) $(P4_LOADER) --image $(P4_BIN) --elf $(P4_ELF) \
	    --port '$(PORT)' --serial '$(P4_SERIAL)' $(P4_CAPTURE_ARGS) $(P4_MONITOR_ARGS)
endif
flash erase:
	@echo 'P4 flash installation and whole-chip erase are unavailable; see arch/esp32p4/README.md.'
	@exit 1
clean:
	$(RM) $(P4_OBJS) $(P4_OBJS:.o=.d) $(P4_ELF) $(P4_BIN) \
	    $(BUILD_DIR)/diagnostic.map $(BUILD_DIR)/.flags
	$(RM) $(BUILD_DIR)/xip.bin $(BUILD_DIR)/diagnostic.paired.elf
lint:
	python3 tools/check_comment_style.py --strict
	sh tools/check_durable_placement.sh
	python3 tools/check_kernel_dependencies.py
-include $(P4_OBJS:.o=.d)
