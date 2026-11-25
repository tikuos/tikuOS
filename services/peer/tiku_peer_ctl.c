/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_ctl.c - the process that owns <peer>/ctl and changes the link's
 * speed with the peer: ask, both switch, check at the new speed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include "tiku_peer_ctl.h"
#include <interfaces/peer/tiku_peer.h>
#include <kernel/ipc/tiku_ipc.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_timer.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

#define WAIT_TICKS   ((tiku_clock_time_t)(TIKU_PEER_CTL_WAIT_S * TIKU_CLOCK_SECOND))
#define AGAIN_TICKS  ((tiku_clock_time_t)(TIKU_PEER_CTL_AGAIN_S * TIKU_CLOCK_SECOND))

/** @brief Where this side is in a change of speed. */
enum {
    STEP_IDLE = 0,
    STEP_ASKED,     /* asked the peer; its answer is awaited */
    STEP_TURN,      /* the change is agreed: switch once this side is sent */
    STEP_CHECK,     /* switched as the asker: CHECKED is awaited */
    STEP_CONFIRM,   /* switched as the answerer: CHECK is awaited */
    STEP_BACK       /* the change failed after a switch: to the first speed */
};

/** @brief This side's end of one peer's "ctl" service. */
typedef struct {
    tiku_ipc_endpoint_t ep;
    char     name[32];          /* "<peer>/ctl" */
    uint8_t  present;           /* the peer lists the service */
    uint8_t  open;              /* ep is bound */
    uint8_t  step;
    uint8_t  asking;            /* this side asked for the change under way */
    uint8_t  id;                /* that change's id */
    uint8_t  tries;             /* failed tries toward want in a row */
    uint8_t  out_len;           /* a message waits to be sent */
    uint8_t  out[TIKU_PEER_CTL_MSG];
    uint16_t restarts;          /* the peer's restart count last seen */
    uint16_t failures;
    uint32_t want;              /* the speed asked for; 0: none */
    uint32_t to;                /* the speed of the change under way */
    tiku_clock_time_t at;       /* when the step began */
    tiku_clock_time_t out_at;   /* when the waiting message was made */
} side_t;

static side_t side[TIKU_PEER_CTL_PEERS];
static struct tiku_timer retry_timer;

TIKU_PROCESS(tiku_peer_ctl_process, "Peer control");

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The driver's speed call for peer @p i; -1 without one. */
static int
ctl_speed(uint8_t i, uint32_t bits, uint8_t what)
{
    const tiku_peer_t *p = tiku_peer_get(i);

    if (p == (const tiku_peer_t *)0 || p->ops->speed == 0) {
        return -1;
    }
    return p->ops->speed(p->context, bits, what);
}

/** @brief Make the message to send next. */
static void
ctl_put(side_t *s, uint8_t op, uint8_t id, uint8_t result, uint32_t bits)
{
    s->out[0] = TIKU_PEER_CTL_KIND;
    s->out[1] = op;
    s->out[2] = id;
    s->out[3] = result;
    s->out[4] = (uint8_t)bits;
    s->out[5] = (uint8_t)(bits >> 8);
    s->out[6] = (uint8_t)(bits >> 16);
    s->out[7] = (uint8_t)(bits >> 24);
    s->out_len = TIKU_PEER_CTL_MSG;
    s->out_at = tiku_clock_time();
}

/** @brief The change holds on both sides: this side keeps that speed. */
static void
ctl_done(side_t *s)
{
    s->want = s->to;
    s->tries = 0u;
    s->step = STEP_IDLE;
    s->at = tiku_clock_time();
}

/** @brief The change failed; after a switch, back to the first speed.  The
 *         asker gives its speed up after TIKU_PEER_CTL_TRIES failures. */
static void
ctl_fail(side_t *s, uint8_t switched)
{
    if (s->asking != 0u && ++s->tries >= TIKU_PEER_CTL_TRIES) {
        s->want = 0u;
    }
    if (s->failures != 0xFFFFu) {
        s->failures++;
    }
    s->step = (switched != 0u) ? STEP_BACK : STEP_IDLE;
    s->at = tiku_clock_time();
}

/** @brief Ticks the answerer waits after its answer before it switches: the
 *         answer and the asker's acknowledgement, 2,000 bits, at @p bits. */
static tiku_clock_time_t
ctl_turn(uint32_t bits)
{
    uint32_t t = 2u;

    if (bits != 0u) {
        t += (2000u * (uint32_t)TIKU_CLOCK_SECOND + bits - 1u) / bits;
    }
    return (tiku_clock_time_t)t;
}

