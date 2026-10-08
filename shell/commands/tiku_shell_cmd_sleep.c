/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sleep.c - "sleep" command implementation.
 *
 * Sets the idle mode the scheduler enters when no events are pending by
 * writing /sys/power/policy; tiku_vfs_write() checks the caller's capability
 * and notifies watchers.  The CPU HAL maps each mode to the platform's entry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_sleep.h"
#include <shell/tiku_shell.h>
#include <kernel/cpu/tiku_power_policy.h>
#include <kernel/vfs/tiku_vfs.h>
#include <hal/tiku_cpu.h>
#include <stddef.h>

/** Room for the longest request, "deepest allow-console-loss". */
#define SLEEP_REQ_MAX  32u

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings for equality.
 */
static uint8_t
streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/**
 * @brief Map a mode name or an MSP430 alias to a HAL idle mode.
 *
 * Takes the policy names (off, light, deep, deepest) and the aliases lpm0,
 * lpm3 and lpm4.
 *
 * @return 0 on match, -1 on unknown token.
 */
static int
parse_mode(const char *tok, tiku_cpu_idle_mode_t *out)
{
    int mode = tiku_power_policy_parse(tok);

    if (mode >= 0) {
        *out = (tiku_cpu_idle_mode_t)mode;
        return 0;
    }
    if (streq(tok, "lpm0")) { *out = TIKU_CPU_IDLE_LIGHT;   return 0; }
    if (streq(tok, "lpm3")) { *out = TIKU_CPU_IDLE_DEEP;    return 0; }
    if (streq(tok, "lpm4")) { *out = TIKU_CPU_IDLE_DEEPEST; return 0; }
    return -1;
}

/**
 * @brief Append @p s to @p buf at @p at, keeping it NUL-terminated.
 *
 * @return The new length
 */
static size_t
append(char *buf, size_t at, const char *s)
{
    while (*s != '\0' && at < SLEEP_REQ_MAX - 1u) {
        buf[at++] = *s++;
    }
    buf[at] = '\0';
    return at;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_sleep(uint8_t argc, const char *argv[])
{
    tiku_cpu_idle_mode_t mode;
    const char *token;
    char req[SLEEP_REQ_MAX];
    uint8_t ack;
    size_t len;
    int rc;

    if (argc < 2) {
        rc = tiku_power_policy_get();
        SHELL_PRINTF("Idle mode: %s\n", rc == TIKU_POWER_CUSTOM
                     ? "custom"
                     : tiku_cpu_idle_mode_desc((tiku_cpu_idle_mode_t)rc));
        return;
    }

    ack = (uint8_t)(argc == 3 && streq(argv[2], "allow-console-loss"));
    if (argc > 3 || (argc == 3 && !ack) ||
        parse_mode(argv[1], &mode) != 0) {
        SHELL_PRINTF("Usage: sleep <off|light|deep|deepest> "
                     "[allow-console-loss]\n");
        return;
    }

    token = tiku_power_policy_token((int)mode);
    if (!ack && tiku_power_policy_supported(mode) &&
        tiku_power_policy_loses_console(mode)) {
        SHELL_PRINTF("Error: %s stops console input; "
                     "add allow-console-loss\n", token);
        return;
    }

    /* tiku_vfs_write() refuses a caller without TIKU_VFS_CAP_SYS and notifies
     * the node's watchers on success. */
    len = append(req, 0, token);
    if (ack) {
        len = append(req, len, " allow-console-loss");
    }
    rc = tiku_vfs_write("/sys/power/policy", req, len);
    switch (rc) {
    case TIKU_VFS_OK:
        break;
    case TIKU_VFS_EPERM:
        SHELL_PRINTF("Error: this link may not set the idle mode\n");
        return;
    case TIKU_VFS_ENOTSUP:
        SHELL_PRINTF("Error: this port has no separate %s mode\n", token);
        return;
    case TIKU_VFS_EBUSY:
        SHELL_PRINTF("Error: no armed wake source ends %s\n", token);
        return;
    default:
        SHELL_PRINTF("Error: idle mode not set (%d)\n", rc);
        return;
    }

    if (mode == TIKU_CPU_IDLE_OFF) {
        SHELL_PRINTF("LPM disabled\n");
    } else {
        SHELL_PRINTF("Idle: %s\n", tiku_cpu_idle_mode_name(mode));
    }
}

const char *
tiku_shell_sleep_mode_str(void)
{
    return tiku_power_policy_mode_name();
}
