/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_vfs.c - VFS /proc subtree (process + driver observability).
 *
 * The dynamic sibling of /sys: the process registry, event-queue depth, the
 * catalog of startable processes and, when those drivers are built, live Wi-Fi
 * and Bluetooth status.  Node tables are regenerated on every _get() call.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_proc_vfs.h"
#include "tiku_process.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/memory/tiku_mem.h>
#include <stdio.h>

/*
 * The /proc/wifi subtree is only compiled when a wireless driver is
 * present.  The interface header lives in interfaces/wireless/
 * regardless, but the readers below call tiku_wireless_status(),
 * which only links when a driver (the CYW43439, the ESP32-C61's radio)
 * provides the implementation.  PROC_WIFI_ENABLED gates both the readers
 * and the directory entry so a radio-less build carries no dead code.
 */
#if (defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE) || \
    (defined(TIKU_DRV_WIFI_ESP_ENABLE) && TIKU_DRV_WIFI_ESP_ENABLE)
#define PROC_WIFI_ENABLED 1
#include <interfaces/wireless/tiku_wireless.h>
#else
#define PROC_WIFI_ENABLED 0
#endif

/*
 * The /proc/bt subtree mirrors /proc/wifi but pulls from the radio under
 * the BLE host stack (the CYW43's BT extension, the ESP32-C61's controller)
 * via the driver-agnostic tiku_bt API, so non-BT builds get no /proc/bt
 * directory at all.
 */
#if (defined(TIKU_DRV_WIFI_CYW43_BT_ENABLE) && TIKU_DRV_WIFI_CYW43_BT_ENABLE) \
    || (defined(TIKU_DRV_BLE_ESP_ENABLE) && TIKU_DRV_BLE_ESP_ENABLE)
#define PROC_BT_ENABLED 1
#include <interfaces/bluetooth/tiku_bt.h>
#else
#define PROC_BT_ENABLED 0
#endif

/*
 * The /proc/threads summary is present only when worker threads are compiled
 * in; it renders one line per worker slot (state, cycles, switches).
 */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#define PROC_THREADS_ENABLED 1
#include <kernel/threads/tiku_thread.h>
#else
#define PROC_THREADS_ENABLED 0
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Number of file nodes inside each per-pid directory.  Must match the count
 * written by build_pid_files() and the width of the pid_files[][] array below.
 */
#define PROC_FILES_PER_PID  9

/*
 * Maximum catalog entries that get their own VFS directory.  Bounds the catalog
 * node and reader arrays, and is tied to the catalog capacity so every slot can
 * surface under /proc/catalog.
 */
#define PROC_CATALOG_VFS_MAX  TIKU_PROCESS_CATALOG_MAX

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Static storage for the /proc VFS tree, rebuilt from scratch by
 * tiku_proc_vfs_get() on each call so it reflects the registry at that
 * moment.  The tables change at run time, so they cannot be const.
 *
 * They are TIKU_RETAINED, not TIKU_DURABLE: rebuilt on every call, they
 * gain nothing from power-cycle durability.  On MSP430 the grade is
 * MPU-protected FRAM, which keeps the tables off the small SRAM, so the
 * rebuild writes them inside an MPU unlock window; on every other port it
 * is SRAM.
 *
 * Layout (a fully populated example):
 *   proc_root ("proc", DIR)
 *     |-- "count"   (FILE)
 *     |-- "queue"   (DIR)   ->  proc_queue_children[]
 *     |-- "catalog" (DIR)   ->  catalog_children[]
 *     |-- "wifi"    (DIR)   ->  proc_wifi_children[]   (wireless driver)
 *     |-- "bt"      (DIR)   ->  proc_bt_children[]     (BT driver)
 *     |-- "threads" (FILE)                             (worker threads)
 *     |-- "0"       (DIR)   ->  pid_files[0][0..8]
 *     '-- ...               one per registered process
 */

/*
 * Backing nodes for every per-pid directory's file children; build_pid_files()
 * fills one row inside the rebuild's MPU unlock window.
 */
static TIKU_RETAINED tiku_vfs_node_t
    pid_files[TIKU_PROCESS_MAX][PROC_FILES_PER_PID];

/* count + queue + catalog, plus one slot per compiled-in optional subtree. */
#define PROC_FIXED_KIDS \
    (3 + PROC_WIFI_ENABLED + PROC_BT_ENABLED + PROC_THREADS_ENABLED)

