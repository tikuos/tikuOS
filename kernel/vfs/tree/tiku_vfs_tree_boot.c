/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_boot.c - /sys/boot VFS nodes and boot bookkeeping.
 *
 * Exposes the decoded reset reason, boot count, boot stage, hang culprit,
 * clock rates and MPU diagnostics.  The boot count and the lifetime uptime
 * are one persist ring, written once per boot.
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
/* BOOT REASON                                                               */
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
 * @brief Decode a SYSRSTIV-style reset code to its short name.
 *
 * Decodes the causes in the table (TI SLAU367): power faults, watchdog
 * variants, FRAM errors, password and MPU segment violations, and software
 * resets.  Any other value renders as "unknown"; /sys/boot/rstiv shows it raw.
 *
 * @param iv  Code from tiku_common_reset_reason() (even; FR5994 reports
 *            0x0000..0x002E, and FR6989 adds 0x0030)
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
    case 0x0022: return "mpu-pwviol";
    case 0x0024: return "cs-pwviol";
    case 0x0026: return "mpu-seg-ip";
    case 0x0028: return "mpu-seg-info";
    case 0x002A: return "mpu-seg1";
    case 0x002C: return "mpu-seg2";
    case 0x002E: return "mpu-seg3";
    case 0x0030: return "fram-acctime";
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
 * @return The full length of the text (snprintf-style)
 */
static int
boot_reason_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", reset_cause_str(boot_reset_cause));
}

/*
 * The boot record: the boots since the record was first written, and the
 * seconds of uptime summed over them.  It is a ring (TIKU_PERSIST_RING) of 16
 * slots where the medium spreads writes, written once per boot, so a boot
 * costs one write and the record's writes cycle through its slots.
 */
typedef struct {
    uint32_t count;     /**< boots, 1 on the first */
    uint32_t lifetime;  /**< seconds of uptime summed over them */
} boot_record_t;

/*
 * Gate key for the record's slot array; uninitialised memory matches it with
 * probability 2^-32.  A new key re-primes the record empty at the next boot.
 */
#define BOOT_RECORD_KEY  0xB007C002UL

TIKU_PERSIST_RING(boot_ring, boot_record_t, BOOT_RECORD_KEY, 16u);

/*
 * The cells an image before the boot record kept the count and the lifetime
 * in: read once to seed an empty record, then retired.
 */
#define BOOT_COUNT_MAGIC  0xB007C001UL
#define LIFETIME_MAGIC    0x4C494645UL /* 'LIFE' */
static TIKU_DURABLE uint32_t boot_count_persist;
TIKU_PERSIST_CELL(boot_count_cell, boot_count_persist,
                  BOOT_COUNT_MAGIC, NULL, 0);
static TIKU_DURABLE uint32_t lifetime_seconds_persist;
TIKU_PERSIST_CELL(lifetime_cell, lifetime_seconds_persist,
                  LIFETIME_MAGIC, NULL, 0);

/** This boot's count, served by the read handler. */
static uint32_t boot_count_value;

int
tiku_vfs_tree_boot_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)boot_count_value);
}

/*---------------------------------------------------------------------------*/
/* LAST RESET                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map a SYSRSTIV-style reset code to one of four user-facing
 *        categories.
 *
 * "watchdog" (counter overflow or password violation), "power" (brownout, SVS,
 * PMM/FRAM violation, or a clean cold start), "reboot" (software BOR/POR or the
 * RST pin), "other".  /sys/boot/reason keeps the detailed name.
 *
 * @param iv  Code from tiku_common_reset_reason()
 * @return Static category string; never NULL
 */
static const char *
last_reset_str(uint16_t iv)
{
    switch (iv) {
    case 0x0016: case 0x0018:
        return "watchdog";
    case 0x0002: case 0x000E: case 0x0020: case 0x001A:
        return "power";
    case 0x0004: case 0x0006: case 0x0014:
        return "reboot";
    case 0x0000:
        return "power";
    default:
        return "other";
    }
}

int
tiku_vfs_tree_boot_last_reset_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", last_reset_str(boot_reset_cause));
}

/*---------------------------------------------------------------------------*/
/* LIFETIME UPTIME                                                           */
/*---------------------------------------------------------------------------*/

/*
 * The lifetime, rendered by /sys/cold_boots despite its name, is the boot
 * record's sum plus this run's uptime.  A read writes nothing durable: it
 * notes the lifetime in a warm record (TIKU_RETAINED), and the next boot
 * stores the noted lifetime in the boot record when a warm reset ended the
 * run.  A power cut loses the run's uptime where retained data does not
 * survive one.
 */

/** Lifetime seconds as of the start of this boot. */
static uint32_t lifetime_at_boot;

/** Magic of a valid warm record ('WARM'). */
#define BOOT_WARM_MAGIC  0x5741524DUL

/** The lifetime noted by this run's last read, for the next boot. */
static TIKU_RETAINED struct {
    uint32_t magic;     /**< BOOT_WARM_MAGIC                     */
    uint32_t count;     /**< the boot that noted it              */
    uint32_t lifetime;  /**< seconds, as of the note             */
    uint32_t check;     /**< count ^ lifetime ^ BOOT_WARM_MAGIC  */
} boot_warm;

