/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpio.h - /dev/gpio and /dev/gpio_dir VFS nodes
 *
 * Linkage contract for the GPIO subtrees: both children tables are
 * exported together with the port count, consumed by the /dev
 * assembly in tiku_vfs_tree_dev.c.  Unlike the fixed-size NCHILD
 * macros of other modules, the count here is derived from the
 * device header's TIKU_DEVICE_HAS_PORTn flags so it tracks the
 * selected silicon automatically.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_GPIO_H_
#define TIKU_VFS_TREE_GPIO_H_

#include "tiku.h"
#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Number of GPIO ports exposed under /dev/gpio and
 *        /dev/gpio_dir.
 *
 * Computed from the per-device TIKU_DEVICE_HAS_PORTn macros (each
 * 0 or 1), so selecting a different MSP430 variant resizes both
 * tables without touching this module.  Every device header must
 * define all four flags.
 */
#define TIKU_VFS_TREE_GPIO_NPORTS ( \
    TIKU_DEVICE_HAS_PORT1 + TIKU_DEVICE_HAS_PORT2 + \
    TIKU_DEVICE_HAS_PORT3 + TIKU_DEVICE_HAS_PORT4)

/**
 * @brief /dev/gpio children: one "1".."4" directory per available
 *        port, each holding eight pin files "0".."7".
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpio_children[];

/**
 * @brief /dev/gpio_dir children: one direction-summary file per
 *        available port ("IIOOIIII\n" style).
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_gpio_dir_children[];

/**
 * @brief Ring /dev/gpio/<port>/<pin> watchers after a hardware edge.
 *
 * The GPIO edge-interrupt to VFS-watch bridge: the port ISR
 * (arch/msp430/tiku_gpio_irq_arch.c) calls this for the pin that
 * fired, ringing tiku_vfs_notify() on that pin node so a rule or
 * `watch` on it reacts to the physical edge.  ISR-safe; an
 * out-of-range or device-absent port/pin is a no-op.
 *
 * @param port  Port number (1-based, P1..P4)
 * @param pin   Pin number (0..7)
 */
void tiku_vfs_tree_gpio_notify(uint8_t port, uint8_t pin);

#endif /* TIKU_VFS_TREE_GPIO_H_ */
