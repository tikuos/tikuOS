/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_named_slots.inl - multi-slot named SAVE, LOAD and DIR.
 *
 * Two backends: ordinary /data files on the region-backed parts, visible to the
 * shell, and a fixed array of durable slots on MSP430 and host.  The whole piece
 * compiles to nothing when named slots are disabled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NAMED_SLOTS > 0
#if BASIC_NVM_ON_REGION
/*
 * NAMED PROGRAMS ARE ORDINARY /data FILES: "/data/<name>.bas".
 *
 * A fixed slot array carrying BASIC_NVM_PERSISTENT cannot work here: that grade
 * is TIKU_DURABLE only on MSP430 -- .ssram on Ambiq (zeroed at boot, like .bss)
 * and EMPTY everywhere else -- so on Nordic, RP2350 and Ambiq a `SAVE "name"`
 * would be silently lost across a reset, with no region-tail fallback.  Such an
 * array also costs 6,180 B of always-resident RAM and caps a named program at
 * TIKU_BASIC_NAMED_SLOT_BYTES (2,048), which a BIG-tier program exceeds after
 * about thirteen lines.
 *
 * Riding the file store settles all of it at once: durable on every platform for
 * free, 4 KB per program, and as many programs as /data has room for (222 on the
 * LM20, not 3).  They also stop being invisible -- `ls /data`,
 * `cat /data/foo.bas`, and `send` to copy one off the board.
 *
 * MSP430 keeps the slot array below: its /data files are 512 B, SMALLER than the
 * slot it has today, and its slots are genuinely durable already.
 */
#define BASIC_NAMED_SUFFIX    ".bas"
/* Keeps "<name>.bas" comfortably inside the file store's name field. */
#define BASIC_NAMED_NAME_MAX  16u

/**
 * @brief Build "/data/<name>.bas", reporting the reason on failure.
 * @return 0 on success, -1 if the name is empty, too long, or will not fit.
 */
