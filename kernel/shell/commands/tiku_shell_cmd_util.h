/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_util.h - argument helpers shared by command modules.
 *
 * Three one-liners that were file-static copies until the power command was split
 * per driver.  They are static inline, so each module gets its own copy with no
 * one-definition-rule games and the compiler drops what a module does not use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_UTIL_H_
#define TIKU_SHELL_CMD_UTIL_H_

#include <stdint.h>
#include <string.h>

/*
 * These are EXACT copies of what tiku_shell_cmd_power.c had as file-statics.
 * A first draft of this header "improved" them -- hex support in parse_u32,
 * enable/disable in parse_on_off.  Both were reverted: S4 is a mechanical
 * split gated on output parity, and a split that quietly changes what
 * `power psram poke 0x100` parses is not a split.  Extend them later, on
 * purpose, with their own gate.
 */

/** @brief Exact string compare.  1 when equal, 0 otherwise. */
static inline int tiku_cmd_streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/** @brief Parse an unsigned DECIMAL; 0 on anything unparseable. */
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
