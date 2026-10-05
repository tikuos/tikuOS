/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_net.c - /sys/net VFS nodes.
 *
 * Reads report existing state and never start a service, poll a protocol or
 * send a packet.  Settings are volatile and not in the durable configuration
 * journal; handlers run in process context, as the network services need.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_net.h"
#include <stdio.h>
#include <string.h>
#if TIKU_VFS_NET_IPV4
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#endif
#if TIKU_VFS_NET_DNS
#include <tikukits/net/ipv4/tiku_kits_net_dns.h>
#endif
#if TIKU_VFS_NET_DHCP
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>
#endif

/*---------------------------------------------------------------------------*/
/* /sys/net/enabled                                                          */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_desc_t net_bool = TIKU_VFS_DESC(
    TIKU_VFS_T_BOOL, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);

/** @brief /sys/net/enabled: 1 when this build carries the IPv4 stack. */
static int
enabled_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_VFS_NET_IPV4);
}

#if TIKU_VFS_NET_IPV4

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_desc_t net_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_CHEAP);
static const tiku_vfs_desc_t net_bytes = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_BYTES, TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t net_number = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);

/** First octet of the loopback block 127.0.0.0/8. */
#define NET_LOOPBACK_OCTET   127u

/** First octet of the multicast and reserved space, 224.0.0.0 and above. */
#define NET_MULTICAST_OCTET  224u

/** @brief Render a dotted-quad address and a newline. */
static int
address_text(char *buf, size_t max, const uint8_t *addr)
{
    return snprintf(buf, max, "%u.%u.%u.%u\n",
                    addr[0], addr[1], addr[2], addr[3]);
}

/** @brief Length of a written value without trailing blanks and line ends. */
static size_t
trim_end(const char *buf, size_t len)
{
    while (len > 0u && (buf[len - 1u] == ' ' || buf[len - 1u] == '\t' ||
                        buf[len - 1u] == '\r' || buf[len - 1u] == '\n')) {
        len--;
    }
    return len;
}

/**
 * @brief Parse exactly four decimal octets, without relying on a NUL.
 *
 * The address setters accept only a host address: unspecified, loopback,
 * multicast and reserved addresses are refused.
 *
 * @return 1 and @p addr filled on success, 0 otherwise
 */
static int
parse_address(const char *buf, size_t len, uint8_t addr[4])
{
    unsigned i;
    size_t pos = 0u;

    len = trim_end(buf, len);
    for (i = 0u; i < 4u; i++) {
        unsigned value = 0u;
        unsigned digits = 0u;

        while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
            if (++digits > 3u) {
                return 0;
            }
            value = value * 10u + (unsigned)(buf[pos++] - '0');
        }
        if (digits == 0u || value > 255u) {
            return 0;
        }
        addr[i] = (uint8_t)value;
        if (i != 3u && (pos == len || buf[pos++] != '.')) {
            return 0;
        }
    }
    return pos == len && addr[0] != 0u && addr[0] != NET_LOOPBACK_OCTET &&
           addr[0] < NET_MULTICAST_OCTET;
}

/*---------------------------------------------------------------------------*/
/* /sys/net/ipv4                                                             */
/*---------------------------------------------------------------------------*/

/** @brief /sys/net/ipv4/address: the source address. */
static int
address_read(char *buf, size_t max)
{
    return address_text(buf, max, tiku_kits_net_ipv4_get_addr());
}

/**
 * @brief /sys/net/ipv4/address: set the source address while offline.
 *
 * Refused with EBUSY while a transport is registered, a DHCP exchange is
 * running or a DNS query is outstanding, so no traffic changes source.
 */
static int
address_write(const char *buf, size_t len)
{
    uint8_t addr[4];

    if (!parse_address(buf, len, addr)) {
        return TIKU_VFS_EINVAL;
    }
    if (tiku_kits_net_ipv4_get_link() != NULL) {
        return TIKU_VFS_EBUSY;
    }
#if TIKU_VFS_NET_DHCP
    if (tiku_kits_net_dhcp_get_state() != TIKU_KITS_NET_DHCP_STATE_IDLE &&
        tiku_kits_net_dhcp_get_state() != TIKU_KITS_NET_DHCP_STATE_ERROR) {
        return TIKU_VFS_EBUSY;
    }
#endif
#if TIKU_VFS_NET_DNS
    if (tiku_kits_net_dns_get_state() == TIKU_KITS_NET_DNS_STATE_SENT) {
        return TIKU_VFS_EBUSY;
    }
#endif
    tiku_kits_net_ipv4_set_addr(addr);
    return TIKU_VFS_OK;
}

