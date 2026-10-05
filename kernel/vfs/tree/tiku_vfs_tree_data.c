/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.c - /data VFS nodes (user data and persisted state).
 *
 * A dynamic directory backed by the Tiku File Store: files are created,
 * written, read, listed and deleted at run time, in the carved NVM region, a
 * .persistent FRAM array on MSP430, or a .bss array on the host.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_data.h"
#include "tiku.h"

/*
 * The store is not a shell feature: the backing memory, the backend, the
 * mount, the /data namespace and /sys/fs/data are all compiled with or without
 * a shell, because loadable modules, radio firmware and device-management
 * transports use the store in builds that have none.
 */

#include <string.h>
#include <stdio.h>

#include "kernel/fs/tiku_tfs.h"
#include <kernel/memory/tiku_mem.h>      /* MPU windows, tiku_tier_nvm_write */
#include "kernel/memory/tiku_nvm_region.h"
#include "kernel/memory/tiku_layout.h"
#include <kernel/memory/tiku_nvm_map.h>  /* TIKU_DEVICE_NVM_LABEL fallback */

/*---------------------------------------------------------------------------*/
/* NVM-BACKED FILE STORE FOR /data                                           */
/*---------------------------------------------------------------------------*/

/* The store's NVM home.  Region-backed parts (Ambiq, RP2350, nRF54L,
 * STM32N6, RA8P1, ESP32-C61) use the file-store extent of the carved NVM
 * region, read in place and written through tiku_tier_nvm_write() -- no SRAM
 * array.  MSP430 uses a `.persistent` FRAM array, written in place; the host
 * build a plain `.bss` array (volatile). */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
    defined(PLATFORM_RA8P1) || defined(PLATFORM_ESP32C61)

/*
 * Capacity is derived from the extent at mount: the largest file count that
 * fits.  The mount enforces the store's floor at run time, because only the
 * linker knows the real carve.
 */

static tiku_tfs_t          data_fs;
static tiku_nvm_backend_t  data_be;

/** @brief Program the extent through tiku_tier_nvm_write(), which brackets
 *         its own NVM window; reads stay plain pointer reads of the extent. */
static int
data_be_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) { return -1; }
    return (tiku_tier_nvm_write((uint8_t *)be->base + off, src, len)
            == TIKU_MEM_OK) ? 0 : -1;
}

/**
 * @brief Point data_be at the file-store extent of the carved region.
 *
 * @param region Out: the whole region, for the provisioning check.
 * @param base   Out: the store's offset inside it.
 * @return 1 when a region large enough exists, else 0.
 */
static int
data_bind(tiku_nvm_backend_t *region, size_t *base)
{
    const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();
    size_t at = (size_t)tiku_layout_base();

    if (rgn == NULL || rgn->base == NULL || rgn->size <= at) {
        return 0;
    }
    /* The tier takes the front of the region up to the layout's base and the
     * store everything behind it; the mount enforces the store's floor. */
    data_be.base  = rgn->base + at;
    data_be.size  = rgn->size - at;
    data_be.write = data_be_write;
    data_be.erase = NULL;
    data_be.ctx   = NULL;
    *region = *rgn;
    *base   = at;
    return 1;
}

/**
 * @brief Report how the carved region is divided, for `df`.
 *
 * The tier and the store tile the region, so `idle_bytes` is always 0.
 *
 * @param out  Snapshot to fill in (extent fields only).
 */
static void
data_fill_extents(tiku_data_df_t *out)
{
    const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();
    uint32_t at = tiku_layout_base();

    out->region_bytes = (rgn != NULL) ? (uint32_t)rgn->size : 0u;
    out->tier_bytes   = (out->region_bytes > at) ? at : out->region_bytes;
    out->fs_bytes     = out->region_bytes - out->tier_bytes;
    out->idle_bytes   = 0u;
}

/**
 * @brief Whether the store may be created at @p base without being asked.
 *
 * Region-backed stores are provisioned by boot before publishing their tier.
 * Mounting never completes a partial initialization or guesses ownership.
 */
static int
data_may_create(const tiku_nvm_backend_t *region, size_t base)
{
    (void)region;
    (void)base;
    return 0;
}

