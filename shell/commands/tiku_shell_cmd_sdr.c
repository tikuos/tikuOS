/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sdr.c - "sdr" command: the radio as a receiver.
 *
 * `start` reserves the capture bank and powers the radio up; `stop` restores
 * the default gain hold and frees the bank. C5 also powers its exclusive
 * PHY off; C61 leaves Wi-Fi up. `spec` and `sweep` print TikuSDR's SPEC lines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "tiku_shell_cmd_sdr.h"
#include <shell/tiku_shell.h>
#include <interfaces/wireless/tiku_wireless.h>
#include <drivers/wifi/esp/tiku_drv_sdr_esp.h>
#if defined(PLATFORM_ESP32C5)
#include <drivers/wifi/esp/c5/sdr_c5.h>
#endif

/** @brief Print the usage. */
static void sdr_help(void)
{
    SHELL_PRINTF("usage: sdr start | stop | info | bands | spec <MHz> [rate] [nfft]"
                 " | sweep <lo> <hi> <step> [rate] [nfft]\n"
                 "       sdr gain [auto | <index>] | reserve | release |"
                 " cap <MHz> [rate 0-5] [words] [reps] | scan |"
                 " hex <offset> <count>\n"
                 "  rate: 0 80, 1 40, 2 20, 3 10, 4 8, 5 4 MS/s; words up to"
                 " %lu\n", (unsigned long)TIKU_DRV_SDR_ESP_WORDS_MAX);
}

/** @brief Parse decimal digits within an inclusive bound; refuse signs and overflow. */
static int decimal(const char *text, uint32_t maximum, uint32_t *value)
{
    uint32_t n = 0;
    if (!text || !*text) { return 0; }
    do {
        unsigned digit = (unsigned)(*text++ - '0');
        if (digit > 9 || digit > maximum || n > (maximum - digit) / 10) { return 0; }
        n = n * 10 + digit;
    } while (*text);
    *value = n;
    return 1;
}

/** @brief Validate numeric command arguments before narrowing their types. */
static int arguments(uint8_t argc, const char *argv[])
{
    uint32_t bounds[5] = {UINT32_MAX, 5, 256, 64, 256}, values[5];
    unsigned minimum = 2, maximum = 2, i;
    const char *verb = argv[1];
    if (!strcmp(verb, "gain")) {
        if (argc == 2 || (argc == 3 && !strcmp(argv[2], "auto"))) { return 1; }
        minimum = maximum = 3;
        bounds[0] = 255;
    } else if (!strcmp(verb, "cap")) {
        minimum = 3; maximum = 6; bounds[2] = TIKU_DRV_SDR_ESP_WORDS_MAX;
    } else if (!strcmp(verb, "spec")) {
        minimum = 3; maximum = 5;
    } else if (!strcmp(verb, "sweep")) {
        minimum = 5; maximum = 7;
        bounds[1] = bounds[2] = UINT32_MAX; bounds[3] = 5;
    } else if (!strcmp(verb, "hex")) {
        minimum = maximum = 4;
        bounds[0] = TIKU_DRV_SDR_ESP_WORDS_MAX - 1;
        bounds[1] = TIKU_DRV_SDR_ESP_WORDS_MAX;
    }
    if (argc < minimum || argc > maximum) { return 0; }
    for (i = 2; i < argc; i++) {
        if (!decimal(argv[i], bounds[i - 2], &values[i - 2])) { return 0; }
    }
    if (!strcmp(verb, "cap") && ((argc >= 5 && !values[2]) ||
                                  (argc >= 6 && !values[3]))) { return 0; }
    if (!strcmp(verb, "hex") && (!values[1] ||
        values[1] > TIKU_DRV_SDR_ESP_WORDS_MAX - values[0])) { return 0; }
    return 1;
}

