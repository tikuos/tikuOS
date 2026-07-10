/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - nRF54L clock/power boot bring-up
 *
 * The nRF54L core runs at 128 MHz on this DK (OSCILLATORS.PLL.CURRENTFREQ
 * reads CK128M on hardware); this file does not change the core frequency.
 * It starts the 32 MHz HFXO so the UARTE (16 MHz reference) and, later, the
 * radio have an accurate high-frequency source.  A start-timeout latches a
 * clock-fault flag reported through the /sys clock view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_freq_boot_arch.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/mdk/nrf54l15.h>

#define TIKU_NORDIC_CPU_HZ       128000000UL
#define TIKU_NORDIC_XOSTART_SPIN 1000000UL   /* ~loop bound, not wall-clock */

static volatile int tiku_nordic_clock_fault;

void tiku_cpu_boot_nordic_init(void)
{
    uint32_t spin;

    /* SysTick-based delays need no setup; call retained for API symmetry. */
    tiku_nordic_dwt_init();

    /* Start the HFXO (32 MHz crystal) and wait for it to report started.
     * Bounded spin so a missing/broken crystal degrades to a flagged fault
     * rather than a boot hang -- the internal source still clocks the core. */
    NRF_CLOCK_S->EVENTS_XOSTARTED = 0U;
    NRF_CLOCK_S->TASKS_XOSTART    = 1U;

    spin = TIKU_NORDIC_XOSTART_SPIN;
    while (NRF_CLOCK_S->EVENTS_XOSTARTED == 0U && spin != 0U) {
        spin--;
    }
    if (NRF_CLOCK_S->EVENTS_XOSTARTED == 0U) {
        tiku_nordic_clock_fault = 1;
    }
}

unsigned long tiku_cpu_nordic_smclk_get_hz(void)
{
    return TIKU_NORDIC_CPU_HZ;
}

int tiku_cpu_nordic_clock_has_fault(void)
{
    return tiku_nordic_clock_fault;
}
