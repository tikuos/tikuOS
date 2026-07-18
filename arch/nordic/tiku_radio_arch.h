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

/** @brief Enumerated TXPOWER code for the current setting (shared by 15.4). */
uint32_t tiku_radio_arch_txpower_code(void);

/** BLE PHYs the silicon can modulate (kintsugi/radio.md R8). */
typedef enum {
    TIKU_RADIO_PHY_1M = 0,              /**< BLE 1M (legacy adv PHY)      */
    TIKU_RADIO_PHY_2M,                  /**< BLE 2M (no legacy adv!)      */
    TIKU_RADIO_PHY_CODED_S8,            /**< Coded S=8, 125 kbps          */
    TIKU_RADIO_PHY_CODED_S2,            /**< Coded S=2, 500 kbps          */
} tiku_radio_arch_phy_t;

/**
 * @brief One 3-channel TX burst at @p phy, reporting per-channel TX-state
 *        poll-iteration counts (R8.1's single-board PHY oracle).
 *
 * Legacy advertising is 1M-ONLY by spec -- a 2M/coded burst on 37/38/39
 * is inaudible to any compliant scanner, so this is a bring-up probe,
 * not a beacon: the proof is ON-DIE.  The iteration count of the polled
 * TX window scales with airtime (verified against host decode during 1M
 * bring-up), so the same PDU must show ~0.5x iterations at 2M, ~3x at
 * S=2 and ~8x at S=8 relative to 1M.  If the modulator runs at the new
 * rate, the ratios cannot lie.  Restores the 1M link config before
 * returning, so beacon/scan semantics are untouched.
 *
 * @param phy    PHY to probe.
 * @param iters  Out: TX-state poll iterations for ch 37/38/39.
 * @return 0 on success, -1 if any channel never reached DISABLED.
 */
int tiku_radio_arch_phy_tx_probe(tiku_radio_arch_phy_t phy,
                                 uint32_t iters[3]);

/**
 * @brief Two-board per-PHY link (R8.2): transmit one prepared PDU at @p phy
 *        on adv channel @p chan (0..2 = 37/38/39).  Neither restores 1M --
 *        the caller loops (holding Constant Latency) then re-inits.
 * @return 0 on TX complete, -1 on ramp/PHYEND timeout or bad @p phy.
 */
int tiku_radio_arch_phy_tx(tiku_radio_arch_phy_t phy, uint8_t chan,
                           const uint8_t *pdu);

/**
 * @brief Continuously receive at @p phy on adv channel @p chan for
 *        @p window_ms, counting CRC-OK packets whose @p tag_len bytes at
 *        @p tag_off match @p tag (tag_len 0 = count all CRC-OK).  Stays armed
 *        (in-place TASKS_START re-arm, no ramp between packets) so it is not
 *        RX-gap-limited.  Uses the adv access address + CRC (PHY-independent).
 * @return the match count, or -1 on bad @p phy.  @p rssi = last match's dBm.
 */
int tiku_radio_arch_phy_rx_count(tiku_radio_arch_phy_t phy, uint8_t chan,
                                 uint32_t window_ms, const uint8_t *tag,
                                 uint8_t tag_off, uint8_t tag_len,
                                 int8_t *rssi);

/**
 * @brief Connectable advertising + CONNECT_IND capture (L-track L1).
 *
 * Transmits ADV_IND and hardware-turns-around (DISABLED_RXEN short)
 * into an RX window on the same channel, where a central answers
 * T_IFS=150 us after our packet: SCAN_REQs are counted, a CONNECT_IND
 * addressed to @p addr has its 22-byte LLData copied out (AA, CRCInit,
 * WinSize/Offset, Interval, Latency, Timeout, ChM, Hop|SCA).
 * Deliberately does not respond -- L1's exit is the decoded capture.
 * Blocking, polled, watchdog-kicked; radio must be idle (arbiter).
 *
 * @return 1 = CONNECT_IND captured, 0 = timeout after @p ms.
 */
int tiku_radio_arch_connadv_probe(const uint8_t *addr, const uint8_t *ad,
                                  uint8_t ad_len, uint8_t lldata[22],
                                  uint32_t ms);
extern uint32_t tiku_radio_arch_dbg_connadv_tx;
extern uint32_t tiku_radio_arch_dbg_connadv_scanreq;
extern uint32_t tiku_radio_arch_dbg_connadv_rsp;   /* SCAN_RSPs (L2)     */
extern uint32_t tiku_radio_arch_dbg_connadv_tifs;  /* measured T_IFS, us */
extern uint32_t tiku_radio_arch_dbg_connadv_rxother;

