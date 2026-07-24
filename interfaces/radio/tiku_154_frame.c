/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_154_frame.c - IEEE 802.15.4-2006 MAC frame primitives (PHY-free).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/radio/tiku_154_frame.h>
#include <string.h>

uint16_t tiku_154_fcs(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0u;
    uint16_t i;
    uint8_t  b;

    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (b = 0u; b < 8u; b++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

uint16_t tiku_154_chan_freq(uint8_t chan)
{
    if (chan < TIKU_154_CHAN_MIN || chan > TIKU_154_CHAN_MAX) {
        return 0u;
    }
    return (uint16_t)(2405u + 5u * (chan - TIKU_154_CHAN_MIN));
}

uint16_t tiku_154_chan_offset(uint8_t chan)
{
    uint16_t f = tiku_154_chan_freq(chan);

    return (f == 0u) ? 0xFFFFu : (uint16_t)(f - 2400u);
}

/* Reserved mode (1) has no valid length -- callers reject frames using it. */
static uint8_t addr_len(uint8_t mode)
{
    if (mode == TIKU_154_ADDR_SHORT) {
        return 2u;
    }
    if (mode == TIKU_154_ADDR_EXT) {
        return 8u;
    }
    return 0u;                       /* NONE or reserved */
}

uint16_t tiku_154_mhr_build(uint8_t *buf, const tiku_154_mhr_t *h)
{
    uint16_t fcf;
    uint8_t  o = 0u;
    uint8_t  dl, sl;

    if (h->dst_mode == 1u || h->src_mode == 1u) {
        return 0u;                   /* reserved addressing mode */
    }
    dl = addr_len(h->dst_mode);
    sl = addr_len(h->src_mode);

    fcf = (uint16_t)(h->type & 7u);
    if (h->ack_req) {
        fcf |= (uint16_t)(1u << 5);
    }
    if (h->pan_compress) {
        fcf |= (uint16_t)(1u << 6);
    }
    fcf |= (uint16_t)((uint16_t)(h->dst_mode & 3u) << 10);
    fcf |= (uint16_t)((uint16_t)(h->src_mode & 3u) << 14);

    buf[o++] = (uint8_t)(fcf & 0xFFu);
    buf[o++] = (uint8_t)(fcf >> 8);
    buf[o++] = h->seq;

    if (h->dst_mode != TIKU_154_ADDR_NONE) {
        buf[o++] = (uint8_t)(h->dst_pan & 0xFFu);
        buf[o++] = (uint8_t)(h->dst_pan >> 8);
        memcpy(&buf[o], h->dst_addr, dl);
        o = (uint8_t)(o + dl);
    }
    if (h->src_mode != TIKU_154_ADDR_NONE) {
        /* Src PAN present iff src addressing present AND not compressed. */
        if (!h->pan_compress) {
            buf[o++] = (uint8_t)(h->src_pan & 0xFFu);
            buf[o++] = (uint8_t)(h->src_pan >> 8);
        }
        memcpy(&buf[o], h->src_addr, sl);
        o = (uint8_t)(o + sl);
    }
    return o;
}

uint16_t tiku_154_mhr_parse(const uint8_t *buf, uint16_t len,
                            tiku_154_mhr_t *h)
{
    uint16_t fcf;
    uint16_t o;
    uint8_t  dl, sl;

    if (len < 3u) {
        return 0u;
    }
    fcf = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    memset(h, 0, sizeof(*h));
    h->type = (uint8_t)(fcf & 7u);
    h->ack_req = (uint8_t)((fcf >> 5) & 1u);
    h->pan_compress = (uint8_t)((fcf >> 6) & 1u);
    h->dst_mode = (uint8_t)((fcf >> 10) & 3u);
    h->src_mode = (uint8_t)((fcf >> 14) & 3u);
    h->seq = buf[2];
    o = 3u;

    if (h->dst_mode == 1u || h->src_mode == 1u) {
        return 0u;                   /* reserved addressing mode */
    }
    dl = addr_len(h->dst_mode);
    sl = addr_len(h->src_mode);

    if (h->dst_mode != TIKU_154_ADDR_NONE) {
        if (len < (uint16_t)(o + 2u + dl)) {
            return 0u;
        }
        h->dst_pan = (uint16_t)(buf[o] | ((uint16_t)buf[o + 1u] << 8));
        o = (uint16_t)(o + 2u);
        memcpy(h->dst_addr, &buf[o], dl);
        o = (uint16_t)(o + dl);
    }
    if (h->src_mode != TIKU_154_ADDR_NONE) {
        if (!h->pan_compress) {
            if (len < (uint16_t)(o + 2u)) {
                return 0u;
            }
            h->src_pan = (uint16_t)(buf[o] | ((uint16_t)buf[o + 1u] << 8));
            o = (uint16_t)(o + 2u);
        } else {
            h->src_pan = h->dst_pan;  /* inherited under compression */
        }
        if (len < (uint16_t)(o + sl)) {
            return 0u;
        }
        memcpy(h->src_addr, &buf[o], sl);
        o = (uint16_t)(o + sl);
    }
    return o;
}
