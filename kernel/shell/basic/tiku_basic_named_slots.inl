/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_named_slots.inl - multi-slot named SAVE, LOAD and DIR.
 *
 * Two backends: /data files on region parts, visible to the shell, and a fixed
 * slot array on MSP430 (durable) and host.  The whole piece compiles to nothing
 * when named slots are disabled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NAMED_SLOTS > 0
#if BASIC_NVM_ON_REGION
/*
 * Named programs are /data files, "/data/<name>.bas": durable, visible to
 * `ls` and `cat`, and as many as the store has room for.  BASIC_NVM_PERSISTENT
 * is durable only on MSP430, so a static slot array would not survive a reset
 * here.  A named program is at most one serialization scratch (4 KB).
 */
#define BASIC_NAMED_SUFFIX    ".bas"
/* Keeps "<name>.bas" within the store's name field (TIKU_TFS_NAME_MAX). */
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

/**
 * @brief SAVE "name": write the program to /data/<name>.bas.
 * @return 0, or -1 after reporting why (bad name, too large, write failed)
 */
static int
basic_save_to_named(const char *name)
{
    char         path[48];
    /* The serialization scratch bounds a named program.  Named SAVE is one
     * interactive command, never concurrent with the unnamed SAVE/LOAD that
     * shares the buffer. */
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

/**
 * @brief LOAD "name": replace the program with /data/<name>.bas.
 * @return 0, or -1 after reporting why (bad name, not found, empty)
 */
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
    /* /data reports the file's full length, which can exceed what it copied:
     * clamp before walking, or an over-long file reads past the buffer. */
    got = ((size_t)rd < cap - 1u) ? (size_t)rd : cap - 1u;
    if (got == 0u) {
        basic_reportf(TIKU_BASIC_ERR_IO, "'%s' is empty", name);
        return -1;
    }

    prog_clear();
    basic_clear_vars();

    /* Dispatch through the dedicated line buffer, not the scratch being
     * walked: process_line() can reach commands that use the scratch. */
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

/** @brief DIR: list the saved programs in /data. */
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

#else  /* MSP430 / host: a fixed slot array, durable FRAM on MSP430 */

/** One named program slot. */
typedef struct {
    char     name[8];                                     /**< "" = empty  */
    uint16_t length;                                      /**< bytes used  */
    uint8_t  pad[2];                                      /**< 4-byte align */
    char     data[TIKU_BASIC_NAMED_SLOT_BYTES];           /**< program text */
} basic_named_slot_t;

static BASIC_NVM_PERSISTENT
basic_named_slot_t basic_named_slots[TIKU_BASIC_NAMED_SLOTS];

/** @brief Index of the slot holding @p name, or -1. */
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

/** @brief Slot for @p name: its own, else the first free one, else -1. */
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

/**
 * @brief SAVE "name": write the program into a named slot.
 * @return 0, or -1 when the program exceeds a slot or every slot is taken
 */
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

/** @brief LOAD "name": replace the program with a named slot's text. */
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

/** @brief DIR: list the named slots in use, with their sizes. */
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
