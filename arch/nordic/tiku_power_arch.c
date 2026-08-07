/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.c - nRF54L cache and DC/DC enables.
 *
 * A Joulescope measured the LM20-DK idling at 6.76 mA against a datasheet 2.6 mA
 * running CoreMark -- doing nothing cost more than doing work.  The cache and the
 * DC/DC are two of the three registers that account for that gap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_power_arch.h>
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>   /* coprocessor work counter, sampled
                                           * in the probe window            */
#endif
#include <arch/nordic/tiku_nordic_mdk.h>
#include <stddef.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_cpu_common.h>  /* tiku_cpu_nordic_delay_ms      */
#include <arch/nordic/tiku_uart_arch.h>
#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_timer_arch.h> /* TIKU_CLOCK_ARCH_SECOND first      */
#include <kernel/cpu/tiku_hang.h>        /* check-in: probe blocks on purpose  */
#include <kernel/timers/tiku_clock.h>   /* tickless stretch for the tick flag */

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
 * INDUCTORDET is only meaningful while the converter is off.  Datasheet
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
     * No software guard, DELIBERATELY.  An earlier version refused to write
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
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

/*
 * Why these exist.  Every core-power figure this port has published comes from a
 * register-only loop -- two instructions, no loads, no stores.  That is a
 * deliberate best case and it says nothing about what memory traffic costs,
 * which is most of what real code does and all of what a durability decision
 * turns on.  These six loops price one access at a time.
 *
 * SIZING.  The cache is 8 KB, two-way, 128 sets.  HOT is 4 KB so a pass is
 * comfortably resident; COLD is 64 KB so a pass cannot be.  The stride is 17
 * words -- coprime with the line, so consecutive accesses land in different
 * sets and sequential prefetch cannot help.  Both mirror the existing
 * cache-workload constants, which were chosen the same way.
 *
 * The loops are 8x unrolled so loop overhead is a small fraction of the access
 * cost being measured, alignment-pinned because an unrelated build option once
 * moved a measured loop and changed its current by 956 uA, and every read feeds
 * a volatile sink so no access can be optimised away.
 */
#define TIKU_MEM_HOT_WORDS   1024u    /* 4 KB  -- inside the 8 KB cache      */
#define TIKU_MEM_COLD_WORDS 16384u    /* 64 KB -- 8x the cache               */
#define TIKU_MEM_STRIDE        17u    /* coprime with the line               */
#define TIKU_MEM_PASS_ACC     256u    /* accesses per accounted pass         */

static const uint32_t tiku_mem_rram_hot[TIKU_MEM_HOT_WORDS]   = { 0 };
static const uint32_t tiku_mem_rram_cold[TIKU_MEM_COLD_WORDS] = { 0 };
static uint32_t       tiku_mem_sram[TIKU_MEM_COLD_WORDS];
volatile uint32_t     tiku_mem_sink;

static uint32_t tiku_mem_accesses;
static uint32_t tiku_mem_checksum;

uint32_t tiku_nordic_mem_access_count(void) { return tiku_mem_accesses; }
uint32_t tiku_nordic_mem_checksum(void)     { return tiku_mem_checksum; }

/* One accounted pass of 256 accesses, 8x unrolled.  `idx` walks with the given
 * stride and wraps on the given mask, so one body serves every kind. */
#define TIKU_MEM_PASS_READ(arr, mask, stride)                                 \
    do {                                                                      \
        unsigned k;                                                           \
        for (k = 0u; k < TIKU_MEM_PASS_ACC / 8u; k++) {                       \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
            acc += (arr)[idx]; idx = (idx + (stride)) & (mask);                \
        }                                                                     \
    } while (0)

#define TIKU_MEM_PASS_WRITE(arr, mask, stride)                                \
    do {                                                                      \
        unsigned k;                                                           \
        for (k = 0u; k < TIKU_MEM_PASS_ACC / 8u; k++) {                        \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
            (arr)[idx] = acc; idx = (idx + (stride)) & (mask);                 \
        }                                                                     \
    } while (0)

/**
 * @brief Run one memory workload for @p ms and report elapsed microseconds.
 *
 * Access count and checksum are published separately: the count is the
 * denominator for energy per access, and the checksum lets a caller confirm
 * that two configurations being compared did the SAME work.
 */
