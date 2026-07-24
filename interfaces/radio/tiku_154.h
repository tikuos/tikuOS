/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_154.h - IEEE 802.15.4 MAC-min facade (kintsugi/radio.md N2).
 *
 * Ties the PHY (arch/nordic/tiku_ieee154_arch) to the PHY-free frame layer
 * (tiku_154_frame): addressed data frames with 16-bit PAN/short addressing
 * and sequence numbers, address filtering on receive, unslotted CSMA-CA on
 * the hardware CCA (N2.2), and auto-ACK via the T_IFS turnaround (N2.3).
 * Same facade discipline as tiku_ble_adv -- the shell command and any
 * future stack sit on this, not on registers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_154_H_
#define TIKU_154_H_

#include <stdint.h>

/** @brief 16-bit broadcast short address / PAN. */
#define TIKU_154_ADDR_BCAST   0xFFFFu

/** @brief Per-frame receive metadata. */
typedef struct {
    uint16_t src;           /**< source short address                     */
    uint8_t  seq;           /**< sequence number                          */
    uint8_t  acked;         /**< 1 if we sent an ACK for it (N2.3)         */
    int8_t   rssi;          /**< RSSI in dBm                              */
} tiku_154_rx_t;

/** @brief 1 if this build has the 15.4 MAC (nRF54L on-die RADIO). */
int tiku_154_available(void);

/**
 * @brief Configure the MAC: PAN id, our short address, and channel (11..26).
 * Idempotent; call again to re-address or retune.
 */
void tiku_154_init(uint16_t pan, uint16_t short_addr, uint8_t channel);

/** @brief Retune while keeping PAN/address. */
void tiku_154_set_channel(uint8_t channel);

/** @brief Our configured short address. */
uint16_t tiku_154_addr(void);

/** @brief Current durable TX security frame counter (survives reboot). */
uint32_t tiku_154_tx_counter(void);

/**
 * @brief Install the 128-bit link key and enable/disable securing outgoing
 *        frames (IEEE 802.15.4 security level 6 = ENC-MIC-64, AES-CCM*).
 *        Received secured frames are always decrypted + MIC-verified when a
 *        key is set.  @p key NULL clears the key.
 */
void tiku_154_set_key(const uint8_t *key);

/** @brief Secure outgoing frames (needs a key set); 0 = send in the clear. */
void tiku_154_set_secure(int on);

/**
 * @brief Send a data frame to @p dst (TIKU_154_ADDR_BCAST for all).
 * @param ack  request an ACK and wait/retry for it (N2.3).
 * @return 0 sent (ACK seen if requested), -1 bad length, -2 channel busy
 *         after CSMA backoff, -3 no ACK after retries.
 */
int tiku_154_send(uint16_t dst, const uint8_t *payload, uint8_t len,
                  uint8_t ack);

/**
 * @brief Receive one data frame addressed to us or broadcast, up to
 *        @p timeout_ms.  Frames for other addresses are skipped within the
 *        window.  ACKs an ack-requesting frame before returning (N2.3).
 * @return payload length, 0 timeout, -1 error.
 */
int tiku_154_recv(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                  tiku_154_rx_t *info);

#endif /* TIKU_154_H_ */
