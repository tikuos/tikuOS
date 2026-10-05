/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpio.h - /dev/gpio, /dev/gpio_dir and /dev/gpio_owner nodes.
 *
 * Linkage contract for the GPIO subtrees: the three child tables plus the port
 * count, consumed by the /dev assembly.  The count comes from the platform's
 * table in tiku_gpio_geometry.h, so it tracks the selected silicon.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_GPIO_H_
#define TIKU_VFS_TREE_GPIO_H_

#include "tiku.h"
#include <kernel/vfs/tiku_vfs.h>
#include <interfaces/gpio/tiku_gpio_geometry.h>

/**
 * @brief Number of GPIO ports under /dev/gpio, /dev/gpio_dir and
 *        /dev/gpio_owner.
 *
 * One per entry in the platform's TIKU_GPIO_PORTS table, so selecting another
 * device resizes all three tables without touching this module.
 */
#define TIKU_VFS_TREE_GPIO_NPORTS TIKU_GPIO_PORT_COUNT

/**
 * @brief /dev/gpio children: one directory per port, named by its number,
 *        each holding one file per pin.
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpio_children[];

/**
 * @brief /dev/gpio_dir children: one direction-summary file per port
 *        ("IIOOIIII\n" style).
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpio_dir_children[];

/**
 * @brief /dev/gpio_owner children: one read-only file per port, with a
 *        "<pin> <owner>\n" line per pin and "free" for an unowned pin.
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpio_owner_children[];

/**
 * @brief Ring /dev/gpio/<port>/<pin> watchers after a hardware edge.
 *
 * The GPIO edge-interrupt to VFS-watch bridge: the port ISR calls this for the
 * pin that fired, so a rule or `watch` on it reacts to the physical edge.
 *
 * @param port  Port number; MSP430 port J is 255
 * @param pin   Pin within the port
 * @note ISR-safe.  A port or pin outside the platform geometry is a no-op.
 */
void tiku_vfs_tree_gpio_notify(uint8_t port, uint8_t pin);

#endif /* TIKU_VFS_TREE_GPIO_H_ */
