/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mem.c - "peek" and "poke" implementation
 *
 * Both handlers parse an address (decimal or 0x-prefixed hex),
 * then dereference it as a `volatile uint8_t *` for the actual
 * read/write.  No software MPU bypass is performed: reads honour
 * the active region permissions, and writes will silently drop
 * if the destination is configured as read-only (this matches
 * what the application would see if it did the same write
 * directly, so the behaviour at the prompt is faithful to runtime).
 *
 * Address-space note: pointers in the small memory model are
 * 16-bit, so addresses are accepted as uint32_t for parsing
 * convenience but rejected if they exceed 0xFFFF.  Far accesses
 * to HIFRAM (>= 0x10000) would need __data20 pointers and a
 * separate "peek_far" / "poke_far" pair; not included in this
 * minimal version because everyday register and FRAM debugging
 * fits in the low 64 KB.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_mem.h"
#include <kernel/shell/tiku_shell.h>

/** Cap one peek at 32 bytes so the printed line stays short. */
#define MEM_PEEK_MAX     32

/*---------------------------------------------------------------------------*/
/* SHARED HELPERS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Parse an unsigned 32-bit value (decimal or 0x-prefixed hex).
 *
 * @return 1 on success with @p out written; 0 on parse error,
 *         empty string, or value > 0xFFFFFFFF (cannot occur for
 *         a uint32_t accumulator, kept for symmetry with the
 *         narrower variants used elsewhere).
 */
static uint8_t
mem_parse_u32(const char *s, uint32_t *out)
{
    uint32_t val = 0;
    uint8_t  hex = 0;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        hex = 1;
        s += 2;
        if (*s == '\0') {
            return 0;
        }
    }
    while (*s != '\0') {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') {
            digit = (uint32_t)(*s - '0');
        } else if (hex && *s >= 'a' && *s <= 'f') {
            digit = (uint32_t)(*s - 'a' + 10);
        } else if (hex && *s >= 'A' && *s <= 'F') {
            digit = (uint32_t)(*s - 'A' + 10);
        } else {
            return 0;
        }
        val = val * (hex ? 16U : 10U) + digit;
        s++;
    }
    *out = val;
    return 1;
}

/**
 * @brief Common 16-bit address parse: rejects HIFRAM and above.
 *
 * @return 1 on success, 0 on parse error or out-of-range.
 */
static uint8_t
mem_parse_addr16(const char *s, uint16_t *out)
{
    uint32_t v;
    if (!mem_parse_u32(s, &v)) {
        return 0;
    }
    if (v > 0xFFFFu) {
        return 0;
    }
    *out = (uint16_t)v;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* peek                                                                      */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_peek(uint8_t argc, const char *argv[])
{
    uint16_t addr;
    uint32_t count = 1;
    uint16_t i;
    volatile const uint8_t *p;

    if (argc < 2 || argc > 3) {
        SHELL_PRINTF("Usage: peek <addr> [count]  (count 1..%u)\n",
                     (unsigned)MEM_PEEK_MAX);
        return;
    }
    if (!mem_parse_addr16(argv[1], &addr)) {
        SHELL_PRINTF("peek: bad address '%s' (0..0xFFFF)\n", argv[1]);
        return;
    }
    if (argc == 3) {
        if (!mem_parse_u32(argv[2], &count) ||
            count == 0 || count > MEM_PEEK_MAX) {
            SHELL_PRINTF("peek: bad count '%s' (1..%u)\n",
                         argv[2], (unsigned)MEM_PEEK_MAX);
            return;
        }
    }
    /* Defend against wraparound at the top of the address space:
     * a peek that would cross 0x10000 is truncated rather than
     * silently rolling over into low memory. */
    if ((uint32_t)addr + count > 0x10000UL) {
        count = 0x10000UL - (uint32_t)addr;
    }

    p = (volatile const uint8_t *)addr;
    SHELL_PRINTF("%04x:", (unsigned)addr);
    for (i = 0; i < count; i++) {
        SHELL_PRINTF(" %02x", (unsigned)p[i]);
    }
    SHELL_PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
/* poke                                                                      */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_poke(uint8_t argc, const char *argv[])
{
    uint16_t addr;
    uint32_t val;
    volatile uint8_t *p;
    uint8_t  before;
    uint8_t  after;

    if (argc != 3) {
        SHELL_PRINTF("Usage: poke <addr> <byte>\n");
        return;
    }
    if (!mem_parse_addr16(argv[1], &addr)) {
        SHELL_PRINTF("poke: bad address '%s' (0..0xFFFF)\n", argv[1]);
        return;
    }
    if (!mem_parse_u32(argv[2], &val) || val > 0xFFu) {
        SHELL_PRINTF("poke: bad byte '%s' (0..0xFF)\n", argv[2]);
        return;
    }

    /* Read-back is informational: it lets the caller see whether
     * the write took effect (handy when poking FRAM through the
     * MPU's read-only mask) without paying for a separate peek. */
    p      = (volatile uint8_t *)addr;
    before = *p;
    *p     = (uint8_t)val;
    after  = *p;

    SHELL_PRINTF("%04x: %02x -> %02x\n",
                 (unsigned)addr, (unsigned)before, (unsigned)after);
    if (after != (uint8_t)val) {
        SHELL_PRINTF("poke: write rejected "
                     "(MPU/peripheral?), value remained %02x\n",
                     (unsigned)after);
    }
}