/*---------------------------------------------------------------------------*/
/* MESSAGES IN                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Act on one message from peer @p i. */
static void
ctl_take(side_t *s, uint8_t i, const uint8_t *d)
{
    const tiku_peer_t *p = tiku_peer_get(i);
    uint32_t bits = (uint32_t)d[4] | ((uint32_t)d[5] << 8) |
                    ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    uint8_t op = d[1], id = d[2];

    if (op == TIKU_PEER_CTL_ASK) {
        /* Asked while asking: the supervisor's change goes first. */
        if (s->step == STEP_ASKED &&
            (p->flags & TIKU_PEER_F_SUPERVISOR) == 0u) {
            s->step = STEP_IDLE;
        }
        if (s->step != STEP_IDLE) {
            ctl_put(s, TIKU_PEER_CTL_ANSWER, id, TIKU_PEER_CTL_BUSY, bits);
        } else if (ctl_speed(i, bits, TIKU_PEER_SPEED_CHECK) != 0) {
            ctl_put(s, TIKU_PEER_CTL_ANSWER, id, TIKU_PEER_CTL_NO, bits);
        } else {
            ctl_put(s, TIKU_PEER_CTL_ANSWER, id, TIKU_PEER_CTL_YES, bits);
            s->asking = 0u;
            s->id = id;
            s->to = bits;
            s->step = STEP_TURN;
            s->at = tiku_clock_time();
        }
        return;
    }
    if (s->id != id || s->to != bits) {
        return;
    }
    if (op == TIKU_PEER_CTL_ANSWER && s->step == STEP_ASKED && s->asking) {
        if (d[3] == TIKU_PEER_CTL_YES) {
            s->step = STEP_TURN;
            s->at = tiku_clock_time();
        } else {
            ctl_fail(s, 0u);
        }
    } else if (op == TIKU_PEER_CTL_CHECK && s->step == STEP_CONFIRM &&
               s->asking == 0u) {
        ctl_put(s, TIKU_PEER_CTL_CHECKED, id, 0u, bits);
        ctl_done(s);
    } else if (op == TIKU_PEER_CTL_CHECKED && s->step == STEP_CHECK &&
               s->asking) {
        ctl_done(s);
    }
}

/*---------------------------------------------------------------------------*/
/* THE PROCESS                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Take a change one step on: a timeout, a switch, or a new ask. */
static void
ctl_step(side_t *s, uint8_t i, const tiku_peer_status_t *st)
{
    tiku_clock_time_t now = tiku_clock_time();
    tiku_clock_time_t t = (tiku_clock_time_t)(now - s->at);
    int rc;

    switch (s->step) {
    case STEP_ASKED:
        if (t >= WAIT_TICKS) {
            ctl_fail(s, 0u);
        }
        break;
    case STEP_TURN:
        /* The answerer lets its answer, and the asker's receipt of it, go
         * at the old speed. */
        if (s->out_len != 0u || (s->asking == 0u && t < ctl_turn(st->speed))) {
            break;
        }
        rc = ctl_speed(i, s->to, TIKU_PEER_SPEED_SET);
        if (rc == 0) {
            s->at = now;
            if (s->asking != 0u) {
                ctl_put(s, TIKU_PEER_CTL_CHECK, s->id, 0u, s->to);
                s->step = STEP_CHECK;
            } else {
                s->step = STEP_CONFIRM;
            }
        } else if (rc < 0 || t >= WAIT_TICKS) {
            ctl_fail(s, 0u);
        }
        break;
    case STEP_CHECK:
        if (t >= WAIT_TICKS) {
            ctl_fail(s, 1u);
        }
        break;
    case STEP_CONFIRM:
        if (t >= (tiku_clock_time_t)(WAIT_TICKS + TIKU_CLOCK_SECOND)) {
            ctl_fail(s, 1u);
        }
        break;
    case STEP_BACK:
        if (ctl_speed(i, 0u, TIKU_PEER_SPEED_START) == 0 || t >= WAIT_TICKS) {
            s->step = STEP_IDLE;
            s->at = now;
        }
        break;
    default:
        /* Idle: ask again for the speed wanted, when the link has another. */
        if (s->want != 0u && s->want != st->speed && st->speed != 0u &&
            st->state == TIKU_PEER_READY && s->out_len == 0u &&
            t >= AGAIN_TICKS) {
            s->id++;
            s->asking = 1u;
            s->to = s->want;
            s->step = STEP_ASKED;
            s->at = now;
            ctl_put(s, TIKU_PEER_CTL_ASK, s->id, 0u, s->to);
        }
        break;
    }
}

