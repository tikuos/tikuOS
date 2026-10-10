/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ieee154_hal.h - IEEE 802.15.4 PHY functions a port with the radio
 * implements.  The MAC (interfaces/radio/tiku_154.c) and the radio154 shell
 * command use only these.  Includes no platform header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_IEEE154_HAL_H_
#define TIKU_IEEE154_HAL_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_154_CHAN_MIN     11u   /**< lowest page-0 channel, 2405 MHz */
#define TIKU_154_CHAN_MAX     26u   /**< highest page-0 channel, 2480 MHz */

/** @brief Largest frame the PHR can count: 127 bytes including the FCS. */
#define TIKU_154_MAX_FRAME   127u
/** @brief Largest MAC frame passed to or from the caller (frame minus FCS). */
#define TIKU_154_MAX_PSDU    125u

/** @brief Return 1 when this build drives an 802.15.4 PHY. */
int tiku_ieee154_arch_available(void);

/**
 * @brief Take the radio for 802.15.4 and tune it to @p channel (clamped
 *        11..26).  The caller must own the radio.
 *
 * @return 0, or -1 when the radio cannot be taken
 */
int tiku_ieee154_arch_mode_154(uint8_t channel);

/**
 * @brief Leave 802.15.4 mode and hand the radio back: to the BLE link
 *        configuration where the radio is shared, else powered down.
 */
void tiku_ieee154_arch_leave(void);

/** @brief Retune to @p channel (clamped 11..26), staying in 15.4 mode. */
void tiku_ieee154_arch_set_channel(uint8_t channel);

/**
 * @brief Transmit one MAC frame and wait until the radio is idle again.
 *
 * The radio appends the 2-byte FCS, so @p psdu holds the frame without it.
 *
 * @param psdu  MAC frame without FCS
 * @param len   Length of @p psdu, at most TIKU_154_MAX_PSDU
 * @return 0 when sent, -1 if @p len is too long, -2 on a ramp or TX timeout
 */
int tiku_ieee154_arch_tx(const uint8_t *psdu, uint8_t len);

/**
 * @brief Listen for one frame for up to @p timeout_ms, then stop the radio.
 *
 * @param buf         Out: the frame with its FCS stripped
 * @param cap         Size of @p buf; a longer frame is truncated to it
 * @param timeout_ms  Listen window in milliseconds
 * @param rssi        Out, optional: last RSSI sample of the listen, in dBm
 * @return bytes copied to @p buf, 0 on timeout, -1 for a frame with a bad FCS
 */
int tiku_ieee154_arch_rx(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                         int8_t *rssi);

/**
 * @brief Measure the energy on @p channel and leave the radio in 15.4 mode,
 *        idle, on that channel.
 *
 * @param channel  Channel 11..26 (clamped)
 * @param dbm      Out, optional: approximate energy level in dBm
 * @return the raw ED level (0..255), or -1 when the radio cannot be taken
 *         or the measurement times out
 */
int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm);

/**
 * @brief Clear-channel assessment (energy-detect mode) on the current
 *        channel.  Leaves the radio idle.
 * @return 1 channel idle (clear to send), 0 busy or ramp/CCA timeout.
 */
int tiku_ieee154_arch_cca(void);

/**
 * @brief Listen for one frame and ACK it when it asks for one.
 *
 * An ACK goes out for a CRC-OK data frame with AR set whose destination PAN
 * and short address are @p my_pan and @p my_addr, within the 192 us
 * turnaround after the frame.
 *
 * @param buf      Out: the frame with its FCS stripped
 * @param cap      Size of @p buf; a longer frame is truncated to it
 * @param timeout_ms  Listen window in milliseconds
 * @param rssi     Out, optional: RSSI in dBm; not written on timeout
 * @param my_pan   PAN ID an ACKed frame must carry as destination
 * @param my_addr  Short address an ACKed frame must carry as destination
 * @param did_ack  Out, optional: 1 if an ACK was committed; not written on
 *                 timeout
 * @return bytes copied to @p buf, 0 on timeout, -1 for a bad FCS
 */
int tiku_ieee154_arch_rx_ack(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                             int8_t *rssi, uint16_t my_pan, uint16_t my_addr,
                             uint8_t *did_ack);

/**
 * @brief Hold the radio's timing path ready across a burst of operations
 *        (@p on non-zero), then release it.  A port that needs no such hold
 *        does nothing.
 */
void tiku_ieee154_arch_hold(int on);

/**
 * @brief AES-128-CCM* with a 13-byte nonce (L = 2), as 802.15.4 security uses.
 *
 * Encrypt: @p m is the plaintext, @p out the ciphertext, @p mic the tag.
 * Decrypt: @p m is the ciphertext, @p out the plaintext, @p mic the tag
 * recomputed over it, which the caller compares with the received one.
 *
 * @param mic_len  4, 8 or 16
 * @return 0, or negative for bad arguments or an engine error
 */
int tiku_ieee154_arch_ccm_star(int decrypt, const uint8_t key[16],
                               const uint8_t nonce[13], const uint8_t *aad,
                               size_t aad_len, const uint8_t *m, size_t m_len,
                               uint8_t mic_len, uint8_t *out, uint8_t *mic);

#endif /* TIKU_IEEE154_HAL_H_ */
