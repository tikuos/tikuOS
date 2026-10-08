/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_adc.c - "adc" command implementation
 *
 * Reads one channel through the platform-independent ADC HAL.  The ADC is
 * opened for each read and closed after it, so it is off between commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_adc.h"
#include <shell/tiku_shell.h>
#include <interfaces/adc/tiku_adc.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief 1 if strings @p a and @p b are equal, else 0. */
static uint8_t
streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/** @brief Parse decimal @p s (0-255) into *out; 0 on success, else -1. */
static int8_t
parse_u8(const char *s, uint8_t *out)
{
    uint16_t val = 0;
    if (s[0] == '\0') {
        return -1;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        val = val * 10 + (*s - '0');
        if (val > 255) {
            return -1;
        }
        s++;
    }
    *out = (uint8_t)val;
    return 0;
}

/** @brief Map a reference name to TIKU_ADC_REF_*; 0 on success, else -1. */
static int8_t
parse_ref(const char *s, uint8_t *out)
{
    if (streq(s, "avcc")) {
        *out = TIKU_ADC_REF_AVCC;
    } else if (streq(s, "1v2")) {
        *out = TIKU_ADC_REF_1V2;
    } else if (streq(s, "2v0")) {
        *out = TIKU_ADC_REF_2V0;
    } else if (streq(s, "2v5")) {
        *out = TIKU_ADC_REF_2V5;
    } else {
        return -1;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_adc(uint8_t argc, const char *argv[])
{
    uint8_t channel;
    uint8_t ref = TIKU_ADC_REF_DEFAULT;
    uint16_t value;
    int rc;
    tiku_adc_config_t cfg;
    const char *ch_label;

    if (argc < 2) {
        SHELL_PRINTF("Usage: adc <channel|temp|bat> [ref]\n");
        SHELL_PRINTF("  channel: 0-15   ref: avcc|1v2|2v0|2v5\n");
        return;
    }

    if (streq(argv[1], "temp")) {
        channel = TIKU_ADC_CH_TEMP;
        ch_label = "temp";
    } else if (streq(argv[1], "bat")) {
        channel = TIKU_ADC_CH_BATTERY;
        ch_label = "bat";
    } else {
        if (parse_u8(argv[1], &channel) < 0 || channel > 15) {
            SHELL_PRINTF("Error: channel must be 0-15, temp, or bat\n");
            return;
        }
        ch_label = argv[1];
    }

    if (argc >= 3) {
        if (parse_ref(argv[2], &ref) < 0) {
            SHELL_PRINTF("Error: ref must be avcc, 1v2, 2v0, or 2v5\n");
            return;
        }
    }

    cfg.resolution = TIKU_ADC_RES_12BIT;
    cfg.reference = ref;

    rc = tiku_adc_init(&cfg);
    if (rc != TIKU_ADC_OK) {
        SHELL_PRINTF("Error: ADC init failed (%d)\n", rc);
        return;
    }

    /* Validate the channel and set up its pin where the port needs one */
    rc = tiku_adc_channel_init(channel);
    if (rc != TIKU_ADC_OK) {
        SHELL_PRINTF("Error: channel %s init failed (%d)\n",
                     ch_label, rc);
        tiku_adc_close();
        return;
    }

    rc = tiku_adc_read(channel, &value);
    tiku_adc_close();

    if (rc != TIKU_ADC_OK) {
        SHELL_PRINTF("Error: read failed (%d)\n", rc);
        return;
    }

    SHELL_PRINTF("A%s = %u (0x%x)\n", ch_label, value, value);
}
