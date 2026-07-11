/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_sys.c - /sys subtree (files + assembly)
 *
 * Two roles in one module.  First, the /sys content that is too
 * small to deserve its own file:
 *
 *   /sys/version         OS version string
 *   /sys/uptime          seconds since boot
 *   /sys/time            (rw) wall-clock seconds since epoch
 *   /sys/device/name     (rw) FRAM-persisted device name
 *   /sys/device/id       stable per-chip ID from the unique-ID ROM
 *   /sys/device/mcu      silicon name ("MSP430FR5969", ...)
 *   /sys/device/version  OS version (alias of /sys/version)
 *   /sys/mem/{sram,nvm}  configured memory sizes
 *   /sys/mem/free        live stack headroom (SP - _end)
 *   /sys/mem/used        sum of per-process SRAM accounting
 *   /sys/cpu/freq        configured CPU frequency
 *   /sys/sched/idle      scheduler idle-loop counter
 *
 * Second, the /sys directory table itself: the boot_count /
 * last_reset / cold_boots files come from the boot module's
 * exported handlers, and the boot/timer/clock/watchdog/htimer/
 * power/init directories come from the sibling modules' exported
 * children tables + NCHILD count macros.  Adding a new /sys entry
 * means adding one initialiser to sys_children below (plus, for a
 * new subtree, including its header).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_sys.h"
#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_crypto_arch.h>   /* /sys/crypto mode + counters */
#endif
#if (TIKU_HAS_BLE_ADV + 0)
#include <interfaces/bluetooth/tiku_ble_adv.h>  /* /sys/radio beacon + scan */
#endif
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>         /* /sys/flpr coprocessor    */
#include <arch/nordic/flpr/tiku_flpr_ipc.h>     /* TIKU_FLPR_MSG_CAP        */
#include <arch/nordic/tiku_device_select.h>     /* NRF_VPR00_NS readbacks   */
#endif
#include "tiku_vfs_tree_boot.h"
#include <kernel/cpu/tiku_stack.h>   /* stack high-water for /sys/mem/stack_free */
#include "tiku_vfs_tree_timer.h"
#include "tiku_vfs_tree_watchdog.h"
#include "tiku_vfs_tree_power.h"
#include "tiku_vfs_tree_persist.h"
#include "tiku_vfs_tree_watch.h"
#include "tiku_vfs_tree_inittab.h"
#if TIKU_SHELL_ENABLE
#include <kernel/shell/tiku_shell_rules.h>   /* /sys/rules/* observability */
#include <kernel/shell/tiku_shell_jobs.h>    /* /sys/jobs/* observability  */
#endif
#include "tiku.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_rtc.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/scheduler/tiku_sched.h>
#include <stdio.h>
#if (TIKU_HAS_BLE_ADV + 0)
#include <stdlib.h>                  /* strtoul: /sys/radio/beacon interval */
#include <string.h>                  /* strchr/strcmp: beacon write parse   */
#endif

/*---------------------------------------------------------------------------*/
/* /sys/uptime                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/uptime.
 *
 * Renders seconds since boot as a decimal line ("3600\n" after an
 * hour).
 *
 * tiku_clock_seconds() is the 32-bit running counter. The tick
 * value tiku_clock_time() is 16 bits and wraps every ~8.5 minutes
 * at 128 Hz, so we cannot derive seconds from it directly.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
uptime_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_clock_seconds());
}

/*---------------------------------------------------------------------------*/
/* /sys/time — wall-clock seconds since epoch                                */
/*---------------------------------------------------------------------------*/

/*
 * Read returns the current wall-clock seconds. Write accepts a
 * decimal seconds-since-epoch value and rebases the offset so that
 * subsequent reads are consistent. Both sit on top of
 * tiku_rtc_*(), which keeps the persistent epoch offset and the
 * MPU-unlock handshake out of the VFS handler.
 *
 * Format choice: plain decimal. Easy to parse from a shell script,
 * easy to feed back in (`write /sys/time $(date +%s)`).
 */

/**
 * @brief Read handler for /sys/time.
 *
 * Renders the wall-clock time as decimal seconds since the Unix
 * epoch ("1750000000\n").  Before anyone sets the time the value
 * is just uptime plus the persisted offset (0 on a fresh FRAM),
 * so a small number means "clock was never set".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
time_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_rtc_get_seconds());
}

/**
 * @brief Write handler for /sys/time.
 *
 * Parses a strict decimal seconds-since-epoch value, terminated by
 * newline, carriage return, NUL or end of input.  Any non-digit
 * before the terminator rejects the whole write (-1) — no partial
 * parses, so a malformed script line cannot half-set the clock.
 * The accepted value is handed to tiku_rtc_set_seconds(), which
 * rebases and persists the epoch offset.
 *
 * @param buf  Input text, decimal digits ("1750000000\n")
 * @param len  Input length in bytes
 * @return 0 on success, -1 on empty or malformed input
 */