/*
 * Child-node table for the top-level /proc directory: the fixed entries, then
 * up to one directory per registered process.  Sized for the worst case,
 * RETAINED grade, and rewritten on each _get() call.
 */
static TIKU_RETAINED tiku_vfs_node_t
    proc_children[TIKU_PROCESS_MAX + PROC_FIXED_KIDS];

/*
 * Directory names for the pid and catalog subdirectories.  String literals
 * only, so this is const and never rewritten; indexed by pid for processes and
 * reused by index for catalog entries.
 */
static const char * const pid_names[] = {
    "0", "1", "2", "3", "4", "5", "6", "7"
};

/*
 * The top-level /proc directory node handed back to the VFS, stamped at the end
 * of _get() to point at the child table with the freshly computed count.
 */
static TIKU_RETAINED tiku_vfs_node_t
    proc_root;

/*---------------------------------------------------------------------------*/
/* READER HELPERS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Each read handler needs to know which pid it serves, but the VFS read
 * signature is int(char *, size_t) with no context pointer.  A macro
 * therefore generates one handler per pid, resolved at build time for
 * zero runtime overhead.
 *
 * Every generator below but PROC_READ_PID resolves its slot with
 * tiku_process_get(idx), which returns NULL for an empty or invalid slot.
 * Each such handler renders a safe placeholder ("(none)" or "0") on NULL,
 * so a read that races a process exit never dereferences a stale pointer.
 * All output is one line terminated by '\n'.  Handlers only read, so they
 * need no MPU unlock.
 */

/*
 * Generate proc_read_name_<idx>(): backs /proc/<idx>/name.  Renders the
 * process name, "(none)" for an empty slot and "(null)" for a live slot with
 * no name string.
 */
#define PROC_READ_NAME(idx)                                                 \
    static int proc_read_name_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "(none)\n"); }           \
        return snprintf(buf, max, "%s\n", p->name ? p->name : "(null)");   \
    }

/*
 * Generate proc_read_state_<idx>(): backs /proc/<idx>/state.  Renders the
 * scheduler state name from tiku_process_state_str(), or "(none)" for an
 * empty slot.
 */
#define PROC_READ_STATE(idx)                                                \
    static int proc_read_state_##idx(char *buf, size_t max)                 \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "(none)\n"); }           \
        return snprintf(buf, max, "%s\n",                                   \
                        tiku_process_state_str(p->state));                  \
    }

/*
 * Generate proc_read_pid_<idx>(): backs /proc/<idx>/pid.  The one reader that
 * does not consult the registry -- the pid is the slot index baked in at macro
 * expansion, so it is right even for an empty slot.
 */
#define PROC_READ_PID(idx)                                                  \
    static int proc_read_pid_##idx(char *buf, size_t max)                   \
    {                                                                       \
        return snprintf(buf, max, "%d\n", idx);                             \
    }

/*
 * Generate proc_read_sram_<idx>(): backs /proc/<idx>/sram_used.  Renders
 * tiku_process_sram_used(): the declared bytes plus an attached SRAM arena's
 * usage; "0" for an empty slot.
 */
#define PROC_READ_SRAM(idx)                                                 \
    static int proc_read_sram_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%lu\n",                                 \
                        (unsigned long)tiku_process_sram_used(p));          \
    }

/**
 * Generate proc_read_fram_<idx>(): backs /proc/<idx>/fram_used.
 *
 * Renders tiku_process_fram_used(): the declared bytes plus an attached
 * non-SRAM arena's usage; "0" when the slot is empty.
 */
#define PROC_READ_FRAM(idx)                                                 \
    static int proc_read_fram_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%lu\n",\
                        (unsigned long)tiku_process_fram_used(p));                   \
    }

/*
 * Generate proc_read_uptime_<idx>(): backs /proc/<idx>/uptime.  Elapsed ticks
 * converted to whole seconds.  The difference is taken in the clock type's
 * width, so on MSP430, whose tick is 16-bit, it reads modulo 512 s at 128 Hz.
 */
#define PROC_READ_UPTIME(idx)                                               \
    static int proc_read_uptime_##idx(char *buf, size_t max)                \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        unsigned long secs;                                                 \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        secs = (unsigned long)(tiku_clock_time() - p->start_time) /         \
               TIKU_CLOCK_SECOND;                                           \
        return snprintf(buf, max, "%lu\n", secs);                           \
    }

