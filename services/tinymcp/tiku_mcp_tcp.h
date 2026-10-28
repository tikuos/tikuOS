/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tcp.h - the MCP server on TCP: newline-delimited on one port,
 * Streamable HTTP on another, and the key that gives the owner's authority.
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
#ifndef TIKU_MCP_HTTP_CONNS
#define TIKU_MCP_HTTP_CONNS 2u     /**< HTTP connections open at once */
#endif
/** Connections at once: one of lines, the rest HTTP. */
#define TIKU_MCP_TCP_SLOTS (1u + TIKU_MCP_HTTP_CONNS)

/**
 * @brief Listen for MCP on @p port, lines (@p http 0) or Streamable HTTP at
 *        /mcp (@p http 1), or stop with port 0; open connections stay.
 * @return 0, or -1 when this build has no TCP or the stack refused
 */
int tiku_mcp_tcp_listen(uint16_t port, int http);

/** @brief Connection @p i's stream, or NULL while it is closed. */
tinymcp_t *tiku_mcp_tcp_stream(unsigned i);

/** @brief 1 when @p stream is one of the TCP connections'. */
int tiku_mcp_tcp_owns(const tinymcp_t *stream);

/**
 * @brief 1 when the request being served on @p stream carries the key the
 *        owner set, as its HTTP bearer token or in _meta; 0 otherwise.
 */
int tiku_mcp_tcp_keyed(tinymcp_t *stream);

/** @brief A TCP stream's reply, whole or in pieces. */
int tiku_mcp_tcp_send(tinymcp_t *stream, const char *msg, size_t len);
int tiku_mcp_tcp_send_part(tinymcp_t *stream, const char *bytes, size_t len,
                           int last);

/** @brief From the shell's pump: the connections' requests. */
void tiku_mcp_tcp_pump(void);

/** @brief The key cell, once at boot. */
void tiku_mcp_tcp_init(void);

/** @brief /sys/mcp/key and /sys/mcp/tcp handlers. */
int tiku_mcp_key_read(char *buf, size_t max);
int tiku_mcp_key_write(const char *buf, size_t len);
int tiku_mcp_tcp_read(char *buf, size_t max);

#endif /* TIKU_MCP_TCP_H_ */
