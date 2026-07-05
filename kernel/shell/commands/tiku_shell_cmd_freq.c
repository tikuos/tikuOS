/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_freq.c - "freq" command: show or set the CPU core frequency.
 *
 *   freq          -- print the current core clock in MHz
 *   freq <mhz>    -- request a core frequency (e.g. 96, or 192 for Ambiq turbo)
 *
 * Setting drives the platform tiku_cpu_freq_init() path: on MSP430 it
 * reconfigures the DCO; on the Ambiq parts it selects the Low-Power / High-
 * Performance perf mode. A request the platform can't honour leaves the clock
 * unchanged and is reported back. (A runtime change can affect peripherals
 * whose clock derives from the core on some parts.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_freq.h"
#include <kernel/shell/tiku_shell.h>   /* SHELL_PRINTF */
#include <hal/tiku_cpu.h>              /* tiku_cpu_freq_init / tiku_cpu_mclk_hz */
#include <string.h>                    /* strcmp ("probe" subcommand)          */

#if defined(PLATFORM_AMBIQ) && defined(AM_PART_APOLLO510)
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h>   /* HP identity probe */

/* "freq probe" -- dump the silicon/trim/power identity that decides whether
 * (and how) High-Performance mode can be enabled on this exact chip. Read-only:
 * changes no clock, voltage, or perf mode. */
static void
freq_cmd_probe(void)
{
    static const char *const buck[4] = { "OFF", "init", "LP", "ACT" };
    tiku_ambiq_hp_probe_t p;
    unsigned maj, min, st;
    unsigned i;

    tiku_cpu_freq_ambiq_hp_probe(&p);

    maj = (unsigned)((p.chiprev >> 4) & 0xFu);   /* 1='A' 2='B' */
    min = (unsigned)(p.chiprev & 0xFu);          /* 1=rev0 2=rev1 3=rev2 */
    SHELL_PRINTF("Apollo510 HP identity probe (read-only):\n");
    SHELL_PRINTF("  CHIPREV     0x%08lx  rev %c%u\n",
                 (unsigned long)p.chiprev,
                 (maj == 2u) ? 'B' : ((maj == 1u) ? 'A' : '?'),
                 (min > 0u) ? (min - 1u) : 9u);
    SHELL_PRINTF("  INFO1       %s (SHADOWVALID=0x%08lx)%s\n",
                 p.info1_in_otp ? "OTP" : "MRAM shadow",
                 (unsigned long)p.shadowvalid,
                 p.info1_ok ? "" : "  [READ FAILED]");
    SHELL_PRINTF("  TRIM_REV    0x%08lx  (B2: 0=PCM2.0 1=PCM2.1 2=PCM2.2)\n",
                 (unsigned long)p.trim_rev);
    SHELL_PRINTF("  PGM_INFO    0x%08lx  (TrimSubRev=%lu)\n",
                 (unsigned long)p.pgm_info,
                 (unsigned long)(p.pgm_info & 0xFFu));
    SHELL_PRINTF("  PATCH_TRK0  0x%08lx  (bit0=UCRG patch)\n",
                 (unsigned long)p.patch_tracker0);
    st = (unsigned)((p.vrstatus >> 4) & 3u);
    SHELL_PRINTF("  SIMOBUCK    %s (VRSTATUS=0x%08lx; HP needs ACT)\n",
                 buck[st], (unsigned long)p.vrstatus);
    SHELL_PRINTF("  MCUPERFREQ  0x%08lx  (mode: %s)\n",
                 (unsigned long)p.mcuperfreq,
                 (((p.mcuperfreq >> 3) & 3u) == 2u) ? "HP" : "LP");
    SHELL_PRINTF("  MEASURED    %lu Hz core clock (SysTick vs 32 kHz XT)\n",
                 tiku_cpu_freq_ambiq_measured_hz());
    SHELL_PRINTF("  POWERSTATE  trim table (INFO1 0x970..):\n");
    for (i = 0u; i < 20u; i += 4u) {
        SHELL_PRINTF("    [%2u] %08lx %08lx %08lx %08lx\n", i,
                     (unsigned long)p.powerstate[i],
                     (unsigned long)p.powerstate[i + 1u],
                     (unsigned long)p.powerstate[i + 2u],
                     (unsigned long)p.powerstate[i + 3u]);
    }
}
#endif /* PLATFORM_AMBIQ && AM_PART_APOLLO510 */

void
tiku_shell_cmd_freq(uint8_t argc, const char *argv[])
{
    const char   *p;
    unsigned long req;
    unsigned long now;

    if (argc < 2) {
        SHELL_PRINTF("CPU: %lu MHz\n", tiku_cpu_mclk_hz() / 1000000UL);
        return;
    }

#if defined(PLATFORM_AMBIQ) && defined(AM_PART_APOLLO510)
    if (strcmp(argv[1], "probe") == 0) {
        freq_cmd_probe();
        return;
    }
#endif

    /* Parse the requested core frequency in MHz (decimal). */
    p   = argv[1];
    req = 0u;
    while (*p >= '0' && *p <= '9') {
        req = req * 10u + (unsigned long)(*p - '0');
        p++;
    }
    if (*p != '\0' || req == 0u) {
        SHELL_PRINTF("Usage: freq [<mhz>]\n");
        SHELL_PRINTF("  no arg: show the core clock; <mhz>: request a frequency "
                     "(96, or turbo: 192 on Apollo4, 250 on Apollo510).\n");
        return;
    }

    tiku_cpu_freq_init((unsigned int)req);
    now = tiku_cpu_mclk_hz() / 1000000UL;
    if (now == req) {
        SHELL_PRINTF("CPU: %lu MHz\n", now);
    } else {
        SHELL_PRINTF("CPU: %lu MHz (requested %lu not applied)\n", now, req);
    }
}