/*
 * Generate proc_read_wake_<idx>(): backs /proc/<idx>/wake_count.  Renders how
 * many times the scheduler has dispatched this process; "0" for an empty slot.
 */
#define PROC_READ_WAKE(idx)                                                 \
    static int proc_read_wake_##idx(char *buf, size_t max)                  \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%u\n", p->wake_count);                  \
    }

/*
 * Generate proc_read_events_<idx>(): backs /proc/<idx>/events.  Walks the
 * global queue counting entries targeted at this process, so broadcasts are
 * attributed to nobody.  A live snapshot -- the queue can change between reads.
 */
#define PROC_READ_EVENTS(idx)                                               \
    static int proc_read_events_##idx(char *buf, size_t max)                \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        uint8_t len, i, cnt = 0;                                            \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        len = tiku_process_queue_length();                                  \
        for (i = 0; i < len; i++) {                                         \
            struct tiku_process *tgt = NULL;                                 \
            tiku_process_queue_peek(i, NULL, &tgt);                         \
            if (tgt == p) { cnt++; }                                        \
        }                                                                   \
        return snprintf(buf, max, "%u\n", cnt);                             \
    }

/*
 * Generate proc_read_restart_<idx>(): backs /proc/<idx>/restarts -- how many
 * times supervision has restarted this process (tiku_process_restarts).  "0"
 * when the slot is empty or the process is unsupervised.
 */
#define PROC_READ_RESTART(idx)                                              \
    static int proc_read_restart_##idx(char *buf, size_t max)               \
    {                                                                       \
        struct tiku_process *p = tiku_process_get(idx);                     \
        if (p == NULL) { return snprintf(buf, max, "0\n"); }               \
        return snprintf(buf, max, "%u\n", tiku_process_restarts(p));        \
    }

/*
 * Emit the full set of per-pid read handlers for one slot, expanded once per
 * pid below; each expansion defines every proc_read_*_idx via the generators
 * above.
 */
#define PROC_READERS(idx)                                                   \
    PROC_READ_NAME(idx)                                                     \
    PROC_READ_STATE(idx)                                                    \
    PROC_READ_PID(idx)                                                      \
    PROC_READ_SRAM(idx)                                                     \
    PROC_READ_FRAM(idx)                                                     \
    PROC_READ_UPTIME(idx)                                                   \
    PROC_READ_WAKE(idx)                                                     \
    PROC_READ_EVENTS(idx)                                                   \
    PROC_READ_RESTART(idx)

/* Generate reader functions for all 8 pid slots (TIKU_PROCESS_MAX) */
PROC_READERS(0)
PROC_READERS(1)
PROC_READERS(2)
PROC_READERS(3)
PROC_READERS(4)
PROC_READERS(5)
PROC_READERS(6)
PROC_READERS(7)

/*
 * Per-pid bundle of read-handler pointers, one field per file under
 * /proc/<pid>/.  build_pid_files() copies these into the node table, which is
 * why the generated handlers are gathered into an indexable struct.
 */
typedef struct {
    tiku_vfs_read_fn name;
    tiku_vfs_read_fn state;
    tiku_vfs_read_fn pid;
    tiku_vfs_read_fn sram;
    tiku_vfs_read_fn fram;
    tiku_vfs_read_fn uptime;
    tiku_vfs_read_fn wake;
    tiku_vfs_read_fn events;
    tiku_vfs_read_fn restart;
} proc_readers_t;

/**
 * Build a proc_readers_t initialiser wiring slot idx's handlers.
 *
 * Pairs each struct field with the matching proc_read_*_idx function
 * emitted by PROC_READERS(idx), and populates readers[] below.
 */
#define READERS_ENTRY(idx)  {                                               \
    proc_read_name_##idx,                                                   \
    proc_read_state_##idx,                                                  \
    proc_read_pid_##idx,                                                    \
    proc_read_sram_##idx,                                                   \
    proc_read_fram_##idx,                                                   \
    proc_read_uptime_##idx,                                                 \
    proc_read_wake_##idx,                                                   \
    proc_read_events_##idx,                                                 \
    proc_read_restart_##idx                                                 \
}

/*
 * Per-pid reader lookup table, indexed by pid; const and flash-resident.  The
 * pid-to-handler binding is fixed at build time and only which rows get a
 * directory varies at run time.
 */
