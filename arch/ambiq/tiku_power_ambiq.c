/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_ambiq.c - Apollo510 power-measurement instruments.
 *
 * The STIMER timebase, L1 cache controls, a core-clock measurement against
 * DWT, the sleep, spin and memory-access probes, and the console-free
 * deep-sleep sequence that tiku_ambiq_power_autorun() runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

/* Apollo510 only: the Makefile sets TIKU_AMBIQ_POWER_PROBE for that part. */
#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)

#include "tiku_power_ambiq.h"
#include "apollo510.h"          /* CMSIS register map: STIMER, PWRCTRL, ... */
#include <kernel/cpu/tiku_hang.h>   /* check-in while a probe blocks          */
#include <arch/ambiq/tiku_timer_arch.h> /* tick rate, STIMER reclock          */
#include <kernel/timers/tiku_clock.h>   /* tickless stretch (tick flag)       */
#include <arch/ambiq/tiku_uart_arch.h>  /* console re-init (uart flag)        */
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h> /* SIMOBUCK enable (autorun) */

/*---------------------------------------------------------------------------*/
/* TIMEBASE -- the always-on STIMER                                          */
/*---------------------------------------------------------------------------*/

/*
 * SysTick stops during WFI on this part, so windows are timed on the STIMER.
 * The counter runs in another clock domain and one read can catch it
 * mid-update: of three reads, the first is used if the first two agree, else
 * the third.
 */
uint32_t tiku_ambiq_stimer_now(void)
{
    uint32_t v0 = STIMER->STTMR;
    uint32_t v1 = STIMER->STTMR;
    uint32_t v2 = STIMER->STTMR;
    return (v0 == v1) ? v0 : v2;
}

/* STIMER rate the probes time with: 32768 Hz on the crystal, or 900 Hz once
 * tiku_ambiq_power_autorun() has moved the STIMER to LFRC_NOMINAL, because
 * debugger-free deep sleep stops the crystal and the STIMER with it.  The LFRC
 * is uncalibrated, so windows timed on it are approximate.  Only the autorun
 * changes this value; tiku_ambiq_stimer_reclock() does not. */
static uint32_t tiku_ambiq_stimer_hz = 32768u;
#define TIKU_AMBIQ_STIMER_HZ tiku_ambiq_stimer_hz

uint32_t tiku_ambiq_stimer_us(uint32_t counts)
{
    /* On the crystal 1e6/32768 == 15625/512 exactly; on the LFRC the result
     * is only as accurate as the nominal rate. */
    if (tiku_ambiq_stimer_hz == 32768u) {
        return (uint32_t)(((uint64_t)counts * 15625u) >> 9);
    }
    return (uint32_t)(((uint64_t)counts * 1000000u) / tiku_ambiq_stimer_hz);
}

/*---------------------------------------------------------------------------*/
/* CACHE -- the Cortex-M55's architectural L1                                */
/*---------------------------------------------------------------------------*/

/*
 * The Apollo510 has no vendor cache controller: the L1 caches are switched
 * through SCB.CCR.IC/DC with the CMSIS maintenance calls, and
 * tiku_ambiq_cache_enabled() reads the state back from CCR.IC.
 */
