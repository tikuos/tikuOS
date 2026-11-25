/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_mount_proc.c - the process behind /peer/<name>.
 *
 * It owns each peer's <peer>/vfs endpoint: it answers the peer's requests
 * from this side's tree and carries this side's requests out.  A caller
 * of tiku_peer_mount_call() runs the scheduler until its answer is in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include "tiku_peer_mount.h"
#include <interfaces/peer/tiku_peer.h>
#include <kernel/ipc/tiku_ipc.h>
#include <kernel/process/tiku_process.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/cpu/tiku_hang.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Ticks a request waits for its answer, and an answer to be sent. */
#define WAIT_TICKS  ((tiku_clock_time_t)(TIKU_PEER_MOUNT_WAIT_S * TIKU_CLOCK_SECOND))

/** @brief This side's end of one peer's "vfs" service. */
typedef struct {
    tiku_ipc_endpoint_t ep;
    char     name[32];          /* "<peer>/vfs" */
    uint8_t  present;           /* the peer lists the service */
    uint8_t  open;              /* ep is bound */
    uint8_t  out_len;           /* an answer waits to be sent */
    uint16_t restarts;          /* the peer's restart count last seen */
    tiku_clock_time_t out_at;   /* when that answer was made */
    uint8_t  out[TIKU_PEER_MOUNT_MSG];
    tiku_peer_mount_server_t server;
} side_t;

static side_t side[TIKU_PEER_MOUNT_PEERS];

/** @brief Where this side's one request is. */
enum { CALL_IDLE = 0, CALL_QUEUED, CALL_SENT, CALL_ANSWERED, CALL_FAILED };

static struct {
    uint8_t state, peer, id, len, answer_len;
    uint8_t request[TIKU_PEER_MOUNT_MSG];
    uint8_t answer[TIKU_PEER_MOUNT_MSG];
} call;

static struct tiku_timer retry_timer;

TIKU_PROCESS(tiku_peer_mount_process, "Peer mount");

/**
 * @brief Move what can move for peer @p i: send a waiting answer, take what
 *        arrived, send a queued request.
 * @return 0 when nothing waits, 1 when a message waits to be sent, 2 when
 *         the endpoint is not open yet
 */
static uint8_t
side_work(uint8_t i)
{
    side_t *s = &side[i];
    tiku_ipc_received_t m;
    tiku_peer_status_t st;
    tiku_ipc_err_t rc;

    if (s->present == 0u) {
        return 0u;
    }
    if (s->open == 0u) {
        if (tiku_ipc_open(s->name, &s->ep) != TIKU_IPC_OK) {
            return 2u;
        }
        s->open = 1u;
    }
    /* A new instance of the peer knows nothing of what was under way. */
    if (tiku_peer_status(i, &st) == TIKU_PEER_OK &&
        st.restarts != s->restarts) {
        s->restarts = st.restarts;
        s->out_len = 0u;
        s->server.held_valid = 0u;
        s->server.write_open = 0u;
    }
    for (;;) {
        if (s->out_len != 0u) {
            rc = tiku_ipc_send(s->ep, s->out, s->out_len);
            if (rc == TIKU_IPC_AGAIN &&
                (tiku_clock_time_t)(tiku_clock_time() - s->out_at) < WAIT_TICKS) {
                break;
            }
            s->out_len = 0u;
        }
        if (tiku_ipc_receive(s->ep, &m) != TIKU_IPC_OK) {
            break;
        }
        if (m.is_buffer) {
            (void)tiku_ipc_ack(s->ep, m.submission, TIKU_IPC_UNSUPPORTED);
            continue;
        }
        if (m.length < TIKU_PEER_MOUNT_HEAD || m.length > TIKU_PEER_MOUNT_MSG) {
            continue;
        }
        if (m.data[0] == TIKU_PEER_MOUNT_ANSWER) {
            if (call.state == CALL_SENT && call.peer == i &&
                m.data[1] == call.id) {
                memcpy(call.answer, m.data, m.length);
                call.answer_len = (uint8_t)m.length;
                call.state = CALL_ANSWERED;
            }
        } else {
            s->out_len = tiku_peer_mount_serve(&s->server, m.data,
                                               (uint8_t)m.length, s->out);
            s->out_at = tiku_clock_time();
        }
    }
    if (call.state == CALL_QUEUED && call.peer == i && s->out_len == 0u) {
        rc = tiku_ipc_send(s->ep, call.request, call.len);
        if (rc == TIKU_IPC_OK) {
            call.state = CALL_SENT;
        } else if (rc != TIKU_IPC_AGAIN) {
            call.state = CALL_FAILED;
        }
    }
    return (uint8_t)(s->out_len != 0u ||
                     (call.state == CALL_QUEUED && call.peer == i));
}