/** @brief Bar of @p v on a log scale: one mark per factor of two. */
static void bar(uint32_t v)
{
    unsigned n = 0U;

    while (v > 1UL) {
        v >>= 1;
        n++;
    }
    while (n-- > 0U) {
        SHELL_PRINTF("#");
    }
    SHELL_PRINTF("\n");
}

/** @brief Print a failed capture's return code and its likely cause. */
static void sdr_failed(int rc)
{
    SHELL_PRINTF("sdr: capture failed (%d): %s\n", rc,
                 rc == -1 ? "run sdr start; check arguments and radio ownership"
                 : rc == -2 ? "the dump unit never reported done"
                            : "incomplete/constant samples or damaged guard");
}

/** @brief The quietest and loudest slices of @p r. */
static void sdr_range(const tiku_drv_sdr_esp_result_t *r, uint32_t *lo,
                      uint32_t *hi)
{
    unsigned s;

    *lo = *hi = r->slice[0];
    for (s = 1U; s < 16U; s++) {
        if (r->slice[s] < *lo) *lo = r->slice[s];
        if (r->slice[s] > *hi) *hi = r->slice[s];
    }
}

/** @brief One line: a snapshot's gain, level and quiet/loud spread, marked
 *         "burst" when the loudest slice exceeds three times the quietest. */
static void sdr_line(uint32_t mhz, const tiku_drv_sdr_esp_result_t *r)
{
    uint32_t lo, hi;

    sdr_range(r, &lo, &hi);
    SHELL_PRINTF("  %4lu MHz gain %2u rms %3u  slices %lu..%lu%s\n",
                 (unsigned long)mhz, (unsigned)r->gain, (unsigned)r->rms,
                 (unsigned long)lo, (unsigned long)hi,
                 hi > 3UL * lo ? "  burst" : "");
}

/**
 * @brief `sdr cap <MHz> [rate] [words] [reps]`: one snapshot in detail, or a
 *        summary line per snapshot when reps > 1.
 */
static void sdr_cap(uint8_t argc, const char *argv[])
{
    tiku_drv_sdr_esp_result_t r;
    uint32_t mhz = (uint32_t)strtoul(argv[2], NULL, 10);
    uint8_t rate = argc >= 4 ? (uint8_t)strtoul(argv[3], NULL, 10) : 5U;
    uint32_t words = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 10)
                               : TIKU_DRV_SDR_ESP_WORDS_MAX;
    uint32_t reps = argc >= 6 ? (uint32_t)strtoul(argv[5], NULL, 10) : 1U;
    unsigned s;
    int rc;

    if (reps > 1U) {
        while (reps-- > 0U) {
            rc = tiku_drv_sdr_esp_capture(mhz, rate, words, &r);
            if (rc != 0) {
                sdr_failed(rc);
                return;
            }
            sdr_line(mhz, &r);
        }
        return;
    }
    rc = tiku_drv_sdr_esp_capture(mhz, rate, words, &r);
    if (rc != 0) {
        sdr_failed(rc);
        return;
    }
    SHELL_PRINTF("sdr: %lu MHz, %lu samples at %lu Hz in %lu us, gain %u\n",
                 (unsigned long)mhz, (unsigned long)r.words,
                 (unsigned long)r.hz, (unsigned long)r.us, (unsigned)r.gain);
    SHELL_PRINTF("  dc I %d Q %d, rms %u, peak %u\n", (int)r.dc_i,
                 (int)r.dc_q, (unsigned)r.rms, (unsigned)r.peak);
    SHELL_PRINTF("  power by time (16 slices):\n");
    for (s = 0U; s < 16U; s++) {
        SHELL_PRINTF("  %2u %8lu ", s, (unsigned long)r.slice[s]);
        bar(r.slice[s]);
    }
    SHELL_PRINTF("  power by frequency in slice %u (%lu kHz bins, low to "
                 "high):\n", (unsigned)r.loud,
                 (unsigned long)(r.hz / TIKU_DRV_SDR_ESP_BINS / 1000UL));
    for (s = 0U; s < TIKU_DRV_SDR_ESP_BINS; s++) {
        SHELL_PRINTF("  %2u %8lu ", s, (unsigned long)r.bin[s]);
        bar(r.bin[s]);
    }
}

