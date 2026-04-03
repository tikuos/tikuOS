/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wake.c - "wake" command implementation
 *
 * Shows which interrupt sources can wake the CPU from low-power
 * mode and which LPM levels each source supports.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_wake.h"
#include <kernel/shell/tiku_shell.h>
#include "tiku.h"
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_wake(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    SHELL_PRINTF("Wake sources:\n");

    /* Timer A0 — system clock, always active */
    {
        uint8_t active = (TA0CTL & MC__UP) != 0;
        SHELL_PRINTF("  Timer A0 (sys clock)  %s  wakes LPM0-3\n",
                     active ? "[on ]" : "[off]");
    }

    /* Timer A1 — hardware timer (rtimer) */
    {
        uint8_t active = (TA1CTL & MC__UP) != 0;
        SHELL_PRINTF("  Timer A1 (htimer)     %s  wakes LPM0-3\n",
                     active ? "[on ]" : "[off]");
    }

    /* UART RX — eUSCI_A0 */
    {
        uint8_t active = (UCA0IE & UCRXIE) != 0;
        SHELL_PRINTF("  UART RX  (eUSCI_A0)   %s  wakes LPM0\n",
                     active ? "[on ]" : "[off]");
    }

    /* Watchdog interval timer */
    {
        uint8_t active = (SFRIE1 & WDTIE) != 0;
        SHELL_PRINTF("  Watchdog (interval)   %s  wakes LPM0-3\n",
                     active ? "[on ]" : "[off]");
    }

    /* GPIO port interrupt enable (P1-P4) */
    {
        uint8_t p1 = P1IE != 0;
        uint8_t p2 = P2IE != 0;
        uint8_t p3 = P3IE != 0;
        uint8_t p4 = P4IE != 0;

        if (p1 || p2 || p3 || p4) {
            SHELL_PRINTF("  GPIO IRQ              [on ]  wakes LPM0-4\n");
            if (p1) {
                SHELL_PRINTF("    P1IE = 0x%02X\n", P1IE);
            }
            if (p2) {
                SHELL_PRINTF("    P2IE = 0x%02X\n", P2IE);
            }
            if (p3) {
                SHELL_PRINTF("    P3IE = 0x%02X\n", P3IE);
            }
            if (p4) {
                SHELL_PRINTF("    P4IE = 0x%02X\n", P4IE);
            }
        } else {
            SHELL_PRINTF("  GPIO IRQ              [off]  wakes LPM0-4\n");
        }
    }

    SHELL_PRINTF("\nNote: LPM4 disables all clocks.\n");
    SHELL_PRINTF("  Only GPIO IRQ can wake from LPM4.\n");
}