/** @brief Why the layout service holds the store, or NULL when it does not. */
static const char *
data_held(void)
{
    const tiku_layout_state_t *ls = tiku_layout_state();

    if (ls->store != TIKU_LAYOUT_STORE_HELD) {
        return NULL;
    }
    switch (ls->held) {
    case TIKU_LAYOUT_HELD_TORN:
        return "the store has lost its header but still holds files";
    case TIKU_LAYOUT_HELD_GEOMETRY:
        return "the store was formatted for a region of another size";
    case TIKU_LAYOUT_HELD_VERSION:
        return "the store is in another format version";
    case TIKU_LAYOUT_HELD_AMBIGUOUS:
        return "more than one store header lies in the region";
    case TIKU_LAYOUT_HELD_INTERRUPTED:
        return "a layout change was interrupted (see layout status)";
    case TIKU_LAYOUT_HELD_IO:
        return "store or layout initialization could not be persisted";
    case TIKU_LAYOUT_HELD_ENTROPY:
        return "ownership identity unavailable; restart to retry blank-media setup";
    case TIKU_LAYOUT_HELD_CONTROL:
        return "layout ownership is missing; recover only at a verified previous offset";
    case TIKU_LAYOUT_HELD_CONTRACT:
        return "layout belongs to another image; explicit recovery required";
    case TIKU_LAYOUT_HELD_REBOOT:
        return "layout recovered; reboot before using /data and the NVM tier";
    case TIKU_LAYOUT_HELD_ELSEWHERE:
    default:
        return "another store header lies elsewhere in the region";
    }
}

/** @brief Whether a layout change is waiting to be resumed. */
static int
data_interrupted(void)
{
    const tiku_layout_state_t *ls = tiku_layout_state();

    return ls->store == TIKU_LAYOUT_STORE_HELD &&
           ls->held == TIKU_LAYOUT_HELD_INTERRUPTED;
}

/** @brief Record @p base as the store's home once a store exists there. */
static int
data_adopt(size_t base)
{
    return tiku_layout_adopt((uint32_t)base);
}

#else  /* MSP430 FRAM / host: a static backing array */

#if defined(PLATFORM_MSP430)
#define DATA_TFS_SECTION TIKU_DURABLE   /* FRAM-backed file store */
#else
#define DATA_TFS_SECTION                /* host: volatile test backing */
#endif

/*
 * MSP430 and host have no carved extent to derive from -- the store's backing
 * is this array -- so here the geometry is stated rather than derived, and the
 * array is sized from it.  Mount then derives the same count straight back,
 * because TIKU_TFS_EXTENT_FOR_SLOTS is the exact inverse of the fit it does, so
 * these platforms take the identical code path rather than a special case.
 */
#ifndef DATA_TFS_SLOTS
#define DATA_TFS_SLOTS  TIKU_TFS_MIN_SLOTS
#endif
static DATA_TFS_SECTION uint8_t
    data_tfs_region[TIKU_TFS_EXTENT_FOR_SLOTS(DATA_TFS_SLOTS)];
static tiku_tfs_t          data_fs;
static tiku_nvm_backend_t  data_be;

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
 * @return Zero on completion, negative on an invalid range or flush failure
 */
static int
data_be_write(tiku_nvm_backend_t *be, size_t off, const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) { return -1; }
    uint16_t mpu = tiku_mpu_unlock_nvm();
    tiku_mem_arch_nvm_write(be->base + off, src, (tiku_mem_arch_size_t)len);
    return tiku_mpu_lock_nvm_status(mpu) == TIKU_MEM_OK ? 0 : -1;
}

/**
 * @brief Point data_be at the static backing array, which is its own region.
 *
 * @param region Out: the array as a region, for the provisioning check.
 * @param base   Out: always 0.
 * @return 1.
 */
static int
data_bind(tiku_nvm_backend_t *region, size_t *base)
{
    data_be.base  = data_tfs_region;
    data_be.size  = sizeof data_tfs_region;
    data_be.write = data_be_write;
    data_be.erase = NULL;
    data_be.ctx   = NULL;
    *region = data_be;
    *base   = 0u;
    return 1;
}

