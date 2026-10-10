/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_154.c - IEEE 802.15.4 MAC-min over the PHY and the frame layer.
 *
 * Addressed data frames with receive filtering on tiku_ieee154_arch and
 * tiku_154_frame: unslotted CSMA-CA, hardware-timed auto-ACK, and AES-CCM*
 * link security with a durable frame counter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/radio/tiku_154.h>
#include <interfaces/radio/tiku_154_frame.h>
#include <hal/tiku_ieee154_hal.h>              /* PHY, hold, AES-CCM*        */
#include "tiku.h"                              /* the platform's tick, clock */
#include <kernel/memory/tiku_mem.h>            /* durable frame-counter cell  */
#include <string.h>

/* Link security: level 6 (ENC-MIC-64) AES-CCM*, key-id-mode 0 (implicit). */
#define MAC_SEC_LEVEL   6u
#define MAC_MIC_LEN     8u
#define MAC_ASH_LEN     5u                       /* SecControl(1)+FrameCtr(4) */
#define FCF_SEC_ENABLED (1u << 3)                /* FCF Security Enabled bit  */

static uint8_t  mac_key[16];
static uint8_t  mac_have_key;
static uint8_t  mac_secure;                      /* secure outgoing frames    */
static uint32_t mac_tx_ctr;                      /* frame security counter */

/**
 * @brief 13-byte CCM* nonce = src ext addr (8, short mapped into the low 2) ||
 *        frame counter (4, BE) || security level (1).
 */
static void mac_nonce(uint8_t n[13], uint16_t src, uint32_t ctr)
{
    n[0] = n[1] = n[2] = n[3] = n[4] = n[5] = 0u;
    n[6] = (uint8_t)(src >> 8);
    n[7] = (uint8_t)src;
    n[8]  = (uint8_t)(ctr >> 24);
    n[9]  = (uint8_t)(ctr >> 16);
    n[10] = (uint8_t)(ctr >> 8);
    n[11] = (uint8_t)ctr;
    n[12] = MAC_SEC_LEVEL;
}

/* Durable TX frame counter.  The nonce is src||counter||level with a fixed
 * key, so a counter that restarted at 0 after a power cycle would repeat a
 * keystream, which breaks CTR mode.  Counters are reserved ahead in durable
 * storage: a high-water mark is persisted WINDOW past what has been handed
 * out, so there is at most one durable write per WINDOW frames, and a reboot
 * resumes above every counter used (unused reserved counters are skipped). */
#define MAC_CTR_MAGIC   0x15CC7201u
#define MAC_CTR_WINDOW  64u

static TIKU_DURABLE uint32_t mac_ctr_hwm_persist;
TIKU_PERSIST_CELL(mac_ctr_cell, mac_ctr_hwm_persist, MAC_CTR_MAGIC, NULL, 0);
static uint8_t mac_ctr_ready;

/**
 * @brief Next TX frame counter; reserves a further window in durable storage
 *        when the current one is used up.
 */
static uint32_t mac_ctr_next(void)
{
    uint32_t c;
    if (mac_ctr_ready == 0u) {
        (void)tiku_persist_cell_init(&mac_ctr_cell);
        mac_tx_ctr = mac_ctr_hwm_persist;        /* resume above all reserved */
        mac_ctr_ready = 1u;
    }
    if (mac_tx_ctr >= mac_ctr_hwm_persist) {      /* window used: reserve */
        tiku_persist_cell_write_u32(&mac_ctr_cell,
                                    mac_tx_ctr + MAC_CTR_WINDOW);
    }
    c = mac_tx_ctr;
    mac_tx_ctr++;
    return c;
}

/* RX anti-replay: per-source last-accepted counter; a frame whose counter is
 * not strictly greater is a replay/stale and dropped.  Small round-robin
 * table (the node<->node case); best-effort under eviction. */
