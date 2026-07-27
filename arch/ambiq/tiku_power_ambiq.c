/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_ambiq.c - Apollo510 power-measurement instruments.
 *
 * WHY THIS FILE EXISTS.  Five power experiments characterised one part
 * (nRF54LM20B, RRAM) in detail.  Every cross-platform claim TikuOS makes --
 * above all the "durability options invert across FRAM/MRAM/RRAM/Flash" thesis
 * -- rests on a single NVM technology's measured behaviour.  This is the second
 * point: Cortex-M55, MRAM, a different power architecture.
 *
 * WHAT IS DELIBERATELY *NOT* COPIED FROM THE NORDIC PORT.  The `quiet` release
 * set, whose meaning is frozen by published experiments over there and must not
 * acquire a second definition here; and any peripheral-release flag whose effect
 * has not yet been measured on this part.  A release vocabulary transcribed
 * across silicon is a list of assumptions wearing the clothes of a measurement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

/* Apollo510 only -- see the Makefile note where TIKU_AMBIQ_POWER_PROBE is set. */
#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)

#include "tiku_power_ambiq.h"
#include "apollo510.h"          /* CMSIS register map: STIMER, PWRCTRL, ... */
#include <kernel/cpu/tiku_hang.h>   /* check-in: these probes block on purpose */
#include <arch/ambiq/tiku_timer_arch.h> /* TIKU_CLOCK_ARCH_SECOND first        */
#include <kernel/timers/tiku_clock.h>   /* tickless stretch for the tick flag  */
#include <arch/ambiq/tiku_uart_arch.h>  /* console re-init after the uart flag */
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h> /* SIMOBUCK enable (autorun)   */

/*---------------------------------------------------------------------------*/
/* TIMEBASE -- the always-on STIMER                                          */
/*---------------------------------------------------------------------------*/

/*
 * The core's SysTick is gated during WFI on this part, so it cannot time a
 * sleep window -- that is exactly why the kernel tick moved to the STIMER.  The
 * three-read vote transcribes am_hal_stimer_counter_get: the counter is in a
 * different clock domain, so a single read can catch it mid-update.
 */
uint32_t tiku_ambiq_stimer_now(void)
{
    uint32_t v0 = STIMER->STTMR;
    uint32_t v1 = STIMER->STTMR;
    uint32_t v2 = STIMER->STTMR;
    return (v0 == v1) ? v0 : v2;
}

/* RUNTIME-ADJUSTABLE, not a constant, since the deep-sleep autorun reclocks the
 * STIMER to LFRC: measured on hardware (halted core, PC in the counter-vote,
 * three identical reads), REAL deep sleep stops the 32 kHz crystal and the
 * STIMER freezes with it -- the datasheet's deep-sleep rows say "LFRC on, XTAL
 * off" for exactly this reason.  LFRC_NOMINAL is ~900 Hz and UNCALIBRATED
 * (tens of percent), so windows timed on it are approximate; for the autorun
 * that is fine -- segments identify by order and rough duration, and the LEVEL
 * is the measurement. */
static uint32_t tiku_ambiq_stimer_hz = 32768u;
#define TIKU_AMBIQ_STIMER_HZ tiku_ambiq_stimer_hz

uint32_t tiku_ambiq_stimer_us(uint32_t counts)
{
    /* At the crystal rate this is exact (1e6/32768 == 15625/512); at the LFRC
     * rate it is nominal-only, like everything timed on an uncalibrated RC. */
    if (tiku_ambiq_stimer_hz == 32768u) {
        return (uint32_t)(((uint64_t)counts * 15625u) >> 9);
    }
    return (uint32_t)(((uint64_t)counts * 1000000u) / tiku_ambiq_stimer_hz);
}

/*---------------------------------------------------------------------------*/
/* CACHE -- the Cortex-M55's architectural L1                                */
/*---------------------------------------------------------------------------*/

/*
 * There is no vendor CACHECTRL block on this part (checked: apollo510.h has
 * none), so the cache knob is the ARCHITECTURAL one -- SCB.CCR.IC/DC plus the
 * required maintenance.  That is a real difference from the nRF54L, whose
 * ICACHE is a vendor peripheral with its own hit/miss counters; there are no
 * equivalent counters here, so "the cache is on" has to be established from
 * CCR and from the workload's own throughput rather than from a hit rate.
 */
