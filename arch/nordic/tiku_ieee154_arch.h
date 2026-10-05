/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ieee154_arch.h - IEEE 802.15.4 250 kbps PHY on the nRF54L RADIO.
 *
 * The RADIO is shared with BLE: 15.4 mode replaces the BLE link config, so the
 * caller must own the radio and restore BLE with tiku_ieee154_arch_mode_ble().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_IEEE154_ARCH_H_
#define TIKU_NORDIC_IEEE154_ARCH_H_

#include <stdint.h>

#define TIKU_154_CHAN_MIN     11u   /**< lowest page-0 channel, 2405 MHz */
#define TIKU_154_CHAN_MAX     26u   /**< highest page-0 channel, 2480 MHz */

/** @brief Largest frame the PHR can count: 127 bytes including the FCS. */
#define TIKU_154_MAX_FRAME   127u
/** @brief Largest MAC frame passed to or from the caller (frame minus FCS). */
#define TIKU_154_MAX_PSDU    125u

/** @brief Return 1: every nRF54L RADIO has the 802.15.4 PHY. */
int tiku_ieee154_arch_available(void);

/**
 * @brief Switch the RADIO into 802.15.4 mode on @p channel (clamped 11..26).
 *
 * Reprograms MODE, PCNF, CRC, SFD, TXPOWER and FREQUENCY and clears SHORTS.
 *
 * @note The RADIO must be DISABLED and not in use by BLE.
 */
void tiku_ieee154_arch_mode_154(uint8_t channel);

/** @brief Restore the BLE link config by calling tiku_radio_arch_init(). */
void tiku_ieee154_arch_mode_ble(void);

/** @brief Retune to @p channel (clamped 11..26), staying in 15.4 mode. */
void tiku_ieee154_arch_set_channel(uint8_t channel);

/**
 * @brief Transmit one MAC frame and wait until the RADIO is DISABLED again.
 *
 * The RADIO appends the 2-byte FCS, so @p psdu holds the frame without it.
 *
 * @param psdu  MAC frame without FCS
 * @param len   Length of @p psdu, at most TIKU_154_MAX_PSDU
 * @return 0 when sent, -1 if @p len is too long, -2 on a ramp or TX timeout
 */
int tiku_ieee154_arch_tx(const uint8_t *psdu, uint8_t len);

/**
 * @brief Listen for one frame for up to @p timeout_ms, then disable the RADIO.
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
 * @brief Measure the energy on @p channel and leave the RADIO in 15.4 mode,
 *        DISABLED.
 *
 * Reprograms the full 15.4 link config before the measurement.
 *
 * @param channel  Channel 11..26 (clamped)
 * @param dbm      Out, optional: approximate energy level in dBm
 * @return the raw ED level (EDSAMPLE, 0..255), or -1 on a ramp or ED timeout
 */
int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm);

/**
 * @brief Clear-channel assessment (energy-detect mode) on the current
 *        channel.  Leaves the radio DISABLED.
 * @return 1 channel idle (clear to send), 0 busy or ramp/CCA timeout.
 */
int tiku_ieee154_arch_cca(void);

/**
 * @brief Listen for one frame and ACK it when it asks for one.
 *
 * An ACK goes out for a CRC-OK data frame with AR set whose destination PAN
 * and short address are @p my_pan and @p my_addr.  The RX ends into a TIFS
 * (192 us) TX turnaround; software swaps in the ACK, or aborts, within it.
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

#endif /* TIKU_NORDIC_IEEE154_ARCH_H_ */