uint32_t tiku_nordic_mem_probe(unsigned kind, uint32_t ms)
{
    uint32_t t0, now;
    uint32_t acc = 0u, idx = 0u;
    const uint32_t hot_mask  = TIKU_MEM_HOT_WORDS - 1u;
    const uint32_t cold_mask = TIKU_MEM_COLD_WORDS - 1u;

    /* SEED THE SRAM BUFFER so its traversals have a live checksum.  The RRAM
     * arrays are `const` zero-filled -- the linker emits them, and the access
     * rates prove the loads really happen (a 64 KB strided RRAM pass runs 3.3x
     * slower than the same pass over SRAM), but every word read back is 0, so
     * for those kinds the checksum is STRUCTURALLY zero and proves nothing.
     * Said plainly here rather than left to imply a verification that is not
     * happening; the access COUNT is the denominator that matters, and it is
     * validated independently by nop landing on its architectural 3 cycles. */
    if (tiku_mem_sram[0] == 0u) {
        uint32_t j;
        for (j = 0u; j < TIKU_MEM_COLD_WORDS; j++) {
            tiku_mem_sram[j] = j * 2654435761u;
        }
    }
    tiku_mem_accesses = 0u;
    tiku_mem_checksum = 0u;
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    do {
        __asm__ volatile (".p2align 4" ::: "memory");
        switch (kind) {
        case TIKU_MEM_KIND_NOP:
        default: {
            /* The register-only reference, matched to the same accounting so
             * "an access" and "a register op" are directly comparable. */
            uint32_t n = TIKU_MEM_PASS_ACC;
            __asm__ volatile ("1: subs %0, %0, #1\n\t"
                              "   bne  1b\n"
                              : "+r" (n) : : "cc");
            break;
        }
        case TIKU_MEM_KIND_SRAM_R:
            TIKU_MEM_PASS_READ(tiku_mem_sram, cold_mask, 1u);
            break;
        case TIKU_MEM_KIND_SRAM_W:
            /* idx starts at 0 and the pattern's [0] is 0 by construction, so
             * bump acc first: the seed check above must stay true across runs. */
            acc |= 1u;
            TIKU_MEM_PASS_WRITE(tiku_mem_sram, cold_mask, 1u);
            break;
        case TIKU_MEM_KIND_SRAM_STRIDE:
            TIKU_MEM_PASS_READ(tiku_mem_sram, cold_mask, TIKU_MEM_STRIDE);
            break;
        case TIKU_MEM_KIND_RRAM_HOT:
            TIKU_MEM_PASS_READ(tiku_mem_rram_hot, hot_mask, 1u);
            break;
        case TIKU_MEM_KIND_RRAM_COLD:
            TIKU_MEM_PASS_READ(tiku_mem_rram_cold, cold_mask, TIKU_MEM_STRIDE);
            break;
        }
        tiku_mem_accesses += TIKU_MEM_PASS_ACC;
        tiku_mem_checksum += acc;
        /* Deliberate blocking: tell the hang detector so it does not name this
         * probe a wedge at 1024 stalled ticks (see power_probe's note). */
        tiku_hang_checkin();
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    } while ((uint32_t)(now - t0) < ms * 1000u);
    tiku_mem_sink = acc;          /* consume, so no read can be elided */
    return (uint32_t)(now - t0);
}

/*---------------------------------------------------------------------------*/
/* SLEEP FLOOR PROBE                                                         */
/*---------------------------------------------------------------------------*/

static uint32_t tiku_sleep_wakes;

uint32_t tiku_nordic_sleep_wake_count(void) { return tiku_sleep_wakes; }

/* Inner iterations per outer pass of the busy loop.  Fixed and exported so the
 * host can turn a pass count into retired instructions without guessing. */
#define TIKU_SPIN_INNER 4096u

static uint32_t tiku_spin_passes;

uint32_t tiku_nordic_spin_pass_count(void) { return tiku_spin_passes; }

#if (TIKU_FLPR_ENABLE + 0)
/* Coprocessor work retired inside the last probe window.  Kept here rather than
 * left to the host: see the sampling note in power_probe(). */
static uint32_t tiku_flpr_passes_at_entry;
static uint32_t tiku_flpr_passes_in_window;

uint32_t tiku_nordic_flpr_pass_delta(void) { return tiku_flpr_passes_in_window; }
#endif
uint32_t tiku_nordic_spin_inner(void)      { return TIKU_SPIN_INNER; }

