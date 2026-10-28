/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_console.c - the console transport: one stream on channel 0xF3,
 * requests queued from the decoder and run from the shell's pump.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp.h"
#include "tiku_mcp_job.h"

#include <string.h>

#include <kernel/cpu/tiku_watchdog.h>
#include <services/console/tiku_console.h>
#include <shell/tiku_shell.h>

/* The decoder hands a frame over inside tiku_console_getc(), from the line
 * editor or from a running command, so a request is copied into a queue
 * slot there and handled from the pump, where rules and jobs run.  Two
 * slots take a notification pipelined behind a request; a third request
 * is refused with "busy". */
#define QUEUE 2u

static tinymcp_t stream;
static char      out_buf[TIKU_MCP_OUT_CAP];
static uint8_t   chan_buf[TIKU_MCP_IN_CAP];
static char      queue[QUEUE][TIKU_MCP_IN_CAP];
static size_t    queue_len[QUEUE];
static uint8_t   queue_head, queue_count;
static uint32_t  busy_count;
static uint8_t   in_frame;       /* a reply's frame is open on the wire */

/** @brief Refuse a request that found the queue full, by its id. */
static void
busy(const uint8_t *buf, size_t len)
{
    tinymcp_jv_t root, id;
    char reply[96];
    tinymcp_jw_t w;

    if (!tinymcp_json_value((const char *)buf, len, &root) ||
        root.type != TINYMCP_J_OBJECT ||
        tinymcp_json_get(&root, "id", &id) != 1 || id.len > 40u) {
        return;
    }
    tinymcp_jw_init(&w, reply, sizeof reply);
    tinymcp_jw_raw(&w, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id.type == TINYMCP_J_STRING) {
        tinymcp_jw_raw(&w, "\"");
        tinymcp_jw_rawn(&w, id.at, id.len);
        tinymcp_jw_raw(&w, "\"");
    } else {
        tinymcp_jw_rawn(&w, id.at, id.len);
    }
    tinymcp_jw_raw(&w, ",\"error\":{\"code\":-32603,\"message\":\"busy\"}}");
    /* Inside an open frame it would split the reply: it is dropped. */
    if (!w.full && !in_frame) {
        (void)tiku_mcp_console_send(reply, w.len);
    }
    busy_count++;
}

/** @brief A frame on the channel: queued, or refused. */
static void
on_frame(void *ctx, uint8_t *buf, size_t len)
{
    uint8_t slot;

    (void)ctx;
    if (queue_count == QUEUE) {
        busy(buf, len);
        return;
    }
    slot = (uint8_t)((queue_head + queue_count) % QUEUE);
    memcpy(queue[slot], buf, len);
    queue_len[slot] = len;
    queue_count++;
}

void
tiku_mcp_console_pump(void)
{
    while (queue_count > 0u) {
        uint8_t slot = queue_head;

        queue_head = (uint8_t)((queue_head + 1u) % QUEUE);
        queue_count--;
        tiku_mcp_identity();
        tinymcp_handle(&stream, queue[slot], queue_len[slot]);
    }
    tiku_mcp_job_tick();
}

int
tiku_mcp_console_send(const char *msg, size_t len)
{
    if (tiku_console_frame_begin((uint8_t)TIKU_MCP_CHANNEL) != 0) {
        return -1;
    }
    tiku_console_frame_put(msg, len);
    tiku_console_frame_end();
    tiku_console_flush();
    return 0;
}

/* A reply in pieces: the frame opens at the first and closes at the last;
 * the wire is slow beside the hang detector, so each piece kicks it. */
int
tiku_mcp_console_send_part(const char *bytes, size_t len, int last)
{
    tiku_watchdog_kick();
    if (!in_frame) {
        if (tiku_console_frame_begin((uint8_t)TIKU_MCP_CHANNEL) != 0) {
            return -1;
        }
        in_frame = 1u;
    }
    tiku_console_frame_put(bytes, len);
    if (last) {
        tiku_console_frame_end();
        tiku_console_flush();
        in_frame = 0u;
    }
    return 0;
}

tinymcp_t *
tiku_mcp_console_stream(void)
{
    return &stream;
}

uint32_t
tiku_mcp_console_busy(void)
{
    return busy_count;
}

void
tiku_mcp_console_init(void)
{
    tinymcp_init(&stream, &tiku_mcp_host, &stream, (char *)0, 0u,
                 out_buf, sizeof out_buf, TIKU_MCP_SUBS);
    /* One byte short of a slot, so a queued frame has room for its
     * terminator. */
    (void)tiku_console_add_channel((uint8_t)TIKU_MCP_CHANNEL, 0xFFu, 0u,
                                   on_frame, (void *)0, chan_buf,
                                   sizeof chan_buf - 1u);
    (void)tiku_shell_add_pump(tiku_mcp_console_pump);
}
