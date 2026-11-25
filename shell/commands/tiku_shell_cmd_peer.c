/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_peer.c - "peer" command implementation.
 *
 * send, listen and ping open the named endpoint for the call, run the
 * scheduler while they wait, and close it again.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_peer.h"
#include <shell/tiku_shell.h>
#include <interfaces/peer/tiku_peer.h>
#include <services/peer/tiku_peer_ctl.h>
#include <kernel/ipc/tiku_ipc.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_hang.h>
#include <string.h>

#if TIKU_SHELL_CMD_PEER

/** @brief One line for the peer at @p index, then one for its services. */
static void
peer_show(uint8_t index)
{
    static const char *const state[] = { "off", "starting", "ready", "lost" };
    static const char *const why[] = { "", " (timeout)", " (fault)",
                                       " (hang)", " (carrier)" };
    const tiku_peer_t *p = tiku_peer_get(index);
    tiku_peer_status_t s;
    uint8_t i;

    if (tiku_peer_status(index, &s) != TIKU_PEER_OK) {
        return;
    }
    SHELL_PRINTF("%-6s %s, %s, %s%s, restarts %u\n", p->name,
                 (p->flags & TIKU_PEER_F_SUPERVISOR) ? "supervised by this side"
                                                     : "supervises this side",
                 p->carrier, state[s.state & 3u],
                 (s.state == TIKU_PEER_LOST && s.why < 5u) ? why[s.why] : "",
                 (unsigned)s.restarts);
    if (s.speed != 0u) {
        uint32_t want = tiku_peer_speed_wanted(index);

        SHELL_PRINTF("       speed: %lu bits a second", (unsigned long)s.speed);
        if (want != s.speed) {
            SHELL_PRINTF(", %lu asked", (unsigned long)want);
        }
        SHELL_PRINTF("\n");
    }
    SHELL_PRINTF("       services:");
    for (i = 0; i < p->service_count; i++) {
        SHELL_PRINTF(" %s/%s", p->name, p->services[i].name);
    }
    SHELL_PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
/* MESSAGES                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Seconds ping waits for one answer. */
#define PEER_PING_WAIT_S  3u

/** @brief Let the other processes and IPC run once, without this one. */
static void
peer_service(void)
{
    (void)tiku_sched_run_once_except(TIKU_THIS());
    tiku_hang_checkin();
}

/** @brief Parse a decimal number from 1 to @p max; 0 if malformed. */
static uint32_t
peer_number(const char *s, uint32_t max)
{
    uint32_t v = 0;

    if (*s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > max) {
            return 0;
        }
        s++;
    }
    return v;
}

/**
 * @brief Open @p name, printing why not on failure.  An endpoint closed a
 *        moment ago is busy until IPC has cleaned it up: that is waited out.
 * @return 0 or -1
 */
static int
peer_open(const char *name, tiku_ipc_endpoint_t *ep)
{
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_ipc_err_t rc;

    while ((rc = tiku_ipc_open(name, ep)) == TIKU_IPC_BUSY &&
           (tiku_clock_time_t)(tiku_clock_time() - t0) < TIKU_CLOCK_SECOND) {
        peer_service();
    }
    if (rc != TIKU_IPC_OK) {
        SHELL_PRINTF("peer: cannot open '%s' (%d)\n", name, (int)rc);
        return -1;
    }
    return 0;
}

/**
 * @brief Send @p len bytes, retrying while the endpoint says again, for at
 *        most @p ticks.  @return 0 once accepted, else -1
 */
static int
peer_put(tiku_ipc_endpoint_t ep, const void *data, uint16_t len,
         tiku_clock_time_t ticks)
{
    tiku_clock_time_t t0 = tiku_clock_time();

    for (;;) {
        tiku_ipc_err_t rc = tiku_ipc_send(ep, data, len);

        if (rc == TIKU_IPC_OK) {
            return 0;
        }
        if (rc != TIKU_IPC_AGAIN ||
            (tiku_clock_time_t)(tiku_clock_time() - t0) > ticks) {
            return -1;
        }
        peer_service();
    }
}

