/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wake.c - "wake" command implementation
 *
 * Shows which interrupt sources can wake the CPU from low-power
 * mode and which LPM levels each source supports. Wake-source
 * detection goes through the platform-agnostic wake HAL
 * (hal/tiku_wake_hal.h); the tables below describe how each
 * MSP430 LPM level interacts with each source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_wake.h"
#include <kernel/shell/tiku_shell.h>
#include <hal/tiku_wake_hal.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_wake(uint8_t argc, const char *argv[])
{
    tiku_wake_sources_t w;
    uint8_t i;

    (void)argc;
    (void)argv;

    tiku_wake_arch_query(&w);

    SHELL_PRINTF("Wake sources:\n");

#if defined(PLATFORM_NORDIC)
    /* nRF54L: every idle mode is a WFI variant, so every NVIC-enabled
     * source below wakes the core; the names are the real peripherals
     * (arch/nordic/tiku_wake_arch.c scans the live NVIC lines). */
    SHELL_PRINTF("  GRTC     (sys tick)   %s  wakes WFI\n",
                 (w.sources & TIKU_WAKE_SYSTICK) ? "[on ]" : "[off]");

    SHELL_PRINTF("  TIMER20  (htimer)     %s  wakes WFI\n",
                 (w.sources & TIKU_WAKE_HTIMER) ? "[on ]" : "[off]");

    SHELL_PRINTF("  UARTE RX (console)    %s  wakes WFI\n",
                 (w.sources & TIKU_WAKE_UART_RX) ? "[on ]" : "[off]");

    SHELL_PRINTF("  WDT30    (watchdog)   %s  wakes WFI\n",
                 (w.sources & TIKU_WAKE_WDT) ? "[on ]" : "[off]");

    if (w.sources & TIKU_WAKE_GPIO) {
        SHELL_PRINTF("  GPIOTE   (gpio irq)   [on ]  wakes WFI\n");
        for (i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
            if (w.gpio_ie[i]) {
                /* SHELL_PRINTF's tiny formatter has no width/%X -- plain %x */
                SHELL_PRINTF("    P%u armed pins = 0x%x\n",
                             (unsigned)i, (unsigned)w.gpio_ie[i]);
            }
        }
    } else {
        SHELL_PRINTF("  GPIOTE   (gpio irq)   [off]  wakes WFI\n");
    }

    SHELL_PRINTF("\nNote: nRF54L idle = WFI at every LPM level;\n");
    SHELL_PRINTF("  any enabled source above wakes the core.\n");
#else
    SHELL_PRINTF("  Timer A0 (sys clock)  %s  wakes LPM0-3\n",
                 (w.sources & TIKU_WAKE_SYSTICK) ? "[on ]" : "[off]");

    SHELL_PRINTF("  Timer A1 (htimer)     %s  wakes LPM0-3\n",
                 (w.sources & TIKU_WAKE_HTIMER) ? "[on ]" : "[off]");

    SHELL_PRINTF("  UART RX  (eUSCI_A0)   %s  wakes LPM0\n",
                 (w.sources & TIKU_WAKE_UART_RX) ? "[on ]" : "[off]");

    SHELL_PRINTF("  Watchdog (interval)   %s  wakes LPM0-3\n",
                 (w.sources & TIKU_WAKE_WDT) ? "[on ]" : "[off]");

    if (w.sources & TIKU_WAKE_GPIO) {
        SHELL_PRINTF("  GPIO IRQ              [on ]  wakes LPM0-4\n");
        for (i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
            if (w.gpio_ie[i]) {
                SHELL_PRINTF("    P%uIE = 0x%02X\n",
                             (unsigned)(i + 1), w.gpio_ie[i]);
            }
        }
    } else {
        SHELL_PRINTF("  GPIO IRQ              [off]  wakes LPM0-4\n");
    }

    SHELL_PRINTF("\nNote: LPM4 disables all clocks.\n");
    SHELL_PRINTF("  Only GPIO IRQ can wake from LPM4.\n");
#endif
}