/**
 * @brief Report the store's extent, for `df` (no carved region here).
 *
 * MSP430 and host builds size the backing array from the store's geometry, so
 * the extent always fits exactly and there is no region to divide -- reporting
 * a zero region tells `df` to omit the region breakdown entirely.
 *
 * @param out  Snapshot to fill in (extent fields only).
 */
static void
data_fill_extents(tiku_data_df_t *out)
{
    out->region_bytes = 0u;
    out->tier_bytes   = 0u;
    out->fs_bytes     = (uint32_t)sizeof data_tfs_region;
    out->idle_bytes   = 0u;
}

/** @brief Create the store only in a wholly, uniformly blank static array. */
static int
data_may_create(const tiku_nvm_backend_t *region, size_t base)
{
    return tiku_tfs_may_provision(region, base, TIKU_TFS_LOCATE_STEP);
}

/** @brief No layout service holds a static array's store. */
static const char *
data_held(void)
{
    return NULL;
}

/** @brief No layout change can be pending on a static array. */
static int
data_interrupted(void)
{
    return 0;
}

/** @brief A static array's store has no base to record. */
static int
data_adopt(size_t base)
{
    (void)base;
    return 0;
}

#endif

/*---------------------------------------------------------------------------*/
/* MOUNT POLICY -- one rule for every platform                               */
/*---------------------------------------------------------------------------*/

/* The mount never formats.  A store is created without asking only on a
 * region or array that is blank end to end; anything else leaves /data absent,
 * with the reason kept so df can say why, until mkfs formats on request. */
enum { DATA_UNTRIED = 0, DATA_READY, DATA_ABSENT, DATA_REFUSED, DATA_HELD };
static uint8_t          data_state;
static tiku_tfs_probe_t data_probe;
static int8_t           data_mount_rc;

/**
 * @brief Mount /data once; provision a wholly blank medium; otherwise refuse.
 *
 * @return 0 once the store is ready, -1 while it is absent or refused.
 */
static int
data_tfs_ensure(void)
{
    tiku_nvm_backend_t region;
    size_t base;
    int rc;

    if (data_state == DATA_READY) {
        return 0;
    }
    if (data_state != DATA_UNTRIED) {
        return -1;
    }
    if (!data_bind(&region, &base)) {
        data_state = DATA_ABSENT;
        return -1;
    }
    if (data_held() != NULL) {
        data_state = DATA_HELD;
        return -1;
    }
    rc = tiku_tfs_mount(&data_fs, &data_be);
    if (rc == TFS_ERR_NOSTORE && data_may_create(&region, base)) {
        rc = tiku_tfs_format(&data_fs);
        if (rc == TFS_OK) {
            if (data_adopt(base) != 0) { rc = TFS_ERR_IO; }
        }
    }
    if (rc == TFS_OK) {
        data_state = DATA_READY;
        return 0;
    }
    data_mount_rc = (int8_t)rc;
    (void)tiku_tfs_probe(&data_be, &data_probe);
    data_state = DATA_REFUSED;
    return -1;
}

const char *
tiku_vfs_tree_data_why(void)
{
    if (data_state == DATA_UNTRIED) {
        (void)data_tfs_ensure();
    }
    if (data_state == DATA_READY) {
        return NULL;
    }
    if (data_state == DATA_ABSENT) {
        return "no NVM region on this part";
    }
    if (data_state == DATA_HELD) {
        return data_held();
    }
    switch (data_probe.kind) {
    case TFS_PROBE_TORN:
        return "the store has lost its header but still holds files";
    case TFS_PROBE_GEOMETRY:
        return "the store was formatted for a region of another size";
    case TFS_PROBE_VERSION:
        return "the store is in another format version";
    case TFS_PROBE_TOOSMALL:
        return "the region is too small for a store";
    case TFS_PROBE_BLANK:
        return "blank store extent without permission to initialize; left untouched";
    case TFS_PROBE_UNKNOWN:
        return "nonblank data with unrecognized metadata; left untouched";
    case TFS_PROBE_COMPATIBLE:
    default:
        break;
    }
    return (data_mount_rc == TFS_ERR_CORRUPT)
        ? "the store's directory is inconsistent" : "the store did not mount";
}