/** @brief `sdr hex <offset> <count>`: dump raw sample words. */
static void sdr_hex(const char *argv[])
{
    const uint32_t *w = tiku_drv_sdr_esp_samples();
    uint32_t off = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t n = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t j;

    if (w == NULL) {
        SHELL_PRINTF("sdr: no bank reserved\n");
        return;
    }
    if (off >= TIKU_DRV_SDR_ESP_WORDS_MAX) { SHELL_PRINTF("ERR hex range\n"); return; }
    for (j = 0U; j < n && j < TIKU_DRV_SDR_ESP_WORDS_MAX - off; j++) {
        SHELL_PRINTF("%lx%s", (unsigned long)w[off + j],
                     (j % 8U == 7U) ? "\n" : " ");
    }
    SHELL_PRINTF("\n");
}

/** @brief Every 2.4 GHz channel in turn: one 4 MS/s snapshot each. */
static void sdr_scan(void)
{
    tiku_drv_sdr_esp_result_t r;
    uint32_t mhz;
    int rc;

    for (mhz = 2412UL; mhz <= 2484UL; mhz += (mhz == 2472UL) ? 12UL : 5UL) {
        rc = tiku_drv_sdr_esp_capture(mhz, 5U, TIKU_DRV_SDR_ESP_WORDS_MAX, &r);
        if (rc != 0) {
            sdr_failed(rc);
            return;
        }
        sdr_line(mhz, &r);
    }
}

/*---------------------------------------------------------------------------*/
/* SPECTRUM LINES                                                            */
/*---------------------------------------------------------------------------*/

/** @brief "SPEC <MHz> <Hz> <gain> <nfft> <hex bins>[ held]": one bin a
 *         byte, half-decibels, low to high, printed 64 hex digits at a
 *         time; the line ends in " held" while a gain index is held.
 *  @return 0, or the spectrum call's error after printing "ERR spec" */
static int sdr_spec_line(uint32_t mhz, uint8_t rate, unsigned nfft)
{
    static const char hexd[] = "0123456789abcdef";
    uint8_t db[256], gain = 0U;
    char chunk[65];
    unsigned j, n = 0U;
    int rc = tiku_drv_sdr_esp_spectrum(mhz, rate, nfft, db, &gain);

    if (rc != 0) {
        SHELL_PRINTF("ERR spec %d\n", rc);
        return rc;
    }
    SHELL_PRINTF("SPEC %lu %lu %u %u ", (unsigned long)mhz,
                 (unsigned long)tiku_drv_sdr_esp_rate_hz(rate), (unsigned)gain,
                 nfft);
    for (j = 0U; j < nfft; j++) {
        chunk[n++] = hexd[db[j] >> 4];
        chunk[n++] = hexd[db[j] & 15U];
        if (n == 64U || j + 1U == nfft) {
            chunk[n] = '\0';
            SHELL_PRINTF("%s", chunk);
            n = 0U;
        }
    }
    SHELL_PRINTF(tiku_drv_sdr_esp_held() >= 0 ? " held\n" : "\n");
    return 0;
}

/** @brief Reserve the capture bank, then power the radio up.  The radio's
 *         heap must not overlap the bank, so a radio that is up is powered
 *         down before the bank is reserved. */
static void sdr_start(void)
{
#if defined(PLATFORM_ESP32C5)
    int reserved = tiku_drv_sdr_esp_reserved();
    if (tiku_drv_sdr_esp_reserve() != 0) { SHELL_PRINTF("ERR start bank\n"); return; }
    if (tiku_sdr_c5_power(1) != 0) {
        if (!reserved) { tiku_drv_sdr_esp_release(); }
        SHELL_PRINTF("ERR start radio (busy or unavailable)\n");
        return;
    }
#else
    if (!tiku_drv_sdr_esp_reserved()) {
        (void)tiku_wireless_power(0U);
        if (tiku_drv_sdr_esp_reserve() != 0) {
            SHELL_PRINTF("ERR start bank\n");
            return;
        }
    }
    if (tiku_wireless_power(1U) != 0) {
        SHELL_PRINTF("ERR start radio\n");
        return;
    }
#endif
    SHELL_PRINTF("SDR ready\n");
}

