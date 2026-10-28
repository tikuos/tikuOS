/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mcp.h - "mcp" command: the tinymcp server's state
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MCP_H_
#define TIKU_SHELL_CMD_MCP_H_

#include <stdint.h>

/**
 * @brief "mcp" command handler: the server's era, counters, watches and
 *        TCP ports; "mcp listen|http <port>|off" opens or closes a port.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_mcp(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MCP_H_ */
