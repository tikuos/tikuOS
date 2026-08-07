/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_payload.c - heartbeat and echo mailbox for the RA8P1 Cortex-M33.
 *
 * Built standalone for cortex-m33 and embedded in the M85 image as bytes.
 * Every address is derived from this core's own PC, so the image runs
 * wherever the loader puts it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_cpu1_ipc.h"

/** @brief INITVTOR discards bits [6:0], so the image base is 128-aligned. */
#define CPU1_BASE_MASK      0x7FUL

/*
 * Sixteen zero words.  The loader patches the first two; the rest are the
 * fault vectors, and zero there escalates any fault to LOCKUP, which the CPU
 * control block raises on the M85 as an NMI.  Real handlers here would make
 * a wedged coprocessor silent instead.
 */
__attribute__((section(".cpu1_vectors"), used))
const uint32_t cpu1_vectors[16] = { 0 };

/**
 * @brief Answer one message: echo the bytes back.
 *
 * @param sh  The shared page
 * @note Replies with the sequence it is answering, so the M85 can match a
 *       reply to its own send rather than to whatever arrived last.
 */
static void cpu1_serve(volatile tiku_cpu1_shared_t *sh, uint32_t seq)
{
    uint32_t len = sh->a2c_len;
    uint32_t i;

    if (len > TIKU_CPU1_MSG_CAP) {
        len = TIKU_CPU1_MSG_CAP;
    }
    for (i = 0U; i < len; i++) {
        sh->c2a_buf[i] = sh->a2c_buf[i];
    }
    sh->c2a_len = len;

    /* Sequence last, and behind a barrier: the M85 takes a matching seq as
     * proof the buffer beside it is already complete. */
    __asm__ volatile ("dmb" ::: "memory");
    sh->c2a_seq = seq;
}

/**
 * @brief Publish the magic word, then serve the mailbox until asked to halt.
 *
 * @note Never returns; the vector table's stack pointer covers the fault
 *       path and this function's own frame.
 */
__attribute__((section(".cpu1_reset"), used, noreturn))
void cpu1_reset(void)
{
    volatile tiku_cpu1_shared_t *sh;
    uint32_t pc;
    uint32_t base;
    uint32_t beats = 0U;
    uint32_t served = 0U;

    /*
     * The base comes from the PC because no link-time address survives the
     * loader copying this image wherever it has room.  Exact while the entry
     * stays inside the first 128 bytes, which the .ld asserts.
     */
    __asm__ volatile ("mov %0, pc" : "=r" (pc));
    base = pc & ~CPU1_BASE_MASK;
    sh = (volatile tiku_cpu1_shared_t *)(base + TIKU_CPU1_SHARED_OFF);

    sh->magic = TIKU_CPU1_MAGIC;

    /* The magic must land before the first heartbeat.  Normal memory is
     * weakly ordered to the other core, and a moving counter with the magic
     * still zero reads exactly like a launch that failed. */
    __asm__ volatile ("dmb" ::: "memory");

    for (;;) {
        uint32_t seq;

        /* Re-read every pass: this is the whole halt protocol.  The counters
         * deliberately survive it, so a resumed payload continues rather
         * than restarts. */
        while (sh->halt != 0U) {
        }

        seq = sh->a2c_seq;
        if (seq != served) {
            cpu1_serve(sh, seq);
            served = seq;
        }

        beats++;
        sh->heartbeat = beats;
    }
}
