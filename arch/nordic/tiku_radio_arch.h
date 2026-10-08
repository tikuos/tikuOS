/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_radio_arch.h - nRF54L BLE radio: advertising, scanning, PHY probes.
 *
 * Blocking, polled paths on the 2.4 GHz RADIO (advertising, the PHY and RF
 * test paths) and an IRQ-driven observer scan.  Connections are the FLPR's
 * (arch/nordic/flpr), under tiku_bt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_RADIO_ARCH_H_
#define TIKU_NORDIC_RADIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure the RADIO for BLE 1M legacy advertising: MODE, packet
 *        format, adv access address, CRC, TX power and the TX-only SHORTS.
 */
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

/**
 * @brief Active scanner: send SCAN_REQs to the advertiser named @p name and
 *        time its SCAN_RSPs on this radio's clock, for @p ms.
 *
 * Counts and the reply gap (TIMER10 ticks from the request's PHYEND to the
 * reply's ADDRESS) go to the tiku_radio_arch_dbg_scanreq_* globals.
 *
 * @param scana  6-byte scanner address (ScanA)
 * @param name   Complete local name to match
 * @param ms     Scan duration in milliseconds
 * @return 0 when the advertiser was heard at all, -1 otherwise
 */
int tiku_radio_arch_scanreq_probe(const uint8_t *scana, const char *name,
                                  uint32_t ms);
/** TIMER10 ticks from an advert's end to the SCAN_REQ's TXEN (200 = 150 us). */
extern uint32_t tiku_radio_arch_scanreq_txen_ticks;
extern uint32_t tiku_radio_arch_dbg_scanreq_adv;     /**< target's ADV_INDs */
extern uint32_t tiku_radio_arch_dbg_scanreq_sent;    /**< SCAN_REQs sent    */
extern uint32_t tiku_radio_arch_dbg_scanreq_rsp;     /**< SCAN_RSPs heard   */
extern uint32_t tiku_radio_arch_dbg_scanreq_gap_min; /**< least reply gap   */
extern uint32_t tiku_radio_arch_dbg_scanreq_gap_max; /**< largest reply gap */
extern uint32_t tiku_radio_arch_dbg_scanreq_gap_sum; /**< sum of reply gaps */
extern uint32_t tiku_radio_arch_dbg_scanreq_crcbad;  /**< replies, bad CRC  */
extern uint32_t tiku_radio_arch_dbg_scanreq_wrong;   /**< other packets     */
extern uint32_t tiku_radio_arch_dbg_scanreq_silent;  /**< empty windows     */
/** First 16 bytes of the first three "wrong" packets. */
extern uint8_t  tiku_radio_arch_dbg_scanreq_pkt[3][16];

/**
 * @brief Build the SCAN_RSP that answers a SCAN_REQ.
 *
 * Same PDU shape as the advert under type 4.
 *
 * @note Give it data the advert does not carry: a scanner's duplicate filter
 *       drops a response that repeats the advert byte for byte, and a host
 *       waiting to pair the two then never reports the device at all.
 *
 * @param pdu     Output buffer (>= 40 bytes, RAM: the radio DMAs from it)
 * @param addr    6-byte advertiser address (little-endian, random static)
 * @param sd      scan-response AD structures
 * @param sd_len  AD length in bytes (capped at 31)
 * @return Total bytes written to @p pdu.
 */
uint8_t tiku_radio_arch_scanrsp_build(uint8_t *pdu, const uint8_t *addr,
                                     const uint8_t *sd, uint8_t sd_len);

/**
 * @brief Transmit @p pdu on all three advertising channels (blocking).
 *
 * @p pdu_len is unused: the length is the PDU's LENGTH byte.
 */
void tiku_radio_arch_adv_send(const uint8_t *pdu, uint8_t pdu_len);

/**
 * @brief Set the TX power in dBm (default +8, the strongest).
 *
 * TXPOWER is an enumerated register: only the silicon's discrete steps
 * (+8..+1, 0..-10, -12..-20 even, -22, -28, -40, -46) are legal; any other
 * value is rejected, never rounded.  Takes effect from the next ramp-up.
 *
 * @note Not while the RADIO is NonSecure for the FLPR beacon offload: a
 *       secure-alias write is then a precise bus fault.  The tiku_ble_adv
 *       facade reclaims and re-arms the RADIO around the call.
 * @return 0 on success, -1 if @p dbm is not a silicon-legal step.
 */
int tiku_radio_arch_set_txpower(int8_t dbm);

