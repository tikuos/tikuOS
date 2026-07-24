/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.c - /data VFS nodes (user-data / persisted state)
 *
 * /data is a DYNAMIC directory backed by the Tiku File Store (kernel/fs):
 * arbitrary files can be created, written, read, listed and deleted at run
 * time and are kept in NVM.  So:
 *
 *   write /data/blink.bas "10 LED 0,1 : ..."   # create / overwrite
 *   ls /data                                    # list files
 *   cat /data/blink.bas                         # read
 *
 * The file store sits on the carved NVM region's filesystem extent and is
 * durable on both NVM families:
 *   - MSP430: a `.persistent` FRAM array, written in place.
 *   - Ambiq : the FS extent of the memory-mapped NVM region -- read in place
 *             (no SRAM shadow), written via the region backend (MRAM bootrom),
 *             so files survive a power cut.  Sized in megabytes (see
 *             TIKU_NVMFS_FS_BYTES / TIKU_TFS_MAX_FILES), between the NVM tier's
 *             bump extent (front) and the reserved durable tail.
 *   - else  : plain `.bss` (functional but volatile) until a backend lands.
 * When BASIC is built, the legacy /data/basic bridge to the interpreter's
 * program store is kept as a static child.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_data.h"
#include "tiku.h"

#if defined(TIKU_SHELL_ENABLE)

#include <string.h>

#include "kernel/fs/tiku_tfs.h"
#include <kernel/memory/tiku_mem.h>      /* tiku_mpu_(un)lock_nvm, tiku_tier_nvm_write */
#include "kernel/memory/tiku_nvm_region.h"

#if TIKU_SHELL_CMD_BASIC
#include "kernel/shell/basic/tiku_basic.h"
#endif

/*---------------------------------------------------------------------------*/
/* NVM-BACKED FILE STORE FOR /data                                           */
/*---------------------------------------------------------------------------*/

/* The store's NVM home.  Ambiq: the FS extent of the carved NVM region (durable
 * MRAM, read in place, written via the region backend) -- no SRAM array.
 * RP2350: the FS extent of the carved Flash region (read in place via XIP,
 * written via the Flash region backend).  nRF54L: the FS extent of the carved
 * RRAM region (byte-writable NVM read in place, written via the region backend
 * through the RRAMC WEN gate).  MSP430: a `.persistent` FRAM array (in place).
 * Other parts: plain `.bss` (volatile) until a backend lands. */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || defined(PLATFORM_NORDIC)

_Static_assert(TIKU_TFS_REGION_BYTES <= TIKU_NVMFS_FS_BYTES,
               "TFS store larger than the region FS extent");

static tiku_tfs_t          data_fs;
static tiku_nvm_backend_t  data_be;
static uint8_t             data_fs_ready;

/* Program through the region backend (MRAM bootrom); it brackets its own NVM
 * window, so reads stay plain pointer derefs into the FS extent. */
static int
data_be_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    return (tiku_tier_nvm_write((uint8_t *)be->base + off, src, len)
            == TIKU_MEM_OK) ? 0 : -1;
}

/**
 * @brief Lazily mount the /data file store over the carved NVM region.
 *
 * Idempotent: returns immediately once mounted.  Locates the region backend
 * (MRAM), places the FS extent between the tier extent (front) and the
 * reserved tail, and mounts the TFS over it.
 *
 * @return 0 once the store is ready; -1 if the region is absent or too small
 *         to hold the FS extent, or the TFS mount fails.
 */
static int
data_tfs_ensure(void)
{
    const tiku_nvm_backend_t *rgn;

    if (data_fs_ready) {
        return 0;
    }
    rgn = tiku_nvm_backend_get();
    if (rgn == NULL || rgn->base == NULL ||
        rgn->size < (size_t)TIKU_NVMFS_FS_BYTES + TIKU_NVM_RESERVED_BYTES) {
        return -1;
    }
    /* FS extent: between the tier extent (front) and the reserved tail. */
    data_be.base  = rgn->base +
        (rgn->size - TIKU_NVMFS_FS_BYTES - TIKU_NVM_RESERVED_BYTES);
    data_be.size  = TIKU_NVMFS_FS_BYTES;
    data_be.write = data_be_write;
    data_be.erase = NULL;
    data_be.ctx   = NULL;
    if (tiku_tfs_mount(&data_fs, &data_be) != TFS_OK) {
        return -1;
    }
    data_fs_ready = 1;
    return 0;
}

#else  /* MSP430 FRAM / host: a static backing array */

