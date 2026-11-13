/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_s31_ram.c - Storage-free S31 SRAM shell assembly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <kernel/scheduler/tiku_sched.h>
#include <shell/tiku_shell.h>
#ifdef TIKU_S31_FLASH_BOOT
#include <arch/esp32s31/tiku_image_desc.h>
#endif

/** @brief Start the UART shell and dispatch processes on CPU0. */
int main(void)
{
    tiku_s31_runtime_init();
    tiku_uart_init();
    tiku_clock_arch_delay(150000);
    tiku_clock_init();
    tiku_sched_init();
    tiku_s31_puts("TIKU S31 SRAM shell v1\n");
#ifdef TIKU_S31_FLASH_BOOT
    tiku_s31_puts("flash_project=");
    tiku_s31_puts(tiku_s31_image_project());
    tiku_s31_puts("\n");
#endif
    tiku_shell_init();
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         65535u, 0, 1);
    tiku_cpu_irq_enable();
    tiku_sched_start(&tiku_shell_process, NULL);
    for (;;) {
        while (tiku_sched_run_once()) { }
        tiku_watchdog_kick();
        __asm__ volatile ("wfi" ::: "memory");
    }
}