static const proc_readers_t readers[TIKU_PROCESS_MAX] = {
    READERS_ENTRY(0), READERS_ENTRY(1), READERS_ENTRY(2), READERS_ENTRY(3),
    READERS_ENTRY(4), READERS_ENTRY(5), READERS_ENTRY(6), READERS_ENTRY(7)
};

/*---------------------------------------------------------------------------*/
/* /proc/count READER                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /proc/count.
 *
 * Renders the number of registered processes, stopped ones included,
 * as a decimal line ("3\n").  Comes from tiku_process_count(), which
 * counts non-empty registry slots.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_read_count(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_count());
}

/*---------------------------------------------------------------------------*/
/* /proc/queue READERS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /proc/queue/length.
 *
 * Renders how many events are pending in the global queue.  A persistently high
 * value against /proc/queue/space points at a process not draining its events.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_read_queue_length(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_queue_length());
}

/**
 * @brief Read handler for /proc/queue/space.
 *
 * Renders the number of free slots remaining in the global event
 * queue as a decimal line.  "0\n" means the queue is full and the
 * next tiku_process_post() will be dropped.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_read_queue_space(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_queue_space());
}

/**
 * @brief Read handler for /proc/queue/dropped.
 *
 * The lifetime count of events refused because the queue or the user budget was
 * full.  A growing value is the tell for overflow bugs that are otherwise
 * silent, since a failed post returns 0 and most callers ignore it.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_read_queue_dropped(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_queue_dropped());
}

/**
 * /proc/queue directory table — event-queue depth views.
 *
 * const, flash-resident, referenced from proc_children[] as the
 * "queue" directory's children in tiku_proc_vfs_get().
 */
static const tiku_vfs_node_t proc_queue_children[] = {
    { "length",  TIKU_VFS_FILE, proc_read_queue_length,  NULL, NULL, 0 },
    { "space",   TIKU_VFS_FILE, proc_read_queue_space,   NULL, NULL, 0 },
    { "dropped", TIKU_VFS_FILE, proc_read_queue_dropped, NULL, NULL, 0 },
};

/*---------------------------------------------------------------------------*/
/* /proc/wifi READERS                                                        */
/*---------------------------------------------------------------------------*/
/*
 * Each reader below snapshots tiku_wireless_status() on demand and
 * projects a single field into text.  The status call copies the
 * driver's cached state, so reading several wifi files back-to-back
 * is fine — there is no shared cached snapshot.
 * When the radio is down (status returns non-zero) or not joined,
 * every reader emits a benign placeholder ("down", "0" or an empty
 * line) so callers never have to special-case the offline state.
 */
#if PROC_WIFI_ENABLED

