/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_arch.c - the P4 LP core behind interfaces/coproc.
 *
 * Adapts the LP core lifecycle to the portable contract; the register work
 * and the shared-page protocol stay in tiku_lpcore_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <interfaces/coproc/tiku_coproc.h>
#include "tiku_lpcore_arch.h"
#include "lpcore/tiku_lpcore_ipc.h"

/* TIKU_COPROC_MSG_CAP comes from the Makefile (-D); it must equal the
 * mailbox it describes. */
_Static_assert(TIKU_COPROC_MSG_CAP == TIKU_LPCORE_MSG_CAP,
               "coproc: the published cap must match the mailbox");

/** @brief Reply marker seen by the previous poll, for the "new" report. */
static uint32_t coproc_seen_seq;

/** @brief Heartbeat reads before a payload counts as not executing; the
 *         payload advances it every few thousand LP core cycles. */
#define COPROC_ALIVE_READS 2000000u

uint32_t tiku_coproc_flags(void)
{
    /* OWN_IMAGE: the payload is a separate link the launch copies into LP
     * SRAM.  Not ONESHOT: a relaunch resets the core and runs it again. */
    return TIKU_COPROC_F_OWN_IMAGE;
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    uint32_t m;

    if (!tiku_p4_lpcore_launched()) {
        return TIKU_COPROC_STOPPED;
    }
    m = tiku_p4_lpcore_magic();
    if (m == TIKU_LPCORE_MAGIC_FAULT) {
        return TIKU_COPROC_FAULTED;
    }
    if (m != TIKU_LPCORE_MAGIC) {
        return TIKU_COPROC_STARTED;
    }
    return tiku_p4_lpcore_running() ? TIKU_COPROC_RUNNING
                                    : TIKU_COPROC_STOPPED;
}

int tiku_coproc_start(void)
{
    if (tiku_p4_lpcore_start() != 0) {
        return tiku_p4_lpcore_image_size() == 0u ||
               tiku_p4_lpcore_image_size() > TIKU_LPCORE_IMAGE_MAX
                   ? TIKU_COPROC_ERR_IMAGE : TIKU_COPROC_ERR_STATE;
    }
    coproc_seen_seq = tiku_p4_lpcore_reply_seq();
    return TIKU_COPROC_OK;
}

int tiku_coproc_stop(void)
{
    return tiku_p4_lpcore_stop() == 0 ? TIKU_COPROC_OK
                                      : TIKU_COPROC_ERR_TIMEOUT;
}

int tiku_coproc_alive(void)
{
    uint32_t first, spin;

    if (!tiku_p4_lpcore_running()) {
        return 0;
    }
    first = tiku_p4_lpcore_heartbeat();
    for (spin = 0u; spin < COPROC_ALIVE_READS; spin++) {
        if (tiku_p4_lpcore_heartbeat() != first) {
            return tiku_p4_lpcore_running();
        }
    }
    return 0;
}

uint32_t tiku_coproc_heartbeat(void)
{
    return tiku_p4_lpcore_heartbeat();
}

uint32_t tiku_coproc_image_size(void)
{
    return tiku_p4_lpcore_image_size();
}

int tiku_coproc_send(const void *data, uint32_t len)
{
    if (len == 0u || len > TIKU_COPROC_MSG_CAP) {
        return TIKU_COPROC_ERR_LEN;
    }
    if (tiku_coproc_state() != TIKU_COPROC_RUNNING) {
        return TIKU_COPROC_ERR_STATE;
    }
    return tiku_p4_lpcore_send(data, len) == 0 ? TIKU_COPROC_OK
                                               : TIKU_COPROC_ERR_STATE;
}

int tiku_coproc_poll(void)
{
    uint32_t seq;

    tiku_p4_lpcore_poll();
    seq = tiku_p4_lpcore_reply_seq();
    if (seq == coproc_seen_seq) {
        return 0;
    }
    coproc_seen_seq = seq;
    return 1;
}

uint32_t tiku_coproc_reply_seq(void)
{
    return tiku_p4_lpcore_reply_seq();
}

uint32_t tiku_coproc_reply(void *out, uint32_t cap)
{
    return tiku_p4_lpcore_reply(out, cap);
}
