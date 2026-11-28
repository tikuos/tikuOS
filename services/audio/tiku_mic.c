/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic.c - The capture process and its PSRAM ring.  Every call here and
 * every port call runs on the kernel thread, so the ring needs no lock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mic.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_timer.h>
#include <string.h>

/* A wake every 16 ms or less drains one or two blocks; the port holds
 * about 128 ms before it loses a block. */
#define POLL_TICKS ((TIKU_CLOCK_SECOND + 63u) / 64u)
#define RING_BYTES (TIKU_MIC_RING_SAMPLES * sizeof(int16_t))
#define ARENA_TAG 0x4du

static struct {
    tiku_arena_t arena;
    int16_t *ring;
    uint32_t head, tail;    /* samples written and read since the start */
    unsigned settle;        /* blocks still to drop after the start */
    tiku_mic_status_t st;
} mic = {.st = {.gain = TIKU_MIC_GAIN_DEFAULT}};

static int16_t block[TIKU_MIC_BLOCK];
static struct tiku_timer tick;

TIKU_PROCESS(tiku_mic_process, "microphone");

/** @brief Release the ring and its arena. */
static void release(void)
{
    if (mic.ring != NULL) (void)tiku_mem_workspace_close(&mic.arena);
    mic.ring = NULL;
    mic.head = mic.tail = 0;
}

/** @brief Copy @p n samples of the block into the ring at its head. */
static void put(unsigned n)
{
    uint32_t at = mic.head % TIKU_MIC_RING_SAMPLES;
    uint32_t first = TIKU_MIC_RING_SAMPLES - at;

    if (first > n) first = n;
    memcpy(&mic.ring[at], block, first * sizeof(int16_t));
    memcpy(mic.ring, &block[first], (n - first) * sizeof(int16_t));
    mic.head += n;
}

/** @brief Move every ready block into the ring, its level into the status;
 *  a port error stops capture and closes the port. */
static void drain(void)
{
    int rc;

    while ((rc = tiku_mic_arch_poll(block)) == 1) {
        uint32_t room = TIKU_MIC_RING_SAMPLES - (mic.head - mic.tail);
        unsigned n = room < TIKU_MIC_BLOCK ? (unsigned)room : TIKU_MIC_BLOCK;
        unsigned peak = 0;

        if (mic.settle) {
            mic.settle--;
            continue;
        }
        for (unsigned i = 0; i < TIKU_MIC_BLOCK; i++) {
            int v = block[i];
            unsigned m = (unsigned)(v < 0 ? -v : v);

            if (m > peak) peak = m;
        }
        mic.st.peak = (uint16_t)(peak > 32767u ? 32767u : peak);
        put(n);
        mic.st.captured += n;
        mic.st.dropped += TIKU_MIC_BLOCK - n;
    }
    if (rc < 0) {
        mic.st.faults++;
        mic.st.state = TIKU_MIC_FAILED;
        tiku_mic_arch_close();
    }
}

TIKU_PROCESS_THREAD(tiku_mic_process, ev, data)
{
    (void)data;
    TIKU_PROCESS_BEGIN();
    while (mic.st.state == TIKU_MIC_RUNNING) {
        tiku_timer_set_event(&tick, POLL_TICKS);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        drain();
    }
    TIKU_PROCESS_END();
}

int tiku_mic_start(void)
{
    uint16_t starts = mic.st.starts;
    uint8_t gain = mic.st.gain;

    if (!tiku_thread_in_kernel() || mic.st.state == TIKU_MIC_RUNNING)
        return TIKU_MIC_ERR_STATE;
    tiku_mic_stop();
    if (tiku_tier_arena_create_span(&mic.arena, TIKU_MEM_PSRAM, 0, RING_BYTES, 64,
                                    ARENA_TAG) != TIKU_MEM_OK)
        return TIKU_MIC_ERR_MEMORY;
    mic.ring = tiku_arena_alloc(&mic.arena, RING_BYTES);
    if (mic.ring == NULL) {
        (void)tiku_arena_destroy(&mic.arena);
        return TIKU_MIC_ERR_MEMORY;
    }
    if (tiku_mic_arch_open() != 0) {
        release();
        return TIKU_MIC_ERR_DEVICE;
    }
    if (tiku_mic_arch_gain(gain) != 0) {
        tiku_mic_arch_close();
        release();
        return TIKU_MIC_ERR_DEVICE;
    }
    memset(&mic.st, 0, sizeof mic.st);
    mic.st.gain = gain;
    mic.st.starts = (uint16_t)(starts + 1u);
    mic.st.state = TIKU_MIC_RUNNING;
    mic.settle = TIKU_MIC_SETTLE_BLOCKS;
    tiku_process_start(&tiku_mic_process, NULL);
    return TIKU_MIC_OK;
}

void tiku_mic_stop(void)
{
    if (!tiku_thread_in_kernel()) return;
    tiku_process_exit(&tiku_mic_process);
    if (mic.st.state == TIKU_MIC_RUNNING) tiku_mic_arch_close();
    mic.st.state = TIKU_MIC_IDLE;
    release();
}

size_t tiku_mic_read(int16_t *out, size_t max)
{
    uint32_t waiting = mic.head - mic.tail, at, first;
    size_t n;

    if (!tiku_thread_in_kernel() || mic.ring == NULL || out == NULL) return 0;
    n = waiting < max ? waiting : max;
    at = mic.tail % TIKU_MIC_RING_SAMPLES;
    first = TIKU_MIC_RING_SAMPLES - at;
    if (first > n) first = (uint32_t)n;
    memcpy(out, &mic.ring[at], first * sizeof(int16_t));
    memcpy(&out[first], mic.ring, (n - first) * sizeof(int16_t));
    mic.tail += (uint32_t)n;
    return n;
}

int tiku_mic_gain(int db)
{
    if (!tiku_thread_in_kernel()) return TIKU_MIC_ERR_STATE;
    if (db < 0 || db > TIKU_MIC_GAIN_MAX) return TIKU_MIC_ERR_RANGE;
    mic.st.gain = (uint8_t)db;
    if (mic.st.state == TIKU_MIC_RUNNING && tiku_mic_arch_gain(db) != 0)
        return TIKU_MIC_ERR_DEVICE;
    return TIKU_MIC_OK;
}

void tiku_mic_status(tiku_mic_status_t *out)
{
    if (out == NULL) return;
    *out = mic.st;
    out->buffered = mic.head - mic.tail;
}
