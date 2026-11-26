/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_154.h - IEEE 802.15.4 MAC-min facade.
 *
 * Ties the PHY to the frame layer for the shell and the stacks: addressed data
 * frames with 16-bit PAN/short addressing, receive filtering, unslotted
 * CSMA-CA on the hardware CCA and auto-ACK on the T_IFS turnaround.
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
    uint8_t  acked;         /**< 1 if an ACK was sent for it              */
    int8_t   rssi;          /**< RSSI in dBm                              */
} tiku_154_rx_t;

/** @brief Returns 1 when the build has an 802.15.4 PHY under the MAC. */
int tiku_154_available(void);

/**
 * @brief Configure the MAC: PAN id, local short address, and channel (11..26,
 *        clamped), and take the radio.  Call again to re-address or retune.
 * @return 0, or -1 when the radio cannot be taken
 */
int tiku_154_init(uint16_t pan, uint16_t short_addr, uint8_t channel);

/** @brief Retune while keeping PAN/address. */
void tiku_154_set_channel(uint8_t channel);

/** @brief The configured short address. */
uint16_t tiku_154_addr(void);

/** @brief Current durable TX security frame counter (survives reboot). */
uint32_t tiku_154_tx_counter(void);

/**
 * @brief Install the 128-bit link key for security level 6 (ENC-MIC-64,
 *        AES-CCM*) and start a fresh replay window.
 *
 * Received secured frames are decrypted and MIC-verified while a key is set;
 * tiku_154_set_secure() turns on securing outgoing frames.  @p key NULL
 * clears the key and stops securing.
 */
void tiku_154_set_key(const uint8_t *key);

/** @brief Secure outgoing frames (needs a key set); 0 = send in the clear. */
void tiku_154_set_secure(int on);

/**
 * @brief Send a data frame to @p dst (TIKU_154_ADDR_BCAST for all).
 * @param ack  request an ACK and wait/retry for it.
 * @return 0 sent (ACK seen if requested), -1 bad length or failed
 *         encryption, -2 channel busy after CSMA backoff, -3 no ACK after
 *         retries.
 */
int tiku_154_send(uint16_t dst, const uint8_t *payload, uint8_t len,
                  uint8_t ack);

/**
 * @brief Receive one data frame addressed to this node or broadcast, up to
 *        @p timeout_ms.  Frames for other addresses or PANs are skipped within
 *        the window, and so are secured frames that arrive with no key set,
 *        fail to decrypt or verify, or repeat an old counter.  An
 *        ack-requesting frame is ACKed before this returns.
 * @return payload length; 0 also when the window closes with nothing
 *         delivered.
 */
int tiku_154_recv(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                  tiku_154_rx_t *info);

#if (TIKU_HAS_154_LISTEN + 0)
/*
 * Raw frames and a background listen, where the PHY listens in the
 * background (TIKU_HAS_154_LISTEN): the frames are PSDUs as the PHY sends and
 * receives them, with no MAC header added or removed.  The listen holds the
 * radio from tiku_154_listen(1) to tiku_154_listen(0); a send or an energy
 * measurement meanwhile pauses it.  Received frames with a good FCS are held
 * in a ring of TIKU_154_HELD_BYTES bytes, two more than each frame, until
 * tiku_154_take() takes them; a frame that does not fit is dropped.
 */

/** @brief Bytes of the ring of held frames. */
#ifndef TIKU_154_HELD_BYTES
#define TIKU_154_HELD_BYTES  256u
#endif

/**
 * @brief Start (@p on non-zero) or end the background listen on the channel
 *        tiku_154_channel() names.
 * @return 0, or -1 when the radio cannot be taken
 */
int tiku_154_listen(int on);

/** @brief 1 while the background listen runs. */
int tiku_154_listening(void);

/** @brief The channel the raw frames and the listen use (11..26). */
uint8_t tiku_154_channel(void);

/**
 * @brief Set the channel (clamped 11..26), the one the MAC also uses; a
 *        running listen moves to it.
 */
void tiku_154_tune(uint8_t channel);

/**
 * @brief Send one raw frame on tiku_154_channel(), with no CCA.
 * @param psdu  The frame without its FCS, which the PHY appends
 * @param len   1..TIKU_154_MAX_PSDU
 * @return 0 sent, -1 bad length, -2 the radio cannot be taken or the
 *         transmission timed out
 */
int tiku_154_send_raw(const uint8_t *psdu, uint8_t len);

/**
 * @brief Measure the energy on tiku_154_channel().
 * @param dbm  Out, optional: the level in dBm
 * @return the raw ED level (0..255), or -1 when the radio cannot be taken or
 *         the measurement times out
 */
int tiku_154_energy(int8_t *dbm);

/**
 * @brief Take the oldest held frame.
 * @param cap   Size of @p buf; a longer frame is cut to it and the rest lost
 * @param rssi  Out, optional: its level in dBm
 * @return the bytes copied, 0 when no frame is held
 */
int tiku_154_take(uint8_t *buf, uint8_t cap, int8_t *rssi);

/** @brief The number of frames held. */
uint8_t tiku_154_held(void);

/**
 * @brief Call @p fn, outside interrupt context, after a drain of the listen
 *        that held at least one frame; NULL removes it.
 */
void tiku_154_set_rx_notify(void (*fn)(void));
#endif /* TIKU_HAS_154_LISTEN */

#endif /* TIKU_154_H_ */
