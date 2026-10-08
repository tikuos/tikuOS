/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_power.c - "power": report and switch power settings.
 *
 * Reports the clock, cache, supply and idle settings and, where the silicon
 * allows, switches them at run time; on nRF54L and Apollo510 it also runs
 * timed idle, spin and memory-access probes for current measurement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_power.h"

#if TIKU_SHELL_CMD_POWER

#include <kernel/shell/tiku_shell_io.h>
#include <hal/tiku_cpu.h>
#include "tiku_shell_cmd_sleep.h"
/* The emmc, psram, usb and nor verbs live in their own modules; `power`
 * forwards them. */
#include "tiku_shell_cmd_emmc.h"
#include "tiku_shell_cmd_psram.h"
#include "tiku_shell_cmd_usb.h"
#include "tiku_shell_cmd_nor.h"
#include "tiku_shell_cmd_util.h"

#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_power_arch.h>
#include <arch/nordic/tiku_device_select.h>  /* instance macros for `floor` */
#include <arch/nordic/tiku_cpu_common.h>     /* tiku_cpu_nordic_delay_ms    */
#endif
#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)
#include <arch/ambiq/tiku_power_ambiq.h>
#include <arch/ambiq/tiku_timer_arch.h>       /* stimer reclock / rate       */
#include <kernel/timers/tiku_clock.h>         /* tickless begin/end (guard)  */
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h>  /* SIMOBUCK enable hook     */
#if (TIKU_AMBIQ_POWER_PROBE_GPU + 0)
#include <arch/ambiq/tiku_gpu_power.h>        /* GPU compute power probe     */
#endif
#if (TIKU_AMBIQ_POWER_PROBE_SIMD + 0)
#include <arch/ambiq/tiku_simd_power.h>       /* Helium vs scalar probe      */
#endif
#include "apollo510.h"                /* PWRCTRL, CLKGEN, STIMER, PWRMODCTL */
#endif
#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_sleep_arch.h>
#include <arch/esp32c61/tiku_cpu_common.h>
#include <arch/esp32c61/tiku_cpu_freq_boot_arch.h>
#include <arch/esp32c61/tiku_esp32c61_regs.h>
#endif