/** @brief Note @p lifetime as boot @p count's, in the warm record. */
static void
warm_note(uint32_t count, uint32_t lifetime)
{
#if TIKU_RETAINED_GATED
    uint16_t saved;

    tiku_atomic_enter();
    saved = tiku_mpu_unlock_nvm();
#endif
    boot_warm.magic = 0u;
    boot_warm.count = count;
    boot_warm.lifetime = lifetime;
    boot_warm.check = count ^ lifetime ^ BOOT_WARM_MAGIC;
    boot_warm.magic = BOOT_WARM_MAGIC;
#if TIKU_RETAINED_GATED
    tiku_mpu_lock_nvm(saved);
    tiku_atomic_exit();
#endif
}

int
tiku_vfs_tree_boot_cold_boots_read(char *buf, size_t max)
{
    uint32_t lifetime = lifetime_at_boot + (uint32_t)tiku_clock_seconds();

    warm_note(boot_count_value, lifetime);
    return snprintf(buf, max, "%lu\n", (unsigned long)lifetime);
}

/*---------------------------------------------------------------------------*/
/* BOOT STAGE                                                                */
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
 * @return The full length of the text (snprintf-style)
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
/* RESET CODE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/boot/rstiv.
 *
 * Renders the latched reset-cause code as four hex digits ("0x0016\n"), for
 * a code that reset_cause_str() does not decode.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
boot_rstiv_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%04x\n", boot_reset_cause);
}

/*---------------------------------------------------------------------------*/
/* CLOCK RATES                                                               */
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
 * @return The full length of the text (snprintf-style)
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
 * @return The full length of the text (snprintf-style)
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
 * @return The full length of the text (snprintf-style)
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
 * (oscillator failure latched since boot), "0\n" otherwise.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
boot_clock_fault_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_cpu_clock_has_fault() ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* MPU DIAGNOSTICS                                                           */
/*---------------------------------------------------------------------------*/
/*
 *   violations — MPU segment violation flags, as a hex bitmask.  RA8P1
 *                reports them from its fault record and RP2350 and Ambiq
 *                from the NOLOAD .mpu_diag region, both of which survive
 *                the reset a violation causes and not a power-off;
 *                MSP430 latches them in SRAM for the current boot; the
 *                other ports report 0.
 *   count      — violations counted across fault-triggered resets, kept in
 *                the NOLOAD .mpu_diag region on RP2350 and Ambiq; 0 on the
 *                other ports.  Decimal.
 *   last_addr  — fault address of the most recent violation: the MMFAR
 *                snapshot on RP2350 and Ambiq, 0 on the other ports.  Hex.
 */

/**
 * @brief Read handler for /sys/boot/mpu/violations.
 *
 * Renders the violation flags in hex, at least two digits ("0x00\n" when
 * clean), in the port's encoding (tiku_mpu_arch_get_violation_flags()).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
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
 * exists the counter survives the reset the violation triggers, so a crash
 * loop raises it from one boot to the next.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
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
 * Renders the faulting address of the most recent violation as eight hex
 * digits ("0x20003ffc\n"): the MMFAR snapshot on RP2350 and Ambiq.  Reads 0
 * before any violation is recorded and on the other ports.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
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
 * Renders the process the check-in watchdog caught holding the cooperative
 * scheduler before the last reset, as "<pid> <name>", or "none".
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

void
tiku_vfs_tree_boot_init(void)
{
    /* Capture the reset cause before anything else clears it.  The HAL
     * returns a SYSRSTIV-style code on every port, which /sys/boot/rstiv,
     * /sys/boot/reason and /sys/last_reset render. */
    boot_reset_cause = tiku_common_reset_reason();

    /* Copy the check-in watchdog's culprit record, set when the last reset
     * was a detected hang, into this boot's view and clear the cross-reset
     * record, so /sys/boot/hang reports it for this boot only. */
    tiku_hang_boot_init();

    /* The last boot's record, seeded from the older cells when the ring
     * holds none; the lifetime the last run noted, when a warm reset ended
     * it; then one write for this boot. */
    {
        boot_record_t rec;

        (void)tiku_persist_ring_init(&boot_ring);
        if (!tiku_persist_ring_read(&boot_ring, &rec)) {
            rec.count = tiku_persist_cell_valid(&boot_count_cell)
                        ? boot_count_persist : 0u;
            rec.lifetime = tiku_persist_cell_valid(&lifetime_cell)
                           ? lifetime_seconds_persist : 0u;
        }
        if (boot_warm.magic == BOOT_WARM_MAGIC &&
            boot_warm.check ==
                (boot_warm.count ^ boot_warm.lifetime ^ BOOT_WARM_MAGIC) &&
            boot_warm.count == rec.count && boot_warm.lifetime > rec.lifetime) {
            rec.lifetime = boot_warm.lifetime;
        }
        rec.count++;
        /* Boot continues if the record cannot be saved; the older cells are
         * retired only once it is. */
        if (tiku_persist_ring_write(&boot_ring, &rec) == TIKU_MEM_OK) {
            (void)tiku_persist_cell_retire(&boot_count_cell);
            (void)tiku_persist_cell_retire(&lifetime_cell);
        }
        boot_count_value = rec.count;
        lifetime_at_boot = rec.lifetime;
        warm_note(rec.count, rec.lifetime);
    }
}