void tiku_ambiq_cache_set(int on)
{
    if (on) {
        SCB_EnableICache();
        SCB_EnableDCache();
    } else {
        /* SCB_DisableDCache() cleans dirty lines to memory as it disables the
         * D-cache; the I-cache goes off after it. */
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
    /* Cortex-M55 cache sizes are an implementer choice.  CCSIDR gives the
     * sets, ways and line length of the cache CSSELR selects. */
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
/* CORE CLOCK MEASUREMENT                                                    */
/*---------------------------------------------------------------------------*/

/* DWT CYCCNT is a free-running 32-bit core-cycle counter with no reload: at
 * 96-250 MHz it wraps every 17-45 s, so a 16 ms window is unambiguous. */
#define TIKU_SCB_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)
#define TIKU_DWT_CTRL    (*(volatile uint32_t *)0xE0001000UL)
#define TIKU_DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004UL)
#define TIKU_DWT_CYCCNTENA (1UL << 0)
#define TIKU_SCB_TRCENA    (1UL << 24)

unsigned long tiku_ambiq_cpu_hz_measure(void)
{
    uint32_t t0, dt, c0, c1;
    uint32_t target = TIKU_AMBIQ_STIMER_HZ / 64u;    /* ~16 ms             */

    /* TRCENA and the cycle counter are turned on and left on. */
    TIKU_SCB_DEMCR |= TIKU_SCB_TRCENA;
    TIKU_DWT_CTRL  |= TIKU_DWT_CYCCNTENA;
    __DSB();
    c0 = TIKU_DWT_CYCCNT;
    t0 = tiku_ambiq_stimer_now();
    /* Returns 0 if CYCCNT does not advance within 100000 polls. */
    if (TIKU_DWT_CYCCNT == c0) {
        uint32_t guard = 0u;
        while (TIKU_DWT_CYCCNT == c0 && guard < 100000u) { guard++; }
        if (TIKU_DWT_CYCCNT == c0) {
            return 0ul;                  /* no DWT cycle counter */
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
    /* The unsigned difference is correct across one CYCCNT wrap.
     * Hz = cycles * STIMER rate / dt. */
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

/**
 * @brief Count @p n down to zero; the spin probe and the NOP memory kind run
 *        this loop.
 *
 * The loop's cycle cost on this core depends on its address modulo 16, and
 * `.p2align` aligns only within a section, so the function has its own section
 * and a declared alignment; noclone keeps GCC from emitting an .isra copy.
 */
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

/**
 * @brief Run a sleep or spin (@p spin non-zero) window of @p ms; returns its
 *        length in microseconds.
 *
 * Both kinds share this body and differ only in the loop step: WFI or one
 * spin pass.  A spin window ignores @p flags.
 */
static uint32_t ambiq_probe(uint32_t ms, unsigned flags, int spin)
{
    uint32_t t0, dt = 0u;
    uint32_t freeze = 0u;
    uint32_t hz_used = TIKU_AMBIQ_STIMER_HZ;
    uint32_t lfrc_hz = 0u;
    uint32_t elp_saved = 0u;
    uint32_t target;

    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_UART) != 0u) {
        /* Wait 4 ms for the caller's last console output to drain, then
         * power the UART1 domain off: an enabled UART holds a standing HFRC
         * request, which keeps HFRC running in deep sleep. */
        tiku_cpu_ambiq_delay_us(4000u);
        PWRCTRL->DEVPWREN_b.PWRENUART1 = 0u;
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_LFRC) != 0u) {
        /* The reclock comes before the tick stretch, which it refuses while
         * one is open, and before the target is computed, so the whole
         * window is timed in LFRC counts at the calibrated rate.  A return of
         * 0 leaves the STIMER, and the window, on the crystal. */
        lfrc_hz = tiku_ambiq_stimer_reclock(1);
        if (lfrc_hz != 0u) {
            hz_used = lfrc_hz;
        }
    }
    target = (uint32_t)(((uint64_t)ms * hz_used) / 1000u);
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_TICK) != 0u) {
        /* Interrupts are masked because tiku_clock_tickless_begin() moves the
         * compare the live tick ISR uses.  The stretch is credited in whole
         * ticks on wake. */
        __asm__ volatile ("cpsid i" ::: "memory");
        (void)tiku_clock_tickless_begin(
            (tiku_clock_time_t)((ms * TIKU_CLOCK_ARCH_SECOND) / 1000u + 2u));
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DBGLOCK) != 0u) {
        MCUCTRL->DEBUGGER = 1u;          /* lockout; cleared below */
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DEEP) != 0u) {
        /* A wake from SLEEPDEEP with ELPSTATE=RET (2) faults (MemManage
         * IACCVIOL or HardFault at a garbage PC): the FP/MVE context does not
         * survive the deep wake path.  ELPSTATE=1 (on, clock stopped) wakes
         * without a fault, so the window writes 1 in place of 2 and
         * restores 2 after.  Any SLEEPDEEP entry needs the same save and
         * restore. */
        elp_saved = (PWRMODCTL->CPDLPSTATE >> 4) & 0x3u;
        if (elp_saved == 2u) {
            PWRMODCTL->CPDLPSTATE =
                (PWRMODCTL->CPDLPSTATE & ~(0x3u << 4)) | (1u << 4);
        }
        SCB->SCR |= (1ul << 2);          /* SLEEPDEEP */
    }
    __DSB();
    s_wakes = 0u;
    s_passes = 0u;
    t0 = tiku_ambiq_stimer_now();
    do {
        if (spin) {
            (void)tiku_ambiq_spin_pass(TIKU_AMBIQ_SPIN_INNER);
            s_passes++;
        } else {
            __asm__ volatile ("wfi" ::: "memory");
            s_wakes++;
        }
        /* The probe holds the CPU for the whole window, so it checks in
         * with the hang detector on every step. */
        tiku_hang_checkin();
        {
            uint32_t now2 = tiku_ambiq_stimer_now();
            if (now2 - t0 == dt) {
                /* Counter unchanged since the last iteration.  After
                 * 2000000 such iterations in a row the STIMER is taken as
                 * stopped and the window ends early. */
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
        SCB->SCR &= ~(1ul << 2);   /* else it applies to every later WFI */
        if (elp_saved == 2u) {     /* restore the caller's ELPSTATE=RET */
            PWRMODCTL->CPDLPSTATE =
                (PWRMODCTL->CPDLPSTATE & ~(0x3u << 4)) | (2u << 4);
        }
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_DBGLOCK) != 0u) {
        MCUCTRL->DEBUGGER = 0u;
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_TICK) != 0u) {
        __asm__ volatile ("cpsid i" ::: "memory");
        tiku_clock_tickless_end();
        __asm__ volatile ("cpsie i" ::: "memory");
    }
    if (!spin && lfrc_hz != 0u) {
        (void)tiku_ambiq_stimer_reclock(0);    /* back to the crystal */
    }
    if (!spin && (flags & TIKU_AMBIQ_SLEEP_STOP_UART) != 0u) {
        PWRCTRL->DEVPWREN_b.PWRENUART1 = 1u;
        {
            uint32_t spin_ack = 200000u;
            while (spin_ack-- != 0u) { __asm__ volatile ("nop"); }
        }
        tiku_uart_init();          /* full re-init of the UART configuration */
    }
    /* dt is in counts of whichever timebase timed the window. */
    return (hz_used == 32768u)
               ? (uint32_t)(((uint64_t)dt * 15625u) >> 9)
               : (uint32_t)(((uint64_t)dt * 1000000u) / hz_used);
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
 * The L1 D-cache is 64 KB with 32-byte lines (CCSIDR).  The hot MRAM set is
 * 4 KB and stays resident.  The cold set is 128 KB, twice the D-cache; a larger
 * one does not fit the 384 KB code window and the 512 KB TCM beside the rest
 * of the image.  A cyclic walk over a set larger than an LRU cache evicts each
 * line before its reuse, but with a 2x margin the hot and cold throughput
 * should be compared before the cold figure is read as all misses.
 *
 * s_sram is in DTCM (0x20000000), which bypasses the L1, so the SRAM kinds
 * measure uncached TCM and the cache setting does not affect them.
 */
