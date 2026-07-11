/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_radio_arch.h - nRF54L15 BLE advertising (TX-only broadcaster).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_RADIO_ARCH_H_
#define TIKU_NORDIC_RADIO_ARCH_H_

#include <stdint.h>

/** @brief Configure the RADIO for BLE 1M legacy advertising (call once). */
void tiku_radio_arch_init(void);

/**
 * @brief Build an ADV_NONCONN_IND PDU into @p pdu.
 *
 * The buffer carries the erratum-49 S1 RAM slot between LENGTH and the
 * payload ([S0][LEN][S1=AdvA0][AdvA][AD]); the slot is not transmitted.
 *
 * @param pdu     Output buffer (>= 40 bytes, RAM: the radio DMAs from it)
 * @param addr    6-byte advertiser address (little-endian, random static)
 * @param ad      AD structures (flags / name / manufacturer data)
 * @param ad_len  AD length in bytes (capped at 31)
 * @return Total bytes written to @p pdu (header + length + S1 + payload)
 */
uint8_t tiku_radio_arch_adv_build(uint8_t *pdu, const uint8_t *addr,
                                  const uint8_t *ad, uint8_t ad_len);

/** @brief Transmit @p pdu on all three advertising channels (blocking). */
void tiku_radio_arch_adv_send(const uint8_t *pdu, uint8_t pdu_len);

/**
 * @brief Diagnostic RX probe (listens on 37/38/39 with the TX link config).
 * @return 1 if a CRC-OK advertising PDU was captured (AdvA in @p out_adva).
 */
int tiku_radio_arch_rx_probe(uint8_t *out_adva, uint32_t *addr_evts,
                             uint32_t *crcok_evts, uint32_t rounds);

/* Bring-up diagnostics captured on the last transmitted channel: the radio
 * TX path is proven on-die when READY and DISABLED both read 1 (STATE
 * returns to 0/DISABLED) AND dbg_tx_iters shows the modulator held the TX
 * state for the frame duration. */
extern uint32_t tiku_radio_arch_dbg_ready, tiku_radio_arch_dbg_disabled;
extern uint32_t tiku_radio_arch_dbg_state, tiku_radio_arch_dbg_spin;
extern uint32_t tiku_radio_arch_dbg_ru_iters, tiku_radio_arch_dbg_tx_iters;

#endif /* TIKU_NORDIC_RADIO_ARCH_H_ */
