/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hang.c - check-in watchdog: live-hang detection + named-culprit
 *               attribution across a warm reset.  See tiku_hang.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_hang.h"
#include <kernel/process/tiku_process.h>
#include <kernel/memory/tiku_mem.h>       /* TIKU_PERSIST_WARM */
#include <hal/tiku_compiler.h>            /* TIKU_WEAK */

/* Set by call_process() to the thread currently on the CPU, cleared to NULL
 * between dispatches -- so a non-NULL value in the tick ISR is the process
 * that was running (and, if stalled, the culprit). */
extern struct tiku_process *tiku_current_process;

#define TIKU_HANG_MAGIC    0x484E4721u   /* "HNG!" */
#define TIKU_HANG_NAMELEN  16u

struct tiku_hang_rec {
    uint32_t magic;
    int8_t   pid;
    char     name[TIKU_HANG_NAMELEN];
};

/*
 * Cross-reset record.  .persistent.warm survives a warm reset (the reset a
 * hang triggers) and, per tiku_mem.h, sits OUTSIDE the NVM mirror + MPU
 * windows -- so the detector can write it from the tick ISR as a plain,
 * unlocked SRAM store, no NVM program, no MPU unlock.
 */
static TIKU_PERSIST_WARM struct tiku_hang_rec tiku_hang_warm;

/* This boot's view, captured by tiku_hang_boot_init() (plain .bss, zeroed at
 * boot).  Reads go here so the record stays visible all boot even after the
 * warm copy is wiped for one-shot semantics. */
static struct tiku_hang_rec tiku_hang_boot;

/* Detector state (per boot). */
static volatile uint8_t  tiku_hang_armed;   /* tick ISR acts only once armed */
static volatile uint32_t tiku_hang_hb;      /* heartbeat, bumped by checkin  */
static uint32_t          tiku_hang_seen;    /* last heartbeat the tick saw   */
static uint16_t          tiku_hang_stall;   /* consecutive stalled ticks     */

/*---------------------------------------------------------------------------*/
/* DETECTION                                                                 */
/*---------------------------------------------------------------------------*/

void tiku_hang_arm(void)
{
    tiku_hang_armed = 1u;
}

void tiku_hang_checkin(void)
{
    tiku_hang_hb++;
}

int8_t tiku_hang_detect_step(void)
{
    const struct tiku_process *cur = tiku_current_process;

    /* Progress (heartbeat moved) OR nobody on the CPU (idle) -> not a hang.
     * The idle case is why a sleeping system is never flagged: between
     * dispatches tiku_current_process is NULL. */
    if (cur == NULL || tiku_hang_hb != tiku_hang_seen) {
        tiku_hang_seen  = tiku_hang_hb;
        tiku_hang_stall = 0;
        return (int8_t)-1;
    }

    /* One process has held the CPU while the loop made no progress. */
    if (++tiku_hang_stall >= (uint16_t)TIKU_HANG_THRESHOLD_TICKS) {
        tiku_hang_stall = 0;
        return cur->pid;
    }
    return (int8_t)-1;
}

void tiku_hang_record(const struct tiku_process *p)
{
    const char *n = (p != NULL && p->name != NULL) ? p->name : "?";
    uint8_t i;
    /* .persistent.warm is warm-surviving SRAM on Cortex-M but MPU-write-
     * protected FRAM on MSP430, so open the NVM window before storing the
     * culprit -- otherwise the MPU silently drops the write and the recovery
     * boot finds no record. */
    uint16_t mpu_state = tiku_mpu_unlock_nvm();

    tiku_hang_warm.pid = (p != NULL) ? p->pid : (int8_t)-1;
    for (i = 0u; i < (TIKU_HANG_NAMELEN - 1u) && n[i] != '\0'; i++) {
        tiku_hang_warm.name[i] = n[i];
    }
    tiku_hang_warm.name[i] = '\0';
    tiku_hang_warm.magic = TIKU_HANG_MAGIC;   /* validate LAST (data first) */

    tiku_mpu_lock_nvm(mpu_state);
}

void tiku_hang_tick(void)
{
    int8_t pid;

    /* Dormant until the scheduler loop arms it (tiku_hang_arm).  A test
     * harness that drives the kernel WITHOUT the sched loop never arms it, so
     * the live detector cannot fire mid-test -- and the pure predicate
     * tiku_hang_detect_step() stays independently testable. */
    if (!tiku_hang_armed) {
        return;
    }
    pid = tiku_hang_detect_step();
    if (pid >= 0) {
        tiku_hang_record(tiku_current_process);
        tiku_hang_arch_reset();               /* does not return */
    }
}

/*---------------------------------------------------------------------------*/
/* RECOVERY                                                                  */
/*---------------------------------------------------------------------------*/

void tiku_hang_boot_init(void)
{
    if (tiku_hang_warm.magic == TIKU_HANG_MAGIC) {
        tiku_hang_boot = tiku_hang_warm;      /* capture for this boot */
    } else {
        tiku_hang_boot.magic = 0u;
    }
    tiku_hang_warm.magic = 0u;                /* one-shot: next boot is clean */
}

/**
 * @brief Whether a valid hang record survived into this boot.
 *
 * @return Non-zero if the warm-boot record captured at boot carries the
 *         expected magic (the previous boot recorded a hang culprit).
 */
static uint8_t tiku_hang_have(void)
{
    return (uint8_t)(tiku_hang_boot.magic == TIKU_HANG_MAGIC);
}

int8_t tiku_hang_last_pid(void)
{
    return tiku_hang_have() ? tiku_hang_boot.pid : (int8_t)-1;
}

const char *tiku_hang_last_name(void)
{
    return tiku_hang_have() ? tiku_hang_boot.name : "";
}

void tiku_hang_clear(void)
{
    tiku_hang_boot.magic = 0u;
    tiku_hang_warm.magic = 0u;
}

uint8_t tiku_hang_is_culprit(const struct tiku_process *p)
{
    uint8_t i;

    if (p == NULL || p->name == NULL || !tiku_hang_have() ||
        tiku_hang_boot.name[0] == '\0') {
        return 0u;
    }
    /* Match by NAME -- pids are reassigned across a reboot, names are the
     * stable identity.  Bounded to the (possibly truncated) recorded name. */
    for (i = 0u; i < (TIKU_HANG_NAMELEN - 1u); i++) {
        if (p->name[i] != tiku_hang_boot.name[i]) {
            return 0u;
        }
        if (p->name[i] == '\0') {
            break;
        }
    }
    return 1u;
}

/*---------------------------------------------------------------------------*/
/* ARCH RESET (weak default)                                                 */
/*---------------------------------------------------------------------------*/

TIKU_WEAK void tiku_hang_arch_reset(void)
{
    /* No arch reset wired: spin so the failure is contained (a real hardware
     * watchdog, where present, still catches it) rather than continuing. */
    for (;;) { }
}