#define TIKU_AMBIQ_MEM_HOT_WORDS   1024u    /* 4 KB   -- inside the L1 D    */
#define TIKU_AMBIQ_MEM_COLD_WORDS 32768u    /* 128 KB -- 2x the L1 D        */
#define TIKU_AMBIQ_MEM_STRIDE        17u    /* odd: walk covers every word  */
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

    /* Seed s_sram so the SRAM kinds produce a non-zero checksum.  The MRAM
     * arrays are zero-filled constants: their loads happen, but their
     * checksum is always 0. */
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
            /* The section-aligned loop the spin probe runs, so an iteration
             * here costs what a spin-probe iteration costs. */
            (void)tiku_ambiq_spin_pass(TIKU_AMBIQ_MEM_PASS_ACC);
            break;
        }
        case TIKU_AMBIQ_MEM_SRAM_R:
            AMBIQ_PASS_READ(s_sram, cold_mask, 1u);
            break;
        case TIKU_AMBIQ_MEM_SRAM_W:
            acc |= 1u;                 /* s_sram[0] stays non-zero: no reseed */
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
/* DEEP-SLEEP AUTORUN                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Runs in place of the scheduler and never returns.  SLEEPDEEP acts as plain
 * sleep while the debug domain is powered, and the on-board J-Link powers it
 * once it latches its DAP power request; its USB (J16) is also the console.
 * So the sequence runs with J16 unplugged and the board powered from the
 * Apollo5 USB connector, and the supply current is its only output: each
 * state has its own duration and level.
 *
 * Bring-up: step 1 unmasks interrupts (marker A, then a 5 s WFI window);
 * step 2 turns SIMOBUCK on and crypto, OTP, NVM1, ROM, CYCCNT and TRCENA off
 * (marker B); step 3 moves the STIMER to the LFRC, or back to the crystal if
 * the LFRC count does not advance (marker C).  Each marker is a 2 s spin, so
 * a trace that ends after a marker stopped in the next step.
 *
 * Each cycle, with durations approximate on the uncalibrated LFRC:
 *     spin   3 s   cycle marker
 *     idle   8 s   plain WFI
 *     deep  30 s   SLEEPDEEP with the UART domain off
 *     spin   2 s   marker
 *     deep  20 s   SLEEPDEEP, UART domain off, tick stretched
 *
 * Every register written here reverts on power-on reset, so with J16
 * connected again the part is flashed as usual.
 */
