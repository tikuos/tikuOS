/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.c - nRF54L cache, DC/DC and power probes.
 *
 * Cache and DC/DC enables, cache and memory-access workloads, sleep and spin
 * probes that release clock requests one at a time, System OFF, and a
 * core-clock measurement against the GRTC.
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
#include <arch/nordic/tiku_timer_arch.h> /* tick rate, before clock.h         */
#include <kernel/cpu/tiku_hang.h>        /* check-in during a blocking probe */
#include <kernel/memory/tiku_mem.h>      /* the mem probe's SRAM set, lent    */
#include <kernel/timers/tiku_clock.h>   /* tickless stretch for the tick flag */

/*---------------------------------------------------------------------------*/
/* CACHE                                                                     */
/*---------------------------------------------------------------------------*/

/*
 * ICACHE is at 0xE0082000 in the Arm private-peripheral region.  The vendored
 * MDK headers have no instance define for it, only NRF_CACHE_Type and the
 * CACHE_* field macros, so the base comes from the datasheet's instance table
 * (4.2.3.4: "ICACHE, APPLICATION, 0xE0082000").
 *
 * Configuration, from the same table: 8 KB, 128 sets, two-way set associative,
 * 64-bit data unit, 4 data units per line (32 bytes), LRU replacement.
 * Instruction and data accesses to NVM are both cached.  The only maintenance
 * operation is invalidate: there is no flush or clean.
 */
#define TIKU_NORDIC_CACHE ((NRF_CACHE_Type *)0xE0082000UL)

