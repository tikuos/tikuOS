/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - nRF54L clock and power boot bring-up.
 *
 * Programs the core PLL to 128 MHz explicitly, because a standalone boot
 * otherwise lands at 64 MHz while a debug session reports 128.  Also starts the
 * 32 MHz HFXO; see the erratum 39 note at the PLLSTART/XOSTART ordering.
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
 * CORE FREQUENCY: 64 or 128 MHz, CHOSEN AT BUILD TIME, APPLIED ONCE AT BOOT.
 *
 * Those are the only two the silicon offers.  The CPU runs from HCLK128M, and
 * the other rails the HFCLK controller produces (PCLK32M / PCLK16M / PCLK1M)
 * feed peripherals only -- there is no path to clock the core from them and no
 * core divider, so 64 MHz is the floor rather than a starting point (datasheet
 * table 16 and section 5.5.3).
 *
 * 128 MHz is the default and is expected to stay it.  The part reaches
 * 3.90 CoreMark/MHz executing from RRAM with the cache on -- essentially the
 * Cortex-M33's ceiling -- so the higher clock buys close to proportionally
 * more work, and race-to-idle then wins because the same job holds the whole
 * MCU power domain up for half as long.  Dropping to 64 MHz is offered so that
 * claim can be MEASURED on a given workload rather than assumed, which is what
 * the TikuBench power suite does with it.
 */
#ifndef TIKU_NORDIC_CPU_MHZ
#define TIKU_NORDIC_CPU_MHZ      128
#endif
#if (TIKU_NORDIC_CPU_MHZ != 64) && (TIKU_NORDIC_CPU_MHZ != 128)
#error "TIKU_NORDIC_CPU_MHZ must be 64 or 128 -- the part supports nothing else"
#endif

#if (TIKU_NORDIC_CPU_MHZ == 128)
#define TIKU_PLL_WANT            TIKU_PLL_CK128M
#else
#define TIKU_PLL_WANT            TIKU_PLL_CK64M
#endif

static volatile int tiku_nordic_clock_fault;

void tiku_cpu_boot_nordic_init(void)
{
    uint32_t spin;

    /* SysTick-based delays need no setup; call retained for API symmetry. */
    tiku_nordic_dwt_init();

    /*
     * THIS IS THE ONLY MOMENT THE FREQUENCY MAY BE SET.  The datasheet is
     * explicit (5.5.3): "The device starts at 64 MHz.  For 128 MHz, it must be
     * configured when the CPU starts and before any peripherals that use the
     * high-frequency clock are enabled.  Changing the frequency on a running
     * system or to an unsupported value causes undefined system behavior and
     * the device can malfunction."
     *
     * Boot ordering already puts us inside that window -- tiku_boot_init_cpu()
     * runs this stage ahead of memory, peripherals and services -- so the write
     * lands before anything has requested the HF clock.  Nothing may move this
     * later, and nothing may repeat it afterwards.
     *
     * The value is written even when it matches the reset default, because
     * what a boot lands on is not always what reset would give: an attached
     * debug session has been observed leaving the PLL at 128 MHz where a
     * standalone boot came up at 64.  Writing it makes the outcome the same
     * either way.  Bounded wait for the switch to be reported; on timeout
     * latch the clock fault -- delays still time correctly, because they read
     * CURRENTFREQ rather than this request.
     */
    NRF_OSCILLATORS_S->PLL.FREQ = TIKU_PLL_WANT;
    spin = TIKU_NORDIC_XOSTART_SPIN;
    while ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != TIKU_PLL_WANT &&
           spin != 0U) {
        spin--;
    }
    if ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) != TIKU_PLL_WANT) {
        tiku_nordic_clock_fault = 1;
    }

    /*
     * ERRATUM 39 -- "device can behave erratically after XOSTART".  If XOSTART
     * is triggered while PLLSTART never was, and the CPU later sleeps, then
     * peripherals outside the MCU power domain (the RADIO) can misbehave and
     * the device can hang.  TikuOS idles in WFI constantly, so the workaround
     * is mandatory: pin the PLL on with its explicit task BEFORE XOSTART.  No
     * paired PLLSTOP is needed because XOSTOP is never issued.
     *
     * The PLL already clocks the core, so PLLSTARTED reports quickly; bounded
     * anyway.
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

    /* Cache and DC/DC last, and deliberately after the frequency is settled.
     * Neither requests the HF clock, so neither closes the window the core
     * frequency had to be chosen in -- but keeping the order one-way means a
     * future addition here cannot accidentally close it either. */
    tiku_nordic_power_boot_init();
}

/*
 * THE PERIPHERAL CLOCK IS NOT THE CORE CLOCK.  This returned the core rate,
 * which was wrong on any reading: the HFCLK controller hands the core
 * HCLK128M (64 or 128 MHz) and hands peripherals PCLK32M / PCLK16M / PCLK1M
 * (datasheet table 16).  Nothing on this part runs peripherals at 128 MHz.
 *
 * 16 MHz is the honest single answer, because it is the rate the peripherals
 * TikuOS actually configures are referenced to -- UARTE's baud constant, the
 * htimer's TIMER20 (16 MHz prescaled to 1 MHz) and the alternate TIMER10 tick.
 * A part with several peripheral rates cannot be summarised in one number, and
 * this HAL entry only offers one; reporting the rate our drivers derive from
 * is more useful than reporting a rate nothing uses.
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

/**
 * @brief Runtime frequency request -- DELIBERATELY A NO-OP.  Do not implement.
 *
 * This is not an unfinished feature, and the earlier comment here calling a
 * runtime switch "a later refinement" was wrong.  The datasheet forbids it
 * outright (5.5.3): "Changing the frequency on a running system or to an
 * unsupported value causes undefined system behavior and the device can
 * malfunction."
 *
 * The core clock and the MCU power domain are the same rail (HCLK128M), so
 * there is no sanctioned sequence for moving it once peripherals hold clock
 * requests -- unlike, say, the cache, which the same datasheet explicitly does
 * allow to be toggled at run time.
 *
 * Frequency is therefore selected at BUILD time via TIKU_NORDIC_CPU_MHZ and
 * applied once, in tiku_cpu_boot_nordic_init(), inside the documented window.
 * Callers are not lied to: the shell's "freq" command reports the request was
 * not applied, because tiku_cpu_mclk_hz() reads the hardware.
 */
void tiku_cpu_freq_nordic_init(unsigned int cpu_freq)
{
    (void)cpu_freq;
}

unsigned long tiku_cpu_nordic_clock_get_hz(void)
{
    /* Read the hardware, do not repeat the request.  This used to return the
     * TIKU_NORDIC_CPU_HZ constant, so it claimed 128 MHz even on the boots
     * that actually came up at 64 -- the exact failure the delay layer already
     * had to work around by reading CURRENTFREQ itself. */
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
