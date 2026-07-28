/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_power.c - "power": report and steer what the part costs.
 *
 * WHY A COMMAND AND NOT JUST BUILD FLAGS.  The first attempt at measuring the
 * cache and the DC/DC built four firmware images with different -D flags and
 * compared the current draw.  All four measured the same, and the reason took
 * a while to find: one of the two settings had silently not applied, and a
 * build flag is not evidence that a register was written.  An instrument
 * measures milliamps; only the device can say what state produced them.
 *
 * So every knob here is both readable and (where the silicon permits)
 * switchable at run time, which also means one boot can measure a workload
 * both ways instead of comparing across two flashes with everything else
 * subtly different.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_power.h"

#if TIKU_SHELL_CMD_POWER

#include <kernel/shell/tiku_shell_io.h>
#include <hal/tiku_cpu.h>
#include "tiku_shell_cmd_sleep.h"
#include <string.h>

#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_power_arch.h>
#include <arch/nordic/tiku_device_select.h>  /* instance macros for `floor` */
#include <arch/nordic/tiku_cpu_common.h>     /* tiku_cpu_nordic_delay_ms    */
#endif
#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)
#include <arch/ambiq/tiku_power_ambiq.h>
#include <arch/ambiq/tiku_timer_arch.h>       /* stimer reclock / rate       */
#include <arch/ambiq/tiku_cpu_common.h>       /* tiku_cpu_ambiq_delay_us     */
#if (TIKU_DRV_NOR_ENABLE + 0)
#include <arch/ambiq/tiku_nor_arch.h>          /* MSPI1 + U12 external NOR    */
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#include <arch/ambiq/tiku_psram_arch.h>        /* MSPI0 + U14 external PSRAM  */
#include <kernel/memory/tiku_mem.h>           /* TIKU_MEM_PSRAM tier gate    */
#endif
#include <kernel/timers/tiku_clock.h>         /* tickless begin/end (guard)  */
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h>  /* SIMOBUCK enable hook     */
#if (TIKU_AMBIQ_POWER_PROBE_GPU + 0)
#include <arch/ambiq/tiku_gpu_power.h>        /* experiment 2: GPU as compute */
#endif
#if (TIKU_AMBIQ_POWER_PROBE_SIMD + 0)
#include <arch/ambiq/tiku_simd_power.h>       /* experiment 3: Helium vs scalar */
#endif
#include "apollo510.h"                       /* PWRCTRL / CLKGEN for `floor` */
#endif

static int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/** @brief Parse "on"/"1" and "off"/"0"; -1 if neither. */
static int parse_on_off(const char *tok)
{
    if (streq(tok, "on") || streq(tok, "1")) {
        return 1;
    }
    if (streq(tok, "off") || streq(tok, "0")) {
        return 0;
    }
    return -1;
}

static void power_report(void)
{
    SHELL_PRINTF("core:  %lu MHz\n", tiku_cpu_mclk_hz() / 1000000UL);
    SHELL_PRINTF("pclk:  %lu MHz\n", tiku_cpu_smclk_hz() / 1000000UL);
#if defined(PLATFORM_NORDIC)
    SHELL_PRINTF("cache: %s\n", tiku_nordic_cache_enabled() ? "on" : "off");
    {
        int det = tiku_nordic_dcdc_inductor_present();
        /* -1 means the detector could not run because the converter is on.
         * Saying "absent" there would be a fabricated answer. */
        SHELL_PRINTF("dcdc:  %s (inductor %s)\n",
                     tiku_nordic_dcdc_enabled() ? "on" : "off",
                     (det < 0) ? "unknown while dcdc on -- 'power probe'"
                               : (det ? "detected" : "absent"));
    }
#endif
    SHELL_PRINTF("idle:  %s\n", tiku_shell_sleep_mode_str());
#if defined(PLATFORM_NORDIC)
    /* Datasheet 9.3: the low-power figures apply to Normal mode; a device
     * still in debug interface mode measures as an upper bound only. */
    SHELL_PRINTF("debug: %s\n", tiku_nordic_debug_attached()
                 ? "ATTACHED (currents are upper bounds)" : "normal");
#endif
}

#if defined(PLATFORM_NORDIC)
static void power_stat(void)
{
    uint32_t hit = 0u, miss = 0u, rd = 0u, wr = 0u;
    uint32_t total;

    tiku_nordic_cache_profile_read(&hit, &miss, &rd, &wr);
    total = hit + miss;
    SHELL_PRINTF("cache hits %lu misses %lu reads %lu writes %lu\n",
                 (unsigned long)hit, (unsigned long)miss,
                 (unsigned long)rd, (unsigned long)wr);
    if (total != 0u) {
        /* Integer percent to one decimal; SHELL_PRINTF has no %f. */
        uint32_t per_mille = (uint32_t)(((uint64_t)hit * 1000u) / total);
        SHELL_PRINTF("hit rate %lu.%lu%%\n",
                     (unsigned long)(per_mille / 10u),
                     (unsigned long)(per_mille % 10u));
    } else {
        /* A counter pair of zero means profiling was never started, not that
         * the cache never hit -- worth distinguishing, because "0 hits" reads
         * like a damning result and is usually just an unarmed counter. */
        SHELL_PRINTF("hit rate n/a (counters not started -- run 'power clear')\n");
    }
}
#endif

#if (TIKU_DRV_NOR_ENABLE + 0)
/** @brief NOR bring-up step tracer -- a wedged step names itself. */
static void nor_trace(const char *step)
{
    SHELL_PRINTF("  nor step: %s\n", step);
}

/** @brief Shared error-name table for the NOR verbs. */
static const char *nor_errname(tiku_nor_err_t rc)
{
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "ID", "ARG", "STATE", "PROGRAM" };
    return ((unsigned)rc < 8u) ? en[rc] : "?";
}
#endif

#if (TIKU_DRV_PSRAM_ENABLE + 0)
/**
 * @brief PSRAM bring-up step tracer.
 *
 * Prints each step BEFORE it runs and flushes, so if a register write stalls
 * the bus the last line on the wire names the step that wedged.  This is how
 * the first bring-up attempt's silent hang was localised.
 */
static void psram_trace(const char *step)
{
    SHELL_PRINTF("  psram step: %s\n", step);
}
#endif

void tiku_shell_cmd_power(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        power_report();
        return;
    }

#if defined(PLATFORM_NORDIC)
    if (streq(argv[1], "stat")) {
        power_stat();
        return;
    }
    if ((streq(argv[1], "idle") || streq(argv[1], "spin")) && argc >= 3) {
        /* ONE PARSER FOR BOTH STATES so they cannot be given different
         * peripheral releases by accident -- if they were, their difference
         * would measure the peripherals rather than the CPU.
         *   idle <ms>  WFI          spin <ms>  while(1)
         * plus "quiet" as shorthand for every release this port knows. */
        int spin = streq(argv[1], "spin");
        unsigned flags = 0u, i;
        uint32_t ms = 0u, us;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        for (i = 3u; i < (unsigned)argc; i++) {
            if (streq(argv[i], "pll"))  { flags |= TIKU_SLEEP_STOP_PLL; }
            if (streq(argv[i], "uart")) { flags |= TIKU_SLEEP_STOP_UART; }
            if (streq(argv[i], "hfxo")) { flags |= TIKU_SLEEP_STOP_HFXO; }
            if (streq(argv[i], "deep")) { flags |= TIKU_SLEEP_DEEP; }
            if (streq(argv[i], "tim"))  { flags |= TIKU_SLEEP_STOP_TIM; }
            if (streq(argv[i], "tick")) { flags |= TIKU_SLEEP_STOP_TICK; }
            if (streq(argv[i], "sysc")) { flags |= TIKU_SLEEP_STOP_SYSC; }
            if (streq(argv[i], "quiet")) {
                flags |= TIKU_SLEEP_STOP_PLL | TIKU_SLEEP_STOP_UART |
                         TIKU_SLEEP_STOP_HFXO | TIKU_SLEEP_STOP_TIM |
                         TIKU_SLEEP_DEEP;
            }
        }
        if (ms == 0u) {
            SHELL_PRINTF("Usage: power %s <ms> "
                         "[quiet|deep|tim|tick|uart|pll|hfxo]\n", argv[1]);
            return;
        }
        /* EVERY flag is printed, including tim.  This line IS the provenance
         * record in a measurement log: one release missing from it and a later
         * reader attributes the current to the wrong state.  It omitted tim
         * while `quiet` was setting it, which is exactly that failure. */
        SHELL_PRINTF("%s %lu ms flags%s%s%s%s%s%s%s -- starting\n", argv[1],
                     (unsigned long)ms,
                     (flags & TIKU_SLEEP_STOP_PLL)  ? " pll"  : "",
                     (flags & TIKU_SLEEP_STOP_UART) ? " uart" : "",
                     (flags & TIKU_SLEEP_STOP_HFXO) ? " hfxo" : "",
                     (flags & TIKU_SLEEP_STOP_TIM)  ? " tim"  : "",
                     (flags & TIKU_SLEEP_STOP_TICK) ? " tick" : "",
                     (flags & TIKU_SLEEP_STOP_SYSC) ? " sysc" : "",
                     (flags & TIKU_SLEEP_DEEP)      ? " deep" : "");
        us = spin ? tiku_nordic_spin_probe(ms, flags)
                  : tiku_nordic_sleep_probe(ms, flags);
        if (spin) {
            /* Report the WORK as well as the window.  Without the denominator a
             * lower current reads as a saving even when it is only a slowdown. */
            uint32_t p = tiku_nordic_spin_pass_count();
            uint32_t in = tiku_nordic_spin_inner();
            SHELL_PRINTF("spin done %lu us passes %lu iter %lu kiter/s %lu\n",
                         (unsigned long)us, (unsigned long)p,
                         (unsigned long)(p * in),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)p * in
                                            * 1000u) / us) : 0u));
        } else {
            SHELL_PRINTF("idle done %lu us wakes %lu (%lu/s)\n",
                         (unsigned long)us,
                         (unsigned long)tiku_nordic_sleep_wake_count(),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)
                            tiku_nordic_sleep_wake_count() * 1000000u) / us) : 0u));
        }
#if (TIKU_FLPR_ENABLE + 0)
        /* Coprocessor work retired inside the SAME window, whichever state the
         * app core was in -- so both cores' costs share one denominator and the
         * host never has to infer the interval from shell round-trips. */
        if (tiku_nordic_flpr_pass_delta() != 0u) {
            SHELL_PRINTF("flpr passes %lu in %lu us\n",
                         (unsigned long)tiku_nordic_flpr_pass_delta(),
                         (unsigned long)us);
        }