/** @brief One machine line: bank state, rates, FFT sizes, tuning range. */
static void sdr_info(void)
{
    SHELL_PRINTF("SDR bank %s rates 80 40 20 10 8 4 nfft 64 128 256 "
#if defined(PLATFORM_ESP32C5)
                 "tune 2400 2500\n",
#else
                 "tune 2200 2700\n",
#endif
                 tiku_drv_sdr_esp_reserved() ? "yes" : "no");
}

/** @brief `sdr sweep <lo> <hi> <step> [rate] [nfft]`: a SPEC line a step, at
 *         most 65 steps, then "SWEEP <n>". */
static void sdr_sweep(uint8_t argc, const char *argv[])
{
    uint32_t lo = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t hi = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t step = (uint32_t)strtoul(argv[4], NULL, 10);
    uint8_t rate = argc >= 6 ? (uint8_t)strtoul(argv[5], NULL, 10) : 1U;
    unsigned nfft = argc >= 7 ? (unsigned)strtoul(argv[6], NULL, 10) : 128U;
    uint32_t mhz, n = 0UL;

    if (step == 0UL || hi < lo || (hi - lo) / step > 64UL) {
        SHELL_PRINTF("ERR sweep range\n");
        return;
    }
    for (mhz = lo; ; mhz += step) {
        if (sdr_spec_line(mhz, rate, nfft) != 0) {
            return;
        }
        n++;
        if (hi - mhz < step) { break; }
    }
    SHELL_PRINTF("SWEEP %lu\n", (unsigned long)n);
}

/** @brief `sdr gain [auto | <index>]`: the gain index captures are held at,
 *         or the AGC's; with no word, say which. */
static void sdr_gain(uint8_t argc, const char *argv[])
{
    if (argc >= 3) {
        tiku_drv_sdr_esp_hold(strcmp(argv[2], "auto") == 0 ? -1
                              : (int)strtoul(argv[2], NULL, 10));
    }
    if (tiku_drv_sdr_esp_held() < 0) {
        SHELL_PRINTF("SDR gain auto\n");
    } else {
        SHELL_PRINTF("SDR gain %d\n", tiku_drv_sdr_esp_held());
    }
}

#if TIKU_DRV_SDR_ESP_PROBE
/** @brief `sdr rd <hexaddr> [n]`: print n (default 1) 32-bit words read
 *         from a raw address, four a line. */
static void sdr_rd(uint8_t argc, const char *argv[])
{
    uintptr_t a = (uintptr_t)strtoul(argv[2], NULL, 16);
    uint32_t n = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 10) : 1U, j;

    for (j = 0U; j < n; j++) {
        if (j % 4U == 0U) {
            SHELL_PRINTF("%s%lx:", j ? "\n" : "", (unsigned long)(a + 4U * j));
        }
        SHELL_PRINTF(" %08lx", (unsigned long)*(volatile uint32_t *)(a + 4U * j));
    }
    SHELL_PRINTF("\n");
}

/** @brief `sdr wr <hexaddr> <hexval>`: write a raw word and read it back. */
static void sdr_wr(const char *argv[])
{
    uintptr_t a = (uintptr_t)strtoul(argv[2], NULL, 16);
    uint32_t v = (uint32_t)strtoul(argv[3], NULL, 16);

    *(volatile uint32_t *)a = v;
    SHELL_PRINTF("%lx: %08lx\n", (unsigned long)a,
                 (unsigned long)*(volatile uint32_t *)a);
}

