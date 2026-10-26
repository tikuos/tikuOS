/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_vfs.c - /sys/mcp: era, transport, library and the counters,
 * one node each, read from the status the service keeps; the ask directory
 * and the journal (tiku_mcp_ask.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_vfs.h"

#include <stdio.h>

#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"

static int mcp_era_read(char *buf, size_t max)
{
    tiku_mcp_status_t s;

    tiku_mcp_status(&s);
    return snprintf(buf, max, "%s\n", s.era);
}

static int mcp_transport_read(char *buf, size_t max)
{
    tiku_mcp_status_t s;

    tiku_mcp_status(&s);
    return snprintf(buf, max, "%s\n", s.transport);
}

static int mcp_library_read(char *buf, size_t max)
{
    return snprintf(buf, max, "tinymcp %s %s\n", TINYMCP_VERSION,
                    TINYMCP_PROTOCOL);
}

static int mcp_channel_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%02X\n", (unsigned)TIKU_MCP_CHANNEL);
}

#define MCP_COUNTER(field)                                                  \
static int mcp_##field##_read(char *buf, size_t max)                        \
{                                                                           \
    tiku_mcp_status_t s;                                                    \
                                                                            \
    tiku_mcp_status(&s);                                                    \
    return snprintf(buf, max, "%lu\n", (unsigned long)s.field);             \
}

MCP_COUNTER(requests)
MCP_COUNTER(errors)
MCP_COUNTER(dropped)
MCP_COUNTER(busy)
MCP_COUNTER(subs)

static const tiku_vfs_node_t mcp_children[] = {
    { .name = "era",       .type = TIKU_VFS_FILE, .read = mcp_era_read },
    { .name = "transport", .type = TIKU_VFS_FILE, .read = mcp_transport_read },
    { .name = "library",   .type = TIKU_VFS_FILE, .read = mcp_library_read },
    { .name = "channel",   .type = TIKU_VFS_FILE, .read = mcp_channel_read },
    { .name = "requests",  .type = TIKU_VFS_FILE, .read = mcp_requests_read },
    { .name = "errors",    .type = TIKU_VFS_FILE, .read = mcp_errors_read },
    { .name = "dropped",   .type = TIKU_VFS_FILE, .read = mcp_dropped_read },
    { .name = "busy",      .type = TIKU_VFS_FILE, .read = mcp_busy_read },
    { .name = "subs",      .type = TIKU_VFS_FILE, .read = mcp_subs_read },
    { .name = "ask",       .type = TIKU_VFS_DIR,
      .children = tiku_mcp_ask_nodes,
      .child_count = (uint8_t)TIKU_MCP_ASK_NODES },
    { .name = "journal",   .type = TIKU_VFS_FILE,
      .read = tiku_mcp_journal_read, .write = tiku_mcp_journal_write,
      .desc = &tiku_mcp_journal_desc },
};

static const tiku_vfs_node_t mcp_dir = {
    .name = "mcp",
    .type = TIKU_VFS_DIR,
    .children = mcp_children,
    .child_count = (uint8_t)(sizeof mcp_children / sizeof mcp_children[0]),
};

const tiku_vfs_node_t *
tiku_mcp_vfs_get(void)
{
    return &mcp_dir;
}
