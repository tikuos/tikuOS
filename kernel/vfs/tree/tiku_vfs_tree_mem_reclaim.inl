/* Included by the /sys tree. SPDX-License-Identifier: Apache-2.0 */
#include "kernel/memory/tiku_reclaim_internal.h"
#if TIKU_MEM_RECLAIM_ENABLE
#define RECLAIM_READER(name) \
static int reclaim_##name##_read(char *buf, size_t max) \
{ return tiku_mem_reclaim_read(#name, buf, max); }
RECLAIM_READER(mode)
RECLAIM_READER(job)
RECLAIM_READER(last)
RECLAIM_READER(pending)
RECLAIM_READER(owners)
RECLAIM_READER(stats)
#undef RECLAIM_READER

static int reclaim_write_status(tiku_mem_err_t err)
{
    switch (err) {
    case TIKU_MEM_OK: return 0;
    case TIKU_MEM_ERR_BUSY: return TIKU_VFS_EBUSY;
    case TIKU_MEM_ERR_IO: return TIKU_VFS_EIO;
    default: return TIKU_VFS_EINVAL;
    }
}
static int reclaim_mode_write(const char *buf, size_t size)
{ return reclaim_write_status(tiku_mem_reclaim_write("mode", buf, size)); }
static int reclaim_retry_write(const char *buf, size_t size)
{ return reclaim_write_status(tiku_mem_reclaim_write("retry", buf, size)); }
static int reclaim_cancel_write(const char *buf, size_t size)
{ return reclaim_write_status(tiku_mem_reclaim_write("cancel", buf, size)); }

static const tiku_vfs_node_t mem_reclaim_children[] = {
    {"mode", TIKU_VFS_FILE, reclaim_mode_read, reclaim_mode_write,
     NULL, 0, &desc_mem_map, NULL, TIKU_VFS_CAP_SYS},
    {"pending", TIKU_VFS_FILE, reclaim_pending_read, NULL, NULL, 0, &desc_mem_map, NULL, 0},
    {"job", TIKU_VFS_FILE, reclaim_job_read, NULL, NULL, 0, &desc_mem_map, NULL, 0},
    {"last", TIKU_VFS_FILE, reclaim_last_read, NULL, NULL, 0, &desc_mem_map, NULL, 0},
    {"owners", TIKU_VFS_FILE, reclaim_owners_read, NULL, NULL, 0, &desc_mem_map, NULL, 0},
    {"stats", TIKU_VFS_FILE, reclaim_stats_read, NULL, NULL, 0, &desc_mem_map, NULL, 0},
    {"retry", TIKU_VFS_FILE, NULL, reclaim_retry_write,
     NULL, 0, NULL, NULL, TIKU_VFS_CAP_SYS},
    {"cancel", TIKU_VFS_FILE, NULL, reclaim_cancel_write,
     NULL, 0, NULL, NULL, TIKU_VFS_CAP_SYS},
};
#endif