static int
time_write(const char *buf, size_t len)
{
    uint32_t v = 0;
    size_t   i;
    int      seen_digit = 0;

    for (i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || c == '\0') {
            break;
        }
        if (c < '0' || c > '9') {
            return TIKU_VFS_EINVAL;
        }
        if (v > (UINT32_MAX - (uint32_t)(c - '0')) / 10u) {
            return TIKU_VFS_ERANGE;   /* would overflow the 32-bit seconds field */
        }
        v = v * 10U + (uint32_t)(c - '0');
        seen_digit = 1;
    }
    if (!seen_digit) {
        return TIKU_VFS_EINVAL;
    }
    tiku_rtc_set_seconds(v);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/sram, /sys/mem/nvm                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/mem/sram.
 *
 * Renders the device's total SRAM size in bytes as a decimal line
 * ("2048\n" on FR5969).  This is the silicon constant from the
 * device header, not a live measurement — see /sys/mem/free for
 * runtime headroom.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
sram_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_DEVICE_RAM_SIZE);
}

/**
 * @brief Read handler for /sys/mem/nvm.
 *
 * Renders the device's total non-volatile memory (FRAM on MSP430,
 * flash on RP2350) in bytes as a decimal line.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
nvm_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_DEVICE_FRAM_SIZE);
}

/**
 * @brief Read handler for /sys/mem/nvmfree.
 *
 * Free bytes in the NVM memory tier (capacity - used). On Ambiq this is the
 * carved, memory-mapped MRAM region; on MSP430 the FRAM NVM pool. Reports 0 if
 * the tier is unavailable (e.g. no region backend on the board yet).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
nvmfree_read(char *buf, size_t max)
{
    tiku_mem_stats_t st;
    if (tiku_tier_stats(TIKU_MEM_NVM, &st) != TIKU_MEM_OK) {
        return snprintf(buf, max, "0\n");
    }
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)(st.total_bytes - st.used_bytes));
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/tiers + /sys/mem/failed — measured tier-allocator truth           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/mem/tiers.
 *
 * One line per available tier with MEASURED bump-allocator state
 * (capacity, used, lifetime peak, carve count, refused carves) straight
 * from tiku_tier_stats().  Unlike /sys/mem/used — which sums what
 * processes self-declare — these numbers cannot drift from reality.
 * Unavailable tiers (e.g. HIFRAM on parts without one) are omitted.
 */
