/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc.c - endpoint queues, process readiness and timed backend progress.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_backend.h"
#include "tiku_ipc_internal.h"
#include <kernel/memory/tiku_ipc_buffer.h>
#include <kernel/memory/tiku_reclaim_internal.h>
#include <kernel/timers/tiku_timer.h>
#include <hal/tiku_cpu.h>
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#include <kernel/threads/tiku_thread.h>
#endif
#include <string.h>

#if TIKU_IPC_ENABLE
#if !TIKU_PROCESS_OBSERVERS_ENABLE
#error IPC requires process observers
#endif
#if TIKU_IPC_ENDPOINTS_MAX < 1 || TIKU_IPC_ENDPOINTS_MAX > 65535 || \
    TIKU_IPC_SUBMISSIONS_MAX < 1 || TIKU_IPC_SUBMISSIONS_MAX > 65535 || \
    TIKU_IPC_QUEUE_DEPTH < 1 || TIKU_IPC_QUEUE_DEPTH > 65535 || \
    TIKU_IPC_GENERATION_MAX < 1 || TIKU_IPC_GENERATION_MAX > 0x3fffffffUL
#error Invalid IPC resource configuration
#endif

enum { FREE, WAITING, LOCAL_QUEUED, ACTIVE, TERMINAL, QUARANTINED };

typedef struct {
    struct tiku_process *owner;
    uint32_t instance, generation, session;
    tiku_ipc_received_t queue[TIKU_IPC_QUEUE_DEPTH];
    uint16_t head, used, high_water;
    uint32_t sent, delivered, drops, stale, cancelled, submit_order;
    tiku_clock_time_t service_due;
    uint8_t bound, cleanup, service_armed;
    tiku_ipc_control_fn control;
    void *control_context;
} endpoint_state_t;

typedef struct {
    tiku_ipc_transfer_t transfer;
    tiku_ipc_buffer_t buffer;
    struct tiku_process *receiver;
    uint32_t receiver_instance, endpoint_generation, destination_generation;
    uint32_t order;
    tiku_clock_time_t deadline, retry_due;
    tiku_ipc_err_t result;
    uint16_t endpoint, destination;
    uint8_t state, cancel, finished, reported, retry_armed;
} submission_state_t;

static const tiku_ipc_endpoint_config_t *config;
static endpoint_state_t endpoints[TIKU_IPC_ENDPOINTS_MAX];
static submission_state_t submissions[TIKU_IPC_SUBMISSIONS_MAX];
static struct {
    struct tiku_process *owner;
    uint32_t instance;
    uint8_t queued, inside;
} notices[TIKU_IPC_ENDPOINTS_MAX];
static uint16_t endpoint_count, cursor, ticket_cursor;
static uint8_t initialized, pumping, timer_dirty;
static struct tiku_timer deadline_timer;
static void (*status_observer)(void *);
static void *status_observer_context;
static uint32_t invalid_handles;

int tiku_ipc_reject_handle(void)
{
    tiku_atomic_enter();
    if (invalid_handles != UINT32_MAX) invalid_handles++;
    tiku_atomic_exit();
    return -1;
}

static void wake_kernel(void)
{
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
    tiku_thread_kernel_wake();
#endif
}

static uint16_t message_limit(unsigned e)
{
    uint16_t limit = config[e].ops ? config[e].ops->max_message : 0;
    return limit && limit < TIKU_IPC_MESSAGE_MAX ? limit : TIKU_IPC_MESSAGE_MAX;
}

static uint16_t buffer_limit(unsigned e)
{
    uint16_t limit = config[e].ops ? config[e].ops->max_buffer : 0;
    return limit && limit < TIKU_IPC_BUFFER_SIZE ? limit : TIKU_IPC_BUFFER_SIZE;
}

static void end_binding(endpoint_state_t *s)
{
    tiku_atomic_enter();
    if (s->bound && s->session)
        s->session = s->session == UINT32_MAX ? 0 : s->session + 1u;
    s->bound = 0;
    s->cleanup = 1;
    tiku_atomic_exit();
}

static int eligible(struct tiku_process *p, uint32_t instance)
{
    if (p == NULL || !p->is_running || p->instance != instance) return 0;
#if TIKU_MEM_RECLAIM_ENABLE
    if (!tiku_mem_reclaim_process_dispatch(p, TIKU_EVENT_IPC)) return 0;
#endif
    return 1;
}