/** @brief Run a probe verb (tone, lb, txcal, src, pwr, nco, fsk, hear, rd or
 *         wr), built with TIKU_DRV_SDR_ESP_PROBE=1.
 *  @return 1 when @p argv[1] named one and @p argc reached that verb's
 *          minimum, else 0 */
static int sdr_probe(uint8_t argc, const char *argv[])
{
    if (strcmp(argv[1], "tone") == 0 && argc >= 3) {
        /* tone off | tone <xpd_a> <xpd_b> <pwr> <step> <gain> */
        if (strcmp(argv[2], "off") == 0) {
            SHELL_PRINTF("sdr: tone off (%d)\n",
                         tiku_drv_sdr_esp_tone(0, 0U, 0U, 0U, 0U, 0));
        } else if (argc >= 7) {
            SHELL_PRINTF("sdr: tone on (%d)\n", tiku_drv_sdr_esp_tone(1,
                (unsigned)strtoul(argv[2], NULL, 0),
                (unsigned)strtoul(argv[3], NULL, 0),
                (unsigned)strtoul(argv[4], NULL, 0),
                (unsigned)strtoul(argv[5], NULL, 0),
                (int)strtol(argv[6], NULL, 0)));
        } else {
            sdr_help();
        }
    } else if (strcmp(argv[1], "lb") == 0) {
        /* lb off | lb <a> <b> <c> */
        if (argc < 3 || (strcmp(argv[2], "off") != 0 && argc < 5)) {
            SHELL_PRINTF("usage: sdr lb off | lb <a> <b> <c>\n");
            return 1;
        }
        if (strcmp(argv[2], "off") == 0) {
            tiku_drv_sdr_esp_loopback(0, 0U, 0U, 0U);
        } else if (argc >= 5) {
            tiku_drv_sdr_esp_loopback(1, (unsigned)strtoul(argv[2], NULL, 0),
                                      (unsigned)strtoul(argv[3], NULL, 0),
                                      (unsigned)strtoul(argv[4], NULL, 0));
        }
        SHELL_PRINTF("sdr: loopback %s\n", argv[2]);
    } else if (strcmp(argv[1], "txcal") == 0 && argc >= 3) {
        tiku_drv_sdr_esp_txcal(strcmp(argv[2], "off") != 0);
        SHELL_PRINTF("sdr: txcal %s\n", argv[2]);
    } else if (strcmp(argv[1], "src") == 0 && argc >= 3) {
        tiku_drv_sdr_esp_source((uint8_t)strtoul(argv[2], NULL, 0));
        SHELL_PRINTF("sdr: source %s\n", argv[2]);
    } else if (strcmp(argv[1], "pwr") == 0 && argc >= 4) {
        /* pwr <tone 0|1> <gain> [sel] */
        SHELL_PRINTF("sdr: power %d\n", tiku_drv_sdr_esp_power_db(
            (int)strtol(argv[2], NULL, 0), (unsigned)strtoul(argv[3], NULL, 0),
            argc >= 5 ? (unsigned)strtoul(argv[4], NULL, 0) : 0U));
    } else if (strcmp(argv[1], "nco") == 0) {
        /* nco off | nco <step> <gain> */
        if (argc < 3 || (strcmp(argv[2], "off") != 0 && argc < 4)) {
            SHELL_PRINTF("usage: sdr nco off | nco <step> <gain>\n");
            return 1;
        }
        if (strcmp(argv[2], "off") == 0) {
            SHELL_PRINTF("sdr: nco off (%d)\n", tiku_drv_sdr_esp_nco(0, 0U, 0));
        } else if (argc >= 4) {
            SHELL_PRINTF("sdr: nco on (%d)\n", tiku_drv_sdr_esp_nco(1,
                (unsigned)strtoul(argv[2], NULL, 0),
                (int)strtol(argv[3], NULL, 0)));
        }
    } else if (strcmp(argv[1], "fsk") == 0 && argc >= 7) {
        /* fsk <MHz> <rate> <stepA> <stepB> <half-cycles>: one capture while
         * tone 1's step switches between stepA and stepB every half-cycles
         * core cycles */
        tiku_drv_sdr_esp_result_t r;
        int rc = tiku_drv_sdr_esp_fsk((uint32_t)strtoul(argv[2], NULL, 10),
            (uint8_t)strtoul(argv[3], NULL, 10), TIKU_DRV_SDR_ESP_WORDS_MAX,
            (unsigned)strtoul(argv[4], NULL, 0), (unsigned)strtoul(argv[5], NULL, 0),
            (uint32_t)strtoul(argv[6], NULL, 10), &r);

        if (rc != 0) {
            sdr_failed(rc);
        } else {
            sdr_line((uint32_t)strtoul(argv[2], NULL, 10), &r);
        }
    } else if (strcmp(argv[1], "hear") == 0 && argc >= 3) {
        tiku_drv_sdr_esp_hear(strcmp(argv[2], "off") != 0);
        SHELL_PRINTF("sdr: hear %s\n", argv[2]);
    } else if (strcmp(argv[1], "rd") == 0 && argc >= 3) {
        sdr_rd(argc, argv);
    } else if (strcmp(argv[1], "wr") == 0 && argc >= 4) {
        sdr_wr(argv);
    } else {
        return 0;
    }
    return 1;
}
#endif