/**
 * @brief Read handler for /proc/wifi/mac.
 *
 * Renders the local radio MAC as lowercase colon-separated hex plus
 * newline ("28:cd:c1:00:11:22\n").  "down\n" when the radio is not
 * reachable (tiku_wireless_status() returns non-zero).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_mac(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    if (tiku_wireless_status(&st) != 0) return snprintf(buf, max, "down\n");
    return snprintf(buf, max, "%02x:%02x:%02x:%02x:%02x:%02x\n",
                    st.mac[0], st.mac[1], st.mac[2],
                    st.mac[3], st.mac[4], st.mac[5]);
}

/**
 * @brief Read handler for /proc/wifi/link.
 *
 * Renders the association state as a word: idle, connecting, joined, failed, or
 * unknown for an unrecognised code.  A failed status call, or idle with the up
 * flag clear, renders "down".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_link(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    const char *s = "down";
    if (tiku_wireless_status(&st) == 0) {
        switch (st.link_state) {
        case TIKU_WIRELESS_LINK_IDLE:       s = st.up ? "idle" : "down"; break;
        case TIKU_WIRELESS_LINK_CONNECTING: s = "connecting";            break;
        case TIKU_WIRELESS_LINK_JOINED:     s = "joined";                break;
        case TIKU_WIRELESS_LINK_FAILED:     s = "failed";                break;
        default:                            s = "unknown";               break;
        }
    }
    return snprintf(buf, max, "%s\n", s);
}

/**
 * @brief Read handler for /proc/wifi/ssid.
 *
 * Only produced while joined with a non-zero length, since a stored SSID means
 * nothing otherwise.  The on-air SSID is not NUL-terminated and may hold
 * arbitrary bytes, so it is copied bounded with non-printables replaced.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_ssid(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    char ssid[33];
    uint8_t i, n;
    if (tiku_wireless_status(&st) != 0
        || st.link_state != TIKU_WIRELESS_LINK_JOINED
        || st.joined_ssid_len == 0U) {
        return snprintf(buf, max, "\n");
    }
    n = st.joined_ssid_len;
    if (n > 32U) n = 32U;
    for (i = 0U; i < n; ++i) {
        char c = (char)st.joined_ssid[i];
        ssid[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    ssid[n] = '\0';
    return snprintf(buf, max, "%s\n", ssid);
}

/**
 * @brief Read handler for /proc/wifi/rssi.
 *
 * The joined AP's signal strength in dBm.  Reads 0 when the status call fails
 * or the link is not joined, RSSI being polled only while associated -- 0 is
 * also the not-yet-polled sentinel.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_rssi(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    if (tiku_wireless_status(&st) != 0
        || st.link_state != TIKU_WIRELESS_LINK_JOINED) {
        return snprintf(buf, max, "0\n");
    }
    return snprintf(buf, max, "%d\n", (int)st.rssi_dbm);
}

/**
 * @brief Read handler for /proc/wifi/last_scan_ms.
 *
 * The last completed scan's duration in milliseconds, converted from the ticks
 * the driver records.  Reads 0 when the status call fails or no scan has
 * finished.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_last_scan_ms(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    unsigned long ms;
    if (tiku_wireless_status(&st) != 0) return snprintf(buf, max, "0\n");
    ms = (unsigned long)((st.last_scan_ticks * 1000UL) / TIKU_CLOCK_SECOND);
    return snprintf(buf, max, "%lu\n", ms);
}

/**
 * @brief Read handler for /proc/wifi/last_join_ms.
 *
 * Renders the duration of the last completed join attempt in
 * milliseconds, mirroring last_scan_ms but using last_join_ticks.
 * "0\n" when the status call fails or no join has completed yet.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_wifi_read_last_join_ms(char *buf, size_t max)
{
    tiku_wireless_status_t st;
    unsigned long ms;
    if (tiku_wireless_status(&st) != 0) return snprintf(buf, max, "0\n");
    ms = (unsigned long)((st.last_join_ticks * 1000UL) / TIKU_CLOCK_SECOND);
    return snprintf(buf, max, "%lu\n", ms);
}

/*
 * /proc/wifi directory table: live, read-only radio status views.  const and
 * flash-resident, built only with the driver, and attached in _get() where its
 * entry count is derived with sizeof.
 */
static const tiku_vfs_node_t proc_wifi_children[] = {
    { "mac",          TIKU_VFS_FILE, proc_wifi_read_mac,          NULL, NULL, 0 },
    { "link",         TIKU_VFS_FILE, proc_wifi_read_link,         NULL, NULL, 0 },
    { "ssid",         TIKU_VFS_FILE, proc_wifi_read_ssid,         NULL, NULL, 0 },
    { "rssi",         TIKU_VFS_FILE, proc_wifi_read_rssi,         NULL, NULL, 0 },
    { "last_scan_ms", TIKU_VFS_FILE, proc_wifi_read_last_scan_ms, NULL, NULL, 0 },
    { "last_join_ms", TIKU_VFS_FILE, proc_wifi_read_last_join_ms, NULL, NULL, 0 },
};
#endif /* PROC_WIFI_ENABLED */

/*---------------------------------------------------------------------------*/
/* /proc/bt READERS                                                          */
/*---------------------------------------------------------------------------*/

#if PROC_BT_ENABLED

