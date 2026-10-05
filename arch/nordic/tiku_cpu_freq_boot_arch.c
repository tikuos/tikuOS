/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - nRF54L clock and power boot bring-up.
 *
 * Sets the core PLL to the next-boot rate (64 or 128 MHz), then starts the
 * 32 MHz HFXO, with PLLSTART ahead of XOSTART for erratum 39.  The core rate
 * is set here once and never changes on a running system.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_freq_boot_arch.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include <arch/nordic/tiku_power_arch.h>

#define TIKU_NORDIC_XOSTART_SPIN 1000000UL   /* ~loop bound, not wall-clock */
#define TIKU_PLL_CK128M          0x1UL       /* OSCILLATORS_PLL_FREQ_CK128M */
#define TIKU_PLL_CK64M           0x3UL       /* OSCILLATORS_PLL_FREQ_CK64M  */

/*
 * The core runs at 64 or 128 MHz, chosen and applied once at boot; 128 MHz is
 * the default.  The CPU runs from HCLK128M; the other HFCLK rails (PCLK32M,
 * PCLK16M, PCLK1M) feed peripherals only, and there is no core divider, so
 * 64 MHz is the floor (datasheet table 16, section 5.5.3).
 */
/** @brief Set when a clock source fails to start at boot. */
static volatile int tiku_nordic_clock_fault;

void tiku_cpu_boot_nordic_init(void)
{
    uint32_t spin;
    uint32_t want = tiku_cpu_nordic_target_hz() == 64000000UL
                        ? TIKU_PLL_CK64M : TIKU_PLL_CK128M;

    /* SysTick-based delays need no setup; the call is a no-op. */
    tiku_nordic_dwt_init();

    /*
     * The frequency may be set only here.  Datasheet 5.5.3: "The device
     * starts at 64 MHz.  For 128 MHz, it must be configured when the CPU
     * starts and before any peripherals that use the high-frequency clock are
     * enabled.  Changing the frequency on a running system or to an
     * unsupported value causes undefined system behavior and the device can
     * malfunction."  tiku_boot_init_cpu() runs this stage before memory,
     * peripherals and services, so no HF clock request precedes the write.
     *
     * The value is written even when it equals the reset default: an
     * attached debug session can leave the PLL at 128 MHz across a reset.
     * The wait for the switch is bounded; a timeout latches the clock fault,
     * and delays still time correctly because they read CURRENTFREQ.
     */
    NRF_OSCILLATORS_S->PLL.FREQ = want;
    spin = TIKU_NORDIC_XOSTART_SPIN;
    while ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != want &&
           spin != 0U) {
        spin--;
    }
    if ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != want) {
        tiku_nordic_clock_fault = 1;
    }

    /*
     * Erratum 39 ("device can behave erratically after XOSTART"): if XOSTART
     * is triggered while PLLSTART never was and the CPU later sleeps,
     * peripherals outside the MCU power domain (the RADIO) can misbehave and
     * the device can hang.  TikuOS idles in WFI, so the PLL is started with
     * its own task before XOSTART.  No PLLSTOP follows, because XOSTOP is
     * never issued.
     *
     * The PLL already clocks the core, so PLLSTARTED reports quickly; the
     * wait is bounded all the same.
     */
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
     * The spin is bounded: a missing or broken crystal sets the clock fault
     * and boot continues on the internal source. */
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

    /* Cache and DC/DC come last, after the frequency write.  Neither
     * requests the HF clock; anything this stage enables goes after the
     * write. */
    tiku_nordic_power_boot_init();
}

/*
 * The HFCLK controller feeds the core HCLK128M (64 or 128 MHz) and the
 * peripherals PCLK32M, PCLK16M and PCLK1M (datasheet table 16).  This
 * returns PCLK16M, the reference for the UARTE baud constant and the
 * htimer's TIMER20 (16 MHz prescaled to 1 MHz).
 */
unsigned long tiku_cpu_nordic_smclk_get_hz(void)
{
    return 16000000UL;              /* PCLK16M -- see the note above */
}

int tiku_cpu_nordic_clock_has_fault(void)
{
    return tiku_nordic_clock_fault;
}

/*---------------------------------------------------------------------------*/
/* Additional clock queries + frequency init (for the shared CPU HAL)        */
/*---------------------------------------------------------------------------*/

/*
 * Runtime frequency request: a no-op.  Datasheet 5.5.3 forbids changing the
 * core frequency on a running system.  A saved rate
 * (tiku_cpu_nordic_target_set()) takes effect at the next
 * tiku_cpu_boot_nordic_init(); tiku_cpu_mclk_hz() keeps reading the
 * hardware, so the shell's "freq" reports a request as not applied.
 */
void tiku_cpu_freq_nordic_init(unsigned int cpu_freq)
{
    (void)cpu_freq;
}

unsigned long tiku_cpu_nordic_clock_get_hz(void)
{
    /* The running rate from the PLL, not the requested one. */
    return tiku_nordic_cpu_hz_now();
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