void tiku_nordic_cache_set(int on)
{
    if (on) {
        /* Invalidate before enabling: the cache keeps its tags across a
         * disable, so NVM written while it was off (file store, persist
         * cells, BASIC saves) would be served stale.  A write made while
         * the cache is on is write-around and invalidates its own line. */
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
 * INDUCTORDET is valid only while the converter is off.  Datasheet 5.7.2.4.2:
 * "The detection can only take place if the DC/DC converter is not enabled
 * (VREGMAIN.DCDCEN = 0)."  Read while it is on, the bit is stale, so this
 * returns -1 then.
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

    /* Detection needs the converter off: turn it off, read, and restore the
     * previous state.  LDO operation is the reset state. */
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
     * DCDCEN is written without an inductor check.  Datasheet 5.7.2.4: "When
     * enabling the DC/DC regulator, the device checks if an inductor is
     * connected to the DCC pin.  If an inductor is not detected, the device
     * remains in LDO mode."
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
 * 16 KB of RRAM, twice the 8 KB cache, so a full pass cannot be resident and
 * the traversal keeps missing.
 */
#define TIKU_CACHE_WL_WORDS   4096u
#define TIKU_CACHE_WL_STRIDE  17u     /* coprime with the line (see below) */
#define TIKU_CACHE_WL_PASSES  64u

static const uint32_t tiku_cache_wl_data[TIKU_CACHE_WL_WORDS] = { 0 };

uint32_t tiku_nordic_cache_workload(uint32_t *out_us)
{
    uint32_t sum = 0u, i, pass, idx = 0u;
    uint32_t t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;

    for (pass = 0u; pass < TIKU_CACHE_WL_PASSES; pass++) {
        for (i = 0u; i < TIKU_CACHE_WL_WORDS; i++) {
            /* The 17-word stride is longer than the 8-word (32-byte) line
             * and coprime with it, so consecutive reads land in different
             * lines and sequential prefetch cannot help. */
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
 * Six loops, one memory access per counted step: NOP (register only), SRAM
 * read, write and strided read, and RRAM reads over a HOT 4 KB set that stays
 * resident in the 8 KB cache and a COLD 64 KB set that cannot.  The 17-word
 * stride is coprime with the line, so consecutive accesses land in different
 * lines and sequential prefetch cannot help.
 *
 * The loops are 8x unrolled, aligned to 16 bytes (a loop's current depends on
 * its alignment), and the accumulator ends in a volatile sink so no read can
 * be optimised away.
 */
#define TIKU_MEM_HOT_WORDS   1024u    /* 4 KB  -- inside the 8 KB cache      */
#define TIKU_MEM_COLD_WORDS 16384u    /* 64 KB -- 8x the cache               */
#define TIKU_MEM_STRIDE        17u    /* coprime with the line               */
#define TIKU_MEM_PASS_ACC     256u    /* accesses per accounted pass         */

static const uint32_t tiku_mem_rram_hot[TIKU_MEM_HOT_WORDS]   = { 0 };
static const uint32_t tiku_mem_rram_cold[TIKU_MEM_COLD_WORDS] = { 0 };
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
 * The access count and checksum are read back separately.
 */
uint32_t tiku_nordic_mem_probe(unsigned kind, uint32_t ms)
{
    uint32_t t0, now;
    uint32_t acc = 0u, idx = 0u;
    const uint32_t hot_mask  = TIKU_MEM_HOT_WORDS - 1u;
    const uint32_t cold_mask = TIKU_MEM_COLD_WORDS - 1u;
    tiku_arena_t sram_arena;
    uint32_t *tiku_mem_sram;

    /* The 64 KB SRAM set is borrowed from the SRAM tier for the run: as a
     * static it was the largest object in the image, a quarter of the
     * nRF54L15's SRAM held for a bench probe.  The tier is free at the
     * prompt; a probe that finds it taken reports 0 us. */
    if (tiku_tier_arena_create(&sram_arena, TIKU_MEM_SRAM,
                               TIKU_MEM_COLD_WORDS * 4u, 0u) != TIKU_MEM_OK) {
        tiku_mem_accesses = 0u;
        tiku_mem_checksum = 0u;
        return 0u;
    }
    tiku_mem_sram = (uint32_t *)sram_arena.buf;
    /* Seed the SRAM buffer before timing so checksums do not depend on
     * a previous write probe. The RRAM arrays are zero-filled constants. */
    {
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
            /* Register-only loop of TIKU_MEM_PASS_ACC iterations, counted
             * like one access pass. */
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
            /* Store a nonzero value across the timed write passes. */
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
        /* The probe blocks its caller; a check-in per pass keeps the hang
         * detector from treating it as a wedge. */
        tiku_hang_checkin();
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    } while ((uint32_t)(now - t0) < ms * 1000u);
    tiku_mem_sink = acc;          /* consume, so no read can be elided */
    (void)tiku_arena_destroy(&sram_arena);
    return (uint32_t)(now - t0);
}

/*---------------------------------------------------------------------------*/
/* SLEEP FLOOR PROBE                                                         */
/*---------------------------------------------------------------------------*/

static uint32_t tiku_sleep_wakes;

uint32_t tiku_nordic_sleep_wake_count(void) { return tiku_sleep_wakes; }

/* Inner iterations per outer pass of the spin loop, reported by
 * tiku_nordic_spin_inner(). */
#define TIKU_SPIN_INNER 4096u

static uint32_t tiku_spin_passes;

uint32_t tiku_nordic_spin_pass_count(void) { return tiku_spin_passes; }

#if (TIKU_FLPR_ENABLE + 0)
/* FLPR spin passes at probe entry, and retired inside the last window. */
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
 * @brief Release what @p flags names, run WFI (or the spin loop when @p spin
 *        is non-zero) for @p ms, then restore.
 *
 * @p spin selects the loop body only; the releases and restores are the same
 * for both probes.
 */
static uint32_t power_probe(uint32_t ms, unsigned flags, int spin)
{
    uint32_t t0, now;
    NRF_UARTE_Type *u = TIKU_BOARD_CONSOLE_UARTE;

    if ((flags & TIKU_SLEEP_STOP_UART) != 0u) {
        /* Give queued console output 20 ms to leave the wire before the
         * transmitter is disabled. */
        tiku_cpu_nordic_delay_ms(20u);
        /* ENABLE=0 alone disables the UARTE outright; this UARTE's stop
         * tasks are under TASKS_DMA. */
        u->ENABLE = 0u;
    }
    if ((flags & TIKU_SLEEP_STOP_PLL) != 0u) {
        /* The erratum-39 workaround starts the PLL at boot and never stops
         * it. */
        NRF_CLOCK_S->TASKS_PLLSTOP = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_HFXO) != 0u) {
        NRF_CLOCK_S->TASKS_XOSTOP = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_TIM) != 0u) {
        /* The htimer's TIMER20 free-runs from scheduler init in every
         * build, a standing PCLK16M request. */
        NRF_TIMER20_S->TASKS_STOP = 1u;
    }
    if ((flags & TIKU_SLEEP_DEEP) != 0u) {
        /* Keep any Constant Latency hold owned by the radio or NPU. */
        /* With SLEEPDEEP clear, WFI keeps the core's own HCLK request
         * standing, and HFCLK cannot stop whatever else is released. */
        TIKU_SCB->SCR |= (1UL << 2);
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");

    /* Count WFI returns: a high count means the part is being woken rather
     * than sleeping. */
    tiku_sleep_wakes = 0u;
    tiku_spin_passes = 0u;
#if (TIKU_FLPR_ENABLE + 0)
    /* Sample the FLPR's spin-pass counter at both window edges. */
    tiku_flpr_passes_at_entry = tiku_flpr_arch_spin_passes();
#endif
    if ((flags & TIKU_SLEEP_STOP_TICK) != 0u) {
        /* Stretch the kernel tick across the window through the tickless
         * path the scheduler's deep idle uses.  Unstretched, the tick compare
         * fires TIKU_CLOCK_SECOND times a second, waking the core and the
         * GRTC SYSCOUNTER and running the tick ISR.  Masked because begin()
         * moves the CC under the live tick ISR. */
        __asm__ volatile ("cpsid i" ::: "memory");
        (void)tiku_clock_tickless_begin(
            (tiku_clock_time_t)((ms * TIKU_CLOCK_SECOND) / 1000u + 2u));
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if ((flags & TIKU_SLEEP_STOP_SYSC) != 0u) {
        /* SYSCOUNTEREN = 1 keeps the GRTC's 1 MHz counter, an HF-domain
         * consumer, running through every sleep.  AUTOEN stays set and runs
         * it whenever a CPU is awake, so the SYSCOUNTER reads here still
         * work.  The wake compare then runs from the 32 kHz domain, so the
         * LFCLK must be running. */
        NRF_GRTC_S->MODE &= ~(1UL << 1);
    }
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    do {
        if (spin) {
            /* Two instructions, no loads or stores, in inline asm so the
             * compiler cannot change what retires.  Aligned to 16 bytes: the
             * loop's cost depends on its alignment.  The GRTC is read once
             * per TIKU_SPIN_INNER iterations. */
            uint32_t n = TIKU_SPIN_INNER;
            __asm__ volatile (".p2align 4\n\t"
                              "1: subs %0, %0, #1\n\t"
                              "   bne  1b\n"
                              : "+r" (n) : : "cc");
            /* Count passes: a lower current over the same window may mean
             * less work retired. */
            tiku_spin_passes++;
        } else {
            __asm__ volatile ("wfi" ::: "memory");
            tiku_sleep_wakes++;
        }
        /* Read twice: coming out of deep sleep the SYSCOUNTER may need a
         * cycle to reactivate, and the first read can be stale. */
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        now = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        /* This loop holds the calling process for the whole window; a
         * check-in per pass keeps the hang detector from warm-resetting the
         * board at TIKU_HANG_THRESHOLD_TICKS stalled ticks. */
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
        /* Cleared again: SLEEPDEEP changes every later WFI, including the
         * scheduler's idle hook. */
        TIKU_SCB->SCR &= ~(1UL << 2);
    }

    /* Restore in reverse order.  The PLL restart is waited for (bounded);
     * the HFXO restart is not. */
    if ((flags & TIKU_SLEEP_STOP_TIM) != 0u) {
        NRF_TIMER20_S->TASKS_START = 1u;   /* free-running again; the origin
                                            * shift is harmless at idle */
    }
    if ((flags & TIKU_SLEEP_STOP_HFXO) != 0u) {
        NRF_CLOCK_S->EVENTS_XOSTARTED = 0u;
        NRF_CLOCK_S->TASKS_XOSTART = 1u;
    }
    if ((flags & TIKU_SLEEP_STOP_PLL) != 0u) {
        /* Bounded wait: if PLLSTARTED never arrives, the probe still
         * returns. */
        uint32_t guard = 0u;
        NRF_CLOCK_S->EVENTS_PLLSTARTED = 0u;
        NRF_CLOCK_S->TASKS_PLLSTART = 1u;
        while (NRF_CLOCK_S->EVENTS_PLLSTARTED == 0u && guard < 20000000u) {
            guard++;
        }
    }
    if ((flags & TIKU_SLEEP_STOP_UART) != 0u) {
        /* Full re-init: the DMA-driven RX path needs its buffer and short
         * re-armed, which tiku_uart_init() does. */
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

    /* Disarm every GRTC compare first: the kernel tick's compare is always
     * armed about one tick out, and a System OFF wake is a reset.  On the
     * LM20-DK, entry is followed at once by a reset that reads as power-on
     * (RESETREAS 0); the cause is not known.  The loop bound is the MDK's
     * GRTC_CC_MaxCount, the size of the CC array. */
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
/* CORE CLOCK MEASUREMENT                                                    */
/*---------------------------------------------------------------------------*/

/*
 * SysTick counts processor clocks; the GRTC SYSCOUNTER counts 1 MHz from a
 * separate source.  Counting one against the other gives the core rate in Hz
 * without reading PLL.CURRENTFREQ.
 *
 * SysTick is a 24-bit down counter: it wraps after 16.77 M cycles, 131 ms at
 * 128 MHz.  The 50 ms window is 6.4 M cycles at 128 MHz, inside one span, and
 * the code has no wrap handling: a window longer than one span reads short.
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

    /* SysTick counts down, so elapsed cycles is s0 - s1 modulo the span. */
    cycles     = (s0 - s1) & 0x00FFFFFFUL;
    elapsed_us = (uint32_t)(t1 - t0);
    if (elapsed_us == 0U) {
        return 0UL;
    }
    /* 64-bit: cycles * 1e6 overflows 32 bits above ~4295 cycles. */
    return (unsigned long)(((uint64_t)cycles * 1000000ULL) / elapsed_us);
}

/*---------------------------------------------------------------------------*/
/* BOOT                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_nordic_power_boot_init(void)
{
    /* Cache first, so it serves the rest of boot. */
#if !defined(TIKU_NORDIC_CACHE_DISABLE) || !TIKU_NORDIC_CACHE_DISABLE
    tiku_nordic_cache_set(1);
#endif

    /* Then the supply; without an inductor the part stays on its LDO. */
#if !defined(TIKU_NORDIC_DCDC_DISABLE) || !TIKU_NORDIC_DCDC_DISABLE
    (void)tiku_nordic_dcdc_set(1);
#endif
}
