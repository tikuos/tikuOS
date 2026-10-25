/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc.c - two-buffer shared-stream and control conformance probe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_cmd_ipc.h"
#include <shell/tiku_shell.h>
#include <kernel/ipc/tiku_ipc.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/cpu/tiku_hang.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <hal/tiku_ipc_shared.h>
#if (TIKU_HAS_COPROC + 0)
#include <interfaces/coproc/tiku_coproc.h>
#endif
#include <string.h>
#if (TIKU_HAS_IPC_UART + 0)
#include "tiku_shell_cmd_ipc_uart.h"
#endif
#if (TIKU_HAS_SDIO_C6 + 0)
#include "tiku_shell_cmd_ipc_sdio.h"
#endif

static void service(struct tiku_process *owner)
{
    (void)tiku_sched_run_once_except(owner);
    tiku_watchdog_kick();
    tiku_hang_checkin();
}

/** @brief Establish peer service and discard the diagnostic warmup echo. */
static int warmup(tiku_ipc_endpoint_t endpoint, struct tiku_process *owner)
{
    tiku_clock_time_t started = tiku_clock_time();
    uint8_t sent = 0;
    while ((tiku_clock_time_t)(tiku_clock_time() - started) < 3u * TIKU_CLOCK_SECOND) {
        tiku_ipc_received_t message;
        service(owner);
        if (!sent && tiku_ipc_send(endpoint, "ready", 5) == TIKU_IPC_OK) sent = 1;
        if (tiku_ipc_receive(endpoint, &message) == TIKU_IPC_OK)
            return sent && message.length == 5 && !memcmp(message.data, "ready", 5);
    }
    return 0;
}

static void stream_test(const char *name)
{
    tiku_ipc_endpoint_t endpoint = {0};
    tiku_ipc_buffer_t buffers[2] = {{0}};
    tiku_ipc_status_t status;
    struct tiku_process *owner = TIKU_THIS();
    uint32_t produced = 0, expected, initial_bytes, chunks = 0, controls = 0;
    uint32_t failures = 0, max_control = 0;
    uint32_t heartbeat_max=0,heartbeat_changes=0;
#if (TIKU_HAS_COPROC + 0)
    uint32_t heartbeat=0;
    tiku_clock_time_t heartbeat_at=0;
#endif
    tiku_clock_time_t started, last_control = 0;
    uint8_t control_pending = 0;
    unsigned i, n;
    const uint32_t total = 600u * 1024u;
    if (tiku_ipc_open(name, &endpoint) != TIKU_IPC_OK) {
        SHELL_PRINTF("IPC FAIL endpoint unavailable\n"); return;
    }
    if (tiku_ipc_query(endpoint, &status) != TIKU_IPC_OK || !status.transport_stats ||
        status.max_buffer < 512 || status.max_message < 7) {
        SHELL_PRINTF("IPC FAIL endpoint has no shared-stream diagnostic\n");
        failures++; goto cleanup;
    }
#if (TIKU_HAS_COPROC + 0)
    if (tiku_coproc_start() != TIKU_COPROC_OK) { failures++; goto cleanup; }
#endif
    if (!warmup(endpoint, owner)) { failures++; goto cleanup; }
    (void)tiku_ipc_query(endpoint, &status);
    initial_bytes = status.transport_bytes;
    expected = ~status.transport_crc;
    SHELL_PRINTF("IPC profile: bytes=%lu window=2 block=512 control_bound=128ticks\n",
                 (unsigned long)total);
    for (i = 0; i < 2; i++) {
        if (tiku_ipc_buffer_alloc(512, &buffers[i]) != TIKU_IPC_OK) {
            failures++; goto cleanup;
        }
    }
    started = tiku_clock_time();
#if (TIKU_HAS_COPROC + 0)
    heartbeat=tiku_coproc_heartbeat();heartbeat_at=started;
#endif
    while (produced < total) {
        unsigned complete = 0;
        tiku_clock_time_t wait = tiku_clock_time();
        for (i = 0; i < 2; i++) {
            void *data;
            uint16_t length;
            tiku_ipc_submission_t ticket;
            if (tiku_ipc_buffer_map(buffers[i], &data, &length) != TIKU_IPC_OK) {
                failures++; goto cleanup;
            }
            for (n = 0; n < length; n++)
                ((uint8_t *)data)[n] = (uint8_t)(((produced + n) * 17u) ^ ((produced + n) >> 8));
            expected = tiku_ipc_shared_crc(expected, data, length);
            if (tiku_ipc_submit(endpoint, buffers[i], TIKU_IPC_INPUT,
                    5u * TIKU_CLOCK_SECOND, &ticket) != TIKU_IPC_OK) {
                failures++; goto cleanup;
            }
            produced += length;
        }
        while (complete < 2) {
            tiku_ipc_completion_t completion;
            tiku_ipc_received_t message;
            service(owner);
#if (TIKU_HAS_COPROC + 0)
            {
                uint32_t current=tiku_coproc_heartbeat();
                if(current!=heartbeat){
                    uint32_t elapsed=(tiku_clock_time_t)(tiku_clock_time()-heartbeat_at);
                    if(elapsed>heartbeat_max)heartbeat_max=elapsed;
                    heartbeat=current;heartbeat_at=tiku_clock_time();heartbeat_changes++;
                }
            }
#endif
            if (!control_pending && tiku_ipc_send(endpoint, "control", 7) == TIKU_IPC_OK) {
                last_control = tiku_clock_time(); control_pending = 1;
            }
            while (tiku_ipc_receive(endpoint, &message) == TIKU_IPC_OK) {
                uint32_t elapsed = (tiku_clock_time_t)(tiku_clock_time() - last_control);
                if (!control_pending || message.length != 7 || memcmp(message.data, "control", 7)) failures++;
                if (elapsed > max_control) max_control = elapsed;
                if (elapsed > 128) failures++;
                controls++; control_pending = 0;
            }
            while (tiku_ipc_completion(endpoint, &completion) == TIKU_IPC_OK) {
                if (completion.status != TIKU_IPC_OK || completion.quarantined) failures++;
                complete++; chunks++;
            }
            if ((tiku_clock_time_t)(tiku_clock_time() - wait) > 10u * TIKU_CLOCK_SECOND) {
                failures++; goto cleanup;
            }
        }
    }
    (void)tiku_ipc_query(endpoint, &status);
    if ((uint32_t)(status.transport_bytes - initial_bytes) != total ||
        status.transport_crc != ~expected || !controls) failures++;
    SHELL_PRINTF("IPC result: bytes=%lu peer_bytes=%lu crc=%08lx peer_crc=%08lx chunks=%lu controls=%lu max_control_ticks=%lu ticks=%lu failures=%lu\n",
        (unsigned long)produced, (unsigned long)(uint32_t)(status.transport_bytes - initial_bytes),
        (unsigned long)~expected, (unsigned long)status.transport_crc,
        (unsigned long)chunks, (unsigned long)controls, (unsigned long)max_control,
        (unsigned long)(tiku_clock_time() - started), (unsigned long)failures);
    SHELL_PRINTF("IPC heartbeat: changes=%lu max_ticks=%lu\n",(unsigned long)heartbeat_changes,
        (unsigned long)heartbeat_max);
cleanup:
    for (i = 0; i < 2; i++)
        if (buffers[i].slot_plus_one) (void)tiku_ipc_buffer_release(buffers[i]);
    (void)tiku_ipc_close(endpoint);
    for (i = 0; i < 20; i++) service(owner);
    if (tiku_ipc_status((uint16_t)(endpoint.slot_plus_one - 1u), &status) == TIKU_IPC_OK)
        SHELL_PRINTF("IPC backing: live=%u free=%u quarantined=%u failures=%lu\n",
            status.buffers_live, status.buffers_free, status.buffers_quarantined,
            (unsigned long)failures);
}

