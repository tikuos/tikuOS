/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ask.c - "ask" command: the words after it, joined by
 * spaces, become /sys/mcp/ask/question; a rule's action can be one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_ask.h"

#include <string.h>

#include <shell/tiku_shell.h>
#include <services/tinymcp/tiku_mcp.h>
#include <services/tinymcp/tiku_mcp_ask.h>

void
tiku_shell_cmd_ask(uint8_t argc, const char *argv[])
{
    static const char *const states[] = { "idle", "asked", "answered" };
    char q[TIKU_MCP_ASK_MAX];
    size_t n = 0u;
    uint8_t i;

    if (argc < 2u) {
        char buf[TIKU_MCP_ASK_MAX + 2u];

        SHELL_PRINTF("#%lu %s\n", (unsigned long)tiku_mcp_ask_seq(),
                     states[tiku_mcp_ask_state()]);
        if (tiku_vfs_read("/sys/mcp/ask/question", buf, sizeof buf) > 0) {
            SHELL_PRINTF("question: %s", buf);
        }
        SHELL_PRINTF("answer:   %s\n", tiku_mcp_ask_answer());
        return;
    }
    for (i = 1u; i < argc; i++) {
        size_t len = strlen(argv[i]);

        if (n + len + 1u >= sizeof q) {
            len = sizeof q - n - 1u;
        }
        if (n > 0u && n + 1u < sizeof q) {
            q[n++] = ' ';
        }
        memcpy(q + n, argv[i], len);
        n += len;
        if (n + 1u >= sizeof q) {
            break;
        }
    }
    q[n] = 0;
    if (tiku_mcp_ask(q, n) != 0) {
        SHELL_PRINTF("ask: the question was refused\n");
        return;
    }
    SHELL_PRINTF("asked #%lu\n", (unsigned long)tiku_mcp_ask_seq());
}
