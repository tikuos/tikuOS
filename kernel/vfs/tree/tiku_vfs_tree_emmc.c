/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_emmc.c - /sys/emmc VFS nodes (Apollo510 SDIO0 + 8 GB eMMC).
 *
 * State (writable: the lifecycle verbs up/down/sleep/wake), CID, capacity,
 * bus clock and width.  Reads come from driver bookkeeping only, so a read
 * while the SDIO0 domain is unpowered cannot stall the APB.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_emmc.h"
#include "tiku.h"
#include <arch/ambiq/tiku_emmc_arch.h>
#include <stdio.h>
#include <string.h>

/** @brief "down", "asleep" or "up", from driver bookkeeping only. */
static int
emmc_state_read(char *buf, size_t max)
{
    const char *st = !tiku_emmc_powered() ? "down"
                   : tiku_emmc_asleep()   ? "asleep"
                                          : "up";
    return snprintf(buf, max, "%s\n", st);
}

/**
 * @brief The card identity on one line, or "unidentified".
 *
 * Manufacturer, OEM, product name, revision, serial and manufacture date;
 * the serial and date tell one card from another.
 */
static int
emmc_cid_read(char *buf, size_t max)
{
    const tiku_emmc_id_t *id = tiku_emmc_id();
    if (id->mfr_id == 0u) {
        return snprintf(buf, max, "unidentified\n");
    }
    return snprintf(buf, max, "mfr %02x oem %04x '%s' rev %02x serial %08lx"
                    " %u/%u\n", id->mfr_id, id->oem_id, id->product, id->rev,
                    (unsigned long)id->serial, id->mfg_month, id->mfg_year);
}

/**
 * @brief Capacity in bytes, computed in 64 bits.
 *
 * An 8 GB card holds more bytes than a uint32_t counts, so the size is
 * computed in 64 bits and printed in two decimal halves.
 */
static int
emmc_size_read(char *buf, size_t max)
{
    uint64_t bytes = (uint64_t)tiku_emmc_capacity_blocks() * 512u;
    return snprintf(buf, max, "%lu%09lu\n",
                    (unsigned long)(bytes / 1000000000u),
                    (unsigned long)(bytes % 1000000000u));
}

/** @brief Bus clock in Hz (0 when the host is down). */
static int
emmc_hz_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_emmc_clock_hz());
}

/** @brief Bus width in bits (0 when the host is down). */
static int
emmc_width_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_emmc_bus_width());
}

/**
 * @brief Write handler for /sys/emmc/state: the lifecycle verbs.
 *
 * "up" walks the full init ladder, "down" releases the SDIO0 domain,
 * "sleep"/"wake" drive CMD5 retention; the verbs match /sys/psram/state so
 * the storage lifecycle devices read and drive alike.
 */
static int
emmc_state_write(const char *buf, size_t len)
{
    char v[8];
    size_t n = 0;

    while (n < len && n < sizeof(v) - 1u &&
           buf[n] != '\n' && buf[n] != '\r' && buf[n] != '\0') {
        v[n] = buf[n];
        n++;
    }
    v[n] = '\0';

    if (strcmp(v, "up") == 0) {
        return (tiku_emmc_init() == TIKU_EMMC_OK) ? 0 : -1;
    }
    if (strcmp(v, "down") == 0) {
        tiku_emmc_deinit();
        return 0;
    }
    if (strcmp(v, "sleep") == 0) {
        return (tiku_emmc_sleep() == TIKU_EMMC_OK) ? 0 : -1;
    }
    if (strcmp(v, "wake") == 0) {
        return (tiku_emmc_wake() == TIKU_EMMC_OK) ? 0 : -1;
    }
    return -1;
}

const tiku_vfs_node_t tiku_vfs_tree_emmc_children[] = {
    { "state", TIKU_VFS_FILE, emmc_state_read, emmc_state_write, NULL, 0,
      NULL, NULL, TIKU_VFS_CAP_SYS },
    { "cid",   TIKU_VFS_FILE, emmc_cid_read,   NULL, NULL, 0 },
    { "size",  TIKU_VFS_FILE, emmc_size_read,  NULL, NULL, 0 },
    { "hz",    TIKU_VFS_FILE, emmc_hz_read,    NULL, NULL, 0 },
    { "width", TIKU_VFS_FILE, emmc_width_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_emmc_children) /
               sizeof(tiku_vfs_tree_emmc_children[0])
               == TIKU_VFS_TREE_EMMC_NCHILD,
               "TIKU_VFS_TREE_EMMC_NCHILD out of sync");
