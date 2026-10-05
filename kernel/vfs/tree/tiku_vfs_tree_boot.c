/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_boot.c - /sys/boot VFS nodes and boot bookkeeping.
 *
 * Exposes the decoded reset reason, boot count, boot stage, hang culprit,
 * clock rates and MPU diagnostics.  The boot counter and lifetime accumulator
 * are magic-gated persist cells.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_boot.h"
#include "tiku.h"
#include <kernel/vfs/tiku_vfs_tree.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_common.h>   /* tiku_common_reset_reason() */
#include <kernel/cpu/tiku_hang.h>     /* check-in watchdog culprit */
#include <boot/tiku_boot.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/boot/reason — decoded reset cause                                    */
/*---------------------------------------------------------------------------*/

/*
 * Reset-cause snapshot, taken once in tiku_vfs_tree_boot_init() via
 * tiku_common_reset_reason(), which returns a SYSRSTIV-style code on every
 * port.  MSP430 latches it once because reading the live register pops the
 * highest pending vector; the other ports map their reset registers onto
 * the same codes.
 */
static uint16_t boot_reset_cause;

/**
 * @brief Decode a raw SYSRSTIV value to its short name.
 *
 * Decodes the causes in the table (TI SLAU367): power faults, watchdog
 * variants, FRAM errors, security violations and software resets.  Any other
 * value renders as "unknown"; /sys/boot/rstiv shows it raw.
 *
 * @param iv  Raw SYSRSTIV value (even; FR5994 reports 0x0000..0x002E)
 * @return Static string naming the cause; never NULL
 */
static const char *
reset_cause_str(uint16_t iv)
{
    switch (iv) {
    case 0x0000: return "none";
    case 0x0002: return "brownout";
    case 0x0004: return "rstnmi";
    case 0x0006: return "sw-bor";
    case 0x0008: return "lpm5-wake";
    case 0x000A: return "security";
    case 0x000E: return "svs";
    case 0x0014: return "sw-por";
    case 0x0016: return "wdt-timeout";
    case 0x0018: return "wdt-pwviol";
    case 0x001A: return "fram-pwviol";
    case 0x001C: return "fram-bit-err";
    case 0x001E: return "periph-fetch";
    case 0x0020: return "pmm-pwviol";
    case 0x0024: return "fll-unlock";
    default:     return "unknown";
    }
}

/**
 * @brief Read handler for /sys/boot/reason.
 *
 * Renders the latched reset cause as its short name plus newline, e.g.
 * "wdt-timeout\n".  /sys/last_reset is the coarse 4-bucket version and
 * /sys/boot/rstiv the raw hex value.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_reason_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", reset_cause_str(boot_reset_cause));
}

/*
 * Durable monotonic boot counter, which survives reset, brownout and power
 * loss.  It is a persist cell (TIKU_PERSIST_CELL), so the tiku_persist_cell
 * API handles the magic gate, first-boot priming and MPU-window writes.
 * Incremented in tiku_vfs_tree_boot_init(); reads serve the SRAM copy
 * boot_count_value.
 */

/*
 * Gate key for the boot-counter cell: an arbitrary non-trivial constant, so
 * uninitialised memory matches it with probability 2^-32.  Bump it if the
 * cell's meaning ever changes incompatibly, which forces a clean re-prime.
 */
#define BOOT_COUNT_MAGIC  0xB007C001UL

/** Durable cell: boots since first power-up (1 on the very first) */
static TIKU_DURABLE uint32_t boot_count_persist;

/** Gate + descriptor: defaults to 0, then pre-increments each boot */
TIKU_PERSIST_CELL(boot_count_cell, boot_count_persist,
                  BOOT_COUNT_MAGIC, NULL, 0);

/**
 * SRAM copy of boot_count_persist, set at init and served by the read
 * handler; tiku_vfs_set_boot_count() overrides it.
 */
static uint32_t boot_count_value;

