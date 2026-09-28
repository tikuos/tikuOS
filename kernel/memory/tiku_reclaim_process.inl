/* Process-owner half of the coordinator. Included once by tiku_reclaim.c.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Actions live in the coordinator, not event payloads. A poll is only a hint.
 * Exit and start hooks are synchronous so dropped EXITED/INIT events cannot
 * transfer an obligation to the wrong process instance. */

static int process_index(const struct tiku_process *p)
{
    unsigned i;
    if (!p) return -1;
    for (i = 0; i < TIKU_MEM_MAX_OWNERS; i++)
        if (owners[i].live && owners[i].registration.process == p) return (int)i;
    return -1;
}
static int process_gated(unsigned oi)
{ return job.active && job.credits && (job.owners & (1u << oi)); }
int tiku_reclaim_process_owner_valid(tiku_mem_owner_t owner, const void *control, size_t size)
{
    int oi = owner_index(owner);
    return oi >= 0 && owners[oi].registration.process && stable_control(control, size);
}
static int process_owner_current(unsigned oi)
{
    const owner_t *o = &owners[oi];
    const struct tiku_process *p = o->registration.process;
    tiku_mem_reclaim_phase_t phase;
    if (!p || p != TIKU_THIS() || !p->is_running || p->generation != o->process_generation ||
        !process_gated(oi) || job.fault || !(job.touched & (1u << oi)) ||
        (job.done & (1u << oi))) return 0;
    phase = tickets[job.ticket].status.phase;
    return phase == TIKU_MEM_RECLAIM_PREPARE || phase == TIKU_MEM_RECLAIM_ABORT ||
           phase == TIKU_MEM_RECLAIM_RESTORE;
}