#endif
        return;
    }
    if (streq(argv[1], "mem") && argc >= 4) {
        /* power mem <kind> <ms> -- price one memory access.
         * Reports accesses and the traversal checksum, so a later reader can
         * confirm two configurations did the SAME work rather than trusting
         * that they did.  Current per unit TIME is not efficiency; accesses
         * are the denominator. */
        static const char *const names[TIKU_MEM_KIND_COUNT] = {
            "nop", "sram_r", "sram_w", "sram_stride", "rram_hot", "rram_cold"
        };
        unsigned kind = TIKU_MEM_KIND_COUNT;
        uint32_t ms = 0u, us, n, i;
        const char *p2 = argv[3];
        for (i = 0u; i < TIKU_MEM_KIND_COUNT; i++) {
            if (streq(argv[2], names[i])) {
                kind = i;
            }
        }
        while (*p2 >= '0' && *p2 <= '9') {
            ms = ms * 10u + (uint32_t)(*p2++ - '0');
        }
        if (kind == TIKU_MEM_KIND_COUNT || ms == 0u) {
            SHELL_PRINTF("Usage: power mem <nop|sram_r|sram_w|sram_stride|"
                         "rram_hot|rram_cold> <ms>\n");
            return;
        }
        SHELL_PRINTF("mem %s %lu ms -- starting\n", names[kind],
                     (unsigned long)ms);
        us = tiku_nordic_mem_probe(kind, ms);
        n = tiku_nordic_mem_access_count();
        SHELL_PRINTF("mem done %lu us acc %lu kacc/s %lu sum %lx\n",
                     (unsigned long)us, (unsigned long)n,
                     (unsigned long)(us ? (uint32_t)(((uint64_t)n * 1000u) / us)
                                        : 0u),
                     (unsigned long)tiku_nordic_mem_checksum());
        return;
    }
    if (streq(argv[1], "lfclk")) {
        /* Start the 32.768 kHz low-frequency clock.  Found NOT RUNNING by
         * `power floor`: the port had never started it, so the GRTC has been
         * keeping time off the HF path since boot -- and timekeeping that
         * needs HF is a standing reason the HF domain can never be gated,
         * whatever else a sleep releases.  The datasheet's ~3 uA System ON
         * idle figure is quoted with the GRTC on the 32 kHz crystal.
         * Deliberately a COMMAND, not a boot default, until its effect is
         * measured: this file's header explains why register writes are made
         * observable before they are made policy. */
        uint32_t src = CLOCK_LFCLK_SRC_SRC_LFXO;       /* DK has the crystal */
        if (argc >= 3 && streq(argv[2], "lfrc")) {
            src = CLOCK_LFCLK_SRC_SRC_LFRC;
        }
        if (argc >= 3 && streq(argv[2], "synth")) {
            src = CLOCK_LFCLK_SRC_SRC_LFSYNT;
        }
        NRF_CLOCK_S->LFCLK.SRC = src;
        NRF_CLOCK_S->EVENTS_LFCLKSTARTED = 0u;
        NRF_CLOCK_S->TASKS_LFCLKSTART = 1u;
        {
            /* LFXO start-up is crystal-settling, hundreds of ms worst case;
             * poll with a generous bound and report what actually happened
             * rather than assuming. */
            uint32_t spin = 0u;
            while (NRF_CLOCK_S->EVENTS_LFCLKSTARTED == 0u &&
                   spin < 60000000u) {
                spin++;
            }
            SHELL_PRINTF("lfclk: src=%lu started=%lu run=%lu stat=%lx\n",
                         (unsigned long)src,
                         (unsigned long)NRF_CLOCK_S->EVENTS_LFCLKSTARTED,
                         (unsigned long)NRF_CLOCK_S->LFCLK.RUN,
                         (unsigned long)NRF_CLOCK_S->LFCLK.STAT);
        }
        return;
    }
    if (streq(argv[1], "floor")) {
        /* Everything plausibly holding the idle floor up, in one read.  The
         * idle current measures ~1 mA against a ~3 uA System ON spec, and the
         * gap cannot be attributed by current alone -- attribution needs the
         * REGISTER state that coexists with the number.  Plain reads only. */
        SHELL_PRINTF("clock: xo.run=%lu pll.run=%lu lfclk.src=%lu run=%lu"
                     " stat=%lx\n",
                     (unsigned long)NRF_CLOCK_S->XO.RUN,
                     (unsigned long)NRF_CLOCK_S->PLL.RUN,
                     (unsigned long)NRF_CLOCK_S->LFCLK.SRC,
                     (unsigned long)NRF_CLOCK_S->LFCLK.RUN,
                     (unsigned long)NRF_CLOCK_S->LFCLK.STAT);
        /* RRAM: LOWPOWERCONFIG reset (0 = PowerOff in low power) is already
         * the frugal setting; CONFIG's ACCESSTIMEOUT field is the
         * active->standby delay in 31.25 ns units. */
        SHELL_PRINTF("rram:  config=%lx lowpower=%lx ready=%lu\n",
                     (unsigned long)NRF_RRAMC_S->POWER.CONFIG,
                     (unsigned long)NRF_RRAMC_S->POWER.LOWPOWERCONFIG,
                     (unsigned long)NRF_RRAMC_S->READY);
        SHELL_PRINTF("grtc:  mode=%lx (1=autoen 2=syscnten)\n",
                     (unsigned long)NRF_GRTC_S->MODE);
        SHELL_PRINTF("wdt30: run=%lu\n",
                     (unsigned long)NRF_WDT30_S->RUNSTATUS);
        SHELL_PRINTF("en:    console-uarte=%lu saadc=%lu spim00=%lu"
                     " cracen=%lx\n",
                     (unsigned long)TIKU_BOARD_CONSOLE_UARTE->ENABLE,
                     (unsigned long)NRF_SAADC_S->ENABLE,
                     (unsigned long)NRF_SPIM00_S->ENABLE,
                     (unsigned long)NRF_CRACEN_S->ENABLE);
        SHELL_PRINTF("irq:   gpiote20.inten=%lx gpiote30.inten=%lx\n",
                     (unsigned long)NRF_GPIOTE20_S->INTENSET0,
                     (unsigned long)NRF_GPIOTE30_S->INTENSET0);
#if defined(NRF_USBHS_S)
        SHELL_PRINTF("usbhs: enable=%lu\n",
                     (unsigned long)NRF_USBHS_S->ENABLE);
#endif
        return;
    }
    if (streq(argv[1], "why")) {
        /* RESETREAS decode.  Exists to answer one question: WHAT woke the
         * part out of System OFF, because "it came back" alone cannot
         * distinguish a button, the GRTC, or the debugger -- and each of
         * those implicates a completely different subsystem. */
        uint32_t r = *(volatile uint32_t *)0x5010E600UL;
        SHELL_PRINTF("RESETREAS %x:%s%s%s%s%s%s%s%s%s%s\n", (unsigned)r,
                     (r & (1u << 0))  ? " pin"      : "",
                     (r & (1u << 1))  ? " dog0"     : "",
                     (r & (1u << 3))  ? " ctrlsoft" : "",
                     (r & (1u << 4))  ? " ctrlhard" : "",
                     (r & (1u << 6))  ? " sreq"     : "",
                     (r & (1u << 8))  ? " OFF-wake" : "",
                     (r & (1u << 10)) ? " DIF(debugger)" : "",
                     (r & (1u << 11)) ? " GRTC"     : "",
                     (r & (1u << 12)) ? " nfc"      : "",
                     (r & (1u << 14)) ? " vbus"     : "");
        if (argc >= 3 && streq(argv[2], "clear")) {
            *(volatile uint32_t *)0x5010E600UL = r;   /* W1C */
            SHELL_PRINTF("cleared\n");
        }
        return;
    }
    if (streq(argv[1], "off")) {
        /* System OFF: the deepest state the part has, ~uA class per datasheet,
         * wake by reset/GPIO only.  This is the CONTROL for the idle-floor
         * hunt: P14 measures the VDDM RAIL, and if hundreds of uA remain with
         * the SoC in System OFF, that current is the BOARD's (or the debug
         * domain's) -- no firmware change can remove it, and chasing it in
         * software would be chasing a ghost. */
        SHELL_PRINTF("entering System OFF -- wake by RESET only\n");
        tiku_cpu_nordic_delay_ms(20u);          /* let the line leave the wire */
        tiku_nordic_system_off();               /* does not return */
    }

    if (streq(argv[1], "bench")) {
        uint32_t us = 0u, hit = 0u, miss = 0u, sum;
        tiku_nordic_cache_profile_start();
        sum = tiku_nordic_cache_workload(&us);
        tiku_nordic_cache_profile_read(&hit, &miss, NULL, NULL);
        /* The checksum is printed so a comparison across configurations can
         * confirm the same work was done, not merely that a number changed. */
        SHELL_PRINTF("bench us %lu sum %lu cache %s hits %lu misses %lu\n",
                     (unsigned long)us, (unsigned long)sum,
                     tiku_nordic_cache_enabled() ? "on" : "off",
                     (unsigned long)hit, (unsigned long)miss);
        return;
    }
    if (streq(argv[1], "clock")) {
        unsigned long meas = tiku_nordic_cpu_hz_measure();
        unsigned long rep  = tiku_cpu_mclk_hz();
        SHELL_PRINTF("reported %lu Hz\n", rep);
        SHELL_PRINTF("measured %lu Hz\n", meas);
        /* 2%% covers the 1 MHz GRTC's quantisation over a 50 ms window plus
         * the handful of cycles spent in the sampling code itself. */
        SHELL_PRINTF("clock: %s\n",
                     (meas > rep - rep / 50UL && meas < rep + rep / 50UL)
                         ? "AGREE" : "MISMATCH");
        return;
    }
    if (streq(argv[1], "probe")) {
        int det = tiku_nordic_dcdc_probe_inductor();
        SHELL_PRINTF("inductor: %s\n", det ? "detected" : "absent");
        SHELL_PRINTF("dcdc:     %s\n",
                     tiku_nordic_dcdc_enabled() ? "on" : "off");
        return;
    }
    if (streq(argv[1], "clear")) {
        tiku_nordic_cache_profile_start();
        SHELL_PRINTF("cache counters cleared and running\n");
        return;
    }
    if (streq(argv[1], "cache") && argc >= 3) {
        int on = parse_on_off(argv[2]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power cache on|off\n");
            return;
        }
        /* The datasheet sanctions this at run time -- unlike the core
         * frequency, which it explicitly does not. */
        tiku_nordic_cache_set(on);
        SHELL_PRINTF("cache: %s\n", tiku_nordic_cache_enabled() ? "on" : "off");
        return;
    }
    if (streq(argv[1], "dcdc") && argc >= 3) {
        int on = parse_on_off(argv[2]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power dcdc on|off\n");
            return;
        }
        if (on && !tiku_nordic_dcdc_inductor_present()) {
            SHELL_PRINTF("dcdc: refused -- no inductor detected on this board\n");
            return;
        }
        (void)tiku_nordic_dcdc_set(on);
        SHELL_PRINTF("dcdc: %s\n", tiku_nordic_dcdc_enabled() ? "on" : "off");
        return;
    }
#endif

