/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tcp.h - the MCP server on a TCP port, one connection at a time,
 * and the key that gives a connection the owner's authority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_TCP_H_
#define TIKU_MCP_TCP_H_

#include <stddef.h>
#include <stdint.h>

#include <tinymcp.h>

#define TIKU_MCP_KEY_MIN 8u        /**< the shortest key the owner may set */
#define TIKU_MCP_KEY_MAX 32u       /**< the longest, without its terminator */
#define TIKU_MCP_KEY_META "org.tinymcp/key"  /**< the request's _meta member */

#ifndef TIKU_MCP_TCP_SEND_MS
#define TIKU_MCP_TCP_SEND_MS 3000u /**< a reply's wait for a free TX segment */
#endif

/**
 * @brief Listen for MCP on @p port, or stop with 0; an open connection
 *        stays until it closes.
 * @return 0, or -1 when this build has no TCP or the stack refused
 */
int tiku_mcp_tcp_listen(uint16_t port);

/** @brief The TCP connection's stream, or NULL while none is open. */
tinymcp_t *tiku_mcp_tcp_stream(void);

/**
 * @brief 1 when the request being served on @p stream carries the key the
 *        owner set; 0 for none, a wrong one, or no key set.
 */
int tiku_mcp_tcp_keyed(tinymcp_t *stream);

/** @brief The TCP stream's reply, whole or in pieces, newline-ended. */
int tiku_mcp_tcp_send(const char *msg, size_t len);
int tiku_mcp_tcp_send_part(const char *bytes, size_t len, int last);

/** @brief From the shell's pump: the connection's requests. */
void tiku_mcp_tcp_pump(void);

/** @brief The key cell, once at boot. */
void tiku_mcp_tcp_init(void);

/** @brief /sys/mcp/key and /sys/mcp/tcp handlers. */
int tiku_mcp_key_read(char *buf, size_t max);
int tiku_mcp_key_write(const char *buf, size_t len);
int tiku_mcp_tcp_read(char *buf, size_t max);

/** @brief Connections taken and refused since boot. */
uint32_t tiku_mcp_tcp_accepted(void);
uint32_t tiku_mcp_tcp_refused(void);

#endif /* TIKU_MCP_TCP_H_ */
