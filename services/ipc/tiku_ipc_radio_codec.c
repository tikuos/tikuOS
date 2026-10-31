/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_radio_codec.c - bounded radio message encoding.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <interfaces/wireless/tiku_wireless_ipc.h>
void tiku_radio_header(uint8_t *p, uint8_t op, uint32_t request, uint32_t claim, uint8_t result,
                       uint8_t payload)
{
    p[0] = 'R';
    p[1] = 'W';
    p[2] = TIKU_RADIO_VERSION;
    p[3] = op;
    tiku_radio_put32(p + 4, request);
    tiku_radio_put32(p + 8, claim);
    p[12] = result;
    p[13] = payload;
    p[14] = p[15] = 0;
}
int tiku_radio_valid(const void *data, size_t length, uint8_t response)
{
    const uint8_t *p = data;
    if (!p || length < TIKU_RADIO_HEADER || length > TIKU_RADIO_CREDENTIALS)
        return 0;
    unsigned op = p[3] & 127u;
    return p[0] == 'R' && p[1] == 'W' && p[2] == TIKU_RADIO_VERSION &&
           !!(p[3] & 128u) == !!response && op >= TIKU_RADIO_DISCOVER && op <= TIKU_RADIO_MODE &&
           tiku_radio_get32(p + 4) && !p[14] && !p[15] && p[13] == length - TIKU_RADIO_HEADER &&
           (response ? p[12] <= TIKU_RADIO_FAILED : p[12] == 0) &&
           (op == TIKU_RADIO_CREDENTIAL || length <= TIKU_RADIO_CONTROL);
}
uint32_t tiku_radio_digest(const void *data, size_t length)
{
    const uint8_t *p = data;
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
void tiku_radio_wipe(void *data, size_t length)
{
    volatile uint8_t *p = data;
    while (length--)
        *p++ = 0;
}