#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)
    /* Apollo510 instruments.  A SEPARATE branch, not a shared one: the two parts
     * have different release vocabularies, different cache architectures and
     * different timebases, and a shared verb that quietly means different things
     * on each is how a cross-platform table ends up comparing nothing. */
    if (streq(argv[1], "floor")) {
        uint32_t ic = 0u, dc = 0u, ln = 0u;
        tiku_ambiq_cache_geometry(&ic, &dc, &ln);
        SHELL_PRINTF("cache: I %lu B  D %lu B  line %lu B  (read from CCSIDR,"
                     " not assumed)\n", (unsigned long)ic, (unsigned long)dc,
                     (unsigned long)ln);
        SHELL_PRINTF("  enabled: %s\n", tiku_ambiq_cache_enabled() ? "yes" : "no");
        /* The per-domain visibility this board has and the nRF54L DK does not:
         * which power domains and memories are actually on. */
        SHELL_PRINTF("pwr:   devpwr=%lx/%lx mempwr=%lx/%lx sys=%lx\n",
                     (unsigned long)PWRCTRL->DEVPWREN,
                     (unsigned long)PWRCTRL->DEVPWRSTATUS,
                     (unsigned long)PWRCTRL->MEMPWREN,
                     (unsigned long)PWRCTRL->MEMPWRSTATUS,
                     (unsigned long)PWRCTRL->SYSPWRSTATUS);
        SHELL_PRINTF("  ssram=%lx/%lx retcfg mem=%lx ssram=%lx perf=%lx\n",
                     (unsigned long)PWRCTRL->SSRAMPWREN,
                     (unsigned long)PWRCTRL->SSRAMPWRST,
                     (unsigned long)PWRCTRL->MEMRETCFG,
                     (unsigned long)PWRCTRL->SSRAMRETCFG,
                     (unsigned long)PWRCTRL->MCUPERFREQ);
        SHELL_PRINTF("clk:   octrl=%lx en=%lx/%lx/%lx misc=%lx\n",
                     (unsigned long)CLKGEN->OCTRL,
                     (unsigned long)CLKGEN->CLOCKENSTAT,
                     (unsigned long)CLKGEN->CLOCKEN2STAT,
                     (unsigned long)CLKGEN->CLOCKEN3STAT,
                     (unsigned long)CLKGEN->MISC);
        SHELL_PRINTF("debug: %s\n", tiku_ambiq_debugger_attached()
                     ? "ATTACHED (currents are upper bounds)" : "normal");
        SHELL_PRINTF("mem workloads: hot %lu B  cold %lu B "
                     "-- CHECK these against the cache sizes above\n",
                     (unsigned long)tiku_ambiq_mem_hot_bytes(),
                     (unsigned long)tiku_ambiq_mem_cold_bytes());
        return;
    }
    if (streq(argv[1], "dev") && argc >= 4) {
        /* Per-domain power switches, for the same reason exp3 grew per-release
         * flags on the nRF54L: a 4.3 mA idle floor cannot be attributed by
         * current alone, only by turning one thing off at a time and watching
         * the meter.  Each verb prints EN and STATUS after the write, because
         * an enable bit is a request, not evidence.
         *   crypto/otp:  DEVPWREN bits the SBL leaves on (SDK powers both off
         *                after use -- known standing drains on this family)
         *   nvm1:        the upper 2 MB MRAM bank; code+data live in NVM0
         *   rom:         boot ROM (needed again only for bootrom MRAM writes)
         *   ssram:       all 3 MB shared SRAM.  DESTRUCTIVE HERE: the 1 MB
         *                tier arena lives in SSRAM in this build, so contents
         *                are lost -- measure, then reboot.  Refused unless the
         *                caller says `off force`. */
        int on = parse_on_off(argv[3]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power dev <crypto|otp|nvm1|rom|ssram|trc> on|off\n");
            return;
        }
        if (streq(argv[2], "crypto")) {
            PWRCTRL->DEVPWREN_b.PWRENCRYPTO = (uint32_t)on;
        } else if (streq(argv[2], "otp")) {
            PWRCTRL->DEVPWREN_b.PWRENOTP = (uint32_t)on;
        } else if (streq(argv[2], "nvm1")) {
            /* GUARDED.  The carved NVM region (tier + file store) can extend
             * into the upper bank, so a running OS that writes /data or a
             * persist cell touches unpowered memory and faults. */
            if (!on && (argc < 5 || !streq(argv[4], "force"))) {
                SHELL_PRINTF("nvm1 off can fault a running OS (the carved NVM "
                             "region may span it).  Add 'force' if the image "
                             "does not touch it.\n");
                return;
            }
            PWRCTRL->MEMPWREN_b.PWRENNVM1 = (uint32_t)on;
        } else if (streq(argv[2], "rom")) {
            /* GUARDED, AND THIS ONE COST A BENCH RECOVERY.  MRAM writes on this
             * part are BOOTROM-MEDIATED, and the OS writes NVM on its own
             * (boot counter, persist cells, /data).  With the ROM powered down
             * the next write faulted, the board entered a ~12 mA fault loop,
             * and SWD attach then failed 8 times in a row -- only a power cycle
             * recovered it.  The J-Link's own attach/reset hooks are suspected
             * to need the ROM too.  Worth just 14 uA measured: the least
             * valuable and most dangerous switch here. */
            if (!on && (argc < 5 || !streq(argv[4], "force"))) {
                SHELL_PRINTF("rom off breaks bootrom-mediated MRAM writes and "
                             "has wedged this board (fault loop + SWD attach "
                             "failure, power cycle to recover).  Worth ~14 uA. "
                             "Add 'force' only in an image that never writes "
                             "NVM.\n");
                return;
            }
            PWRCTRL->MEMPWREN_b.PWRENROM = (uint32_t)on;
        } else if (streq(argv[2], "ssram")) {
            if (!on && (argc < 5 || !streq(argv[4], "force"))) {
                SHELL_PRINTF("ssram off LOSES the tier arena (1 MB lives "
                             "there in this build).  'power dev ssram off "
                             "force', then reboot before trusting /data-tier "
                             "state.\n");
                return;
            }
            if (on) {
                PWRCTRL->SSRAMPWREN = 0x7u;
                PWRCTRL->SSRAMRETCFG |= (0x7u << 3);   /* SSRAMACTMCU back */
            } else {
                /* Clear the ACT force first or PWREN=0 is a CONTESTED request:
                 * measured, that state drew 1.25 mA MORE and later
                 * fault-rebooted the board. */
                PWRCTRL->SSRAMRETCFG &= ~((0x7u << 3) | (0x7u << 9)
                                          | (0x7u << 12));
                PWRCTRL->SSRAMPWREN = 0x0u;
            }
        } else if (streq(argv[2], "trc")) {
            /* The measurement's own footprint: the DWT clock oracle set
             * DEMCR.TRCENA and left it on.  Make that releasable too. */
            volatile uint32_t *demcr = (volatile uint32_t *)0xE000EDFCUL;
            volatile uint32_t *dwtcr = (volatile uint32_t *)0xE0001000UL;
            if (on) {
                *demcr |= (1UL << 24);
                *dwtcr |= 1UL;
            } else {
                *dwtcr &= ~1UL;
                *demcr &= ~(1UL << 24);
            }
        } else {
            SHELL_PRINTF("unknown domain '%s'\n", argv[2]);
            return;
        }
        {
            uint32_t spin = 200000u;   /* domains ack in STATUS, not instantly */
            while (spin-- != 0u) { __asm__ volatile ("nop"); }
        }
        SHELL_PRINTF("dev %s %s: devpwr=%lx/%lx mem=%lx/%lx ssram=%lx/%lx\n",
                     argv[2], on ? "on" : "off",
                     (unsigned long)PWRCTRL->DEVPWREN,
                     (unsigned long)PWRCTRL->DEVPWRSTATUS,
                     (unsigned long)PWRCTRL->MEMPWREN,
                     (unsigned long)PWRCTRL->MEMPWRSTATUS,
                     (unsigned long)PWRCTRL->SSRAMPWREN,
                     (unsigned long)PWRCTRL->SSRAMPWRST);
        return;
    }
    if (streq(argv[1], "buck")) {
        /* The Apollo counterpart of `power dcdc`.  Enable-only: the vendor
         * sequence hands the load from the LDOs to the buck, and there is no
         * validated reverse path here, so this deliberately does not pretend to
         * offer one -- a reboot returns to LDO. */
        int rc;
        SHELL_PRINTF("buck before: VRSTATUS=%lx (SIMOBUCKST=%lu, 3=ACT)\n",
                     (unsigned long)PWRCTRL->VRSTATUS,
                     (unsigned long)((PWRCTRL->VRSTATUS >> 4) & 3u));
        rc = tiku_cpu_freq_ambiq_simobuck_enable();
        SHELL_PRINTF("buck enable rc=%d  VRSTATUS=%lx (SIMOBUCKST=%lu)\n", rc,
                     (unsigned long)PWRCTRL->VRSTATUS,
                     (unsigned long)((PWRCTRL->VRSTATUS >> 4) & 3u));
        SHELL_PRINTF("core still %lu Hz\n", tiku_ambiq_cpu_hz_measure());
        return;
    }
    if (streq(argv[1], "clock")) {
        unsigned long hz = tiku_ambiq_cpu_hz_measure();
        SHELL_PRINTF("core measured %lu Hz (DWT cycle counter timed against "
                     "the always-on STIMER)%s\n", hz,
                     hz ? "" : " -- REFUSED: DWT unavailable or wrapped");
        return;
    }
    if (streq(argv[1], "cache") && argc >= 3) {
        int on = parse_on_off(argv[2]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power cache on|off\n");
            return;
        }
        tiku_ambiq_cache_set(on);
        SHELL_PRINTF("cache: %s\n", tiku_ambiq_cache_enabled() ? "on" : "off");
        return;
    }
