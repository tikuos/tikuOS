/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_consent.h - the owner's yes before an agent reboots, sleeps,
 * powers down, retunes the clock or changes the memory layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_CONSENT_H_
#define TIKU_MCP_CONSENT_H_

#include <stddef.h>

#include <tinymcp.h>

/** What tiku_mcp_consent() returns when the action may go ahead. */
#define TIKU_MCP_CONSENT_GO 100

/** Seconds a question for consent stays open. */
#ifndef TIKU_MCP_CONSENT_TTL_S
#define TIKU_MCP_CONSENT_TTL_S 60u
#endif

/**
 * @brief What writing @p path would do, when that needs the owner's yes.
 * @return A phrase ("change the CPU clock"), or NULL when it needs none
 */
const char *tiku_mcp_consent_write(const char *path);

/**
 * @brief The same for a shell line, including a command that once, every
 *        or on would run later, and a write the shell's write makes.
 */
const char *tiku_mcp_consent_line(const char *line);

/**
 * @brief Ask the owner before @p action ("w <path> <value>" or "s <line>"),
 *        described by @p desc, for the client of @p stream.
 *
 * A client that can elicit gets an input_required form; any other gets a
 * token, whose write to /sys/mcp/consent grants the same call made again
 * within TIKU_MCP_CONSENT_TTL_S.  The journal records ask and answer.
 *
 * @return TIKU_MCP_CONSENT_GO to act now; else the tool's return code,
 *         TINYMCP_NEED_INPUT or TINYMCP_TOOL_ERROR with the reason in
 *         @p text
 */
int tiku_mcp_consent(tinymcp_t *stream, const char *action, const char *desc,
                     tinymcp_sink_t *text);

/** @brief /sys/mcp/consent: the open question's read, and the token's write. */
int tiku_mcp_consent_read(char *buf, size_t max);
int tiku_mcp_consent_grant(const char *buf, size_t len);

#endif /* TIKU_MCP_CONSENT_H_ */
