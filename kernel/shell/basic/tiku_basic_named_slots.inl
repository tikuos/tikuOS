/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_named_slots.inl - multi-slot named SAVE / LOAD / DIR.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * The default unnamed SAVE / LOAD use the BASIC_PERSIST_KEY slot in
 * the persist store (see tiku_basic_persist.inl).  Named slots live
 * in a fixed-size FRAM-backed array indexed by an 8-char name, with
 * DIR listing what's currently saved.  Each slot is
 * TIKU_BASIC_NAMED_SLOT_BYTES wide; the count is bounded by
 * TIKU_BASIC_NAMED_SLOTS.  All four functions compile to nothing
 * when TIKU_BASIC_NAMED_SLOTS is 0.
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

#if TIKU_BASIC_NAMED_SLOTS > 0
typedef struct {
    char     name[8];                                     /* "" = empty */
    uint16_t length;
    uint8_t  pad[2];                                      /* 4-byte align */
    char     data[TIKU_BASIC_NAMED_SLOT_BYTES];
} basic_named_slot_t;

static BASIC_NVM_PERSISTENT
basic_named_slot_t basic_named_slots[TIKU_BASIC_NAMED_SLOTS];

static int
basic_slot_find_by_name(const char *name)
{
    int i;
    for (i = 0; i < TIKU_BASIC_NAMED_SLOTS; i++) {
        if (basic_named_slots[i].name[0] == '\0') continue;
        if (strncmp(basic_named_slots[i].name, name,
                    sizeof(basic_named_slots[i].name)) == 0) return i;
    }
    return -1;
}

static int
basic_slot_alloc(const char *name)
{
    int i = basic_slot_find_by_name(name);
    if (i >= 0) return i;
    for (i = 0; i < TIKU_BASIC_NAMED_SLOTS; i++) {
        if (basic_named_slots[i].name[0] == '\0') return i;
    }
    return -1;
}

static int
basic_save_to_named(const char *name)
{
    static char tmp[TIKU_BASIC_NAMED_SLOT_BYTES];
    size_t pos = 0;
    uint16_t cur = 0;
    int slot;
    uint16_t mpu;

    while (1) {
        int idx = prog_next_index(cur);
        int n;
        if (idx < 0) break;
        n = snprintf(tmp + pos, sizeof(tmp) - pos, "%u %s\n",
                     (unsigned)prog[idx].number, prog[idx].text);
        if (n < 0 || (size_t)n >= sizeof(tmp) - pos) {
            SHELL_PRINTF(SH_RED "? slot too small for program\n" SH_RST);
            return -1;
        }
        pos += (size_t)n;
        if (prog[idx].number == 0xFFFFu) break;
        cur = (uint16_t)(prog[idx].number + 1);
    }
    slot = basic_slot_alloc(name);
    if (slot < 0) {
        SHELL_PRINTF(SH_RED "? all slots in use\n" SH_RST);
        return -1;
    }
    mpu = tiku_mpu_unlock_nvm();
    memcpy(basic_named_slots[slot].data, tmp, pos);
    basic_named_slots[slot].length = (uint16_t)pos;
    strncpy(basic_named_slots[slot].name, name,
            sizeof(basic_named_slots[slot].name));
    basic_named_slots[slot].name[sizeof(basic_named_slots[slot].name) - 1] = '\0';
    tiku_mpu_lock_nvm(mpu);
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST " to '"
                 SH_BOLD "%s" SH_RST "'\n", (unsigned)pos, name);
    return 0;
}

static int
basic_load_from_named(const char *name)
{
    int slot = basic_slot_find_by_name(name);
    char tmp[TIKU_BASIC_NAMED_SLOT_BYTES + 1];
    size_t n;
    char *line, *p;
    if (slot < 0) {
        SHELL_PRINTF(SH_RED "? '%s' not found\n" SH_RST, name);
        return -1;
    }
    n = basic_named_slots[slot].length;
    if (n > sizeof(tmp) - 1) n = sizeof(tmp) - 1;
    memcpy(tmp, basic_named_slots[slot].data, n);
    tmp[n] = '\0';
    prog_clear();
    line = tmp;
    for (p = tmp; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            if (line != p) process_line(line);
            line = p + 1;
        }
    }
    if (line && *line) process_line(line);
    SHELL_PRINTF(SH_GREEN "loaded %u bytes" SH_RST " from '"
                 SH_BOLD "%s" SH_RST "'\n",
                 (unsigned)basic_named_slots[slot].length, name);
    return 0;
}

static void
basic_list_named_slots(void)
{
    int i, any = 0;
    SHELL_PRINTF(SH_CYAN "  name      size" SH_RST "\n");
    for (i = 0; i < TIKU_BASIC_NAMED_SLOTS; i++) {
        if (basic_named_slots[i].name[0] == '\0') continue;
        SHELL_PRINTF("  " SH_BOLD "%-7s" SH_RST
                     " " SH_DIM "%5u B" SH_RST "\n",
                     basic_named_slots[i].name,
                     (unsigned)basic_named_slots[i].length);
        any = 1;
    }
    if (!any) SHELL_PRINTF("  " SH_DIM "(no saved programs)" SH_RST "\n");
}
#endif /* TIKU_BASIC_NAMED_SLOTS */

/* Scratch buffer for IF/THEN truncation -- when ELSE is present we
 * need to stop the THEN branch's exec_stmt from consuming the ELSE
 * keyword as if it were part of its own arguments. The simplest
 * portable approach is to copy the THEN branch into a buffer with
 * the ELSE position turned into a NUL. The buffer lives at file
 * scope rather than on the stack so deep IF nesting (which can
 * happen via GOSUB) won't blow the limited MSP430 stack. */
