/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_ipc.c - read-only IPC capabilities, queues and backing counts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_tree_ipc.h"
#include <kernel/ipc/tiku_ipc.h>
#include <stdio.h>
#include <string.h>

#if TIKU_IPC_ENABLE
#if TIKU_IPC_VFS_NODES_MAX < 1 || TIKU_IPC_VFS_NODES_MAX > 65535
#error Invalid IPC VFS node bound
#endif
enum { FIELD_COUNT = 8, NODE_MAX = TIKU_IPC_VFS_NODES_MAX };
static const char *const fields[FIELD_COUNT] = {
    "state", "caps", "queue", "credits", "counters", "buffers", "transport", "reset"
};
static tiku_vfs_node_t nodes[NODE_MAX];
static struct { uint16_t endpoint; uint8_t field; } labels[NODE_MAX];
static char names[TIKU_IPC_ENDPOINTS_MAX][31];
static tiku_ipc_status_t previous[TIKU_IPC_ENDPOINTS_MAX];
static uint16_t node_count;
static uint8_t built;

/** @brief Find the static file whose handler the VFS is dispatching. */
static int serving(void)
{
    const tiku_vfs_node_t *node = tiku_vfs_serving();
    unsigned i;
    for (i = 1; i < node_count; i++)
        if (node == &nodes[i] && node->type == TIKU_VFS_FILE) return (int)i;
    return -1;
}

/** @brief Render the endpoint field selected by the VFS serving node. */
static int read(char *buf, size_t max)
{
    int index = serving();
    tiku_ipc_status_t s;
    const char *field;
    if (index < 0 || tiku_ipc_status(labels[index].endpoint, &s) != TIKU_IPC_OK)
        return TIKU_VFS_ENOENT;
    field = fields[labels[index].field];
    if (!strcmp(field, "state"))
        return snprintf(buf, max, "%s session=%lu\n", s.open ? "open" : "closed",
                         (unsigned long)s.session);
    if (!strcmp(field, "caps"))
        return snprintf(buf, max,
            "message=%u depth=%u buffer=%u alignment=%u methods=%u reset=%u\n",
            s.max_message, s.depth, s.max_buffer, s.alignment, s.methods,
            s.reset_scope);
    if (!strcmp(field, "queue"))
        return snprintf(buf, max, "depth=%u used=%u high_water=%u drops=%lu\n",
            s.depth, s.queued, s.high_water, (unsigned long)s.drops);
    if (!strcmp(field, "credits")) return snprintf(buf, max, "%u\n", s.credits);
    if (!strcmp(field, "counters"))
        return snprintf(buf, max,
            "sent=%lu delivered=%lu stale=%lu cancelled=%lu pending=%u invalid_handles_global=%lu\n",
            (unsigned long)s.sent, (unsigned long)s.delivered,
            (unsigned long)s.stale, (unsigned long)s.cancelled, s.pending_wakeup,
            (unsigned long)s.invalid_handles_global);
    if (!strcmp(field, "buffers"))
        return snprintf(buf, max, "live=%u free=%u quarantined=%u\n",
            s.buffers_live, s.buffers_free, s.buffers_quarantined);
    if (!strcmp(field, "transport"))
        return snprintf(buf, max,
            "supported=%u bytes=%lu crc=%08lx inflight=%u peer_gap_cycles=%lu peer_calls=%lu bad=%lu stale=%lu duplicates=%lu failed=%lu\n",
            s.transport_stats, (unsigned long)s.transport_bytes,
            (unsigned long)s.transport_crc, s.transport_inflight,
            (unsigned long)s.peer_service_max_gap, (unsigned long)s.peer_service_calls,
            (unsigned long)s.wire_bad, (unsigned long)s.wire_stale,
            (unsigned long)s.wire_duplicates, (unsigned long)s.wire_failed);
    if (!strcmp(field, "reset"))
        return snprintf(buf, max, "%u\n", s.reset_scope);
    return TIKU_VFS_ENOENT;
}