void tiku_ambiq_cache_set(int on)
{
    if (on) {
        SCB_EnableICache();
        SCB_EnableDCache();
    } else {
        /* Clean before disabling the D-cache: dirty lines must reach memory or
         * a later read returns stale data.  CMSIS's disable does the clean, but
         * the ordering (D first, then I) is ours to get right. */
        SCB_DisableDCache();
        SCB_DisableICache();
    }
    __DSB();
    __ISB();
}

int tiku_ambiq_cache_enabled(void)
{
    return (SCB->CCR & SCB_CCR_IC_Msk) != 0u;
}

void tiku_ambiq_cache_geometry(uint32_t *i_bytes, uint32_t *d_bytes,
                               uint32_t *line)
{
    /* READ the geometry, do not assume it.  Cortex-M55 cache sizes are an
     * implementer choice, so a size copied from another part's datasheet is how
     * a "cache-resident" working set silently stops being resident.  CCSIDR
     * gives sets/ways/line for whichever cache CSSELR selects. */
    uint32_t sel, ccsidr, sets, ways, lw, sz;
    uint32_t saved = SCB->CSSELR;

    if (i_bytes != (uint32_t *)0) { *i_bytes = 0u; }
    if (d_bytes != (uint32_t *)0) { *d_bytes = 0u; }
    if (line != (uint32_t *)0)    { *line = 0u; }

    for (sel = 0u; sel < 2u; sel++) {
        /* CSSELR: level 0, InD = 1 selects instruction, 0 selects data. */
        SCB->CSSELR = sel;   /* 0 = D, 1 = I */
        __DSB();
        ccsidr = SCB->CCSIDR;
        lw   = (ccsidr & 7u) + 4u;                    /* log2(line bytes)     */
        ways = ((ccsidr >> 3) & 0x3FFu) + 1u;
        sets = ((ccsidr >> 13) & 0x7FFFu) + 1u;
        sz   = sets * ways * (1u << lw);
        if (sel == 0u) {
            if (d_bytes != (uint32_t *)0) { *d_bytes = sz; }
        } else {
            if (i_bytes != (uint32_t *)0) { *i_bytes = sz; }
        }
        if (line != (uint32_t *)0) { *line = (1u << lw); }
    }
    SCB->CSSELR = saved;
    __DSB();
}

/*---------------------------------------------------------------------------*/
/* CLOCK ORACLE                                                              */
/*---------------------------------------------------------------------------*/

/* DWT, not SysTick.  SysTick counts DOWN and RELOADS: with a short RVR it wraps
 * many times inside any window the 32.768 kHz STIMER can resolve, and two
 * samples cannot tell one wrap from ten.  The first cut of this oracle did
 * exactly that and reported 14 kHz for a 96 MHz core -- wrong by 6800x, and
 * wrong in the direction that looks like a real (low) number rather than an
 * obvious failure.  DWT's CYCCNT is a free-running 32-bit core-cycle counter
 * with no reload: at 96-250 MHz it wraps every 17-45 s, so a 16 ms window is
 * unambiguous.  (mrambench already relies on DWT here, so the block is known
 * good on this part.) */
#define TIKU_SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)
#define TIKU_DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define TIKU_DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define TIKU_DWT_CYCCNTENA (1UL << 0)
#define TIKU_SCB_TRCENA    (1UL << 24)