static int endpoint_get(tiku_ipc_endpoint_t h)
{
    unsigned i;
    TIKU_MEM_KERNEL_ONLY(-1);
    if (!initialized || !h.slot_plus_one || h.slot_plus_one > endpoint_count ||
        (h.generation & 3u) != 1u) return tiku_ipc_reject_handle();
    i = h.slot_plus_one - 1u;
    return endpoints[i].bound && endpoints[i].session &&
        endpoints[i].generation == h.generation &&
        endpoints[i].owner == TIKU_THIS() &&
        eligible(TIKU_THIS(), endpoints[i].instance) ? (int)i : tiku_ipc_reject_handle();
}

static int submission_get(tiku_ipc_submission_t h)
{
    unsigned i;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_IPC_SUBMISSIONS_MAX ||
        (h.generation & 3u) != 3u) return tiku_ipc_reject_handle();
    i = h.slot_plus_one - 1u;
    return submissions[i].state != FREE &&
        submissions[i].transfer.submission.generation == h.generation
        ? (int)i : tiku_ipc_reject_handle();
}

static int owns_submission(unsigned e, unsigned t)
{
    return submissions[t].endpoint == e &&
        submissions[t].endpoint_generation == endpoints[e].generation;
}

static int waiting_eligible(unsigned t)
{
    const submission_state_t *s = &submissions[t];
    unsigned other;
    if (s->state != WAITING || (s->retry_armed &&
        TIKU_CLOCK_LT(tiku_clock_time(), s->retry_due))) return 0;
    for (other = 0; other < TIKU_IPC_SUBMISSIONS_MAX; other++) {
        uint32_t distance = s->order - submissions[other].order;
        if (submissions[other].state == WAITING &&
            submissions[other].endpoint == s->endpoint && distance &&
            distance <= UINT32_MAX / 2u) return 0;
    }
    return config[s->endpoint].ops != NULL || !endpoints[s->destination].bound ||
           endpoints[s->destination].used < TIKU_IPC_QUEUE_DEPTH;
}

static int endpoint_ready(unsigned e)
{
    unsigned i;
    if (endpoints[e].used) return 1;
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX; i++) {
        if (owns_submission(e, i) && !submissions[i].reported &&
            (submissions[i].state == TERMINAL ||
             submissions[i].state == QUARANTINED)) return 1;
    }
    return 0;
}

static int process_ready(struct tiku_process *p, uint32_t instance)
{
    unsigned e;
    for (e = 0; e < endpoint_count; e++)
        if (endpoints[e].bound && endpoints[e].owner == p &&
            endpoints[e].instance == instance && endpoint_ready(e)) return 1;
    return 0;
}

static void observer(const tiku_process_observation_t *record, void *context)
{
    unsigned i;
    (void)context;
    tiku_atomic_enter();
    for (i = 0; i < TIKU_IPC_ENDPOINTS_MAX; i++) {
        if (notices[i].owner != record->process ||
            notices[i].instance != record->instance) continue;
        if (record->transition == TIKU_PROCESS_EXITING) {
            notices[i].owner = NULL;
            notices[i].queued = notices[i].inside = 0;
        } else if (record->event == TIKU_EVENT_IPC) {
            if (record->transition != TIKU_PROCESS_AFTER) notices[i].queued = 0;
            notices[i].inside = record->transition == TIKU_PROCESS_BEFORE;
        }
    }
    if (record->transition == TIKU_PROCESS_EXITING) {
        tiku_ipc_buffer_owner_exit(record->process, record->instance);
        for (i = 0; i < endpoint_count; i++) {
            if (endpoints[i].owner == record->process &&
                endpoints[i].instance == record->instance) {
                end_binding(&endpoints[i]);
            }
        }
    }
    tiku_atomic_exit();
    wake_kernel();
}

static void deadline_callback(void *unused)
{
    (void)unused;
    timer_dirty = 1;
    wake_kernel();
}

static int name_valid(const char *name)
{
    const char *part, *end;
    if (name == NULL || !name[0] || strlen(name) > 30) return 0;
    part = name;
    for (;;) {
        size_t length;
        end = strchr(part, '/');
        length = end != NULL ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (end == NULL) return 1;
        part = end + 1;
    }
}