/**
 * @brief CSA#1 next data channel (L3 groundwork; Core 4.5.8.2).
 *
 * @param last_unmapped  Previous UNMAPPED channel (advance with
 *                       @p unmapped_out, never with the return value --
 *                       the classic implementation bug).
 * @param hop            hopIncrement from CONNECT_IND (5..16).
 * @param chmap          37-bit channel map, LSB-first (5 bytes).
 * @param unmapped_out   Receives the new unmapped channel.
 * @return The (possibly remapped) data channel to use.
 */
uint8_t tiku_radio_ll_csa1_next(uint8_t last_unmapped, uint8_t hop,
                                const uint8_t chmap[5],
                                uint8_t *unmapped_out);

/** 1-bit SN/NESN acknowledgement window (L3 groundwork; Core 4.5.9). */
typedef struct {
    uint8_t sn;                 /**< seq number of the PDU I transmit    */
    uint8_t nesn;               /**< seq number I expect next from peer  */
} tiku_radio_ll_ack_t;

/** Connection outcome stats (L3). */
typedef struct {
    uint32_t events;            /**< connection events attempted         */
    uint32_t rx_ok;             /**< events with a CRC-valid central PDU */
    uint32_t addr_seen;         /**< AA matched but CRC bad (decode diag)*/
    uint32_t missed;            /**< events that heard nothing           */
    uint32_t ms;                /**< connection lifetime, ms             */
    int32_t  first_delta;       /**< (actual - predicted) 1st anchor, us */
    uint8_t  fail_bytes[5];     /**< first CRC-failed packet's RAM bytes  */
    uint16_t interval;          /**< connInterval, 1.25ms units (diag)   */
    uint16_t winoff;            /**< transmitWindowOffset units (diag)   */
    uint32_t ctrl_tx;           /**< LL control PDUs we sent+acked (L4)  */
    uint32_t ctrl_rx;           /**< LL control PDUs received (L4)        */
    uint8_t  peer_vers;         /**< peer VersNr from LL_VERSION_IND (L4) */
    uint8_t  winsize;           /**< transmitWindowSize units (diag)     */
    uint8_t  first_chan;        /**< first CSA#1 data channel (diag)      */
    uint8_t  hop;               /**< hopIncrement (diag)                  */
    uint8_t  reason;            /**< 0 caller-cap, 1 supervision, 2 never */
    uint8_t  att_step;          /**< ATT client progress 0..8 (L5/L6)    */
    uint8_t  att_ok;            /**< ATT read-back matched write (L5)    */
    uint8_t  att_readback;      /**< value read back over ATT (L5)       */
    uint8_t  att_disc;          /**< GATT discovery matched (L6)         */
} tiku_radio_ll_conn_stats_t;

/**
 * @brief Advertise connectably, accept ONE central, and hold the link (L3).
 *
 * The peripheral-role connection engine: ADV_IND until a CONNECT_IND for
 * @p addr arrives, then per connection event RX the central's PDU on the
 * CSA#1 data channel and hardware-T_IFS respond with an empty PDU (correct
 * SN/NESN), re-syncing the anchor to each packet until the supervision
 * timeout or @p max_secs.  Blocking + polled (parks the shell while
 * connected); empty PDUs only -- enough to keep the link UP.
 *
 * @return 0 once a connection was held (see @p st->events / reason), or
 *         -1 if no central connected within @p max_secs.
 */
int tiku_radio_arch_connect(const uint8_t *addr, const uint8_t *ad,
                            uint8_t ad_len, uint32_t max_secs,
                            tiku_radio_ll_conn_stats_t *st);

/**
 * @brief CENTRAL role: scan for TIKU-CONN, connect, drive the link (L3
 *        two-board harness).
 *
 * The debug oracle for L3: as master we impose the CONNECT_IND params
 * (deterministic anchor, lenient windows/supervision) and define the
 * event cadence, so a board-to-board connection can be brought up with
 * ground truth instead of a black-box phone.  Blocking + polled.
 *
 * @return 0 once a connection ran (see @p st), -1 if no peripheral found.
 */
int tiku_radio_arch_central(const uint8_t *my_addr, uint32_t max_secs,
                            tiku_radio_ll_conn_stats_t *st);

/**
 * @brief Arm the Phase A LL-control exercise on the next central() run.
 *
 * When on, the master sends LL_CHANNEL_MAP_UPDATE_IND then
 * LL_CONNECTION_UPDATE_IND mid-connection (after the ATT loopback),
 * applying each at its Instant.  Used to prove the peripheral follows both
 * (audit G6); off by default so normal central runs are unaffected.
 */