unsigned long tiku_ambiq_cpu_hz_measure(void)
{
    uint32_t t0, dt, c0, c1;
    uint32_t target = TIKU_AMBIQ_STIMER_HZ / 64u;    /* ~16 ms, 512 counts */

    /* Enable the cycle counter if nothing else has.  Left enabled afterwards:
     * it is a free-running counter with no side effects, and other benches on
     * this part expect it available. */
    TIKU_SCB_DEMCR |= TIKU_SCB_TRCENA;
    TIKU_DWT_CTRL  |= TIKU_DWT_CYCCNTENA;
    __DSB();
    c0 = TIKU_DWT_CYCCNT;
    t0 = tiku_ambiq_stimer_now();
    /* If CYCCNT is not actually incrementing, say so instead of dividing by it. */
    if (TIKU_DWT_CYCCNT == c0) {
        uint32_t guard = 0u;
        while (TIKU_DWT_CYCCNT == c0 && guard < 100000u) { guard++; }
        if (TIKU_DWT_CYCCNT == c0) {
            return 0ul;                  /* DWT unavailable on this part/config */
        }
        c0 = TIKU_DWT_CYCCNT;
        t0 = tiku_ambiq_stimer_now();
    }
    do {
        dt = tiku_ambiq_stimer_now() - t0;
    } while (dt < target);
    c1 = TIKU_DWT_CYCCNT;
    if (dt == 0u) {
        return 0ul;
    }
    /* CYCCNT counts UP and wraps only every 17-45 s, so the unsigned difference
     * is correct even across a single wrap.  Hz = cycles / (dt / 32768). */
    return (unsigned long)(((uint64_t)(uint32_t)(c1 - c0) * TIKU_AMBIQ_STIMER_HZ)
                           / dt);
}

/*---------------------------------------------------------------------------*/
/* PROBES                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_AMBIQ_SPIN_INNER 4096u

static uint32_t s_wakes;
static uint32_t s_passes;

uint32_t tiku_ambiq_sleep_wake_count(void) { return s_wakes; }
uint32_t tiku_ambiq_spin_pass_count(void)  { return s_passes; }
uint32_t tiku_ambiq_spin_inner(void)       { return TIKU_AMBIQ_SPIN_INNER; }

/*
 * THE REFERENCE LOOP LIVES IN ITS OWN 16-BYTE-ALIGNED SECTION.
 *
 * `.p2align 4` inside inline asm is NOT an alignment guarantee: the assembler
 * aligns within its section and the linker then places that section wherever it
 * likes.  Measured on this part, the identical two instructions ran at 1.431 and
 * 3.423 cycles/iteration depending only on where they landed -- a 2.4x spread,
 * and in the OPPOSITE direction from the nRF54L's misalignment penalty.  The
 * directive had put the loop at mod 16 = 8 while claiming otherwise.
 *
 * Fix: give the loop its own function, its own section, and a DECLARED
 * alignment, so the assembler's frame and the link-time address agree.  The
 * harness disassembles and logs the runtime address every build; a measurement
 * primitive whose cost depends on link order is not a reference.
 */
/* noclone as well as noinline: GCC's IPA otherwise emits a `.isra` clone, and a
 * clone is a DIFFERENT symbol that need not inherit the section or alignment --
 * the guarantee would then hold by luck, which is what this whole change exists
 * to stop. */
__attribute__((noinline, noclone, aligned(16),
               section(".text.tiku_ambiq_spinpass")))
static uint32_t tiku_ambiq_spin_pass(uint32_t n)
{
    __asm__ volatile (".p2align 4\n\t"
                      "1: subs %0, %0, #1\n\t"
                      "   bne  1b\n"
                      : "+r" (n) : : "cc");
    return n;
}

/* One body for both states, for the same reason as the Nordic port: an idle
 * figure and a busy figure are only comparable if the ONLY difference between
 * them is what the CPU is doing. */
