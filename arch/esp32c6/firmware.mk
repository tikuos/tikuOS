#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# firmware.mk - Shell, VFS and BASIC source selection.
#
# SPDX-License-Identifier: Apache-2.0
#
ifeq ($(TIKU_SHELL_ENABLE),1)
ifneq ($(C6_STORAGE),1)
$(error The C6 shell requires C6_STORAGE=1)
endif
C6_TIER_MIN ?= 65536
CFLAGS += -DTIKU_BOARD_I2C_BRW_100K=1 -DTIKU_SHELL_ENABLE=1 -DTIKU_TIER_SRAM_MIN=$(C6_TIER_MIN)
C6_SRCS += arch/esp32c6/tiku_adc_arch.c arch/esp32c6/tiku_irq_arch.c arch/esp32c6/tiku_uart_arch.c arch/esp32c6/tiku_spi_arch.c arch/esp32c6/tiku_i2c_arch.c \
 arch/esp32c6/tiku_timer_arch.c boot/tiku_boot_c6_native.c arch/esp32c6/tiku_cpu_arch.c \
 kernel/cpu/tiku_common.c kernel/cpu/tiku_rtc.c kernel/cpu/tiku_hang.c \
 kernel/cpu/tiku_power_policy.c kernel/cpu/tiku_stack.c \
 kernel/process/tiku_proc_vfs.c kernel/drivers/tiku_drv_registry.c \
 kernel/vfs/tiku_vfs.c kernel/vfs/tiku_vfs_cache.c kernel/vfs/tiku_vfs_tree.c \
 boot/tiku_boot_vfs.c kernel/vfs/tree/tiku_vfs_tree_ipc.c \
 services/console/tiku_console_remote.c services/link/tiku_link_console.c shell/tiku_shell_vfs.c shell/tiku_shell_io.c shell/tiku_shell_parser.c \
 shell/tiku_shell.c shell/tiku_shell_pump.c shell/tiku_shell_cwd.c \
 shell/tiku_shell_jobs.c shell/tiku_shell_rules.c shell/tiku_shell_alias.c \
 interfaces/adc/tiku_adc.c interfaces/bus/tiku_spi_bus.c interfaces/bus/tiku_i2c_bus.c \
 interfaces/led/tiku_led.c interfaces/button/tiku_button.c \
 interfaces/gpio/tiku_gpio_owner.c arch/esp32c6/tiku_gpio_arch.c \
 kernel/timers/tiku_htimer.c kernel/cpu/tiku_watchdog.c arch/esp32c6/tiku_watchdog_arch.c
C6_SRCS += $(addprefix kernel/vfs/tree/tiku_vfs_tree_,sys.c boot.c timer.c watchdog.c power.c net.c wifi.c persist.c layout.c watch.c dev.c gpio.c sensor.c data.c)
C6_SRCS += $(addprefix shell/commands/tiku_shell_cmd_,gpio.c irq.c wake.c sleep.c freq.c power.c trng.c adc.c ps.c info.c console.c timer.c kill.c resume.c queue.c reboot.c history.c ls.c cd.c toggle.c start.c write.c read.c fs.c watch.c calc.c every.c once.c jobs.c on.c rules.c changed.c free.c df.c layout.c name.c tree.c clear.c echo.c alias.c unalias.c)
C6_LIBS = -lc_nano -lm -lgcc
ifeq ($(TIKU_SHELL_BASIC_ENABLE),1)
CFLAGS += -DTIKU_SHELL_CMD_BASIC=1
C6_SRCS += basic/tiku_basic.c basic/tiku_basic_module.c basic/tiku_basic_vfs.c shell/commands/tiku_shell_cmd_basic.c
endif
endif
