/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpagent_arch.c - P4 LP agent: the job table from the HP side.
 *
 * The payload's copy in LP SRAM is the one that runs; the durable copy
 * (specs and command lines, no run state) reloads it after a power cycle.
 * Periods are converted to LP timer ticks at the rate measured this boot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <kernel/memory/tiku_mem.h>

#include "tiku_lpagent_arch.h"
#include "tiku_lpcore_arch.h"
#include "tiku_sleep_arch.h"

#ifndef TIKU_P4_FENCE
#define TIKU_P4_FENCE() __asm__ volatile("fence iorw, iorw" ::: "memory")
#endif

/** @brief The longest period a job takes: a day. */
#define LPAGENT_PERIOD_MAX_MS 86400000UL

typedef struct {
    tiku_lpagent_spec_t job[TIKU_LPAGENT_JOBS];
} lpagent_table_t;

static TIKU_DURABLE lpagent_table_t lpagent_table;
TIKU_PERSIST_CELL(lpagent_cell, lpagent_table, 0x4C504A31UL /* LPJ1 */,
                  NULL, 0);

/** @brief This boot's job wake, and whether its command was handed over. */
static tiku_lpagent_wake_t woke;
static uint8_t woke_valid, woke_cmd_taken;

/** @brief Write slot @p slot of the payload's table from @p spec. */
static void load_job(unsigned slot, const tiku_lpagent_spec_t *spec)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    tiku_lpagent_job_t *j = &sh->jobs[slot];
    uint64_t ticks = ((uint64_t)spec->period_ms * tiku_p4_lp_hz()) / 1000ULL;
    unsigned i;

    j->kind = 0u;
    TIKU_P4_FENCE();
    j->source = spec->source;
    j->op = spec->op;
    j->action = spec->action;
    j->period = ticks == 0ULL ? 1u
              : ticks > 0x7FFFFFFFULL ? 0x7FFFFFFFu : (uint32_t)ticks;
    j->threshold = spec->threshold;
    j->next_due = 0u;
    j->last = 0;
    j->state = 0u;
    j->hits = 0u;
    for (i = 0u; i < TIKU_LPAGENT_CMD_MAX; i++) {
        sh->agent_cmd[slot][i] = spec->cmd[i];
    }
    TIKU_P4_FENCE();
    j->kind = spec->kind;
}

/** @brief Write slot @p slot of the durable copy, then commit it. */
static void store_job(unsigned slot, const tiku_lpagent_spec_t *spec)
{
    lpagent_table_t next = lpagent_table;

    if (spec != NULL) {
        next.job[slot] = *spec;
    } else {
        memset(&next.job[slot], 0, sizeof(next.job[slot]));
    }
    tiku_persist_cell_commit(&lpagent_cell, &next, (uint16_t)sizeof(next));
}

void tiku_lpagent_boot(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    unsigned slot, jobs = 0u;

    (void)tiku_persist_cell_init(&lpagent_cell);
    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        jobs += lpagent_table.job[slot].kind != 0u;
    }
    if (tiku_p4_lpcore_launched()) {
        slot = sh->agent_wake_job;
        if ((tiku_p4_wake_cause() & TIKU_P4_WAKE_LP_CORE) &&
            slot >= 1u && slot <= TIKU_LPAGENT_JOBS) {
            woke.job = slot - 1u;
            woke.value = sh->agent_wake_value;
            woke.lp_ticks = sh->agent_wake_ticks;
            memcpy(woke.cmd, (const char *)sh->agent_cmd[slot - 1u],
                   TIKU_LPAGENT_CMD_MAX);
            woke.cmd[TIKU_LPAGENT_CMD_MAX - 1u] = '\0';
            woke_valid = 1u;
        }
        sh->agent_wake_job = 0u;
        sh->agent_armed = 0u;
    } else if (jobs == 0u || tiku_p4_lpcore_start() != 0) {
        return;
    }
    /* A durable job the running table lacks (a fresh load) is loaded. */
    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        if (lpagent_table.job[slot].kind != 0u && sh->jobs[slot].kind == 0u) {
            load_job(slot, &lpagent_table.job[slot]);
        }
    }
}

