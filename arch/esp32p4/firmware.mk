# firmware.mk -- the source lists of the native ESP32-P4 CPU0 firmware.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# SPDX-License-Identifier: Apache-2.0

# Native CPU0 firmware uses the portable core, shell and BASIC source lists.
ASM_SRCS += arch/esp32p4/tiku_crt_early.S arch/esp32p4/tiku_pie_arch.S
ASM_SRCS += arch/esp32p4/tiku_memcpy_arch.S
SRCS += $(addprefix arch/esp32p4/,tiku_boot_arch.c tiku_trap_arch.c \
    tiku_irq_arch.c tiku_systimer_arch.c tiku_alarm_arch.c tiku_timer_arch.c \
    tiku_cpu_arch.c tiku_cpu_firmware.c tiku_clock_arch.c tiku_clock_pll.c \
    tiku_pmu_arch.c tiku_watchdog_arch.c tiku_fault_arch.c tiku_libc_arch.c \
    tiku_cache_arch.c tiku_mmap_arch.c tiku_xip_arch.c tiku_flash_arch.c \
    tiku_mspi_arch.c tiku_mem_arch.c tiku_region_arch.c tiku_mpu_arch.c \
    tiku_nvm_arch.c tiku_nvm_region_arch.c tiku_psram_arch.c \
    tiku_sleep_arch.c)
SRCS += services/console/tiku_usb_serial_jtag.c services/console/tiku_serial_jtag_io.c
SRCS += arch/esp32p4/tiku_gpio_arch.c
ifeq ($(TIKU_BOARD_DEFINE),TIKU_BOARD_FIREBEETLE2_ESP32P4)
SRCS += arch/esp32p4/tiku_uart_arch.c arch/esp32p4/tiku_i2c_arch.c
SRCS += arch/esp32p4/tiku_adc_arch.c arch/esp32p4/tiku_spi_arch.c
SRCS += arch/esp32p4/tiku_onewire_arch.c arch/esp32p4/tiku_crypto_arch.c
SRCS += arch/esp32p4/tiku_dma_arch.c arch/esp32p4/tiku_pwm_arch.c
SRCS += arch/esp32p4/tiku_dma2d_arch.c arch/esp32p4/tiku_ppa_arch.c
SRCS += arch/esp32p4/tiku_jpeg_arch.c arch/esp32p4/tiku_dwgdma_arch.c
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += shell/commands/tiku_shell_cmd_jpeg.c
endif
endif
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += shell/commands/tiku_shell_cmd_diag.c
endif
SRCS += arch/esp32p4/tiku_trng_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_psram_p4.c
ifeq ($(TIKU_THREADS_ENABLE),1)
SRCS += kernel/threads/tiku_thread.c arch/esp32p4/tiku_thread_arch.c
endif

# The LP core payload (arch/esp32p4/lpcore/), opt-in: built with this
# toolchain, linked into the XIP window and copied into LP SRAM at launch.
# It backs interfaces/coproc and /sys/coproc.  TIKU_COPROC_MSG_CAP must equal
# the payload's mailbox cap; tiku_coproc_arch.c asserts it.
ifeq ($(TIKU_LPCORE_ENABLE),1)
SRCS += arch/esp32p4/tiku_lpcore_arch.c arch/esp32p4/tiku_coproc_arch.c
SRCS += arch/esp32p4/tiku_lpagent_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_coproc.c kernel/vfs/tree/tiku_vfs_tree_lp.c
ifeq ($(TIKU_SHELL_ENABLE),1)
SRCS += shell/commands/tiku_shell_cmd_lpcore.c shell/commands/tiku_shell_cmd_lp.c
endif
CFLAGS += -DTIKU_LPCORE_ENABLE=1 -DTIKU_HAS_COPROC=1 -DTIKU_COPROC_MSG_CAP=240u
LPCORE_BUILD = $(BUILD_DIR)/lpcore
LPCORE_CFLAGS = $(ESP32P4_LPCORE_ARCH) -Os -Wall -Wextra -Werror -ffreestanding \
                -nostdlib -nostartfiles -ffunction-sections -fdata-sections \
                -MMD -MP
LPCORE_OBJS = $(LPCORE_BUILD)/tiku_lpcore_crt0.o $(LPCORE_BUILD)/tiku_lpcore_main.o \
              $(LPCORE_BUILD)/tiku_lpagent_jobs.o $(LPCORE_BUILD)/tiku_lpcore_i2c.o
TIKU_LPCORE_IMG_O = $(LPCORE_BUILD)/tiku_lpcore_img.o
endif
