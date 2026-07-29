/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_emmc.c - /sys/emmc VFS nodes (Apollo510 SDIO0 + 8 GB eMMC).
 *
 * State, CID, capacity, bus clock and width, all read-only.  Every value comes
 * from the driver's own bookkeeping and nothing here touches the card or host
 * controller, so a read while the SDIO0 domain is unpowered cannot stall the APB.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_emmc.h"
#include "tiku.h"
#include <arch/ambiq/tiku_emmc_arch.h>
#include <stdio.h>

/** @brief The lifecycle rung, from driver bookkeeping only. */
static int
emmc_state_read(char *buf, size_t max)
{
    const char *st = !tiku_emmc_powered() ? "down"
                   : tiku_emmc_asleep()   ? "asleep"
                                          : "up";
    return snprintf(buf, max, "%s\n", st);
}

/**
 * @brief Identity in one line.
 *
 * The day-one trophy, made greppable.  Serial and date are here because
 * they are how you tell one board's card from another's when a result looks
 * strange -- this is the same physical part that reported 'IS008' in E1.
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
 * @brief Capacity in BYTES, which is 64-bit on this part.
 *
 * 15 307 776 blocks x 512 is 7.84 GB, past what a uint32_t holds, so the
 * arithmetic is done in 64 bits and printed as such.
 */
static int
emmc_size_read(char *buf, size_t max)
{
    uint64_t bytes = (uint64_t)tiku_emmc_capacity_blocks() * 512u;
    return snprintf(buf, max, "%lu%09lu\n",
                    (unsigned long)(bytes / 1000000000u),
                    (unsigned long)(bytes % 1000000000u));
}

/** @brief Live bus clock (0 when the host is down). */
static int
emmc_hz_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_emmc_clock_hz());
}

/** @brief Live bus width in bits (0 when the host is down). */
static int
emmc_width_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_emmc_bus_width());
}

const tiku_vfs_node_t tiku_vfs_tree_emmc_children[] = {
    { "state", TIKU_VFS_FILE, emmc_state_read, NULL, NULL, 0 },
    { "cid",   TIKU_VFS_FILE, emmc_cid_read,   NULL, NULL, 0 },
    { "size",  TIKU_VFS_FILE, emmc_size_read,  NULL, NULL, 0 },
    { "hz",    TIKU_VFS_FILE, emmc_hz_read,    NULL, NULL, 0 },
    { "width", TIKU_VFS_FILE, emmc_width_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_emmc_children) /
               sizeof(tiku_vfs_tree_emmc_children[0])
               == TIKU_VFS_TREE_EMMC_NCHILD,
               "TIKU_VFS_TREE_EMMC_NCHILD out of sync");