void tiku_radio_arch_central_updates(uint8_t on);

/** Peripheral T_IFS measured by the central (us), ground truth for L3. */
extern uint32_t tiku_radio_arch_dbg_cen_tifs;

#define TIKU_RADIO_LL_NEWDATA  (1u << 0)  /**< rx payload is new, deliver */
#define TIKU_RADIO_LL_ACKED    (1u << 1)  /**< my TX landed, advance      */

/**
 * @brief Fold one received Data-PDU header into the ack window.
 *
 * Updates @p a and returns TIKU_RADIO_LL_NEWDATA / _ACKED flags (either,
 * both, or neither -- the flips are independent).  @p has_payload must be
 * nonzero only for PDUs that carry a payload (LEN != 0); NESN advances
 * solely for those, so empty keepalives never forge a false ACK.
 */
uint8_t tiku_radio_ll_ack(tiku_radio_ll_ack_t *a, uint8_t rx_sn,
                          uint8_t rx_nesn, uint8_t has_payload);

/**
 * @brief One extended advertising event at 1M (R8.3a, blocking ~1.3 ms).
 *
 * ADV_EXT_IND (ch 37, ADI + AuxPtr) followed by a HARDWARE-timed
 * AUX_ADV_IND (secondary ch 20) carrying AdvA + up to 200 bytes of
 * AdvData -- the >31-byte payloads legacy advertising cannot.  The aux
 * launch is TIMER10+DPPI-exact; dbg_aux_us captures the aux packet's
 * actual start relative to the EXT_IND (nominal 600, the AuxPtr
 * offset).  Requires the radio idle (the facade arbiter's job).
 * Coded-PHY variant is a MODE/AuxPtr-PHY change once a coded-capable
 * receiver exists (kintsugi/radio.md R8.3).
 *
 * @return 0 on success, -1 EXT_IND never finished, -2 aux never flew.
 */
int tiku_radio_arch_extadv_burst(const uint8_t *addr,
                                 const uint8_t *ad, uint8_t ad_len);

/** On-die aux-timing proof: CC[2] capture of the aux start (us). */
extern uint32_t tiku_radio_arch_dbg_aux_us;

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
 * @brief Start (and re-arm) the HFXO the erratum-safe way before a TXEN /
 *        RXEN, and hold it across a long RX.  Shared with the 15.4 PHY,
 *        which drives this same RADIO from a separate arch file.
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
 * Round-robins the advertising channels for @p ms milliseconds, invoking
 * @p cb per CRC-OK packet with RSSI.  Counters are optional (NULL ok).
 */
void tiku_radio_arch_scan(tiku_radio_arch_scan_cb_t cb, void *ud, uint32_t ms,
                          uint32_t *addr_evts, uint32_t *crcok_evts);

/*
 * Non-blocking observer engine (R7): the same IRQ+hardware-window machine
 * as the blocking scan, split so a background service can own it.
 * start() arms it (holds Constant Latency until stop -- erratum 20);
 * scan_service() drains the ISR's packet ring into @p cb and runs the
 * counted safety rotation -- call it every tick or two from a timer
 * callback; stop() disarms and releases the radio (one more service()
 * call afterwards drains teardown stragglers).  The blocking scan is a
 * start/service+WFE/stop wrapper around exactly these.
 */
void tiku_radio_arch_scan_start(void);
uint8_t tiku_radio_arch_scan_service(tiku_radio_arch_scan_cb_t cb, void *ud);
void tiku_radio_arch_scan_stop(void);

/*
 * Time-division radio borrow (R7.5): let a beacon share the radio with a
 * running observer.  pause() disarms the RX engine and leaves the radio
 * idle with TX shorts -- ready for tiku_radio_arch_adv_send() -- WITHOUT
 * resetting the packet ring or dropping Constant Latency (the beacon
 * session holds it).  resume() re-arms RX, ring intact.  Both are
 * no-yield and must bracket a single burst from cooperative context.
 */
void tiku_radio_arch_scan_pause(void);
void tiku_radio_arch_scan_resume(void);

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
/* Scan-window diagnostics (R6.2): win_hw counts channels closed by the
 * TIMER10->DPPI hardware window; win_forced counts the drain loop's
 * coarse safety rotation.  With the hardware window alive, forced MUST
 * read 0 -- a nonzero value means the DPPI wiring is dead and the scan
 * is silently limping on the fallback. */
extern uint32_t tiku_radio_arch_dbg_win_hw, tiku_radio_arch_dbg_win_forced;

#endif /* TIKU_NORDIC_RADIO_ARCH_H_ */
