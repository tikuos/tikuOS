/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_usb.c - /sys/usb and /sys/store VFS nodes.
 *
 * Read-only: these report what the controller and the store are, and every
 * value comes from hardware or from the medium rather than from a cache.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_usb.h"
#include "tiku.h"
#include <arch/ra8p1/tiku_usbhs_arch.h>
#include <arch/ra8p1/tiku_store_arch.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/usb                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/usb/state.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_state_read(char *buf, size_t max)
{
    static const char *const name[5] = {
        "powered", "default", "address", "configured", "suspend"
    };
    unsigned d;

    if (!tiku_ra8p1_usbhs_up_state()) {
        return snprintf(buf, max, "down\n");
    }
    d = (unsigned)tiku_ra8p1_usbhs_devstate();
    return snprintf(buf, max, "%s\n", name[d < 5u ? d : 4u]);
}

/**
 * @brief Read handler for /sys/usb/speed.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_speed_read(char *buf, size_t max)
{
    static const char *const name[3] = { "none", "full", "high" };
    unsigned s = (unsigned)tiku_ra8p1_usbhs_speed();

    return snprintf(buf, max, "%s\n", name[s < 3u ? s : 0u]);
}

/**
 * @brief Read handler for /sys/usb/address.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_addr_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_ra8p1_usbhs_address());
}

/**
 * @brief Read handler for /sys/usb/config.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_config_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_ra8p1_usbhs_configured());
}

/*
 * The interrupt count is the one number that distinguishes "enumeration is
 * being carried by the interrupt" from "the CPU happened to be free": EP0 has
 * no polling path, so a configured device with irq at zero is impossible.
 */

/**
 * @brief Read handler for /sys/usb/irq.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_irq_read(char *buf, size_t max)
{
    uint32_t irqs = 0, dvst = 0;

    tiku_ra8p1_usbhs_irq_stats(&irqs, &dvst);
    return snprintf(buf, max, "%lu %lu\n",
                    (unsigned long)irqs, (unsigned long)dvst);
}

/**
 * @brief Read handler for /sys/usb/cbw.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int usb_cbw_read(char *buf, size_t max)
{
    uint32_t c = 0, rd = 0, wr = 0, bad = 0;

    tiku_ra8p1_usbhs_msc_stats(&c, &rd, &wr, &bad);
    return snprintf(buf, max, "%lu %lu %lu %lu\n",
                    (unsigned long)c, (unsigned long)rd,
                    (unsigned long)wr, (unsigned long)bad);
}

/*---------------------------------------------------------------------------*/
/* /sys/store                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/store/name.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int store_name_read(char *buf, size_t max)
{
    char name[TIKU_STORE_NAME_MAX + 1u];
    uint32_t len = 0;

    if (!tiku_ra8p1_store_info(name, &len)) {
        return snprintf(buf, max, "-\n");
    }
    return snprintf(buf, max, "%s\n", name);
}

/**
 * @brief Read handler for /sys/store/bytes.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int store_bytes_read(char *buf, size_t max)
{
    char name[TIKU_STORE_NAME_MAX + 1u];
    uint32_t len = 0;

    (void)tiku_ra8p1_store_info(name, &len);
    return snprintf(buf, max, "%lu\n", (unsigned long)len);
}

/*
 * `present` re-derives the CRC from the medium rather than trusting the
 * header it sits beside, so reading it is a check and not a claim.  It costs
 * a full pass over the blob, which is why it is a separate node from the
 * name and length that merely read a header.
 */

/**
 * @brief Read handler for /sys/store/present.
 *
 * @param buf Output buffer
 * @param max Capacity of @p buf
 * @return Bytes written, or -1
 */
static int store_present_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    tiku_ra8p1_store_verify() ? "ok" : "no");
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t tiku_vfs_tree_usb_children[] = {
    { "state",   TIKU_VFS_FILE, usb_state_read,  NULL, NULL, 0 },
    { "speed",   TIKU_VFS_FILE, usb_speed_read,  NULL, NULL, 0 },
    { "address", TIKU_VFS_FILE, usb_addr_read,   NULL, NULL, 0 },
    { "config",  TIKU_VFS_FILE, usb_config_read, NULL, NULL, 0 },
    { "irq",     TIKU_VFS_FILE, usb_irq_read,    NULL, NULL, 0 },
    { "cbw",     TIKU_VFS_FILE, usb_cbw_read,    NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_usb_children) /
               sizeof(tiku_vfs_tree_usb_children[0])
               == TIKU_VFS_TREE_USB_NCHILD,
               "TIKU_VFS_TREE_USB_NCHILD out of sync");

const tiku_vfs_node_t tiku_vfs_tree_store_children[] = {
    { "name",    TIKU_VFS_FILE, store_name_read,    NULL, NULL, 0 },
    { "bytes",   TIKU_VFS_FILE, store_bytes_read,   NULL, NULL, 0 },
    { "present", TIKU_VFS_FILE, store_present_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_store_children) /
               sizeof(tiku_vfs_tree_store_children[0])
               == TIKU_VFS_TREE_STORE_NCHILD,
               "TIKU_VFS_TREE_STORE_NCHILD out of sync");
