/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - nRF54L clock/power boot bring-up
 *
 * Programs the core PLL to 128 MHz explicitly.  The PLL's reset default is
 * CK64M, and whether a given boot arrives at 64 or 128 MHz turns out to
 * depend on the debug session: with nrfutil/J-Link attached CURRENTFREQ
 * reads CK128M (the Phase-0 observation this file used to trust), but a
 * STANDALONE boot runs at 64 MHz -- which made every SysTick busy-delay 2x
 * slow (surfaced by the watchdog C-unit test: 500 ms kick delays stretched
 * past the 1 s timeout, reset-looping the device).  Setting PLL.FREQ here
 * removes the ambiguity; the delay layer additionally reads CURRENTFREQ so
 * its math is correct at either speed.
 *
 * Also starts the 32 MHz HFXO so the UARTE (16 MHz reference) and, later,
 * the radio have an accurate high-frequency source.  A start-timeout
 * latches a clock-fault flag reported through the /sys clock view.
 *
 * Erratum 39 ("Device can behave erratically after XOSTART"): if XOSTART
 * is triggered while the PLLSTART task has never been triggered and the
 * CPU later sleeps, peripherals OUTSIDE the MCU power domain -- i.e. the
 * RADIO -- can behave erratically and the device can become unresponsive.
 * TikuOS idles in WFI constantly (tickless), so the prescribed workaround
 * is mandatory here: trigger CLOCK.TASKS_PLLSTART (pin the PLL on,
 * independent of automatic clock requests) BEFORE CLOCK.TASKS_XOSTART.
 * TikuOS never issues XOSTOP, so the paired PLLSTOP is not needed.
 *
 * The HFXO wait also covers EVENTS_XOTUNED: XOSTART kicks an automatic
 * load-capacitor tuning pass, and only a TUNED crystal is radio-grade
 * (carrier accuracy).  An untuned-but-running XO is fine for UARTE, so
 * XOTUNED timeout latches the same non-fatal clock-fault flag.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_freq_boot_arch.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_nordic_mdk.h>

#define TIKU_NORDIC_CPU_HZ       128000000UL
#define TIKU_NORDIC_XOSTART_SPIN 1000000UL   /* ~loop bound, not wall-clock */
#define TIKU_PLL_CK128M          0x1UL       /* OSCILLATORS_PLL_FREQ_CK128M */

static volatile int tiku_nordic_clock_fault;

void tiku_cpu_boot_nordic_init(void)
{
    uint32_t spin;

    /* SysTick-based delays need no setup; call retained for API symmetry. */
    tiku_nordic_dwt_init();

    /* Core PLL -> 128 MHz (the reset default is CK64M).  Bounded wait for
     * the switch to be reported; on timeout latch the clock fault (delays
     * still time correctly -- they read CURRENTFREQ, not this request). */
    NRF_OSCILLATORS_S->PLL.FREQ = TIKU_PLL_CK128M;
    spin = TIKU_NORDIC_XOSTART_SPIN;
    while ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != TIKU_PLL_CK128M &&
           spin != 0U) {
        spin--;
    }
    if ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != TIKU_PLL_CK128M) {
        tiku_nordic_clock_fault = 1;
    }

    /* Erratum 39: pin the PLL on via its explicit task BEFORE XOSTART (see
     * the header comment).  The PLL already clocks the core, so PLLSTARTED
     * reports quickly; bounded anyway. */
    NRF_CLOCK_S->EVENTS_PLLSTARTED = 0U;
    NRF_CLOCK_S->TASKS_PLLSTART    = 1U;
    spin = TIKU_NORDIC_XOSTART_SPIN;
    while (NRF_CLOCK_S->EVENTS_PLLSTARTED == 0U && spin != 0U) {
        spin--;
    }
    if (NRF_CLOCK_S->EVENTS_PLLSTARTED == 0U) {
        tiku_nordic_clock_fault = 1;
    }

    /* Start the HFXO (32 MHz crystal) and wait for it to report started.
     * Bounded spin so a missing/broken crystal degrades to a flagged fault
     * rather than a boot hang -- the internal source still clocks the core. */
    NRF_CLOCK_S->EVENTS_XOSTARTED = 0U;
    NRF_CLOCK_S->EVENTS_XOTUNED   = 0U;
    NRF_CLOCK_S->TASKS_XOSTART    = 1U;

    spin = TIKU_NORDIC_XOSTART_SPIN;
    while (NRF_CLOCK_S->EVENTS_XOSTARTED == 0U && spin != 0U) {
        spin--;
    }
    if (NRF_CLOCK_S->EVENTS_XOSTARTED == 0U) {
        tiku_nordic_clock_fault = 1;
    }

    /* Radio-grade accuracy needs the post-start tuning pass to finish. */
    spin = TIKU_NORDIC_XOSTART_SPIN;
    while (NRF_CLOCK_S->EVENTS_XOTUNED == 0U && spin != 0U) {
        spin--;
    }
    if (NRF_CLOCK_S->EVENTS_XOTUNED == 0U) {
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

/*---------------------------------------------------------------------------*/
/* Additional clock queries + frequency init (for the shared CPU HAL)        */
/*---------------------------------------------------------------------------*/

void tiku_cpu_freq_nordic_init(unsigned int cpu_freq)
{
    /* The core PLL is fixed at 128 MHz by the boot configuration; TikuOS does
     * not reprogram it (a runtime 64/128 MHz switch is a later refinement), so
     * this is a no-op accepted for HAL symmetry. */
    (void)cpu_freq;
}

unsigned long tiku_cpu_nordic_clock_get_hz(void)
{
    return TIKU_NORDIC_CPU_HZ;      /* MCLK == core clock (128 MHz) */
}

unsigned long tiku_cpu_nordic_aclk_get_hz(void)
{
    return 32768UL;                 /* ACLK == 32.768 kHz LFCLK */
}

void tiku_cpu_boot_nordic_power_wfi_enter(void)
{
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("wfi" ::: "memory");
}