/**
 * @brief Read handler for /sys/boot_count and /sys/boot/count.
 *
 * Renders the boot counter as a decimal line ("17\n" = seventeenth
 * boot since the durable memory was first initialised).  Serves the
 * SRAM copy — see boot_count_value above.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int
tiku_vfs_tree_boot_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)boot_count_value);
}

/*---------------------------------------------------------------------------*/
/* /sys/last_reset — coarse-bucketed reset cause                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map raw SYSRSTIV to one of four user-facing categories.
 *
 * "watchdog" (counter overflow or password violation), "power" (brownout, SVS,
 * PMM/FRAM violation, or a clean cold start), "reboot" (software BOR/POR or the
 * RST pin), "other".  /sys/boot/reason keeps the detailed name.
 *
 * @param iv  Raw SYSRSTIV value
 * @return Static category string; never NULL
 */
static const char *
last_reset_str(uint16_t iv)
{
    switch (iv) {
    /* Watchdog: counter overflow or password violation */
    case 0x0016: case 0x0018:
        return "watchdog";
    /* Power: brownout, SVS, PMM violation, FRAM power violation */
    case 0x0002: case 0x000E: case 0x0020: case 0x001A:
        return "power";
    /* Software-initiated: BOR, POR, NMI from RST pin */
    case 0x0004: case 0x0006: case 0x0014:
        return "reboot";
    /* Cold start (no reset cause) reported as power-on */
    case 0x0000:
        return "power";
    default:
        return "other";
    }
}

/**
 * @brief Read handler for /sys/last_reset.
 *
 * Renders the bucketed cause ("watchdog\n", "power\n", "reboot\n"
 * or "other\n") from the same reset-cause snapshot as
 * /sys/boot/reason.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int
tiku_vfs_tree_boot_last_reset_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", last_reset_str(boot_reset_cause));
}

/*---------------------------------------------------------------------------*/
/* /sys/cold_boots — lifetime uptime accumulator                             */
/*---------------------------------------------------------------------------*/

/*
 * Durable sum of uptime across every boot since the chip was
 * first programmed. Saved on every read: the persisted cell
 * tracks lifetime within one read interval of accuracy, so a
 * monitoring loop that polls /sys/cold_boots once a minute loses
 * at most 60 seconds on a power-loss event between reads.
 *
 * Lifetime = lifetime_at_boot (snapshotted at init) + current
 * uptime in seconds.
 */

/**
 * Gate key for the lifetime-accumulator cell ('LIFE'), distinct from
 * BOOT_COUNT_MAGIC so the two cells validate independently.
 */
#define LIFETIME_MAGIC  0x4C494645UL /* 'LIFE' */

/** Durable cell: lifetime seconds persisted up to the last save */
static TIKU_DURABLE uint32_t lifetime_seconds_persist;

/** Gate + descriptor: defaults to 0 on blank memory */
TIKU_PERSIST_CELL(lifetime_cell, lifetime_seconds_persist,
                  LIFETIME_MAGIC, NULL, 0);

/*
 * Snapshot of lifetime_seconds_persist taken at init, before this run began
 * adding uptime.  Adding tiku_clock_seconds() yields the live lifetime.
 */
static uint32_t lifetime_at_boot;

/**
 * @brief Read handler for /sys/cold_boots.
 *
 * Renders the lifetime uptime in seconds and saves it to the lifetime cell.
 * On MSP430 the save clears and re-sets the cell's gate around two word
 * stores, so a power cut inside it re-primes the lifetime to 0.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or TIKU_VFS_EIO when the cell cannot be saved
 */
int
tiku_vfs_tree_boot_cold_boots_read(char *buf, size_t max)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();
    uint32_t lifetime = lifetime_at_boot + now;

    /* Save the fresh lifetime so a later power loss keeps the
     * time covered by this run.  The cell write owns the MPU
     * unlock window. */
    if (tiku_persist_cell_write_u32_status(&lifetime_cell, lifetime)
            != TIKU_MEM_OK) {
        return TIKU_VFS_EIO;
    }

    return snprintf(buf, max, "%lu\n", (unsigned long)lifetime);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/stage                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/stage.
 *
 * Renders the boot sequencer's stage as a word: "init", "cpu", "memory",
 * "peripherals", "services" or "complete" (indexing tiku_boot_stage_e).
 * Anything but "complete" means the read raced boot, or boot stalled.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_stage_read(char *buf, size_t max)
{
    static const char * const stage_names[] = {
        "init", "cpu", "memory", "peripherals", "services", "complete"
    };
    tiku_boot_stage_e s = tiku_boot_get_stage();
    const char *name = (s <= TIKU_BOOT_STAGE_COMPLETE)
                           ? stage_names[s] : "unknown";
    return snprintf(buf, max, "%s\n", name);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/rstiv — raw reset-cause code (hex)                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/rstiv.
 *
 * Renders the latched reset-cause code as four hex digits ("0x0016\n"), for
 * a code that reset_cause_str() does not decode.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_rstiv_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%04x\n", boot_reset_cause);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/clock/ — clock frequencies                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/clock/mclk.
 *
 * Renders the CPU master clock in Hz ("8000000\n") as the CPU HAL reports it.
 * On MSP430 that is the configured rate, not a measurement; a failed
 * oscillator shows in /sys/boot/clock/fault instead.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_clock_mclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_cpu_mclk_hz());
}