/**
 * @brief Move what can move for peer @p i.
 * @return 0 when nothing waits, 1 when a change is under way or a message
 *         waits, 2 while the endpoint is not open, 3 while a speed is wanted:
 *         the link may fall back to its first speed and need asking again
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
    if (tiku_peer_status(i, &st) != TIKU_PEER_OK) {
        return 0u;
    }
    /* A new instance of the peer knows nothing of a change under way. */
    if (st.restarts != s->restarts) {
        s->restarts = st.restarts;
        s->out_len = 0u;
        s->tries = 0u;
        if (s->step != STEP_BACK) {
            s->step = STEP_IDLE;
        }
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
        if (m.length == TIKU_PEER_CTL_MSG && m.data[0] == TIKU_PEER_CTL_KIND) {
            ctl_take(s, i, m.data);
        }
    }
    ctl_step(s, i, &st);
    if (s->out_len != 0u) {
        rc = tiku_ipc_send(s->ep, s->out, s->out_len);
        if (rc != TIKU_IPC_AGAIN) {
            s->out_len = 0u;
        }
    }
    if (s->step != STEP_IDLE || s->out_len != 0u) {
        return 1u;
    }
    return (s->want != 0u && s->tries < TIKU_PEER_CTL_TRIES) ? 3u : 0u;
}

/** @brief Work every peer, then come back in a tick while a change is under
 *         way, in a second while an endpoint is not open, and after the wait
 *         between tries while a speed is wanted. */
static void
ctl_service(void)
{
    uint8_t i, w, soonest = 0u;
    static const tiku_clock_time_t after[4] = {
        0u, 1u, TIKU_CLOCK_SECOND, AGAIN_TICKS
    };

    for (i = 0; i < TIKU_PEER_CTL_PEERS; i++) {
        w = side_work(i);
        if (w != 0u && (soonest == 0u || after[w] < after[soonest])) {
            soonest = w;
        }
    }
    tiku_timer_stop(&retry_timer);
    if (soonest != 0u) {
        tiku_timer_set_event(&retry_timer, after[soonest]);
    }
}

TIKU_PROCESS_THREAD(tiku_peer_ctl_process, ev, data)
{
    (void)ev;
    (void)data;

    TIKU_PROCESS_BEGIN();
    for (;;) {
        ctl_service();
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* CALLS                                                                     */
/*---------------------------------------------------------------------------*/

int
tiku_peer_set_speed(uint8_t index, uint32_t bits)
{
    side_t *s;

    if (index >= TIKU_PEER_CTL_PEERS || side[index].present == 0u) {
        return TIKU_PEER_ERR_NAME;
    }
    if (ctl_speed(index, bits, TIKU_PEER_SPEED_CHECK) != 0) {
        return TIKU_PEER_ERR_DRIVER;
    }
    s = &side[index];
    s->want = bits;
    s->tries = 0u;
    s->at = (tiku_clock_time_t)(tiku_clock_time() - AGAIN_TICKS);
    tiku_process_poll(&tiku_peer_ctl_process);
    return TIKU_PEER_OK;
}

uint32_t
tiku_peer_speed_wanted(uint8_t index)
{
    tiku_peer_status_t st;

    if (index < TIKU_PEER_CTL_PEERS && side[index].want != 0u) {
        return side[index].want;
    }
    return (tiku_peer_status(index, &st) == TIKU_PEER_OK) ? st.speed : 0u;
}

uint16_t
tiku_peer_ctl_failures(uint8_t index)
{
    return (index < TIKU_PEER_CTL_PEERS) ? side[index].failures : 0u;
}

void
tiku_peer_ctl_start(void)
{
    uint8_t i, any = 0u;

    for (i = 0; i < tiku_peer_count && i < TIKU_PEER_CTL_PEERS; i++) {
        const tiku_peer_t *p = tiku_peer_get(i);
        size_t n = strlen(p->name);

        if (tiku_peer_service(i, "ctl") < 0 || n + 5u > sizeof(side[i].name)) {
            continue;
        }
        memcpy(side[i].name, p->name, n);
        memcpy(side[i].name + n, "/ctl", 5u);
        side[i].present = 1u;
        any = 1u;
    }
    if (any != 0u && !tiku_process_is_running(&tiku_peer_ctl_process)) {
        (void)tiku_process_register("Peer control", &tiku_peer_ctl_process);
        tiku_process_start(&tiku_peer_ctl_process, (tiku_event_data_t)0);
    }
}

#endif /* TIKU_PEER_ENABLE */