/** @brief Echo copied messages for a bounded diagnostic interval. */
static void echo_test(const char *name, const char *duration)
{
    tiku_ipc_endpoint_t endpoint = {0};
    tiku_ipc_received_t message;
    struct tiku_process *owner = TIKU_THIS();
    tiku_clock_time_t started;
    uint32_t ticks = 0, echoes = 0, failures = 0;
    uint8_t pending = 0;
    for (const char *p = duration; *p; p++) {
        if (*p < '0' || *p > '9' || ticks > (UINT32_MAX-9u)/10u) return;
        ticks = ticks*10u+(uint32_t)(*p-'0');
    }
    if (!ticks || ticks > TIKU_CLOCK_MAX_INTERVAL ||
        tiku_ipc_open(name,&endpoint) != TIKU_IPC_OK) {
        SHELL_PRINTF("IPC FAIL echo arguments or endpoint\n"); return;
    }
    SHELL_PRINTF("IPC echo ready: %s ticks=%lu\n",name,(unsigned long)ticks);
    started = tiku_clock_time();
    while ((tiku_clock_time_t)(tiku_clock_time()-started) < ticks) {
        tiku_ipc_err_t result;
        service(owner);
        if (!pending && tiku_ipc_receive(endpoint,&message) == TIKU_IPC_OK) {
            if (message.is_buffer) {
                (void)tiku_ipc_ack(endpoint,message.submission,TIKU_IPC_UNSUPPORTED);
                failures++;
            } else pending = 1;
        }
        if (!pending) continue;
        result = tiku_ipc_send(endpoint,message.data,message.length);
        if (result == TIKU_IPC_OK) { pending = 0; echoes++; }
        else if (result != TIKU_IPC_AGAIN) { failures++; break; }
    }
    (void)tiku_ipc_close(endpoint);
    SHELL_PRINTF("IPC echo: messages=%lu failures=%lu\n",
                 (unsigned long)echoes,(unsigned long)failures);
}

void tiku_shell_cmd_ipc(uint8_t argc, const char *argv[])
{
    uint16_t e;
#if (TIKU_HAS_IPC_UART + 0)
    if(argc==2&&!strcmp(argv[1],"uart")){tiku_shell_ipc_uart();return;}
#endif
#if (TIKU_HAS_SDIO_C6 + 0)
    if (argc >= 2 && !strcmp(argv[1],"sdio")) { tiku_shell_ipc_sdio(argc,argv); return; }
#endif
    if (argc == 4 && !strcmp(argv[1],"echo")) { echo_test(argv[2],argv[3]); return; }
    if (argc == 3 && !strcmp(argv[1], "test")) { stream_test(argv[2]); return; }
    if (argc != 1) {
        SHELL_PRINTF("ipc [test <endpoint> | echo <endpoint> <ticks>]\n"); return;
    }
    for (e = 0; e < tiku_ipc_endpoint_count(); e++) {
        tiku_ipc_status_t s;
        (void)tiku_ipc_status(e, &s);
        SHELL_PRINTF("%s %s queued=%u credits=%u methods=%u\n",
            tiku_ipc_endpoint_name(e), s.open ? "open" : "closed",
            s.queued, s.credits, s.methods);
    }
}