/** @brief Currently configured TX power in dBm. */
int8_t tiku_radio_arch_txpower(void);

/** @brief Enumerated TXPOWER code for the current setting (shared by 15.4). */
uint32_t tiku_radio_arch_txpower_code(void);

/** BLE PHYs the silicon can modulate. */
typedef enum {
    TIKU_RADIO_PHY_1M = 0,              /**< BLE 1M (legacy adv PHY)      */
    TIKU_RADIO_PHY_2M,                  /**< BLE 2M (not for legacy adv)  */
    TIKU_RADIO_PHY_CODED_S8,            /**< Coded S=8, 125 kbps          */
    TIKU_RADIO_PHY_CODED_S2,            /**< Coded S=2, 500 kbps          */
} tiku_radio_arch_phy_t;

/**
 * @brief One 3-channel TX burst at @p phy, reporting per-channel TX-state
 *        poll counts.
 *
 * Legacy advertising is 1M only, so a compliant scanner ignores a 2M or coded
 * burst on 37/38/39.  The poll count scales with airtime: for the same PDU,
 * about 0.5x at 2M, 3x at S=2 and 8x at S=8 relative to 1M.
 *
 * @note Restores the 1M MODE and PCNF0 before returning.
 * @param phy    PHY to probe.
 * @param iters  Out: TX-state poll iterations for ch 37/38/39.
 * @return 0 on success, -1 if any channel never reached DISABLED.
 */
int tiku_radio_arch_phy_tx_probe(tiku_radio_arch_phy_t phy,
                                 uint32_t iters[3]);

/**
 * @brief Transmit one prepared PDU at @p phy on advertising channel @p chan
 *        (0..2 = 37/38/39, others map to 0), on the PHY-link access address.
 *
 * @note Does not restore 1M: the caller loops, holds Constant Latency
 *       (erratum 20) and calls tiku_radio_arch_init() afterwards.
 * @return 0 on TX complete, -1 on ramp/PHYEND timeout or bad @p phy.
 */
int tiku_radio_arch_phy_tx(tiku_radio_arch_phy_t phy, uint8_t chan,
                           const uint8_t *pdu);

/**
 * @brief Receive at @p phy on advertising channel @p chan for @p window_ms,
 *        counting CRC-OK packets whose @p tag_len bytes at @p tag_off match
 *        @p tag (@p tag_len 0 counts every CRC-OK packet).
 *
 * The receiver stays armed between packets (TASKS_START from RXIDLE, no
 * ramp), on the access address of tiku_radio_arch_phy_tx().  Coded S=2 is
 * received in LR125 mode, which decodes S=2 too.
 *
 * @param rssi  Out, optional: RSSI of the last match, in dBm
 * @return the match count, or -1 on bad @p phy
 * @note Does not restore 1M; leaves the RADIO disabled with SHORTS 0.
 */
int tiku_radio_arch_phy_rx_count(tiku_radio_arch_phy_t phy, uint8_t chan,
                                 uint32_t window_ms, const uint8_t *tag,
                                 uint8_t tag_off, uint8_t tag_len,
                                 int8_t *rssi);

/**
 * @brief Start a continuous RF test transmission and leave it on.
 *
 * Unmodulated parks the RADIO in TXIDLE emitting a pure carrier at @p mhz,
 * one line on a spectrum analyser.  Modulated adds a spectrally busy
 * back-to-back payload via an END->START short, for occupied bandwidth.
 *
 * @note Returns with the RADIO enabled.  The caller must stop it before any
 *       beacon, scan or connection work, which all start from DISABLED.  A
 *       second start stops the previous carrier first; TX power is the one
 *       last selected.
 * @param phy        PHY whose modulation/preamble to use
 * @param mhz        Centre frequency in MHz, 2360..2500 (the low band
 *                   below 2400 is reached via FREQUENCY.MAP)
 * @param modulated  0 = unmodulated carrier, 1 = modulated
 * @return 0 on success, -1 for an out-of-band @p mhz or a ramp-up that
 *         never completed
 */
int tiku_radio_arch_carrier_start(tiku_radio_arch_phy_t phy,
                                  uint16_t mhz, int modulated);

/**
 * @brief Stop a test carrier and restore the beacon/scan contract.
 *
 * Forces the RADIO to DISABLED, returns MODE to 1M and releases the
 * per-operation Constant Latency.  Does nothing when no carrier is running.
 */
void tiku_radio_arch_carrier_stop(void);

/**
 * @brief Non-zero while a test carrier is transmitting.
 */