int
tiku_vfs_tree_data_probe(tiku_tfs_probe_t *out)
{
    tiku_nvm_backend_t region;
    size_t base;

    if (out == NULL || !data_bind(&region, &base)) {
        return -1;
    }
    (void)base;
    return (tiku_tfs_probe(&data_be, out) == TFS_OK) ? 0 : -1;
}

int
tiku_vfs_tree_data_untouched(void)
{
    tiku_nvm_backend_t region;
    size_t base;

    return data_bind(&region, &base) && data_may_create(&region, base);
}

void
tiku_vfs_tree_data_retry(void)
{
    if (data_state != DATA_READY) {
        data_state = DATA_UNTRIED;
    }
}

int
tiku_vfs_tree_data_format(void)
{
    tiku_nvm_backend_t region;
    size_t base;

    if (data_interrupted()) {
        return -2;
    }
    if (data_fs.wr_open) { return -1; }
    if (!data_bind(&region, &base)) {
        return -1;
    }
    if (tiku_tfs_init(&data_fs, &data_be) != TFS_OK) {
        data_state = DATA_UNTRIED;
        return -1;
    }
    if (data_adopt(base) != 0) {
        data_state = DATA_HELD;
        return -3;                  /* formatted, but control not committed */
    }
    if (data_held() != NULL) {
        data_state = DATA_HELD;
        return 1;                   /* recovery requires a reboot */
    }
    data_state = DATA_READY;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* VFS PRESENTATION                                                          */
/*---------------------------------------------------------------------------*/
/* Available with or without a command shell. */

#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC
#include "kernel/shell/basic/tiku_basic.h"
#endif

/*---------------------------------------------------------------------------*/
/* DYNAMIC-DIRECTORY OPS — bridge /data to the file store                     */
/*---------------------------------------------------------------------------*/

/** @brief The VFS list callback and context data_list_thunk() forwards to. */
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

/** @brief Folder-aware list op: the flat store as a tree under @p prefix. */
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
/* /data/basic — the saved BASIC program (only when BASIC is built)          */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC

/**
 * @brief Read handler for /data/basic, the saved BASIC program.
 *
 * Renders the BASIC interpreter's saved program as text.
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
 * @brief Write handler for /data/basic, the saved BASIC program.
 *
 * Saves @p len bytes of numbered program text as the BASIC program.
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
 * Called once per file by data_df_mounted(): increments the file count
 * and adds the entry's byte length to the running total.
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

/**
 * @brief Fill a usage snapshot from the store only if it is mounted.
 *
 * Never mounts or provisions: an untouched store reports nothing.
 *
 * @param out  Snapshot to fill
 * @return 0, or -1 when the store is not mounted or @p out is NULL
 */
static int
data_df_mounted(tiku_data_df_t *out)
{
    data_df_acc_t acc;

    acc.files = 0u;
    acc.bytes = 0u;
    if (out == NULL || data_state != DATA_READY || !data_fs.mounted) {
        return -1;
    }
    (void)tiku_tfs_list(&data_fs, data_df_thunk, &acc);
    out->used_files = acc.files;
    out->used_bytes = acc.bytes;
    out->max_files  = data_fs.nfiles;          /* derived at mount */
    out->used_slots = (uint16_t)tiku_tfs_used_slots(&data_fs);
    out->total_slots = data_fs.nslots;
    out->slot_bytes = (uint16_t)TIKU_TFS_SLOT_DATA;
    out->cap_bytes  = (uint32_t)data_fs.nslots * (uint32_t)TIKU_TFS_SLOT_DATA;
    /* The NVM's name comes from TIKU_DEVICE_NVM_LABEL in the device
     * header. */
    out->backing = TIKU_DEVICE_NVM_LABEL;
    data_fill_extents(out);
    return 0;
}

int tiku_vfs_tree_data_df(tiku_data_df_t *out)
{
    if (out == NULL || data_tfs_ensure() != 0) {
        return -1;
    }
    return data_df_mounted(out);
}

/*---------------------------------------------------------------------------*/
/* /sys/fs/data — usage that never mounts or provisions the store            */
/*---------------------------------------------------------------------------*/

/**
 * @brief /sys/fs/data/state: not-mounted, mounted, absent, refused or held.
 *
 * Reading it never mounts the store; usage stays unavailable until a file
 * operation has mounted it.
 */
static int
data_state_read(char *buf, size_t max)
{
    static const char *const states[] = {
        "not-mounted", "mounted", "absent", "refused", "held"
    };

    return snprintf(buf, max, "%s\n", states[data_state]);
}

/** @brief Define a /sys/fs/data usage handler; ENOTSUP while the store is
 *         not mounted. */
#define DATA_STAT(name, expression)                                          \
    static int data_stat_##name(char *buf, size_t max)                       \
    {                                                                        \
        tiku_data_df_t st;                                                   \
                                                                             \
        if (data_df_mounted(&st) != 0) {                                     \
            return TIKU_VFS_ENOTSUP;                                         \
        }                                                                    \
        return snprintf(buf, max, "%lu\n", (unsigned long)(expression));     \
    }

DATA_STAT(files, st.used_files)
DATA_STAT(used_bytes, st.used_bytes)
DATA_STAT(capacity_bytes, st.cap_bytes)
DATA_STAT(allocated_bytes, (uint32_t)st.used_slots * st.slot_bytes)
DATA_STAT(free_bytes,
          (uint32_t)(st.total_slots - st.used_slots) * st.slot_bytes)
#undef DATA_STAT

/** @brief /sys/fs/data/backing: the kind of NVM the store sits on. */
static int
data_backing_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_DEVICE_NVM_LABEL);
}

