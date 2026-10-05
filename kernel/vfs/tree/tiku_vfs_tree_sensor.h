/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_sensor.h - /dev/sensors VFS nodes.
 *
 * Linkage contract for the temperature-sensor subtree: which drivers this
 * build has, the children table and its entry count, kept in step by a
 * _Static_assert beside the table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_SENSOR_H_
#define TIKU_VFS_TREE_SENSOR_H_

#include "tiku.h"
#include <kernel/vfs/tiku_vfs.h>

/** @brief 1 when the sensor kit's I2C drivers (MCP9808, ADT7410) are built. */
#if (TIKU_KIT_SENSORS_ENABLE + 0)
#define TIKU_VFS_SENSOR_I2C 1
#else
#define TIKU_VFS_SENSOR_I2C 0
#endif

/** @brief 1 when the DS18B20 driver is built on a board with 1-Wire. */
#if (TIKU_KIT_SENSORS_ENABLE + 0) && (TIKU_BOARD_OW_AVAILABLE + 0)
#define TIKU_VFS_SENSOR_OW 1
#else
#define TIKU_VFS_SENSOR_OW 0
#endif

/** @brief Entry count of /dev/sensors: "enabled" plus one per driver. */
#define TIKU_VFS_TREE_SENSOR_NCHILD                                          \
    (1 + 2 * TIKU_VFS_SENSOR_I2C + TIKU_VFS_SENSOR_OW)

/**
 * @brief /dev/sensors children: enabled, mcp9808/, adt7410/, ds18b20/,
 *        each directory present only when its driver is built.
 *
 * Referenced by the /dev directory table in tiku_vfs_tree_dev.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_sensor_children[];

#endif /* TIKU_VFS_TREE_SENSOR_H_ */
