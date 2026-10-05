/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_arch.c - the RA8P1 Cortex-M33 behind interfaces/coproc.
 *
 * Adapts the CPU1 lifecycle to the portable contract; the register work and
 * the shared-page discipline stay in tiku_cpu1_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/coproc/tiku_coproc.h>

#include "tiku_cpu1_arch.h"
#include "cpu1/tiku_cpu1_ipc.h"

/* TIKU_COPROC_MSG_CAP comes from a -D flag; this fails the build when it
 * differs from the mailbox. */
_Static_assert(TIKU_COPROC_MSG_CAP == TIKU_CPU1_MSG_CAP,
               "coproc: the published cap must match the mailbox");

/** @brief Reply marker seen by the previous poll, for the "new" report. */
static uint32_t coproc_seen_seq;

uint32_t tiku_coproc_flags(void)
{
    /* ONESHOT: ACTREQ acts only while ACT is 0, and nothing returns CPU1 to
     * power gating.  OWN_IMAGE: the payload is a separate image copied in at
     * start. */
    return TIKU_COPROC_F_ONESHOT | TIKU_COPROC_F_OWN_IMAGE;
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    uint32_t m;

    if (!tiku_ra8p1_cpu1_active()) {
        return TIKU_COPROC_STOPPED;
    }
    /* Read the magic first: a faulted payload keeps the running flag set
     * until tiku_ra8p1_cpu1_magic() sees the fault. */
    m = tiku_ra8p1_cpu1_magic();
    if (m == TIKU_CPU1_MAGIC_FAULT || m == TIKU_CPU1_MAGIC_HANG) {
        /* A hang caught by WDT1 is reported as a fault: it needs a restart. */
        return TIKU_COPROC_FAULTED;
    }
    if (!tiku_ra8p1_cpu1_running()) {
        return TIKU_COPROC_STOPPED;         /* powered, payload parked */
    }
    return (m == TIKU_CPU1_MAGIC) ? TIKU_COPROC_RUNNING
                                  : TIKU_COPROC_STARTED;
}

int tiku_coproc_start(void)
{
    int rc = tiku_ra8p1_cpu1_start();

    if (rc == TIKU_RA8P1_CPU1_ERR_IMG) {
        return TIKU_COPROC_ERR_IMAGE;
    }
    if (rc != TIKU_RA8P1_CPU1_OK) {
        /* ERR_ACT and ERR_DEAD (a locked-up core) both map here. */
        return TIKU_COPROC_ERR_STATE;
    }
    coproc_seen_seq = tiku_ra8p1_cpu1_reply_seq();
    return TIKU_COPROC_OK;
}

int tiku_coproc_stop(void)
{
    tiku_ra8p1_cpu1_stop();

    /* tiku_ra8p1_cpu1_stop() infers the park from a heartbeat that stops; a
     * heartbeat still moving afterwards is reported as
     * TIKU_COPROC_ERR_TIMEOUT. */
    return tiku_ra8p1_cpu1_alive() ? TIKU_COPROC_ERR_TIMEOUT : TIKU_COPROC_OK;
}

int tiku_coproc_alive(void)
{
    return tiku_ra8p1_cpu1_alive();
}

uint32_t tiku_coproc_heartbeat(void)
{
    return tiku_ra8p1_cpu1_heartbeat();
}

uint32_t tiku_coproc_image_size(void)
{
    return tiku_ra8p1_cpu1_image_size();
}

int tiku_coproc_send(const void *data, uint32_t len)
{
    int rc = tiku_ra8p1_cpu1_send(data, len);

    if (rc == TIKU_RA8P1_CPU1_ERR_LEN) {
        return TIKU_COPROC_ERR_LEN;
    }
    return (rc == TIKU_RA8P1_CPU1_OK) ? TIKU_COPROC_OK : TIKU_COPROC_ERR_STATE;
}

int tiku_coproc_poll(void)
{
    uint32_t seq;

    /* The cache maintenance of a sequence read runs only after a doorbell.
     * The sequence decides: doorbells coalesce, and one can arrive for a reply
     * already collected. */
    if (!tiku_ra8p1_cpu1_bell_take()) {
        return 0;
    }
    seq = tiku_ra8p1_cpu1_reply_seq();
    if (seq == coproc_seen_seq) {
        return 0;
    }
    coproc_seen_seq = seq;
    return 1;
}

uint32_t tiku_coproc_reply_seq(void)
{
    return tiku_ra8p1_cpu1_reply_seq();
}

uint32_t tiku_coproc_reply(void *out, uint32_t cap)
{
    return tiku_ra8p1_cpu1_reply(out, cap);
}
