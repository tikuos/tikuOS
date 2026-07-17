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
 * @brief Set the TX power in dBm (default +8, the strongest).
 *
 * TXPOWER is an enumerated register: only the silicon's discrete steps
 * (+8..+1, 0..-10, -12..-20 even, -22, -28, -40, -46) are legal; any other
 * value is rejected, never rounded.  Takes effect from the next ramp-up.
 * Must NOT be called while the RADIO is flipped NonSecure for the FLPR
 * beacon offload (secure-alias write = precise bus fault) -- the
 * tiku_ble_adv facade owns that reclaim/re-arm dance.
 *
 * @return 0 on success, -1 if @p dbm is not a silicon-legal step.
 */
int tiku_radio_arch_set_txpower(int8_t dbm);

/** @brief Currently configured TX power in dBm. */
int8_t tiku_radio_arch_txpower(void);

/**
 * @brief Session-scoped Constant Latency hold (nRF54L15 erratum 20).
 *
 * A duty-cycled radio user (background beacon) must hold Constant Latency
 * across the SLEEPS between its bursts, not just during each burst --
 * otherwise the erratum corrupts the on-air payload after a tickless idle.
 * While held, the per-operation exit to Low Power is suppressed.
 */
void tiku_radio_arch_constlat_hold(int on);

/**
 * @brief Per-packet observer callback (CRC-OK packets only).
 *
 * @param buf   Raw RAM buffer: [S0][LEN][S1 slot][payload...] -- the
 *              erratum-49 S1INCL slot shifts received payload to byte 3.
 * @param len   The on-air LENGTH byte (payload byte count).
 * @param rssi  RSSI of the packet in dBm.
 * @param ud    Opaque context.
 */
typedef void (*tiku_radio_arch_scan_cb_t)(const uint8_t *buf, uint8_t len,
                                          int8_t rssi, void *ud);

/**
 * @brief Observer scan on 37/38/39 with the TX link config (blocking).
 *
 * Round-robins the advertising channels for @p ms milliseconds, invoking
 * @p cb per CRC-OK packet with RSSI.  Counters are optional (NULL ok).
 */
void tiku_radio_arch_scan(tiku_radio_arch_scan_cb_t cb, void *ud, uint32_t ms,
                          uint32_t *addr_evts, uint32_t *crcok_evts);

/* Bring-up diagnostics captured on the last transmitted channel: the radio
 * TX path is proven on-die when READY and DISABLED both read 1 (STATE
 * returns to 0/DISABLED) AND dbg_tx_iters shows the modulator held the TX
 * state for the frame duration. */
extern uint32_t tiku_radio_arch_dbg_ready, tiku_radio_arch_dbg_disabled;
extern uint32_t tiku_radio_arch_dbg_state, tiku_radio_arch_dbg_spin;
extern uint32_t tiku_radio_arch_dbg_ru_iters, tiku_radio_arch_dbg_tx_iters;
/* HFXO gate diagnostics: XO.STAT at the last radio op's entry, and how many
 * poll iterations the XOTUNED wait took (0-ish = XO was hot). */
extern uint32_t tiku_radio_arch_dbg_xo_stat, tiku_radio_arch_dbg_xo_wait;
extern uint32_t tiku_radio_arch_dbg_xo_restarts;

#endif /* TIKU_NORDIC_RADIO_ARCH_H_ */