static int
mem_tiers_read(char *buf, size_t max)
{
    static const struct { tiku_mem_tier_t t; const char *name; } tiers[] = {
        { TIKU_MEM_SRAM,   "sram"   },
        { TIKU_MEM_NVM,    "nvm"    },
        { TIKU_MEM_HIFRAM, "hifram" },
    };
    tiku_mem_stats_t st;
    size_t off = 0;
    uint8_t i;
    int n;

    for (i = 0; i < (uint8_t)(sizeof(tiers) / sizeof(tiers[0])); i++) {
        if (tiku_tier_stats(tiers[i].t, &st) != TIKU_MEM_OK) {
            continue;
        }
        n = snprintf(buf + off, (off < max) ? max - off : 0,
                     "%s total=%lu used=%lu peak=%lu allocs=%lu fail=%lu\n",
                     tiers[i].name,
                     (unsigned long)st.total_bytes,
                     (unsigned long)st.used_bytes,
                     (unsigned long)st.peak_bytes,
                     (unsigned long)st.alloc_count,
                     (unsigned long)st.fail_count);
        if (n < 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return (int)off;
}

/**
 * @brief Read handler for /sys/mem/failed.
 *
 * Total refused carve requests across all tiers since boot.  Non-zero
 * means something asked for memory it did not get — OOM pressure that
 * was previously invisible (the caller saw only a NULL).
 */
static int
mem_failed_read(char *buf, size_t max)
{
    static const tiku_mem_tier_t all[] = {
        TIKU_MEM_SRAM, TIKU_MEM_NVM, TIKU_MEM_HIFRAM
    };
    tiku_mem_stats_t st;
    unsigned long total = 0;
    uint8_t i;

    for (i = 0; i < (uint8_t)(sizeof(all) / sizeof(all[0])); i++) {
        if (tiku_tier_stats(all[i], &st) == TIKU_MEM_OK) {
            total += (unsigned long)st.fail_count;
        }
    }
    return snprintf(buf, max, "%lu\n", total);
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/free — live stack headroom                                        */
/*---------------------------------------------------------------------------*/

/** Linker symbols bounding the gap the stack grows into: _end is
 *  the top of static data, __stack the initial stack pointer. */
extern char _end;
extern char __stack;

/**
 * @brief Read handler for /sys/mem/free.
 *
 * Renders the live gap between the current stack pointer and the
 * end of static data, in bytes — i.e. how much deeper the stack
 * could grow right now before colliding with .bss/.data.  Reads
 * the SP register directly via inline asm, so the answer reflects
 * the call depth at the moment of the read (the VFS path itself
 * costs a few frames).
 *
 * "0\n" means the SP is at or below _end — the stack has already
 * met (or overflowed into) static data and a crash is imminent or
 * past.  Platform branches: MSP430 reads the 16-bit R1, Cortex-M
 * the 32-bit SP; host builds report 0.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
mem_free_read(char *buf, size_t max)
{
#if defined(PLATFORM_MSP430)
    /* MSP430: 16-bit SP via R1. */
    uint16_t sp;
    uint16_t end_addr = (uint16_t)(uintptr_t)&_end;
    __asm__ volatile ("mov r1, %0" : "=r"(sp));
    if (sp > end_addr) {
        return snprintf(buf, max, "%u\n", sp - end_addr);
    }
    return snprintf(buf, max, "0\n");
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
    /* Cortex-M: 32-bit SP. The stack grows down from __stack toward
     * _end; live free space is (SP - _end). */
    uintptr_t sp;
    uintptr_t end_addr = (uintptr_t)&_end;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    if (sp > end_addr) {
        return snprintf(buf, max, "%lu\n",
                        (unsigned long)(sp - end_addr));
    }
    return snprintf(buf, max, "0\n");
#else
    /* Host fallback: report 0. */
    (void)max;
    return snprintf(buf, max, "0\n");
#endif
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/used — sum of per-process SRAM allocation                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/mem/used.
 *
 * Renders the sum of every registered process's SRAM use as a decimal
 * line: each process's declared proc-mem footprint plus its measured
 * live allocation (e.g. the BASIC arena), via tiku_process_sram_used().
 * Kernel statics and the stack are not included.  Walks the registry by
 * pid; empty slots return NULL from tiku_process_get() and are skipped.
 *
 * The accumulator is 32-bit: tiku_process_sram_used() returns a uint32_t
 * (a BASIC arena alone is hundreds of KB on the big-RAM parts), so a
 * 16-bit sum here would wrap modulo 65536 and under-report.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
mem_used_read(char *buf, size_t max)
{
    uint32_t total = 0;
    uint8_t i;
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        struct tiku_process *p = tiku_process_get((int8_t)i);
        if (p != NULL) {
            total += tiku_process_sram_used(p);
        }
    }
    return snprintf(buf, max, "%lu\n", (unsigned long)total);
}

/*---------------------------------------------------------------------------*/
/* /sys/sched/idle                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/sched/idle.
 *
 * Renders the scheduler's idle counter as a decimal line — the
 * number of times the main loop found no runnable process and
 * dropped into the configured sleep mode.  The growth rate
 * between two reads is a cheap duty-cycle estimate: fast growth
 * means the system is mostly asleep (good for the power budget).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
sched_idle_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_sched_idle_count());
}

/*---------------------------------------------------------------------------*/
/* /sys/version                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/version.
 *
 * Renders the TIKU_VERSION string from tiku.h ("0.05\n").  Also
 * exposed as /sys/device/version for clients that read the whole
 * device directory in one sweep.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
version_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_VERSION);
}

/*---------------------------------------------------------------------------*/
/* /sys/device/{name,id,mcu,version}                                         */
/*---------------------------------------------------------------------------*/

/*
 * Device name: FRAM-backed user-set string, declared as a persist
 * cell with its own gate.  Before the cell conversion it was primed
 * off the boot module's shared magic word via a first_boot flag;
 * the cell gives it independent validity and removes that coupling.
 */

/**
 * Maximum stored device-name length, excluding the terminator.
 * Sized to fit a typical mDNS hostname; longer writes are silently
 * truncated to this length rather than rejected.
 */
#define DEVICE_NAME_MAX  31

/** Gate key for the device-name cell ('NAME') */
#define DEVICE_NAME_MAGIC  0x4E414D45UL /* 'NAME' */

/** FRAM cell: NUL-terminated user-visible device name */
static char __attribute__((section(".persistent")))
    device_name_persist[DEVICE_NAME_MAX + 1];

/** Gate + descriptor: primed to the default name "tiku" */
TIKU_PERSIST_CELL(device_name_cell, device_name_persist,
                  DEVICE_NAME_MAGIC, "tiku", 5);

/**
 * @brief Read handler for /sys/device/name.
 *
 * Renders the persisted device name plus newline ("tiku\n" until
 * someone renames the board).  The backing store survives reboots
 * and power loss.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
device_name_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", device_name_persist);
}

/**
 * @brief Write handler for /sys/device/name.
 *
 * Stores up to DEVICE_NAME_MAX bytes of the payload as the new
 * persistent name.  A single trailing newline is stripped (the
 * shell appends one); an empty result is rejected because the read
 * path expects a non-empty NUL-terminated string and clearing the
 * name has no use case.  Longer names are truncated, not rejected.
 *
 * The new name is staged NUL-terminated in SRAM and stored through
 * tiku_persist_cell_write(), which owns the MPU unlock window for
 * the FRAM copy.
 *
 * @param buf  New name text
 * @param len  Text length in bytes
 * @return 0 on success, -1 on empty input
 */
static int
device_name_write(const char *buf, size_t len)
{
    char tmp[DEVICE_NAME_MAX + 1];
    size_t i;
    size_t copy_len;

    /* Strip a trailing newline (shell typically appends one) */
    if (len > 0 && buf[len - 1] == '\n') {
        len--;
    }

    /* Reject empty writes — clearing the name has no use case
     * and the read path expects a NUL-terminated string. */
    if (len == 0) {
        return -1;
    }

    copy_len = (len > DEVICE_NAME_MAX) ? DEVICE_NAME_MAX : len;

    for (i = 0; i < copy_len; i++) {
        tmp[i] = buf[i];
    }
    tmp[copy_len] = '\0';
    tiku_persist_cell_write(&device_name_cell, tmp,
                            (uint16_t)(copy_len + 1));

    return 0;
}

/**
 * @brief Read handler for /sys/device/id.
 *
 * Renders a stable per-chip identifier derived from the MCU's
 * unique-device-ID registers: "tiku-XXXX\n" where XXXX are the
 * first two ID bytes in hex.  Four hex characters are enough to
 * distinguish a handful of devices on the same network without
 * dragging in a UUID library; unlike /sys/device/name this cannot
 * be changed, so it is the right key for scripts that must follow
 * the physical board across renames.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
device_id_read(char *buf, size_t max)
{
    uint8_t id[2] = {0, 0};
    (void)tiku_common_unique_id(id, sizeof(id));
    return snprintf(buf, max, "tiku-%02x%02x\n",
                    (unsigned)id[0], (unsigned)id[1]);
}

/**
 * @brief Read handler for /sys/device/mcu.
 *
 * Renders the silicon name from the selected device header
 * ("MSP430FR5969\n", "RP2350\n", ...).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
device_mcu_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_DEVICE_NAME);
}

/**
 * @brief Read handler for /sys/device/version.
 *
 * Same payload as /sys/version — duplicated under device/ so a
 * single `tree /sys/device` gives the full identity picture
 * (name, id, mcu, version) in one place.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
device_version_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_VERSION);
}

/*---------------------------------------------------------------------------*/
/* /sys/cpu/freq                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/cpu/freq.
 *
 * Renders the configured CPU frequency (TIKU_MAIN_CPU_HZ) in Hz as
 * a decimal line ("8000000\n").  This is the build-time constant;
 * compare against /sys/boot/clock/mclk, the live measurement, to
 * spot a DCO that failed to reach its target.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
cpu_freq_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)TIKU_MAIN_CPU_HZ);
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/* Type descriptors (const, FRAM) for the typed /sys nodes below. */
static const tiku_vfs_desc_t desc_mem_static =     /* sram, nvm: fixed sizes */
    TIKU_VFS_DESC(TIKU_VFS_T_U32, TIKU_VFS_U_BYTES,
                  TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_mem_live =       /* free, used: vary, cheap */
    TIKU_VFS_DESC(TIKU_VFS_T_U32, TIKU_VFS_U_BYTES,
                  TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_freq =
    TIKU_VFS_DESC(TIKU_VFS_T_U32, TIKU_VFS_U_HERTZ,
                  TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_idle =
    TIKU_VFS_DESC(TIKU_VFS_T_U32, TIKU_VFS_U_COUNT,
                  TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_uptime =
    TIKU_VFS_DESC(TIKU_VFS_T_U32, TIKU_VFS_U_SECONDS,
                  TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);

/*
 * /sys/mem/stack_free -- worst-case stack headroom since boot (bytes): the
 * intact painted cushion above the MPU stack guard.  Unlike /sys/mem/free
 * (the live gap at this instant) this is the closest the stack has EVER come
 * to the guard -- the number to snapshot under load when sizing the stack.
 * "0" on an arch that has not declared its stack bounds (feature dormant).
 */
static int stack_free_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_stack_free());
}

/** /sys/mem directory table — sizes (sram, nvm) + live (free, used) */
static const tiku_vfs_node_t sys_mem_children[] = {
    { "sram", TIKU_VFS_FILE, sram_read,      NULL, NULL, 0, &desc_mem_static },
    { "nvm",  TIKU_VFS_FILE, nvm_read,       NULL, NULL, 0, &desc_mem_static },
    { "free", TIKU_VFS_FILE, mem_free_read,  NULL, NULL, 0, &desc_mem_live },
    { "used", TIKU_VFS_FILE, mem_used_read,  NULL, NULL, 0, &desc_mem_live },
    { "nvmfree", TIKU_VFS_FILE, nvmfree_read, NULL, NULL, 0, &desc_mem_live },
    { "tiers",   TIKU_VFS_FILE, mem_tiers_read,  NULL, NULL, 0, &desc_mem_live },
    { "failed",  TIKU_VFS_FILE, mem_failed_read, NULL, NULL, 0, &desc_mem_live },
    { "stack_free", TIKU_VFS_FILE, stack_free_read, NULL, NULL, 0,
      &desc_mem_live },
};

/** /sys/cpu directory table */
static const tiku_vfs_node_t sys_cpu_children[] = {
    { "freq", TIKU_VFS_FILE, cpu_freq_read, NULL, NULL, 0, &desc_freq },
};

/** /sys/sched directory table */
static const tiku_vfs_node_t sys_sched_children[] = {
    { "idle", TIKU_VFS_FILE, sched_idle_read, NULL, NULL, 0, &desc_idle },
};

/** /sys/device directory table — name is the only writable node */
static const tiku_vfs_node_t sys_device_children[] = {
    { "name",    TIKU_VFS_FILE, device_name_read,    device_name_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_FS },   /* writes commit to the persistent store */
    { "id",      TIKU_VFS_FILE, device_id_read,      NULL,              NULL, 0 },
    { "mcu",     TIKU_VFS_FILE, device_mcu_read,     NULL,              NULL, 0 },
    { "version", TIKU_VFS_FILE, device_version_read, NULL,              NULL, 0 },
};

/**
 * The /sys directory table — the master list of everything under
 * /sys.
 *
 * Three kinds of entries:
 *   - files handled in this module (version, uptime, time)
 *   - files whose handlers the boot module exports (boot_count,
 *     last_reset, cold_boots — boot-domain data surfaced at the
 *     top level for script convenience)
 *   - directories: local tables above, or sibling modules'
 *     exported children + NCHILD pairs
 *
 * To add a /sys node: implement the handler (here or in the owning
 * module), append the entry, and — if referencing a sibling's
 * table — keep its NCHILD macro beside the children pointer.
 */
#if TIKU_SHELL_ENABLE
/*---------------------------------------------------------------------------*/
/* /sys/rules, /sys/jobs — read-only view of the shell's reactive automation */
/*---------------------------------------------------------------------------*/
/*
 * Observability only (an agent can see what automation is armed).  Mutating
 * add/del still goes through the `rules`/`on`/`every`/`once` shell commands;
 * writable VFS control is a deliberate follow-up.
 */
static int
rules_count_read(char *buf, size_t max)
{
    uint8_t  i;
    unsigned n = 0;
    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        if (tiku_shell_rules_get(i) != (const tiku_shell_rule_t *)0) {
            n++;
        }
    }
    return snprintf(buf, max, "%u\n", n);
}

/* One line per armed rule: "<id> <path> <op> <value> -> <action>". */
static int
rules_list_read(char *buf, size_t max)
{
    uint8_t i;
    int     off = 0;
    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        const tiku_shell_rule_t *r = tiku_shell_rules_get(i);
        size_t room;
        int    m;
        if (r == (const tiku_shell_rule_t *)0) {
            continue;
        }
        room = ((size_t)off < max) ? (max - (size_t)off) : 0u;
        m = snprintf(buf + off, room, "%u %s %s %s -> %s\n",
                     (unsigned)i, r->path, tiku_shell_rules_op_name(r->op),
                     r->value, r->action);
        if (m > 0) {
            off += m;
        }
    }
    return off;   /* 0 = no rules armed (empty read) */
}

/**
 * @brief Read handler for /sys/jobs/count.
 *
 * Renders the number of currently occupied scheduler job slots by
 * scanning the job table for armed (non-NULL) entries.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int
jobs_count_read(char *buf, size_t max)
{
    uint8_t  i;
    unsigned n = 0;
    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        if (tiku_shell_jobs_get(i) != (const tiku_shell_job_t *)0) {
            n++;
        }
    }
    return snprintf(buf, max, "%u\n", n);
}

/* One line per scheduled job: "<id> every|once <interval>s -> <cmd>". */
static int
jobs_list_read(char *buf, size_t max)
{
    uint8_t i;
    int     off = 0;
    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        const tiku_shell_job_t *j = tiku_shell_jobs_get(i);
        size_t room;
        int    m;
        if (j == (const tiku_shell_job_t *)0) {
            continue;
        }
        room = ((size_t)off < max) ? (max - (size_t)off) : 0u;
        m = snprintf(buf + off, room, "%u %s %us -> %s\n",
                     (unsigned)i,
                     (j->type == TIKU_SHELL_JOB_EVERY) ? "every" : "once",
                     (unsigned)j->interval_sec, j->cmd);
        if (m > 0) {
            off += m;
        }
    }
    return off;
}

static const tiku_vfs_node_t sys_rules_children[] = {
    { "count", TIKU_VFS_FILE, rules_count_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_FILE, rules_list_read,  NULL, NULL, 0 },
};
static const tiku_vfs_node_t sys_jobs_children[] = {
    { "count", TIKU_VFS_FILE, jobs_count_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_FILE, jobs_list_read,  NULL, NULL, 0 },
};
#endif /* TIKU_SHELL_ENABLE */

#if defined(PLATFORM_NORDIC)
/*---------------------------------------------------------------------------*/
/* /sys/crypto — CRACEN offload runtime switch + path counters               */
/*---------------------------------------------------------------------------*/
/*
 * The hardware-crypto backend keeps the kit APIs unchanged; this is the
 * runtime control surface: `mode` selects auto (hardware with software
 * fallback) or sw (software only -- the A/B switch and field kill-switch),
 * and `ops` reports which path actually served the calls, so a test can
 * assert "hardware really ran" instead of trusting the knob.
 */

static int
crypto_mode_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    (tiku_crypto_hw_mode() == TIKU_CRYPTO_HW_MODE_SW)
                        ? "sw" : "auto");
}