#if (TIKU_DRV_NOR_ENABLE + 0)
    if (streq(argv[1], "nor")) {
        /* N1/N2 bring-up and gates for the board's 8 MB octal NOR (U12).
         *
         *   power nor id [octal]  serial bring-up + identity; "octal" also
         *                         switches to octal DDR and re-verifies
         *   power nor fault       the same, with D0 stolen -- the guard
         *                         must ERROR rather than invent an answer
         *   power nor gate        the gate only an NVM can pass: erase,
         *                         program, verify, and report the stamp
         *                         that a later power cycle must still find
         *   power nor verify      re-read that stamp WITHOUT writing --
         *                         run it after a reboot or a load-switch
         *                         cycle to prove persistence
         *   power nor off | on    load switch: true zero / restore
         *   power nor erases      how many erases this boot has spent
         */
        tiku_nor_id_t id;
        tiku_nor_err_t rc;

        if (argc >= 3 && streq(argv[2], "regs")) {
            uint32_t g[12]; unsigned k7;
            static const char *const nm[] = {
                "devpwrstatus","ioclkctrl","dev0cfg","dev0cfg1","dev0ddr",
                "dev0xip","dev0instr","padouten","mspicfg","ctrl","intstat",
                "rxentries" };
            tiku_nor_regs(g, 12u);
            SHELL_PRINTF("nor regs (read back):\n");
            for (k7 = 0u; k7 < 12u; k7++) {
                SHELL_PRINTF("  %-13s %08lx\n", nm[k7], (unsigned long)g[k7]);
            }
            return;
        }
        if (argc >= 4 && streq(argv[2], "ls") && streq(argv[3], "really")) {
            SHELL_PRINTF("nor ls: refused -- driving GP208 wedged the board\n"
                         "  (SWD dead at every speed/reset type; needed a\n"
                         "   physical power cycle).  Polarity and load are\n"
                         "   unestablished; confirm on a scope first.\n");
            return;
        }
        if (argc >= 4 && streq(argv[2], "ls") && 0) {
            /* Settle the load-switch polarity by experiment: drive the pad
             * each way (and high-Z) and see which state lets identity read. */
            int lv = streq(argv[3], "z") ? -1 : (argv[3][0] == '1' ? 1 : 0);
            tiku_nor_ls_set(lv);
            rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
            if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
            SHELL_PRINTF("nor ls=%s: mfr %02x type %02x cap %02x -- %s\n",
                         (lv < 0) ? "hi-Z" : (lv ? "high" : "low"),
                         id.mfr, id.type, id.capacity, nor_errname(rc));
            return;
        }
        if (argc >= 3 && streq(argv[2], "erases")) {
            SHELL_PRINTF("nor: %lu erases performed this boot (scratch"
                         " sector %08lx)\n",
                         (unsigned long)tiku_nor_erase_count(),
                         (unsigned long)TIKU_NOR_SCRATCH_ADDR);
            return;
        }
        if (argc >= 3 && streq(argv[2], "off")) {
            tiku_nor_deinit();
            tiku_nor_power(0);
            SHELL_PRINTF("nor: load switch OFF -- VDD_FLASH at true zero,"
                         " contents retained\n");
            return;
        }
        if (argc >= 3 && streq(argv[2], "verify")) {
            /* Persistence check with NO writes: read the stamp back. */
            static uint8_t rd[64];
            uint32_t i6;
            int ok6 = 1;
            rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
            if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor verify: bring-up %s\n", nor_errname(rc));
                return;
            }
            rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor verify: read %s\n", nor_errname(rc));
                return;
            }
            for (i6 = 0u; i6 < sizeof rd; i6++) {
                if (rd[i6] != (uint8_t)(0xA5u ^ i6)) { ok6 = 0; }
            }
            SHELL_PRINTF("nor verify: stamp %02x %02x %02x %02x ... -- %s\n",
                         rd[0], rd[1], rd[2], rd[3],
                         ok6 ? "INTACT (survived power loss)"
                             : "absent/modified");
            return;
        }
        if (argc >= 3 && streq(argv[2], "gate")) {
            /* erase -> program -> verify, on the scratch sector only. */
            static uint8_t wr[64], rd[64];
            uint32_t i6;
            int ok6 = 1;
            rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
            if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor gate: bring-up %s\n", nor_errname(rc));
                return;
            }
            SHELL_PRINTF("  erasing scratch sector %08lx (this spends one"
                         " erase cycle)...\n",
                         (unsigned long)TIKU_NOR_SCRATCH_ADDR);
            rc = tiku_nor_erase(TIKU_NOR_SCRATCH_ADDR, 0, 0);
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor gate: erase %s\n", nor_errname(rc));
                return;
            }
            rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
            for (i6 = 0u; i6 < sizeof rd; i6++) {
                if (rd[i6] != 0xFFu) { ok6 = 0; }
            }
            SHELL_PRINTF("  after erase: %s (erased NOR must read all ff)\n",
                         ok6 ? "all ff" : "NOT ERASED");
            for (i6 = 0u; i6 < sizeof wr; i6++) {
                wr[i6] = (uint8_t)(0xA5u ^ i6);
            }
            rc = tiku_nor_program(TIKU_NOR_SCRATCH_ADDR, wr, sizeof wr);
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor gate: program %s\n", nor_errname(rc));
                return;
            }
            rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
            ok6 = 1;
            for (i6 = 0u; i6 < sizeof rd; i6++) {
                if (rd[i6] != wr[i6]) { ok6 = 0; }
            }
            SHELL_PRINTF("nor gate: program+verify %s -- erases used %lu\n",
                         ok6 ? "bit-exact" : "MISMATCH",
                         (unsigned long)tiku_nor_erase_count());
            SHELL_PRINTF("  now: power-cycle the board, then"
                         " `power nor verify`\n");
            return;
        }
        {
            int want_octal = 0, want_fault = 0, k6;
            for (k6 = 2; k6 < argc; k6++) {
                if (streq(argv[k6], "octal")) { want_octal = 1; }
                if (streq(argv[k6], "fault"))  { want_fault = 1; }
            }
            tiku_nor_set_trace(nor_trace);
            rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
            tiku_nor_set_trace((void (*)(const char *))0);
            if (rc != TIKU_NOR_OK) {
                SHELL_PRINTF("nor init: %s\n", nor_errname(rc));
                return;
            }
            if (want_fault) { tiku_nor_fault_inject(1); }
            rc = tiku_nor_read_id(&id);
            if (want_fault) { tiku_nor_fault_inject(0); }
            SHELL_PRINTF("nor serial @%lu Hz: mfr %02x (9d=ISSI) type %02x"
                         " cap %02x status %02x nvcr6 %02x\n",
                         tiku_nor_clock_hz(), id.mfr, id.type, id.capacity,
                         id.status, id.ncr6);
            SHELL_PRINTF("  verdict: %s%s\n", nor_errname(rc),
                         want_fault ? "  (fault injected: error EXPECTED)"
                                    : "");
            if (want_octal && rc == TIKU_NOR_OK) {
                tiku_nor_set_trace(nor_trace);
                rc = tiku_nor_enter_octal(TIKU_NOR_CLK_96MHZ);
                tiku_nor_set_trace((void (*)(const char *))0);
                if (rc == TIKU_NOR_ERR_STATE) {
                    SHELL_PRINTF("nor octal: REFUSED -- non-volatile CR[6]"
                                 " is %02x, not ff; this driver does not"
                                 " write non-volatile config\n", id.ncr6);
                    return;
                }
                (void)tiku_nor_read_id(&id);
                SHELL_PRINTF("nor octal @%lu Hz: %s -- mfr %02x cap %02x"
                             " (identity re-read IN OCTAL)\n",
                             tiku_nor_clock_hz(), nor_errname(rc),
                             id.mfr, id.capacity);
            }
        }
        return;
    }
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (streq(argv[1], "psram")) {
        /* M1 bring-up verb for the board's 64 MB octal-DDR PSRAM (EVB U14).
         *
         *   power psram id [clk]   power MSPI0, reset the device, read its
         *                          mode registers and check identity
         *   power psram fault      the SAME read with D0 taken away from the
         *                          controller -- proves the error path fires
         *                          instead of returning plausible garbage
         *   power psram off        release the controller domain
         *
         * Identity before anything else, at the lowest clock, because a
         * mis-timed octal bus answers with numbers that look real. */
        unsigned clk = TIKU_PSRAM_CLK_48MHZ;
        int want_fault = (argc >= 3 && streq(argv[2], "fault"));
        int nodqs = 0;
        {   /* any trailing "nodqs" word switches the strobe off */
            int k;
            for (k = 2; k < argc; k++) {
                if (streq(argv[k], "nodqs")) { nodqs = 1; }
            }
        }
        if (argc >= 3 && streq(argv[2], "up")) {
            /* power psram up [mhz] -- the M4 lifecycle: speed + identity +
             * (at 192) timing scan + XIP map + TIKU_MEM_PSRAM tier attach. */
            unsigned row = TIKU_PSRAM_CLK_192MHZ, n3 = 0u;
            tiku_psram_err_t rc;
            if (argc >= 4) {
                const char *q3 = argv[3];
                while (*q3 >= '0' && *q3 <= '9') { n3 = n3*10u + (unsigned)(*q3++ - '0'); }
                if (n3 && n3 < 96u)        { row = TIKU_PSRAM_CLK_48MHZ; }
                else if (n3 && n3 < 125u)  { row = TIKU_PSRAM_CLK_96MHZ; }
                else if (n3 && n3 < 192u)  { row = TIKU_PSRAM_CLK_125MHZ; }
            }
            rc = tiku_psram_up(row, (row == TIKU_PSRAM_CLK_192MHZ) ? 1 : 0);
            SHELL_PRINTF("psram up: %s -- io %lu Hz, tap %u, tier %s,"
                         " 64 MB at 0x%08lx\n",
                         (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED",
                         tiku_psram_clock_hz(), tiku_psram_tap(),
                         (rc == TIKU_PSRAM_OK) ? "attached" : "no",
                         (unsigned long)TIKU_PSRAM_XIP_BASE);
            return;
        }
        if (argc >= 3 && streq(argv[2], "down")) {
            int force = (argc >= 4 && streq(argv[3], "force"));
            tiku_psram_err_t rc = tiku_psram_down(force);
            SHELL_PRINTF("psram down: %s%s\n",
                         (rc == TIKU_PSRAM_OK) ? "ok (contents gone)"
                         : "REFUSED -- tier has live allocations",
                         (rc != TIKU_PSRAM_OK) ? " (use: down force)" : "");
            return;
        }
        if (argc >= 3 && streq(argv[2], "sleep")) {
            tiku_psram_err_t rc = tiku_psram_halfsleep();
            SHELL_PRINTF("psram sleep: %s (contents retained on"
                         " self-refresh; access refused until wake)\n",
                         (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED");
            return;
        }
        if (argc >= 3 && streq(argv[2], "wake")) {
            tiku_psram_err_t rc = tiku_psram_wake();
            SHELL_PRINTF("psram wake: %s\n",
                         (rc == TIKU_PSRAM_OK) ? "ok -- identity re-verified"
                                               : "FAILED");
            if (rc == TIKU_PSRAM_OK) {
                (void)tiku_psram_xip_enable(1);   /* restore the mapping */
            }
            return;
        }
        if (argc >= 3 && streq(argv[2], "tier")) {
            /* The M4 acceptance gate: carve 32 MB from the PSRAM tier, fill
             * through the aperture, checksum it back, and report -- then
             * survive a sleep/wake with the SAME checksum. */
            static tiku_arena_t ar;
            uint8_t *p2;
            uint32_t i3, sum1 = 0u, sum2 = 0u;
            const uint32_t N = 32u * 1024u * 1024u;
            if (tiku_tier_arena_create(&ar, TIKU_MEM_PSRAM, N, 42u)
                    != TIKU_MEM_OK) {
                SHELL_PRINTF("tier: arena create failed (is psram up?)\n");
                return;
            }
            p2 = (uint8_t *)tiku_arena_alloc(&ar, N - 64u);
            if (!p2) {
                SHELL_PRINTF("tier: alloc failed\n");
                return;
            }
            SHELL_PRINTF("tier: 32 MB arena, buf %08lx -- filling\n",
                         (unsigned long)(uintptr_t)p2);
            for (i3 = 0u; i3 < N - 64u; i3 += 4u) {
                *(volatile uint32_t *)(p2 + i3) = i3 * 2654435761u;
                if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
            }
            tiku_cpu_dcache_clean(p2, N - 64u);
            tiku_cpu_dcache_invalidate(p2, N - 64u);
            for (i3 = 0u; i3 < N - 64u; i3 += 4096u) {
                sum1 += *(volatile uint32_t *)(p2 + i3);
                if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
            }
            SHELL_PRINTF("tier: filled, sparse checksum %08lx --"
                         " sleeping...\n", (unsigned long)sum1);
            if (tiku_psram_halfsleep() != TIKU_PSRAM_OK) {
                SHELL_PRINTF("tier: sleep failed\n");
                return;
            }
            {   /* hold half sleep long enough to mean something */
                uint32_t ms3;
                for (ms3 = 0u; ms3 < 1500u; ms3++) {
                    tiku_cpu_ambiq_delay_us(1000u);
                    if ((ms3 & 63u) == 0u) { tiku_hang_checkin(); }
                }
            }
            if (tiku_psram_wake() != TIKU_PSRAM_OK) {
                SHELL_PRINTF("tier: wake failed\n");
                return;
            }
            (void)tiku_psram_xip_enable(1);
            tiku_cpu_dcache_invalidate(p2, N - 64u);
            for (i3 = 0u; i3 < N - 64u; i3 += 4096u) {
                sum2 += *(volatile uint32_t *)(p2 + i3);
                if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
            }
            SHELL_PRINTF("tier: after 1.5 s half sleep, checksum %08lx --"
                         " %s\n", (unsigned long)sum2,
                         (sum1 == sum2) ? "RETAINED, gate PASSES"
                                        : "LOST -- gate FAILS");
            return;
        }
        if (argc >= 3 && streq(argv[2], "speed") && argc >= 4) {
            /* power psram speed <48|96|125|192> -- program device latencies
             * and reconfigure the controller, then prove it with the
             * identity gate at the new clock. */
            unsigned n = 0u; const char *q = argv[3];
            unsigned row;
            tiku_psram_id_t id; tiku_psram_err_t rc;
            while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
            row = (n >= 192u) ? TIKU_PSRAM_CLK_192MHZ
                : (n >= 125u) ? TIKU_PSRAM_CLK_125MHZ
                : (n >= 96u)  ? TIKU_PSRAM_CLK_96MHZ
                              : TIKU_PSRAM_CLK_48MHZ;
            rc = tiku_psram_set_speed(row);
            if (rc != TIKU_PSRAM_OK) {
                SHELL_PRINTF("speed: set failed (%d)\n", (int)rc);
                return;
            }
            rc = tiku_psram_read_id(&id);
            SHELL_PRINTF("speed: io clock %lu Hz, identity %s"
                         " (vendor %02x density %x)\n",
                         tiku_psram_clock_hz(),
                         (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED",
                         id.vendor_id, id.density_code);
            return;
        }
        if (argc >= 3 && streq(argv[2], "scan3")) {
            /* power psram scan3 [mhz] -- the real M2 timing scan at the live
             * (or requested) clock.  The output must show failing taps
             * bracketing the window, or the scan proved nothing. */
            uint32_t mask = 0u; unsigned center = 0u, width, t;
            if (argc >= 5) { }
            if (argc >= 4) {
                unsigned n = 0u; const char *q = argv[3];
                while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
                if (n) {
                    unsigned row = (n >= 192u) ? TIKU_PSRAM_CLK_192MHZ
                                 : (n >= 125u) ? TIKU_PSRAM_CLK_125MHZ
                                 : (n >= 96u)  ? TIKU_PSRAM_CLK_96MHZ
                                               : TIKU_PSRAM_CLK_48MHZ;
                    if (tiku_psram_set_speed(row) != TIKU_PSRAM_OK) {
                        SHELL_PRINTF("scan3: speed set failed\n");
                        return;
                    }
                }
            }
            width = tiku_psram_timing_scan(&mask, &center);
            SHELL_PRINTF("timing scan @ %lu Hz: taps 0..31 = ",
                         tiku_psram_clock_hz());
            for (t = 0u; t < 32u; t++) {
                SHELL_PRINTF("%c", (mask & (1u << t)) ? 'P' : '.');
            }
            SHELL_PRINTF("\n  widest window %lu taps, shipped tap %u%s\n",
                         (unsigned long)width, center,
                         (width == 32u) ? "  [WARNING: passes everywhere --"
                                          " not a proven scan at this clock]"
                                        : "");
            return;
        }
        if (argc >= 3 && streq(argv[2], "mem")) {
            /* power psram mem -- the M2 acceptance gate: 64 KB address-derived
             * pattern across low + high regions, bit-exact, via PIO. */
            static uint8_t wr[1024], rd[1024];
            static uint32_t xorh[256];
            static const uint32_t base[2] = { 0x00010000u, 0x03F00000u };
            uint32_t r, off, i, errs = 0u, sum = 0u;
            for (i = 0u; i < 256u; i++) { xorh[i] = 0u; }
            for (r = 0u; r < 2u; r++) {
                for (off = 0u; off < 32768u; off += (uint32_t)(sizeof wr)) {
                    for (i = 0u; i < (uint32_t)(sizeof wr); i++) {
                        uint32_t a = base[r] + off + i;
                        wr[i] = (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
                    }
                    if (tiku_psram_mem_write(base[r] + off, wr,
                            (uint32_t)(sizeof wr)) != TIKU_PSRAM_OK) {
                        SHELL_PRINTF("mem: write fail @%lx\n",
                                     (unsigned long)(base[r] + off));
                        return;
                    }
                }
                for (off = 0u; off < 32768u; off += (uint32_t)(sizeof rd)) {
                    if (tiku_psram_mem_read(base[r] + off, rd,
                            (uint32_t)(sizeof rd)) != TIKU_PSRAM_OK) {
                        SHELL_PRINTF("mem: read fail @%lx\n",
                                     (unsigned long)(base[r] + off));
                        return;
                    }
                    for (i = 0u; i < (uint32_t)(sizeof rd); i++) {
                        uint32_t a = base[r] + off + i;
                        uint8_t e = (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
                        if (rd[i] != e) {
                            if (errs < 6u) {
                                SHELL_PRINTF("    @%08lx want %02x got %02x\n",
                                             (unsigned long)a, e, rd[i]);
                            }
                            errs++;
                            xorh[(uint8_t)(rd[i] ^ e)]++;
                        }
                        sum += rd[i];
                    }
                    tiku_hang_checkin();
                }
            }
            for (i = 0u; i < 256u; i++) {
                if (xorh[i] != 0u) {
                    SHELL_PRINTF("    xor %02lx : %lu times\n",
                                 (unsigned long)i, (unsigned long)xorh[i]);
                }
            }
            SHELL_PRINTF("mem: 64 KB x2 regions @ %lu Hz: %lu errors,"
                         " checksum %08lx -- %s\n",
                         tiku_psram_clock_hz(), (unsigned long)errs,
                         (unsigned long)sum,
                         errs ? "FAIL" : "bit-exact");
            return;
        }
        if (argc >= 3 && streq(argv[2], "retain")) {
            /* power psram retain <ms> -- the refresh-integrity gate: write a
             * pattern, WAIT (self-refresh must carry it), verify bit-exact.
             * Guards every burst/pause tuning against silent decay. */
            static uint8_t wr2[1024];
            uint32_t ms2 = 500u, off2, i2, errs2 = 0u;
            if (argc >= 4) {
                unsigned n2 = 0u; const char *q2 = argv[3];
                while (*q2 >= '0' && *q2 <= '9') { n2 = n2*10u + (unsigned)(*q2++ - '0'); }
                if (n2) { ms2 = n2; }
            }
            for (off2 = 0u; off2 < 65536u; off2 += (uint32_t)(sizeof wr2)) {
                for (i2 = 0u; i2 < (uint32_t)(sizeof wr2); i2++) {
                    uint32_t a2 = 0x00200000u + off2 + i2;
                    wr2[i2] = (uint8_t)(a2 ^ (a2 >> 8) ^ 0x3Cu);
                }
                if (tiku_psram_mem_write(0x00200000u + off2, wr2,
                        (uint32_t)(sizeof wr2)) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("retain: write fail\n");
                    return;
                }
            }
            for (i2 = 0u; i2 < ms2; i2++) {
                tiku_cpu_ambiq_delay_us(1000u);
                if ((i2 & 63u) == 0u) { tiku_hang_checkin(); }
            }
            for (off2 = 0u; off2 < 65536u; off2 += (uint32_t)(sizeof wr2)) {
                if (tiku_psram_mem_read(0x00200000u + off2, wr2,
                        (uint32_t)(sizeof wr2)) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("retain: read fail\n");
                    return;
                }
                for (i2 = 0u; i2 < (uint32_t)(sizeof wr2); i2++) {
                    uint32_t a2 = 0x00200000u + off2 + i2;
                    if (wr2[i2] != (uint8_t)(a2 ^ (a2 >> 8) ^ 0x3Cu)) { errs2++; }
                }
                tiku_hang_checkin();
            }
            SHELL_PRINTF("retain: 64 KB held %lu ms: %lu errors -- %s\n",
                         (unsigned long)ms2, (unsigned long)errs2,
                         errs2 ? "FAIL (refresh starved?)" : "bit-exact");
            return;
        }
        if (argc >= 3 && streq(argv[2], "dbb") && argc >= 4) {
            /* power psram dbb <code> -- DMA boundary A/B: 6=1K 7=2K 8=4K
             * 9=8K 10=16K.  Longer bursts amortize the fixed per-row tax;
             * the RISK is CE-low time vs the die's refresh (tCEM), which is
             * exactly what the retain gate after each setting must clear. */
            unsigned n5 = 0u; const char *q5 = argv[3];
            while (*q5 >= '0' && *q5 <= '9') { n5 = n5*10u + (unsigned)(*q5++ - '0'); }
            MSPI0->DEV0BOUNDARY_b.DMABOUND0 = n5;
            SHELL_PRINTF("dbb: DMABOUND0 = %lu\n",
                         (unsigned long)MSPI0->DEV0BOUNDARY_b.DMABOUND0);
            return;
        }
        if (argc >= 3 && streq(argv[2], "dtl") && argc >= 4) {
            /* power psram dtl <n> -- runtime DMATIMELIMIT A/B (the per-KB
             * plateau hunt).  Integrity gates (mem/retain) MUST follow any
             * change before a number is believed. */
            unsigned n4 = 0u; const char *q4 = argv[3];
            while (*q4 >= '0' && *q4 <= '9') { n4 = n4*10u + (unsigned)(*q4++ - '0'); }
            MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0 = n4;
            SHELL_PRINTF("dtl: DMATIMELIMIT0 = %lu\n",
                         (unsigned long)MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0);
            return;
        }
        if (argc >= 3 && streq(argv[2], "bench")) {
            /* power psram bench -- M3: DWT-timed bandwidth through each path.
             * Work is the denominator: bytes moved + checksum per leg. */
            extern void tiku_psram_bench_run(void);
            tiku_psram_bench_run();
            return;
        }
        if (argc >= 3 && streq(argv[2], "scan2")) {
            /* Hunt the RX capture point in no-DQS mode.  The device is
             * proven alive (bit-bang: MR1 0x8d, MR2 0xde), so any cell that
             * reads 8d is the capture configuration this silicon wants. */
            unsigned rn, rc2, rs, ta;
            SHELL_PRINTF("rx capture sweep (no DQS), target MR1=8d:\n");
            for (rn = 0u; rn <= 1u; rn++) {
             for (rc2 = 0u; rc2 <= 1u; rc2++) {
              for (rs = 0u; rs <= 3u; rs++) {
                for (ta = 8u; ta <= 18u; ta += 1u) {
                    tiku_psram_id_t id;
                    tiku_psram_deinit();
                    tiku_psram_set_dqs(0);
                    tiku_psram_set_rx(rn, rc2, rs);
                    tiku_psram_set_turnaround(ta);
                    if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ)
                            != TIKU_PSRAM_OK) { continue; }
                    (void)tiku_psram_read_id(&id);
                    if (id.mr1 != 0x42u && id.mr1 != 0x00u) {
                        SHELL_PRINTF("  rxneg %u rxcap %u rxsmp %u ta %2u:"
                                     " MR1 %02x MR2 %02x%s\n",
                                     rn, rc2, rs, ta, id.mr1, id.mr2,
                                     (id.mr1 == 0x8Du) ? "  <== TARGET" : "");
                    }
                }
              }
             }
            }
            SHELL_PRINTF("sweep done (silent cells read 42 or 00)\n");
            tiku_psram_set_rx(0u, 0u, 1u);
            tiku_psram_set_turnaround(0u);
            tiku_psram_set_dqs(1);
            return;
        }
        if (argc >= 3 && streq(argv[2], "txtest")) {
            /* Does CONTROLLER TX reach the device at all?  The device is
             * proven alive over GPIO, so: bit-bang-read MR0, write MR0
             * through the CONTROLLER (drive-strength bits flipped), then
             * bit-bang-read it again.  A change proves controller TX end to
             * end with no dependence on controller RX; no change means the
             * controller's bus never reaches the part and every RX theory
             * is moot. */
            static uint8_t before[16], after[16];
            unsigned k; uint8_t b0 = 0u, a0 = 0u;
            uint32_t wr;
            tiku_psram_deinit();
            tiku_psram_bitbang_reg(0u, before, 16u);
            for (k = 8u; k < 16u; k += 2u) {   /* steady repeat region */
                if (before[k] == before[k + 2u < 16u ? k + 2u : k]) {
                    b0 = before[k]; break;
                }
            }
            tiku_psram_set_dqs(0);
            if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ) != TIKU_PSRAM_OK) {
                SHELL_PRINTF("txtest: init failed\n");
                return;
            }
            wr = (uint32_t)(b0 ^ 0x01u);       /* flip DS bit0 */
            (void)tiku_psram_reg_write(0u, wr);
            tiku_psram_deinit();
            tiku_psram_bitbang_reg(0u, after, 16u);
            for (k = 8u; k < 16u; k += 2u) {
                if (after[k] == after[k + 2u < 16u ? k + 2u : k]) {
                    a0 = after[k]; break;
                }
            }
            SHELL_PRINTF("txtest: MR0 before:");
            for (k = 0u; k < 16u; k++) { SHELL_PRINTF(" %02x", before[k]); }
            SHELL_PRINTF("\n        MR0 after :");
            for (k = 0u; k < 16u; k++) { SHELL_PRINTF(" %02x", after[k]); }
            SHELL_PRINTF("\n        wrote %02lx: %s\n", (unsigned long)wr,
                         (a0 == (uint8_t)wr) ? "CONTROLLER TX REACHES DEVICE"
                         : (a0 == b0) ? "no change -- controller TX never lands"
                                      : "changed to something ELSE (partial)");
            return;
        }
        if (argc >= 3 && streq(argv[2], "arb")) {
            /* THE ARBITER.  Controller-write a distinctive 64 B pattern at
             * 0x4000, then bit-bang-read 0x3800 / 0x4000 / 0x4800 and print
             * the streams.  Wherever the pattern physically shows up names
             * the guilty path: at 0x4000 = write correct (read path adds
             * 0x800); at 0x4800 = write path adds 0x800; at 0x3800 = write
             * path subtracts. */
            static uint8_t pat[64]; static uint8_t ed[48];
            uint32_t i; unsigned k2;
            static const uint32_t probe[3] = { 0x3800u, 0x4000u, 0x4800u };
            tiku_psram_err_t rc;
            unsigned row = TIKU_PSRAM_CLK_48MHZ;
            if (argc >= 4) {
                unsigned n2 = 0u; const char *q2 = argv[3];
                while (*q2 >= '0' && *q2 <= '9') { n2 = n2*10u + (unsigned)(*q2++ - '0'); }
                if (n2 >= 192u)      { row = TIKU_PSRAM_CLK_192MHZ; }
                else if (n2 >= 125u) { row = TIKU_PSRAM_CLK_125MHZ; }
                else if (n2 >= 96u)  { row = TIKU_PSRAM_CLK_96MHZ; }
            }
            rc = tiku_psram_set_speed(row);
            if (rc != TIKU_PSRAM_OK) {
                SHELL_PRINTF("arb: speed failed\n");
                return;
            }
            for (i = 0u; i < 64u; i++) { pat[i] = (uint8_t)(0xB0u + i); }
            if (tiku_psram_mem_write(0x4000u, pat, 64u) != TIKU_PSRAM_OK) {
                SHELL_PRINTF("arb: write failed\n");
                return;
            }
            tiku_psram_deinit();
            for (k2 = 0u; k2 < 3u; k2++) {
                tiku_psram_bitbang_mem(probe[k2], ed, 48u);
                SHELL_PRINTF("  bb @%04lx:", (unsigned long)probe[k2]);
                for (i = 0u; i < 48u; i++) { SHELL_PRINTF(" %02x", ed[i]); }
                SHELL_PRINTF("\n");
            }
            SHELL_PRINTF("  (wrote b0,b1,b2.. at 4000 via controller;"
                         " find it above)\n");
            return;
        }
        if (argc >= 3 && streq(argv[2], "bb")) {
            /* Ground truth: the same identity read, bit-banged on GPIO with
             * the MSPI controller out of the picture entirely. */
            static uint8_t edges[32];
            unsigned k;
            tiku_psram_deinit();     /* controller off the pads first */
            tiku_psram_bitbang_id(edges, 32u);
            SHELL_PRINTF("bitbang MR1 read, D0-7 after each edge:\n ");
            for (k = 0u; k < 32u; k++) {
                SHELL_PRINTF(" %02x", edges[k]);
                if ((k & 7u) == 7u) { SHELL_PRINTF("\n "); }
            }
            SHELL_PRINTF("(0d/8d somewhere = device alive in octal;"
                         " 00/ff throughout = no answer)\n");
            return;
        }
        if (argc >= 3 && streq(argv[2], "scan")) {
            /* Sweep the read window and print the identity register for each
             * setting.  The right answer is the one that reads 0x0D in the
             * low five bits -- and the sweep must SHOW the wrong settings
             * either side of it, or it has not proven anything. */
            unsigned ta;
            SHELL_PRINTF("turnaround sweep (%s), want MR1 vendor 0d:\n",
                         nodqs ? "no DQS" : "DQS");
            for (ta = 4u; ta <= 30u; ta += 1u) {
                tiku_psram_id_t id;
                tiku_psram_err_t rc;
                tiku_psram_deinit();
                tiku_psram_set_dqs(nodqs ? 0 : 1);
                tiku_psram_set_turnaround(ta);
                if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("  ta %2u: init failed\n", ta);
                    continue;
                }
                rc = tiku_psram_read_id(&id);
                SHELL_PRINTF("  ta %2u: MR1 %02x MR2 %02x vendor %02x%s\n",
                             ta, id.mr1, id.mr2, id.vendor_id,
                             (rc == TIKU_PSRAM_OK) ? "   <== MATCH" : "");
            }
            tiku_psram_set_turnaround(0u);
            return;
        }
        if (argc >= 3 && streq(argv[2], "cmd")) {
            uint32_t c = 0u;
            tiku_psram_err_t rc;
            static const char *const en2[] = { "ok", "POWER", "CLOCK",
                                              "TIMEOUT", "ID", "ARG" };
            tiku_psram_set_dqs(nodqs ? 0 : 1);
            rc = tiku_psram_init(clk);
            if (rc != TIKU_PSRAM_OK) {
                SHELL_PRINTF("psram init: %s\n", en2[rc]);
                return;
            }
            rc = tiku_psram_cmd_probe(&c);
            SHELL_PRINTF("psram cmd (no data phase): %s  ctrl %08lx"
                         " (bit1 STATUS=done, bit2 BUSY)\n",
                         en2[rc], (unsigned long)c);
            return;
        }

        if (argc >= 3 && streq(argv[2], "regs")) {
            tiku_psram_regs_t g;
            tiku_psram_regs(&g);
            SHELL_PRINTF("psram regs (read back, not assumed):\n");
            SHELL_PRINTF("  devpwrstatus %08lx  clkgen.misc %08lx  ioclkctrl %08lx\n",
                         (unsigned long)g.devpwrstatus,
                         (unsigned long)g.clkgen_misc,
                         (unsigned long)g.mspiioclkctrl);
            SHELL_PRINTF("  dev0cfg %08lx cfg1 %08lx ddr %08lx xip %08lx instr %08lx\n",
                         (unsigned long)g.dev0cfg, (unsigned long)g.dev0cfg1,
                         (unsigned long)g.dev0ddr, (unsigned long)g.dev0xip,
                         (unsigned long)g.dev0instr);
            SHELL_PRINTF("  padouten %08lx mspicfg %08lx ctrl %08lx intstat %08lx\n",
                         (unsigned long)g.padouten, (unsigned long)g.mspicfg,
                         (unsigned long)g.ctrl, (unsigned long)g.intstat);
            SHELL_PRINTF("  rx %lu tx %lu entries\n",
                         (unsigned long)g.rxentries, (unsigned long)g.txentries);
            SHELL_PRINTF("  during last xfer: ctrl@start %08lx tx@write %lu"
                         " -> tx %lu ctrl %08lx intstat %08lx\n",
                         (unsigned long)g.dbg_ctrl_after_start,
                         (unsigned long)g.dbg_tx_after_write,
                         (unsigned long)g.dbg_tx_settled,
                         (unsigned long)g.dbg_ctrl_settled,
                         (unsigned long)g.dbg_intstat);
            return;
        }
        if (argc >= 3 && streq(argv[2], "off")) {
            tiku_psram_deinit();
            SHELL_PRINTF("psram: MSPI0 domain released (powered %d)\n",
                         tiku_psram_powered());
            return;
        }
        if (argc >= 4) {
            const char *q = argv[3]; unsigned n = 0u;
            while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
            if (n >= 192u)      { clk = TIKU_PSRAM_CLK_192MHZ; }
            else if (n >= 125u) { clk = TIKU_PSRAM_CLK_125MHZ; }
            else if (n >= 96u)  { clk = TIKU_PSRAM_CLK_96MHZ; }
        }
        {
            tiku_psram_id_t id;
            tiku_psram_err_t rc;
            tiku_psram_set_trace(psram_trace);
            tiku_psram_set_dqs(nodqs ? 0 : 1);
            rc = tiku_psram_init(clk);
            tiku_psram_set_trace((void (*)(const char *))0);
            static const char *const en[] = { "ok", "POWER", "CLOCK",
                                              "TIMEOUT", "ID", "ARG" };
            if (rc != TIKU_PSRAM_OK) {
                SHELL_PRINTF("psram init: %s\n", en[rc]);
                return;
            }
            SHELL_PRINTF("psram: MSPI0 up, io clock %lu Hz, powered %d\n",
                         tiku_psram_clock_hz(), tiku_psram_powered());
            if (want_fault) {
                tiku_psram_fault_inject(1);
            }
            rc = tiku_psram_read_id(&id);
            if (want_fault) {
                tiku_psram_fault_inject(0);
            }
            /* Raw bytes ALWAYS printed, verdict separately: the caller needs
             * the numbers to tell a dead bus from a wrong part. */
            SHELL_PRINTF("  MR0 %02x MR1 %02x MR2 %02x MR3 %02x MR4 %02x MR8 %02x\n",
                         id.mr0, id.mr1, id.mr2, id.mr3, id.mr4, id.mr8);
            SHELL_PRINTF("  vendor %02x (0d=AP) density %x (6=512Mb) gen %u die %s\n",
                         id.vendor_id, id.density_code,
                         (unsigned)(id.generation == 0u ? 5u : id.generation + 1u),
                         id.good_die ? "pass" : "BAD");
            SHELL_PRINTF("  size %lu bytes -- verdict: %s%s\n",
                         (unsigned long)id.size_bytes, en[rc],
                         want_fault ? "  (fault injected: error EXPECTED)" : "");
        }
        return;
    }
#endif
    if (streq(argv[1], "stimer")) {
        /* Timebase health: current rate, is the counter visibly counting, and
         * does the tickless guard accept or refuse a stretch.
         *
         * `power stimer kill` is the FAILURE-INJECTION form: it deliberately
         * selects NOCLK, shows the guard REFUSE a stretch on the dead clock,
         * then restores the crystal and shows recovery -- all in one verb, so
         * the board can never be left parked on a dead timebase.  A guard
         * that has never been seen to fire is a guard that may not exist. */
        int kill = (argc >= 3 && streq(argv[2], "kill"));
        int pass;
        for (pass = 0; pass < (kill ? 2 : 1); pass++) {
            uint32_t rate, c0, c1, spin_n = 4000000u;
            int ok;
            if (kill && pass == 0) {
                STIMER->STCFG = (STIMER->STCFG & ~0xFu);      /* NOCLK */
            }
            if (kill && pass == 1) {
                STIMER->STCFG = (STIMER->STCFG & ~0xFu) | 3u; /* XTAL  */
            }
            rate = tiku_ambiq_stimer_rate_hz();
            c0 = tiku_ambiq_stimer_now();
            while (tiku_ambiq_stimer_now() == c0 && --spin_n != 0u) { }
            c1 = tiku_ambiq_stimer_now();
            __asm__ volatile ("cpsid i" ::: "memory");
            ok = tiku_clock_tickless_begin(2u);
            tiku_clock_tickless_end();
            __asm__ volatile ("cpsie i" ::: "memory");
            SHELL_PRINTF("stimer%s rate %lu Hz counting %s stretch %s\n",
                         kill ? (pass ? " [restored]" : " [killed]") : "",
                         (unsigned long)rate,
                         (c1 != c0) ? "yes" : "NO (frozen)",
                         ok ? "accepted" : "REFUSED (guard)");
        }
        return;
    }
    if (streq(argv[1], "reclock") && argc >= 3) {
        /* power reclock lfrc|xtal -- exercise the deep-sleep timebase switch
         * from the shell, so the verified-switch path is provable without a
         * deep-sleep window around it. */
        int lf = streq(argv[2], "lfrc");
        uint32_t hz = tiku_ambiq_stimer_reclock(lf ? 1 : 0);
        SHELL_PRINTF("reclock %s -> %lu Hz%s\n", lf ? "lfrc" : "xtal",
                     (unsigned long)hz,
                     hz ? "" : " (FAILED, timebase left on XTAL)");
        return;
    }
    if ((streq(argv[1], "idle") || streq(argv[1], "spin")) && argc >= 3) {
        int spin = streq(argv[1], "spin");
        unsigned flags = 0u, i;
        uint32_t ms = 0u, us;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        for (i = 3u; i < (unsigned)argc; i++) {
            if (streq(argv[i], "deep")) { flags |= TIKU_AMBIQ_SLEEP_DEEP; }
            if (streq(argv[i], "uart")) { flags |= TIKU_AMBIQ_SLEEP_STOP_UART; }
            if (streq(argv[i], "tick")) { flags |= TIKU_AMBIQ_SLEEP_STOP_TICK; }
            if (streq(argv[i], "dbg"))  { flags |= TIKU_AMBIQ_SLEEP_DBGLOCK; }
            if (streq(argv[i], "lfrc")) { flags |= TIKU_AMBIQ_SLEEP_LFRC; }
        }
        if (ms == 0u) {
            SHELL_PRINTF("Usage: power %s <ms> [deep|uart|tick|dbg]\n", argv[1]);
            return;
        }
        /* EVERY flag printed -- this line is the provenance record. */
        SHELL_PRINTF("%s %lu ms flags%s%s%s%s -- starting\n", argv[1],
                     (unsigned long)ms,
                     (flags & TIKU_AMBIQ_SLEEP_STOP_UART) ? " uart" : "",
                     (flags & TIKU_AMBIQ_SLEEP_STOP_TICK) ? " tick" : "",
                     (flags & TIKU_AMBIQ_SLEEP_DBGLOCK)   ? " dbg"  : "",
                     (flags & TIKU_AMBIQ_SLEEP_LFRC)      ? " lfrc" : "",
                     (flags & TIKU_AMBIQ_SLEEP_DEEP)      ? " deep" : "");
        us = spin ? tiku_ambiq_spin_probe(ms)
                  : tiku_ambiq_sleep_probe(ms, flags);
        if (spin) {
            uint32_t n = tiku_ambiq_spin_pass_count();
            uint32_t in = tiku_ambiq_spin_inner();
            SHELL_PRINTF("spin done %lu us passes %lu iter %lu kiter/s %lu\n",
                         (unsigned long)us, (unsigned long)n,
                         (unsigned long)(n * in),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)n * in
                                            * 1000u) / us) : 0u));
        } else {
            SHELL_PRINTF("idle done %lu us wakes %lu (%lu/s)\n",
                         (unsigned long)us,
                         (unsigned long)tiku_ambiq_sleep_wake_count(),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)
                            tiku_ambiq_sleep_wake_count() * 1000000u) / us) : 0u));
        }
        return;
    }
    if (streq(argv[1], "mem") && argc >= 4) {
        static const char *const names[TIKU_AMBIQ_MEM_KIND_COUNT] = {
            "nop", "sram_r", "sram_w", "sram_stride", "mram_hot", "mram_cold"
        };
        unsigned kind = TIKU_AMBIQ_MEM_KIND_COUNT, i;
        uint32_t ms = 0u, us, n;
        const char *p2 = argv[3];
        for (i = 0u; i < TIKU_AMBIQ_MEM_KIND_COUNT; i++) {
            if (streq(argv[2], names[i])) { kind = i; }
        }
        while (*p2 >= '0' && *p2 <= '9') {
            ms = ms * 10u + (uint32_t)(*p2++ - '0');
        }
        if (kind == TIKU_AMBIQ_MEM_KIND_COUNT || ms == 0u) {
            SHELL_PRINTF("Usage: power mem <nop|sram_r|sram_w|sram_stride|"
                         "mram_hot|mram_cold> <ms>\n");
            return;
        }
        SHELL_PRINTF("mem %s %lu ms -- starting\n", names[kind],
                     (unsigned long)ms);
        us = tiku_ambiq_mem_probe(kind, ms);
        n = tiku_ambiq_mem_access_count();
        SHELL_PRINTF("mem done %lu us acc %lu kacc/s %lu sum %lx\n",
                     (unsigned long)us, (unsigned long)n,
                     (unsigned long)(us ? (uint32_t)(((uint64_t)n * 1000u) / us)
                                        : 0u),
                     (unsigned long)tiku_ambiq_mem_checksum());
        return;
    }
    /* ---- power cpdlp [elp|clp <0-3>] : Cortex-M55 low-power state ----
     * CPDLPSTATE decides what the core power domain does when the PE enters a
     * low-power state (WFI).  Three independent fields, each ON / ON-clock-off
     * / RET / OFF:
     *   CLPSTATE  the core itself          -- we have NEVER written it (= ON)
     *   ELPSTATE  the FP/MVE extension     -- boot sets ON-clock-off (level 1)
     *   RLPSTATE  the core's RAM           -- never written (= ON); OFF would
     *                                         lose TCM, so this verb refuses it
     * The boot choice of ELP level 1 is the PERFORMANCE option (no power-up
     * stall); levels 2 and 3 trade wake latency for power and have never been
     * measured.  This verb makes that measurable. */
    if (streq(argv[1], "cpdlp")) {
        static const char *const lp[4] = { "ON", "ON-clk-off", "RET", "OFF" };
        uint32_t v;
        if (argc >= 4) {
            unsigned nv = (unsigned)(argv[3][0] - '0');
            if (nv > 3u) {
                SHELL_PRINTF("cpdlp: level must be 0..3\n");
                return;
            }
            v = PWRMODCTL->CPDLPSTATE;
            if (streq(argv[2], "elp")) {
                /* ELP=OFF DISCARDS the FP/MVE register state on every
                 * low-power entry.  This build is hard-float: the kernel holds
                 * live floating-point context across sleeps, so losing it
                 * corrupts whatever was interrupted.  Setting it once cost a
                 * boot-looping board and a physical power cycle (2026-07-28),
                 * and the loop took SWD down with it until the probe speed was
                 * dropped to 1 MHz.  Refused rather than documented. */
                if (nv == 3u) {
                    SHELL_PRINTF("cpdlp: refusing ELP=OFF -- discards FP/MVE "
                                 "state, and this build is hard-float\n");
                    return;
                }
                v = (v & ~(3u << 4)) | (nv << 4);
            } else if (streq(argv[2], "clp")) {
                /* CLP=OFF is deep-sleep territory the SDK never requests from
                 * here; refuse it rather than invent a sequence. */
                if (nv == 3u) {
                    SHELL_PRINTF("cpdlp: refusing CLP=OFF (deep-sleep path, "
                                 "not this verb's job)\n");
                    return;
                }
                v = (v & ~(3u << 0)) | (nv << 0);
            } else {
                SHELL_PRINTF("cpdlp: field must be elp or clp "
                             "(rlp refused: OFF loses TCM)\n");
                return;
            }
            PWRMODCTL->CPDLPSTATE = v;
            __DSB();
            __ISB();
        }
        v = PWRMODCTL->CPDLPSTATE;
        SHELL_PRINTF("cpdlp %08lx: clp=%s elp=%s rlp=%s\n",
                     (unsigned long)v,
                     lp[v & 3u], lp[(v >> 4) & 3u], lp[(v >> 8) & 3u]);
        return;
    }

