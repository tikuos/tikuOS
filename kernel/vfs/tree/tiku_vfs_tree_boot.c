/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_boot.c - /sys/boot VFS nodes + boot bookkeeping
 *
 * Everything a postmortem wants to know about how and how often
 * this device boots:
 *
 *   /sys/boot/reason     decoded SYSRSTIV ("wdt-timeout", "brownout"...)
 *   /sys/boot/count      monotonic FRAM boot counter
 *   /sys/boot/stage      current boot stage from boot/tiku_boot.c
 *   /sys/boot/rstiv      raw SYSRSTIV value in hex, for scripting
 *   /sys/boot/clock/...  live MCLK/SMCLK/ACLK frequencies + fault flag
 *   /sys/boot/mpu/...    MPU violation diagnostics
 *   /sys/last_reset      coarse 4-bucket reset cause (top-level /sys)
 *   /sys/cold_boots      lifetime uptime accumulator (top-level /sys)
 *
 * Persistence model: two uint32 values live in .persistent (FRAM)
 * — boot_count_persist and lifetime_seconds_persist — each declared
 * as a magic-gated persist cell (TIKU_PERSIST_CELL, kernel/memory).
 * The shared cell API owns the gate validation, first-boot priming
 * and MPU-window writes that this module used to hand-roll, and
 * each cell validates independently (no more shared magic across
 * modules).  Read paths serve SRAM mirrors so the hot path never
 * unlocks the MPU; only init and the lazy cold_boots save write to
 * FRAM, through the cell API.
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
#include <kernel/cpu/tiku_common.h>   /* tiku_common_reset_reason() — per-arch */
#include <kernel/cpu/tiku_hang.h>     /* check-in watchdog culprit (this boot) */
#include <boot/tiku_boot.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/boot/reason — last reset cause from SYSRSTIV                         */
/*---------------------------------------------------------------------------*/

/**
 * Reset-cause snapshot taken once in tiku_vfs_tree_boot_init() via the
 * per-arch tiku_common_reset_reason() HAL (an MSP430-SYSRSTIV-compatible
 * code on every platform).
 *
 * On MSP430 reading the live SYSRSTIV register pops the highest pending
 * vector (hardware walks toward 0 on each read), so the cause must be
 * latched exactly once at boot and served from this copy ever after; the
 * HAL does that latching.  RP2350 maps WD_REASON; Ambiq decodes
 * RSTGEN->STAT (watchdog / reboot / power) in the arch layer.
 */
static uint16_t boot_reset_cause;

/**
 * @brief Decode a raw SYSRSTIV value to its short name.
 *
 * Covers every cause the FR59xx family reports (TI SLAU367,
 * SYSRSTIV table): power faults, watchdog variants, FRAM
 * integrity errors, security violations and software resets.
 * Values not in the table render as "unknown" rather than
 * faulting — future silicon may add vectors.
 *
 * @param iv  Raw SYSRSTIV value (even, 0x0000..0x0024)
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
 * Renders the latched reset cause as its short name plus newline,
 * e.g. "wdt-timeout\n" after a watchdog reset.  See
 * /sys/last_reset for the coarse 4-bucket version and
 * /sys/boot/rstiv for the raw hex value.
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
 * FRAM-backed monotonic boot counter.
 *
 * Lives in .persistent so its value survives reset, brownout, and
 * power loss. Declared as a persist cell (TIKU_PERSIST_CELL) so the
 * magic-gate validation, first-boot priming and MPU-window writes
 * are handled by the shared tiku_persist_cell API. Initialised and
 * incremented in tiku_vfs_tree_boot_init(); the SRAM mirror
 * boot_count_value is what the VFS read returns so the inner loop
 * never has to unlock the MPU.
 */

/**
 * Gate key for the boot-counter cell.
 *
 * An arbitrary non-trivial constant ("B007 C001"): the odds of
 * uninitialised FRAM matching it are 1 in 2^32.  Bump the value if
 * the cell's meaning ever changes incompatibly — that forces a
 * clean re-prime on the next boot after reflashing.  (Until the
 * persist-cell conversion this one magic gated the lifetime
 * accumulator and the device name too; each now has its own gate,
 * so the three validate independently.)
 */
#define BOOT_COUNT_MAGIC  0xB007C001UL

/** FRAM cell: boots since first power-up (1 on the very first) */
static uint32_t __attribute__((section(".persistent")))
    boot_count_persist;

/** Gate + descriptor: defaults to 0, then pre-increments each boot */
TIKU_PERSIST_CELL(boot_count_cell, boot_count_persist,
                  BOOT_COUNT_MAGIC, NULL, 0);