static int
crypto_mode_write(const char *buf, size_t len)
{
    if (len >= 2u && buf[0] == 's' && buf[1] == 'w') {
        tiku_crypto_hw_mode_set(TIKU_CRYPTO_HW_MODE_SW);
        return 0;
    }
    if (len >= 4u && buf[0] == 'a' && buf[1] == 'u' &&
        buf[2] == 't' && buf[3] == 'o') {
        tiku_crypto_hw_mode_set(TIKU_CRYPTO_HW_MODE_AUTO);
        return 0;
    }
    return TIKU_VFS_EINVAL;
}

static int
crypto_ops_read(char *buf, size_t max)
{
    uint16_t hw, sw, errs;
    tiku_crypto_hw_counters(&hw, &sw, &errs);
    return snprintf(buf, max, "hw=%u sw=%u err=%u\n",
                    (unsigned)hw, (unsigned)sw, (unsigned)errs);
}

static int
crypto_pk_read(char *buf, size_t max)
{
#if defined(TIKU_CRACEN_PK_ENABLE)
    uint16_t ops, errs;
    tiku_crypto_arch_pk_counters(&ops, &errs);
    return snprintf(buf, max, "hw-capable ops=%u errs=%u\n",
                    (unsigned)ops, (unsigned)errs);
#else
    (void)buf; (void)max;
    return snprintf(buf, max, "sw\n");
#endif
}

