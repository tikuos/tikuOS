/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_154_server.c - C6 owner process for IEEE 802.15.4 mailbox requests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_154.h"
#include <kernel/timers/tiku_timer.h>
TIKU_PROCESS(radio154_server, "C6 802.15.4 service");
static tiku_ipc_wire_t *wire;
static tiku_ipc_endpoint_t endpoint;
static tiku_154_server_t state;
static struct tiku_timer timer;
static unsigned pending;
int tiku_154_service_local_allowed(void)
{
    return TIKU_THIS() == &radio154_server || !state.claim;
}
TIKU_PROCESS_THREAD(radio154_server, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/154", &endpoint)) TIKU_PROCESS_EXIT();
    for (;;) {
        if (!wire->ready || state.session != wire->local_session) pending = 0;
        tiku_154_server_tick(&state, wire->local_session, tiku_clock_time(), wire->ready);
        if (!state.reply_n) pending = 0;
        if (wire->ready && !pending) {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(endpoint, &m) == TIKU_IPC_OK) {
                if (!m.is_buffer)
                    pending = tiku_154_server_request(&state, m.data, m.length, tiku_clock_time());
                else (void)tiku_ipc_ack(endpoint, m.submission, TIKU_IPC_UNSUPPORTED);
            }
        }
        if (pending && tiku_ipc_send(endpoint, state.reply, pending) == TIKU_IPC_OK) pending = 0;
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_154_service_start(tiku_ipc_wire_t *w)
{
    wire = w;
    if (!radio154_server.is_running) tiku_process_start(&radio154_server, NULL);
}