tiku_mem_err_t tiku_mem_owner_process_action(tiku_mem_owner_t owner,
                                             tiku_mem_owner_action_t *out)
{
    int oi = owner_index(owner);
    tiku_mem_reclaim_phase_t phase;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (oi < 0 || !out || !owners[oi].registration.process ||
        owners[oi].registration.process != TIKU_THIS() ||
        owners[oi].process_generation != TIKU_THIS()->generation) return TIKU_MEM_ERR_INVALID;
    if (!process_owner_current((unsigned)oi)) return TIKU_MEM_ERR_NOT_FOUND;
    phase = tickets[job.ticket].status.phase;
    out->job = job_handle();
    out->phase = phase == TIKU_MEM_RECLAIM_PREPARE ? TIKU_MEM_OWNER_PREPARE :
                 phase == TIKU_MEM_RECLAIM_ABORT ? TIKU_MEM_OWNER_ABORT : TIKU_MEM_OWNER_RESTORE;
    out->rebuilding = (uint8_t)(job.committed || (job.old_rebound & (1u << oi)));
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_mem_owner_process_ready(tiku_mem_owner_t owner, tiku_mem_job_t token)
{
    int oi = owner_index(owner);
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (oi < 0 || !job_equal(token) || !process_owner_current((unsigned)oi) ||
        tickets[job.ticket].status.phase != TIKU_MEM_RECLAIM_PREPARE ||
        !snapshot_unchanged() || owners[oi].consent ||
        TIKU_THIS()->exit_reason != TIKU_EXIT_NONE) return TIKU_MEM_ERR_INVALID;
    owners[oi].consent = 1;
    TIKU_THIS()->exit_reason = TIKU_EXIT_RECLAIM;
    return TIKU_MEM_OK;
}

tiku_mem_err_t tiku_mem_owner_process_ack(tiku_mem_owner_t owner,
    const tiku_mem_owner_action_t *action, tiku_mem_owner_result_t result)
{
    int oi = owner_index(owner);
    tiku_mem_owner_action_t current;
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    if (!action || oi < 0 || tiku_mem_owner_process_action(owner, &current) != TIKU_MEM_OK ||
        !job_equal(action->job) || current.phase != action->phase ||
        current.rebuilding != action->rebuilding ||
        (unsigned)result > TIKU_MEM_OWNER_FAULT) return TIKU_MEM_ERR_INVALID;
    if (result == TIKU_MEM_OWNER_WAIT) return TIKU_MEM_OK;
    if (result == TIKU_MEM_OWNER_DONE) {
        if (action->phase == TIKU_MEM_OWNER_PREPARE) return TIKU_MEM_ERR_INVALID;
        if (action->rebuilding && !owner_restored((unsigned)oi)) {
            fault(TIKU_MEM_RECLAIM_OWNER_FAULT); return TIKU_MEM_ERR_INVALID;
        }
        job.done |= (uint16_t)(1u << oi); phase_deadline();
    } else if (action->phase == TIKU_MEM_OWNER_PREPARE)
        abort_job(result == TIKU_MEM_OWNER_BUSY ? TIKU_MEM_RECLAIM_REFUSED : TIKU_MEM_RECLAIM_OWNER_FAULT);
    else fault(TIKU_MEM_RECLAIM_OWNER_FAULT);
    return TIKU_MEM_OK;
}

int tiku_mem_reclaim_process_exit_valid(const struct tiku_process *p)
{
    int oi = process_index(p);
    return oi >= 0 && process_gated((unsigned)oi) && !job.fault &&
           owners[oi].process_generation == p->generation && owners[oi].consent &&
           tickets[job.ticket].status.phase == TIKU_MEM_RECLAIM_PREPARE;
}

int tiku_mem_reclaim_process_exit(struct tiku_process *p)
{
    unsigned i; int oi;
    /* Cancel before an 8-bit event generation can wrap or supervision restarts. */
    for (i = 0; i < TIKU_MEM_MAX_TICKETS; i++)
        if (tickets[i].live && !terminal(&tickets[i]) && tickets[i].process == p)
            cancel(&tickets[i], TIKU_MEM_RECLAIM_CANCELLED);
    oi = process_index(p);
    if (oi < 0 || !process_gated((unsigned)oi)) return 0;
    owners[oi].stopped = 1;
    if (p->exit_reason == TIKU_EXIT_RECLAIM && tiku_mem_reclaim_process_exit_valid(p)) {
        owners[oi].consent = 0;
        job.done |= (uint16_t)(1u << oi); phase_deadline();
        return 1;
    }
    /* An unexpected exit is not release consent. Hold the old/partial objects
     * until the explicitly registered cleanup callback attests stopped access. */
    owners[oi].consent = 0; owners[oi].init_failed = 1;
    job.touched |= (uint16_t)(1u << oi);
    p->exit_reason = TIKU_EXIT_FAILED;
    (void)tiku_process_restart_charge(p);
    if (!job.committed && tickets[job.ticket].status.phase != TIKU_MEM_RECLAIM_ABORT)
        abort_job(TIKU_MEM_RECLAIM_OWNER_FAULT);
    job.done &= (uint16_t)~(1u << oi);
    fault(TIKU_MEM_RECLAIM_OWNER_FAULT);
    return 1;
}

void tiku_mem_reclaim_process_yielded(struct tiku_process *p)
{
    int oi = process_index(p);
    if (oi < 0 || !process_gated((unsigned)oi) || !owners[oi].consent) return;
    /* ready() is valid only immediately before a real protothread exit. */
    owners[oi].consent = 0; p->exit_reason = TIKU_EXIT_NONE;
    abort_job(TIKU_MEM_RECLAIM_OWNER_FAULT);
    fault(TIKU_MEM_RECLAIM_OWNER_FAULT);
}

int tiku_mem_reclaim_process_start_allowed(const struct tiku_process *p)
{
    int oi = process_index(p);
    return oi < 0 || !process_gated((unsigned)oi) || owners[oi].starting;
}
void tiku_mem_reclaim_process_started(struct tiku_process *p)
{
    int oi = process_index(p);
    if (oi < 0) return;
    owners[oi].process_generation = p->generation;
    owners[oi].stopped = 0; owners[oi].starting = 0;
}
int tiku_mem_reclaim_process_dispatch(const struct tiku_process *p, unsigned ev)
{
    int oi = process_index(p);
    if (oi < 0 || !process_gated((unsigned)oi)) return 1;
    if (job.fault || owners[oi].process_generation != p->generation ||
        !(job.touched & (1u << oi)) || (job.done & (1u << oi))) return 0;
    return ev == TIKU_EVENT_POLL || ev == TIKU_EVENT_INIT;
}

static tiku_mem_owner_result_t process_step(unsigned oi)
{
    owner_t *o = &owners[oi];
    struct tiku_process *p = o->registration.process;
    tiku_mem_reclaim_phase_t phase = tickets[job.ticket].status.phase;
    uint16_t bit = (uint16_t)(1u << oi);
    if (!p->is_running) {
        if (phase == TIKU_MEM_RECLAIM_PREPARE || !o->stopped) return TIKU_MEM_OWNER_FAULT;
        if (p->restart != TIKU_RESTART_ALWAYS && p->restart != TIKU_RESTART_ON_FAILURE)
            return TIKU_MEM_OWNER_FAULT; /* respect the ordinary storm limit */
        if (o->init_failed) {
            tiku_mem_owner_result_t result;
            callback_owner = (int)oi;
            result = o->registration.step(o->registration.context, job_handle(),
                phase == TIKU_MEM_RECLAIM_ABORT ? TIKU_MEM_OWNER_ABORT : TIKU_MEM_OWNER_RESTORE);
            callback_owner = -1;
            if (job.fault || tickets[job.ticket].status.phase != phase) return TIKU_MEM_OWNER_WAIT;
            if (result != TIKU_MEM_OWNER_DONE) return result;
            if ((job.committed || (job.old_rebound & bit)) &&
                !tiku_reclaim_rearm(job.layout, job.count, owner_handle(oi))) return TIKU_MEM_OWNER_FAULT;
            o->init_failed = 0;
        }
        if (phase == TIKU_MEM_RECLAIM_ABORT && !(job.old_rebound & bit)) {
            if (!tiku_reclaim_restore_original(job.old, job.layout, job.count, owner_handle(oi)))
                return TIKU_MEM_OWNER_FAULT;
            job.old_rebound |= bit;
        }
        /* start() binds the generation before INIT, including synchronous INIT.
         * Successful coordinated starts are counted separately from crashes. */
        o->starting = 1;
        if (o->process_restarts != UINT32_MAX) o->process_restarts++;
        tiku_process_start(p, p->init_data);
        o->starting = 0;
        return TIKU_MEM_OWNER_WAIT;
    }
    if (p->generation != o->process_generation) return TIKU_MEM_OWNER_FAULT;
    tiku_process_poll(p);
    return TIKU_MEM_OWNER_WAIT;
}
