/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hang.h - check-in (software) watchdog: live-hang detection with
 *               named-culprit attribution across a reset.
 *
 * Process supervision (tiku_process) recovers a process that EXITS.  A
 * process that WEDGES the cooperative scheduler -- an infinite loop that
 * never yields -- is different: the run loop never turns, so the supervisor
 * can never run to save it, and the board goes down.  Today that reset is
 * anonymous.
 *
 * This turns it into a NAMED, quarantinable event.  The scheduler bumps a
 * progress heartbeat once per dispatched event; the system-tick ISR (which
 * preempts even a wedged thread) watches it.  If a process has held the CPU
 * for TIKU_HANG_THRESHOLD_TICKS without the loop making progress, the ISR
 * records WHICH process was on the CPU (tiku_current_process) into
 * warm-reset-surviving storage and resets.  On the recovery boot the culprit
 * is readable (/sys/boot/hang) and the autostart quarantines it once, so a
 * process that hangs on start cannot wedge the board forever.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_HANG_H_
#define TIKU_HANG_H_

#include <stdint.h>

struct tiku_process;

/*
 * Consecutive stalled ticks before a non-yielding process is declared hung.
 * Must EXCEED the longest legitimate non-yielding slice.  2 s (256 ticks)
 * proved too tight on device: an inline TLS certificate-chain verify (an
 * RSA chain, no worker offload) legitimately holds the CPU past 2 s between
 * its milestone kicks, and the web sweep's first fetch warm-reset mid-
 * handshake with the shell named in /sys/boot/hang.  8 s clears every
 * measured slice with margin while still catching a real wedge fast.
 * Unbounded waits (a REPL prompt, DELAY, INPUT) must still check in --
 * tiku_watchdog_kick() feeds the heartbeat -- no threshold covers those.
 * Override per build.
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
 * Call once when the scheduler loop starts.  Until armed, the per-tick
 * detector is a no-op -- so a test harness that drives the kernel without the
 * sched loop can never trip a false hang reset, while the pure predicate
 * tiku_hang_detect_step() remains independently testable.
 */
void tiku_hang_arm(void);

/** @brief Scheduler progress heartbeat -- call once per dispatched event. */
void tiku_hang_checkin(void);

/**
 * @brief Per-tick detector, called from the system-tick ISR.
 *
 * On a confirmed hang it records the culprit and resets the chip (never
 * returns in that case).  A no-op until the stall threshold is crossed.
 */
void tiku_hang_tick(void);

/**
 * @brief One detection step (NO reset): the culprit pid once the stall has
 *        lasted TIKU_HANG_THRESHOLD_TICKS, else -1.  Exposed for testing.
 */
int8_t tiku_hang_detect_step(void);

/** @brief Record @p p as the hang culprit (internal; public for tests). */
void tiku_hang_record(const struct tiku_process *p);

/*---------------------------------------------------------------------------*/
/* RECOVERY (boot-time)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Capture the pre-reset culprit for this boot, then clear the
 *        cross-reset record.
 *
 * Call once early in boot, before autostart.  The record is one-shot: it is
 * read into this boot's view and wiped, so a single hang quarantines the
 * culprit for exactly the recovery boot, not forever.
 */
void tiku_hang_boot_init(void);

/** @brief Culprit pid recorded before the last reset, or -1 if none. */
int8_t tiku_hang_last_pid(void);

/** @brief Culprit name recorded before the last reset, or "" if none. */
const char *tiku_hang_last_name(void);

/** @brief Non-zero if @p p is this boot's hang culprit (match by name). */
uint8_t tiku_hang_is_culprit(const struct tiku_process *p);

/** @brief Forget this boot's culprit (e.g. after the user acknowledges). */
void tiku_hang_clear(void);

/**
 * @brief Reset the chip (weak; an arch provides NVIC_SystemReset et al.).
 *
 * The portable default spins, so an un-wired arch at least contains the
 * failure rather than silently continuing.
 */
void tiku_hang_arch_reset(void);

#endif /* TIKU_HANG_H_ */
