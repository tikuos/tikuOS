/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_ask.h - the board asks: /sys/mcp/ask, one question and its
 * answer at a time, and the journal of who asked, answered and changed what.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_ASK_H_
#define TIKU_MCP_ASK_H_

#include <stddef.h>
#include <stdint.h>

#include <kernel/vfs/tiku_vfs.h>

#ifndef TIKU_MCP_ASK_MAX
#define TIKU_MCP_ASK_MAX 192u      /**< a question or answer, NUL included */
#endif
#ifndef TIKU_MCP_JOURNAL_MAX
#define TIKU_MCP_JOURNAL_MAX 16u   /**< journal entries kept, oldest dropped */
#endif
#define TIKU_MCP_JOURNAL_TEXT 72u  /**< one entry's text, NUL included */

/** @brief Where a question stands. */
typedef enum {
    TIKU_MCP_ASK_IDLE = 0,         /**< nothing asked since boot */
    TIKU_MCP_ASK_ASKED,            /**< a question waits for its answer */
    TIKU_MCP_ASK_ANSWERED          /**< the last question has its answer */
} tiku_mcp_ask_state_t;

/**
 * @brief Ask @p q (@p len bytes, one trailing newline dropped) on behalf of
 *        tiku_mcp_actor(), as a write to /sys/mcp/ask/question does: the
 *        sequence number grows, the answer clears, subscribers hear it.
 * @return 0, or TIKU_VFS_EINVAL for an empty question
 */
int tiku_mcp_ask(const char *q, size_t len);

/** @brief The question's sequence number; 0 before the first. */
uint32_t tiku_mcp_ask_seq(void);

/** @brief Where the question stands. */
tiku_mcp_ask_state_t tiku_mcp_ask_state(void);

/** @brief The answer's text, "" until there is one. */
const char *tiku_mcp_ask_answer(void);

/** @brief Add an entry to the journal under @p who; the text is cut to fit. */
void tiku_mcp_journal(const char *who, const char *text);

/** @brief The children of /sys/mcp/ask. */
#define TIKU_MCP_ASK_NODES 6u
extern const tiku_vfs_node_t tiku_mcp_ask_nodes[TIKU_MCP_ASK_NODES];

/** @brief /sys/mcp/journal's handlers and its streaming descriptor. */
int tiku_mcp_journal_read(char *buf, size_t max);
int tiku_mcp_journal_write(const char *buf, size_t len);
extern const tiku_vfs_desc_t tiku_mcp_journal_desc;

/** @brief Register BASIC's ASK statement where the build has BASIC. */
void tiku_mcp_ask_init(void);

#endif /* TIKU_MCP_ASK_H_ */
