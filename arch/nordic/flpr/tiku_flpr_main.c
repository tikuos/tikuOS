/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_main.c - FLPR coprocessor firmware (F1: liveness heartbeat).
 *
 * Runs on the nRF54L15's VPR RISC-V core out of the SRAM carve.  F1 scope
 * is deliberately tiny: stamp the magic (proves the crt reached C), then
 * bump the heartbeat forever (proves steady-state life, visible to the
 * app core through /sys/flpr/heartbeat).  The pacing loop keeps the bump
 * rate in the ~kHz class so the counter is obviously moving yet reads
 * stay meaningful across a slow console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/flpr/tiku_flpr_ipc.h>

void tiku_flpr_main(void)
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    volatile uint32_t pace;

    sh->heartbeat = 0u;
    sh->magic = TIKU_FLPR_MAGIC;

    for (;;) {
        /* Cooperative park/resume (see tiku_flpr_ipc.h): heartbeat freezes
         * while parked; RESUME returns to this loop.  The parked wait is a
         * paced busy-poll for now -- a WFI here could never wake, since no
         * interrupt source is wired to the VPR yet (the F2 doorbell will
         * turn parking into genuine sleep). */
        if (sh->cmd == TIKU_FLPR_CMD_PARK) {
            sh->rsp = TIKU_FLPR_RSP_PARKED;
            while (sh->cmd != TIKU_FLPR_CMD_RESUME) {
                for (pace = 0u; pace < 8000u; pace++) {
                }
            }
            sh->cmd = 0u;
            sh->rsp = 0u;
        }
        sh->heartbeat = sh->heartbeat + 1u;
        for (pace = 0u; pace < 8000u; pace++) {
        }
    }
}