void tiku_ambiq_power_autorun(void)
{
    /* Step 1: the reset handler leaves interrupts masked and no scheduler
     * runs to unmask them; masked, every WFI here would return at once. */
    __asm__ volatile ("cpsie i" ::: "memory");
    (void)tiku_ambiq_spin_probe(2000u);          /* marker A: past cpsie */

    (void)tiku_ambiq_sleep_probe(5000u, 0u);     /* SWD can attach here */

    /* Step 2: SIMOBUCK on; crypto, OTP, NVM1 and ROM powered down;
     * DWT_CTRL.CYCCNTENA and DEMCR.TRCENA cleared. */
    (void)tiku_cpu_freq_ambiq_simobuck_enable();
    PWRCTRL->DEVPWREN_b.PWRENCRYPTO = 0u;
    PWRCTRL->DEVPWREN_b.PWRENOTP    = 0u;
    PWRCTRL->MEMPWREN_b.PWRENNVM1   = 0u;
    PWRCTRL->MEMPWREN_b.PWRENROM    = 0u;
    (*(volatile uint32_t *)0xE0001000UL) &= ~1UL;
    (*(volatile uint32_t *)0xE000EDFCUL) &= ~(1UL << 24);
    (void)tiku_ambiq_spin_probe(2000u);          /* marker B: past step 2 */

    /* Step 3: the 32 kHz crystal stops in deep sleep, so the STIMER moves to
     * LFRC_NOMINAL and stays there only if the counter is seen to advance;
     * otherwise it returns to the crystal. */
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
    (void)tiku_ambiq_spin_probe(2000u);          /* marker C: past step 3 */

    for (;;) {
        (void)tiku_ambiq_spin_probe(3000u);
        (void)tiku_ambiq_sleep_probe(8000u, 0u);
        (void)tiku_ambiq_sleep_probe(30000u,
                                     TIKU_AMBIQ_SLEEP_DEEP
                                     | TIKU_AMBIQ_SLEEP_STOP_UART);
        (void)tiku_ambiq_spin_probe(2000u);
        /* The tick stretch is armed in counts of the crystal tick period,
         * which step 3 leaves unchanged: its 20 s of crystal counts last
         * about 12 minutes on the 900 Hz LFRC, and on the crystal fallback,
         * which debugger-free deep sleep stops, the compare never fires.
         * The board stays in deep sleep with no wakes for that time. */
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
    /* Bit 0 of MCUCTRL.DEBUGGER is the SWD lockout; clear means the debug
     * interface is enabled. */
    return ((MCUCTRL->DEBUGGER & 1u) == 0u) ? 1 : 0;
}

#endif /* PLATFORM_AMBIQ && TIKU_AMBIQ_POWER_PROBE */
