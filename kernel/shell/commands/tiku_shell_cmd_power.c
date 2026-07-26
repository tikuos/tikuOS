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
    if (streq(argv[1], "idle") && argc >= 3) {
        /* power idle <ms> [pll] [uart] [hfxo] -- sit in WFI with the named
         * standing clock requests released, so an external instrument can
         * attribute the idle floor instead of the operator guessing. */
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
        }
        if (ms == 0u) { SHELL_PRINTF("Usage: power idle <ms> [pll] [uart] [hfxo]\n"); return; }
        SHELL_PRINTF("idle %lu ms flags%s%s%s%s -- starting\n", (unsigned long)ms,
                     (flags & TIKU_SLEEP_STOP_PLL)  ? " pll"  : "",
                     (flags & TIKU_SLEEP_STOP_UART) ? " uart" : "",
                     (flags & TIKU_SLEEP_STOP_HFXO) ? " hfxo" : "",
                     (flags & TIKU_SLEEP_DEEP)      ? " deep" : "");
        us = tiku_nordic_sleep_probe(ms, flags);
        SHELL_PRINTF("idle done %lu us wakes %lu (%lu/s)\n",
                     (unsigned long)us,
                     (unsigned long)tiku_nordic_sleep_wake_count(),
                     (unsigned long)(us ? (uint32_t)(((uint64_t)
                        tiku_nordic_sleep_wake_count() * 1000000u) / us) : 0u));
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
    if (streq(argv[1], "spin") && argc >= 3) {
        uint32_t ms = 0u, us;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        if (ms == 0u) { SHELL_PRINTF("Usage: power spin <ms>\n"); return; }
        SHELL_PRINTF("spin %lu ms -- starting\n", (unsigned long)ms);
        us = tiku_nordic_spin_probe(ms);
        SHELL_PRINTF("spin done %lu us\n", (unsigned long)us);
        return;
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

    SHELL_PRINTF("Usage: power [cache on|off | dcdc on|off | bench | clock | probe | stat | clear]\n");
}

#endif /* TIKU_SHELL_CMD_POWER */
