/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_sdio.c - reserved traffic and fenced SDIO mode transitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_sdio.h"
#include <string.h>
#if TIKU_IPC_ENABLE
enum { FREE, QUEUED, ACTIVE, DONE, QUARANTINED };
/** @brief Validate a relative tick deadline against the clock's half range. */
static int interval(tiku_clock_time_t ticks)
{ return ticks && ticks <= TIKU_CLOCK_MAX_INTERVAL; }
/** @brief Return a stage after final access; reported quarantine needs no second take. */
static void finish(tiku_ipc_sdio_t *c, unsigned index, tiku_ipc_err_t result)
{
    c->jobs[index].result = result;
    c->jobs[index].state = c->jobs[index].reported ? FREE : DONE;
}
/** @brief Cancel unstarted work and establish a fresh local-quiescence deadline. */
static void recover(tiku_ipc_sdio_t *c, tiku_sdio_mode_t wanted)
{
    if (c->active != UINT8_MAX) c->jobs[c->active].result = TIKU_IPC_DOWN;
    for (unsigned i = 0; i < TIKU_SDIO_JOBS; i++)
        if (c->jobs[i].state == QUEUED) finish(c,i,TIKU_IPC_DOWN);
    c->wanted = wanted;
    c->state = TIKU_SDIO_QUIESCE;
    c->deadline = (tiku_clock_time_t)(tiku_clock_time()+c->profile.abort);
}
/** @brief Read native replies only after physical completion and cache synchronization. */
static tiku_ipc_err_t verify(tiku_ipc_sdio_t *c, unsigned index)
{
    if (index != 0) return TIKU_IPC_OK;
    if (!tiku_sdio_diag_valid(c->jobs[0].rx,32,24) ||
        c->jobs[0].rx[0] != c->jobs[0].tx[0] ||
        tiku_sdio_diag_session(c->jobs[0].rx,24) != c->session) return TIKU_IPC_IO;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_sdio_open(tiku_ipc_sdio_t *c, const tiku_sdio_owner_ops_t *ops,
    void *driver, const tiku_ipc_access_domain_t *domain, uint32_t engine, uint32_t peer,
    uint32_t session, tiku_sdio_mode_t initial, const tiku_sdio_profile_t *profile)
{
    tiku_ipc_err_t result;
    if (!c || c->claimed || !ops || !ops->claim || !ops->release || !ops->start ||
        !ops->poll || !ops->abort || !ops->ready || !ops->reset_to || !ops->negotiate ||
        !profile || !session || (initial != TIKU_SDIO_NATIVE && initial != TIKU_SDIO_LOADER &&
        initial != TIKU_SDIO_FACTORY) ||
        !interval(profile->burst) || !interval(profile->detect) || !interval(profile->abort) ||
        !interval(profile->boot) || !interval(profile->negotiate)) return TIKU_IPC_INVALID;
    if (c->ops && !tiku_ipc_sdio_reusable(c)) return TIKU_IPC_BUSY;
    if (c->ops && c->session == session) return TIKU_IPC_INVALID;
    for (unsigned i = 0; i < TIKU_SDIO_JOBS; i++) {
        if (tiku_ipc_domain_check(domain,engine,peer,c->jobs[i].tx,TIKU_SDIO_STAGE,
                TIKU_IPC_INPUT) != TIKU_IPC_OK ||
            tiku_ipc_domain_check(domain,engine,peer,c->jobs[i].rx,TIKU_SDIO_STAGE,
                TIKU_IPC_OUTPUT) != TIKU_IPC_OK) return TIKU_IPC_INVALID;
    }
    result = ops->claim(driver);
    if (result != TIKU_IPC_OK) return result;
    c->ops = ops; c->driver = driver; c->domain = domain; c->engine = engine; c->peer = peer;
    c->session = session; c->profile = *profile; c->wanted = initial;
    c->serial = 0;
    c->native_sequence = 0;
    memset(c->native_request,0,sizeof c->native_request);
    c->mode = TIKU_SDIO_OFF;
    c->active = UINT8_MAX; c->claimed = 1; c->state = TIKU_SDIO_DISCOVER;
    c->deadline = (tiku_clock_time_t)(tiku_clock_time()+profile->detect);
    return TIKU_IPC_OK;
}

void tiku_ipc_sdio_step(tiku_ipc_sdio_t *c)
{
    tiku_ipc_err_t result;
    uint64_t completed_token = 0;
    tiku_clock_time_t now = tiku_clock_time();
    if (!c || !c->claimed) return;
    for (unsigned i = 0; i < TIKU_SDIO_JOBS; i++)
        if (c->jobs[i].state == QUEUED && !TIKU_CLOCK_LT(now,c->jobs[i].deadline))
            finish(c,i,TIKU_IPC_TIMEOUT);
    if (c->state == TIKU_SDIO_QUIESCE || c->state == TIKU_SDIO_FAILED) {
        if (c->active != UINT8_MAX) {
            result = c->ops->poll(c->driver,&completed_token);
            if ((result == TIKU_IPC_OK && completed_token == c->exchange) ||
                c->ops->abort(c->driver,c->exchange)) {
                finish(c,c->active,c->jobs[c->active].result);
                c->active = UINT8_MAX;
            } else {
                if (!TIKU_CLOCK_LT(now,c->deadline)) {
                    c->jobs[c->active].state = QUARANTINED;
                    c->jobs[c->active].result = TIKU_IPC_TIMEOUT;
                    c->state = TIKU_SDIO_FAILED;
                }
                return;
            }
        }
        if (c->state == TIKU_SDIO_FAILED) return;
        if (c->wanted == TIKU_SDIO_OFF) {
            if (!c->ops->release(c->driver)) {
                if (!TIKU_CLOCK_LT(now,c->deadline)) c->state = TIKU_SDIO_FAILED;
                return;
            }
            c->claimed = 0;
            c->state = TIKU_SDIO_CLOSED; c->mode = TIKU_SDIO_OFF;
            return;
        }
        if (c->session == UINT32_MAX) { c->state = TIKU_SDIO_FAILED; return; }
        c->session++;
        c->serial = 0;
        c->native_sequence = 0;
        c->state = TIKU_SDIO_RESET;
        c->deadline = (tiku_clock_time_t)(now+c->profile.boot);
        return;
    }
    if (c->state == TIKU_SDIO_RESET) {
        result = c->ops->reset_to(c->driver,c->wanted);
        if (result == TIKU_IPC_OK) c->state = TIKU_SDIO_BOOT;
        else if (result != TIKU_IPC_AGAIN || !TIKU_CLOCK_LT(now,c->deadline))
            c->state = TIKU_SDIO_FAILED;
        return;
    }
    if (c->state == TIKU_SDIO_BOOT || c->state == TIKU_SDIO_DISCOVER) {
        result = c->ops->ready(c->driver,c->wanted);
        if (result == TIKU_IPC_OK) {
            c->state = TIKU_SDIO_NEGOTIATE;
            c->deadline = (tiku_clock_time_t)(now+c->profile.negotiate);
        } else if (result != TIKU_IPC_AGAIN || !TIKU_CLOCK_LT(now,c->deadline)) {
            if (c->state == TIKU_SDIO_DISCOVER) recover(c,TIKU_SDIO_LOADER);
            else c->state = TIKU_SDIO_FAILED;
        }
        return;
    }
    if (c->state == TIKU_SDIO_NEGOTIATE) {
        result = c->ops->negotiate(c->driver,c->wanted,c->session);
        if (result == TIKU_IPC_OK) {
            c->mode = c->wanted; c->state = TIKU_SDIO_READY;
            c->lost_at = (tiku_clock_time_t)(now+c->profile.detect);
        } else if (result != TIKU_IPC_AGAIN || !TIKU_CLOCK_LT(now,c->deadline))
            c->state = TIKU_SDIO_FAILED;
        return;
    }
    if (c->state != TIKU_SDIO_READY) return;
    if (c->active != UINT8_MAX) {
        result = c->ops->poll(c->driver,&completed_token);
        if (result != TIKU_IPC_AGAIN && completed_token != c->exchange) {
            c->stale++;
            result = TIKU_IPC_AGAIN;
        }
        if (result == TIKU_IPC_OK) {
            finish(c,c->active,verify(c,c->active));
            c->active = UINT8_MAX;
        } else if (result != TIKU_IPC_AGAIN || !TIKU_CLOCK_LT(now,c->active_until)) {
            recover(c,TIKU_SDIO_LOADER);
            c->jobs[c->active].result = result == TIKU_IPC_AGAIN ? TIKU_IPC_TIMEOUT : result;
        }
        return;
    }
    result = c->ops->ready(c->driver,c->mode);
    if (result == TIKU_IPC_OK) c->lost_at = (tiku_clock_time_t)(now+c->profile.detect);
    else if (result != TIKU_IPC_AGAIN || !TIKU_CLOCK_LT(now,c->lost_at)) {
        recover(c,TIKU_SDIO_LOADER); return;
    }
    {
        unsigned index = UINT8_MAX;
        if (c->mode == TIKU_SDIO_LOADER || c->mode == TIKU_SDIO_FACTORY) {
            if (c->jobs[3].state == QUEUED) index = 3;
        }
        else if (c->jobs[0].state == QUEUED) index = 0;
        else for (unsigned n = 0; n < 2; n++) {
            unsigned candidate = 1u+((c->cursor+n)%2u);
            if (c->jobs[candidate].state == QUEUED) { index = candidate; break; }
        }
        if (index == UINT8_MAX) return;
        if (c->serial == UINT32_MAX) {
            finish(c,index,TIKU_IPC_DOWN);
            c->state = TIKU_SDIO_FAILED;
            return;
        }
        c->exchange = (uint64_t)c->session << 32 | (c->serial+1u);
        result = c->ops->start(c->driver,c->mode,index == 0 ? TIKU_SDIO_CONTROL :
            index == 3 ? TIKU_SDIO_UPDATE : TIKU_SDIO_BULK,
            c->jobs[index].tx,c->jobs[index].tx_length,c->jobs[index].rx,c->jobs[index].rx_length,
            c->exchange);
        if (result == TIKU_IPC_OK) {
            c->serial++;
            if (index == 0) {
                c->native_sequence = c->jobs[0].tx[0];
                memcpy(c->native_request,c->jobs[0].tx,20);
            }
            c->active = (uint8_t)index; c->jobs[index].state = ACTIVE;
            c->active_until = index == 3 ? c->jobs[index].deadline :
                (tiku_clock_time_t)(now+c->profile.burst);
            if (TIKU_CLOCK_LT(c->jobs[index].deadline,c->active_until))
                c->active_until = c->jobs[index].deadline;
            if (index == 1 || index == 2) c->cursor = (uint8_t)(index%2u);
        } else if (result != TIKU_IPC_AGAIN) finish(c,index,result);
    }
}

tiku_ipc_err_t tiku_ipc_sdio_submit(tiku_ipc_sdio_t *c, tiku_sdio_class_t kind,
    const void *data, uint16_t length, uint16_t reply_length,
    tiku_clock_time_t timeout, tiku_sdio_job_t *out)
{
    unsigned first, last, index;
    if (!c || !c->claimed || !out || !data || !length || length > TIKU_SDIO_STAGE ||
        !reply_length || reply_length > TIKU_SDIO_STAGE || !interval(timeout)) return TIKU_IPC_INVALID;
    if (c->state != TIKU_SDIO_READY) return TIKU_IPC_DOWN;
    if (kind == TIKU_SDIO_CONTROL) {
        const uint8_t *request = data;
        uint8_t expected = c->native_sequence == 255 ? 1 : (uint8_t)(c->native_sequence+1u);
        if (c->mode != TIKU_SDIO_NATIVE) return TIKU_IPC_DOWN;
        if (length != 20 || reply_length != 32 ||
            !tiku_sdio_diag_valid(data,length,12) || tiku_sdio_diag_session(data,12) != c->session)
            return TIKU_IPC_INVALID;
        if (request[0] != expected && (request[0] != c->native_sequence ||
            memcmp(data,c->native_request,20))) return TIKU_IPC_INVALID;
        first = 0; last = 1;
    } else if (kind == TIKU_SDIO_BULK) {
        if (c->mode != TIKU_SDIO_NATIVE) return TIKU_IPC_DOWN;
        first = 1; last = 3;
    } else if (kind == TIKU_SDIO_UPDATE) {
        if (c->mode != TIKU_SDIO_LOADER && c->mode != TIKU_SDIO_FACTORY) return TIKU_IPC_DOWN;
        first = 3; last = 4;
    } else return TIKU_IPC_INVALID;
    for (index = first; index < last; index++)
        if (c->jobs[index].state == FREE && c->jobs[index].generation != UINT32_MAX) break;
    if (index == last) return TIKU_IPC_AGAIN;
    c->jobs[index].generation++;
    c->jobs[index].reported = 0;
    c->jobs[index].tx_length = length; c->jobs[index].rx_length = reply_length;
    memset(c->jobs[index].tx,0,TIKU_SDIO_STAGE);
    memset(c->jobs[index].rx,0,TIKU_SDIO_STAGE);
    memcpy(c->jobs[index].tx,data,length);
    c->jobs[index].deadline = (tiku_clock_time_t)(tiku_clock_time()+timeout);
    c->jobs[index].state = QUEUED;
    *out = (tiku_sdio_job_t){c->jobs[index].generation,(uint16_t)(index+1)};
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_sdio_take(tiku_ipc_sdio_t *c, tiku_sdio_job_t job,
    void *out, uint16_t capacity, tiku_sdio_result_t *result)
{
    unsigned index;
    if (!c || !job.slot_plus_one || job.slot_plus_one > TIKU_SDIO_JOBS || !result)
        return TIKU_IPC_INVALID;
    index = job.slot_plus_one-1u;
    if (c->jobs[index].generation != job.generation || c->jobs[index].state == FREE)
        return TIKU_IPC_INVALID;
    if (c->jobs[index].state != DONE && c->jobs[index].state != QUARANTINED) return TIKU_IPC_AGAIN;
    if (c->jobs[index].reported) return TIKU_IPC_AGAIN;
    if (c->jobs[index].result == TIKU_IPC_OK &&
        (!out || capacity < c->jobs[index].rx_length)) return TIKU_IPC_INVALID;
    result->status = c->jobs[index].result;
    result->quarantined = c->jobs[index].state == QUARANTINED;
    if (result->status == TIKU_IPC_OK && c->jobs[index].rx_length)
        memcpy(out,c->jobs[index].rx,c->jobs[index].rx_length);
    c->jobs[index].reported = 1;
    if (!result->quarantined) c->jobs[index].state = FREE;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_sdio_mode(tiku_ipc_sdio_t *c, tiku_sdio_mode_t wanted)
{
    if (!c || !c->claimed || (wanted != TIKU_SDIO_OFF && wanted != TIKU_SDIO_NATIVE &&
        wanted != TIKU_SDIO_LOADER && wanted != TIKU_SDIO_FACTORY)) return TIKU_IPC_INVALID;
    recover(c,wanted);
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_sdio_discard(tiku_ipc_sdio_t *c, tiku_sdio_job_t job)
{
    unsigned index;
    if (!c || !job.slot_plus_one || job.slot_plus_one > TIKU_SDIO_JOBS)
        return TIKU_IPC_INVALID;
    index = job.slot_plus_one-1u;
    if (c->jobs[index].state == FREE || c->jobs[index].generation != job.generation)
        return TIKU_IPC_INVALID;
    c->jobs[index].reported = 1;
    if (c->jobs[index].state == QUEUED || c->jobs[index].state == DONE)
        c->jobs[index].state = FREE;
    return TIKU_IPC_OK;
}
uint8_t tiku_ipc_sdio_reusable(const tiku_ipc_sdio_t *c)
{
    if (!c || c->claimed || c->state != TIKU_SDIO_CLOSED) return 0;
    for (unsigned i = 0; i < TIKU_SDIO_JOBS; i++) if (c->jobs[i].state != FREE) return 0;
    return 1;
}
#endif
