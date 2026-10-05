/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_arch.h - nRF54L FLPR (VPR RISC-V) coprocessor control.
 *
 * App-core side of the coprocessor: load and start the embedded image, park
 * and resume it, and drive the firmware's services (mailbox, pulse engine,
 * beacon, RX probe, BLE controller, compute load) through the shared page.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_FLPR_ARCH_H_
#define TIKU_NORDIC_FLPR_ARCH_H_

#include <stdint.h>

/**
 * @brief Start the coprocessor: load the image the first time, then resume
 *        a parked firmware or restart a faulted one.
 *
 * The first call copies the embedded image into the carve, scrubs the shared
 * page, sets INITPC and CPURUN, and arms the doorbell once the magic appears.
 * The image is never reloaded in the same power-on.
 *
 * @return 0 on success, -1 if the embedded image is missing or oversized, or
 *         a faulted payload does not come back.
 */
int tiku_flpr_arch_start(void);

/**
 * @brief Park the firmware in its polling loop and wait (bounded) for the
 *        answer.  CPURUN stays set.
 */
void tiku_flpr_arch_stop(void);

/** @brief 1 when the image has been loaded and the firmware is not parked. */
int tiku_flpr_arch_running(void);

/** @brief The payload's boot/fault magic, raw; 0 before the first launch. */
uint32_t tiku_flpr_arch_magic(void);

/** @brief 1 when the firmware has stamped its magic (reached main()). */
int tiku_flpr_arch_alive(void);

/** @brief Current heartbeat counter from the shared page. */
uint32_t tiku_flpr_arch_heartbeat(void);

/** @brief Embedded image size in bytes (0 if the build carries none). */
uint32_t tiku_flpr_arch_image_size(void);

/**
 * @brief Send one message (<= TIKU_FLPR_MSG_CAP bytes) to the firmware.
 * @return 0 on success, negative when not running / oversized.
 */
int tiku_flpr_arch_send(const void *data, uint32_t len);

/** @brief Capture a pending flpr->app message as the doorbell ISR would. */
void tiku_flpr_arch_poll(void);

/** @brief Count of flpr->app messages captured (ISR or pull). */
uint32_t tiku_flpr_arch_reply_seq(void);

/** @brief Copy the most recent reply into @p out; returns its length. */
uint32_t tiku_flpr_arch_reply(void *out, uint32_t cap);

/**
 * @brief Command a waveform from the pulse engine and verify it.
 *
 * Blocks while the firmware emits @p edges transitions at 50%% duty with
 * @p period_us microsecond period on P2.07 (LED3), sampling the same pad
 * from this core the whole time.
 *
 * @param measured  Out: transitions observed by this core's sampler.
 * @param ms        Out: wall-clock milliseconds the pattern took (pace
 *                  calibration: expected = period_us * edges / 2000).
 * @return 0 done, -1 bad args / not running, -2 firmware never finished.
 */
int tiku_flpr_arch_pulse(uint32_t period_us, uint32_t edges,
                         uint32_t *measured, uint32_t *ms);

/**
 * @brief Start a compute-only load on the coprocessor (non-blocking).
 *
 * Returns once the command is posted, so the caller can sleep while the
 * coprocessor works.  The load drives no pin and leaves the radio alone.
 *
 * @param iters Outer passes; each is 4096 register-only inner iterations.
 * @return 0 if handed over, -1 if the coprocessor is not running.
 */
int tiku_flpr_arch_spin_start(uint32_t iters);

/** @brief Outer passes the coprocessor has retired so far. */
uint32_t tiku_flpr_arch_spin_passes(void);

/** @brief End a compute load early; the firmware returns to its main loop. */
void tiku_flpr_arch_spin_abort(void);

/** @brief Non-zero once the coprocessor has finished the requested passes. */
int tiku_flpr_arch_spin_done(void);

/**
 * @brief Run a fixed compute load and time it against the GRTC.
 *
 * Blocks until the passes finish.  The coprocessor shares HCLK128M with the
 * application core, so the same work takes half as long at 128 MHz as at
 * 64 MHz.
 *
 * @note The app core busy-polls while waiting and contends for the shared
 *       SRAM, which slows the coprocessor; tiku_flpr_arch_spin_start() leaves
 *       the app core free to sleep.
 * @param iters   Outer passes to run.
 * @param passes  Out: passes retired (may be NULL).
 * @param us      Out: elapsed GRTC microseconds (may be NULL).
 * @return 0 on completion, -1 if not running, -2 if it never reported done.
 */
int tiku_flpr_arch_spin_timed(uint32_t iters, uint32_t *passes, uint32_t *us);

/**
 * @brief Offload duty-cycled BLE beaconing to the coprocessor.
 *
 * Makes RADIO and UARTE21 non-secure and starts beacon mode.
 *
 * @note The radio link-config registers must already be programmed
 *       (tiku_radio_arch_init) and the session CONSTLAT hold taken.  While
 *       offloaded the M33 does not touch RADIO or UARTE21.
 * @param pdu          RAM-format PDU ([S0][LEN][S1][payload...]).
 * @param len          Buffer bytes (<= 48).
 * @param interval_ms  Burst interval.
 * @return 0 on success, negative if not running / bad args.
 */
