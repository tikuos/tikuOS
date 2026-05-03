/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_program.inl - line table + accessors.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * `prog` is a flat array of basic_line_t with `number == 0`
 * marking empty slots.  prog_store inserts / replaces / deletes
 * lines; prog_next_index walks forward in numeric order;
 * prog_find_exact looks up by line number; prog_list streams the
 * program through the shell I/O (used by the LIST command).  All
 * callers go through these helpers, so the array layout is
 * encapsulated.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Mark every line slot empty. */
static void
prog_clear(void)
{
    uint8_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) prog[i].number = 0;
}

static int
prog_store(uint16_t lineno, const char *body)
{
    uint8_t i;
    const char *t = body;
    skip_ws(&t);
    /* Empty body -> delete the line if present. */
    if (*t == '\0') {
        for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
            if (prog[i].number == lineno) prog[i].number = 0;
        }
        return 0;
    }
    /* Replace existing line. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) {
            strncpy(prog[i].text, t, TIKU_BASIC_LINE_MAX - 1);
            prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
            return 0;
        }
    }
    /* Find empty slot. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0) {
            prog[i].number = lineno;
            strncpy(prog[i].text, t, TIKU_BASIC_LINE_MAX - 1);
            prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

/* Index of the lowest-numbered line whose number >= @p lineno;
 * returns -1 if no such line exists. */
static int
prog_next_index(uint16_t lineno)
{
    int      best     = -1;
    uint16_t best_num = 0xFFFF;
    uint8_t  i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0)        continue;
        if (prog[i].number < lineno)    continue;
        if (prog[i].number < best_num) {
            best_num = prog[i].number;
            best = (int)i;
        }
    }
    return best;
}

static int
prog_find_exact(uint16_t lineno)
{
    uint8_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) return (int)i;
    }
    return -1;
}

static void
prog_list(void)
{
    uint16_t cur = 0;
    while (1) {
        int idx = prog_next_index(cur);
        if (idx < 0) break;
        SHELL_PRINTF("%u %s\n", (unsigned)prog[idx].number, prog[idx].text);
        if (prog[idx].number == 0xFFFFu) break;
        cur = (uint16_t)(prog[idx].number + 1);
    }
}