static uint32_t ambiq_probe(uint32_t ms, unsigned flags, int spin)
{
    uint32_t t0, dt = 0u;
    uint32_t freeze = 0u;
    uint32_t target = (uint32_t)(((uint64_t)ms * TIKU_AMBIQ_STIMER_HZ) / 1000u);

    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_UART) != 0u) {
        /* Let the caller's announcement leave the wire, then power the UART
         * domain off.  This is the release that matters most for deep sleep:
         * an enabled UART is a standing HFRC request, and the datasheet's
         * uA-class rows all assume HFRC off. */
        tiku_cpu_ambiq_delay_us(4000u);
        PWRCTRL->DEVPWREN_b.PWRENUART1 = 0u;
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_TICK) != 0u) {
        /* Same pattern as the nRF54L probe: stretch through the port's own
         * tickless path, masked because begin() moves the compare under the
         * live tick ISR.  The 510's accounting credits the whole stretch on
         * wake, so uptime stays exact. */
        __asm__ volatile ("cpsid i" ::: "memory");
        (void)tiku_clock_tickless_begin(
            (tiku_clock_time_t)((ms * TIKU_CLOCK_ARCH_SECOND) / 1000u + 2u));
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DBGLOCK) != 0u) {
        MCUCTRL->DEBUGGER = 1u;          /* lockout; restored below */
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DEEP) != 0u) {
        SCB->SCR |= (1ul << 2);          /* SLEEPDEEP */
    }
    __DSB();
    s_wakes = 0u;
    s_passes = 0u;
    t0 = tiku_ambiq_stimer_now();
    do {
        if (spin) {
            /* PIN THE ALIGNMENT.  This is the reference workload for every core
             * figure, so its cost must not depend on where the linker dropped
             * it: on the nRF54L the identical two instructions measured 956 uA
             * apart purely because an unrelated build option moved them. */
            (void)tiku_ambiq_spin_pass(TIKU_AMBIQ_SPIN_INNER);
            s_passes++;
        } else {
            __asm__ volatile ("wfi" ::: "memory");
            s_wakes++;
        }
        /* Deliberate blocking: say so, or the check-in hang detector names this
         * probe the culprit at 1024 stalled ticks and resets the board. */
        tiku_hang_checkin();
        {
            uint32_t now2 = tiku_ambiq_stimer_now();
            if (now2 - t0 == dt) {
                /* Counter unchanged since last iteration: count it.  A frozen
                 * timebase turned v1/v2 of the deep-sleep autorun into an
                 * eternal busy-poll of a dead counter; a probe must never
                 * trust its clock unconditionally. */
                if (++freeze != 0u && freeze > 2000000u) {
                    break;
                }
            } else {
                freeze = 0u;
            }
            dt = now2 - t0;
        }
    } while (dt < target);

    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DEEP) != 0u) {
        SCB->SCR &= ~(1ul << 2);   /* never left set: it changes every later WFI */
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DBGLOCK) != 0u) {
        MCUCTRL->DEBUGGER = 0u;
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_TICK) != 0u) {
        __asm__ volatile ("cpsid i" ::: "memory");
        tiku_clock_tickless_end();
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_UART) != 0u) {
        PWRCTRL->DEVPWREN_b.PWRENUART1 = 1u;
        {
            uint32_t spin_ack = 200000u;
            while (spin_ack-- != 0u) { __asm__ volatile ("nop"); }
        }
        tiku_uart_init();          /* full re-init: DMA/config, not just power */
    }
    return tiku_ambiq_stimer_us(dt);
}

uint32_t tiku_ambiq_sleep_probe(uint32_t ms, unsigned flags)
{
    return ambiq_probe(ms, flags, 0);
}

uint32_t tiku_ambiq_spin_probe(uint32_t ms)
{
    return ambiq_probe(ms, 0u, 1);
}

/*---------------------------------------------------------------------------*/
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

/*
 * SIZED AGAINST THE MEASURED GEOMETRY -- and then against the LINKER.
 *
 * First run on hardware read CCSIDR: **I 64 KB, D 64 KB, line 32 B**, eight
 * times the nRF54L's 8 KB.  That is exactly why the geometry is read rather than
 * transcribed: a working set sized for an 8 KB cache is fully resident in a
 * 64 KB one, and the "cache-defeating" workload would have measured a blend.
 *
 * The obvious fix -- 512 KB, 8x the cache -- does NOT FIT: this device's code
 * window is 384 KB (apollo510.ld: MRAM LENGTH = 0x60000) with ~122 KB spare, and
 * TCM is 512 KB shared with .data/.bss/heap/stack.  512 KB overflowed MRAM by
 * 262 KB and TCM by 30 KB.  So COLD is 128 KB: only **2x** the D-cache.
 *
 * Two is enough IN THEORY -- a cyclic walk over a working set larger than an LRU
 * cache evicts every line before its reuse -- but it is a much thinner margin
 * than the 8x this experiment's nRF54L counterpart had, so the hot-vs-cold
 * throughput ratio MUST be checked before the cold figure is called a miss.
 * A larger set is available without linker cost by walking the carved MRAM
 * region directly (memory-mapped above the code window) instead of a linked
 * const array; that is the right move for the full experiment.
 *
 * AND THE SRAM TIER HERE IS NOT WHAT IT IS ON THE nRF54L.  s_sram lands in DTCM
 * (0x20000000), which on Cortex-M55 is TIGHTLY COUPLED and bypasses L1
 * entirely -- so the sram_* kinds measure an UNCACHED tier, and the cache knob
 * should not move them at all.  That makes three genuinely distinct tiers here
 * (uncached TCM / cached MRAM / cache-missing MRAM) where the nRF54L had two,
 * but it also means these numbers are NOT the same measurement as experiment 7's
 * "SRAM read" and must not be put in the same column.
 */
