/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.c - nRF54L cache + DC/DC.  See tiku_power_arch.h for why.
 *
 * HOW THESE WERE FOUND.  A Joulescope on the LM20-DK's P14 header measured the
 * board drawing 6.76 mA with the shell idle, against a datasheet figure of
 * 2.6 mA for the same part running CoreMark.  Doing nothing cost more than
 * doing work.  Chasing that gap turned up three registers the port had never
 * written -- the cache and the DC/DC live here; the third (an idle hook) is
 * the scheduler's, not the arch's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_power_arch.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include <stddef.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* CACHE                                                                     */
/*---------------------------------------------------------------------------*/

/*
 * ICACHE lives at 0xE0082000 in the Arm private-peripheral region and has NO
 * instance define in any of the three MDK headers -- only the NRF_CACHE_Type
 * layout and the CACHE_* field macros are vendored.  So the base comes from
 * the datasheet's own instance table (4.2.3.4: "ICACHE, APPLICATION,
 * 0xE0082000") and the layout comes from the MDK, which is the right split:
 * the address is a documented fact, the offsets are the vendor's business.
 *
 * Configuration, from the same table: 8 KB, 128 sets, two-way set associative,
 * 64-bit data unit, 4 data units per line, LRU replacement.  Both instruction
 * AND data accesses to NVM are cached.  No flush and no clean are supported --
 * only invalidate -- which is why the disable path below invalidates rather
 * than trying to write back.
 */
#define TIKU_NORDIC_CACHE ((NRF_CACHE_Type *)0xE0082000UL)