int tiku_radio_arch_carrier_active(void);

/**
 * @brief Live RADIO.STATE.
 *
 * For a test carrier: TXIDLE (0xA) while emitting an unmodulated carrier,
 * TX (0xB) while modulating; DISABLED is 0x0.
 */
uint32_t tiku_radio_arch_state(void);

/**
 * @brief CSA#1 next data channel (Core Vol 6 Part B 4.5.8.2).
 *
 * @param last_unmapped  Previous unmapped channel (advance with
 *                       @p unmapped_out, never with the return value).
 * @param hop            hopIncrement from CONNECT_IND (5..16).
 * @param chmap          37-bit channel map, LSB-first (5 bytes).
 * @param unmapped_out   Receives the new unmapped channel.
 * @return The (possibly remapped) data channel to use.
 */
uint8_t tiku_radio_ll_csa1_next(uint8_t last_unmapped, uint8_t hop,
                                const uint8_t chmap[5],
                                uint8_t *unmapped_out);

/** 1-bit SN/NESN acknowledgement window (Core Vol 6 Part B 4.5.9). */
typedef struct {
    uint8_t sn;                 /**< sequence number of the local PDU    */
    uint8_t nesn;               /**< sequence number expected from peer  */
} tiku_radio_ll_ack_t;

#define TIKU_RADIO_LL_NEWDATA  (1u << 0)  /**< rx payload is new, deliver */
#define TIKU_RADIO_LL_ACKED    (1u << 1)  /**< local TX acked, advance    */

/**
 * @brief Fold one received Data-PDU header into the ack window.
 *
 * Updates @p a and returns TIKU_RADIO_LL_NEWDATA / _ACKED flags -- either, both
 * or neither, the flips being independent.  @p has_payload must be nonzero only
 * for PDUs carrying one, so empty keepalives never forge a false ACK.
 */
uint8_t tiku_radio_ll_ack(tiku_radio_ll_ack_t *a, uint8_t rx_sn,
                          uint8_t rx_nesn, uint8_t has_payload);

/**
 * @brief One extended advertising event at 1M (blocking, ~1.3 ms).
 *
 * ADV_EXT_IND (ch 37, ADI + AuxPtr), then an AUX_ADV_IND on secondary
 * channel 20, launched by TIMER10 through DPPI, carrying AdvA and up to 200
 * bytes of AdvData; dbg_aux_us records its start (nominally 600 us).
 *
 * @note Requires the RADIO idle.
 * @return 0 on success, -1 EXT_IND never finished, -2 aux never flew.
 */
int tiku_radio_arch_extadv_burst(const uint8_t *addr,
                                 const uint8_t *ad, uint8_t ad_len);

/** Aux start after the EXT_IND's READY, microseconds (TIMER10 CC[2]). */
extern uint32_t tiku_radio_arch_dbg_aux_us;

/**
 * @brief Counts from the current or last scan: interrupts serviced, address
 *        matches, and packets whose CRC held.
 *
 * isr rising with addr at 0 means armed but hearing nothing; addr rising
 * with crcok at 0 means heard but corrupt.  Any pointer may be NULL.
 */
void tiku_radio_arch_scan_counts(uint32_t *isr, uint32_t *addr,
                                 uint32_t *crcok);

/**
 * @brief Session-scoped Constant Latency hold (nRF54L15 erratum 20).
 *
 * A duty-cycled radio user holds Constant Latency across the sleeps between
 * bursts, as the erratum requires; while held, the per-operation exit does
 * nothing.  @p on 0 releases it to Low Power.
 */
void tiku_radio_arch_constlat_hold(int on);

/**
 * @brief Request the HF clock for about 1.4 ms with a 16-byte UARTE21 TX DMA
 *        (pins disconnected).
 *
 * Call before a TXEN or RXEN and periodically across a long RX.  The 15.4
 * PHY, which shares the RADIO, calls it too.
 */
void tiku_radio_arch_hfclk_kick(void);

/** @brief Live RADIO.MODE decoded ("ble-1m" / "ieee802154" / ...). */
const char *tiku_radio_arch_mode_str(void);

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
 * Runs the IRQ observer for @p ms of wall clock, sleeping in WFE, and hands
 * each CRC-OK packet to @p cb with the RSSI latched at its ADDRESS.  Heard
 * packets confirm the shared frequency, AA, whitening and CRC config.
 *
 * @param cb          Per-packet callback, or NULL
 * @param ud          Opaque context for @p cb
 * @param ms          Scan duration in milliseconds
 * @param addr_evts   Optional: the scan's access-address matches are added
 * @param crcok_evts  Optional: the scan's CRC-OK packets are added
 */
