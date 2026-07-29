/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_power.c - "power": report and steer what the part costs.
 *
 * Every knob is readable and, where the silicon allows, switchable at run time.
 * A build flag is not evidence that a register was written, and one boot
 * measuring a workload both ways beats comparing two flashes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_power.h"

#if TIKU_SHELL_CMD_POWER

#include <kernel/shell/tiku_shell_io.h>
#include <hal/tiku_cpu.h>
#include "tiku_shell_cmd_sleep.h"
/* S4: the emmc/psram/usb/nor verbs moved to their own modules.  `power` keeps
 * the top-level spelling and forwards, so `power emmc id` is unchanged. */
#include "tiku_shell_cmd_emmc.h"
#include "tiku_shell_cmd_psram.h"
#include "tiku_shell_cmd_usb.h"
#include "tiku_shell_cmd_nor.h"
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
#include <kernel/cpu/tiku_hang.h>             /* long probe loops block      */
#if (TIKU_DRV_NOR_ENABLE + 0)
#include <arch/ambiq/tiku_nor_arch.h>          /* MSPI1 + U12 external NOR    */
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
#include <arch/ambiq/tiku_emmc_arch.h>         /* SDIO0 + U11 8 GB eMMC       */
#endif
#if (TIKU_DRV_USB_ENABLE + 0)
#include <arch/ambiq/tiku_usb_arch.h>          /* USB device controller       */
#include <kernel/scheduler/tiku_sched.h>       /* idle hook for the MSC pump  */
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

/** @brief Parse an unsigned decimal; 0 on anything unparseable.
 *  Marked unused: only some driver configurations reference it. */
static uint32_t parse_u32(const char *tok) __attribute__((unused));
static uint32_t parse_u32(const char *tok)
{
    uint32_t v = 0u;
    while (*tok >= '0' && *tok <= '9') {
        v = v * 10u + (uint32_t)(*tok++ - '0');
    }
    return v;
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
#if (TIKU_DRV_USB_ENABLE + 0)
    if (streq(argv[1], "usb")) {
        tiku_shell_cmd_usb(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
    if (streq(argv[1], "emmc")) {
        tiku_shell_cmd_emmc(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_NOR_ENABLE + 0)
    if (streq(argv[1], "nor")) {
        tiku_shell_cmd_nor(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (streq(argv[1], "psram")) {
        tiku_shell_cmd_psram(argc, argv);
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
