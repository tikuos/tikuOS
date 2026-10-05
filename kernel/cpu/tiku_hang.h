/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hang.h - check-in watchdog: live-hang detection with named attribution.
 *
 * A tick that sees the heartbeat stall while one process holds the CPU records
 * that process and resets; autostart skips it on the next boot.  Only the
 * nRF54L and Apollo4l tick ISRs call tiku_hang_tick().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_HANG_H_
#define TIKU_HANG_H_

#include <stdint.h>

struct tiku_process;

/**
 * @brief Consecutive stalled ticks before a non-yielding process is hung.
 *
 * It must exceed the longest slice that holds the CPU without a check-in,
 * such as an inline RSA certificate-chain verify.  The default, 1024 ticks, is
 * 8 s at the default 128 Hz tick; a build may override it.
 *
 * @note An unbounded wait (a REPL prompt, DELAY, INPUT) checks in through
 *       tiku_hang_checkin() or tiku_watchdog_kick(); no threshold covers it.
 */
#ifndef TIKU_HANG_THRESHOLD_TICKS
#define TIKU_HANG_THRESHOLD_TICKS  1024u
#endif

/*---------------------------------------------------------------------------*/
/* DETECTION (run-time)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Arm the detector.
 *
 * Until armed, tiku_hang_tick() returns at once.  tiku_sched_loop() is the
 * only caller, so code that runs the kernel without that loop never trips it.
 *
 * @note Call once, when the scheduler loop starts.
 */
void tiku_hang_arm(void);

/**
 * @brief Liveness check-in: bump the heartbeat the hang detector watches.
 *
 * The scheduler checks in once per dispatched event; tiku_watchdog_kick() and
 * long driver loops check in too.  An operation that keeps checking in is not
 * reset, however long it runs.
 */
void tiku_hang_checkin(void);

/**
 * @brief Per-tick detector.
 *
 * Does nothing until tiku_hang_arm().  After TIKU_HANG_THRESHOLD_TICKS ticks
 * with no check-in and a process on the CPU, it records that process and
 * calls tiku_hang_arch_reset(), which does not return.
 *
 * @note System-tick ISR context.
 */
void tiku_hang_tick(void);

/**
 * @brief One detection step, without the reset: the culprit pid once the
 *        stall has lasted TIKU_HANG_THRESHOLD_TICKS, else -1.  Exposed for
 *        testing.
 */
int8_t tiku_hang_detect_step(void);

/**
 * @brief Record @p p as the hang culprit (internal; public for tests).
 *
 * A NULL @p p records pid -1 and the name "?".
 */
void tiku_hang_record(const struct tiku_process *p);

/*---------------------------------------------------------------------------*/
/* RECOVERY (boot-time)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Capture the pre-reset culprit for this boot, then clear the
 *        cross-reset record.
 *
 * The record is one-shot: it is read into this boot's view and wiped, so one
 * hang quarantines the culprit for the recovery boot only.
 *
 * @note Call once, early in boot, before autostart.
 */
void tiku_hang_boot_init(void);

/** @brief Culprit pid recorded before the last reset, or -1 if none. */
int8_t tiku_hang_last_pid(void);

/** @brief Culprit name recorded before the last reset, or "" if none. */
const char *tiku_hang_last_name(void);

/** @brief Non-zero if @p p is this boot's hang culprit (match by name). */
uint8_t tiku_hang_is_culprit(const struct tiku_process *p);

/** @brief Forget this boot's culprit and clear the warm-reset record. */
void tiku_hang_clear(void);

/**
 * @brief Reset the chip.
 *
 * The nRF54L and Apollo4l ports define it.  The weak default spins forever,
 * which a running hardware watchdog then ends with a reset.
 */
void tiku_hang_arch_reset(void);

#endif /* TIKU_HANG_H_ */
