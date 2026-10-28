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
#include <services/tinymcp/tiku_mcp_tcp.h>

#include <stdlib.h>
#include <string.h>

/**
 * @brief "mcp listen|http <port>|off": MCP as lines, or Streamable HTTP, on
 *        a TCP port, or not.
 */
static void
listen_cmd(const char *what, const char *arg)
{
    long p = strcmp(arg, "off") == 0 ? 0L : strtol(arg, (char **)0, 10);
    char line[160];

    if (p < 0L || p > 65535L || (p == 0L && strcmp(arg, "off") != 0)) {
        SHELL_PRINTF("usage: mcp %s <port>|off\n", what);
        return;
    }
    if (tiku_mcp_tcp_listen((uint16_t)p, strcmp(what, "http") == 0) != 0) {
        SHELL_PRINTF("mcp: no TCP listener on %ld\n", p);
        return;
    }
    (void)tiku_mcp_tcp_read(line, sizeof line);
    SHELL_PRINTF("%s", line);
}

void
tiku_shell_cmd_mcp(uint8_t argc, const char *argv[])
{
    tiku_mcp_status_t s;
    char line[160];

    if (argc >= 3u && (strcmp(argv[1], "listen") == 0 ||
                       strcmp(argv[1], "http") == 0)) {
        listen_cmd(argv[1], argv[2]);
        return;
    }
    tiku_mcp_status(&s);
    SHELL_PRINTF("tinymcp %s, protocol %s\n", TINYMCP_VERSION, TINYMCP_PROTOCOL);
    SHELL_PRINTF("transport %s, channel 0x%02X, era %s\n", s.transport,
                 (unsigned)TIKU_MCP_CHANNEL, s.era);
    SHELL_PRINTF("requests %lu  errors %lu  dropped %lu  busy %lu\n",
                 (unsigned long)s.requests, (unsigned long)s.errors,
                 (unsigned long)s.dropped, (unsigned long)s.busy);
    SHELL_PRINTF("watches %u of %u\n", (unsigned)s.subs,
                 (unsigned)TIKU_MCP_WATCH_MAX);
    (void)tiku_mcp_tcp_read(line, sizeof line);
    SHELL_PRINTF("%s", line);
}