#define MAC_RX_SEEN 4u
static struct {
    uint16_t src;
    uint32_t ctr;
    uint8_t  used;
} mac_rx_seen[MAC_RX_SEEN];
static uint8_t mac_rx_evict;

/**
 * @brief Accept @p ctr from @p src if it is newer than the last one accepted.
 * @return 1 when fresh (and recorded), 0 for a replayed or stale frame
 */
static int mac_rx_fresh(uint16_t src, uint32_t ctr)
{
    uint8_t i, slot = MAC_RX_SEEN;
    for (i = 0u; i < MAC_RX_SEEN; i++) {
        if (mac_rx_seen[i].used != 0u && mac_rx_seen[i].src == src) {
            if (ctr <= mac_rx_seen[i].ctr) {
                return 0;                        /* replay / out-of-order     */
            }
            mac_rx_seen[i].ctr = ctr;
            return 1;
        }
        if (mac_rx_seen[i].used == 0u) {
            slot = i;
        }
    }
    if (slot >= MAC_RX_SEEN) {                    /* full: round-robin evict  */
        slot = mac_rx_evict;
        mac_rx_evict = (uint8_t)((mac_rx_evict + 1u) % MAC_RX_SEEN);
    }
    mac_rx_seen[slot].used = 1u;
    mac_rx_seen[slot].src  = src;
    mac_rx_seen[slot].ctr  = ctr;
    return 1;
}

void tiku_154_set_key(const uint8_t *key)
{
    memset(mac_rx_seen, 0, sizeof(mac_rx_seen));  /* fresh replay window      */
    mac_rx_evict = 0u;
    if (key == 0) {
        mac_have_key = 0u;
        mac_secure = 0u;
        return;
    }
    memcpy(mac_key, key, 16u);
    mac_have_key = 1u;
}

void tiku_154_set_secure(int on)
{
    mac_secure = (on != 0 && mac_have_key) ? 1u : 0u;
}

uint32_t tiku_154_tx_counter(void)
{
    if (mac_ctr_ready == 0u) {
        (void)tiku_persist_cell_init(&mac_ctr_cell);
        mac_tx_ctr = mac_ctr_hwm_persist;
        mac_ctr_ready = 1u;
    }
    return mac_tx_ctr;
}

static uint16_t mac_pan  = 0xABCDu;
static uint16_t mac_addr = 0x0000u;
static uint8_t  mac_chan = 15u;
static uint8_t  mac_seq;

/* Unslotted CSMA-CA and ACK-wait bounds, timed in software. */
#define MAC_MAX_CSMA      4u    /* CCA backoffs before giving up            */
#define MAC_MAX_RETRIES   3u    /* frame retransmits waiting for an ACK     */
#define MAC_ACK_WAIT_MS   8u    /* how long to listen for the ACK           */

int tiku_154_available(void)
{
    return tiku_ieee154_arch_available();
}

/** @brief Clamp @p ch to TIKU_154_CHAN_MIN..TIKU_154_CHAN_MAX. */
static uint8_t clamp_chan(uint8_t ch)
{
    if (ch < TIKU_154_CHAN_MIN) {
        return TIKU_154_CHAN_MIN;
    }
    return (ch > TIKU_154_CHAN_MAX) ? TIKU_154_CHAN_MAX : ch;
}

int tiku_154_init(uint16_t pan, uint16_t short_addr, uint8_t channel)
{
    mac_pan  = pan;
    mac_addr = short_addr;
    mac_chan = clamp_chan(channel);
    return tiku_ieee154_arch_mode_154(mac_chan);
}

void tiku_154_set_channel(uint8_t channel)
{
    mac_chan = clamp_chan(channel);
    tiku_ieee154_arch_set_channel(mac_chan);
}

uint16_t tiku_154_addr(void)
{
    return mac_addr;
}

/**
 * @brief Build a DATA frame [MHR][payload] (no FCS -- the radio appends it).
 * @return Frame length, or 0 when it does not fit
 */