#define TIKU_AMBIQ_MEM_HOT_WORDS   1024u    /* 4 KB   -- inside the L1 D    */
#define TIKU_AMBIQ_MEM_COLD_WORDS 32768u    /* 128 KB -- 2x the L1 D (see below) */
#define TIKU_AMBIQ_MEM_STRIDE        17u    /* coprime with any power-2 line */
#define TIKU_AMBIQ_MEM_PASS_ACC     256u

static const uint32_t s_mram_hot[TIKU_AMBIQ_MEM_HOT_WORDS]   = { 0 };
static const uint32_t s_mram_cold[TIKU_AMBIQ_MEM_COLD_WORDS] = { 0 };
static uint32_t       s_sram[TIKU_AMBIQ_MEM_COLD_WORDS];
volatile uint32_t     tiku_ambiq_mem_sink;

static uint32_t s_acc;
static uint32_t s_sum;

uint32_t tiku_ambiq_mem_access_count(void) { return s_acc; }
uint32_t tiku_ambiq_mem_checksum(void)     { return s_sum; }
uint32_t tiku_ambiq_mem_hot_bytes(void)
{
    return TIKU_AMBIQ_MEM_HOT_WORDS * 4u;
}
uint32_t tiku_ambiq_mem_cold_bytes(void)
{
    return TIKU_AMBIQ_MEM_COLD_WORDS * 4u;
}

