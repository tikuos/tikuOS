# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# services.mk - S31 durable storage, namespace and BASIC assembly.
#
# SPDX-License-Identifier: Apache-2.0

S31_FLAGS := $(filter-out -DTIKU_MEM_NVM_ENABLE=0,$(S31_FLAGS))
S31_FLAGS += -DTIKU_MEM_NVM_ENABLE=1 -DTIKU_CELL_TABLE=1 -DTIKU_SHELL_BASIC_ENABLE=1 \
    -DTIKU_BASIC_GPIO_ENABLE=0 -DTIKU_BASIC_ADC_ENABLE=0 \
    -DTIKU_BASIC_LED_ENABLE=0 -DTIKU_BASIC_NET_ENABLE=0 \
    -DTIKU_BASIC_JSON_ENABLE=0 -DTIKU_BASIC_CRYPTO_ENABLE=0
S31_SHELL_COMMANDS += LS CD WRITE READ FS FREE DF LAYOUT NAME TREE CAT BASIC REBOOT CONSOLE FREQ
S31_SRCS += arch/esp32s31/tiku_storage_arch.c arch/esp32s31/tiku_storage_io.c \
    arch/esp32s31/tiku_mpu_arch.c arch/esp32s31/tiku_cpu_services.c \
    kernel/memory/tiku_nvm_region.c kernel/memory/tiku_nvm_media.c \
    kernel/memory/tiku_nvm_meter.c kernel/memory/tiku_nvm_sync.c \
    kernel/memory/tiku_layout.c kernel/memory/tiku_persist.c \
    kernel/memory/tiku_persist_move.c kernel/memory/tiku_mpu.c \
    kernel/fs/tiku_tfs.c kernel/cpu/tiku_common.c kernel/cpu/tiku_rtc.c \
    kernel/cpu/tiku_bench.c kernel/cpu/tiku_power_policy.c kernel/cpu/tiku_stack.c \
    kernel/process/tiku_proc_vfs.c kernel/drivers/tiku_drv_registry.c \
    kernel/vfs/tiku_vfs_cache.c kernel/vfs/tiku_vfs_tree.c boot/tiku_boot_vfs.c \
    services/link/tiku_link_console.c services/link/tiku_link.c \
    shell/tiku_shell_vfs.c shell/tiku_shell_pump.c \
    shell/tiku_shell_jobs.c shell/tiku_shell_rules.c \
    interfaces/adc/tiku_adc.c interfaces/bus/tiku_spi_bus.c \
    interfaces/led/tiku_led.c interfaces/button/tiku_button.c \
    arch/esp32s31/tiku_spi_arch.c \
    basic/tiku_basic.c basic/tiku_basic_module.c basic/tiku_basic_vfs.c
S31_SRCS += $(addprefix kernel/vfs/tree/tiku_vfs_tree_,sys.c boot.c timer.c watchdog.c power.c net.c wifi.c persist.c layout.c watch.c dev.c gpio.c sensor.c data.c)
S31_SRCS += $(addprefix shell/commands/tiku_shell_cmd_,ls.c cd.c write.c read.c fs.c free.c df.c layout.c name.c tree.c basic.c reboot.c console.c freq.c)
ifneq ($(origin S31_APPLICATION_MANIFEST),undefined)
ifeq ($(wildcard $(S31_APPLICATION_MANIFEST)),)
$(error Requested S31_APPLICATION_MANIFEST does not exist)
endif
endif
S31_APPLICATION_MANIFEST ?= applications/applications.mk
-include $(S31_APPLICATION_MANIFEST)
S31_GUI_KITS ?= applications/kits
S31_SRCS += $(patsubst applications/kits/%,$(S31_GUI_KITS)/%,$(APPL_SRCS))
S31_FLAGS += $(subst -Iapplications/kits/,-I$(S31_GUI_KITS)/,$(APPL_CFLAGS))

ifeq ($(S31_SERVICES_TEST),1)
S31_FLAGS += -DTIKU_S31_SERVICES_TEST=1
S31_SRCS += TikuBench/tests/esp32s31/test_services.c
endif

S31_MEDIA_ENABLE ?= 1
ifeq ($(S31_MEDIA_ENABLE),1)
S31_FLAGS += -DTIKU_MEDIA_FS_ENABLE=1 -DTIKU_S31_SDCARD_ENABLE=1
S31_SHELL_COMMANDS += MEDIA
S31_SRCS += arch/esp32s31/tiku_sdcard_arch.c arch/esp32s31/tiku_mfs_block_arch.c \
    kernel/fs/tiku_mfs.c kernel/fs/tiku_mfs_copy.c \
    services/storage/tiku_media_card.c services/storage/tiku_media_store.c \
    services/storage/tiku_media_vfs.c services/storage/tiku_media_transfer.c \
    services/storage/tiku_media_transfer_server.c shell/commands/tiku_shell_cmd_media.c
endif