/**
 * @brief Read handler for /proc/bt/bd_addr.
 *
 * The controller's Bluetooth address as lowercase colon-separated hex.  The
 * bytes come back in display order, so the printed order matches what vendors
 * quote.  Reads "?" when the subsystem is not up.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_bd_addr(char *buf, size_t max)
{
    uint8_t mac[6];
    if (tiku_bt_addr(mac) != 0) return snprintf(buf, max, "?\n");
    return snprintf(buf, max,
                    "%02x:%02x:%02x:%02x:%02x:%02x\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Read handler for /proc/bt/ready.
 *
 * Renders "1\n" once BT bring-up has completed and the stack is ready
 * for HCI traffic (tiku_bt_is_ready()), "0\n" before that.  The
 * bd_addr/version nodes only return real data once this reads "1".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_ready(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_bt_is_ready() ? 1U : 0U);
}

/**
 * @brief Read handler for /proc/bt/advertising.
 *
 * Renders "1\n" while LE advertising is enabled, "0\n" otherwise
 * (tiku_bt_is_advertising()).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_advertising(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_bt_is_advertising() ? 1U : 0U);
}

/**
 * @brief Read handler for /proc/bt/scanning.
 *
 * Renders "1\n" while an LE scan is running, "0\n" otherwise
 * (tiku_bt_is_scanning()).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_scanning(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_bt_is_scanning() ? 1U : 0U);
}

/**
 * @brief Read handler for /proc/bt/scan_count.
 *
 * Renders the number of distinct devices currently in the scan-results
 * cache as a decimal line (tiku_bt_scan_count(); each BD_ADDR appears
 * at most once).  Cleared whenever a new scan starts.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_scan_count(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_bt_scan_count());
}

/**
 * @brief Read handler for /proc/bt/connections.
 *
 * Renders the number of active LE links as a decimal line
 * (tiku_bt_connection_count(); 0..TIKU_BT_CONN_MAX).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_connections(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_bt_connection_count());
}

/**
 * @brief Read handler for /proc/bt/version.
 *
 * The controller version cached at bring-up -- HCI, LMP and manufacturer id
 * from Read_Local_Version_Information.  Reads "?" when the subsystem is not up.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_bt_read_version(char *buf, size_t max)
{
    tiku_bt_version_t v;
    if (tiku_bt_local_version(&v) != 0) {
        return snprintf(buf, max, "?\n");
    }
    return snprintf(buf, max, "HCI=%u LMP=%u mfr=0x%04x\n",
                    v.hci_version, v.lmp_version, v.manufacturer);
}

/*
 * /proc/bt directory table: live, read-only Bluetooth status views.  const and
 * flash-resident, built only with the driver, and attached in _get() where its
 * entry count is derived with sizeof.
 */
static const tiku_vfs_node_t proc_bt_children[] = {
    { "bd_addr",     TIKU_VFS_FILE, proc_bt_read_bd_addr,     NULL, NULL, 0 },
    { "ready",       TIKU_VFS_FILE, proc_bt_read_ready,       NULL, NULL, 0 },
    { "advertising", TIKU_VFS_FILE, proc_bt_read_advertising, NULL, NULL, 0 },
    { "scanning",    TIKU_VFS_FILE, proc_bt_read_scanning,    NULL, NULL, 0 },
    { "scan_count",  TIKU_VFS_FILE, proc_bt_read_scan_count,  NULL, NULL, 0 },
    { "connections", TIKU_VFS_FILE, proc_bt_read_connections, NULL, NULL, 0 },
    { "version",     TIKU_VFS_FILE, proc_bt_read_version,     NULL, NULL, 0 },
};
#endif /* PROC_BT_ENABLED */

/*---------------------------------------------------------------------------*/
/* /proc/catalog READERS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /proc/catalog/count.
 *
 * Renders the number of catalog entries — processes advertised as
 * startable but not necessarily running — as a decimal line
 * (tiku_process_catalog_count()).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int proc_read_catalog_count(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_process_catalog_count());
}

/*
 * Generate proc_read_catname_<idx>(): backs /proc/catalog/<idx>/name.  Mirrors
 * the pid name reader but resolves through the catalog, which returns NULL past
 * the populated count: "(none)" then, "(null)" for a populated nameless entry.
 */
#define PROC_READ_CATALOG_NAME(idx)                                         \
    static int proc_read_catname_##idx(char *buf, size_t max)               \
    {                                                                       \
        const tiku_process_catalog_entry_t *e =                             \
            tiku_process_catalog_get(idx);                                  \
        if (e == NULL) { return snprintf(buf, max, "(none)\n"); }          \
        return snprintf(buf, max, "%s\n", e->name ? e->name : "(null)");   \
    }

PROC_READ_CATALOG_NAME(0)
PROC_READ_CATALOG_NAME(1)
PROC_READ_CATALOG_NAME(2)
PROC_READ_CATALOG_NAME(3)
PROC_READ_CATALOG_NAME(4)
PROC_READ_CATALOG_NAME(5)
PROC_READ_CATALOG_NAME(6)
PROC_READ_CATALOG_NAME(7)

