/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_arch.c - the nRF54L FLPR behind interfaces/coproc.
 *
 * Adapts the VPR lifecycle to the portable contract; the register work and
 * the shared-page discipline stay in tiku_flpr_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/coproc/tiku_coproc.h>

#include "tiku_flpr_arch.h"
#include "flpr/tiku_flpr_ipc.h"

/* The published capacity is a -D, so this is what stops it drifting from the
 * mailbox it describes. */
_Static_assert(TIKU_COPROC_MSG_CAP == TIKU_FLPR_MSG_CAP,
               "coproc: the published cap must match the mailbox");

/** @brief Reply marker seen by the previous poll, for the "new" report. */
static uint32_t coproc_seen_seq;

uint32_t tiku_coproc_flags(void)
{
    /* Both are hardware facts: the VPR cannot be returned to power gating
     * and CPURUN re-set resumes at the current PC, and the payload is its
     * own RISC-V link the launch copies into the SRAM carve. */
    return TIKU_COPROC_F_ONESHOT | TIKU_COPROC_F_OWN_IMAGE;
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    uint32_t m = tiku_flpr_arch_magic();

    if (m == 0u) {
        return TIKU_COPROC_STOPPED;         /* never launched this power-on */
    }
    /* The magic first: a faulted payload is parked in its trap handler, and
     * this read is what notices it. */
    if (m == TIKU_FLPR_MAGIC_FAULT) {
        return TIKU_COPROC_FAULTED;
    }
    if (m != TIKU_FLPR_MAGIC) {
        return TIKU_COPROC_STARTED;         /* released, magic not out yet  */
    }
    return tiku_flpr_arch_running() ? TIKU_COPROC_RUNNING
                                    : TIKU_COPROC_STOPPED;  /* parked */
}

int tiku_coproc_start(void)
{
    if (tiku_flpr_arch_start() != 0) {
        /* One code for both refusals: an absent or oversize image before
         * the first launch, a core that never came back after it. */
        return (tiku_flpr_arch_magic() == 0u) ? TIKU_COPROC_ERR_IMAGE
                                              : TIKU_COPROC_ERR_STATE;
    }
    coproc_seen_seq = tiku_flpr_arch_reply_seq();
    return TIKU_COPROC_OK;
}

int tiku_coproc_stop(void)
{
    tiku_flpr_arch_stop();

    /* The park is inferred from heartbeat stasis, so a core that never
     * settled is the timeout this contract promises to report. */
    return tiku_flpr_arch_alive() ? TIKU_COPROC_ERR_TIMEOUT : TIKU_COPROC_OK;
}

int tiku_coproc_alive(void)
{
    return tiku_flpr_arch_alive();
}

uint32_t tiku_coproc_heartbeat(void)
{
    return tiku_flpr_arch_heartbeat();
}

uint32_t tiku_coproc_image_size(void)
{
    return tiku_flpr_arch_image_size();
}

int tiku_coproc_send(const void *data, uint32_t len)
{
    if (len == 0u || len > TIKU_COPROC_MSG_CAP) {
        return TIKU_COPROC_ERR_LEN;
    }
    if (tiku_coproc_state() != TIKU_COPROC_RUNNING) {
        return TIKU_COPROC_ERR_STATE;
    }
    return (tiku_flpr_arch_send(data, len) == 0) ? TIKU_COPROC_OK
                                                 : TIKU_COPROC_ERR_STATE;
}

int tiku_coproc_poll(void)
{
    uint32_t seq;

    /* No doorbell-take on this backend: arch_poll() drains the VEVIF
     * pending bit and refreshes the shared view, and the sequence is the
     * authority for "new" -- the same rule the RA8P1 backend applies after
     * its bell. */
    tiku_flpr_arch_poll();
    seq = tiku_flpr_arch_reply_seq();
    if (seq == coproc_seen_seq) {
        return 0;
    }
    coproc_seen_seq = seq;
    return 1;
}

uint32_t tiku_coproc_reply_seq(void)
{
    return tiku_flpr_arch_reply_seq();
}

uint32_t tiku_coproc_reply(void *out, uint32_t cap)
{
    return tiku_flpr_arch_reply(out, cap);
}