#if defined(PLATFORM_MSP430)
#define DATA_TFS_SECTION TIKU_DURABLE   /* FRAM-backed file store */
#else
#define DATA_TFS_SECTION                /* host: volatile test backing */
#endif

static DATA_TFS_SECTION uint8_t data_tfs_region[TIKU_TFS_REGION_BYTES];
static tiku_tfs_t          data_fs;
static tiku_nvm_backend_t  data_be;
static uint8_t             data_fs_ready;

/**
 * @brief NVM backend write callback for the /data file store (FRAM/host).
 *
 * Copies @p len bytes from @p src to offset @p off within the backing
 * array, bracketing the copy in an MPU NVM-unlock window so the
 * `.persistent` FRAM region is writable.
 *
 * @param be   Backend descriptor (its base is the store's backing array)
 * @param off  Byte offset within the backing store
 * @param src  Source bytes to program
 * @param len  Number of bytes to write
 * @return 0 always (the in-place copy cannot fail)
 */
static int
data_be_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    uint16_t mpu = tiku_mpu_unlock_nvm();
    memcpy(be->base + off, src, len);
    tiku_mpu_lock_nvm(mpu);
    return 0;
}

/**
 * @brief Lazily mount the Tiku File Store backing /data (FRAM/host).
 *
 * Idempotent: returns immediately once mounted.  On first call it wires
 * the NVM backend to the static backing array and mounts the store.
 *
 * @return 0 if the store is mounted (or already was), -1 on mount failure
 */
static int
data_tfs_ensure(void)
{
    if (data_fs_ready) {
        return 0;
    }
    data_be.base  = data_tfs_region;
    data_be.size  = sizeof data_tfs_region;
    data_be.write = data_be_write;
    data_be.erase = NULL;
    data_be.ctx   = NULL;
    if (tiku_tfs_mount(&data_fs, &data_be) != TFS_OK) {
        return -1;
    }
    data_fs_ready = 1;
    return 0;
}

#endif

/*---------------------------------------------------------------------------*/
/* DYNAMIC-DIRECTORY OPS — bridge /data to the file store                     */
/*---------------------------------------------------------------------------*/

typedef struct { tiku_vfs_dyn_list_cb cb; void *ctx; } data_list_w_t;

/**
 * @brief File-store list adapter: forward each entry to the VFS callback.
 *
 * Bridges the tiku_tfs_list callback (name, len, ctx) to the VFS
 * dynamic-list callback (name, ctx), discarding the length.
 *
 * @param name  File name reported by the store
 * @param len   Entry length (unused)
 * @param vw    Wrapper carrying the VFS callback and its context
 */
static void
data_list_thunk(const char *name, size_t len, void *vw)
{
    data_list_w_t *w = (data_list_w_t *)vw;
    (void)len;
    w->cb(name, w->ctx);
}

/**
 * @brief List op for the /data dynamic directory.
 *
 * Enumerates every file in the store, invoking @p cb once per name
 * (via data_list_thunk).  No-op if the store fails to mount.
 *
 * @param cb   Per-entry callback
 * @param ctx  Opaque context passed to @p cb
 */
static void
data_dyn_list(tiku_vfs_dyn_list_cb cb, void *ctx)
{
    data_list_w_t w;
    if (data_tfs_ensure() != 0) {
        return;
    }
    w.cb = cb;
    w.ctx = ctx;
    (void)tiku_tfs_list(&data_fs, data_list_thunk, &w);
}

/**
 * @brief Read op for /data/<name> dynamic files.
 *
 * Reads up to @p max bytes of file @p name from the store into @p buf.
 *
 * @param name  File name under /data
 * @param buf   Output buffer
 * @param max   Capacity of @p buf
 * @return Bytes read, or -1 if the store is unmounted or the file is absent
 */
static int
data_dyn_read(const char *name, char *buf, size_t max)
{
    size_t n = 0;
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    if (tiku_tfs_read(&data_fs, name, buf, max, &n) != TFS_OK) {
        return -1;
    }
    return (int)n;
}

/**
 * @brief Write op for /data/<name> dynamic files.
 *
 * Creates or overwrites file @p name in the store with @p len bytes
 * from @p buf.
 *
 * @param name  File name under /data
 * @param buf   Bytes to store
 * @param len   Number of bytes
 * @return 0 on success, -1 on mount failure or a full/failed store
 */
static int
data_dyn_write(const char *name, const char *buf, size_t len)
{
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    return (tiku_tfs_write(&data_fs, name, buf, len) == TFS_OK) ? 0 : -1;
}

/**
 * @brief Unlink op for /data/<name> dynamic files.
 *
 * Deletes file @p name from the store.
 *
 * @param name  File name under /data
 * @return 0 on success, -1 on mount failure or if the file is absent
 */