static const tiku_vfs_node_t sys_crypto_children[] = {
    { "mode", TIKU_VFS_FILE, crypto_mode_read, crypto_mode_write, NULL, 0 },
    { "ops",  TIKU_VFS_FILE, crypto_ops_read,  NULL,              NULL, 0 },
    { "pk",   TIKU_VFS_FILE, crypto_pk_read,   NULL,              NULL, 0 },
};
#endif /* PLATFORM_NORDIC */

#if (TIKU_HAS_BLE_ADV + 0)
/*---------------------------------------------------------------------------*/
/* /sys/radio — BLE broadcaster/observer control + observability             */
/*---------------------------------------------------------------------------*/
/*
 * Control surface over the tiku_ble_adv facade: `beacon` is the writable
 * knob ("NAME[,interval_ms]" starts, "off"/"0" stops), which also makes
 * beaconing available to the rules engine and `watch`; the rest report
 * state so a bench can assert "the radio really transmitted".
 */

static int
radio_beacon_read(char *buf, size_t max)
{
    if (!tiku_ble_adv_active()) {
        return snprintf(buf, max, "off\n");
    }
    return snprintf(buf, max, "%s,%u\n", tiku_ble_adv_name(),
                    (unsigned)tiku_ble_adv_interval_ms());
}