/**
 * SRAM mirror of boot_count_persist, refreshed at init and served
 * by the read handler (keeps the read path MPU-free).  Overridable
 * via tiku_vfs_set_boot_count() on the hibernate resume path.
 */
static uint32_t boot_count_value;

/**
 * @brief Read handler for /sys/boot_count and /sys/boot/count.
 *
 * Renders the boot counter as a decimal line ("17\n" = seventeenth
 * boot since the FRAM was first initialised).  Serves the SRAM
 * mirror — see boot_count_value above.
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
 * The detailed name is still available at /sys/boot/reason; this
 * is the field a script wants to branch on:
 *
 *   "watchdog"  counter overflow or password violation — the
 *               firmware hung or corrupted the WDT
 *   "power"     brownout, SVS, PMM/FRAM power violation, or a
 *               clean cold start (iv 0)
 *   "reboot"    deliberate: software BOR/POR or the RST pin
 *   "other"     anything else (security, FRAM bit error, ...)
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
 * or "other\n") from the same SYSRSTIV snapshot as
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
 * FRAM-backed sum of uptime across every boot since the chip was
 * first programmed. Saved lazily on every read: the persisted
 * cell tracks lifetime within one read interval of accuracy, so a
 * monitoring loop that polls /sys/cold_boots once a minute loses
 * at most 60 seconds on a power-loss event between reads.
 *
 * Lifetime = lifetime_at_boot (snapshotted at init) + current
 * uptime in seconds.
 */

/**
 * Gate key for the lifetime-accumulator cell ('LIFE').  Split from
 * BOOT_COUNT_MAGIC by the persist-cell conversion so the two values
 * validate independently.
 */
#define LIFETIME_MAGIC  0x4C494645UL /* 'LIFE' */

/** FRAM cell: lifetime seconds persisted up to the last save */
static uint32_t __attribute__((section(".persistent")))
    lifetime_seconds_persist;

/** Gate + descriptor: defaults to 0 on a virgin FRAM */
TIKU_PERSIST_CELL(lifetime_cell, lifetime_seconds_persist,
                  LIFETIME_MAGIC, NULL, 0);

/**
 * Snapshot of lifetime_seconds_persist taken at init, before this
 * run started adding uptime.  Adding tiku_clock_seconds() to it
 * yields the live lifetime without rewriting FRAM on every read
 * of an unrelated node.
 */
static uint32_t lifetime_at_boot;

/**
 * @brief Read handler for /sys/cold_boots.
 *
 * Renders the lifetime uptime in seconds as a decimal line and, as
 * a side effect, persists the freshly computed value back to FRAM
 * (lazy checkpointing — see the block comment above).  The unlock
 * window is two store instructions, so the power-loss exposure of
 * the save itself is negligible.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
int
tiku_vfs_tree_boot_cold_boots_read(char *buf, size_t max)
{
    uint32_t now = (uint32_t)tiku_clock_seconds();
    uint32_t lifetime = lifetime_at_boot + now;

    /* Push the freshly-computed lifetime back to FRAM so a
     * subsequent power loss does not lose the time covered by
     * this run. The cell write owns the MPU unlock window. */
    tiku_persist_cell_write_u32(&lifetime_cell, lifetime);

    return snprintf(buf, max, "%lu\n", (unsigned long)lifetime);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/stage                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/stage.
 *
 * Renders the boot sequencer's current stage as a word: "init",
 * "cpu", "memory", "peripherals", "services" or "complete" (the
 * names index tiku_boot_stage_e in order).  After a successful
 * boot this always reads "complete\n" — anything else means the
 * read raced the boot sequence or boot stalled, which is exactly
 * what makes the node useful from an external monitor.
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
/* /sys/boot/rstiv — raw SYSRSTIV value (hex, for scripting)                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/rstiv.
 *
 * Renders the latched SYSRSTIV snapshot as four hex digits
 * ("0x0016\n").  This is the escape hatch when the decoded names
 * are not enough — e.g. correlating against the device errata or
 * a vector reset_cause_str() does not know yet.
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
/* /sys/boot/clock/ — live clock frequencies                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/clock/mclk.
 *
 * Renders the measured/derived CPU master clock frequency in Hz as
 * a decimal line (e.g. "8000000\n").  Comes from the CPU HAL, not
 * the configured constant — so a DCO that failed to lock shows up
 * here as the wrong number.
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
 * Renders the auxiliary clock frequency in Hz as a decimal line —
 * normally 32768 from the LFXT crystal; a different value means
 * the crystal failed and the fallback source is in use.
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
/*                                                                            */
/*   violations — MPU segment violation flags from the current boot, as a    */
/*                hex bitmask. Cleared on every fresh boot (.bss-backed).    */
/*   count      — Persistent counter incremented on every violation,         */
/*                surviving the fault-triggered reset on platforms with a    */
/*                NOLOAD diagnostic region (RP2350 .mpu_diag). Decimal.      */
/*   last_addr  — MMFAR snapshot of the most recent violation, hex. Lets a   */
/*                postmortem boot answer "which pointer killed me last       */
/*                time?" via `cat /sys/boot/mpu/last_addr`.                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/mpu/violations.
 *
 * Renders the current boot's violation flag bitmask as two hex
 * digits ("0x00\n" when clean).  Bit meanings are defined by
 * kernel/memory/tiku_mpu.c; non-zero means a segment violation
 * fired since this boot.
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
 * Renders the cumulative violation count as a decimal line.  On
 * platforms with a NOLOAD diagnostic region the counter survives
 * the reset that the violation itself triggers, so a crash-loop
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
 * /sys/boot directory table.
 *
 * Exported so tiku_vfs_tree_sys.c can attach it as the "boot"
 * directory; the entry count travels as TIKU_VFS_TREE_BOOT_NCHILD
 * (asserted below).  "count" reuses the exported boot-counter
 * read handler that also backs the top-level /sys/boot_count.
 */