/* CoreDebug DHCSR; C_DEBUGEN (bit 0) is set by the debugger over SWD and can
 * only be cleared by it -- or by the pin reset the datasheet prescribes. */
#define TIKU_DHCSR (*(volatile uint32_t *)0xE000EDF0UL)

int tiku_nordic_debug_attached(void)
{
    return (TIKU_DHCSR & 1UL) != 0UL;
}

/**
 * @brief The one probe body, shared by the idle and busy measurements.
 *
 * ONE FUNCTION AND NOT TWO: an idle figure and a busy figure are comparable
 * only if the ONLY difference is what the CPU is doing, so @p spin selects the
 * loop body and nothing else.
 *
 * @note Two paths releasing peripherals from separate copies of this list would
 *       eventually drift, and the drift would surface as a physical result
 *       about the core.
 */
static uint32_t power_probe(uint32_t ms, unsigned flags, int spin)
{
    uint32_t t0, now;
    NRF_UARTE_Type *u = TIKU_BOARD_CONSOLE_UARTE;

    if ((flags & TIKU_SLEEP_STOP_UART) != 0u) {
        /* Let the caller's announcement finish leaving the wire before the
         * transmitter is torn down, or the line that says what is about to
         * happen is the line that gets truncated.  A few ms at 115200 clears
         * anything the shell has queued. */
        tiku_cpu_nordic_delay_ms(20u);
        /* ENABLE=0 alone: on this UARTE the stop tasks live under TASKS_DMA
         * rather than the legacy TASKS_STOPRX/STOPTX, and disabling the
         * peripheral outright is what the probe wants anyway. */
        u->ENABLE = 0u;
    }
    if ((flags & TIKU_SLEEP_STOP_PLL) != 0u) {
        /* The erratum-39 workaround pins this on for the life of the boot.
         * Releasing it is the whole question: does HFCLK then stop? */
        NRF_CLOCK_S->TASKS_PLLSTOP = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_HFXO) != 0u) {
        NRF_CLOCK_S->TASKS_XOSTOP = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_TIM) != 0u) {
        /* The htimer's TIMER20 free-runs from sched init in EVERY build --
         * a permanent PCLK16M request, i.e. a peripheral that is never idle
         * in a system whose sleep depends on all of them being idle. */
        NRF_TIMER20_S->TASKS_STOP = 1u;
    }
    if ((flags & TIKU_SLEEP_DEEP) != 0u) {
        /* Sub-power mode: force Low-power in case anything earlier in the
         * boot latched Constant Latency (the reset default is Low-power, but
         * radio bursts and the Axon shim both touch CONSTLAT). */
        NRF_POWER_S->TASKS_LOWPWR = 1u;
        /* SLEEPDEEP is the difference between "the CPU pipeline is stalled"
         * and "the CPU has released its clock".  Shallow WFI keeps the core's
         * own HCLK request standing, so the HFCLK controller can never stop
         * the clock no matter what else is released -- which is why stopping
         * the PLL, the UARTE and the HFXO under shallow WFI measured a mere
         * 85 uA of the 955: the biggest requestor was the sleeper itself. */
        TIKU_SCB->SCR |= (1UL << 2);
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");

    /* COUNT THE WAKES.  A WFI that returns immediately is not sleeping, and
     * from the outside that is indistinguishable from one that is -- the
     * current is simply higher than it should be and nobody knows why.  The
     * wake count separates "the part will not sleep" from "the part sleeps and
     * something else is drawing the current". */
    tiku_sleep_wakes = 0u;
    tiku_spin_passes = 0u;
#if (TIKU_FLPR_ENABLE + 0)
    /* Sample the COPROCESSOR's work counter inside this window too.  Doing it
     * from the host instead costs two shell round-trips at the window edges,
     * which on the short windows the probe is limited to (see the ~1024-tick
     * cliff note in experiments/power/experiment3) is a ~20% error on the
     * rate -- and the rate is the denominator of every energy-per-work
     * figure.  Sampling here is exact and free. */
    tiku_flpr_passes_at_entry = tiku_flpr_arch_spin_passes();
#endif
    if ((flags & TIKU_SLEEP_STOP_TICK) != 0u) {
        /* Stretch the kernel tick across the whole window, through the same
         * tickless path the scheduler's deep idle uses.  Without this the CC
         * fires 128 times a second, and each firing is not just a wake: it
         * keeps the GRTC's SYSCOUNTER cycling through its active state and
         * runs the accounting ISR.  The stretch is the difference between
         * measuring "WFI as this kernel idles today" and "the floor this
         * silicon can reach with the kernel's own timekeeping intact".
         * Masked because begin() moves the CC under the live tick ISR. */
        __asm__ volatile ("cpsid i" ::: "memory");
        (void)tiku_clock_tickless_begin(
            (tiku_clock_time_t)((ms * TIKU_CLOCK_SECOND) / 1000u + 2u));
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if ((flags & TIKU_SLEEP_STOP_SYSC) != 0u) {
        /* SYSCOUNTEREN held 1 keeps the GRTC's 1 MHz counter -- an HF-domain
         * consumer -- active through every sleep, wake or no wake.  AUTOEN
         * (kept) re-requests it whenever a CPU is awake, so clearing the
         * permanent enable only changes what happens DURING sleep; every
         * SYSCOUNTER read this probe does happens awake, where AUTOEN has it
         * running.  The wake compare falls to the 32 kHz domain, which is why
         * this release is only offered once the LFCLK is running. */
        NRF_GRTC_S->MODE &= ~(1UL << 1);
    }
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    do {
        if (spin) {
            /* THE while(1) REFERENCE.  Two instructions, no loads, no stores,
             * resident in cache or prefetch: as close to "the core is simply
             * running" as this part can be asked for.  Written in asm so no
             * optimiser decision stands between the source and what retires --
             * an empty C while(1) becomes one backward branch and a counted C
             * loop may or may not survive -O2 intact.  The GRTC is read once
             * per 4096 iterations, keeping the peripheral bus under ~0.1% of
             * the window so what is measured is the core, not the bus. */
            /* PIN THE ALIGNMENT.  This loop is the reference workload for every
             * core-power number, so its cost must not depend on where the
             * linker happened to drop it.  Unaligned, the same two instructions
             * measured 956 uA higher and -- with the cache off -- 18 cycles per
             * iteration instead of 8, purely because an unrelated build option
             * shifted the address.  A measurement primitive that moves with
             * link order is not a reference. */
            uint32_t n = TIKU_SPIN_INNER;
            __asm__ volatile (".p2align 4\n\t"
                              "1: subs %0, %0, #1\n\t"
                              "   bne  1b\n"
                              : "+r" (n) : : "cc");
            /* COUNT THE WORK, not just the time.  Current for a fixed DURATION
             * cannot distinguish "this configuration draws less" from "this
             * configuration executed less" -- and the two have opposite
             * meanings.  It cost a real confusion: with the cache off a spin
             * loop drew LESS current, which reads as a saving until you ask how
             * many iterations each configuration actually retired. */
            tiku_spin_passes++;
        } else {
            __asm__ volatile ("wfi" ::: "memory");
            tiku_sleep_wakes++;
        }
        /* Read twice: coming out of deep sleep the SYSCOUNTER may need a
         * cycle to reactivate, and the first read can be stale. */
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        /* DECLARE THE BLOCKING DELIBERATE.  This loop holds the shell process
         * for the whole window, which is exactly the shape the check-in hang
         * detector exists to catch: at 1024 stalled ticks (8.000 s) it names
         * this process the culprit and warm-resets the board.  It did -- the
         * "1024-tick cliff" that limited every experiment window to <= 7.5 s
         * was the detector doing its job against an instrument that never
         * said it was alive.  One check-in per pass is the honest fix; the
         * detector stays armed for real wedges. */
        tiku_hang_checkin();
    } while ((uint32_t)(now - t0) < ms * 1000u);
#if (TIKU_FLPR_ENABLE + 0)
    tiku_flpr_passes_in_window = tiku_flpr_arch_spin_passes()
                                 - tiku_flpr_passes_at_entry;
#endif
    if ((flags & TIKU_SLEEP_STOP_SYSC) != 0u) {
        NRF_GRTC_S->MODE |= (1UL << 1);   /* SYSCOUNTEREN back to permanent */
    }
    if ((flags & TIKU_SLEEP_STOP_TICK) != 0u) {
        /* Close the stretch: credit every tick the window covered and restore
         * the per-tick cadence.  Masked for the same reason begin() is. */
        __asm__ volatile ("cpsid i" ::: "memory");
        tiku_clock_tickless_end();
        __asm__ volatile ("cpsie i" ::: "memory");
    }

    if ((flags & TIKU_SLEEP_DEEP) != 0u) {
        /* Never left set: SLEEPDEEP changes what every later WFI in the
         * system means, including the scheduler's idle hook, and that is a
         * decision for the idle policy, not a side effect of one probe. */
        TIKU_SCB->SCR &= ~(1UL << 2);
    }

    /* Restore in the reverse order, and give the clocks time to come back
     * before anything tries to use them. */
    if ((flags & TIKU_SLEEP_STOP_TIM) != 0u) {
        NRF_TIMER20_S->TASKS_START = 1u;   /* free-running again; the origin
                                            * shift is harmless at idle */
    }
    if ((flags & TIKU_SLEEP_STOP_HFXO) != 0u) {
        NRF_CLOCK_S->EVENTS_XOSTARTED = 0u;
        NRF_CLOCK_S->TASKS_XOSTART = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_PLL) != 0u) {
        /* BOUNDED wait, not while(!started).  An earlier version spun
         * unconditionally on EVENTS_PLLSTARTED with a comment claiming the
         * PLL's lock time bounds it -- but a wait is only as bounded as the
         * event is guaranteed, and a probe that hangs in its own RESTORE path
         * presents exactly like the measurement having killed the board.
         * After a 20 s window the console died with 'starting' printed and
         * 'done' never delivered; an unbounded spin here is one of the few
         * places that can produce that signature. */
        uint32_t guard = 0u;
        NRF_CLOCK_S->EVENTS_PLLSTARTED = 0u;
        NRF_CLOCK_S->TASKS_PLLSTART = 1u;
        while (NRF_CLOCK_S->EVENTS_PLLSTARTED == 0u && guard < 20000000u) {
            guard++;
        }
    }
    if ((flags & TIKU_SLEEP_STOP_UART) != 0u) {
        /* Full re-init rather than restoring ENABLE: the RX path is
         * DMA-driven and needs its buffer and short re-armed, which only
         * tiku_uart_init() knows how to do. */
        tiku_uart_init();
    }
    return (uint32_t)(now - t0);
}