static int
radio_beacon_write(const char *buf, size_t len)
{
    char tmp[40];
    char *comma;
    unsigned long ms = 0ul;

    if (len == 0u || len >= sizeof(tmp)) {
        return TIKU_VFS_EINVAL;
    }
    memcpy(tmp, buf, len);
    tmp[len] = '\0';
    /* Trim a trailing newline from `write /sys/radio/beacon ...`. */
    while (len > 0u && (tmp[len - 1u] == '\n' || tmp[len - 1u] == '\r')) {
        tmp[--len] = '\0';
    }
    if (len == 0u) {
        return TIKU_VFS_EINVAL;
    }
    if (strcmp(tmp, "off") == 0 || strcmp(tmp, "0") == 0) {
        tiku_ble_adv_stop();
        return 0;
    }
    comma = strchr(tmp, ',');
    if (comma != NULL) {
        *comma = '\0';
        ms = strtoul(comma + 1, NULL, 10);
    }
    return (tiku_ble_adv_beacon(tmp, (uint16_t)ms) == 0)
               ? 0 : TIKU_VFS_EINVAL;
}

static int
radio_bursts_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_ble_adv_bursts());
}

static int
radio_state_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    tiku_ble_adv_active() ? "beacon" : "idle");
}

static int
radio_scan_read(char *buf, size_t max)
{
    const tiku_ble_adv_report_t *b = tiku_ble_adv_last_scan_best();
    if (b == NULL) {
        return snprintf(buf, max, "devices=%u\n",
                        (unsigned)tiku_ble_adv_last_scan_count());
    }
    return snprintf(buf, max,
                    "devices=%u best=%02X:%02X:%02X:%02X:%02X:%02X "
                    "rssi=%d name=%s\n",
                    (unsigned)tiku_ble_adv_last_scan_count(),
                    b->addr[5], b->addr[4], b->addr[3],
                    b->addr[2], b->addr[1], b->addr[0],
                    (int)b->rssi, b->name);
}

