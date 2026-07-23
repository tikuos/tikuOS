/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpu.c - /sys/gpu VFS nodes (Apollo510 2.5D GPU)
 *
 * Read-only views of the from-scratch Nema-class GPU driver:
 *
 *   /sys/gpu/power   "on"/"off" -- is the GFX power domain up
 *   /sys/gpu/id      IDREG (fixed silicon id) in hex, or "off"
 *   /sys/gpu/status  STATUS register in hex + "busy"/"idle", or "off"
 *   /sys/gpu/irqs    count of GPU completion interrupts serviced
 *
 * All nodes are power-safe: id/status touch GPU registers only after
 * confirming the domain is powered (an unpowered register access would
 * fault), so `cat`-ing them with the GPU off reports "off" rather than
 * wedging. The whole subtree is compiled only under TIKU_DRV_GPU_ENABLE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_vfs_tree_gpu.h"
#include "tiku.h"
#include <arch/ambiq/tiku_gpu_arch.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/gpu/power                                                            */
/*---------------------------------------------------------------------------*/

/** @brief 1 if the GFX power domain is up (safe: reads PWRCTRL, not the GPU). */
static int
gpu_power_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_gpu_powered() ? "on" : "off");
}

/*---------------------------------------------------------------------------*/
/* /sys/gpu/id                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Fixed GPU id (IDREG) in hex, or "off" when the domain is down. */
static int
gpu_id_read(char *buf, size_t max)
{
    if (!tiku_gpu_powered()) {
        return snprintf(buf, max, "off\n");
    }
    return snprintf(buf, max, "0x%08lx\n", (unsigned long)tiku_gpu_id());
}

/*---------------------------------------------------------------------------*/
/* /sys/gpu/status                                                           */
/*---------------------------------------------------------------------------*/

/** @brief STATUS register in hex + a busy/idle word, or "off" when unpowered. */
static int
gpu_status_read(char *buf, size_t max)
{
    uint32_t s;

    if (!tiku_gpu_powered()) {
        return snprintf(buf, max, "off\n");
    }
    s = tiku_gpu_status();
    return snprintf(buf, max, "0x%08lx %s\n",
                    (unsigned long)s, (s != 0u) ? "busy" : "idle");
}

/*---------------------------------------------------------------------------*/
/* /sys/gpu/irqs                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Number of GPU completion interrupts serviced since init. */
static int
gpu_irqs_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_gpu_irq_count());
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * /sys/gpu directory table. Exported so tiku_vfs_tree_sys.c can attach it
 * as the "gpu" directory; the entry count travels as
 * TIKU_VFS_TREE_GPU_NCHILD (asserted below). All nodes read-only.
 */
const tiku_vfs_node_t tiku_vfs_tree_gpu_children[] = {
    { "power",  TIKU_VFS_FILE, gpu_power_read,  NULL, NULL, 0 },
    { "id",     TIKU_VFS_FILE, gpu_id_read,     NULL, NULL, 0 },
    { "status", TIKU_VFS_FILE, gpu_status_read, NULL, NULL, 0 },
    { "irqs",   TIKU_VFS_FILE, gpu_irqs_read,   NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_gpu_children) /
               sizeof(tiku_vfs_tree_gpu_children[0])
               == TIKU_VFS_TREE_GPU_NCHILD,
               "TIKU_VFS_TREE_GPU_NCHILD out of sync");
