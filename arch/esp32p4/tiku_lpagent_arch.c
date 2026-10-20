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
#include <tiku.h>
#include <kernel/memory/tiku_mem.h>
#include <interfaces/gpio/tiku_gpio_owner.h>

#include "tiku_lpagent_arch.h"
#include "tiku_lpcore_arch.h"
#include "tiku_sleep_arch.h"
#if defined(TIKU_BOARD_ADC_AVAILABLE)
#include "tiku_adc_arch.h"
#endif

#ifndef TIKU_P4_FENCE
#define TIKU_P4_FENCE() __asm__ volatile("fence iorw, iorw" ::: "memory")
#endif

/** @brief The longest period a job takes: a day. */
#define LPAGENT_PERIOD_MAX_MS 86400000UL

typedef struct {
    tiku_lpagent_spec_t job[TIKU_LPAGENT_JOBS];
} lpagent_table_t;

static TIKU_DURABLE lpagent_table_t lpagent_table;
TIKU_PERSIST_CELL(lpagent_cell, lpagent_table, 0x4C504A32UL /* LPJ2 */,
                  NULL, 0);

/** @brief Owner name of the I2C pins while I2C jobs exist. */
static const char lpagent_owner[] = "lpagent";

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
    j->arg = spec->arg;
#if defined(TIKU_BOARD_I2C_SDA_PIN)
    if (spec->kind == TIKU_LPAGENT_KIND_I2C) {
        j->arg = spec->arg | ((uint32_t)TIKU_BOARD_I2C_SDA_PIN << 16) |
                 ((uint32_t)TIKU_BOARD_I2C_SCL_PIN << 24);
    }
#endif
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

/** @brief Jobs of @p kind in the durable table. */
static unsigned kind_count(unsigned kind)
{
    unsigned slot, n = 0u;

    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        n += lpagent_table.job[slot].kind == kind;
    }
    return n;
}

/** @brief Claim (@p take) or release the board's I2C pins as the agent's.
 *  @return 0, or -1 when another owner holds one */
static int i2c_pins(int take)
{
#if defined(TIKU_BOARD_I2C_SDA_PIN)
    static const uint8_t pins[2] = {TIKU_BOARD_I2C_SDA_PIN,
                                    TIKU_BOARD_I2C_SCL_PIN};
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        if (!take) {
            (void)tiku_gpio_release((uint8_t)(pins[i] / 8u + 1u),
                                    (uint8_t)(pins[i] % 8u), lpagent_owner);
        } else if (tiku_gpio_claim((uint8_t)(pins[i] / 8u + 1u),
                                   (uint8_t)(pins[i] % 8u), lpagent_owner) != 0) {
            while (i-- > 0u) {
                (void)tiku_gpio_release((uint8_t)(pins[i] / 8u + 1u),
                                        (uint8_t)(pins[i] % 8u), lpagent_owner);
            }
            return -1;
        }
    }
    return 0;
#else
    (void)take;
    return -1;
#endif
}

/** @brief Take from the HP drivers what a job of @p kind reads.
 *  @return 0, or -1 when they hold it */
static int resource_take(unsigned kind)
{
    if (kind == TIKU_LPAGENT_KIND_ADC) {
#if defined(TIKU_BOARD_ADC_AVAILABLE)
        return tiku_p4_adc_lp_hold();
#else
        return -1;
#endif
    }
    return kind == TIKU_LPAGENT_KIND_I2C ? i2c_pins(1) : 0;
}

/** @brief Give back what the jobs of @p kind read. */
static void resource_give(unsigned kind)
{
    if (kind == TIKU_LPAGENT_KIND_ADC) {
#if defined(TIKU_BOARD_ADC_AVAILABLE)
        tiku_p4_adc_lp_release();
#endif
    } else if (kind == TIKU_LPAGENT_KIND_I2C) {
        (void)i2c_pins(0);
    }
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
    /* The HP side of ADC and I2C jobs is lost with HP RAM: take it again.
     * A kind whose resources stay held elsewhere is not run. */
    {
        uint8_t held[TIKU_LPAGENT_KIND_I2C + 1u] = {0};
        unsigned kind;

        for (kind = TIKU_LPAGENT_KIND_ADC; kind <= TIKU_LPAGENT_KIND_I2C;
             kind++) {
            held[kind] = kind_count(kind) == 0u || resource_take(kind) == 0;
        }
        for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
            kind = lpagent_table.job[slot].kind;
            if (kind >= TIKU_LPAGENT_KIND_ADC && !held[kind]) {
                sh->jobs[slot].kind = 0u;
            } else if (kind != 0u && sh->jobs[slot].kind == 0u) {
                /* A durable job the running table lacks (a fresh load). */
                load_job(slot, &lpagent_table.job[slot]);
            }
        }
    }
}

int tiku_lpagent_add(const tiku_lpagent_spec_t *spec)
{
    unsigned slot;

    if (spec == NULL ||
        spec->kind < TIKU_LPAGENT_KIND_IO || spec->kind > TIKU_LPAGENT_KIND_I2C ||
        (spec->kind == TIKU_LPAGENT_KIND_IO &&
         spec->source >= TIKU_LPCORE_LP_IO_COUNT) ||
        (spec->kind == TIKU_LPAGENT_KIND_ADC && spec->source > 7u) ||
        (spec->kind == TIKU_LPAGENT_KIND_I2C &&
         (spec->source < 0x08u || spec->source > 0x77u ||
          (spec->arg >> 8) < 1u || (spec->arg >> 8) > 2u)) ||
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
    if (kind_count(spec->kind) == 0u && resource_take(spec->kind) != 0) {
        return -4;
    }
    load_job(slot, spec);
    store_job(slot, spec);
    return (int)slot;
}

int tiku_lpagent_del(unsigned slot)
{
    unsigned kind;

    if (slot >= TIKU_LPAGENT_JOBS || lpagent_table.job[slot].kind == 0u) {
        return -1;
    }
    kind = lpagent_table.job[slot].kind;
    TIKU_LPCORE_SHARED->jobs[slot].kind = 0u;
    store_job(slot, NULL);
    if (kind_count(kind) == 0u) {
        resource_give(kind);
    }
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

int tiku_lpagent_uses_sar(void)
{
    unsigned slot;

    if (!tiku_p4_lpcore_launched()) {
        return 0;
    }
    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        if (TIKU_LPCORE_SHARED->jobs[slot].kind == TIKU_LPAGENT_KIND_ADC) {
            return 1;
        }
    }
    return 0;
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