int tiku_flpr_arch_beacon(const uint8_t *pdu, uint32_t len,
                          uint32_t interval_ms);

/** @brief Stop the offloaded beacon and restore peripheral security. */
void tiku_flpr_arch_beacon_stop(void);

/** @brief Bursts transmitted by the coprocessor since beacon start. */
uint32_t tiku_flpr_arch_beacon_bursts(void);

/**
 * @brief Listen on advertising channel 37 from the FLPR and report what it
 *        heard.
 *
 * Blocks while the FLPR listens (1500 RX windows), with RADIO and UARTE21
 * non-secure for the duration.
 *
 * @note Same handoff as the beacon: the link config is programmed and the
 *       CONSTLAT hold taken before the call.
 * @param addr_evts   Out: ADDRESS matches (AA matched, CRC unchecked).
 * @param crcok_evts  Out: CRC-valid packets received.
 * @param first       Out: head bytes of the first CRC-valid packet.
 * @param cap         Capacity of @p first.
 * @param flen        Out: bytes written to @p first.
 * @return 0 done, -1 not running, -2 firmware never finished.
 */
int tiku_flpr_arch_rxprobe(uint32_t *addr_evts, uint32_t *crcok_evts,
                           uint8_t *first, uint32_t cap, uint32_t *flen);

/** @brief Parsed CONNECT_IND the FLPR captured. */
typedef struct {
    uint32_t aa;            /**< data-channel access address        */
    uint32_t crcinit;       /**< 24-bit CRC init                    */
    uint16_t interval;      /**< connInterval, 1.25 ms units        */
    uint16_t timeout;       /**< supervision timeout, 10 ms units   */
    uint8_t  hop;           /**< hopIncrement                       */
    uint8_t  winsize;       /**< transmitWindowSize units           */
} tiku_flpr_conn_info_t;

/**
 * @brief Advertise connectably from the FLPR and capture the CONNECT_IND.
 *
 * Blocks until a central connects, the FLPR gives up, or the M33's spin
 * bound runs out.  On -2 the radio is secure again; on 0 the FLPR holds the
 * link and the radio stays non-secure until tiku_flpr_arch_conn_stop().
 *
 * @note Same handoff as the beacon: tiku_radio_arch_init has run and the
 *       CONSTLAT hold is taken.
 * @param adv      Connectable ADV PDU ([S0=0x40][LEN][S1][AdvA][AD...]).
 * @param adv_len  Bytes in @p adv (<= 48).
 * @param addr     AdvA to match in the CONNECT_IND (6 bytes).
 * @param rsp      SCAN_RSP PDU answering a SCAN_REQ; 0 length leaves the
 *                 controller mirroring the advert, which a scanner's
 *                 duplicate filter may drop as a repeat.
 * @param rsp_len  bytes in @p rsp.
 * @param out      Filled with the parsed CONNECT_IND when connected.
 * @return 0 connected (out filled), -1 not running / bad args, -2 gave up.
 */
int tiku_flpr_arch_conn_capture(const uint8_t *adv, uint32_t adv_len,
                                const uint8_t *rsp, uint32_t rsp_len,
                                const uint8_t *addr,
                                tiku_flpr_conn_info_t *out);

/** @brief 1 while the FLPR is holding a live connection. */
int tiku_flpr_arch_conn_active(void);

/**
 * @brief Advertising telemetry for the last (or running) advertise session.
 *
 * @param tx      ADV_IND PDUs transmitted.
 * @param scanreq SCAN_REQs addressed to this advertiser.
 * @param scanrsp SCAN_RSPs transmitted in reply.
 * @param other   other CRC-good PDUs seen in the post-ADV window.
 */
void tiku_flpr_arch_adv_counts(uint32_t *tx, uint32_t *scanreq,
                               uint32_t *scanrsp, uint32_t *other);

/** @brief TIMER10 ticks (2 MHz) from the end of the last answered SCAN_REQ
 *         to the ADDRESS event of the SCAN_RSP. */
uint32_t tiku_flpr_arch_adv_tifs(void);

/** @brief TIMER10 ticks from a SCAN_REQ's end to the reply's TXEN, handed
 *         to the controller at the next advertise; 0 = its own figure. */
extern uint32_t tiku_flpr_arch_adv_txen_ticks;

/** @brief Raw conn_state: 0 advertising, 1 connected, 2 gave up, 3 ended. */
uint32_t tiku_flpr_arch_conn_state(void);

/** @brief Connection events the FLPR has serviced (rising = link alive). */
uint32_t tiku_flpr_arch_conn_events(void);