/** @brief /sys/net/ipv4/link: the registered transport, or "none". */
static int
link_read(char *buf, size_t max)
{
    const tiku_kits_net_link_t *link = tiku_kits_net_ipv4_get_link();
    const char *name = (link == NULL)       ? "none"
                     : (link->name == NULL) ? "unnamed"
                     :                        link->name;

    return snprintf(buf, max, "%s\n", name);
}

/** @brief /sys/net/ipv4/mtu: the build's MTU in bytes. */
static int
mtu_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_KITS_NET_MTU);
}

/** @brief /sys/net/ipv4/ttl: the TTL of outgoing packets. */
static int
ttl_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_KITS_NET_TTL);
}

static const tiku_vfs_node_t ipv4_children[] = {
    { .name = "address", .type = TIKU_VFS_FILE, .read = address_read,
      .write = address_write, .desc = &net_text,
      .req_cap = TIKU_VFS_CAP_NET },
    { .name = "link", .type = TIKU_VFS_FILE, .read = link_read,
      .desc = &net_text },
    { .name = "mtu", .type = TIKU_VFS_FILE, .read = mtu_read,
      .desc = &net_bytes },
    { .name = "ttl", .type = TIKU_VFS_FILE, .read = ttl_read,
      .desc = &net_number },
};

#endif /* TIKU_VFS_NET_IPV4 */

#if TIKU_VFS_NET_DNS

/*---------------------------------------------------------------------------*/
/* /sys/net/dns                                                              */
/*---------------------------------------------------------------------------*/

/** @brief /sys/net/dns/state: idle, sent, done or error. */
static int
dns_state_read(char *buf, size_t max)
{
    static const char *const names[] = { "idle", "sent", "done", "error" };
    unsigned state = (unsigned)tiku_kits_net_dns_get_state();

    return snprintf(buf, max, "%s\n",
                    (state < sizeof names / sizeof names[0]) ?
                    names[state] : "unknown");
}

/** @brief /sys/net/dns/server: the current query server, or "unset". */
static int
dns_server_read(char *buf, size_t max)
{
    const uint8_t *addr = tiku_kits_net_dns_get_server();

    if (addr == NULL) {
        return snprintf(buf, max, "unset\n");
    }
    return address_text(buf, max, addr);
}

/** @brief /sys/net/dns/default_server: the server the next query uses. */
static int
dns_default_read(char *buf, size_t max)
{
    uint8_t addr[4];

    tiku_kits_net_dns_default_server(addr);
    return address_text(buf, max, addr);
}

/** @brief /sys/net/dns/override: the boot-session override, or "auto". */
static int
dns_override_read(char *buf, size_t max)
{
    const uint8_t *addr = tiku_kits_net_dns_get_default_override();

    if (addr == NULL) {
        return snprintf(buf, max, "auto\n");
    }
    return address_text(buf, max, addr);
}

/** @brief /sys/net/dns/override: an address, or "auto" for none. */
static int
dns_override_write(const char *buf, size_t len)
{
    uint8_t addr[4];
    int automatic;

    len = trim_end(buf, len);
    automatic = (len == 4u && memcmp(buf, "auto", 4u) == 0);
    if (!automatic && !parse_address(buf, len, addr)) {
        return TIKU_VFS_EINVAL;
    }
    if (tiku_kits_net_dns_get_state() == TIKU_KITS_NET_DNS_STATE_SENT) {
        return TIKU_VFS_EBUSY;
    }
    if (tiku_kits_net_dns_set_default_override(automatic ? NULL : addr) !=
        TIKU_KITS_NET_OK) {
        return TIKU_VFS_EBUSY;
    }
    return TIKU_VFS_OK;
}

/** @brief /sys/net/dns/cache_flush: "flush" empties the answer cache. */
static int
dns_flush_write(const char *buf, size_t len)
{
    len = trim_end(buf, len);
    if (len != 5u || memcmp(buf, "flush", 5u) != 0) {
        return TIKU_VFS_EINVAL;
    }
    if (tiku_kits_net_dns_get_state() == TIKU_KITS_NET_DNS_STATE_SENT) {
        return TIKU_VFS_EBUSY;
    }
    tiku_kits_net_dns_cache_flush();
    return TIKU_VFS_OK;
}

static const tiku_vfs_node_t dns_children[] = {
    { .name = "state", .type = TIKU_VFS_FILE, .read = dns_state_read,
      .desc = &net_text },
    { .name = "server", .type = TIKU_VFS_FILE, .read = dns_server_read,
      .desc = &net_text },
    { .name = "default_server", .type = TIKU_VFS_FILE,
      .read = dns_default_read, .desc = &net_text },
    { .name = "override", .type = TIKU_VFS_FILE, .read = dns_override_read,
      .write = dns_override_write, .desc = &net_text,
      .req_cap = TIKU_VFS_CAP_NET },
    { .name = "cache_flush", .type = TIKU_VFS_FILE, .write = dns_flush_write,
      .desc = &net_text, .req_cap = TIKU_VFS_CAP_NET },
};

