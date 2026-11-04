/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_154_codec.c - Bounded IEEE 802.15.4 mailbox encoding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_154.h"
void tiku_154_ipc_header(uint8_t *p, uint8_t op, uint32_t id,
                        uint32_t claim, uint8_t rc, uint8_t payload)
{
    tiku_radio_header(p, op, id, claim, rc, payload);
    p[1] = '4';
}
int tiku_154_ipc_valid(const uint8_t *p, unsigned n, int response)
{
    if (!p || n < 16 || n > 64) return 0;
    unsigned op = p[3] & 127u;
    return p[0] == 'R' && p[1] == '4' && p[2] == TIKU_RADIO_VERSION &&
        !!(p[3] & 128u) == !!response &&
        ((op >= TIKU_RADIO_DISCOVER && op <= TIKU_RADIO_RELEASE) ||
         (op >= TIKU_154_MODE && op <= TIKU_154_CCA)) &&
        tiku_radio_get32(p + 4) && !p[14] && !p[15] && p[13] == n - 16 &&
        (response ? p[12] <= TIKU_RADIO_FAILED : !p[12]);
}