static const tiku_vfs_node_t sys_radio_children[] = {
    { "beacon", TIKU_VFS_FILE, radio_beacon_read, radio_beacon_write,
      NULL, 0 },
    { "bursts", TIKU_VFS_FILE, radio_bursts_read, NULL, NULL, 0 },
    { "state",  TIKU_VFS_FILE, radio_state_read,  NULL, NULL, 0 },
    { "scan",   TIKU_VFS_FILE, radio_scan_read,   NULL, NULL, 0 },
};
#endif /* TIKU_HAS_BLE_ADV */

#if (TIKU_FLPR_ENABLE + 0)
/*---------------------------------------------------------------------------*/
/* /sys/flpr — the VPR RISC-V coprocessor                                    */
/*---------------------------------------------------------------------------*/
/*
 * Control + liveness for the FLPR: `run` starts (loads the embedded image
 * first) and stops the core; `state` distinguishes stopped / running-but-
 * not-yet-in-main / alive; `heartbeat` is the firmware's forever-counter,
 * the ground truth that RISC-V code is executing right now.
 */

static int
flpr_state_read(char *buf, size_t max)
{
    const char *s = "stopped";
    if (tiku_flpr_arch_running()) {
        s = tiku_flpr_arch_alive() ? "alive" : "started";
    }
    return snprintf(buf, max, "%s\n", s);
}

static int
flpr_heartbeat_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_flpr_arch_heartbeat());
}

static int
flpr_image_read(char *buf, size_t max)
{
    /* Bring-up: image size + live doorbell-plumbing readbacks. */
    return snprintf(buf, max, "%lu inten=%lx trig16=%lu intpend=%lx\n",
                    (unsigned long)tiku_flpr_arch_image_size(),
                    (unsigned long)NRF_VPR00_NS->INTEN,
                    (unsigned long)NRF_VPR00_NS->EVENTS_TRIGGERED[16],
                    (unsigned long)NRF_VPR00_NS->INTPEND);
}

static int
flpr_run_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%d\n", tiku_flpr_arch_running());
}

static int
flpr_run_write(const char *buf, size_t len)
{
    if (len >= 1u && buf[0] == '1') {
        return (tiku_flpr_arch_start() == 0) ? 0 : TIKU_VFS_EINVAL;
    }
    if (len >= 1u && buf[0] == '0') {
        tiku_flpr_arch_stop();
        return 0;
    }
    return TIKU_VFS_EINVAL;
}

/* Echo surface over the mailbox IPC: writing sends the bytes to the FLPR
 * (its echo service mirrors them back, doorbell -> ISR capture); reading
 * returns "<reply_seq> <last reply>".  A seq that advances after a write
 * proves the ENTIRE cross-core interrupt path, which is exactly what the
 * TikuBench flpr suite asserts. */
static int
flpr_echo_read(char *buf, size_t max)
{
    char body[TIKU_FLPR_MSG_CAP + 1];
    uint32_t n;
    tiku_flpr_arch_poll();                      /* doorbell fallback: pull */
    n = tiku_flpr_arch_reply(body, sizeof(body) - 1u);
    body[n] = '\0';
    return snprintf(buf, max, "%lu %s\n",
                    (unsigned long)tiku_flpr_arch_reply_seq(), body);
}

static int
flpr_echo_write(const char *buf, size_t len)
{
    return (tiku_flpr_arch_send(buf, (uint32_t)len) == 0)
               ? 0 : TIKU_VFS_EINVAL;
}

static const tiku_vfs_node_t sys_flpr_children[] = {
    { "run",       TIKU_VFS_FILE, flpr_run_read,       flpr_run_write,
      NULL, 0 },
    { "state",     TIKU_VFS_FILE, flpr_state_read,     NULL, NULL, 0 },
    { "heartbeat", TIKU_VFS_FILE, flpr_heartbeat_read, NULL, NULL, 0 },
    { "image",     TIKU_VFS_FILE, flpr_image_read,     NULL, NULL, 0 },
    { "echo",      TIKU_VFS_FILE, flpr_echo_read,      flpr_echo_write,
      NULL, 0 },
};
#endif /* TIKU_FLPR_ENABLE */