#endif /* TIKU_VFS_NET_DNS */

#if TIKU_VFS_NET_DHCP

/*---------------------------------------------------------------------------*/
/* /sys/net/dhcp                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_desc_t net_seconds = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_SECONDS, TIKU_VFS_FRESH_CACHED,
    TIKU_VFS_E_CHEAP);

/** @brief /sys/net/dhcp/state: the client's exchange state. */
static int
dhcp_state_read(char *buf, size_t max)
{
    static const char *const names[] = {
        "idle", "discover-sent", "requesting", "bound", "error"
    };
    unsigned state = (unsigned)tiku_kits_net_dhcp_get_state();

    return snprintf(buf, max, "%s\n",
                    (state < sizeof names / sizeof names[0]) ?
                    names[state] : "unknown");
}

/* One read handler per lease address; ENOTSUP while no lease is bound. */
#define LEASE_ADDRESS(name, field)                                           \
    static int name##_read(char *buf, size_t max)                            \
    {                                                                        \
        const tiku_kits_net_dhcp_lease_t *lease =                            \
            tiku_kits_net_dhcp_get_lease();                                  \
        if (lease == NULL) {                                                 \
            return TIKU_VFS_ENOTSUP;                                         \
        }                                                                    \
        return address_text(buf, max, lease->field);                         \
    }

LEASE_ADDRESS(lease_address, ip)
LEASE_ADDRESS(lease_netmask, mask)
LEASE_ADDRESS(lease_gateway, gateway)
LEASE_ADDRESS(lease_server, server)
LEASE_ADDRESS(lease_dns, dns)
#undef LEASE_ADDRESS

/** @brief /sys/net/dhcp/lease_seconds: ENOTSUP while no lease is bound. */
static int
lease_seconds_read(char *buf, size_t max)
{
    const tiku_kits_net_dhcp_lease_t *lease = tiku_kits_net_dhcp_get_lease();

    if (lease == NULL) {
        return TIKU_VFS_ENOTSUP;
    }
    return snprintf(buf, max, "%lu\n", (unsigned long)lease->lease_sec);
}

static const tiku_vfs_node_t dhcp_children[] = {
    { .name = "state", .type = TIKU_VFS_FILE, .read = dhcp_state_read,
      .desc = &net_text },
    { .name = "address", .type = TIKU_VFS_FILE, .read = lease_address_read,
      .desc = &net_text },
    { .name = "netmask", .type = TIKU_VFS_FILE, .read = lease_netmask_read,
      .desc = &net_text },
    { .name = "gateway", .type = TIKU_VFS_FILE, .read = lease_gateway_read,
      .desc = &net_text },
    { .name = "server", .type = TIKU_VFS_FILE, .read = lease_server_read,
      .desc = &net_text },
    { .name = "dns", .type = TIKU_VFS_FILE, .read = lease_dns_read,
      .desc = &net_text },
    { .name = "lease_seconds", .type = TIKU_VFS_FILE,
      .read = lease_seconds_read, .desc = &net_seconds },
};

#endif /* TIKU_VFS_NET_DHCP */

/*---------------------------------------------------------------------------*/
/* /sys/net                                                                  */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t tiku_vfs_tree_net_children[] = {
#if TIKU_VFS_NET_WIFI
    { .name = "wifi", .type = TIKU_VFS_DIR,
      .children = tiku_vfs_tree_wifi_children,
      .child_count = TIKU_VFS_TREE_WIFI_NCHILD },
#endif
    { .name = "enabled", .type = TIKU_VFS_FILE, .read = enabled_read,
      .desc = &net_bool },
#if TIKU_VFS_NET_IPV4
    { .name = "ipv4", .type = TIKU_VFS_DIR, .children = ipv4_children,
      .child_count = sizeof(ipv4_children) / sizeof(ipv4_children[0]) },
#endif
#if TIKU_VFS_NET_DNS
    { .name = "dns", .type = TIKU_VFS_DIR, .children = dns_children,
      .child_count = sizeof(dns_children) / sizeof(dns_children[0]) },
#endif
#if TIKU_VFS_NET_DHCP
    { .name = "dhcp", .type = TIKU_VFS_DIR, .children = dhcp_children,
      .child_count = sizeof(dhcp_children) / sizeof(dhcp_children[0]) },
#endif
};

_Static_assert(sizeof(tiku_vfs_tree_net_children) /
               sizeof(tiku_vfs_tree_net_children[0])
               == TIKU_VFS_TREE_NET_NCHILD,
               "TIKU_VFS_TREE_NET_NCHILD out of sync");
