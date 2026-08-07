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

/* The published capacity is a -D, so this is what stops it drifting from the
 * mailbox it describes. */
_Static_assert(TIKU_COPROC_MSG_CAP == TIKU_CPU1_MSG_CAP,
               "coproc: the published cap must match the mailbox");

/** @brief Reply marker seen by the previous poll, for the "new" report. */
static uint32_t coproc_seen_seq;

uint32_t tiku_coproc_flags(void)
{
    /* Both are hardware facts: ACTREQ acts only while ACT is 0 and nothing
     * returns CPU1 to power gating, and the payload is its own link. */
    return TIKU_COPROC_F_ONESHOT | TIKU_COPROC_F_OWN_IMAGE;
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    if (!tiku_ra8p1_cpu1_active()) {
        return TIKU_COPROC_STOPPED;
    }
    if (!tiku_ra8p1_cpu1_running()) {
        return TIKU_COPROC_STOPPED;         /* powered, payload parked */
    }
    return (tiku_ra8p1_cpu1_magic() == TIKU_CPU1_MAGIC)
               ? TIKU_COPROC_RUNNING
               : TIKU_COPROC_STARTED;
}

int tiku_coproc_start(void)
{
    int rc = tiku_ra8p1_cpu1_start();

    if (rc == TIKU_RA8P1_CPU1_ERR_IMG) {
        return TIKU_COPROC_ERR_IMAGE;
    }
    if (rc != TIKU_RA8P1_CPU1_OK) {
        return TIKU_COPROC_ERR_STATE;
    }
    coproc_seen_seq = tiku_ra8p1_cpu1_reply_seq();
    return TIKU_COPROC_OK;
}

int tiku_coproc_stop(void)
{
    tiku_ra8p1_cpu1_stop();

    /* The park is inferred from heartbeat stasis, so a core that never
     * settled is the timeout this contract promises to report. */
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
    uint32_t seq = tiku_ra8p1_cpu1_reply_seq();

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
