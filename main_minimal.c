/*
 * Tiku Operating System v0.05 — minimal smoke test (ARM ports)
 *
 * No kernel, no scheduler, no shell. Just brings up clocks + console
 * and prints a heartbeat in a loop, toggling an LED each iteration.
 * Intended as a debugging aid: if THIS doesn't print, the failure is in
 * the boot/clock/console layer; if it does, the failure is higher up.
 *
 * RP2350 (UART0 @115200 on GP0/GP1, LED on GP25):
 *   make MCU=rp2350 MINIMAL=1
 *   sudo picotool load -fx main.uf2
 *   make monitor MCU=rp2350
 *
 * Apollo510 EVB (console on SWO/ITM @1MHz, LED0 on pad 165):
 *   make MCU=apollo510 MINIMAL=1
 *   <flash main.bin to MRAM 0x410000 via J-Link>
 *   <open a SWO viewer at 1 MHz / SWOClock=1000>
 *
 * Expected output:
 *   TikuOS minimal: hello #0  clk=... Hz  fault=0
 *   TikuOS minimal: hello #1  clk=... Hz  fault=0
 *   ...
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#if defined(PLATFORM_AMBIQ)

#include "arch/ambiq/tiku_cpu_freq_boot_arch.h"
#include "arch/ambiq/tiku_cpu_common.h"
#include "arch/ambiq/tiku_uart_arch.h"
#include "arch/ambiq/tiku_gpio_arch.h"

/* EVB LED0 (open-drain, active-low in the BSP). The SWO heartbeat is the
 * primary signal; the LED toggle is a secondary scope-visible indicator. */
#define TIKU_MIN_LED_PAD   165u

int main(void)
{
    /* Power/clock/cache bring-up (am_bsp_low_power_init under the hood —
     * note its ~2 s silicon-settle delay before the first print). */
    tiku_cpu_boot_ambiq_init();

    tiku_ambiq_gpio_init_output(TIKU_MIN_LED_PAD);

    /* SWO/ITM console @1MHz (bind am_util_stdio to am_hal_itm_print). */
    tiku_uart_init();

    tiku_cpu_ambiq_delay_ms(100);
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Apollo510 EVB) ---\n");

    unsigned long clk = tiku_cpu_ambiq_smclk_get_hz();
    int           fault = tiku_cpu_ambiq_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        tiku_ambiq_gpio_toggle(TIKU_MIN_LED_PAD);
        tiku_cpu_ambiq_delay_ms(500u);
        i++;
    }

    return 0;
}

#else /* PLATFORM_RP2350 */

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

#endif /* PLATFORM_* */