static uint16_t mac_build(uint8_t *frame, uint16_t dst, const uint8_t *payload,
                          uint8_t len, uint8_t ack)
{
    tiku_154_mhr_t h;
    uint16_t hlen;

    memset(&h, 0, sizeof(h));
    h.type = TIKU_154_FT_DATA;
    h.ack_req = (ack != 0u && dst != TIKU_154_ADDR_BCAST) ? 1u : 0u;
    h.pan_compress = 1u;                         /* src+dst share the PAN     */
    h.seq = mac_seq++;
    h.dst_mode = TIKU_154_ADDR_SHORT;
    h.dst_pan  = mac_pan;
    h.dst_addr[0] = (uint8_t)dst;
    h.dst_addr[1] = (uint8_t)(dst >> 8);
    h.src_mode = TIKU_154_ADDR_SHORT;
    h.src_pan  = mac_pan;
    h.src_addr[0] = (uint8_t)mac_addr;
    h.src_addr[1] = (uint8_t)(mac_addr >> 8);

    hlen = tiku_154_mhr_build(frame, &h);
    if (hlen == 0u || (uint16_t)(hlen + len) > TIKU_154_MAX_PSDU) {
        return 0u;
    }
    if (len != 0u) {
        memcpy(frame + hlen, payload, len);
    }
    return (uint16_t)(hlen + len);
}

/** @brief Bounded busy-wait backoff, spacing retries under contention. */
static void mac_backoff(uint8_t n)
{
    volatile uint32_t i;
    uint32_t bound = (uint32_t)(n + 1u) * 30000u;
    for (i = 0u; i < bound; i++) {
    }
}

/**
 * @brief Listen briefly for an ACK frame carrying @p seq.  The PHY hands back
 *        the MAC frame with the FCS stripped: an ACK is [FCF_lo][FCF_hi][seq].
 * @return 1 when the ACK arrived, else 0
 */
static int mac_wait_ack(uint8_t seq)
{
    uint8_t abuf[8];
    int8_t  rssi = 0;
    int n = tiku_ieee154_arch_rx(abuf, sizeof(abuf), MAC_ACK_WAIT_MS, &rssi);
    if (n >= 3 && (uint8_t)(abuf[0] & 0x07u) == TIKU_154_FT_ACK &&
        abuf[2] == seq) {
        return 1;
    }
    return 0;
}

/**
 * @brief Build a secured DATA frame: MHR (Security Enabled) + Auxiliary
 *        Security Header + AES-CCM*-encrypted payload + MIC.
 *
 * AAD = MHR||ASH (authenticated, not encrypted); nonce = local addr || frame
 * counter || sec level.
 *
 * @return Frame length, or 0 when it does not fit or encryption fails
 */
