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

/* EVB LED0: Apollo510 pad 165, Apollo4 Lite pad 12 (active-low in the BSP).
 * The console heartbeat is the primary signal; the LED toggle is a secondary
 * scope-visible indicator. */
#if defined(TIKU_DEVICE_APOLLO4L)
#define TIKU_MIN_LED_PAD   12u
#else
#define TIKU_MIN_LED_PAD   165u
#endif

int main(void)
{
    /* Power/clock/cache bring-up (am_bsp_low_power_init under the hood —
     * note its ~2 s silicon-settle delay before the first print). */
    tiku_cpu_boot_ambiq_init();

    tiku_ambiq_gpio_init_output(TIKU_MIN_LED_PAD);

    /* SWO/ITM console @1MHz (bind am_util_stdio to am_hal_itm_print). */
    tiku_uart_init();

    tiku_cpu_ambiq_delay_ms(100);
#if defined(TIKU_DEVICE_APOLLO4L)
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Apollo4 Lite EVB) ---\n");
#else
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Apollo510 EVB) ---\n");
#endif

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

#elif defined(PLATFORM_NORDIC)

#include "arch/nordic/tiku_cpu_freq_boot_arch.h"
#include "arch/nordic/tiku_cpu_common.h"
#include "arch/nordic/tiku_uart_arch.h"
#include "arch/nordic/tiku_gpio_arch.h"

/* nRF54L15-DK LEDs (active-low: drive low = lit).  Two LEDs so the smoke test
 * is a boot beacon independent of the console UART:
 *   LED1 (P2.09) solid-on  = reached main() + GPIO works
 *   LED2 (P1.10) blinking  = reached the loop (delays work) */
#define TIKU_MIN_LED1_PORT  2u
#define TIKU_MIN_LED1_PIN   9u
#define TIKU_MIN_LED2_PORT  1u
#define TIKU_MIN_LED2_PIN   10u

/* Fixed RAM progress markers, readable over the debugger (nrfutil device read
 * <symbol addr>) so boot/loop progress can be confirmed without eyeballing an
 * LED or relying on the console UART.  Volatile so the writes are not elided. */
volatile uint32_t g_nordic_main_reached;
volatile uint32_t g_nordic_loop_count;

int main(void)
{
    /* Marker: proves execution reached main() (read this RAM word back). */
    g_nordic_main_reached = 0xB007B007u;

    /* FIRST thing: light LED1 (active-low -> level 0) so a solid LED proves
     * the reset handler ran, the C runtime came up, and GPIO works -- before
     * any clock/delay/UART code that could hang. */
    tiku_nordic_gpio_init_output(TIKU_MIN_LED1_PORT, TIKU_MIN_LED1_PIN, 0u);

    /* Start the HFXO (UARTE reference). Delays use SysTick (no setup). */
    tiku_cpu_boot_nordic_init();

    /* LED2 off initially (active-low -> drive high = off). */
    tiku_nordic_gpio_init_output(TIKU_MIN_LED2_PORT, TIKU_MIN_LED2_PIN, 1u);

    /* Console UARTE at TIKU_BOARD_UART_BAUD on the board-selected pins. */
    tiku_uart_init();

    /* Let any stale bytes from the J-Link VCOM reset settle. */
    tiku_cpu_nordic_delay_ms(100);

    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (nRF54L15-DK) ---\n");

    unsigned long clk = tiku_cpu_nordic_smclk_get_hz();
    int fault         = tiku_cpu_nordic_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        /* LED2 blinks = the loop (and the delay path) is alive. */
        tiku_nordic_gpio_toggle(TIKU_MIN_LED2_PORT, TIKU_MIN_LED2_PIN);
        tiku_cpu_nordic_delay_ms(500u);
        g_nordic_loop_count++;      /* progress marker (read over debugger) */
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
