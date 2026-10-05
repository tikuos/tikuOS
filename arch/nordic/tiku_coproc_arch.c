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

/* TIKU_COPROC_MSG_CAP comes from the Makefile (-D); it must equal the
 * mailbox it describes. */
_Static_assert(TIKU_COPROC_MSG_CAP == TIKU_FLPR_MSG_CAP,
               "coproc: the published cap must match the mailbox");

/** @brief Reply marker seen by the previous poll, for the "new" report. */
static uint32_t coproc_seen_seq;

uint32_t tiku_coproc_flags(void)
{
    /* ONESHOT: the VPR cannot return to power gating, and re-setting CPURUN
     * resumes at the current PC.  OWN_IMAGE: the payload is a separate
     * RISC-V link that the launch copies into the SRAM carve. */
    return TIKU_COPROC_F_ONESHOT | TIKU_COPROC_F_OWN_IMAGE;
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    uint32_t m = tiku_flpr_arch_magic();

    if (m == 0u) {
        return TIKU_COPROC_STOPPED;         /* never launched this power-on */
    }
    /* A faulted payload parks in its trap handler with MAGIC_FAULT in the
     * magic word. */
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
        /* Before the first launch the refusal is an absent or oversized
         * image (ERR_IMAGE); after it, a faulted core that did not come
         * back (ERR_STATE). */
        return (tiku_flpr_arch_magic() == 0u) ? TIKU_COPROC_ERR_IMAGE
                                              : TIKU_COPROC_ERR_STATE;
    }
    coproc_seen_seq = tiku_flpr_arch_reply_seq();
    return TIKU_COPROC_OK;
}

int tiku_coproc_stop(void)
{
    tiku_flpr_arch_stop();

    /* The contract infers the park from heartbeat stasis: a core that is
     * still alive after the park request is reported as ERR_TIMEOUT.
     * tiku_flpr_arch_alive() tests only the magic word, which a parked
     * payload keeps, so a successful park also returns ERR_TIMEOUT here. */
    return tiku_flpr_arch_alive() ? TIKU_COPROC_ERR_TIMEOUT : TIKU_COPROC_OK;
}

int tiku_coproc_alive(void)
{
    /* The contract asks for a published magic and an advancing heartbeat;
     * this tests the magic only, so a parked or wedged payload reads 1. */
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

    /* tiku_flpr_arch_poll() captures a new reply as the doorbell ISR would;
     * the reply sequence decides whether there is a new one. */
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
