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
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_sys.h"
#include "tiku_vfs_tree_boot.h"
#include "tiku_vfs_tree_timer.h"
#include "tiku_vfs_tree_watchdog.h"
#include "tiku_vfs_tree_power.h"
#include "tiku_vfs_tree_persist.h"
#include "tiku_vfs_tree_inittab.h"
#include "tiku.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_rtc.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_process.h>
#include <kernel/scheduler/tiku_sched.h>
#include <stdio.h>

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
            return -1;
        }
        v = v * 10U + (uint32_t)(c - '0');
        seen_digit = 1;
    }
    if (!seen_digit) {
        return -1;
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
#elif defined(PLATFORM_RP2350)
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
 * Renders the sum of every registered process's sram_used
 * accounting field as a decimal line.  This measures what
 * processes *declared* through the proc-mem API, not actual SRAM
 * consumption — kernel statics and the stack are not included.
 * Walks the registry by pid; empty slots return NULL from
 * tiku_process_get() and are skipped.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
mem_used_read(char *buf, size_t max)
{
    uint16_t total = 0;
    uint8_t i;
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        struct tiku_process *p = tiku_process_get((int8_t)i);
        if (p != NULL) {
            total += p->sram_used;
        }
    }
    return snprintf(buf, max, "%u\n", total);
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

/** /sys/mem directory table — sizes (sram, nvm) + live (free, used) */
static const tiku_vfs_node_t sys_mem_children[] = {
    { "sram", TIKU_VFS_FILE, sram_read,      NULL, NULL, 0 },
    { "nvm",  TIKU_VFS_FILE, nvm_read,       NULL, NULL, 0 },
    { "free", TIKU_VFS_FILE, mem_free_read,  NULL, NULL, 0 },
    { "used", TIKU_VFS_FILE, mem_used_read,  NULL, NULL, 0 },
};

/** /sys/cpu directory table */
static const tiku_vfs_node_t sys_cpu_children[] = {
    { "freq", TIKU_VFS_FILE, cpu_freq_read, NULL, NULL, 0 },
};

/** /sys/sched directory table */
static const tiku_vfs_node_t sys_sched_children[] = {
    { "idle", TIKU_VFS_FILE, sched_idle_read, NULL, NULL, 0 },
};

/** /sys/device directory table — name is the only writable node */
static const tiku_vfs_node_t sys_device_children[] = {
    { "name",    TIKU_VFS_FILE, device_name_read,    device_name_write, NULL, 0 },
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
static const tiku_vfs_node_t sys_children[] = {
    { "version",    TIKU_VFS_FILE, version_read,    NULL, NULL, 0 },
    { "device",     TIKU_VFS_DIR,  NULL, NULL, sys_device_children, 4 },
    { "uptime",     TIKU_VFS_FILE, uptime_read,     NULL, NULL, 0 },
    { "time",       TIKU_VFS_FILE, time_read,       time_write, NULL, 0 },
    { "boot_count", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_count_read,      NULL, NULL, 0 },
    { "last_reset", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_last_reset_read, NULL, NULL, 0 },
    { "cold_boots", TIKU_VFS_FILE,
      tiku_vfs_tree_boot_cold_boots_read, NULL, NULL, 0 },
    { "mem",      TIKU_VFS_DIR,  NULL, NULL, sys_mem_children, 4 },
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
    { "sched",    TIKU_VFS_DIR,  NULL, NULL, sys_sched_children, 1 },
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
