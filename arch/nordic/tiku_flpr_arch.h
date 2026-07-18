/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_arch.h - nRF54L15 FLPR (VPR RISC-V) coprocessor control.
 *
 * App-core side of the coprocessor: load the embedded FLPR image into the
 * SRAM carve, start/stop the core (VPR00 INITPC + CPURUN), and read the
 * liveness state the firmware publishes through the shared page
 * (arch/nordic/flpr/tiku_flpr_ipc.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_FLPR_ARCH_H_
#define TIKU_NORDIC_FLPR_ARCH_H_

#include <stdint.h>

/**
 * @brief Copy the embedded FLPR image into the carve and start the core.
 *
 * Idempotent: restarting reloads the image (fresh .data/.bss world) and
 * re-arms INITPC before CPURUN.
 *
 * @return 0 on success, negative if the embedded image is missing/oversized.
 */
int tiku_flpr_arch_start(void);

/** @brief Stop the coprocessor (CPURUN=Stopped). Idempotent. */
void tiku_flpr_arch_stop(void);

/** @brief 1 when CPURUN reads Running. */
int tiku_flpr_arch_running(void);

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

/** @brief Pull any pending flpr->app message (doorbell fallback). */
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
 * @brief Offload duty-cycled BLE beaconing to the coprocessor.
 *
 * Caller contract: radio link-config registers already programmed
 * (tiku_radio_arch_init) and the session CONSTLAT hold taken.  While
 * offloaded, the M33 must not touch RADIO/UARTE21 (they are flipped to
 * NonSecure for the FLPR).
 *
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
 * @brief RX probe (L6 F-L6.1 step 0): prove the FLPR can drive RADIO RX.
 *
 * Same handoff as the beacon (caller programmed link config via
 * tiku_radio_arch_init and holds CONSTLAT; RADIO+UARTE21 flipped NonSecure
 * here).  Blocks ~4-5 s while the coprocessor listens on adv channel 37,
 * then reports what it heard.  Restores peripheral security on return.
 *
 * @param addr_evts   Out: ADDRESS matches (AA matched, CRC unchecked).
 * @param crcok_evts  Out: CRC-valid packets received.
 * @param first       Out: head bytes of the first CRC-valid packet.
 * @param cap         Capacity of @p first.
 * @param flen        Out: bytes written to @p first.
 * @return 0 done, -1 not running, -2 firmware never finished.
 */
int tiku_flpr_arch_rxprobe(uint32_t *addr_evts, uint32_t *crcok_evts,
                           uint8_t *first, uint32_t cap, uint32_t *flen);

#endif /* TIKU_NORDIC_FLPR_ARCH_H_ */