int tiku_lpagent_add(const tiku_lpagent_spec_t *spec)
{
    unsigned slot;

    if (spec == NULL ||
        (spec->kind != TIKU_LPAGENT_KIND_IO &&
         spec->kind != TIKU_LPAGENT_KIND_TIMER) ||
        (spec->kind == TIKU_LPAGENT_KIND_IO &&
         spec->source >= TIKU_LPCORE_LP_IO_COUNT) ||
        spec->op < TIKU_LPAGENT_OP_EQ || spec->op > TIKU_LPAGENT_OP_ALWAYS ||
        (spec->action & ~(TIKU_LPAGENT_ACT_WAKE | TIKU_LPAGENT_ACT_SAMPLE)) ||
        spec->period_ms == 0u || spec->period_ms > LPAGENT_PERIOD_MAX_MS ||
        memchr(spec->cmd, '\0', TIKU_LPAGENT_CMD_MAX) == NULL) {
        return -2;
    }
    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        if (lpagent_table.job[slot].kind == 0u) {
            break;
        }
    }
    if (slot == TIKU_LPAGENT_JOBS) {
        return -1;
    }
    if (!tiku_p4_lpcore_running() && tiku_p4_lpcore_start() != 0) {
        return -3;
    }
    load_job(slot, spec);
    store_job(slot, spec);
    return (int)slot;
}

int tiku_lpagent_del(unsigned slot)
{
    if (slot >= TIKU_LPAGENT_JOBS || lpagent_table.job[slot].kind == 0u) {
        return -1;
    }
    TIKU_LPCORE_SHARED->jobs[slot].kind = 0u;
    store_job(slot, NULL);
    return 0;
}

int tiku_lpagent_get(unsigned slot, tiku_lpagent_spec_t *out, uint32_t *hits)
{
    if (slot >= TIKU_LPAGENT_JOBS || lpagent_table.job[slot].kind == 0u) {
        return 0;
    }
    *out = lpagent_table.job[slot];
    *hits = (tiku_p4_lpcore_launched() &&
             TIKU_LPCORE_SHARED->jobs[slot].kind != 0u)
                ? TIKU_LPCORE_SHARED->jobs[slot].hits : 0u;
    return 1;
}

uint32_t tiku_lpagent_sample_count(void)
{
    return tiku_p4_lpcore_launched() ? TIKU_LPCORE_SHARED->sample_count : 0u;
}

int tiku_lpagent_sample(uint32_t n, uint32_t *lp_ticks, uint32_t *job,
                        int32_t *value)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t count = tiku_lpagent_sample_count();
    const tiku_lpagent_sample_t *s;

    if (n >= count || count - n > TIKU_LPAGENT_SAMPLES) {
        return 0;
    }
    TIKU_P4_FENCE();
    s = &sh->samples[n % TIKU_LPAGENT_SAMPLES];
    *lp_ticks = s->lp_ticks;
    *job = s->job;
    *value = s->value;
    return 1;
}

void tiku_lpagent_arm(void)
{
    if (tiku_p4_lpcore_launched()) {
        TIKU_LPCORE_SHARED->agent_wake_job = 0u;
        TIKU_P4_FENCE();
        TIKU_LPCORE_SHARED->agent_armed = 1u;
    }
}

int tiku_lpagent_woken(tiku_lpagent_wake_t *out)
{
    if (woke_valid) {
        *out = woke;
    }
    return woke_valid;
}

int tiku_lpagent_take_wake_cmd(char *out, unsigned cap)
{
    if (!woke_valid || woke_cmd_taken || woke.cmd[0] == '\0' || cap == 0u) {
        return 0;
    }
    woke_cmd_taken = 1u;
    strncpy(out, woke.cmd, cap - 1u);
    out[cap - 1u] = '\0';
    return 1;
}
