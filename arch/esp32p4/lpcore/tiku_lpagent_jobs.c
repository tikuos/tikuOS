/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpagent_jobs.c - the LP agent's job runner (P4 LP core payload).
 *
 * Reads the hardware through the payload's tiku_lpcore_io_get(),
 * _adc_get() and _i2c_get() and wakes the HP core through its
 * tiku_lpcore_wake_hp(), so a host model can run it on a page of its own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_lpagent_jobs.h"

/**
 * @brief Whether a job fires on @p value, and its new state.
 *
 * Comparisons fire on the run they turn true; CHANGE, RISE and FALL need a
 * previous run (SEEDED).
 */
static int agent_fires(tiku_lpagent_job_t *j, int32_t value)
{
    uint32_t state = j->state;
    int32_t last = j->last;
    int cond, fire;

    switch (j->op) {
    case TIKU_LPAGENT_OP_EQ: cond = value == j->threshold; break;
    case TIKU_LPAGENT_OP_NE: cond = value != j->threshold; break;
    case TIKU_LPAGENT_OP_GT: cond = value > j->threshold; break;
    case TIKU_LPAGENT_OP_LT: cond = value < j->threshold; break;
    default: cond = 0; break;
    }
    switch (j->op) {
    case TIKU_LPAGENT_OP_CHANGE:
        fire = (state & TIKU_LPAGENT_SEEDED) && value != last;
        break;
    case TIKU_LPAGENT_OP_RISE:
        fire = (state & TIKU_LPAGENT_SEEDED) && last == 0 && value != 0;
        break;
    case TIKU_LPAGENT_OP_FALL:
        fire = (state & TIKU_LPAGENT_SEEDED) && last != 0 && value == 0;
        break;
    case TIKU_LPAGENT_OP_ALWAYS:
        fire = 1;
        break;
    default:
        fire = cond && !(state & TIKU_LPAGENT_MATCHED);
        break;
    }
    j->last = value;
    j->state = TIKU_LPAGENT_SEEDED | (cond ? TIKU_LPAGENT_MATCHED : 0u);
    return fire;
}

/** @brief Read job @p j's source. */
static int32_t agent_read(const tiku_lpagent_job_t *j)
{
    switch (j->kind) {
    case TIKU_LPAGENT_KIND_IO:
        return (int32_t)tiku_lpcore_io_get(j->source & 15u);
    case TIKU_LPAGENT_KIND_ADC:
        return tiku_lpcore_adc_get(j->source & 7u);
    case TIKU_LPAGENT_KIND_I2C:
        return tiku_lpcore_i2c_get(j->source & 0x7Fu, j->arg);
    default:                                /* KIND_TIMER: the run count */
        return (j->state & TIKU_LPAGENT_SEEDED) ? j->last + 1 : 1;
    }
}

void tiku_lpagent_run(tiku_lpcore_shared_t *sh, uint32_t now)
{
    uint32_t i;

    for (i = 0u; i < TIKU_LPAGENT_JOBS; i++) {
        tiku_lpcore_stream_step();
        tiku_lpagent_job_t *j = &sh->jobs[i];
        uint32_t kind = j->kind;
        int32_t value;

        if (kind == 0u) {
            continue;
        }
        if (j->state == 0u) {
            j->next_due = now;              /* a fresh job runs at once */
        }
        if ((int32_t)(now - j->next_due) < 0) {
            continue;
        }
        value = agent_read(j);
        if (value == TIKU_LPAGENT_NO_READING) {
            continue;                       /* due again on the next pass */
        }
        /* The cadence holds through a late pass; a run behind by a whole
         * period starts a new one from now. */
        j->next_due = j->next_due + (j->period ? j->period : 1u);
        if ((int32_t)(now - j->next_due) >= 0) {
            j->next_due = now + (j->period ? j->period : 1u);
        }
        if (!agent_fires(j, value)) {
            continue;
        }
        j->hits = j->hits + 1u;
        if (j->action & TIKU_LPAGENT_ACT_SAMPLE) {
            tiku_lpagent_sample_t *s =
                &sh->samples[sh->sample_count % TIKU_LPAGENT_SAMPLES];
            s->lp_ticks = now;
            s->job = i;
            s->value = value;
            TIKU_LPAGENT_FENCE();
            sh->sample_count = sh->sample_count + 1u;
        }
        if ((j->action & TIKU_LPAGENT_ACT_WAKE) && sh->agent_armed) {
            sh->agent_armed = 0u;
            sh->agent_wake_value = value;
            sh->agent_wake_ticks = now;
            sh->agent_wake_job = i + 1u;
            sh->wake_count = sh->wake_count + 1u;
            tiku_lpcore_wake_hp();
        }
    }
}