static const tiku_vfs_desc_t desc_fs_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_fs_bytes = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_BYTES, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_CHEAP);
static const tiku_vfs_desc_t desc_fs_count = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_COUNT, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_CHEAP);

static const tiku_vfs_node_t data_stats[] = {
    { .name = "state", .type = TIKU_VFS_FILE, .read = data_state_read,
      .desc = &desc_fs_text },
    { .name = "backing", .type = TIKU_VFS_FILE, .read = data_backing_read,
      .desc = &desc_fs_text },
    { .name = "files", .type = TIKU_VFS_FILE, .read = data_stat_files,
      .desc = &desc_fs_count },
    { .name = "used_bytes", .type = TIKU_VFS_FILE,
      .read = data_stat_used_bytes, .desc = &desc_fs_bytes },
    { .name = "capacity_bytes", .type = TIKU_VFS_FILE,
      .read = data_stat_capacity_bytes, .desc = &desc_fs_bytes },
    { .name = "allocated_bytes", .type = TIKU_VFS_FILE,
      .read = data_stat_allocated_bytes, .desc = &desc_fs_bytes },
    { .name = "free_bytes", .type = TIKU_VFS_FILE,
      .read = data_stat_free_bytes, .desc = &desc_fs_bytes },
};

const tiku_vfs_node_t tiku_vfs_tree_fs_children[TIKU_VFS_TREE_FS_NCHILD] = {
    { .name = "data", .type = TIKU_VFS_DIR, .children = data_stats,
      .child_count = sizeof(data_stats) / sizeof(data_stats[0]) },
};

/*---------------------------------------------------------------------------*/
/* PUBLIC                                                                     */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *
tiku_vfs_tree_data_get(void)
{
    return &data_node;
}

void tiku_vfs_tree_data_extents(tiku_data_df_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof *out);
        data_fill_extents(out);
    }
}

tiku_tfs_t *tiku_vfs_tree_data_store_if_mounted(void)
{
    return (data_state == DATA_READY && data_fs.mounted) ? &data_fs : NULL;
}

tiku_tfs_t *
tiku_vfs_tree_data_store(void)
{
    /* Same lazy mount the VFS nodes use; callers that want whole objects
     * (tiku_blob) work against the store rather than through path reads. */
    if (data_tfs_ensure() != 0) {
        return NULL;
    }
    return &data_fs;
}