/*
 * Catalog-name reader lookup table, indexed by catalog slot; const and
 * flash-resident.  _get() reads it to wire the name file inside each catalog
 * entry directory.
 */
static const tiku_vfs_read_fn catalog_name_readers[PROC_CATALOG_VFS_MAX] = {
    proc_read_catname_0, proc_read_catname_1,
    proc_read_catname_2, proc_read_catname_3,
    proc_read_catname_4, proc_read_catname_5,
    proc_read_catname_6, proc_read_catname_7,
};

/*
 * Backing nodes for each catalog entry's file children.  The inner dimension is
 * 1 because an entry currently exposes only its name.  RETAINED grade, written
 * inside the _get() unlock window.
 */
static TIKU_RETAINED tiku_vfs_node_t
    catalog_entry_files[PROC_CATALOG_VFS_MAX][1];

/*
 * Child-node table for /proc/catalog: slot 0 is the count file, the rest hold
 * one directory per populated entry.  RETAINED grade, rebuilt on each _get()
 * call.
 */
static TIKU_RETAINED tiku_vfs_node_t
    catalog_children[1 + PROC_CATALOG_VFS_MAX];

/* Catalog entry directory names are reused from pid_names[] above. */

/*---------------------------------------------------------------------------*/
/* TREE BUILDER                                                              */
/*---------------------------------------------------------------------------*/

#if PROC_THREADS_ENABLED
/**
 * @brief Read handler for /proc/threads: one line per occupied worker slot.
 *
 * Each line is "<slot> <state> <cycles> <switches>", state being one of
 * unused|ready|run|done|blocked; "none" when no slot is occupied.
 */
static int proc_threads_read(char *buf, size_t max)
{
    /* Indexed by tiku_thread_state_t. */
    static const char *const st[] = {
        "unused", "ready", "run", "done", "blocked"
    };
    uint8_t i, cnt = tiku_thread_count();
    int n = 0;

    for (i = 0; i < cnt; i++) {
        tiku_thread_t *t = tiku_thread_get(i);
        size_t room = ((size_t)n < max) ? (max - (size_t)n) : 0u;
        unsigned s;
        if (t == (tiku_thread_t *)0) {
            continue;
        }
        s = (unsigned)tiku_thread_state(t);
        n += snprintf(buf + n, room, "%u %s %llu %u\n", (unsigned)i,
                      (s < sizeof st / sizeof st[0]) ? st[s] : "unknown",
                      (unsigned long long)tiku_thread_cycles(t),
                      (unsigned)tiku_thread_switches(t));
    }
    if (n == 0) {
        n = snprintf(buf, max, "none\n");
    }
    return n;
}
#endif /* PROC_THREADS_ENABLED */

/**
 * @brief Fill the file nodes for a single pid's /proc directory.
 *
 * Stamps each file node wired to the matching handler; the order here is the
 * order they appear under /proc/<idx>/ and stays in step with
 * PROC_FILES_PER_PID.
 *
 * @param idx  Process slot index (0..TIKU_PROCESS_MAX-1)
 * @note On MSP430 the table is MPU-protected FRAM: call inside the caller's
 *       unlock window.
 */