/**
 * @brief Peer and local address from the CONNECT_IND, for SMP f5/f6.
 * @param inita out: initiator (central) address A (6 B, little-endian); or
 *              NULL.
 * @param adva  out: advertiser (local) address B (6 B); or NULL.
 * @return address-type bitfield: bit0 InitA, bit1 AdvA (1 = random,
 *         0 = public).
 */
uint8_t tiku_flpr_arch_conn_addrs(uint8_t inita[6], uint8_t adva[6]);

/**
 * @brief Service an LL_ENC_REQ forwarded by the FLPR.
 *
 * When the FLPR has published a new LL_ENC_REQ (SKDm, IVm), generates SKDs
 * and IVs, derives SK = e(LTK, SKDm||SKDs) and IV = IVm||IVs (the FLPR has
 * no AES), publishes them, and releases the FLPR to send LL_ENC_RSP.
 *
 * @param ltk the pairing Long Term Key.
 * @return 1 on the call that services a request (SK now readable), else 0.
 */
int tiku_flpr_arch_enc_service(const uint8_t ltk[16]);

/** @brief Copy the session key (valid once enc_service() has returned 1). */
void tiku_flpr_arch_enc_sk(uint8_t sk[16]);

/** @brief Copy the session IV = IVm||IVs (valid after enc_service() == 1). */
void tiku_flpr_arch_enc_iv(uint8_t iv[8]);

/** @brief Effective max LL payload after LL_LENGTH_REQ: 0 before one, then
 *         min(peer MaxRxOctets, TIKU_FLPR_DLE_MAX_OCTETS), at least 27. */
uint32_t tiku_flpr_arch_dle_max(void);

/** @brief Current PHY (0 = 1M, 1 = 2M, 2 = Coded S8); @p at_evt gets
 *         conn_events at the switch. */
uint32_t tiku_flpr_arch_conn_phy(uint32_t *at_evt);

/** @brief PHY-switch telemetry: MODE read back at the switch, and ADDRESS
 *         and CRCOK events caught off the 1M PHY.  Any pointer may be NULL. */
void tiku_flpr_arch_conn_phy_diag(uint32_t *mode, uint32_t *addr,
                                  uint32_t *crcok);

/**
 * @brief LL updates the FLPR applied this connection.
 * @param chan_map  out: LL_CHANNEL_MAP_UPDATE_INDs followed to their Instant.
 * @param conn_upd  out: LL_CONNECTION_UPDATE_INDs followed to their Instant.
 * @return chan_map + conn_upd.
 */
uint32_t tiku_flpr_arch_conn_updates(uint32_t *chan_map, uint32_t *conn_upd);

/**
 * @brief End the FLPR's held link and make the RADIO secure again.
 *
 * @note Sends TIKU_FLPR_CMD_CONN_STOP only while conn_state is 1; an FLPR
 *       still advertising (conn_state 0) is not stopped, and only the radio
 *       is made secure.
 */
void tiku_flpr_arch_conn_stop(void);

/**
 * @brief Anchored-RX telemetry: RADIO-off and RX-wait loop iterations of
 *        the closed-loop idle (see tiku_flpr_main.c).
 * @param gap_off_it  out: RADIO-off loop iterations per interval (0 while
 *                    not anchored).
 * @param rxon_it     out: measured RX-wait loop iterations.
 * @return RX-on duty as a percentage of the interval (off+on), or 100 while
 *         continuous.  Both values are FLPR loop iterations, a ratio and
 *         not a wall-clock time.
 */
uint32_t tiku_flpr_arch_conn_anchor(uint32_t *gap_off_it, uint32_t *rxon_it);

/**
 * @brief Start advertising and holding a link on the FLPR without blocking.
 * @return 0 handed over, -1 not running / bad args.  Poll conn_active().
 */
int tiku_flpr_arch_conn_start(const uint8_t *adv, uint32_t adv_len,
                              const uint8_t *rsp, uint32_t rsp_len,
                              const uint8_t *addr);

/** @brief Always 0: the controller never sets conn_sub; tiku_ble_host
 *         tracks the NUS CCCD. */
int tiku_flpr_arch_conn_subscribed(void);

/** @brief Is a received L2CAP fragment waiting? (peek, no consume). */
int tiku_flpr_arch_conn_rx_ready(void);

/**
 * @brief Pop the L2CAP fragment the controller forwarded; count (0 if none).
 * @param llid out (may be NULL): 2 = start of an L2CAP PDU, 1 = continuation.
 */
int tiku_flpr_arch_conn_recv(uint8_t *buf, uint32_t cap, uint8_t *llid);

/**
 * @brief Hand one L2CAP fragment to the controller for TX; count, or -2 if
 *        the previous fragment is still unconsumed (retry).
 * @param llid 2 = start of an L2CAP PDU, 1 = continuation.
 */
int tiku_flpr_arch_conn_send(const uint8_t *buf, uint32_t len, uint8_t llid);

#endif /* TIKU_NORDIC_FLPR_ARCH_H_ */