#if (TIKU_AMBIQ_POWER_PROBE_SIMD + 0)
    if (streq(argv[1], "simd") && argc >= 2) {
        static const char *const kn[TIKU_SP_KIND_COUNT] = {
            "fill", "copy", "multiply", "scale", "affine", "lut",
            "sum", "addsat", "saxpy", "dot"
        };
        /* ---- power simd verify : gate for every energy number ---- */
        if (argc >= 3 && streq(argv[2], "verify")) {
            uint32_t mism = 0u;
            int ok = tiku_simd_power_verify(&mism);
            SHELL_PRINTF("simd verify %s mismatch %08lx native %s\n",
                         ok ? "OK" : "FAILED", (unsigned long)mism,
                         tiku_simd_power_native_backend() ? "helium" : "scalar");
            SHELL_PRINTF("  buffers: dtcm %08lx ssram %08lx\n",
                         (unsigned long)(uintptr_t)
                            tiku_simd_power_buf(TIKU_SP_TIER_DTCM),
                         (unsigned long)(uintptr_t)
                            tiku_simd_power_buf(TIKU_SP_TIER_SSRAM));
            return;
        }
        /* ---- power simd <kernel> <scalar|helium> <dtcm|ssram> <bytes> <ms> ---- */
        if (argc >= 7) {
            unsigned k = TIKU_SP_KIND_COUNT, i, be, tr;
            uint32_t nb = 0u, ms = 0u, us;
            const char *a = argv[5], *b = argv[6];
            for (i = 0u; i < TIKU_SP_KIND_COUNT; i++) {
                if (streq(argv[2], kn[i])) { k = i; }
            }
            be = streq(argv[3], "helium") ? TIKU_SP_BACKEND_HELIUM
                                          : TIKU_SP_BACKEND_SCALAR;
            tr = streq(argv[4], "ssram")  ? TIKU_SP_TIER_SSRAM
                                          : TIKU_SP_TIER_DTCM;
            while (*a >= '0' && *a <= '9') { nb = nb*10u + (uint32_t)(*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { ms = ms*10u + (uint32_t)(*b++ - '0'); }
            if (k == TIKU_SP_KIND_COUNT || nb == 0u || ms == 0u) {
                SHELL_PRINTF("Usage: power simd <kernel> <scalar|helium>"
                             " <dtcm|ssram> <bytes> <ms>\n");
                return;
            }
            SHELL_PRINTF("simd %s %s %s %lu B -- starting\n", kn[k],
                         (be == TIKU_SP_BACKEND_HELIUM) ? "helium" : "scalar",
                         (tr == TIKU_SP_TIER_SSRAM) ? "ssram" : "dtcm",
                         (unsigned long)nb);
            us = tiku_simd_power_probe(k, be, tr, nb, ms);
            /* cyc/elem printed as milli-units: the formatter has no floats and
             * the figure is well below 1 for the vector paths. */
            SHELL_PRINTF("simd done %lu us passes %lu bytes %lu elems %lu "
                         "cycles %lu mcpe %lu fp %lx\n",
                         (unsigned long)us,
                         (unsigned long)tiku_simd_power_passes(),
                         (unsigned long)tiku_simd_power_bytes(),
                         (unsigned long)tiku_simd_power_elems(),
                         (unsigned long)tiku_simd_power_cycles(),
                         (unsigned long)(tiku_simd_power_elems()
                            ? (uint32_t)(((uint64_t)tiku_simd_power_cycles()
                                * 1000u) / tiku_simd_power_elems()) : 0u),
                         (unsigned long)tiku_simd_power_fingerprint());
            return;
        }
        SHELL_PRINTF("Usage: power simd verify | power simd <kernel>"
                     " <scalar|helium> <dtcm|ssram> <bytes> <ms>\n");
        SHELL_PRINTF("  kernels: fill copy multiply scale affine lut sum"
                     " addsat saxpy dot\n");
        return;
    }
#endif
#if (TIKU_AMBIQ_POWER_PROBE_GPU + 0)
    if (streq(argv[1], "gpu") && argc >= 3) {
        static const char *const wn[TIKU_GPU_W_KIND_COUNT] = {
            "fill", "copy", "multiply", "scale", "lut", "reduce"
        };
        static const char *const pn[4] = {
            "LP-96", "HP1-192", "HP2-125", "HP3-250"
        };
        /* ---- power gpu state <off|on> [perf] : the availability ladder ---- */
        if (streq(argv[2], "off")) {
            tiku_gpu_deinit();
            SHELL_PRINTF("gpu off: powered %d\n", tiku_gpu_powered());
            return;
        }
        if (streq(argv[2], "on")) {
            unsigned perf = 0u;
            tiku_gpu_err_t rc;
            if (argc >= 4) {
                const char *q = argv[3];
                perf = 0u;
                while (*q >= '0' && *q <= '9') { perf = perf*10u + (unsigned)(*q++ - '0'); }
                if (perf > 3u) { perf = 0u; }
            }
            rc = tiku_gpu_init((tiku_gpu_perf_t)perf);
            /* Report the mode and rail the SILICON has, not the request. */
            {   /* Clock-gating forensics: CGCTRL's DISCLK* fields DISABLE
                 * automatic gating.  The driver only ever read this register;
                 * if the reset value has them set, an "idle" GPU is fully
                 * clocked, which would explain a 6.37 mA standing draw. */
                tiku_gpu_bringup_t bi;
                tiku_gpu_bringup_info(&bi);
                SHELL_PRINTF("  cgctrl %08lx (DISCLK proc %lu cfg %lu frame %lu"
                             " core %lu mod %lu) status %08lx active %08lx\n",
                             (unsigned long)bi.cgctrl,
                             (unsigned long)(bi.cgctrl & 1u),
                             (unsigned long)((bi.cgctrl >> 1) & 1u),
                             (unsigned long)((bi.cgctrl >> 2) & 3u),
                             (unsigned long)((bi.cgctrl >> 23) & 1u),
                             (unsigned long)((bi.cgctrl >> 30) & 3u),
                             (unsigned long)bi.status,
                             (unsigned long)bi.active);
                /* The clock DELIVERED to the domain is a separate control from
                 * the block's own gating, and lives in CLKGEN, not the GPU. */
                SHELL_PRINTF("  clkgen clkctrl %08lx (GFXCORECLKEN %lu "
                             "GFXCORECLKSEL %lu)\n",
                             (unsigned long)CLKGEN->CLKCTRL,
                             (unsigned long)(CLKGEN->CLKCTRL & 1u),
                             (unsigned long)((CLKGEN->CLKCTRL >> 1) & 3u));
            }
            SHELL_PRINTF("gpu on rc %d: powered %d id %08lx perf %lu (%s) "
                         "rail %s\n", (int)rc, tiku_gpu_powered(),
                         (unsigned long)tiku_gpu_id(),
                         (unsigned long)tiku_gpu_perf_get(),
                         pn[tiku_gpu_perf_get() & 3u],
                         tiku_gpu_rail_is_vddf() ? "VDDF" : "VDDC");
            return;
        }
        /* ---- power gpu ram <0..7> : SSRAMACTGFX forensics ----
         * Boot state ships SSRAMACTGFX=7: ALL SSRAM banks forced ACTIVE
         * whenever the GFX domain is powered -- including through the MCU's
         * WFI, when they could otherwise fall back to retention.  The SDK's
         * own default for this field is NONE; banks wake on access anyway
         * (datasheet 4.3.4).  This verb A/Bs the field so the cost is a
         * measurement, not an argument. */
        if (streq(argv[2], "ram") && argc >= 4) {
            unsigned v = (unsigned)(argv[3][0] - '0') & 7u;
            unsigned before = PWRCTRL->SSRAMRETCFG_b.SSRAMACTGFX;
            PWRCTRL->SSRAMRETCFG_b.SSRAMACTGFX = v;
            SHELL_PRINTF("gpu ram: SSRAMACTGFX %u -> %lu (retcfg %08lx)\n",
                         before,
                         (unsigned long)PWRCTRL->SSRAMRETCFG_b.SSRAMACTGFX,
                         (unsigned long)PWRCTRL->SSRAMRETCFG);
            return;
        }

        /* ---- power gpu <work|cpu|contend> ... ---- */
        if (streq(argv[2], "work") && argc >= 6) {
            unsigned k = TIKU_GPU_W_KIND_COUNT, i;
            uint32_t side = 0u, ms = 0u, us;
            const char *a = argv[4], *b = argv[5];
            /* "async" alone = batch 1 (one draw per list); "async <N>"
             * batches N draws into each submitted list. */
            int async = 0;
            if (argc >= 7 && streq(argv[6], "async")) {
                async = 1;
                if (argc >= 8) {
                    const char *q = argv[7]; int n = 0;
                    while (*q >= '0' && *q <= '9') { n = n*10 + (*q++ - '0'); }
                    if (n > 0) { async = n; }
                }
            }
            for (i = 0u; i < TIKU_GPU_W_KIND_COUNT; i++) {
                if (streq(argv[3], wn[i])) { k = i; }
            }
            while (*a >= '0' && *a <= '9') { side = side*10u + (uint32_t)(*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { ms   = ms*10u   + (uint32_t)(*b++ - '0'); }
            if (k == TIKU_GPU_W_KIND_COUNT || side == 0u || ms == 0u) {
                SHELL_PRINTF("Usage: power gpu work <fill|copy|multiply|scale|"
                             "lut|reduce> <side> <ms> [async]\n");
                return;
            }
            if (async) {
                SHELL_PRINTF("gpu work %s side %lu async batch %d -- starting\n",
                             wn[k], (unsigned long)side, async);
            } else {
                SHELL_PRINTF("gpu work %s side %lu blocking -- starting\n",
                             wn[k], (unsigned long)side);
            }
            us = tiku_gpu_power_probe(k, side, ms, async);
            SHELL_PRINTF("gpu done %lu us ops %lu bytes %lu MBps %lu wakes %lu "
                         "sum %lx exact %d\n",
                         (unsigned long)us,
                         (unsigned long)tiku_gpu_power_ops(),
                         (unsigned long)tiku_gpu_power_bytes(),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)
                            tiku_gpu_power_bytes() * 1000u) / us / 1024u) : 0u),
                         (unsigned long)tiku_gpu_power_wakes(),
                         (unsigned long)tiku_gpu_power_checksum(),
                         tiku_gpu_power_exact());
            return;
        }
        if (streq(argv[2], "cpu") && argc >= 6) {
            uint32_t side = 0u, ms = 0u, us;
            unsigned k = streq(argv[3], "copy") ? TIKU_GPU_CPU_COPY
                                                : TIKU_GPU_CPU_FILL;
            const char *a = argv[4], *b = argv[5];
            while (*a >= '0' && *a <= '9') { side = side*10u + (uint32_t)(*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { ms   = ms*10u   + (uint32_t)(*b++ - '0'); }
            if (side == 0u || ms == 0u) {
                SHELL_PRINTF("Usage: power gpu cpu <fill|copy> <side> <ms>\n");
                return;
            }
            SHELL_PRINTF("gpu cpu %s side %lu -- starting\n", argv[3],
                         (unsigned long)side);
            us = tiku_gpu_power_cpu_probe(k, side, ms);
            SHELL_PRINTF("gpu done %lu us ops %lu bytes %lu MBps %lu wakes 0 "
                         "sum %lx exact %d\n",
                         (unsigned long)us,
                         (unsigned long)tiku_gpu_power_ops(),
                         (unsigned long)tiku_gpu_power_bytes(),
                         (unsigned long)(us ? (uint32_t)(((uint64_t)
                            tiku_gpu_power_bytes() * 1000u) / us / 1024u) : 0u),
                         (unsigned long)tiku_gpu_power_checksum(),
                         tiku_gpu_power_exact());
            return;
        }
        if (streq(argv[2], "contend") && argc >= 5) {
            uint32_t side = 0u, ms = 0u, us;
            const char *a = argv[3], *b = argv[4];
            while (*a >= '0' && *a <= '9') { side = side*10u + (uint32_t)(*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { ms   = ms*10u   + (uint32_t)(*b++ - '0'); }
            if (side == 0u || ms == 0u) {
                SHELL_PRINTF("Usage: power gpu contend <side> <ms>\n");
                return;
            }
            SHELL_PRINTF("gpu contend side %lu -- starting\n",
                         (unsigned long)side);
            us = tiku_gpu_power_contend_probe(side, ms);
            SHELL_PRINTF("gpu done %lu us ops %lu bytes %lu cpuops %lu "
                         "sum %lx exact %d\n",
                         (unsigned long)us,
                         (unsigned long)tiku_gpu_power_ops(),
                         (unsigned long)tiku_gpu_power_bytes(),
                         (unsigned long)tiku_gpu_power_cpu_ops(),
                         (unsigned long)tiku_gpu_power_checksum(),
                         tiku_gpu_power_exact());
            return;
        }
        SHELL_PRINTF("Usage: power gpu on [0-3] | off | work <kind> <side> <ms>"
                     " [async] | cpu <fill|copy> <side> <ms> | contend <side>"
                     " <ms>\n");
        SHELL_PRINTF("  surfaces: dst %08lx src %08lx (must be SSRAM)\n",
                     (unsigned long)(uintptr_t)tiku_gpu_power_dst(),
                     (unsigned long)(uintptr_t)tiku_gpu_power_src());
        return;
    }
#endif
    SHELL_PRINTF("Usage: power [floor | clock | cache on|off | idle <ms> [deep]"
                 " | spin <ms> | mem <kind> <ms>"
#if (TIKU_AMBIQ_POWER_PROBE_GPU + 0)
                 " | gpu ..."
#endif
                 "]\n");
    return;
#endif

    SHELL_PRINTF("Usage: power [cache on|off | dcdc on|off | bench | clock | probe | stat | clear]\n");
}

#endif /* TIKU_SHELL_CMD_POWER */