/**
 * @brief Read handler for /sys/boot/clock/smclk.
 *
 * Renders the sub-main (peripheral) clock frequency in Hz as a
 * decimal line.  UART/SPI/I2C bit clocks derive from this.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_clock_smclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_cpu_smclk_hz());
}

/**
 * @brief Read handler for /sys/boot/clock/aclk.
 *
 * Renders the auxiliary clock frequency in Hz as the CPU HAL reports
 * it (32768 when ACLK runs from the LFXT crystal).  On MSP430 that is
 * the configured rate; a failed crystal shows in /sys/boot/clock/fault.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_clock_aclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", tiku_cpu_aclk_hz());
}

/**
 * @brief Read handler for /sys/boot/clock/fault.
 *
 * Renders "1\n" when the clock system reports a fault flag
 * (oscillator failure latched since boot), "0\n" when healthy.
 * Check this first when timers drift or UART baud is off.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_clock_fault_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_cpu_clock_has_fault() ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/mpu/{violations,count,last_addr}                                */
/*---------------------------------------------------------------------------*/
/*
 *   violations — MPU segment violation flags from the current boot, as a
 *                hex bitmask.  Cleared on every fresh boot (.bss-backed).
 *   count      — counter incremented on every violation, surviving the
 *                fault-triggered reset on platforms with a NOLOAD
 *                diagnostic region (RP2350 .mpu_diag).  Decimal.
 *   last_addr  — fault address of the most recent violation (the MMFAR
 *                snapshot on Cortex-M), hex.
 */

/**
 * @brief Read handler for /sys/boot/mpu/violations.
 *
 * Renders this boot's violation flag bitmask as two hex digits ("0x00\n" when
 * clean).  Bit meanings are defined by kernel/memory/tiku_mpu.c.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_mpu_violations_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%02x\n",
                    tiku_mpu_get_violation_flags());
}

/**
 * @brief Read handler for /sys/boot/mpu/count.
 *
 * Renders the cumulative violation count.  Where a NOLOAD diagnostic region
 * exists the counter survives the reset the violation triggers, so a crash-loop
 * shows up as a steadily climbing number across reboots.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_mpu_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_mpu_get_violation_count());
}

/**
 * @brief Read handler for /sys/boot/mpu/last_addr.
 *
 * Renders the faulting address of the most recent violation as
 * eight hex digits ("0x20003ffc\n") — the MMFAR snapshot on
 * Cortex-M.  Reads 0 when no violation has been recorded.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
boot_mpu_last_addr_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%08lx\n",
                    (unsigned long)tiku_mpu_get_last_fault_addr());
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * /sys/boot/clock directory table — read-only frequency/fault
 * views.  Private; referenced only by the "clock" entry below.
 */
static const tiku_vfs_node_t boot_clock_children[] = {
    { "mclk",  TIKU_VFS_FILE, boot_clock_mclk_read,  NULL, NULL, 0 },
    { "smclk", TIKU_VFS_FILE, boot_clock_smclk_read, NULL, NULL, 0 },
    { "aclk",  TIKU_VFS_FILE, boot_clock_aclk_read,  NULL, NULL, 0 },
    { "fault", TIKU_VFS_FILE, boot_clock_fault_read, NULL, NULL, 0 },
};

/**
 * /sys/boot/mpu directory table — read-only violation diagnostics.
 * Private; referenced only by the "mpu" entry below.
 */