void tiku_radio_arch_scan(tiku_radio_arch_scan_cb_t cb, void *ud, uint32_t ms,
                          uint32_t *addr_evts, uint32_t *crcok_evts);

/**
 * @brief Arm the non-blocking observer: clear the ring and counters, enter
 *        Constant Latency (erratum 20) until scan_stop() and start RX.
 *
 * Call tiku_radio_arch_scan_service() every tick or two from cooperative
 * context, then tiku_radio_arch_scan_stop(); the blocking scan wraps these.
 */
void tiku_radio_arch_scan_start(void);

/**
 * @brief Drain the ISR's packet ring and run the safety rotation.
 *
 * The cooperative half of the observer: pops every packet the RADIO_0 ISR
 * queued (8-entry SPSC ring; a full ring drops new packets) and hands each to
 * @p cb with its latched RSSI.  Call every tick or two, never from an ISR.
 *
 * @note Also the safety net: with no RADIO interrupt for 4 ticks (2 in
 *       TIMER10-tick builds) it forces a channel rotation and counts it in
 *       dbg_win_forced, which reads 0 while the hardware window works.
 *       After scan_stop() it only drains.
 * @param cb  Per-packet callback; NULL discards the drained packets.
 * @param ud  Opaque context passed to @p cb.
 * @return Number of packets delivered on this call.
 */
uint8_t tiku_radio_arch_scan_service(tiku_radio_arch_scan_cb_t cb, void *ud);

/**
 * @brief Disarm the observer and release the radio.
 *
 * Masks the RADIO IRQ, unwires the TIMER10->DPPI window, drives TASKS_DISABLE,
 * restores the TX-only SHORTS contract, then drops the per-operation Constant
 * Latency unless a beacon session holds it (erratum 20).
 *
 * @note A live SUBSCRIBE_DISABLE left behind would later kill a TX burst
 *       mid-air.  Ring stragglers survive: call scan_service() once more.
 */
void tiku_radio_arch_scan_stop(void);

/**
 * @brief Lend the radio to one TX burst: disarm the RX engine and leave the
 *        RADIO idle with the TX-only SHORTS tiku_radio_arch_adv_send() uses.
 *
 * The packet ring and Constant Latency (held by the beacon session) are left
 * alone; tiku_radio_arch_scan_resume() re-arms RX with the ring intact.
 *
 * @note Pause, the burst and resume run from cooperative context, no yield.
 */
void tiku_radio_arch_scan_pause(void);

/**
 * @brief Re-arm the RX engine after a borrowed TX burst.
 *
 * Restores the RX SHORTS, re-enables the RADIO IRQ, re-wires the TIMER10->DPPI
 * listen window and restarts RX on the current scan channel.  The packet ring
 * is untouched, so anything queued before the pause survives.
 *
 * @note Constant Latency is left alone, the beacon session holding it.  Must
 *       pair with a preceding scan_pause() from cooperative context, with no
 *       yield between the two.
 */
void tiku_radio_arch_scan_resume(void);

/** TX diagnostics from the last transmitted channel: EVENTS_READY and
 *  EVENTS_DISABLED. */
extern uint32_t tiku_radio_arch_dbg_ready, tiku_radio_arch_dbg_disabled;
/** The final RADIO.STATE and the number of polls. */
extern uint32_t tiku_radio_arch_dbg_state, tiku_radio_arch_dbg_spin;
/** Polls spent in TXRU and in TX. */
extern uint32_t tiku_radio_arch_dbg_ru_iters, tiku_radio_arch_dbg_tx_iters;
/** Clock diagnostics: XO.STAT at the last radio operation's entry, and
 *  dbg_xo_wait, which is always 0 (the HF clock kick does not wait). */
extern uint32_t tiku_radio_arch_dbg_xo_stat, tiku_radio_arch_dbg_xo_wait;
/** Radio operations that found the XO restarted or stopped. */
extern uint32_t tiku_radio_arch_dbg_xo_restarts;
/** Scan windows closed by the TIMER10->DPPI window (win_hw) and by the
 *  drain loop's forced rotation (win_forced, 0 while the window works). */
extern uint32_t tiku_radio_arch_dbg_win_hw, tiku_radio_arch_dbg_win_forced;

#endif /* TIKU_NORDIC_RADIO_ARCH_H_ */