/** @brief The words argv[first..] joined by spaces into @p out; its length. */
static uint16_t
peer_join(uint8_t argc, const char *argv[], uint8_t first, char *out,
          uint16_t cap)
{
    uint16_t n = 0;
    uint8_t i;

    for (i = first; i < argc; i++) {
        const char *w = argv[i];

        if (i > first && n < cap) {
            out[n++] = ' ';
        }
        while (*w != '\0' && n < cap) {
            out[n++] = *w++;
        }
    }
    return n;
}

/** @brief Send one message of the words given. */
static void
peer_send(uint8_t argc, const char *argv[])
{
    tiku_ipc_endpoint_t ep = {0};
    char text[TIKU_IPC_MESSAGE_MAX];
    uint16_t n = peer_join(argc, argv, 3, text, (uint16_t)sizeof(text));

    if (n == 0u || peer_open(argv[2], &ep) != 0) {
        return;
    }
    if (peer_put(ep, text, n, TIKU_CLOCK_SECOND) == 0) {
        SHELL_PRINTF("sent %u bytes\n", (unsigned)n);
    } else {
        SHELL_PRINTF("peer: not accepted (is the peer ready?)\n");
    }
    (void)tiku_ipc_close(ep);
}

/** @brief Print each message that arrives within @p seconds. */
static void
peer_listen(const char *name, uint32_t seconds)
{
    tiku_ipc_endpoint_t ep = {0};
    tiku_ipc_received_t m;
    tiku_clock_time_t t0;
    uint32_t count = 0;
    uint16_t i;

    if (peer_open(name, &ep) != 0) {
        return;
    }
    t0 = tiku_clock_time();
    while ((tiku_clock_time_t)(tiku_clock_time() - t0) <
           (tiku_clock_time_t)(seconds * TIKU_CLOCK_SECOND)) {
        peer_service();
        if (tiku_ipc_receive(ep, &m) != TIKU_IPC_OK) {
            continue;
        }
        if (m.is_buffer) {
            (void)tiku_ipc_ack(ep, m.submission, TIKU_IPC_UNSUPPORTED);
            continue;
        }
        count++;
        SHELL_PRINTF("got %u: ", (unsigned)m.length);
        for (i = 0; i < m.length; i++) {
            SHELL_PRINTF("%c", (m.data[i] >= 0x20u && m.data[i] < 0x7Fu)
                               ? m.data[i] : '.');
        }
        SHELL_PRINTF("\n");
    }
    (void)tiku_ipc_close(ep);
    SHELL_PRINTF("listen: %lu messages\n", (unsigned long)count);
}

/**
 * @brief Send @p count numbered messages and wait for each to come back
 *        unchanged; the other side echoes (its `ipc echo`).
 */
static void
peer_ping(const char *name, uint32_t count)
{
    tiku_ipc_endpoint_t ep = {0};
    tiku_ipc_received_t m;
    tiku_clock_time_t start = tiku_clock_time();
    uint32_t i, ok = 0, lost = 0, wrong = 0;
    uint8_t msg[32];

    if (peer_open(name, &ep) != 0) {
        return;
    }
    for (i = 0; i < count; i++) {
        tiku_clock_time_t t0;
        uint8_t k, answered = 0;

        for (k = 0; k < sizeof(msg); k++) {
            msg[k] = (uint8_t)(i * 7u + k * 13u + 1u);
        }
        msg[0] = (uint8_t)i;
        msg[1] = (uint8_t)(i >> 8);
        if (peer_put(ep, msg, (uint16_t)sizeof(msg),
                     PEER_PING_WAIT_S * TIKU_CLOCK_SECOND) != 0) {
            lost++;
            continue;
        }
        t0 = tiku_clock_time();
        while (answered == 0u &&
               (tiku_clock_time_t)(tiku_clock_time() - t0) <
               (tiku_clock_time_t)(PEER_PING_WAIT_S * TIKU_CLOCK_SECOND)) {
            peer_service();
            if (tiku_ipc_receive(ep, &m) != TIKU_IPC_OK) {
                continue;
            }
            if (m.is_buffer) {
                (void)tiku_ipc_ack(ep, m.submission, TIKU_IPC_UNSUPPORTED);
                continue;
            }
            answered = 1u;
            if (m.length == sizeof(msg) && memcmp(m.data, msg, sizeof(msg)) == 0) {
                ok++;
            } else {
                wrong++;
            }
        }
        if (answered == 0u) {
            lost++;
        }
    }
    (void)tiku_ipc_close(ep);
    SHELL_PRINTF("ping: %lu sent, %lu back intact, %lu wrong, %lu lost, "
                 "%lu ticks\n", (unsigned long)count, (unsigned long)ok,
                 (unsigned long)wrong, (unsigned long)lost,
                 (unsigned long)(tiku_clock_time_t)(tiku_clock_time() - start));
}

