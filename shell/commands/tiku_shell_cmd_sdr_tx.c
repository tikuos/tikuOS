/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_shell_cmd_sdr_tx.c - TX capability, sequential IQ upload and bursts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_cmd_sdr_tx.h"
#include <shell/tiku_shell_io.h>
#include <string.h>
#if (TIKU_DRV_SDR_ESP_TX_ENABLE + 0)
#include <drivers/wifi/esp/tiku_drv_sdr_tx_esp.h>

/** @brief Parse a bounded decimal integer without signs or trailing text. */
static int decimal(const char *s, uint32_t limit, uint32_t *out)
{
    uint32_t value = 0;
    if (!s || !*s) { return 0; }
    for (; *s; s++) {
        unsigned digit = (unsigned char)*s - '0';
        if (digit > 9 || digit > limit || value > (limit - digit) / 10u) { return 0; }
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

/** @brief Parse one packed IQ10 word written as eight hexadecimal digits. */
static int sample(const char *s, uint32_t *out)
{
    uint32_t value = 0;
    unsigned n;
    if (!s || strlen(s) != 8) { return 0; }
    for (n = 0; n < 8; n++) {
        unsigned c = (unsigned char)s[n], digit;
        if (c >= '0' && c <= '9') { digit = c - '0'; }
        else if (c >= 'a' && c <= 'f') { digit = c - 'a' + 10; }
        else if (c >= 'A' && c <= 'F') { digit = c - 'A' + 10; }
        else { return 0; }
        value = (value << 4) | digit;
    }
    if (value >> 20) { return 0; }
    *out = value;
    return 1;
}

/** @brief Print the sequential upload's accepted and expected sample counts. */
static void progress(void)
{
    uint32_t written, total;
    tiku_drv_sdr_tx_esp_status(&written, &total);
    SHELL_PRINTF("TX load %lu %lu\n", (unsigned long)written, (unsigned long)total);
}

/** @brief Report that waveform playback has started on the hardware. */
static void started(void *context)
{
    const uint32_t *v = context;
    SHELL_PRINTF("TX active %lu %lu %lu %lu\n", (unsigned long)v[0],
                 (unsigned long)TIKU_SDR_TX_HZ, (unsigned long)v[1], (unsigned long)v[2]);
}
#endif

void tiku_shell_cmd_sdr_tx(uint8_t argc, const char *argv[])
{
    if (argc == 3 && !strcmp(argv[2], "info")) {
#if (TIKU_DRV_SDR_ESP_TX_ENABLE + 0)
        const tiku_sdr_tx_band_t *bands;
        unsigned count = tiku_drv_sdr_tx_esp_bands(&bands), n;
        SHELL_PRINTF("TXCAP 1 %lu %u %u %u 2412 2462 5 %u\n",
                     (unsigned long)TIKU_SDR_TX_HZ, TIKU_SDR_TX_WORDS_MAX,
                     TIKU_SDR_TX_ALIGN, TIKU_SDR_TX_MS_MAX, TIKU_SDR_TX_CHUNK);
        SHELL_PRINTF("TXBANDS 1 %u", count);
        for (n = 0; n < count; n++) {
            SHELL_PRINTF(" %lu %lu %lu", (unsigned long)bands[n].low_mhz,
                         (unsigned long)bands[n].high_mhz,
                         (unsigned long)bands[n].step_mhz);
        }
        SHELL_PRINTF("\n");
#else
        SHELL_PRINTF("TXCAP 0\n");
#endif
        return;
    }
#if (TIKU_DRV_SDR_ESP_TX_ENABLE + 0)
    if (argc >= 3) {
        uint32_t a, b, values[TIKU_SDR_TX_CHUNK];
        int rc;
        if (argc == 3 && !strcmp(argv[2], "status")) { progress(); return; }
        if (argc == 3 && !strcmp(argv[2], "clear")) {
            rc = tiku_drv_sdr_tx_esp_clear();
            if (!rc) { SHELL_PRINTF("TX cleared\n"); return; }
        } else if (argc == 4 && !strcmp(argv[2], "begin") &&
                   decimal(argv[3], TIKU_SDR_TX_WORDS_MAX, &a)) {
            rc = tiku_drv_sdr_tx_esp_begin(a);
        } else if (argc >= 5 && argc <= 4 + TIKU_SDR_TX_CHUNK &&
                   !strcmp(argv[2], "put") && decimal(argv[3], TIKU_SDR_TX_WORDS_MAX, &a)) {
            unsigned n;
            for (n = 4; n < argc; n++) {
                if (!sample(argv[n], &values[n - 4])) { goto arguments; }
            }
            rc = tiku_drv_sdr_tx_esp_write(a, values, argc - 4);
        } else if (argc == 5 && !strcmp(argv[2], "tone") &&
                   decimal(argv[3] + (argv[3][0] == '-'), 128, &a) &&
                   decimal(argv[4], 128, &b)) {
            rc = tiku_drv_sdr_tx_esp_tone(argv[3][0] == '-' ? -(int)a : (int)a, b);
        } else if (argc == 5 && !strcmp(argv[2], "send") &&
                   decimal(argv[3], UINT32_MAX, &a) && decimal(argv[4], TIKU_SDR_TX_MS_MAX, &b)) {
            tiku_sdr_tx_result_t result;
            uint32_t context[3] = {a, 0, b};
            tiku_drv_sdr_tx_esp_status(&context[1], NULL);
            rc = tiku_drv_sdr_tx_esp_send(a, b, started, context, &result);
            SHELL_PRINTF("TX done %d %lu %lu %lu\n", rc,
                         (unsigned long)result.elapsed_us, (unsigned long)result.hz,
                         (unsigned long)result.words);
            return;
        } else { goto arguments; }
        if (rc) { SHELL_PRINTF("ERR TX %d\n", rc); }
        else { progress(); }
        return;
    }
arguments:
    SHELL_PRINTF("ERR TX arguments\n");
#else
    SHELL_PRINTF("ERR TX unsupported\n");
#endif
}
