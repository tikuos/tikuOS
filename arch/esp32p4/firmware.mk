# firmware.mk -- the source lists of the native ESP32-P4 CPU0 firmware.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# SPDX-License-Identifier: Apache-2.0

# Native CPU0 firmware uses the portable core, shell and BASIC source lists.
ASM_SRCS += arch/esp32p4/tiku_crt_early.S
SRCS += $(addprefix arch/esp32p4/,tiku_boot_arch.c tiku_trap_arch.c \
    tiku_irq_arch.c tiku_systimer_arch.c tiku_alarm_arch.c tiku_timer_arch.c \
    tiku_cpu_arch.c tiku_cpu_firmware.c tiku_clock_arch.c tiku_clock_pll.c \
    tiku_pmu_arch.c tiku_watchdog_arch.c tiku_fault_arch.c tiku_libc_arch.c \
    tiku_cache_arch.c tiku_mmap_arch.c tiku_xip_arch.c tiku_flash_arch.c \
    tiku_mspi_arch.c tiku_mem_arch.c tiku_region_arch.c tiku_mpu_arch.c \
    tiku_nvm_arch.c tiku_nvm_region_arch.c tiku_psram_arch.c)
SRCS += services/console/tiku_usb_serial_jtag.c services/console/tiku_serial_jtag_io.c
SRCS += arch/esp32p4/tiku_gpio_arch.c
SRCS += arch/esp32p4/tiku_trng_arch.c
SRCS += kernel/vfs/tree/tiku_vfs_tree_psram_p4.c
ifeq ($(TIKU_THREADS_ENABLE),1)
SRCS += kernel/threads/tiku_thread.c arch/esp32p4/tiku_thread_arch.c
endif