void tiku_nordic_cache_set(int on)
{
    if (on) {
        /* Invalidate before enabling.  The cache retains tag state across a
         * disable, so enabling without invalidating could serve lines that
         * describe NVM as it was before an intervening write -- and this port
         * does write NVM at run time (the file store, persist cells, BASIC
         * saves).  The datasheet notes writes to cached memory are
         * write-around and invalidate their line, but that only covers writes
         * made while the cache was ON. */
        TIKU_NORDIC_CACHE->TASKS_INVALIDATECACHE = 1UL;
        __asm__ volatile ("dsb 0xF" ::: "memory");
        TIKU_NORDIC_CACHE->ENABLE = CACHE_ENABLE_ENABLE_Enabled;
    } else {
        TIKU_NORDIC_CACHE->ENABLE = CACHE_ENABLE_ENABLE_Disabled;
        __asm__ volatile ("dsb 0xF" ::: "memory");
        TIKU_NORDIC_CACHE->TASKS_INVALIDATECACHE = 1UL;
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF" ::: "memory");
}

int tiku_nordic_cache_enabled(void)
{
    return (TIKU_NORDIC_CACHE->ENABLE & CACHE_ENABLE_ENABLE_Msk) != 0UL;
}

void tiku_nordic_cache_profile_start(void)
{
    TIKU_NORDIC_CACHE->PROFILING.ENABLE = CACHE_PROFILING_ENABLE_ENABLE_Enable;
    TIKU_NORDIC_CACHE->PROFILING.CLEAR  = CACHE_PROFILING_CLEAR_CLEAR_Clear;
}

void tiku_nordic_cache_profile_read(uint32_t *hits, uint32_t *misses,
                                    uint32_t *reads, uint32_t *writes)
{
    if (hits != NULL) {
        *hits = TIKU_NORDIC_CACHE->PROFILING.HIT;
    }
    if (misses != NULL) {
        *misses = TIKU_NORDIC_CACHE->PROFILING.MISS;
    }
    if (reads != NULL) {
        *reads = TIKU_NORDIC_CACHE->PROFILING.READS;
    }
    if (writes != NULL) {
        *writes = TIKU_NORDIC_CACHE->PROFILING.WRITES;
    }
}

/*---------------------------------------------------------------------------*/
/* SUPPLY                                                                    */
/*---------------------------------------------------------------------------*/

/*
 * INDUCTORDET IS ONLY MEANINGFUL WHILE THE CONVERTER IS OFF.  Datasheet
 * 5.7.2.4.2: "The detection can only take place if the DC/DC converter is not
 * enabled (VREGMAIN.DCDCEN = 0)."
 *
 * Reading it at any other time returns a stale or meaningless value, and this
 * cost real confusion: a first cut reported "dcdc on, inductor absent", which
 * is not a state the hardware can be in.  The converter was on -- so the
 * detector could not run -- and the zero it read was reported as an answer.
 *
 * @return 1 detected, 0 not detected, -1 cannot tell (converter is on).
 */
int tiku_nordic_dcdc_inductor_present(void)
{
    if (tiku_nordic_dcdc_enabled()) {
        return -1;
    }
    return (NRF_REGULATORS_S->VREGMAIN.INDUCTORDET &
            REGULATORS_VREGMAIN_INDUCTORDET_DETECTED_Msk) != 0UL;
}

int tiku_nordic_dcdc_enabled(void)
{
    return (NRF_REGULATORS_S->VREGMAIN.DCDCEN &
            REGULATORS_VREGMAIN_DCDCEN_VAL_Msk) != 0UL;
}

int tiku_nordic_dcdc_probe_inductor(void)
{
    int was_on = tiku_nordic_dcdc_enabled();
    int det;

    /* Detection needs the converter off, so take it off, look, and put it
     * back exactly as it was.  Momentary LDO operation is the reset state and
     * is always safe; the reverse -- guessing -- is not. */
    if (was_on) {
        NRF_REGULATORS_S->VREGMAIN.DCDCEN =
            REGULATORS_VREGMAIN_DCDCEN_VAL_Disabled;
        __asm__ volatile ("dsb 0xF" ::: "memory");
    }
    det = (NRF_REGULATORS_S->VREGMAIN.INDUCTORDET &
           REGULATORS_VREGMAIN_INDUCTORDET_DETECTED_Msk) != 0UL;
    if (was_on) {
        NRF_REGULATORS_S->VREGMAIN.DCDCEN =
            REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled;
        __asm__ volatile ("dsb 0xF" ::: "memory");
    }
    return det;
}

int tiku_nordic_dcdc_set(int on)
{
    /*
     * NO SOFTWARE GUARD, DELIBERATELY.  An earlier version refused to write
     * DCDCEN unless it had already seen an inductor, which was both wrong and
     * unnecessary: the SILICON does this check, at exactly the right moment.
     * Datasheet 5.7.2.4: "When enabling the DC/DC regulator, the device checks
     * if an inductor is connected to the DCC pin.  If an inductor is not
     * detected, the device remains in LDO mode."
     *
     * The guard also broke the thing it was guarding.  Detection only runs
     * while DCDCEN is 0, so refusing to set DCDCEN on the strength of a prior
     * INDUCTORDET read made the outcome depend on when that read happened
     * rather than on the board.  Writing the bit and letting the part decide
     * is both simpler and the documented sequence.
     */
    NRF_REGULATORS_S->VREGMAIN.DCDCEN = on
        ? REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled
        : REGULATORS_VREGMAIN_DCDCEN_VAL_Disabled;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    return tiku_nordic_dcdc_enabled();
}

/*---------------------------------------------------------------------------*/
/* CACHE WORKLOAD                                                            */
/*---------------------------------------------------------------------------*/

/*
 * 16 KB of NVM -- twice the 8 KB cache -- so a full pass cannot be resident and
 * the traversal keeps missing.  Sized deliberately above the cache rather than
 * below it: a working set that FITS would report a flattering hit rate and a
 * power difference that no real workload would ever see.
 */
#define TIKU_CACHE_WL_WORDS   4096u
#define TIKU_CACHE_WL_STRIDE  17u     /* coprime with the line size (see below) */
#define TIKU_CACHE_WL_PASSES  64u

static const uint32_t tiku_cache_wl_data[TIKU_CACHE_WL_WORDS] = { 0 };

uint32_t tiku_nordic_cache_workload(uint32_t *out_us)
{
    uint32_t sum = 0u, i, pass, idx = 0u;
    uint32_t t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;

    for (pass = 0u; pass < TIKU_CACHE_WL_PASSES; pass++) {
        for (i = 0u; i < TIKU_CACHE_WL_WORDS; i++) {
            /* Stride 17 words is coprime with the 4-word (16-byte) line, so
             * successive reads land in different lines and no two consecutive
             * accesses share one -- sequential prefetch cannot help, and the
             * cache is exercised on its actual job of retaining scattered
             * lines rather than on streaming. */
            idx = (idx + TIKU_CACHE_WL_STRIDE) % TIKU_CACHE_WL_WORDS;
            sum += tiku_cache_wl_data[idx];
            sum ^= idx;
        }
    }
    if (out_us != NULL) {
        *out_us = (uint32_t)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0);
    }
    return sum;
}