/*
 * /sys/boot/hang — the process the check-in watchdog caught wedging the
 * cooperative scheduler before the last reset, or "none".  Names the culprit
 * so a hang reboot is a diagnosable, quarantinable event instead of an
 * anonymous board reset.
 */
static int boot_hang_read(char *buf, size_t max)
{
    int8_t pid = tiku_hang_last_pid();

    if (pid < 0) {
        return snprintf(buf, max, "none\n");
    }
    return snprintf(buf, max, "%d %s\n", (int)pid, tiku_hang_last_name());
}

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
 * @brief Capture the reset cause and bump the FRAM boot counter.
 *
 * Runs as the first step of tiku_vfs_tree_init() — see the header
 * for why ordering matters (SYSRSTIV reads are destructive).
 *
 * Sequence:
 *   1. Latch SYSRSTIV into boot_reset_cause (MSP430 only; RP2350
 *      leaves it 0 = "none").
 *   2. Validate both persist cells — tiku_persist_cell_init()
 *      primes a virgin (all-zero or junk) FRAM to 0 with the
 *      gate stamped last, and keeps real persisted values.
 *   3. Increment the boot counter (so the very first boot reads
 *      1), refresh the SRAM mirror, and snapshot the lifetime
 *      accumulator for cold_boots reads.
 */
void
tiku_vfs_tree_boot_init(void)
{
    /* Capture the reset cause before anything else clears it, via the
     * per-arch HAL rather than a raw MSP430 register: it returns an
     * MSP430-SYSRSTIV-compatible code on every platform (MSP430 latches
     * SYSRSTIV, RP2350 maps WD_REASON, Ambiq is 0 until its RSTGEN decode
     * lands).  So /sys/boot/{rstiv,reason,last} are meaningful cross-platform
     * instead of the VFS layer reading a chip-specific register directly. */
    boot_reset_cause = tiku_common_reset_reason();

    /* Capture the check-in watchdog's culprit (if the last reset was a
     * detected hang) into this boot's view and clear the cross-reset record.
     * One-shot: the culprit is quarantined for the recovery boot only, and
     * /sys/boot/hang reports it for the life of this boot. */
    tiku_hang_boot_init();

    /* Validate/prime the cells, then bump the boot counter. The
     * SRAM mirror is what the VFS read returns so the read path
     * does not unlock the MPU. */
    (void)tiku_persist_cell_init(&boot_count_cell);
    (void)tiku_persist_cell_init(&lifetime_cell);

    tiku_persist_cell_write_u32(&boot_count_cell,
                                boot_count_persist + 1U);
    boot_count_value = boot_count_persist;
    lifetime_at_boot = lifetime_seconds_persist;
}

/**
 * @brief Override the boot count exposed via /sys/boot_count.
 *
 * Public API (declared in tiku_vfs_tree.h).  Called from the
 * hibernate resume path, which restores a snapshotted counter
 * value after waking from a hibernation image: only the SRAM
 * mirror is touched — the FRAM cell keeps its true monotonic
 * count.
 *
 * @param count  Value subsequent /sys/boot_count reads will report
 */
void
tiku_vfs_set_boot_count(uint32_t count)
{
    boot_count_value = count;
}