void tiku_shell_cmd_sdr(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        sdr_help();
#if TIKU_DRV_SDR_ESP_PROBE
    } else if (sdr_probe(argc, argv)) {
        /* a probe verb ran */
#endif
    } else if (!arguments(argc, argv)) {
        SHELL_PRINTF("ERR arguments\n");
    } else if (strcmp(argv[1], "scan") == 0) {
        sdr_scan();
    } else if (strcmp(argv[1], "start") == 0) {
        sdr_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        tiku_drv_sdr_esp_hold(TIKU_DRV_SDR_ESP_HOLD);
        tiku_drv_sdr_esp_release();
        SHELL_PRINTF(tiku_drv_sdr_esp_reserved() ? "ERR stop busy\n" : "SDR stopped\n");
    } else if (strcmp(argv[1], "info") == 0) {
        sdr_info();
    } else if (strcmp(argv[1], "bands") == 0) {
#if defined(PLATFORM_ESP32C5)
        SHELL_PRINTF("SDR bands 2400 2500 4900 5900 bank_bytes 131072\n");
#else
        SHELL_PRINTF("SDR bands 2200 2700 bank_bytes 65536\n");
#endif
    } else if (strcmp(argv[1], "gain") == 0) {
        sdr_gain(argc, argv);
    } else if (strcmp(argv[1], "spec") == 0 && argc >= 3) {
        (void)sdr_spec_line((uint32_t)strtoul(argv[2], NULL, 10),
            argc >= 4 ? (uint8_t)strtoul(argv[3], NULL, 10) : 1U,
            argc >= 5 ? (unsigned)strtoul(argv[4], NULL, 10) : 256U);
    } else if (strcmp(argv[1], "sweep") == 0 && argc >= 5) {
        sdr_sweep(argc, argv);
    } else if (strcmp(argv[1], "reserve") == 0) {
        SHELL_PRINTF(tiku_drv_sdr_esp_reserve() == 0 ? "sdr: bank reserved\n"
                                                     : "sdr: no bank\n");
    } else if (strcmp(argv[1], "release") == 0) {
        tiku_drv_sdr_esp_release();
        SHELL_PRINTF(tiku_drv_sdr_esp_reserved() ? "ERR release busy\n" : "sdr: bank released\n");
    } else if (strcmp(argv[1], "cap") == 0 && argc >= 3) {
        sdr_cap(argc, argv);
    } else if (strcmp(argv[1], "hex") == 0 && argc >= 4) {
        sdr_hex(argv);
    } else {
        sdr_help();
    }
}
