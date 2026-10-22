/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mcp.c - "mcp" command: one line per fact of the server's
 * state, as /sys/mcp reports it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_mcp.h"

#include <shell/tiku_shell.h>
#include <services/tinymcp/tiku_mcp.h>

void
tiku_shell_cmd_mcp(uint8_t argc, const char *argv[])
{
    tiku_mcp_status_t s;

    (void)argc;
    (void)argv;
    tiku_mcp_status(&s);
    SHELL_PRINTF("tinymcp %s, protocol %s\n", TINYMCP_VERSION, TINYMCP_PROTOCOL);
    SHELL_PRINTF("transport %s, channel 0x%02X, era %s\n", s.transport,
                 (unsigned)TIKU_MCP_CHANNEL, s.era);
    SHELL_PRINTF("requests %lu  errors %lu  dropped %lu  busy %lu\n",
                 (unsigned long)s.requests, (unsigned long)s.errors,
                 (unsigned long)s.dropped, (unsigned long)s.busy);
    SHELL_PRINTF("watches %u of %u\n", (unsigned)s.subs,
                 (unsigned)TIKU_MCP_WATCH_MAX);
}