static uint16_t mac_build_secured(uint8_t *frame, uint16_t dst,
                                  const uint8_t *payload, uint8_t len,
                                  uint8_t ack)
{
    tiku_154_mhr_t h;
    uint16_t hlen;
    uint8_t  nonce[13];
    uint32_t ctr = mac_ctr_next();               /* durable, no reboot reuse  */

    memset(&h, 0, sizeof(h));
    h.type = TIKU_154_FT_DATA;
    h.ack_req = (ack != 0u && dst != TIKU_154_ADDR_BCAST) ? 1u : 0u;
    h.pan_compress = 1u;
    h.seq = mac_seq++;
    h.dst_mode = TIKU_154_ADDR_SHORT;
    h.dst_pan  = mac_pan;
    h.dst_addr[0] = (uint8_t)dst;
    h.dst_addr[1] = (uint8_t)(dst >> 8);
    h.src_mode = TIKU_154_ADDR_SHORT;
    h.src_pan  = mac_pan;
    h.src_addr[0] = (uint8_t)mac_addr;
    h.src_addr[1] = (uint8_t)(mac_addr >> 8);

    hlen = tiku_154_mhr_build(frame, &h);
    if (hlen == 0u) {
        return 0u;
    }
    if ((uint16_t)(hlen + MAC_ASH_LEN + len + MAC_MIC_LEN) >
        TIKU_154_MAX_PSDU) {
        return 0u;
    }
    frame[0] |= FCF_SEC_ENABLED;
    frame[hlen]      = MAC_SEC_LEVEL;             /* SecControl, keyidmode 0  */
    frame[hlen + 1u] = (uint8_t)ctr;             /* FrameCounter, LE */
    frame[hlen + 2u] = (uint8_t)(ctr >> 8);
    frame[hlen + 3u] = (uint8_t)(ctr >> 16);
    frame[hlen + 4u] = (uint8_t)(ctr >> 24);

    mac_nonce(nonce, mac_addr, ctr);
    if (tiku_ieee154_arch_ccm_star(0, mac_key, nonce,
                                   frame, (size_t)(hlen + MAC_ASH_LEN),
                                   payload, len, MAC_MIC_LEN,
                                   &frame[hlen + MAC_ASH_LEN],
                                   &frame[hlen + MAC_ASH_LEN + len]) != 0) {
        return 0u;
    }
    return (uint16_t)(hlen + MAC_ASH_LEN + len + MAC_MIC_LEN);
}

int tiku_154_send(uint16_t dst, const uint8_t *payload, uint8_t len,
                  uint8_t ack)
{
    uint8_t  frame[TIKU_154_MAX_PSDU];
    uint16_t flen = mac_secure
        ? mac_build_secured(frame, dst, payload, len, ack)
        : mac_build(frame, dst, payload, len, ack);
    uint8_t  want_ack = (ack != 0u && dst != TIKU_154_ADDR_BCAST) ? 1u : 0u;
    uint8_t  seq, attempt;
    int rc = -3;

    if (flen == 0u) {
        return -1;
    }
    seq = frame[2];                              /* FCF is 2 B, seq follows   */
    tiku_ieee154_arch_hold(1);                   /* held across the burst     */
    for (attempt = 0u; attempt <= MAC_MAX_RETRIES; attempt++) {
        uint8_t bo;
        /* Unslotted CSMA-CA: CCA, backoff-and-retry while busy. */
        for (bo = 0u; ; bo++) {
            if (tiku_ieee154_arch_cca()) {
                break;                           /* channel idle: send        */
            }
            if (bo >= MAC_MAX_CSMA) {
                tiku_ieee154_arch_hold(0);
                return -2;                       /* stayed busy               */
            }
            mac_backoff(bo);
        }
        (void)tiku_ieee154_arch_tx(frame, (uint8_t)flen);
        if (!want_ack) {
            rc = 0;
            break;
        }
        if (mac_wait_ack(seq)) {
            rc = 0;
            break;                               /* acknowledged              */
        }
        /* no ACK within the window: retransmit */
    }
    tiku_ieee154_arch_hold(0);
    return rc;
}