/** @brief Print the clocks and idle mode, plus nRF54L cache/DC-DC/debug. */
static void power_report(void)
{
    SHELL_PRINTF("core:  %lu MHz\n", tiku_cpu_mclk_hz() / 1000000UL);
    SHELL_PRINTF("pclk:  %lu MHz\n", tiku_cpu_smclk_hz() / 1000000UL);
#if defined(PLATFORM_NORDIC)
    SHELL_PRINTF("cache: %s\n", tiku_nordic_cache_enabled() ? "on" : "off");
    {
        int det = tiku_nordic_dcdc_inductor_present();
        /* -1: the detector cannot run while the converter is on, and the
         * inductor prints as unknown. */
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
/** @brief Print the cache profiling counters and the hit rate. */
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
        /* A zero hit+miss total means the profiling counters are not
         * running. */
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
    (void)argv;     /* read only by the platform branches below */

#if defined(PLATFORM_NORDIC)
    if (tiku_cmd_streq(argv[1], "stat")) {
        power_stat();
        return;
    }
    if ((tiku_cmd_streq(argv[1], "idle") ||
         tiku_cmd_streq(argv[1], "spin")) && argc >= 3) {
        /* idle and spin share one flag parser, so a pair of runs with the
         * same flags differs only in what the CPU does.
         *   idle <ms>  WFI          spin <ms>  while(1)
         * "quiet" sets pll, uart, hfxo, tim and deep. */
        int spin = tiku_cmd_streq(argv[1], "spin");
        unsigned flags = 0u, i;
        uint32_t ms = 0u, us;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        for (i = 3u; i < (unsigned)argc; i++) {
            if (tiku_cmd_streq(argv[i], "pll")) {
                flags |= TIKU_SLEEP_STOP_PLL;
            }
            if (tiku_cmd_streq(argv[i], "uart")) {
                flags |= TIKU_SLEEP_STOP_UART;
            }
            if (tiku_cmd_streq(argv[i], "hfxo")) {
                flags |= TIKU_SLEEP_STOP_HFXO;
            }
            if (tiku_cmd_streq(argv[i], "deep")) {
                flags |= TIKU_SLEEP_DEEP;
            }
            if (tiku_cmd_streq(argv[i], "tim")) {
                flags |= TIKU_SLEEP_STOP_TIM;
            }
            if (tiku_cmd_streq(argv[i], "tick")) {
                flags |= TIKU_SLEEP_STOP_TICK;
            }
            if (tiku_cmd_streq(argv[i], "sysc")) {
                flags |= TIKU_SLEEP_STOP_SYSC;
            }
            if (tiku_cmd_streq(argv[i], "quiet")) {
                flags |= TIKU_SLEEP_STOP_PLL | TIKU_SLEEP_STOP_UART |
                         TIKU_SLEEP_STOP_HFXO | TIKU_SLEEP_STOP_TIM |
                         TIKU_SLEEP_DEEP;
            }
        }
        if (ms == 0u) {
            SHELL_PRINTF("Usage: power %s <ms> "
                         "[quiet|deep|tim|tick|uart|pll|hfxo|sysc]\n", argv[1]);
            return;
        }
        /* The line names every flag set; a measurement log keeps it as
         * the record of the state the current was drawn in. */
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
            /* The pass and iteration counts measure the work done in the
             * window. */
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
        /* FLPR coprocessor passes completed in the same window, whatever
         * state the app core was in, printed against the same microsecond
         * count. */
        if (tiku_nordic_flpr_pass_delta() != 0u) {
            SHELL_PRINTF("flpr passes %lu in %lu us\n",
                         (unsigned long)tiku_nordic_flpr_pass_delta(),
                         (unsigned long)us);
        }
#endif
        return;
    }
    if (tiku_cmd_streq(argv[1], "mem") && argc >= 4) {
        /* power mem <kind> <ms>: run one memory-access pattern for <ms>.
         * Prints the access count, the access rate and a traversal
         * checksum; equal checksums show two runs did the same work. */
        static const char *const names[TIKU_MEM_KIND_COUNT] = {
            "nop", "sram_r", "sram_w", "sram_stride", "rram_hot", "rram_cold"
        };
        unsigned kind = TIKU_MEM_KIND_COUNT;
        uint32_t ms = 0u, us, n, i;
        const char *p2 = argv[3];
        for (i = 0u; i < TIKU_MEM_KIND_COUNT; i++) {
            if (tiku_cmd_streq(argv[2], names[i])) {
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
        if (us == 0u) {
            SHELL_PRINTF("mem: the SRAM tier has no 64 KB to lend the "
                         "probe (free what holds it, then retry)\n");
            return;
        }
        SHELL_PRINTF("mem done %lu us acc %lu kacc/s %lu sum %lx\n",
                     (unsigned long)us, (unsigned long)n,
                     (unsigned long)(us ? (uint32_t)(((uint64_t)n * 1000u) / us)
                                        : 0u),
                     (unsigned long)tiku_nordic_mem_checksum());
        return;
    }
    if (tiku_cmd_streq(argv[1], "lfclk")) {
        /* Start the 32.768 kHz low-frequency clock, which the port leaves
         * off at boot.  Without it the GRTC keeps time from the HF clock,
         * which keeps the HF domain powered whatever else a sleep releases.
         * The datasheet's System ON idle current assumes the GRTC on the
         * 32 kHz crystal. */
        uint32_t src = CLOCK_LFCLK_SRC_SRC_LFXO;       /* DK has the crystal */
        if (argc >= 3 && tiku_cmd_streq(argv[2], "lfrc")) {
            src = CLOCK_LFCLK_SRC_SRC_LFRC;
        }
        if (argc >= 3 && tiku_cmd_streq(argv[2], "synth")) {
            src = CLOCK_LFCLK_SRC_SRC_LFSYNT;
        }
        NRF_CLOCK_S->LFCLK.SRC = src;
        NRF_CLOCK_S->EVENTS_LFCLKSTARTED = 0u;
        NRF_CLOCK_S->TASKS_LFCLKSTART = 1u;
        {
            /* LFXO start-up is crystal settling, hundreds of ms worst case;
             * poll with a generous bound and print the state reached. */
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
    if (tiku_cmd_streq(argv[1], "floor")) {
        /* Print the clock, RRAM, GRTC, watchdog, enable and interrupt
         * state that can raise the idle current.  Reads only. */
        SHELL_PRINTF("clock: xo.run=%lu pll.run=%lu lfclk.src=%lu run=%lu"
                     " stat=%lx\n",
                     (unsigned long)NRF_CLOCK_S->XO.RUN,
                     (unsigned long)NRF_CLOCK_S->PLL.RUN,
                     (unsigned long)NRF_CLOCK_S->LFCLK.SRC,
                     (unsigned long)NRF_CLOCK_S->LFCLK.RUN,
                     (unsigned long)NRF_CLOCK_S->LFCLK.STAT);
        /* RRAMC: LOWPOWERCONFIG 0, the reset value, powers the RRAM off
         * in low power; CONFIG.ACCESSTIMEOUT is the active-to-standby
         * delay in 31.25 ns units. */
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
    if (tiku_cmd_streq(argv[1], "why")) {
        /* Decode RESETREAS, the causes of the last reset, including a
         * wake from System OFF.  `why clear` writes the bits back to clear
         * them. */
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
        if (argc >= 3 && tiku_cmd_streq(argv[2], "clear")) {
            *(volatile uint32_t *)0x5010E600UL = r;   /* W1C */
            SHELL_PRINTF("cleared\n");
        }
        return;
    }
    if (tiku_cmd_streq(argv[1], "off")) {
        /* System OFF, the part's lowest-power state.
         * tiku_nordic_system_off() disarms the GRTC compares first, and any
         * wake is a reset.  On the DK, P14 measures the VDDM rail; current
         * left in System OFF is drawn by the board or the debug domain. */
        SHELL_PRINTF("entering System OFF -- wake by RESET only\n");
        tiku_cpu_nordic_delay_ms(20u);          /* let the line drain */
        tiku_nordic_system_off();               /* does not return */
    }

    if (tiku_cmd_streq(argv[1], "bench")) {
        uint32_t us = 0u, hit = 0u, miss = 0u, sum;
        tiku_nordic_cache_profile_start();
        sum = tiku_nordic_cache_workload(&us);
        tiku_nordic_cache_profile_read(&hit, &miss, NULL, NULL);
        /* Runs that did the same work print the same checksum. */
        SHELL_PRINTF("bench us %lu sum %lu cache %s hits %lu misses %lu\n",
                     (unsigned long)us, (unsigned long)sum,
                     tiku_nordic_cache_enabled() ? "on" : "off",
                     (unsigned long)hit, (unsigned long)miss);
        return;
    }
    if (tiku_cmd_streq(argv[1], "clock")) {
        unsigned long meas = tiku_nordic_cpu_hz_measure();
        unsigned long rep  = tiku_cpu_mclk_hz();
        SHELL_PRINTF("reported %lu Hz\n", rep);
        SHELL_PRINTF("measured %lu Hz\n", meas);
        /* 2% covers the 1 MHz GRTC's quantisation over a 50 ms window plus
         * the handful of cycles spent in the sampling code itself. */
        SHELL_PRINTF("clock: %s\n",
                     (meas > rep - rep / 50UL && meas < rep + rep / 50UL)
                         ? "AGREE" : "MISMATCH");
        return;
    }
    if (tiku_cmd_streq(argv[1], "probe")) {
        int det = tiku_nordic_dcdc_probe_inductor();
        SHELL_PRINTF("inductor: %s\n", det ? "detected" : "absent");
        SHELL_PRINTF("dcdc:     %s\n",
                     tiku_nordic_dcdc_enabled() ? "on" : "off");
        return;
    }
    if (tiku_cmd_streq(argv[1], "clear")) {
        tiku_nordic_cache_profile_start();
        SHELL_PRINTF("cache counters cleared and running\n");
        return;
    }
    if (tiku_cmd_streq(argv[1], "cache") && argc >= 3) {
        int on = tiku_cmd_parse_on_off(argv[2]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power cache on|off\n");
            return;
        }
        /* The datasheet allows the cache to be switched at run time; the
         * core frequency is set at boot only. */
        tiku_nordic_cache_set(on);
        SHELL_PRINTF("cache: %s\n", tiku_nordic_cache_enabled() ? "on" : "off");
        return;
    }
    if (tiku_cmd_streq(argv[1], "dcdc") && argc >= 3) {
        int on = tiku_cmd_parse_on_off(argv[2]);
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
    SHELL_PRINTF("Usage: power [stat | idle|spin <ms> [flags] | mem <kind> <ms>"
                 " | lfclk [lfrc|synth] | floor | why [clear] | off | bench"
                 " | clock | probe | clear | cache on|off | dcdc on|off]\n");
    return;
#endif

#if defined(PLATFORM_ESP32C61)
    /* Deep sleep powers the HP domain down, SRAM with it, and the wake is a
     * reset: `off` checkpoints durable state first, and `why` prints what
     * ended the last sleep.  `nap` is light sleep, which keeps all state and
     * returns, early on a keystroke or, with `pin`, on GPIO9 low. */
    if (tiku_cmd_streq(argv[1], "nap") && argc >= 3) {
        uint32_t ms = tiku_cmd_parse_u32(argv[2]);
        unsigned flags = (argc >= 4 && tiku_cmd_streq(argv[3], "pin")) ?
                         TIKU_ESP32C61_NAP_PIN : 0U;
        uint64_t t0 = tiku_cpu_esp32c61_systimer();
        uint32_t w = tiku_esp32c61_light_sleep((uint64_t)ms * 1000ULL, flags);
        uint64_t dt = tiku_cpu_esp32c61_systimer() - t0;

        SHELL_PRINTF("nap: %lu us,%s%s%s%s\n", (unsigned long)(dt / 16ULL),
                     w ? " woke by" : " refused: a wake was waiting",
                     (w & ESP32C61_PMU_WAKE_TIMER) ? " timer" : "",
                     (w & ESP32C61_PMU_WAKE_UART0) ? " uart0" : "",
                     (w & ESP32C61_PMU_WAKE_GPIO)  ? " gpio"  : "");
        return;
    }
    if (tiku_cmd_streq(argv[1], "why")) {
        uint32_t w = tiku_esp32c61_wake_cause();

        SHELL_PRINTF("reset: rom code %lu%s\n",
                     (unsigned long)tiku_cpu_esp32c61_reset_code(),
                     tiku_esp32c61_sleep_missed()
                         ? " (a deep sleep that failed, reset late)" : "");
        SHELL_PRINTF("wake: %s%s%s%s%s\n", w ? "" : "none (not a sleep wake)",
                     (w & ESP32C61_PMU_WAKE_TIMER) ? " timer" : "",
                     (w & ESP32C61_PMU_WAKE_EXT1)  ? " ext1"  : "",
                     (w & ESP32C61_PMU_WAKE_GPIO)  ? " gpio"  : "",
                     (w & ESP32C61_PMU_WAKE_UART0) ? " uart0" : "");
        return;
    }
    if (tiku_cmd_streq(argv[1], "off")) {
        uint32_t ms = (argc >= 3) ? tiku_cmd_parse_u32(argv[2]) : 0u;

        if (ms == 0u) {
            SHELL_PRINTF("deep sleep -- wake by RESET only\n");
        } else {
            SHELL_PRINTF("deep sleep for %lu ms -- the wake is a reset\n",
                         (unsigned long)ms);
        }
        tiku_esp32c61_deep_sleep((uint64_t)ms * 1000ULL);
    }
    SHELL_PRINTF("Usage: power [why | off [ms] | nap <ms> [pin]]\n");
    return;
#endif

#if defined(PLATFORM_AMBIQ) && (TIKU_AMBIQ_POWER_PROBE + 0)
    /* Apollo510 verbs.  `floor`, `cache`, `clock`, `idle`, `spin` and `mem`
     * share names with the nRF54L verbs but read and switch this part's own
     * registers, release flags and timebase. */
    if (tiku_cmd_streq(argv[1], "floor")) {
        uint32_t ic = 0u, dc = 0u, ln = 0u;
        tiku_ambiq_cache_geometry(&ic, &dc, &ln);
        SHELL_PRINTF("cache: I %lu B  D %lu B  line %lu B  (read from CCSIDR,"
                     " not assumed)\n", (unsigned long)ic, (unsigned long)dc,
                     (unsigned long)ln);
        SHELL_PRINTF("  enabled: %s\n", tiku_ambiq_cache_enabled() ? "yes" : "no");
        /* Which power domains and memories are on (enable / status). */
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
    if (tiku_cmd_streq(argv[1], "dev") && argc >= 4) {
        /* Per-domain power switches.  Each verb prints EN and STATUS after
         * the write; STATUS shows whether the domain followed.
         *   crypto/otp:  DEVPWREN bits the SBL leaves on; boot clears both
         *                when TIKU_AMBIQ_BOOT_TIDY is 1, the default
         *   nvm1:        the upper 2 MB MRAM bank; the code window is in NVM0
         *   rom:         boot ROM (needed again only for bootrom MRAM writes)
         *   ssram:       all 3 MB shared SRAM; the SRAM tier lives there, so
         *                its contents are lost -- measure, then reboot
         *   trc:         DEMCR.TRCENA and the DWT cycle counter
         * nvm1, rom and ssram refuse `off` unless followed by `force`. */
        int on = tiku_cmd_parse_on_off(argv[3]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power dev <crypto|otp|nvm1|rom|ssram|trc> on|off\n");
            return;
        }
        if (tiku_cmd_streq(argv[2], "crypto")) {
            PWRCTRL->DEVPWREN_b.PWRENCRYPTO = (uint32_t)on;
        } else if (tiku_cmd_streq(argv[2], "otp")) {
            PWRCTRL->DEVPWREN_b.PWRENOTP = (uint32_t)on;
        } else if (tiku_cmd_streq(argv[2], "nvm1")) {
            /* The upper bank holds part of the carved NVM region (tier and
             * file store) and the 16 KB persist mirror at the top of MRAM:
             * with it off, the next NVM flush faults, and so does a /data
             * access to that bank. */
            if (!on && (argc < 5 || !tiku_cmd_streq(argv[4], "force"))) {
                SHELL_PRINTF("nvm1 off can fault a running OS (the carved NVM "
                             "region may span it).  Add 'force' if the image "
                             "does not touch it.\n");
                return;
            }
            PWRCTRL->MEMPWREN_b.PWRENNVM1 = (uint32_t)on;
        } else if (tiku_cmd_streq(argv[2], "rom")) {
            /* MRAM writes on this part go through the boot ROM, and the OS
             * writes NVM on its own (boot counter, persist cells, /data): with
             * the ROM off the next write faults, and the fault loop can keep
             * SWD from attaching until a power cycle. */
            if (!on && (argc < 5 || !tiku_cmd_streq(argv[4], "force"))) {
                SHELL_PRINTF("rom off breaks bootrom-mediated MRAM writes and "
                             "has wedged this board (fault loop + SWD attach "
                             "failure, power cycle to recover).  Worth ~14 uA. "
                             "Add 'force' only in an image that never writes "
                             "NVM.\n");
                return;
            }
            PWRCTRL->MEMPWREN_b.PWRENROM = (uint32_t)on;
        } else if (tiku_cmd_streq(argv[2], "ssram")) {
            if (!on && (argc < 5 || !tiku_cmd_streq(argv[4], "force"))) {
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
                /* Clear the SSRAMACT forces (MCU, GFX and display) first:
                 * PWREN=0 with them still set is a contested request that
                 * raises the current and can fault-reboot the board. */
                PWRCTRL->SSRAMRETCFG &= ~((0x7u << 3) | (0x7u << 9)
                                          | (0x7u << 12));
                PWRCTRL->SSRAMPWREN = 0x0u;
            }
        } else if (tiku_cmd_streq(argv[2], "trc")) {
            /* The DWT clock measurement sets DEMCR.TRCENA and leaves it on;
             * `off` releases it. */
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
            uint32_t spin = 200000u;   /* domains ack in STATUS, not at once */
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
    if (tiku_cmd_streq(argv[1], "buck")) {
        /* The Apollo counterpart of `power dcdc`, enable only:
         * tiku_cpu_freq_ambiq_simobuck_enable() moves the load from the LDOs
         * to the SIMO buck, and only a reboot returns it to the LDOs. */
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
    if (tiku_cmd_streq(argv[1], "clock")) {
        unsigned long hz = tiku_ambiq_cpu_hz_measure();
        SHELL_PRINTF("core measured %lu Hz (DWT cycle counter timed against "
                     "the always-on STIMER)%s\n", hz,
                     hz ? "" : " -- REFUSED: DWT or STIMER not counting");
        return;
    }
    if (tiku_cmd_streq(argv[1], "cache") && argc >= 3) {
        int on = tiku_cmd_parse_on_off(argv[2]);
        if (on < 0) {
            SHELL_PRINTF("Usage: power cache on|off\n");
            return;
        }
        tiku_ambiq_cache_set(on);
        SHELL_PRINTF("cache: %s\n", tiku_ambiq_cache_enabled() ? "on" : "off");
        return;
    }
#if (TIKU_DRV_USB_ENABLE + 0)
    if (tiku_cmd_streq(argv[1], "usb")) {
        tiku_shell_cmd_usb(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
    if (tiku_cmd_streq(argv[1], "emmc")) {
        tiku_shell_cmd_emmc(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_NOR_ENABLE + 0)
    if (tiku_cmd_streq(argv[1], "nor")) {
        tiku_shell_cmd_nor(argc, argv);
        return;
    }
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (tiku_cmd_streq(argv[1], "psram")) {
        tiku_shell_cmd_psram(argc, argv);
        return;
    }
#endif
    if (tiku_cmd_streq(argv[1], "stimer")) {
        /* Timebase health: current rate, whether the counter is counting,
         * and whether the tickless guard accepts or refuses a stretch.
         *
         * `power stimer kill` selects NOCLK, reports whether the guard
         * refuses a stretch on the stopped clock, then selects the crystal
         * (XTAL_32KHZ) and reports again within the same call. */
        int kill = (argc >= 3 && tiku_cmd_streq(argv[2], "kill"));
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
    if (tiku_cmd_streq(argv[1], "reclock") && argc >= 3) {
        /* power reclock lfrc|xtal: switch the STIMER timebase the way deep
         * sleep does, outside a deep sleep. */
        int lf = tiku_cmd_streq(argv[2], "lfrc");
        uint32_t hz = tiku_ambiq_stimer_reclock(lf ? 1 : 0);
        SHELL_PRINTF("reclock %s -> %lu Hz%s\n", lf ? "lfrc" : "xtal",
                     (unsigned long)hz,
                     hz ? "" : " (FAILED, timebase left on XTAL)");
        return;
    }
    if ((tiku_cmd_streq(argv[1], "idle") ||
         tiku_cmd_streq(argv[1], "spin")) && argc >= 3) {
        int spin = tiku_cmd_streq(argv[1], "spin");
        unsigned flags = 0u, i;
        uint32_t ms = 0u, us;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        for (i = 3u; i < (unsigned)argc; i++) {
            if (tiku_cmd_streq(argv[i], "deep")) {
                flags |= TIKU_AMBIQ_SLEEP_DEEP;
            }
            if (tiku_cmd_streq(argv[i], "uart")) {
                flags |= TIKU_AMBIQ_SLEEP_STOP_UART;
            }
            if (tiku_cmd_streq(argv[i], "tick")) {
                flags |= TIKU_AMBIQ_SLEEP_STOP_TICK;
            }
            if (tiku_cmd_streq(argv[i], "dbg")) {
                flags |= TIKU_AMBIQ_SLEEP_DBGLOCK;
            }
            if (tiku_cmd_streq(argv[i], "lfrc")) {
                flags |= TIKU_AMBIQ_SLEEP_LFRC;
            }
        }
        if (ms == 0u) {
            SHELL_PRINTF("Usage: power %s <ms> [deep|uart|tick|dbg|lfrc]\n",
                         argv[1]);
            return;
        }
        if (spin && flags != 0u) {
            SHELL_PRINTF("power spin: sleep flags are not supported\n");
            return;
        }
        /* The line names every flag set. */
        SHELL_PRINTF("%s %lu ms flags%s%s%s%s%s -- starting\n", argv[1],
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
    if (tiku_cmd_streq(argv[1], "mem") && argc >= 4) {
        static const char *const names[TIKU_AMBIQ_MEM_KIND_COUNT] = {
            "nop", "sram_r", "sram_w", "sram_stride", "mram_hot", "mram_cold"
        };
        unsigned kind = TIKU_AMBIQ_MEM_KIND_COUNT, i;
        uint32_t ms = 0u, us, n;
        const char *p2 = argv[3];
        for (i = 0u; i < TIKU_AMBIQ_MEM_KIND_COUNT; i++) {
            if (tiku_cmd_streq(argv[2], names[i])) { kind = i; }
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
     *   CLPSTATE  the core itself          -- not written at boot (= ON)
     *   ELPSTATE  the FP/MVE extension     -- boot sets TIKU_AMBIQ_ELP_STATE
     *                                         (default 2, RET)
     *   RLPSTATE  the core's RAM           -- never written (= ON); OFF would
     *                                         lose TCM, so this verb refuses it
     * ELP level 1 avoids the power-up stall on wake; level 2 trades wake
     * latency for power; level 3 is refused (hard-float). */
    if (tiku_cmd_streq(argv[1], "cpdlp")) {
        static const char *const lp[4] = { "ON", "ON-clk-off", "RET", "OFF" };
        uint32_t v;
        if (argc >= 4) {
            unsigned nv = (unsigned)(argv[3][0] - '0');
            if (nv > 3u) {
                SHELL_PRINTF("cpdlp: level must be 0..3\n");
                return;
            }
            v = PWRMODCTL->CPDLPSTATE;
            if (tiku_cmd_streq(argv[2], "elp")) {
                /* ELP=OFF discards the FP/MVE register state on every
                 * low-power entry.  This build is hard-float: the kernel holds
                 * live floating-point context across sleeps, so losing it
                 * corrupts whatever was interrupted. */
                if (nv == 3u) {
                    SHELL_PRINTF("cpdlp: refusing ELP=OFF -- discards FP/MVE "
                                 "state, and this build is hard-float\n");
                    return;
                }
                v = (v & ~(3u << 4)) | (nv << 4);
            } else if (tiku_cmd_streq(argv[2], "clp")) {
                /* This verb refuses CLP=OFF, which lets the core itself
                 * power off at a low-power entry. */
                if (nv == 3u) {
                    SHELL_PRINTF("cpdlp: refusing CLP=OFF; CPU power-off is "
                                 "unsupported\n");
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
    if (tiku_cmd_streq(argv[1], "simd") && argc >= 2) {
        static const char *const kn[TIKU_SP_KIND_COUNT] = {
            "fill", "copy", "multiply", "scale", "affine", "lut",
            "sum", "addsat", "saxpy", "dot"
        };
        /* ---- power simd verify : both backends agree on every kernel ---- */
        if (argc >= 3 && tiku_cmd_streq(argv[2], "verify")) {
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
        /* ---- power simd <kernel> <scalar|helium> <dtcm|ssram> <bytes> <ms> */
        if (argc >= 7) {
            unsigned k = TIKU_SP_KIND_COUNT, i, be, tr;
            uint32_t nb = 0u, ms = 0u, us;
            const char *a = argv[5], *b = argv[6];
            for (i = 0u; i < TIKU_SP_KIND_COUNT; i++) {
                if (tiku_cmd_streq(argv[2], kn[i])) { k = i; }
            }
            be = tiku_cmd_streq(argv[3], "helium") ? TIKU_SP_BACKEND_HELIUM
                                          : TIKU_SP_BACKEND_SCALAR;
            tr = tiku_cmd_streq(argv[4], "ssram")  ? TIKU_SP_TIER_SSRAM
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
            /* cyc/elem printed as milli-units: the formatter has no floats
             * and the figure can be below 1. */
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
    if (tiku_cmd_streq(argv[1], "gpu") && argc >= 3) {
        static const char *const wn[TIKU_GPU_W_KIND_COUNT] = {
            "fill", "copy", "multiply", "scale", "lut", "reduce"
        };
        static const char *const pn[4] = {
            "LP-96", "HP1-192", "HP2-125", "HP3-250"
        };
        /* ---- power gpu off | on [perf] ---- */
        if (tiku_cmd_streq(argv[2], "off")) {
            tiku_gpu_deinit();
            SHELL_PRINTF("gpu off: powered %d\n", tiku_gpu_powered());
            return;
        }
        if (tiku_cmd_streq(argv[2], "on")) {
            unsigned perf = 0u;
            tiku_gpu_err_t rc;
            if (argc >= 4) {
                const char *q = argv[3];
                perf = 0u;
                while (*q >= '0' && *q <= '9') { perf = perf*10u + (unsigned)(*q++ - '0'); }
                if (perf > 3u) { perf = 0u; }
            }
            rc = tiku_gpu_init((tiku_gpu_perf_t)perf);
            /* The mode and rail printed are read back from the hardware. */
            {   /* CGCTRL's DISCLK* fields disable automatic clock gating; the
                 * driver never writes the register, so with them set at reset
                 * an idle GPU stays fully clocked. */
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
                /* CLKGEN's CLKCTRL gates the clock delivered to the GFX
                 * domain, independently of the GPU's own CGCTRL gating. */
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
        /* ---- power gpu ram <0..7> : SSRAMACTGFX ----
         * SSRAMACTGFX is 7 after boot: every SSRAM bank is held active while
         * the GFX domain is powered, through the MCU's WFI as well.  0
         * (NONE) leaves the banks in retention until an access wakes them
         * (datasheet 4.3.4).  The verb writes the field and prints it before
         * and after. */
        if (tiku_cmd_streq(argv[2], "ram") && argc >= 4) {
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
        if (tiku_cmd_streq(argv[2], "work") && argc >= 6) {
            unsigned k = TIKU_GPU_W_KIND_COUNT, i;
            uint32_t side = 0u, ms = 0u, us;
            const char *a = argv[4], *b = argv[5];
            /* "async" alone = batch 1 (one draw per list); "async <N>"
             * batches N draws into each submitted list. */
            int async = 0;
            if (argc >= 7 && tiku_cmd_streq(argv[6], "async")) {
                async = 1;
                if (argc >= 8) {
                    const char *q = argv[7]; int n = 0;
                    while (*q >= '0' && *q <= '9') { n = n*10 + (*q++ - '0'); }
                    if (n > 0) { async = n; }
                }
            }
            for (i = 0u; i < TIKU_GPU_W_KIND_COUNT; i++) {
                if (tiku_cmd_streq(argv[3], wn[i])) { k = i; }
            }
            while (*a >= '0' && *a <= '9') { side = side*10u + (uint32_t)(*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { ms   = ms*10u   + (uint32_t)(*b++ - '0'); }
            if (k == TIKU_GPU_W_KIND_COUNT || side == 0u || ms == 0u) {
                SHELL_PRINTF("Usage: power gpu work <fill|copy|multiply|scale|"
                             "lut|reduce> <side> <ms> [async]\n");
                return;
            }
            if (async && k != TIKU_GPU_W_FILL) {
                SHELL_PRINTF("gpu work: async is supported only for fill\n");
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
        if (tiku_cmd_streq(argv[2], "cpu") && argc >= 6) {
            uint32_t side = 0u, ms = 0u, us;
            unsigned k = tiku_cmd_streq(argv[3], "copy") ? TIKU_GPU_CPU_COPY
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
        if (tiku_cmd_streq(argv[2], "contend") && argc >= 5) {
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
    SHELL_PRINTF("Usage: power [floor | dev <name> on|off | buck | clock"
                 " | cache on|off | stimer [kill] | reclock lfrc|xtal"
                 " | idle|spin <ms> [flags] | mem <kind> <ms>"
                 " | cpdlp [elp|clp <0-3>]"
#if (TIKU_DRV_USB_ENABLE + 0)
                 " | usb ..."
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
                 " | emmc ..."
#endif
#if (TIKU_DRV_NOR_ENABLE + 0)
                 " | nor ..."
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
                 " | psram ..."
#endif
#if (TIKU_AMBIQ_POWER_PROBE_SIMD + 0)
                 " | simd ..."
#endif
#if (TIKU_AMBIQ_POWER_PROBE_GPU + 0)
                 " | gpu ..."
#endif
                 "]\n");
    return;
#endif

    /* A build with no verb branch above accepts only a bare `power`. */
    SHELL_PRINTF("Usage: power\n");
}

#endif /* TIKU_SHELL_CMD_POWER */