static const tiku_vfs_node_t boot_mpu_children[] = {
    { "violations", TIKU_VFS_FILE, boot_mpu_violations_read, NULL, NULL, 0 },
    { "count",      TIKU_VFS_FILE, boot_mpu_count_read,      NULL, NULL, 0 },
    { "last_addr",  TIKU_VFS_FILE, boot_mpu_last_addr_read,  NULL, NULL, 0 },
};

/**
 * @brief Read handler for /sys/boot/hang.
 *
 * The process the check-in watchdog caught wedging the cooperative scheduler
 * before the last reset, as "<pid> <name>", or "none".
 */
static int boot_hang_read(char *buf, size_t max)
{
    int8_t pid = tiku_hang_last_pid();

    if (pid < 0) {
        return snprintf(buf, max, "none\n");
    }
    return snprintf(buf, max, "%d %s\n", (int)pid, tiku_hang_last_name());
}

/*
 * /sys/boot directory table, exported so tiku_vfs_tree_sys.c can attach it as
 * the "boot" directory; the entry count travels as TIKU_VFS_TREE_BOOT_NCHILD
 * (asserted below).  "count" reuses the exported boot-counter read handler.
 */
const tiku_vfs_node_t tiku_vfs_tree_boot_children[] = {
    { "reason", TIKU_VFS_FILE, boot_reason_read,              NULL, NULL, 0 },
    { "count",  TIKU_VFS_FILE, tiku_vfs_tree_boot_count_read, NULL, NULL, 0 },
    { "stage",  TIKU_VFS_FILE, boot_stage_read,               NULL, NULL, 0 },
    { "rstiv",  TIKU_VFS_FILE, boot_rstiv_read,               NULL, NULL, 0 },
    { "hang",   TIKU_VFS_FILE, boot_hang_read,                NULL, NULL, 0 },
    { "clock",  TIKU_VFS_DIR,  NULL, NULL, boot_clock_children, 4 },
    { "mpu",    TIKU_VFS_DIR,  NULL, NULL, boot_mpu_children,
      sizeof(boot_mpu_children) / sizeof(boot_mpu_children[0]) },
};

_Static_assert(sizeof(tiku_vfs_tree_boot_children) /
               sizeof(tiku_vfs_tree_boot_children[0])
               == TIKU_VFS_TREE_BOOT_NCHILD,
               "TIKU_VFS_TREE_BOOT_NCHILD out of sync");

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Capture the reset cause and bump the durable boot counter.
 *
 * Latches the reset cause (tiku_common_reset_reason()) and the hang culprit,
 * validates both persist cells -- blank or corrupt ones are primed to 0 with
 * the gate stamped last -- then bumps the counter and snapshots the lifetime.
 *
 * @note Runs first in tiku_vfs_tree_init(): SYSRSTIV reads are destructive.
 */
void
tiku_vfs_tree_boot_init(void)
{
    /* Capture the reset cause before anything else clears it.  The HAL
     * returns a SYSRSTIV-style code on every port, which /sys/boot/rstiv,
     * /sys/boot/reason and /sys/last_reset render. */
    boot_reset_cause = tiku_common_reset_reason();

    /* Capture the check-in watchdog's culprit (if the last reset was a
     * detected hang) into this boot's view and clear the cross-reset record.
     * One-shot: the culprit is quarantined for the recovery boot only, and
     * /sys/boot/hang reports it for the life of this boot. */
    tiku_hang_boot_init();

    /* Validate or prime the cells, then bump the boot counter; the
     * read handler serves the SRAM copy. */
    (void)tiku_persist_cell_init(&boot_count_cell);
    (void)tiku_persist_cell_init(&lifetime_cell);

    /* Unchecked: boot continues if the counter cannot be saved. */
    tiku_persist_cell_write_u32(&boot_count_cell,
                                boot_count_persist + 1U);
    boot_count_value = boot_count_persist;
    lifetime_at_boot = lifetime_seconds_persist;
}

/**
 * @brief Override the boot count exposed via /sys/boot_count.
 *
 * Only the SRAM copy changes, so the durable cell keeps its monotonic count.
 *
 * @param count  Value subsequent /sys/boot_count reads will report
 */
void
tiku_vfs_set_boot_count(uint32_t count)
{
    boot_count_value = count;
}