int tiku_154_recv(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                  tiku_154_rx_t *info)
{
    uint8_t frame[TIKU_154_MAX_FRAME];
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t dl =
        (tiku_clock_time_t)(((uint32_t)TIKU_CLOCK_SECOND * timeout_ms) /
                            1000u);
    int ret = 0;

    tiku_ieee154_arch_hold(1);
    for (;;) {
        tiku_clock_time_t el = (tiku_clock_time_t)(tiku_clock_time() - start);
        uint32_t left;
        int8_t rssi = 0;
        uint8_t did_ack = 0u;
        int n;
        tiku_154_mhr_t h;
        uint16_t hlen, dst;

        if (el >= dl) {
            ret = 0;                             /* window elapsed            */
            break;
        }
        left = (uint32_t)((((uint32_t)(dl - el)) * 1000u) / TIKU_CLOCK_SECOND);
        if (left == 0u) {
            left = 1u;
        }
        /* RX + hardware-timed auto-ACK in one shot (the ACK, if any, is sent
         * by the PHY within the 192 us turnaround for a unicast-to-this-node,
         * ack-requesting, CRC-OK data frame). */
        n = tiku_ieee154_arch_rx_ack(frame, sizeof(frame), left, &rssi,
                                     mac_pan, mac_addr, &did_ack);
        if (n <= 0) {
            continue;                            /* timeout slice / bad FCS   */
        }
        hlen = tiku_154_mhr_parse(frame, (uint16_t)n, &h);
        if (hlen == 0u || h.type != TIKU_154_FT_DATA) {
            continue;                            /* not a data frame */
        }
        dst = (uint16_t)(h.dst_addr[0] | ((uint16_t)h.dst_addr[1] << 8));
        if (h.dst_pan != mac_pan && h.dst_pan != TIKU_154_ADDR_BCAST) {
            continue;
        }
        if (dst != mac_addr && dst != TIKU_154_ADDR_BCAST) {
            continue;                            /* addressed to someone else */
        }
        /* The PHY already sent the hardware-timed ACK (if warranted) in the
         * turnaround window.  Decrypt + MIC-verify a secured frame, else
         * deliver the cleartext payload. */
        {
            uint8_t plen;
            if ((frame[0] & FCF_SEC_ENABLED) != 0u) {
                static uint8_t pt[TIKU_154_MAX_PSDU];
                uint8_t  nonce[13], rmic[MAC_MIC_LEN];
                uint16_t need = (uint16_t)(hlen + MAC_ASH_LEN + MAC_MIC_LEN);
                uint16_t ctlen;
                uint32_t rctr;
                if (mac_have_key == 0u || (uint16_t)n < need) {
                    continue;                    /* can't/won't decrypt: drop */
                }
                rctr = (uint32_t)frame[hlen + 1u] |
                       ((uint32_t)frame[hlen + 2u] << 8) |
                       ((uint32_t)frame[hlen + 3u] << 16) |
                       ((uint32_t)frame[hlen + 4u] << 24);
                ctlen = (uint16_t)((uint16_t)n - need);
                mac_nonce(nonce, (uint16_t)(h.src_addr[0] |
                          ((uint16_t)h.src_addr[1] << 8)), rctr);
                if (tiku_ieee154_arch_ccm_star(
                        1, mac_key, nonce, frame,
                        (size_t)(hlen + MAC_ASH_LEN),
                        &frame[hlen + MAC_ASH_LEN], ctlen, MAC_MIC_LEN,
                        pt, rmic) != 0) {
                    continue;
                }
                if (memcmp(rmic, &frame[hlen + MAC_ASH_LEN + ctlen],
                           MAC_MIC_LEN) != 0) {
                    continue;                    /* MIC fail: forged/bad key */
                }
                if (mac_rx_fresh((uint16_t)(h.src_addr[0] |
                        ((uint16_t)h.src_addr[1] << 8)), rctr) == 0) {
                    continue;                    /* replayed/stale frame      */
                }
                plen = (uint8_t)((ctlen > cap) ? cap : ctlen);
                if (plen != 0u) {
                    memcpy(buf, pt, plen);
                }
            } else {
                plen = (uint8_t)((uint16_t)n - hlen);
                if (plen > cap) {
                    plen = cap;
                }
                if (plen != 0u) {
                    memcpy(buf, frame + hlen, plen);
                }
            }
            if (info != 0) {
                info->src = (uint16_t)(h.src_addr[0] |
                                       ((uint16_t)h.src_addr[1] << 8));
                info->seq = h.seq;
                info->acked = did_ack;
                info->rssi = rssi;
            }
            ret = (int)plen;
        }
        break;
    }
    tiku_ieee154_arch_hold(0);
    return ret;
}
