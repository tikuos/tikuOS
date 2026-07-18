/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ieee154_arch.h - from-scratch IEEE 802.15.4-2006 250 kbps PHY on the
 *                       nRF54L on-die RADIO (N1 PHY bring-up; kintsugi/
 *                       radio.md N-track).  Clean-room: MDK registers only,
 *                       no SoftDevice / OpenThread / sdk-nrf.
 *
 * The RADIO is a single peripheral shared with the BLE facade; 15.4 mode
 * REPLACES the link config (MODE/PCNF/CRC/SFD).  Callers must own the
 * radio (idle BLE first) and restore BLE with tiku_ieee154_arch_mode_ble()
 * when done.  PHY only -- no addressing, ACK, or CSMA yet (that is N2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_IEEE154_ARCH_H_
#define TIKU_NORDIC_IEEE154_ARCH_H_

#include <stdint.h>

/** @brief 2.4 GHz 802.15.4 channel page-0 range (2405..2480 MHz). */
#define TIKU_154_CHAN_MIN     11u
#define TIKU_154_CHAN_MAX     26u

/** @brief Max on-air frame (PHR counts it): 127 B incl the 2-byte FCS. */
#define TIKU_154_MAX_FRAME   127u
/** @brief Max MAC payload we hand to/from the caller (frame minus FCS). */
#define TIKU_154_MAX_PSDU    125u

/** @brief 1 if this build has the 15.4 PHY (nRF54L on-die RADIO). */
int tiku_ieee154_arch_available(void);

/**
 * @brief Switch the RADIO into 802.15.4 mode on @p channel (11..26).
 * Reprograms MODE/PCNF/CRC/SFD/FREQUENCY; leaves the radio DISABLED.
 * The caller must already own the peripheral (BLE idle).
 */
void tiku_ieee154_arch_mode_154(uint8_t channel);

/** @brief Restore the BLE-advertising link config (tiku_radio_arch_init). */
void tiku_ieee154_arch_mode_ble(void);

/** @brief Retune to @p channel while staying in 15.4 mode. */
void tiku_ieee154_arch_set_channel(uint8_t channel);

/**
 * @brief Blocking transmit of a raw MAC frame (no FCS -- the radio appends
 *        it).  @p len is the MAC payload length (<= TIKU_154_MAX_PSDU).
 * @return 0 on PHYEND, -1 bad length, -2 ramp/TX timeout.
 */
int tiku_ieee154_arch_tx(const uint8_t *psdu, uint8_t len);

/**
 * @brief Blocking receive of one frame, up to @p timeout_ms.
 * @param buf   out: MAC payload (FCS stripped), up to @p cap bytes.
 * @param rssi  out (optional): RSSI in dBm of the received frame.
 * @return >0 payload length (FCS OK), 0 timeout, -1 frame with bad FCS.
 */
int tiku_ieee154_arch_rx(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                         int8_t *rssi);

/**
 * @brief Energy-detect one sample on @p channel (leaves the radio in 15.4
 *        mode, DISABLED).
 * @param dbm  out (optional): approximate energy level in dBm.
 * @return the raw ED level (0..255), or -1 on ramp/ED timeout.
 */
int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm);

/**
 * @brief Clear-channel assessment (energy-detect mode) on the current
 *        channel.  Leaves the radio DISABLED.
 * @return 1 channel idle (clear to send), 0 busy or ramp/CCA timeout.
 */
int tiku_ieee154_arch_cca(void);

#endif /* TIKU_NORDIC_IEEE154_ARCH_H_ */
