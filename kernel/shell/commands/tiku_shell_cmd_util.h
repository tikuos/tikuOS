/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_util.h - argument helpers shared by command modules.
 *
 * Three small helpers the command modules share.  They are static inline, so
 * each module gets its own copy with no one-definition-rule conflict and the
 * compiler drops what a module does not use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_UTIL_H_
#define TIKU_SHELL_CMD_UTIL_H_

#include <stdint.h>
#include <string.h>

/** @brief Exact string compare.  1 when equal, 0 otherwise. */
static inline int tiku_cmd_streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/** @brief Parse leading decimal digits; 0 when there are none. */
static inline uint32_t tiku_cmd_parse_u32(const char *tok)
{
    uint32_t v = 0u;

    while (*tok >= '0' && *tok <= '9') {
        v = v * 10u + (uint32_t)(*tok++ - '0');
    }
    return v;
}

/** @brief Parse "on"/"1" and "off"/"0"; -1 if neither. */
static inline int tiku_cmd_parse_on_off(const char *tok)
{
    if (tiku_cmd_streq(tok, "on") || tiku_cmd_streq(tok, "1")) {
        return 1;
    }
    if (tiku_cmd_streq(tok, "off") || tiku_cmd_streq(tok, "0")) {
        return 0;
    }
    return -1;
}

#endif /* TIKU_SHELL_CMD_UTIL_H_ */
