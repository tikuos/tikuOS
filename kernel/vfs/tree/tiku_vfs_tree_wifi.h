/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_wifi.h - /sys/net/wifi VFS nodes.
 *
 * Linkage contract for the Wi-Fi subtree, present when a Wi-Fi radio driver
 * is built: the children table and its entry count, kept in step by a
 * _Static_assert beside the table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_WIFI_H_
#define TIKU_VFS_TREE_WIFI_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief 1 when a Wi-Fi radio driver is built, else 0.
 *
 * The subtree uses only the tiku_wireless_* interface, so either radio
 * (the CYW43439 or the ESP32-C61's own) provides it.
 */
#if (TIKU_DRV_WIFI_CYW43_ENABLE + 0) || (TIKU_DRV_WIFI_ESP_ENABLE + 0)
#define TIKU_VFS_NET_WIFI 1
#else
#define TIKU_VFS_NET_WIFI 0
#endif

/** @brief Entry count of /sys/net/wifi. */
#define TIKU_VFS_TREE_WIFI_NCHILD 11

/**
 * @brief /sys/net/wifi children: state, ssid, rssi_dbm, mac, scan_count,
 *        scan, scan_results/, disconnect, saved, profiles/,
 *        profile_lifetime.
 *
 * Defined only when TIKU_VFS_NET_WIFI is 1; referenced by /sys/net.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_wifi_children[];

#endif /* TIKU_VFS_TREE_WIFI_H_ */