static void build_pid_files(uint8_t idx)
{
    tiku_vfs_node_t *f = pid_files[idx];

    f[0] = (tiku_vfs_node_t){
        "name",       TIKU_VFS_FILE, readers[idx].name,   NULL, NULL, 0};
    f[1] = (tiku_vfs_node_t){
        "state",      TIKU_VFS_FILE, readers[idx].state,  NULL, NULL, 0};
    f[2] = (tiku_vfs_node_t){
        "pid",        TIKU_VFS_FILE, readers[idx].pid,    NULL, NULL, 0};
    f[3] = (tiku_vfs_node_t){
        "sram_used",  TIKU_VFS_FILE, readers[idx].sram,   NULL, NULL, 0};
    f[4] = (tiku_vfs_node_t){
        "fram_used",  TIKU_VFS_FILE, readers[idx].fram,   NULL, NULL, 0};
    f[5] = (tiku_vfs_node_t){
        "uptime",     TIKU_VFS_FILE, readers[idx].uptime, NULL, NULL, 0};
    f[6] = (tiku_vfs_node_t){
        "wake_count", TIKU_VFS_FILE, readers[idx].wake,   NULL, NULL, 0};
    f[7] = (tiku_vfs_node_t){
        "events",     TIKU_VFS_FILE, readers[idx].events, NULL, NULL, 0};
    f[8] = (tiku_vfs_node_t){
        "restarts",   TIKU_VFS_FILE, readers[idx].restart, NULL, NULL, 0};
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build and return the live /proc directory node.
 *
 * Rebuilds the whole subtree on every call, so it mirrors registry, catalog and
 * driver state at that instant.  On MSP430 the node tables are MPU-protected
 * FRAM, so the rebuild runs inside one unlock bracket.
 *
 * @return Pointer to the freshly rebuilt "proc" VFS directory node
 */
const tiku_vfs_node_t *tiku_proc_vfs_get(void)
{
    uint8_t i;
    uint8_t child_idx = 0;
    uint8_t cat_child_idx = 0;
    uint8_t cat_count;
    uint16_t mpu_state;

    /* On MSP430 the tables are MPU-protected FRAM, where a write outside
     * an unlock window is dropped and the tables keep their previous
     * contents.  The whole rebuild runs inside one window, and the previous
     * protection state is restored on exit. */
    mpu_state = tiku_mpu_unlock_nvm();

    /* /proc/count */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "count", TIKU_VFS_FILE, proc_read_count, NULL, NULL, 0
    };

    /* /proc/queue/ */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "queue", TIKU_VFS_DIR, NULL, NULL, proc_queue_children, 3
    };

    /* /proc/catalog/ — build entries for each catalog slot */
    cat_count = tiku_process_catalog_count();
    catalog_children[cat_child_idx++] = (tiku_vfs_node_t){
        "count", TIKU_VFS_FILE, proc_read_catalog_count, NULL, NULL, 0
    };
    for (i = 0; i < cat_count && i < PROC_CATALOG_VFS_MAX; i++) {
        catalog_entry_files[i][0] = (tiku_vfs_node_t){
            "name", TIKU_VFS_FILE, catalog_name_readers[i], NULL, NULL, 0
        };
        catalog_children[cat_child_idx++] = (tiku_vfs_node_t){
            pid_names[i], TIKU_VFS_DIR, NULL, NULL,
            catalog_entry_files[i], 1
        };
    }
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "catalog", TIKU_VFS_DIR, NULL, NULL,
        catalog_children, cat_child_idx
    };

#if PROC_WIFI_ENABLED
    /* /proc/wifi/{mac,link,ssid,rssi,last_scan_ms,last_join_ms}.
     * Snapshot-style files: each read calls tiku_wireless_status()
     * and projects one field. */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "wifi", TIKU_VFS_DIR, NULL, NULL, proc_wifi_children,
        sizeof(proc_wifi_children) / sizeof(proc_wifi_children[0])
    };
#endif

#if PROC_BT_ENABLED
    /* /proc/bt/{bd_addr,ready,advertising,scanning,scan_count,
     *           connections,version}. Each read calls a getter on
     * the BT driver and renders one field. */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "bt", TIKU_VFS_DIR, NULL, NULL, proc_bt_children,
        sizeof(proc_bt_children) / sizeof(proc_bt_children[0])
    };
#endif

#if PROC_THREADS_ENABLED
    /* /proc/threads -- worker-thread summary (state, cycles, switches). */
    proc_children[child_idx++] = (tiku_vfs_node_t){
        "threads", TIKU_VFS_FILE, proc_threads_read, NULL, NULL, 0
    };
#endif

    /* One directory per registered process */
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (tiku_process_get((int8_t)i) != NULL) {
            build_pid_files(i);
            proc_children[child_idx++] = (tiku_vfs_node_t){
                pid_names[i], TIKU_VFS_DIR, NULL, NULL,
                pid_files[i], PROC_FILES_PER_PID
            };
        }
    }

    proc_root = (tiku_vfs_node_t){
        "proc", TIKU_VFS_DIR, NULL, NULL, proc_children, child_idx
    };

    tiku_mpu_lock_nvm(mpu_state);

    return &proc_root;
}

/**
 * @brief Return the current number of children under /proc.
 *
 * The /proc directory contains fixed nodes (count, queue, catalog,
 * plus wifi, bt and threads when built) plus one subdirectory per
 * registered process.
 *
 * @return Total child count (PROC_FIXED_KIDS + registered processes).
 */
uint8_t tiku_proc_vfs_child_count(void)
{
    return PROC_FIXED_KIDS + tiku_process_count();
}
