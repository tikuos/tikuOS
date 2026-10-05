/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_power.c - /sys/power VFS nodes.
 *
 * The idle mode the scheduler enters, which modes the port offers, and which
 * wake sources are armed.  policy is the one writable node.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_power.h"
#include "tiku.h"
#include <hal/tiku_wake_hal.h>
#include <kernel/cpu/tiku_power_policy.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* /sys/power/mode, policy, available                                        */
/*---------------------------------------------------------------------------*/

/** Longest policy name, "deepest", plus its terminator. */
#define POLICY_TOKEN_MAX  8u

/** The acknowledgement a write adds for a mode that stops console input. */
static const char ack_word[] = "allow-console-loss";

/**
 * @brief Read handler for /sys/power/mode.
 *
 * Renders the platform's name for the mode the scheduler enters -- "LPM0",
 * "LPM3" or "LPM4" on MSP430, "WFI" on the Cortex-M parts, "off", or
 * "custom" for a hook installed outside the policy service.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
power_mode_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_power_policy_mode_name());
}

/**
 * @brief Read handler for /sys/power/policy: off, light, deep, deepest or
 *        custom.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
power_policy_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    tiku_power_policy_token(tiku_power_policy_get()));
}

/**
 * @brief Read handler for /sys/power/available: the modes the port offers.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
power_available_read(char *buf, size_t max)
{
    return snprintf(buf, max, "off%s%s%s\n",
        tiku_power_policy_supported(TIKU_CPU_IDLE_LIGHT) ? " light" : "",
        tiku_power_policy_supported(TIKU_CPU_IDLE_DEEP) ? " deep" : "",
        tiku_power_policy_supported(TIKU_CPU_IDLE_DEEPEST)
            ? " deepest" : "");
}

/** @brief Is @p c a space, tab, CR or LF? */
static int
is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * @brief Write handler for /sys/power/policy: "<mode> [allow-console-loss]".
 *
 * @param buf  The request; trailing blanks are ignored
 * @param len  Length of @p buf
 * @return TIKU_VFS_OK; EINVAL for a malformed request or a missing
 *         acknowledgement, ENOTSUP for a mode the port lacks, EBUSY when no
 *         armed source wakes the mode
 */
static int
power_policy_write(const char *buf, size_t len)
{
    char token[POLICY_TOKEN_MAX];
    size_t i = 0;
    uint8_t ack = 0;
    int mode;
    int rc;

    while (len > 0 && is_blank(buf[len - 1])) {
        len--;
    }
    while (i < len && !is_blank(buf[i])) {
        if (i == sizeof(token) - 1u || buf[i] == '\0') {
            return TIKU_VFS_EINVAL;
        }
        token[i] = buf[i];
        i++;
    }
    token[i] = '\0';
    mode = tiku_power_policy_parse(token);
    if (mode < 0) {
        return TIKU_VFS_EINVAL;
    }
    while (i < len && is_blank(buf[i])) {
        i++;
    }
    if (i < len) {
        if (len - i != sizeof(ack_word) - 1u ||
            memcmp(buf + i, ack_word, sizeof(ack_word) - 1u) != 0) {
            return TIKU_VFS_EINVAL;
        }
        ack = 1;
    }

    rc = tiku_power_policy_set((tiku_cpu_idle_mode_t)mode, ack);
    switch (rc) {
    case TIKU_POWER_OK:
        return TIKU_VFS_OK;
    case TIKU_POWER_UNSUPPORTED:
        return TIKU_VFS_ENOTSUP;
    case TIKU_POWER_NO_WAKE:
        return TIKU_VFS_EBUSY;
    default:
        return TIKU_VFS_EINVAL;
    }
}

/*---------------------------------------------------------------------------*/
/* /sys/power/wake                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/power/wake.
 *
 * Renders all four wake sources on one line as name:on or name:off, e.g.
 * "timer0:on uart:on wdt:off gpio:off\n" (system tick, UART RX, watchdog
 * interval, armed pin interrupt).  A source shown off cannot wake the MCU.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
power_wake_read(char *buf, size_t max)
{
    tiku_wake_sources_t w;

    tiku_wake_arch_query(&w);

    return snprintf(buf, max, "timer0:%s uart:%s wdt:%s gpio:%s\n",
                    (w.sources & TIKU_WAKE_SYSTICK) ? "on" : "off",
                    (w.sources & TIKU_WAKE_UART_RX) ? "on" : "off",
                    (w.sources & TIKU_WAKE_WDT) ? "on" : "off",
                    (w.sources & TIKU_WAKE_GPIO) ? "on" : "off");
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/*
 * /sys/power directory table, exported so tiku_vfs_tree_sys.c can attach it as
 * the "power" directory; the entry count travels as TIKU_VFS_TREE_POWER_NCHILD
 * (asserted below).  policy is writable with the SYS capability.
 */
static const tiku_vfs_desc_t desc_power = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);

const tiku_vfs_node_t tiku_vfs_tree_power_children[] = {
    { "mode", TIKU_VFS_FILE, power_mode_read, NULL, NULL, 0, &desc_power },
    { "wake", TIKU_VFS_FILE, power_wake_read, NULL, NULL, 0, &desc_power },
    { "policy", TIKU_VFS_FILE, power_policy_read, power_policy_write,
      NULL, 0, &desc_power, NULL, TIKU_VFS_CAP_SYS },
    { "available", TIKU_VFS_FILE, power_available_read, NULL, NULL, 0,
      &desc_power },
};

_Static_assert(sizeof(tiku_vfs_tree_power_children) /
               sizeof(tiku_vfs_tree_power_children[0])
               == TIKU_VFS_TREE_POWER_NCHILD,
               "TIKU_VFS_TREE_POWER_NCHILD out of sync");
