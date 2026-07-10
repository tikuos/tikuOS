/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - nRF54L busy delays (SysTick) + system reset
 *
 * Delays use SysTick as a polled one-shot down-counter clocked from the
 * processor clock.  SysTick is core-internal and runs whether or not a
 * debugger is attached -- unlike the DWT cycle counter, whose CYCCNT can be
 * frozen without an active trace clock (an early nRF54L bring-up used DWT and
 * hung the delay loop when run standalone, printing nothing over UART).  The
 * kernel tick will live on the GRTC (low-power, always-on), so SysTick stays
 * free for busy-delays in the full build too.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_nordic_core.h>

/* The nRF54L15-DK boots with the PLL at 128 MHz (OSCILLATORS.PLL.CURRENTFREQ
 * reads CK128M on hardware -- the CK64M register reset value is overridden by
 * the boot configuration).  SysTick is clocked from this processor clock, so
 * the delay math must use 128 MHz or delays run 2x fast. */
#define TIKU_NORDIC_CPU_HZ   128000000UL
#define TIKU_SYSTICK_MAX     0x00FFFFFFUL   /* SysTick reload is 24-bit */

void tiku_nordic_dwt_init(void)
{
    /* Retained for API compatibility (boot bring-up calls it); SysTick-based
     * delays need no pre-initialisation, so this is intentionally a no-op. */
}

/** @brief Busy-wait for @p cycles core cycles using SysTick (polled). */
static void tiku_nordic_delay_cycles(uint32_t cycles)
{
    while (cycles != 0U) {
        uint32_t chunk = (cycles > TIKU_SYSTICK_MAX) ? TIKU_SYSTICK_MAX : cycles;

        TIKU_SYSTICK->CTRL = 0U;                 /* stop + clear COUNTFLAG   */
        TIKU_SYSTICK->LOAD = chunk;              /* counts chunk+1 cycles    */
        TIKU_SYSTICK->VAL  = 0U;                 /* clear current + flag     */
        TIKU_SYSTICK->CTRL = TIKU_SYSTICK_CTRL_ENABLE |
                             TIKU_SYSTICK_CTRL_CLKSOURCE;   /* proc clock    */

        while ((TIKU_SYSTICK->CTRL & TIKU_SYSTICK_CTRL_COUNTFLAG) == 0U) {
            /* wait until the counter underflows past zero */
        }
        TIKU_SYSTICK->CTRL = 0U;
        cycles -= chunk;
    }
}

void tiku_cpu_nordic_delay_us(uint32_t us)
{
    tiku_nordic_delay_cycles(us * (TIKU_NORDIC_CPU_HZ / 1000000UL));
}

void tiku_cpu_nordic_delay_ms(uint32_t ms)
{
    while (ms-- != 0U) {
        tiku_cpu_nordic_delay_us(1000U);
    }
}

/*---------------------------------------------------------------------------*/
/* System reset                                                              */
/*---------------------------------------------------------------------------*/

void tiku_cpu_nordic_reset(void)
{
    tiku_nordic_system_reset();   /* SCB AIRCR SYSRESETREQ; does not return */
}