/** @brief Work every peer, then come back in a tick while a message waits
 *         to be sent, in a second while an endpoint is not open. */
static void
mount_service(void)
{
    uint8_t i, most = 0u;

    for (i = 0; i < TIKU_PEER_MOUNT_PEERS; i++) {
        uint8_t w = side_work(i);

        if (w == 1u || (w == 2u && most == 0u)) {
            most = w;
        }
    }
    tiku_timer_stop(&retry_timer);
    if (most == 1u) {
        tiku_timer_set_event(&retry_timer, 1u);
    } else if (most == 2u) {
        tiku_timer_set_event(&retry_timer, TIKU_CLOCK_SECOND);
    }
}

TIKU_PROCESS_THREAD(tiku_peer_mount_process, ev, data)
{
    (void)ev;
    (void)data;

    TIKU_PROCESS_BEGIN();
    for (;;) {
        mount_service();
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}

int
tiku_peer_mount_call(uint8_t peer, uint8_t *request, uint8_t length,
                     uint8_t *answer, uint8_t *answer_length)
{
    struct tiku_process *self = TIKU_THIS();
    tiku_peer_status_t st;
    tiku_clock_time_t t0;
    uint16_t restarts;
    uint8_t state;

    if (length < TIKU_PEER_MOUNT_HEAD || length > TIKU_PEER_MOUNT_MSG) {
        return TIKU_VFS_EINVAL;
    }
    if (peer >= TIKU_PEER_MOUNT_PEERS || side[peer].present == 0u ||
        tiku_peer_status(peer, &st) != TIKU_PEER_OK ||
        st.state != TIKU_PEER_READY) {
        return TIKU_VFS_ENOTSUP;
    }
    if (call.state != CALL_IDLE || self == &tiku_peer_mount_process) {
        return TIKU_VFS_EBUSY;
    }
    restarts = st.restarts;
    call.id++;
    request[1] = call.id;
    memcpy(call.request, request, length);
    call.len = length;
    call.peer = peer;
    call.state = CALL_QUEUED;
    tiku_process_poll(&tiku_peer_mount_process);
    t0 = tiku_clock_time();
    while (call.state == CALL_QUEUED || call.state == CALL_SENT) {
        if ((tiku_clock_time_t)(tiku_clock_time() - t0) >= WAIT_TICKS ||
            tiku_peer_status(peer, &st) != TIKU_PEER_OK ||
            st.state != TIKU_PEER_READY || st.restarts != restarts) {
            call.state = CALL_FAILED;
            break;
        }
        (void)tiku_sched_run_once_except(self);
        tiku_hang_checkin();
    }
    state = call.state;
    call.state = CALL_IDLE;
    if (state != CALL_ANSWERED) {
        return TIKU_VFS_EIO;
    }
    memcpy(answer, call.answer, call.answer_len);
    *answer_length = call.answer_len;
    return TIKU_VFS_OK;
}

void
tiku_peer_mount_start(void)
{
    uint8_t i, any = 0u;

    for (i = 0; i < tiku_peer_count && i < TIKU_PEER_MOUNT_PEERS; i++) {
        const tiku_peer_t *p = tiku_peer_get(i);
        size_t n = strlen(p->name);

        if (tiku_peer_service(i, "vfs") < 0 || n + 5u > sizeof(side[i].name)) {
            continue;
        }
        memcpy(side[i].name, p->name, n);
        memcpy(side[i].name + n, "/vfs", 5u);
        side[i].server.cap = TIKU_PEER_MOUNT_CAP;
        side[i].present = 1u;
        any = 1u;
    }
    if (any != 0u && !tiku_process_is_running(&tiku_peer_mount_process)) {
        (void)tiku_process_register("Peer mount", &tiku_peer_mount_process);
        tiku_process_start(&tiku_peer_mount_process, (tiku_event_data_t)0);
    }
}

#endif /* TIKU_PEER_ENABLE */
