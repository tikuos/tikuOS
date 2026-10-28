/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_job.h - the basic tool: a BASIC program run as a job, its
 * output, progress and end, and /sys/mcp/job.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_JOB_H_
#define TIKU_MCP_JOB_H_

#include <stddef.h>

#include <tinymcp.h>
#include <kernel/vfs/tiku_vfs.h>

#ifndef TIKU_MCP_JOB_OUT
#define TIKU_MCP_JOB_OUT 1024u     /**< the last bytes of a job's output kept */
#endif
#ifndef TIKU_MCP_JOB_BUDGET_S
#define TIKU_MCP_JOB_BUDGET_S 60u  /**< seconds a job runs while its call waits */
#endif
#ifndef TIKU_MCP_JOB_NOTE_MS
#define TIKU_MCP_JOB_NOTE_MS 250u  /**< the least time between two progress notes */
#endif
#define TIKU_MCP_JOB_SECONDS_MAX 86400L  /**< the largest budget a call may set */

/**
 * @brief The basic tool: start the program in @p args as the job.
 * @return TINYMCP_LATER while the call waits for the job's end, TINYMCP_OK
 *         for a background job, else the tool's error with its reason
 */
int tiku_mcp_job_tool(tinymcp_t *stream, const tinymcp_args_t *args,
                      tinymcp_sink_t *text);

/** @brief The client cancelled the kept call @p call: stop its job. */
void tiku_mcp_job_cancel(unsigned call);

/**
 * @brief From the pump: progress while the job runs, its result and the
 *        journal when it ends, the budget's end.
 */
void tiku_mcp_job_tick(void);

/** @brief The children of /sys/mcp/job. */
#define TIKU_MCP_JOB_NODES 5u
extern const tiku_vfs_node_t tiku_mcp_job_nodes[TIKU_MCP_JOB_NODES];

#endif /* TIKU_MCP_JOB_H_ */