/** @brief Accept exactly "1" with optional trailing line whitespace. */
static int reset_write(const char *buf, size_t length)
{
    int index = serving();
    tiku_ipc_err_t result;
    if (index < 0 || !length || buf[0] != '1') return TIKU_VFS_EINVAL;
    for (size_t i = 1; i < length; i++)
        if (buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t')
            return TIKU_VFS_EINVAL;
    result = tiku_ipc_reset(labels[index].endpoint);
    if (result == TIKU_IPC_UNSUPPORTED) return TIKU_VFS_ENOTSUP;
    if (result == TIKU_IPC_AGAIN || result == TIKU_IPC_BUSY) return TIKU_VFS_EBUSY;
    return result == TIKU_IPC_OK ? TIKU_VFS_OK : TIKU_VFS_EIO;
}

/** @brief Reserve siblings together, then recursively populate their children. */
static int build(tiku_vfs_node_t *parent, const char *prefix, size_t depth)
{
    uint16_t chosen[TIKU_IPC_ENDPOINTS_MAX], count = 0, first = node_count;
    unsigned e, i;
    for (e = 0; e < tiku_ipc_endpoint_count(); e++) {
        const char *name = tiku_ipc_endpoint_name((uint16_t)e);
        if (strlen(name) < depth || strncmp(name, prefix, depth)) continue;
        for (i = 0; i < count; i++)
            if (!strcmp(names[e] + depth, names[chosen[i]] + depth)) break;
        if (i == count) chosen[count++] = (uint16_t)e;
    }
    if (count > 255 || node_count + count > NODE_MAX) return TIKU_VFS_E2BIG;
    parent->children = &nodes[first];
    parent->child_count = (uint8_t)count;
    node_count = (uint16_t)(node_count + count);
    for (i = 0; i < count; i++) {
        uint16_t endpoint = chosen[i];
        tiku_vfs_node_t *child = &nodes[first + i];
        size_t end = depth + strlen(names[endpoint] + depth);
        *child = (tiku_vfs_node_t){ .name = names[endpoint] + depth,
                                  .type = TIKU_VFS_DIR };
        if (end < strlen(tiku_ipc_endpoint_name(endpoint))) {
            int result = build(child, tiku_ipc_endpoint_name(endpoint), end + 1u);
            if (result) return result;
        } else {
            if (node_count + FIELD_COUNT > NODE_MAX) return TIKU_VFS_E2BIG;
            child->children = &nodes[node_count];
            child->child_count = FIELD_COUNT;
            for (e = 0; e < FIELD_COUNT; e++, node_count++) {
                nodes[node_count] = (tiku_vfs_node_t){
                    .name = fields[e], .type = TIKU_VFS_FILE, .read = read,
                    .write = e == FIELD_COUNT - 1 ? reset_write : NULL,
                    .req_cap = e == FIELD_COUNT - 1 ? TIKU_VFS_CAP_SYS : 0
                };
                labels[node_count].endpoint = endpoint;
                labels[node_count].field = (uint8_t)e;
            }
        }
    }
    return TIKU_VFS_OK;
}

/** @brief Notify stable file identities after endpoint status changes. */
static void changed(void *unused)
{
    uint16_t e, n;
    (void)unused;
    for (e = 0; e < tiku_ipc_endpoint_count(); e++) {
        tiku_ipc_status_t current;
        if (tiku_ipc_status(e, &current) != TIKU_IPC_OK) continue;
        if (!memcmp(&previous[e], &current, sizeof current)) continue;
        previous[e] = current;
        for (n = 1; n < node_count; n++)
            if (nodes[n].type == TIKU_VFS_FILE && labels[n].endpoint == e &&
                labels[n].field != 1 && labels[n].field != FIELD_COUNT - 1)
                tiku_vfs_notify(&nodes[n]);
    }
}

int tiku_vfs_tree_ipc_init(void)
{
    unsigned e, i;
    int result;
    if (built) return TIKU_VFS_OK;
    for (e = 0; e < tiku_ipc_endpoint_count(); e++) {
        strcpy(names[e], tiku_ipc_endpoint_name((uint16_t)e));
        for (i = 0; i < 30; i++) if (names[e][i] == '/') names[e][i] = '\0';
    }
    node_count = 1;
    nodes[0] = (tiku_vfs_node_t){ .name = "ipc", .type = TIKU_VFS_DIR };
    result = build(&nodes[0], "", 0);
    if (result) return result;
    result = tiku_vfs_validate_node(&nodes[0], 4);
    if (result) return result;
    if (tiku_ipc_observe(changed, NULL) != TIKU_IPC_OK) return TIKU_VFS_EBUSY;
    for (e = 0; e < tiku_ipc_endpoint_count(); e++)
        (void)tiku_ipc_status((uint16_t)e, &previous[e]);
    built = 1;
    return TIKU_VFS_OK;
}

const tiku_vfs_node_t *tiku_vfs_tree_ipc_get(void)
{
    return built ? &nodes[0] : NULL;
}
#endif
