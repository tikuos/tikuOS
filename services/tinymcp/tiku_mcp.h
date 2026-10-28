/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp.h - the board as a Model Context Protocol server: the tinymcp
 * library bound to the namespace and the shell, over the console channel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_H_
#define TIKU_MCP_H_

#include <stddef.h>
#include <stdint.h>

#include <tinymcp.h>

/* One request and one reply, sized for the port: a 4 KB /data file plus
 * its escaping and the envelope fit the out buffer; the scratch holds one
 * page of a node's text. */
#ifndef TIKU_MCP_IN_CAP
#if defined(__MSP430__)
#define TIKU_MCP_IN_CAP  256u
#define TIKU_MCP_OUT_CAP 512u
#define TIKU_MCP_SCRATCH 514u
#else
#define TIKU_MCP_IN_CAP  1024u
#define TIKU_MCP_OUT_CAP 4608u
#define TIKU_MCP_SCRATCH 4098u
#endif
#endif
#ifndef TIKU_MCP_SUBS
#define TIKU_MCP_SUBS 4u       /**< subscription slots per stream */
#endif
#define TIKU_MCP_WATCH_MAX 8u  /**< watches across every stream */
#define TIKU_MCP_PROMPTS_MAX 16u /**< prompt files in one page of prompts/list */
#define TIKU_MCP_NAME_MAX 32u  /**< a /data file name, with its terminator */

#define TIKU_MCP_CHANNEL 0xF3u /**< the console channel marker */

/** @brief What the mcp command and /sys/mcp report. */
typedef struct {
    uint32_t    requests;   /**< messages handled */
    uint32_t    errors;     /**< error replies sent */
    uint32_t    dropped;    /**< messages the transport refused */
    uint32_t    busy;       /**< requests refused while two were queued */
    uint8_t     subs;       /**< watches held for subscribers */
    const char *era;        /**< the console stream's era */
    const char *transport;  /**< "console" */
} tiku_mcp_status_t;

/** @brief The host table every stream binds the library with. */
extern const tinymcp_host_t tiku_mcp_host;

/**
 * @brief Register the console channel, the shell pump and the process.
 * @note Call once from main(), after tiku_shell_init().
 */
void tiku_mcp_init(void);

/** @brief The counters and state. */
void tiku_mcp_status(tiku_mcp_status_t *out);

/** @brief Read /sys/device/name again as the server's name. */
void tiku_mcp_identity(void);

/**
 * @brief Who is changing the board: the client's name while one of its tool
 *        calls runs ("agent" when it gave none), "board" otherwise.
 */
const char *tiku_mcp_actor(void);

/**
 * @brief 1 when the request being served on @p stream has the owner's
 *        authority: the console's, or a TCP request carrying the key.
 */
int tiku_mcp_owner(tinymcp_t *stream);

/** @brief The console transport: its stream, registration and pump. */
void       tiku_mcp_console_init(void);
void       tiku_mcp_console_pump(void);
tinymcp_t *tiku_mcp_console_stream(void);
int        tiku_mcp_console_send(const char *msg, size_t len);
int        tiku_mcp_console_send_part(const char *bytes, size_t len, int last);
uint32_t   tiku_mcp_console_busy(void);

#endif /* TIKU_MCP_H_ */
