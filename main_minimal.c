/*
 * Tiku Operating System v0.05 — RP2350 minimal smoke test
 *
 * No kernel, no scheduler, no shell. Just brings up clocks + UART
 * and prints a heartbeat in a loop. Intended as a debugging aid:
 * if THIS doesn't print, the failure is in boot/clock/UART; if it
 * does, the failure is somewhere up the kernel stack.
 *
 * Build:
 *   make MCU=rp2350 MINIMAL=1
 *   sudo picotool load -fx main.uf2
 *   make monitor MCU=rp2350      # picocom @ 115200 on /dev/ttyUSB0
 *
 * Expected output (115200 8N1):
 *
 *   TikuOS minimal: hello #0  clk=150000000 Hz  fault=0
 *   TikuOS minimal: hello #1  clk=150000000 Hz  fault=0
 *   ...
 *
 * GP25 toggles on each iteration so a scope or multimeter on the
 * pin shows ~1 Hz square-wave even if UART is silent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "arch/arm-rp2350/tiku_cpu_freq_boot_arch.h"
#include "arch/arm-rp2350/tiku_cpu_common.h"
#include "arch/arm-rp2350/tiku_uart_arch.h"
#include "arch/arm-rp2350/tiku_gpio_arch.h"
#include "arch/arm-rp2350/tiku_rp2350_regs.h"

int main(void)
{
    /* Bring up XOSC + (try to) PLL_SYS + CLK_SYS + CLK_PERI. Falls
     * back silently to 12 MHz XOSC if the PLL can't lock. */
    tiku_cpu_boot_rp2350_init();

    /* Onboard LED pin (GP25 on plain Pico 2; on Pico 2 W this is
     * shared with the CYW43 WL_CLK so it won't actually light an
     * LED, but driving it as GPIO is harmless and a scope can see
     * the toggle). */
    tiku_rp2350_gpio_init_output(25U);

    /* UART0 on GP0 (TX) / GP1 (RX) at TIKU_BOARD_UART_BAUD baud. */
    tiku_uart_init();

    /* Burn 100 ms before the first message so any stale boot bytes
     * from the FT232 / picotool reset settle. */
    tiku_cpu_rp2350_delay_ms(100);

    /* Two-line preamble — easy to spot even if the UART line had
     * pre-existing junk in the picocom buffer. */
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Pico 2 W) ---\n");

    unsigned long clk = tiku_cpu_rp2350_smclk_get_hz();
    int fault         = tiku_cpu_rp2350_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        tiku_rp2350_gpio_toggle(25U);
        tiku_cpu_rp2350_delay_ms(500U);
        i++;
    }

    return 0;
}