tiku_ipc_err_t tiku_ipc_init(const tiku_ipc_endpoint_config_t *table,
                            uint16_t count)
{
    unsigned i, j;
    if (initialized || TIKU_THIS() != NULL || count > TIKU_IPC_ENDPOINTS_MAX ||
        (count && table == NULL)) return TIKU_IPC_INVALID;
    for (i = 0; i < count; i++) {
        if (!name_valid(table[i].name) ||
            (!table[i].ops && table[i].peer >= count)) return TIKU_IPC_INVALID;
        for (j = 0; j < i; j++) {
            size_t a = strlen(table[i].name), b = strlen(table[j].name);
            size_t shorter = a < b ? a : b;
            if (!memcmp(table[i].name, table[j].name, shorter) &&
                (a == b || (a < b ? table[j].name[a] : table[i].name[b]) == '/'))
                return TIKU_IPC_INVALID;
        }
    }
    if (!tiku_process_observer_add(observer, NULL)) return TIKU_IPC_AGAIN;
    config = table;
    endpoint_count = count;
    for (i = 0; i < count; i++) endpoints[i].session = 1;
    tiku_timer_set_callback(&deadline_timer, 1, deadline_callback, NULL);
    tiku_timer_stop(&deadline_timer);
    tiku_ipc_buffer_init();
    initialized = 1;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_open(const char *name, tiku_ipc_endpoint_t *out)
{
    unsigned e, n;
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || name == NULL || out == NULL || TIKU_THIS() == NULL ||
        !TIKU_THIS()->is_running) return TIKU_IPC_INVALID;
    for (e = 0; e < endpoint_count; e++) if (!strcmp(name, config[e].name)) break;
    if (e == endpoint_count) return TIKU_IPC_INVALID;
    if (endpoints[e].bound || endpoints[e].cleanup) return TIKU_IPC_BUSY;
    if (!endpoints[e].session ||
        (endpoints[e].generation >> 2) >= TIKU_IPC_GENERATION_MAX)
        return TIKU_IPC_DOWN;
    for (n = 0; n < TIKU_IPC_ENDPOINTS_MAX; n++)
        if (notices[n].owner == TIKU_THIS() &&
            notices[n].instance == TIKU_THIS()->instance) break;
    if (n == TIKU_IPC_ENDPOINTS_MAX) {
        for (n = 0; n < TIKU_IPC_ENDPOINTS_MAX; n++)
            if (notices[n].owner == NULL) break;
        if (n == TIKU_IPC_ENDPOINTS_MAX) return TIKU_IPC_AGAIN;
        notices[n].owner = TIKU_THIS();
        notices[n].instance = TIKU_THIS()->instance;
        notices[n].queued = notices[n].inside = 0;
    }
    endpoints[e].owner = TIKU_THIS();
    endpoints[e].instance = TIKU_THIS()->instance;
    endpoints[e].generation = ((endpoints[e].generation >> 2) + 1u) * 4u + 1u;
    endpoints[e].bound = 1;
    *out = (tiku_ipc_endpoint_t){endpoints[e].generation, (uint16_t)(e + 1u)};
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_close(tiku_ipc_endpoint_t h)
{
    int e = endpoint_get(h);
    if (e < 0) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    end_binding(&endpoints[e]);
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

/** @brief Mutate an ingress queue inside the caller's atomic section. */
static tiku_ipc_err_t enqueue(unsigned e, const tiku_ipc_received_t *message)
{
    endpoint_state_t *s = &endpoints[e];
    if (!s->bound || !s->session) return TIKU_IPC_DOWN;
    if (s->used == TIKU_IPC_QUEUE_DEPTH) { s->drops++; return TIKU_IPC_AGAIN; }
    s->queue[(s->head + s->used) % TIKU_IPC_QUEUE_DEPTH] = *message;
    s->used++;
    if (s->used > s->high_water) s->high_water = s->used;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_deliver(uint16_t e, uint32_t session,
                               const void *data, uint16_t length)
{
    tiku_ipc_received_t message = {0};
    tiku_ipc_err_t result;
    if (!initialized || e >= endpoint_count || length > TIKU_IPC_MESSAGE_MAX ||
        (length && data == NULL)) return TIKU_IPC_INVALID;
    if (length > message_limit(e)) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    if (!session || session != endpoints[e].session) {
        endpoints[e].stale++;
        tiku_atomic_exit();
        return TIKU_IPC_INVALID;
    }
    message.length = length;
    if (length) memcpy(message.data, data, length);
    result = enqueue(e, &message);
    tiku_atomic_exit();
    wake_kernel();
    return result;
}

tiku_ipc_err_t tiku_ipc_send(tiku_ipc_endpoint_t h,
                            const void *data, uint16_t length)
{
    int e = endpoint_get(h);
    tiku_ipc_err_t result;
    if (e < 0 || length > TIKU_IPC_MESSAGE_MAX || (length && data == NULL))
        return TIKU_IPC_INVALID;
    if (length > message_limit((unsigned)e)) return TIKU_IPC_INVALID;
    if (config[e].ops != NULL) {
        if (config[e].ops->send == NULL) return TIKU_IPC_UNSUPPORTED;
        result = config[e].ops->send(config[e].context, (uint16_t)e,
                                    endpoints[e].session, data, length);
    } else {
        unsigned peer = config[e].peer;
        result = tiku_ipc_deliver((uint16_t)peer, endpoints[peer].session,
                                  data, length);
    }
    if (result == TIKU_IPC_OK) endpoints[e].sent++;
    return result;
}

tiku_ipc_err_t tiku_ipc_receive(tiku_ipc_endpoint_t h, tiku_ipc_received_t *out)
{
    int e = endpoint_get(h);
    endpoint_state_t *s;
    if (e < 0 || out == NULL) return TIKU_IPC_INVALID;
    s = &endpoints[e];
    tiku_atomic_enter();
    if (!s->used) { tiku_atomic_exit(); return TIKU_IPC_AGAIN; }
    *out = s->queue[s->head];
    s->head = (uint16_t)((s->head + 1u) % TIKU_IPC_QUEUE_DEPTH);
    s->used--;
    s->delivered++;
    if (out->is_buffer) {
        int t = submission_get(out->submission);
        if (t < 0 || submissions[t].state != LOCAL_QUEUED) {
            tiku_atomic_exit();
            return TIKU_IPC_INVALID;
        }
        submissions[t].state = ACTIVE;
        submissions[t].receiver = TIKU_THIS();
        submissions[t].receiver_instance = TIKU_THIS()->instance;
    }
    tiku_atomic_exit();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_submit(tiku_ipc_endpoint_t h, tiku_ipc_buffer_t buffer,
    tiku_ipc_direction_t direction, tiku_clock_time_t timeout,
    tiku_ipc_submission_t *out)
{
    int e = endpoint_get(h);
    unsigned i;
    uint32_t generation;
    void *data;
    uint16_t length;
    tiku_ipc_err_t result;
    if (e < 0 || out == NULL || !timeout || timeout > TIKU_CLOCK_MAX_INTERVAL)
        return TIKU_IPC_INVALID;
    if (config[e].ops && !config[e].ops->start) return TIKU_IPC_UNSUPPORTED;
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX; i++) {
        if (submissions[i].endpoint == (unsigned)e &&
            submissions[i].state == QUARANTINED) return TIKU_IPC_BUSY;
    }
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX; i++)
        if (submissions[i].state == FREE &&
            (submissions[i].transfer.submission.generation >> 2) <
             TIKU_IPC_GENERATION_MAX) break;
    if (i == TIKU_IPC_SUBMISSIONS_MAX) return TIKU_IPC_AGAIN;
    result = tiku_ipc_buffer_loan(buffer, direction, buffer_limit((unsigned)e),
                                  &data, &length);
    if (result != TIKU_IPC_OK) return result;
    generation = ((submissions[i].transfer.submission.generation >> 2) + 1u)
                 * 4u + 3u;
    submissions[i] = (submission_state_t){0};
    submissions[i].transfer = (tiku_ipc_transfer_t){
        {generation, (uint16_t)(i + 1u)}, endpoints[e].session,
        data, length, direction};
    submissions[i].buffer = buffer;
    submissions[i].endpoint = (uint16_t)e;
    submissions[i].destination = config[e].peer;
    submissions[i].endpoint_generation = endpoints[e].generation;
    submissions[i].order = ++endpoints[e].submit_order;
    submissions[i].deadline = (tiku_clock_time_t)(tiku_clock_time() + timeout);
    submissions[i].state = WAITING;
    endpoints[e].sent++;
    *out = submissions[i].transfer.submission;
    timer_dirty = 1;
    wake_kernel();
    return TIKU_IPC_OK;
}

static int received_loan(tiku_ipc_endpoint_t h, tiku_ipc_submission_t ticket)
{
    int e = endpoint_get(h), t = submission_get(ticket);
    if (e < 0 || t < 0 || config[submissions[t].endpoint].ops != NULL ||
        submissions[t].destination != (unsigned)e ||
        submissions[t].destination_generation != endpoints[e].generation ||
        submissions[t].receiver != TIKU_THIS() ||
        submissions[t].receiver_instance != TIKU_THIS()->instance ||
        (submissions[t].state != ACTIVE &&
         submissions[t].state != QUARANTINED)) return -1;
    return t;
}

tiku_ipc_err_t tiku_ipc_loan_read(tiku_ipc_endpoint_t h,
    tiku_ipc_submission_t ticket, const void **data, uint16_t *length)
{
    int t = received_loan(h, ticket);
    if (t < 0 || data == NULL || length == NULL ||
        submissions[t].state != ACTIVE || submissions[t].cancel ||
        submissions[t].transfer.direction != TIKU_IPC_INPUT)
        return TIKU_IPC_INVALID;
    *data = submissions[t].transfer.data;
    *length = submissions[t].transfer.length;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_loan_write(tiku_ipc_endpoint_t h,
    tiku_ipc_submission_t ticket, void **data, uint16_t *length)
{
    int t = received_loan(h, ticket);
    if (t < 0 || data == NULL || length == NULL ||
        submissions[t].state != ACTIVE || submissions[t].cancel ||
        submissions[t].transfer.direction != TIKU_IPC_OUTPUT)
        return TIKU_IPC_INVALID;
    *data = submissions[t].transfer.data;
    *length = submissions[t].transfer.length;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_ack(tiku_ipc_endpoint_t h,
    tiku_ipc_submission_t ticket, tiku_ipc_err_t status)
{
    int t = received_loan(h, ticket);
    if (t < 0) return TIKU_IPC_INVALID;
    return tiku_ipc_complete(ticket, submissions[t].transfer.session, status);
}

tiku_ipc_err_t tiku_ipc_complete(tiku_ipc_submission_t ticket,
    uint32_t session, tiku_ipc_err_t status)
{
    int t;
    tiku_atomic_enter();
    t = submission_get(ticket);
    if (t < 0 || !session || submissions[t].transfer.session != session ||
        (submissions[t].state != ACTIVE &&
         submissions[t].state != QUARANTINED) || submissions[t].finished) {
        tiku_atomic_exit();
        return TIKU_IPC_INVALID;
    }
    if (!submissions[t].cancel) {
        submissions[t].result = !TIKU_CLOCK_LT(tiku_clock_time(),
            submissions[t].deadline) ? TIKU_IPC_TIMEOUT : status;
    }
    submissions[t].finished = 1;
    timer_dirty = 1;
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_cancel(tiku_ipc_endpoint_t h,
                              tiku_ipc_submission_t ticket)
{
    int e = endpoint_get(h), t = submission_get(ticket);
    if (e < 0 || t < 0 || !owns_submission((unsigned)e, (unsigned)t))
        return TIKU_IPC_INVALID;
    if (submissions[t].state == TERMINAL ||
        submissions[t].state == QUARANTINED) return TIKU_IPC_OK;
    tiku_atomic_enter();
    submissions[t].cancel = 1;
    submissions[t].result = TIKU_IPC_CANCELLED;
    timer_dirty = 1;
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_completion(tiku_ipc_endpoint_t h,
                                  tiku_ipc_completion_t *out)
{
    int e = endpoint_get(h);
    unsigned t;
    if (e < 0 || out == NULL) return TIKU_IPC_INVALID;
    for (t = 0; t < TIKU_IPC_SUBMISSIONS_MAX; t++) {
        submission_state_t *s = &submissions[t];
        if (!owns_submission((unsigned)e, t) || s->reported ||
            (s->state != TERMINAL && s->state != QUARANTINED)) continue;
        *out = (tiku_ipc_completion_t){s->transfer.submission, s->result,
                                      s->state == QUARANTINED};
        s->reported = 1;
        if (s->state == TERMINAL) s->state = FREE;
        return TIKU_IPC_OK;
    }
    return TIKU_IPC_AGAIN;
}

tiku_ipc_err_t tiku_ipc_service_at(uint16_t e, tiku_clock_time_t delay)
{
    if (!initialized || e >= endpoint_count || delay > TIKU_CLOCK_MAX_INTERVAL)
        return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    endpoints[e].service_due = (tiku_clock_time_t)(tiku_clock_time() + delay);
    endpoints[e].service_armed = 1;
    timer_dirty = 1;
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_service_stop(uint16_t e)
{
    if (!initialized || e >= endpoint_count) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    endpoints[e].service_armed = 0;
    timer_dirty = 1;
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_peer_reset(uint16_t e)
{
    unsigned i, t;
    if (!initialized || e >= endpoint_count) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    for (i = 0; i < endpoint_count; i++) {
        if (i != e && (!config[e].ops || config[i].ops != config[e].ops ||
                       config[i].context != config[e].context)) continue;
        endpoints[i].session = endpoints[i].session == UINT32_MAX
                               ? 0 : endpoints[i].session + 1u;
        endpoints[i].head = endpoints[i].used = 0;
        for (t = 0; t < TIKU_IPC_SUBMISSIONS_MAX; t++) {
            if (submissions[t].endpoint == i &&
                submissions[t].state != FREE && submissions[t].state != TERMINAL) {
                submissions[t].cancel = 1;
                submissions[t].result = TIKU_IPC_DOWN;
            }
        }
    }
    timer_dirty = 1;
    tiku_atomic_exit();
    wake_kernel();
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_reset(uint16_t e)
{
    tiku_ipc_err_t result;
    const tiku_ipc_backend_ops_t *ops;
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || e >= endpoint_count || pumping) return TIKU_IPC_INVALID;
    ops = config[e].ops;
    if (!ops || !ops->reset || ops->reset_scope == TIKU_IPC_RESET_NONE)
        return TIKU_IPC_UNSUPPORTED;
    result = ops->reset(config[e].context, e);
    if (result == TIKU_IPC_OK) {
        if (ops->reset_scope == TIKU_IPC_RESET_ENDPOINT) {
            unsigned t;
            tiku_atomic_enter();
            endpoints[e].session = endpoints[e].session == UINT32_MAX
                                   ? 0 : endpoints[e].session + 1u;
            endpoints[e].head = endpoints[e].used = 0;
            for (t = 0; t < TIKU_IPC_SUBMISSIONS_MAX; t++) {
                if (submissions[t].endpoint == e && submissions[t].state != FREE &&
                    submissions[t].state != TERMINAL) {
                    submissions[t].cancel = 1;
                    submissions[t].result = TIKU_IPC_DOWN;
                }
            }
            timer_dirty = 1;
            tiku_atomic_exit();
            wake_kernel();
        } else (void)tiku_ipc_peer_reset(e);
    }
    return result;
}

tiku_ipc_err_t tiku_ipc_observe(void (*callback)(void *), void *context)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || TIKU_THIS() || !callback || status_observer)
        return TIKU_IPC_INVALID;
    status_observer = callback;
    status_observer_context = context;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_control_bind(uint16_t e, tiku_ipc_control_fn call, void *context)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || TIKU_THIS() || e >= endpoint_count || !call || endpoints[e].control)
        return TIKU_IPC_INVALID;
    endpoints[e].control = call;
    endpoints[e].control_context = context;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_control(uint16_t e, uint16_t operation, tiku_ipc_control_t *request)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || e >= endpoint_count || !request || pumping) return TIKU_IPC_INVALID;
    if (!endpoints[e].control) return TIKU_IPC_UNSUPPORTED;
    return endpoints[e].control(endpoints[e].control_context,operation,request);
}

/** @brief Remove queued receipts inside the caller's atomic section. */
static void purge_ticket(tiku_ipc_submission_t ticket)
{
    unsigned e, i;
    for (e = 0; e < endpoint_count; e++) {
        endpoint_state_t *s = &endpoints[e];
        uint16_t kept = 0;
        for (i = 0; i < s->used; i++) {
            tiku_ipc_received_t *m = &s->queue[(s->head + i) % TIKU_IPC_QUEUE_DEPTH];
            if (m->is_buffer && m->submission.slot_plus_one == ticket.slot_plus_one &&
                m->submission.generation == ticket.generation) continue;
            s->queue[(s->head + kept) % TIKU_IPC_QUEUE_DEPTH] = *m;
            kept++;
        }
        s->used = kept;
    }
}

static void finalize(unsigned t, int quiescent)
{
    submission_state_t *s = &submissions[t];
    if (quiescent) {
        tiku_ipc_buffer_return(s->buffer, s->result);
        s->state = TERMINAL;
        s->finished = 0;
        if (s->reported || !endpoints[s->endpoint].bound ||
            endpoints[s->endpoint].generation != s->endpoint_generation)
            s->state = FREE;
    } else {
        tiku_ipc_buffer_quarantine(s->buffer);
        s->state = QUARANTINED;
    }
    timer_dirty = 1;
}

static void service_submission(unsigned t)
{
    submission_state_t *s = &submissions[t];
    unsigned e = s->endpoint;
    if (s->state == FREE || s->state == TERMINAL) return;
    if (s->finished) { finalize(t, 1); return; }
    if (s->state == QUARANTINED) return;
    if (!endpoints[e].bound || endpoints[e].generation != s->endpoint_generation) {
        s->cancel = 1;
        s->result = TIKU_IPC_CANCELLED;
    }
    if (!s->cancel && !TIKU_CLOCK_LT(tiku_clock_time(), s->deadline)) {
        s->cancel = 1;
        s->result = TIKU_IPC_TIMEOUT;
    }
    if (s->cancel) {
        uint8_t quiescent = s->state != ACTIVE;
        if (s->state == LOCAL_QUEUED) {
            tiku_atomic_enter();
            purge_ticket(s->transfer.submission);
            tiku_atomic_exit();
        }
        if (s->state == ACTIVE && config[e].ops && config[e].ops->quiesce)
            quiescent = config[e].ops->quiesce(config[e].context, (uint16_t)e,
                                              &s->transfer);
        endpoints[e].cancelled++;
        finalize(t, quiescent);
        return;
    }
    if (s->state == WAITING) {
        tiku_ipc_err_t result;
        if (!waiting_eligible(t)) return;
        s->retry_armed = 0;
        if (config[e].ops) {
            s->state = ACTIVE;
            result = config[e].ops->start(config[e].context, (uint16_t)e,
                                           &s->transfer);
            if (result == TIKU_IPC_AGAIN) {
                s->state = WAITING;
                s->retry_due = (tiku_clock_time_t)(tiku_clock_time() + 1u);
                s->retry_armed = 1;
            } else if (result != TIKU_IPC_OK) {
                s->result = result;
                finalize(t, 1);
            }
        } else {
            tiku_ipc_received_t message = {0};
            message.is_buffer = 1;
            message.length = s->transfer.length;
            message.submission = s->transfer.submission;
            message.direction = s->transfer.direction;
            tiku_atomic_enter();
            result = enqueue(s->destination, &message);
            if (result == TIKU_IPC_OK) {
                s->destination_generation = endpoints[s->destination].generation;
                s->state = LOCAL_QUEUED;
            }
            tiku_atomic_exit();
            if (result != TIKU_IPC_OK && result != TIKU_IPC_AGAIN) {
                s->result = result;
                finalize(t, 1);
            }
        }
    }
}

static void rearm_timer(void)
{
    unsigned i;
    uint8_t found = 0;
    tiku_clock_time_t earliest = 0, now = tiku_clock_time();
    for (i = 0; i < endpoint_count; i++) {
        if (endpoints[i].service_armed &&
            (!found || TIKU_CLOCK_LT(endpoints[i].service_due, earliest))) {
            earliest = endpoints[i].service_due;
            found = 1;
        }
    }
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX; i++) {
        if (submissions[i].state != WAITING &&
            submissions[i].state != ACTIVE &&
            submissions[i].state != LOCAL_QUEUED) continue;
        if (!found || TIKU_CLOCK_LT(submissions[i].deadline, earliest)) {
            earliest = submissions[i].deadline;
            found = 1;
        }
        if (submissions[i].retry_armed &&
            TIKU_CLOCK_LT(submissions[i].retry_due, earliest))
            earliest = submissions[i].retry_due;
    }
    if (!found) tiku_timer_stop(&deadline_timer);
    else {
        tiku_clock_time_t delay = TIKU_CLOCK_LT(now, earliest)
                                 ? (tiku_clock_time_t)(earliest - now) : 1;
        if (!deadline_timer.active || (tiku_clock_time_t)(deadline_timer.start +
                deadline_timer.interval) != earliest) {
            deadline_timer.interval = delay;
            tiku_timer_restart(&deadline_timer);
        }
    }
    timer_dirty = 0;
}

void tiku_ipc_progress(uint16_t budget)
{
    unsigned i;
    if (!initialized || pumping || !budget) return;
    pumping = 1;
    for (i = 0; i < endpoint_count; i++) {
        endpoint_state_t *s = &endpoints[i];
        if (s->bound && s->owner->instance != s->instance) end_binding(s);
        if (s->cleanup) {
            s->head = s->used = 0;
            s->cleanup = 0;
            for (unsigned t = 0; t < TIKU_IPC_SUBMISSIONS_MAX; t++) {
                if ((submissions[t].endpoint == i ||
                    (!config[submissions[t].endpoint].ops &&
                     submissions[t].destination == i)) &&
                    submissions[t].state != FREE && submissions[t].state != TERMINAL) {
                    submissions[t].cancel = 1;
                    submissions[t].result = TIKU_IPC_CANCELLED;
                }
                if (submissions[t].endpoint == i && submissions[t].state == TERMINAL)
                    submissions[t].state = FREE;
            }
        }
    }
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX && i < budget; i++) {
        service_submission(ticket_cursor);
        ticket_cursor = (uint16_t)((ticket_cursor + 1u) % TIKU_IPC_SUBMISSIONS_MAX);
    }
    for (i = 0; i < endpoint_count && i < budget; i++) {
        unsigned e = cursor;
        cursor = (uint16_t)((cursor + 1u) % endpoint_count);
        if (endpoints[e].service_armed &&
            !TIKU_CLOCK_LT(tiku_clock_time(), endpoints[e].service_due)) {
            endpoints[e].service_armed = 0;
            if (config[e].ops && config[e].ops->progress)
                config[e].ops->progress(config[e].context, (uint16_t)e, budget);
            timer_dirty = 1;
        }
    }
    tiku_ipc_buffer_collect();
    tiku_atomic_enter();
    for (i = 0; i < TIKU_IPC_ENDPOINTS_MAX; i++) {
        if (notices[i].owner != NULL && !notices[i].queued && !notices[i].inside &&
            eligible(notices[i].owner, notices[i].instance) &&
            process_ready(notices[i].owner, notices[i].instance)) {
            if (tiku_process_post(notices[i].owner, TIKU_EVENT_IPC, NULL))
                notices[i].queued = 1;
            else wake_kernel();
        }
    }
    rearm_timer();
    tiku_atomic_exit();
    if (status_observer) status_observer(status_observer_context);
    pumping = 0;
}

uint8_t tiku_ipc_pending(void)
{
    unsigned i;
    uint8_t pending = 0;
    if (!initialized) return 0;
    tiku_atomic_enter();
    if (timer_dirty) pending = 1;
    for (i = 0; i < endpoint_count && !pending; i++)
        if (endpoints[i].cleanup || (endpoints[i].service_armed &&
            !TIKU_CLOCK_LT(tiku_clock_time(), endpoints[i].service_due))) pending = 1;
    for (i = 0; i < TIKU_IPC_SUBMISSIONS_MAX && !pending; i++) {
        submission_state_t *s = &submissions[i];
        if (s->finished || (s->state != FREE && s->state != TERMINAL &&
            s->state != QUARANTINED && (s->cancel ||
            !TIKU_CLOCK_LT(tiku_clock_time(), s->deadline)))) pending = 1;
        if (waiting_eligible(i)) pending = 1;
    }
    for (i = 0; i < TIKU_IPC_ENDPOINTS_MAX && !pending; i++)
        if (notices[i].owner != NULL && !notices[i].queued && !notices[i].inside &&
            eligible(notices[i].owner, notices[i].instance) &&
            process_ready(notices[i].owner, notices[i].instance)) pending = 1;
    tiku_atomic_exit();
    return pending;
}

uint16_t tiku_ipc_endpoint_count(void) { return endpoint_count; }
tiku_ipc_err_t tiku_ipc_query(tiku_ipc_endpoint_t endpoint, tiku_ipc_status_t *out)
{
    int e = endpoint_get(endpoint);
    return e < 0 ? TIKU_IPC_INVALID : tiku_ipc_status((uint16_t)e, out);
}
const char *tiku_ipc_endpoint_name(uint16_t e)
{
    return e < endpoint_count ? config[e].name : NULL;
}

tiku_ipc_err_t tiku_ipc_status(uint16_t e, tiku_ipc_status_t *out)
{
    endpoint_state_t *s;
    unsigned i;
    if (!initialized || e >= endpoint_count || out == NULL) return TIKU_IPC_INVALID;
    s = &endpoints[e];
    tiku_atomic_enter();
    *out = (tiku_ipc_status_t){0};
    out->max_message = message_limit(e);
    out->max_buffer = buffer_limit(e);
    out->alignment = TIKU_MEM_ARCH_ALIGNMENT;
    if (config[e].ops == NULL || config[e].ops->send != NULL)
        out->methods |= TIKU_IPC_CAP_COPY;
    if (config[e].ops == NULL || config[e].ops->start != NULL)
        out->methods |= TIKU_IPC_CAP_LOAN | TIKU_IPC_CAP_STREAM;
    if (!(out->methods & TIKU_IPC_CAP_COPY)) out->max_message = 0;
    if (!(out->methods & TIKU_IPC_CAP_LOAN)) out->max_buffer = 0;
    out->depth = TIKU_IPC_QUEUE_DEPTH;
    out->queued = s->used;
    out->credits = (uint16_t)(TIKU_IPC_QUEUE_DEPTH - s->used);
    out->high_water = s->high_water;
    out->session = s->session;
    out->sent = s->sent; out->delivered = s->delivered;
    out->drops = s->drops; out->stale = s->stale; out->cancelled = s->cancelled;
    out->invalid_handles_global = invalid_handles;
    out->open = s->bound;
    if (s->control) out->methods |= TIKU_IPC_CAP_CONTROL;
    if (config[e].ops && config[e].ops->reset) {
        out->reset_scope = config[e].ops->reset_scope;
        out->reset_supported = out->reset_scope != TIKU_IPC_RESET_NONE;
    }
    for (i = 0; i < TIKU_IPC_ENDPOINTS_MAX; i++)
        if (notices[i].owner == s->owner && notices[i].instance == s->instance)
            out->pending_wakeup = notices[i].queued;
    tiku_ipc_buffer_counts(&out->buffers_live, &out->buffers_free,
                            &out->buffers_quarantined);
    if (config[e].ops != NULL && config[e].ops->status != NULL)
        config[e].ops->status(config[e].context, out);
    tiku_atomic_exit();
    return TIKU_IPC_OK;
}
#endif