static int
basic_named_path(char *out, size_t cap, const char *name)
{
    int n;

    if (name == NULL || name[0] == '\0') {
        basic_report(TIKU_BASIC_ERR_SYNTAX, "named save/load: empty name");
        return -1;
    }
    if (strlen(name) > BASIC_NAMED_NAME_MAX) {
        basic_report(TIKU_BASIC_ERR_SYNTAX, "named save/load: name too long");
        return -1;
    }
    n = snprintf(out, cap, "/data/%s%s", name, BASIC_NAMED_SUFFIX);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

static int
basic_save_to_named(const char *name)
{
    char         path[48];
    /* The 4 KB serialization scratch is exactly one /data file, and named SAVE
     * is a single interactive command -- never concurrent with the unnamed
     * SAVE/LOAD that shares it. */
    char *const  tmp = basic_persist_scratch;
    const size_t cap = sizeof basic_persist_scratch;
    size_t       pos = 0;
    uint16_t     cur = 0;
    int          full = 0;

    if (basic_named_path(path, sizeof path, name) != 0) {
        return -1;
    }
    for (;;) {
        int idx = prog_next_index(cur);
        int n;

        if (idx < 0) {
            break;
        }
        /* Number + detokenized body: the stored format stays plain text. */
        n = snprintf(tmp + pos, cap - pos, "%u ", (unsigned)prog[idx].number);
        if (n < 0 || (size_t)n >= cap - pos) {
            full = 1;
            break;
        }
        pos += (size_t)n;
        n = basic_detok(tmp + pos, cap - pos, prog[idx].text);
        if (n < 0 || (size_t)n + 2u > cap - pos) {
            full = 1;
            break;
        }
        pos += (size_t)n;
        tmp[pos++] = '\n';
        if (prog[idx].number == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(prog[idx].number + 1);
    }
    if (full) {
        basic_report(TIKU_BASIC_ERR_IO,
                     "named save: program too large for one file");
        return -1;
    }
    if (tiku_vfs_write(path, tmp, pos) < 0) {
        basic_reportf(TIKU_BASIC_ERR_IO, "named save: cannot write '%s'", path);
        return -1;
    }
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST " to '"
                 SH_BOLD "%s" SH_RST "'\n", (unsigned)pos, name);
    return 0;
}

static int
basic_load_from_named(const char *name)
{
    char         path[48];
    char *const  tmp = basic_persist_scratch;
    const size_t cap = sizeof basic_persist_scratch;
    int          rd;
    size_t       got, i, ls = 0;

    if (basic_named_path(path, sizeof path, name) != 0) {
        return -1;
    }
    rd = tiku_vfs_read(path, tmp, cap - 1u);
    if (rd < 0) {
        basic_reportf(TIKU_BASIC_ERR_SYNTAX, "'%s' not found", name);
        return -1;
    }
    /* /data reports the file's TRUE length, which can exceed what it copied --
     * clamp before walking, or an over-long file would read past the buffer. */
    got = ((size_t)rd < cap - 1u) ? (size_t)rd : cap - 1u;
    if (got == 0u) {
        basic_reportf(TIKU_BASIC_ERR_IO, "'%s' is empty", name);
        return -1;
    }

    prog_clear();
    basic_clear_vars();

    /* Dispatch through the dedicated line buffer, not the scratch being
     * walking: process_line() can reach commands that use the scratch. */
    for (i = 0; i <= got; i++) {
        char c = (i < got) ? tmp[i] : '\n';

        if (c != '\n' && c != '\r') {
            continue;
        }
        if (i > ls && (i - ls) < sizeof basic_load_line) {
            memcpy(basic_load_line, tmp + ls, i - ls);
            basic_load_line[i - ls] = '\0';
            process_line(basic_load_line);
        }
        ls = i + 1;
    }
    SHELL_PRINTF(SH_GREEN "loaded %u bytes" SH_RST " from '"
                 SH_BOLD "%s" SH_RST "'\n", (unsigned)got, name);
    return 0;
}

/**
 * @brief Per-entry callback for DIR: print /data children ending in ".bas".
 */
static void
basic_named_dir_cb(const tiku_vfs_node_t *node, void *vctx)
{
    const size_t sfx = sizeof(BASIC_NAMED_SUFFIX) - 1u;
    char         nm[BASIC_NAMED_NAME_MAX + 1u];
    size_t       n;

    if (node == NULL || node->name == NULL) {
        return;
    }
    n = strlen(node->name);
    if (n <= sfx || n - sfx > BASIC_NAMED_NAME_MAX ||
        strcmp(node->name + (n - sfx), BASIC_NAMED_SUFFIX) != 0) {
        return;
    }
    memcpy(nm, node->name, n - sfx);
    nm[n - sfx] = '\0';
    SHELL_PRINTF("  " SH_BOLD "%s" SH_RST "\n", nm);
    *(int *)vctx = 1;
}

static void
basic_list_named_slots(void)
{
    int any = 0;

    SHELL_PRINTF(SH_CYAN "  saved programs" SH_RST SH_DIM
                 "  (/data/<name>" BASIC_NAMED_SUFFIX ")" SH_RST "\n");
    (void)tiku_vfs_list("/data", basic_named_dir_cb, &any);
    if (!any) {
        SHELL_PRINTF("  " SH_DIM "(no saved programs)" SH_RST "\n");
    }
}

#else  /* MSP430 / host: the durable FRAM slot array */

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
        /* Number + detokenized body: slot format stays plain text. */
        n = snprintf(tmp + pos, sizeof(tmp) - pos, "%u ",
                     (unsigned)prog[idx].number);
        if (n < 0 || (size_t)n >= sizeof(tmp) - pos) {
            basic_report(TIKU_BASIC_ERR_IO, "slot too small for program");
            return -1;
        }
        pos += (size_t)n;
        n = basic_detok(tmp + pos, sizeof(tmp) - pos, prog[idx].text);
        if (n < 0 || (size_t)n + 2u > sizeof(tmp) - pos) {
            basic_report(TIKU_BASIC_ERR_IO, "slot too small for program");
            return -1;
        }
        pos += (size_t)n;
        tmp[pos++] = '\n';
        tmp[pos]   = '\0';
        if (prog[idx].number == 0xFFFFu) break;
        cur = (uint16_t)(prog[idx].number + 1);
    }
    slot = basic_slot_alloc(name);
    if (slot < 0) {
        basic_report(TIKU_BASIC_ERR_IO, "all slots in use");
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
        basic_reportf(TIKU_BASIC_ERR_SYNTAX, "'%s' not found", name);
        return -1;
    }
    n = basic_named_slots[slot].length;
    if (n > sizeof(tmp) - 1) n = sizeof(tmp) - 1;
    memcpy(tmp, basic_named_slots[slot].data, n);
    tmp[n] = '\0';
    prog_clear();
    basic_clear_vars();
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
#endif /* BASIC_NVM_ON_REGION */
#endif /* TIKU_BASIC_NAMED_SLOTS */

/* Scratch buffer for IF/THEN truncation -- when ELSE is present the
 * need to stop the THEN branch's exec_stmt from consuming the ELSE
 * keyword as if it were part of its own arguments. The simplest
 * portable approach is to copy the THEN branch into a buffer with
 * the ELSE position turned into a NUL. The buffer lives at file
 * scope rather than on the stack so deep IF nesting (which can
 * happen via GOSUB) won't blow the limited MSP430 stack. */