static const tiku_vfs_node_t sys_children[] = {
    { "version",    TIKU_VFS_FILE, version_read,    NULL, NULL, 0 },
    { "device",     TIKU_VFS_DIR,  NULL, NULL, sys_device_children, 4 },
    { "uptime",     TIKU_VFS_FILE, uptime_read,     NULL, NULL, 0,
      &desc_uptime },
    { "time",       TIKU_VFS_FILE, time_read,       time_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_SYS },   /* clock skew breaks TLS cert validity */
    { "boot_count", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_count_read,      NULL, NULL, 0 },
    { "last_reset", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_last_reset_read, NULL, NULL, 0 },
    { "cold_boots", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_cold_boots_read, NULL, NULL, 0 },
    { "mem",      TIKU_VFS_DIR,  NULL, NULL, sys_mem_children, 8 },
    { "cpu",      TIKU_VFS_DIR,  NULL, NULL, sys_cpu_children, 1 },
    { "power",    TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_power_children,    TIKU_VFS_TREE_POWER_NCHILD },
    { "timer",    TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_timer_children,    TIKU_VFS_TREE_TIMER_NCHILD },
    { "clock",    TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_clock_children,    TIKU_VFS_TREE_CLOCK_NCHILD },
    { "watchdog", TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_watchdog_children, TIKU_VFS_TREE_WATCHDOG_NCHILD },
    { "htimer",   TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_htimer_children,   TIKU_VFS_TREE_HTIMER_NCHILD },
    { "boot",     TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_boot_children,     TIKU_VFS_TREE_BOOT_NCHILD },
    { "persist",  TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_persist_children,  TIKU_VFS_TREE_PERSIST_NCHILD },
    { "watch",    TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_watch_children,    TIKU_VFS_TREE_WATCH_NCHILD },
    { "vfs",      TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_vfs_children,      TIKU_VFS_TREE_VFS_NCHILD },
#if TIKU_SHELL_ENABLE
    { "rules",    TIKU_VFS_DIR,  NULL, NULL, sys_rules_children, 2 },
    { "jobs",     TIKU_VFS_DIR,  NULL, NULL, sys_jobs_children,  2 },
#endif
    { "sched",    TIKU_VFS_DIR,  NULL, NULL, sys_sched_children, 1 },
#if defined(PLATFORM_NORDIC)
    { "crypto",   TIKU_VFS_DIR,  NULL, NULL, sys_crypto_children, 3 },
#endif
#if (TIKU_HAS_BLE_ADV + 0)
    { "radio",    TIKU_VFS_DIR,  NULL, NULL, sys_radio_children,  4 },
#endif
#if (TIKU_FLPR_ENABLE + 0)
    { "flpr",     TIKU_VFS_DIR,  NULL, NULL, sys_flpr_children,   5 },
#endif
#if TIKU_INIT_ENABLE
    { "init",     TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_inittab_children,  TIKU_VFS_TREE_INITTAB_NCHILD },
#endif
};

/**
 * The /sys directory node itself, fully formed with its name and a
 * sizeof-derived child count so the root assembly can copy it by
 * value (getter pattern — see tiku_vfs_tree_sys_get()).
 */
static const tiku_vfs_node_t sys_node = {
    "sys", TIKU_VFS_DIR, NULL, NULL, sys_children,
    sizeof(sys_children) / sizeof(sys_children[0])
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Get the fully-formed /sys directory node.
 *
 * See the header for the copy-by-value contract with the root
 * assembly.
 *
 * @return Pointer to the static /sys directory node
 */
const tiku_vfs_node_t *
tiku_vfs_tree_sys_get(void)
{
    return &sys_node;
}

/**
 * @brief Initialise /sys state (RTC epoch, device name default).
 *
 * Two independent pieces of persistent state are made valid here:
 * the RTC epoch offset (tiku_rtc_init() validates its own persist
 * cell, so the call is unconditional and idempotent) and the
 * device-name cell, which tiku_persist_cell_init() primes to its
 * "tiku" default exactly when its own gate reports virgin FRAM.
 * Neither depends on the boot module any more — each cell carries
 * its own validity.
 */
void
tiku_vfs_tree_sys_init(void)
{
    /* Validate (and on first boot, prime) the persistent wall-clock
     * epoch offset. Idempotent — short-circuits when valid. */
    tiku_rtc_init();

    /* Validate/prime the device name ("tiku" until renamed via
     * /sys/device/name). */
    (void)tiku_persist_cell_init(&device_name_cell);
}