/*---------------------------------------------------------------------------*/
/* CLOCK ORACLE                                                              */
/*---------------------------------------------------------------------------*/

/*
 * MEASURE THE CORE CLOCK AGAINST A CLOCK THAT CANNOT MOVE WITH IT.
 *
 * SysTick is clocked from the processor clock; the GRTC's SYSCOUNTER runs at
 * 1 MHz from an entirely separate source.  Counting one against the other
 * yields the core rate in Hz with nothing external attached and no calibrated
 * constant to drift -- which is the whole point: reading PLL.CURRENTFREQ tells
 * you what the register SAYS, and this tells you what the core is DOING.
 *
 * That distinction is not academic on this part.  A standalone boot was once
 * observed running at 64 MHz while a debugger-attached boot of the same image
 * reported 128, and every busy-delay silently ran at half speed until someone
 * measured rather than asked.
 *
 * SysTick is a 24-bit DOWN counter, so it wraps after 16.77 M cycles -- 131 ms
 * at 128 MHz.  The window below is 50 ms, which is 6.4 M cycles at 128 MHz and
 * 3.2 M at 64: comfortably inside one span at either rate, so no wrap handling
 * is needed and none is written (unwritten wrap handling is better than
 * untested wrap handling).
 */
#define TIKU_CLKMEAS_WINDOW_US   50000UL

unsigned long tiku_nordic_cpu_hz_measure(void)
{
    uint32_t save_ctrl, save_load;
    uint32_t t0, t1, s0, s1, cycles, elapsed_us;

    save_ctrl = TIKU_SYSTICK->CTRL;
    save_load = TIKU_SYSTICK->LOAD;

    /* Free-run SysTick over the full 24-bit span, processor-clocked, no IRQ. */
    TIKU_SYSTICK->CTRL = 0U;
    TIKU_SYSTICK->LOAD = 0x00FFFFFFUL;
    TIKU_SYSTICK->VAL  = 0U;
    TIKU_SYSTICK->CTRL = TIKU_SYSTICK_CTRL_ENABLE | TIKU_SYSTICK_CTRL_CLKSOURCE;

    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    s0 = TIKU_SYSTICK->VAL;
    do {
        t1 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    } while ((uint32_t)(t1 - t0) < TIKU_CLKMEAS_WINDOW_US);
    s1 = TIKU_SYSTICK->VAL;

    TIKU_SYSTICK->CTRL = 0U;
    TIKU_SYSTICK->LOAD = save_load;
    TIKU_SYSTICK->VAL  = 0U;
    TIKU_SYSTICK->CTRL = save_ctrl;

    /* SysTick counts DOWN, so elapsed cycles is s0 - s1 modulo the span. */
    cycles     = (s0 - s1) & 0x00FFFFFFUL;
    elapsed_us = (uint32_t)(t1 - t0);
    if (elapsed_us == 0U) {
        return 0UL;
    }
    /* 64-bit intermediate: cycles * 1e6 overflows 32 bits above ~4295 cycles. */
    return (unsigned long)(((uint64_t)cycles * 1000000ULL) / elapsed_us);
}

/*---------------------------------------------------------------------------*/
/* BOOT                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_nordic_power_boot_init(void)
{
    /* Cache first: it changes how every subsequent instruction fetch is
     * served, so the earlier it is on, the more of boot it covers. */
#if !defined(TIKU_NORDIC_CACHE_DISABLE) || !TIKU_NORDIC_CACHE_DISABLE
    tiku_nordic_cache_set(1);
#endif

    /* Then the supply.  Silent when there is no inductor -- an LDO board is a
     * perfectly valid board, just a thirstier one. */
#if !defined(TIKU_NORDIC_DCDC_DISABLE) || !TIKU_NORDIC_DCDC_DISABLE
    (void)tiku_nordic_dcdc_set(1);
#endif
}