static int
data_dyn_unlink(const char *name)
{
    if (data_tfs_ensure() != 0) {
        return -1;
    }
    return (tiku_tfs_delete(&data_fs, name) == TFS_OK) ? 0 : -1;
}

/* Folder-aware listing: present the flat store as a tree under @p prefix. */
static void
data_dyn_list_dir(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx)
{
    data_list_w_t w;
    if (data_tfs_ensure() != 0) {
        return;
    }
    w.cb = cb;
    w.ctx = ctx;
    (void)tiku_tfs_list_dir(&data_fs, prefix, data_list_thunk, &w);
}

static const tiku_vfs_dynops_t data_dynops = {
    data_dyn_list, data_dyn_read, data_dyn_write, data_dyn_unlink,
    data_dyn_list_dir
};

/*---------------------------------------------------------------------------*/
/* /data/basic — legacy BASIC program bridge (only when BASIC is built)      */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_BASIC

/**
 * @brief Read handler for /data/basic (legacy BASIC program bridge).
 *
 * Renders the BASIC interpreter's current program store as text.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (see tiku_basic_vfs_read)
 */
static int
data_basic_read(char *buf, size_t max)
{
    return tiku_basic_vfs_read(buf, (unsigned int)max);
}

/**
 * @brief Write handler for /data/basic (legacy BASIC program bridge).
 *
 * Loads @p len bytes of program text into the BASIC interpreter's store.
 *
 * @param buf  Program text
 * @param len  Number of bytes
 * @return 0 on success, negative on error (see tiku_basic_vfs_write)
 */
static int
data_basic_write(const char *buf, size_t len)
{
    return tiku_basic_vfs_write(buf, (unsigned int)len);
}

static const tiku_vfs_node_t data_children[] = {
    { "basic", TIKU_VFS_FILE, data_basic_read, data_basic_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_FS },
};

static const tiku_vfs_node_t data_node = {
    "data", TIKU_VFS_DIR, NULL, NULL,
    data_children, (uint8_t)(sizeof(data_children) / sizeof(data_children[0])),
    NULL, &data_dynops
};

#else  /* no BASIC: /data is purely the dynamic file store */

static const tiku_vfs_node_t data_node = {
    "data", TIKU_VFS_DIR, NULL, NULL,
    NULL, 0,
    NULL, &data_dynops
};

#endif

/*---------------------------------------------------------------------------*/
/* df SUPPORT — file-store usage stats                                       */
/*---------------------------------------------------------------------------*/

typedef struct { uint16_t files; uint32_t bytes; } data_df_acc_t;

/**
 * @brief File-store accumulator for df usage stats.
 *
 * Called once per file by tiku_vfs_tree_data_df(): increments the file
 * count and adds the entry's byte length to the running total.
 *
 * @param name  File name (unused)
 * @param len   File length in bytes, added to the accumulator
 * @param vacc  Pointer to the data_df_acc_t accumulator
 */
static void
data_df_thunk(const char *name, size_t len, void *vacc)
{
    data_df_acc_t *a = (data_df_acc_t *)vacc;
    (void)name;
    a->files++;
    a->bytes += (uint32_t)len;
}

int
tiku_vfs_tree_data_df(tiku_data_df_t *out)
{
    data_df_acc_t acc;

    acc.files = 0u;
    acc.bytes = 0u;
    if (out == NULL || data_tfs_ensure() != 0) {
        return -1;
    }
    (void)tiku_tfs_list(&data_fs, data_df_thunk, &acc);
    out->used_files = acc.files;
    out->used_bytes = acc.bytes;
    out->max_files  = (uint16_t)TIKU_TFS_MAX_FILES;
    out->slot_bytes = (uint16_t)TIKU_TFS_SLOT_DATA;
    out->cap_bytes  = (uint32_t)TIKU_TFS_MAX_FILES * (uint32_t)TIKU_TFS_SLOT_DATA;
#if defined(PLATFORM_AMBIQ)
    out->backing = "MRAM";
#elif defined(PLATFORM_MSP430)
    out->backing = "FRAM";
#elif defined(PLATFORM_RP2350)
    out->backing = "Flash";  /* carved QSPI region, erase+program backend */
#elif defined(PLATFORM_NORDIC)
    out->backing = "RRAM";   /* carved byte-writable RRAM region, WEN-gated */
#else
    out->backing = "RAM*";   /* volatile until a backend lands */
#endif
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC                                                                     */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *
tiku_vfs_tree_data_get(void)
{
    return &data_node;
}

#endif /* TIKU_SHELL_ENABLE */