#define AMBIQ_PASS_READ(arr, mask, stride)                                    \
    do {                                                                      \
        unsigned k;                                                           \
        for (k = 0u; k < TIKU_AMBIQ_MEM_PASS_ACC / 8u; k++) {                 \
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

#define AMBIQ_PASS_WRITE(arr, mask, stride)                                   \
    do {                                                                      \
        unsigned k;                                                           \
        for (k = 0u; k < TIKU_AMBIQ_MEM_PASS_ACC / 8u; k++) {                 \
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

uint32_t tiku_ambiq_mem_probe(unsigned kind, uint32_t ms)
{
    uint32_t t0, dt, acc = 0u, idx = 0u;
    const uint32_t hot_mask  = TIKU_AMBIQ_MEM_HOT_WORDS - 1u;
    const uint32_t cold_mask = TIKU_AMBIQ_MEM_COLD_WORDS - 1u;
    uint32_t target = (uint32_t)(((uint64_t)ms * TIKU_AMBIQ_STIMER_HZ) / 1000u);

    /* Seed the SRAM buffer so ITS traversals have a live checksum.  The MRAM
     * arrays are `const` zero-filled: the linker emits them and the loads really
     * happen, but every word read back is 0, so for those kinds the checksum is
     * STRUCTURALLY zero and proves nothing.  Stated here rather than left to
     * imply a verification that is not happening. */
    if (s_sram[0] == 0u) {
        uint32_t j;
        for (j = 0u; j < TIKU_AMBIQ_MEM_COLD_WORDS; j++) {
            s_sram[j] = j * 2654435761u;
        }
    }
    s_acc = 0u;
    s_sum = 0u;
    t0 = tiku_ambiq_stimer_now();
    do {
        __asm__ volatile (".p2align 4" ::: "memory");
        switch (kind) {
        case TIKU_AMBIQ_MEM_NOP:
        default: {
            /* THE SAME PRIMITIVE as tiku_ambiq_spin_probe, not a copy of it.
             * Two hand-aligned copies of "the same" loop measured 2.1x apart
             * on this core; one shared, section-aligned function cannot. */
            (void)tiku_ambiq_spin_pass(TIKU_AMBIQ_MEM_PASS_ACC);
            break;
        }
        case TIKU_AMBIQ_MEM_SRAM_R:
            AMBIQ_PASS_READ(s_sram, cold_mask, 1u);
            break;
        case TIKU_AMBIQ_MEM_SRAM_W:
            acc |= 1u;                 /* keep s_sram[0] non-zero (seed gate) */
            AMBIQ_PASS_WRITE(s_sram, cold_mask, 1u);
            break;
        case TIKU_AMBIQ_MEM_SRAM_STRIDE:
            AMBIQ_PASS_READ(s_sram, cold_mask, TIKU_AMBIQ_MEM_STRIDE);
            break;
        case TIKU_AMBIQ_MEM_MRAM_HOT:
            AMBIQ_PASS_READ(s_mram_hot, hot_mask, 1u);
            break;
        case TIKU_AMBIQ_MEM_MRAM_COLD:
            AMBIQ_PASS_READ(s_mram_cold, cold_mask, TIKU_AMBIQ_MEM_STRIDE);
            break;
        }
        s_acc += TIKU_AMBIQ_MEM_PASS_ACC;
        s_sum += acc;
        tiku_hang_checkin();
        dt = tiku_ambiq_stimer_now() - t0;
    } while (dt < target);
    tiku_ambiq_mem_sink = acc;
    return tiku_ambiq_stimer_us(dt);
}

/*---------------------------------------------------------------------------*/
/* DEEP-SLEEP AUTORUN STAIRCASE                                              */
/*---------------------------------------------------------------------------*/

/*
 * EXISTS BECAUSE REAL DEEP SLEEP AND A CONSOLE ARE MUTUALLY EXCLUSIVE ON THIS
 * RIG.  SLEEPDEEP is demoted to normal sleep while the debug domain is powered
 * (measured: deep == plain to 3 uA, twice), the domain is powered whenever the
 * on-board J-Link has latched its DAP power request, and the J-Link's USB is
 * also the console.  So the deep-sleep measurement must run with J16 unplugged
 * -- no console, no debugger -- and the firmware carries the whole protocol
 * itself.  The trace IS the report: each state has a distinct duration and
 * level, so the meter's recording segments unambiguously without a wire.
 *
 * One cycle (Joulescope-readable):
 *     spin  3 s     high plateau -- cycle marker + liveness proof
 *     idle 10 s     plain WFI reference (all clocks running)
 *     deep 45 s     SLEEPDEEP + UART domain off + tick stretched
 *
 * With the debugger attached, the deep segment reads ~2.6-2.8 mA (demoted --
 * a live rehearsal of the sequence).  With J16 unplugged and the board powered
 * from the Apollo5 USB connector, PWRSTDBG never latches and the same segment
 * should fall to the datasheet's deep-sleep-2 class: 57 uW all-retained
 * (~32 uA at 1.8 V) -- SSRAM retention is the default, NVMPWDSLP=1 already,
 * TCM retains.  The tidy steps mirror the measured ladder: buck (2.07x on
 * dynamic), crypto/OTP/NVM1/ROM off (-1.19 mA), TRCENA clear (-98 uA).
 *
 * Runs INSTEAD of the scheduler (the TIKU_TURBO_BENCH pattern) and never
 * returns.  Reflashing afterwards: reconnect J16 and flash as usual -- the
 * J-Link connect sequence takes the part via reset, and nothing here is
 * persistent (every register this touches reverts on POR).
 */
void tiku_ambiq_power_autorun(void)
{
    /* v5 -- INSTRUMENTED BRING-UP.  v4 died in a fast reset loop (SWD could
     * not even attach) somewhere between unmasking interrupts and the first
     * cycle, and with no console the only debugger left is the METER: a short
     * spin marker after every risky step turns the current trace into a
     * progress log.  A trace that ends after marker N names step N+1.
     * The CLKGEN LFRCCTRL writes from v4 are gone entirely -- unverified
     * register interface, prime fault suspect; if LFRC's output is not already
     * running, the verified-reclock step falls back to the crystal instead. */

    /* step 1: unmask IRQs (reset handler masks; the scheduler normally
     * unmasks; without this every WFI in v1-v3 fell straight through). */
    __asm__ volatile ("cpsie i" ::: "memory");
    (void)tiku_ambiq_spin_probe(2000u);          /* marker A: survived cpsie */

    (void)tiku_ambiq_sleep_probe(5000u, 0u);     /* attach window */

    /* step 2: regulator + standing domains (the measured ladder). */
    (void)tiku_cpu_freq_ambiq_simobuck_enable();
    PWRCTRL->DEVPWREN_b.PWRENCRYPTO = 0u;
    PWRCTRL->DEVPWREN_b.PWRENOTP    = 0u;
    PWRCTRL->MEMPWREN_b.PWRENNVM1   = 0u;
    PWRCTRL->MEMPWREN_b.PWRENROM    = 0u;
    (*(volatile uint32_t *)0xE0001000UL) &= ~1UL;
    (*(volatile uint32_t *)0xE000EDFCUL) &= ~(1UL << 24);
    (void)tiku_ambiq_spin_probe(2000u);          /* marker B: survived tidy */

    /* step 3: move the timebase off the sick crystal, WITH verification and
     * fallback -- a clock is trusted only after it is seen counting. */
    STIMER->STCFG = (STIMER->STCFG & ~0xFu) | 6u;   /* LFRC_NOMINAL */
    {
        uint32_t c0 = tiku_ambiq_stimer_now();
        uint32_t spin = 3000000u;
        while (tiku_ambiq_stimer_now() == c0 && spin-- != 0u) { }
        if (tiku_ambiq_stimer_now() != c0) {
            tiku_ambiq_stimer_hz = 900u;
        } else {
            STIMER->STCFG = (STIMER->STCFG & ~0xFu) | 3u;  /* XTAL fallback */
        }
    }
    (void)tiku_ambiq_spin_probe(2000u);          /* marker C: clock verified */

    for (;;) {
        (void)tiku_ambiq_spin_probe(3000u);
        (void)tiku_ambiq_sleep_probe(8000u, 0u);
        (void)tiku_ambiq_sleep_probe(30000u,
                                     TIKU_AMBIQ_SLEEP_DEEP
                                     | TIKU_AMBIQ_SLEEP_STOP_UART);
        (void)tiku_ambiq_spin_probe(2000u);
        /* THE FINAL SEGMENT IS A MEASUREMENT DISGUISED AS A DEFECT.  The
         * tick-stretched deep sleep never wakes under real sleep (the far
         * compare does not fire -- a genuine tickless bug on this port, on
         * the work list).  Debugger-free, that failure mode IS the target
         * state: an eternal, wake-free, true deep sleep -- the exact
         * configuration of the datasheet's uW rows, held indefinitely for
         * the meter.  One full cycle of markers runs first, so the trace
         * proves the board was alive and which state it parked in. */
        (void)tiku_ambiq_sleep_probe(20000u,
                                     TIKU_AMBIQ_SLEEP_DEEP
                                     | TIKU_AMBIQ_SLEEP_STOP_UART
                                     | TIKU_AMBIQ_SLEEP_STOP_TICK);
    }
}

/*---------------------------------------------------------------------------*/
/* DEBUGGER STATE                                                            */
/*---------------------------------------------------------------------------*/

int tiku_ambiq_debugger_attached(void)
{
    /* MCUCTRL.DEBUGGER: a forgotten debug session cost ~130 uA on the other
     * platform and quietly turned every low-power figure into an upper bound.
     * Bit 0 low means the debugger interface is enabled on this part. */
    return ((MCUCTRL->DEBUGGER & 1u) == 0u) ? 1 : 0;
}

#endif /* PLATFORM_AMBIQ && TIKU_AMBIQ_POWER_PROBE */