/** @brief Seconds `peer speed` waits for the link to reach the speed. */
#define PEER_SPEED_WAIT_S  8u

/** @brief Ask for the link speed of the peer named @p name, then wait for
 *         the link to reach it or the change to fail. */
static void
peer_speed(const char *name, const char *bits_text)
{
    int index = tiku_peer_find(name);
    uint32_t bits = peer_number(bits_text, 100000000UL);
    tiku_peer_status_t s;
    tiku_clock_time_t t0;
    uint16_t failed;

    if (index < 0) {
        SHELL_PRINTF("peer: no peer '%s'\n", name);
        return;
    }
    if (bits == 0u) {
        SHELL_PRINTF("peer: give the speed in bits a second\n");
        return;
    }
    failed = tiku_peer_ctl_failures((uint8_t)index);
    if (tiku_peer_set_speed((uint8_t)index, bits) != TIKU_PEER_OK) {
        SHELL_PRINTF("peer: no setting for %lu here\n", (unsigned long)bits);
        return;
    }
    t0 = tiku_clock_time();
    while (tiku_peer_status((uint8_t)index, &s) == TIKU_PEER_OK &&
           (s.speed != bits || s.state != TIKU_PEER_READY) &&
           tiku_peer_ctl_failures((uint8_t)index) == failed &&
           (tiku_clock_time_t)(tiku_clock_time() - t0) <
           (tiku_clock_time_t)(PEER_SPEED_WAIT_S * TIKU_CLOCK_SECOND)) {
        peer_service();
    }
    if (tiku_peer_ctl_failures((uint8_t)index) != failed) {
        SHELL_PRINTF("peer: the change did not go through\n");
    }
    peer_show((uint8_t)index);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_peer(uint8_t argc, const char *argv[])
{
    int index, rc;

    if (argc < 2) {
        uint8_t i;

        for (i = 0; i < tiku_peer_count; i++) {
            peer_show(i);
        }
        return;
    }
    if (argc >= 4 && strcmp(argv[1], "send") == 0) {
        peer_send(argc, argv);
        return;
    }
    if (argc == 4 && (strcmp(argv[1], "listen") == 0 ||
                      strcmp(argv[1], "ping") == 0)) {
        uint32_t n = peer_number(argv[3], 100000UL);

        if (n == 0u) {
            SHELL_PRINTF("peer: give a number from 1 to 100000\n");
        } else if (argv[1][0] == 'l') {
            peer_listen(argv[2], n);
        } else {
            peer_ping(argv[2], n);
        }
        return;
    }
    if (argc == 4 && strcmp(argv[1], "speed") == 0) {
        peer_speed(argv[2], argv[3]);
        return;
    }
    if (argc != 3) {
        SHELL_PRINTF("Usage: peer [start|stop|reset <name> | "
                     "speed <name> <bits> | "
                     "send <endpoint> <word>... | listen <endpoint> <seconds> "
                     "| ping <endpoint> <count>]\n");
        return;
    }
    index = tiku_peer_find(argv[2]);
    if (index < 0) {
        SHELL_PRINTF("peer: no peer '%s'\n", argv[2]);
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        rc = tiku_peer_start((uint8_t)index);
    } else if (strcmp(argv[1], "stop") == 0) {
        rc = tiku_peer_stop((uint8_t)index);
    } else if (strcmp(argv[1], "reset") == 0) {
        rc = tiku_peer_reset((uint8_t)index);
    } else {
        SHELL_PRINTF("peer: unknown '%s'\n", argv[1]);
        return;
    }
    if (rc == TIKU_PEER_OK) {
        peer_show((uint8_t)index);
    } else {
        SHELL_PRINTF("peer: %s (%d)\n",
                     rc == TIKU_PEER_ERR_ROLE  ? "this side has no such control" :
                     rc == TIKU_PEER_ERR_STATE ? "not possible for this peer" :
                                                 "its driver refused", rc);
    }
}

#endif /* TIKU_SHELL_CMD_PEER */
