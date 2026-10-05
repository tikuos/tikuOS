/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_net.h - /sys/net VFS nodes.
 *
 * Linkage contract for the network subtree: which services this build has,
 * the children table and its entry count, kept in step by a _Static_assert
 * beside the table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_NET_H_
#define TIKU_VFS_TREE_NET_H_

#include <kernel/vfs/tiku_vfs.h>
#include "tiku_vfs_tree_wifi.h"     /* TIKU_VFS_NET_WIFI */

/** @brief 1 when the IPv4 network kit is built, else 0. */
#if (TIKU_KIT_NET_ENABLE + 0)
#define TIKU_VFS_NET_IPV4 1
#else
#define TIKU_VFS_NET_IPV4 0
#endif

/** @brief 1 when the DNS resolver is built (always in a full network kit). */
#if TIKU_VFS_NET_IPV4 && \
    (!(TIKU_KIT_NET_MIN + 0) || (TIKU_KITS_NET_DNS_ENABLE + 0))
#define TIKU_VFS_NET_DNS 1
#else
#define TIKU_VFS_NET_DNS 0
#endif

/** @brief 1 when the DHCP client is built, else 0. */
#if TIKU_VFS_NET_IPV4 && (TIKU_KITS_NET_DHCP_ENABLE + 0)
#define TIKU_VFS_NET_DHCP 1
#else
#define TIKU_VFS_NET_DHCP 0
#endif

/** @brief Entry count of /sys/net: "enabled" plus one per built service. */
#define TIKU_VFS_TREE_NET_NCHILD                                             \
    (1 + TIKU_VFS_NET_IPV4 + TIKU_VFS_NET_DNS + TIKU_VFS_NET_DHCP +          \
     TIKU_VFS_NET_WIFI)

/**
 * @brief /sys/net children: wifi/, enabled, ipv4/, dns/, dhcp/, each
 *        directory present only when its service is built.
 *
 * Referenced by the /sys directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_net_children[];

#endif /* TIKU_VFS_TREE_NET_H_ */