uint32_t tiku_nordic_sleep_probe(uint32_t ms, unsigned flags)
{
    return power_probe(ms, flags, 0);
}

uint32_t tiku_nordic_spin_probe(uint32_t ms, unsigned flags)
{
    return power_probe(ms, flags, 1);
}

void tiku_nordic_system_off(void)
{
    unsigned i;

    /* DISARM EVERY WAKE SOURCE FIRST.  The kernel tick's GRTC compare is
     * armed ~8 ms out at any moment, and a System OFF wake is a RESET, so an
     * armed compare turns this into an instant reboot and the "measurement"
     * is just the shell idling again.
     *
     * MEASURED CAVEAT (LM20-DK, 2026-07-26): disarming did NOT make System
     * OFF hold on this rig.  The board still came back immediately, and with
     * RESETREAS reading 0 -- a power-on-reset signature, not the OFF bit a
     * real System OFF wake sets.  So entry collapses into a POR-class reset
     * here, cause unresolved (candidates: a VDDM transient at the OFF
     * load-step through the series instrument, or interference from the
     * on-board debugger).  The disarm stays because it is necessary for the
     * day entry works; it just is not sufficient on this bench. */
    /* GRTC_CC_MaxCount, not a hard-coded 16: this array holds 12 entries on
     * every nRF54L part, and looping to 16 wrote four registers PAST the end of
     * it -- straight into whatever the GRTC map has next.  The compiler said so
     * (-Waggressive-loop-optimizations, "iteration 12 invokes undefined
     * behavior"); taking the bound from the MDK keeps it right if a future part
     * changes the count. */
    for (i = 0u; i < (unsigned)GRTC_CC_MaxCount; i++) {
        NRF_GRTC_S->CC[i].CCEN = 0u;
    }
    NRF_GRTC_S->INTENCLR0 = 0xFFFFFFFFu;
    __asm__ volatile ("dsb 0xF" ::: "memory");

    NRF_REGULATORS_S->SYSTEMOFF = 1u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/*---------------------------------------------------------------------------*/
/* CLOCK ORACLE                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Measure the core clock against a clock that cannot move with it.
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
