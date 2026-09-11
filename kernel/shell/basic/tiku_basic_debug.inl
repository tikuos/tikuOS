/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_debug.inl - the line debugger of the cooperative shell mode.
 *
 * Commands are explicit DEBUG requests, never BASIC expressions: the
 * debugger reads interpreter state and never writes it.  A run is paused
 * between lines, never inside one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#if TIKU_BASIC_DEBUG_ENABLE
#define BASIC_DEBUG_BREAKS 16
#define BASIC_DEBUG_WATCHES 8
static uint8_t basic_debug_on, basic_debug_paused, basic_debug_skip;
static uint8_t basic_debug_started;
static uint8_t basic_debug_overflow;
static int8_t basic_debug_steps;
static uint16_t basic_debug_breaks[BASIC_DEBUG_BREAKS];
static char basic_debug_watches[BASIC_DEBUG_WATCHES]
                              [TIKU_BASIC_NAMEDVAR_LEN + 1];

/** @brief Whether a run is parked at a pause, so the tick lets it be. */
static int
basic_debug_parked(void)
{
    return basic_debug_paused;
}

static void
basic_debug_reset(void)
{
    basic_debug_on = basic_debug_paused = basic_debug_skip = 0;
    basic_debug_started = 0;
    basic_debug_overflow = 0;
    basic_debug_steps = -1;
    memset(basic_debug_breaks, 0, sizeof basic_debug_breaks);
    memset(basic_debug_watches, 0, sizeof basic_debug_watches);
}

/** Resolve existing scalar slots only: inspecting allocates nothing. */
static void
basic_debug_watch(const char *name)
{
    char key[TIKU_BASIC_NAMEDVAR_LEN + 1];
    size_t n = strlen(name);
    int idx = -1, i, string = n && name[n - 1] == '$';

    memcpy(key, name, n + 1);
    if (string) { key[--n] = '\0'; }
    if (n == 1 && key[0] >= 'A' && key[0] <= 'Z') { idx = key[0] - 'A'; }
    else for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        const char *candidate = basic_namedvar_names[i];
#if TIKU_BASIC_STRVARS_ENABLE
        if (string) { candidate = basic_namedstrvar_names[i]; }
#endif
        if (strcmp(candidate, key) == 0) { idx = i + 26; break; }
    }
    if (idx < 0) { SHELL_PRINTF("[TDBG MISSING %s]\n", name); return; }
    if (!string) {
        SHELL_PRINTF("[TDBG VAR %s %ld]\n", name, basic_vars[idx]);
        return;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    {
        const unsigned char *s = (const unsigned char *)basic_strvars[idx];
        unsigned k;
        SHELL_PRINTF("[TDBG STR %s ", name);
        for (k = 0; s && s[k] && k < 32; k++) {
            SHELL_PRINTF("%02X", (unsigned)s[k]);
        }
        SHELL_PRINTF("%s]\n", s && s[k] ? "+" : "");
    }
#else
    SHELL_PRINTF("[TDBG MISSING %s]\n", name);
#endif
}

static void
basic_debug_snapshot(unsigned line)
{
    int i;
    SHELL_PRINTF("\n[TDBG PAUSED %u]\n", line);
    for (i = 0; i < BASIC_DEBUG_WATCHES; i++) {
        if (basic_debug_watches[i][0]) {
            basic_debug_watch(basic_debug_watches[i]);
        }
    }
    SHELL_PRINTF("[TDBG READY]\n");
}

/** Stop before a line, not mid-statement, and yield normally while paused. */
static int
basic_debug_before_step(void)
{
    int i, hit = 0;
    if (!basic_debug_on) { return 0; }
    if (basic_debug_paused) { return 1; }
    if (basic_wait_pending) { return 0; }
    if (!basic_debug_skip) {
        for (i = 0; i < BASIC_DEBUG_BREAKS; i++) {
            if (basic_debug_breaks[i] &&
                basic_debug_breaks[i] == basic_pc) { hit = 1; }
        }
    }
    basic_debug_skip = 0;
    if (basic_debug_steps == 0 || hit) {
        basic_debug_paused = 1;
        basic_debug_snapshot(basic_pc);
        return 1;
    }
    return 0;
}

static void
basic_debug_begin(void)
{
    basic_debug_started = basic_debug_on;
    basic_debug_paused = basic_debug_skip = 0;
    basic_debug_steps = 0;
}

static void
basic_debug_end(void)
{
    if (basic_debug_started) {
        basic_debug_snapshot(0);
        SHELL_PRINTF("[TDBG END]\n");
    }
    basic_debug_paused = 0;
    basic_debug_started = 0;
    basic_debug_steps = 0;
}

/** A small, strict protocol: an invalid request alters no state. */
static void
basic_debug_command(const char *p)
{
    char op[12], arg[TIKU_BASIC_NAMEDVAR_LEN + 16];
    size_t n = 0;
    int i;

    skip_ws(&p);
    while (*p && *p != ' ' && *p != '\t' && n + 1 < sizeof op) {
        op[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
        p++;
    }
    op[n] = '\0';
    skip_ws(&p);
    n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) { n--; }
    if (n >= sizeof arg) { goto bad; }
    for (i = 0; i < (int)n; i++) {
        arg[i] = p[i] >= 'a' && p[i] <= 'z' ? (char)(p[i] - 32) : p[i];
    }
    arg[n] = '\0';
    if (!basic_mode_on) { goto bad; }
    if (!strcmp(op, "ON") && !n && !basic_running) {
        basic_debug_reset(); basic_debug_on = 1;
        SHELL_PRINTF("[TDBG VERSION 1]\n"); return;
    }
    if (!strcmp(op, "OFF") && !n && !basic_running) {
        basic_debug_reset(); return;
    }
    if (!basic_debug_on || (basic_running && !basic_debug_paused)) { goto bad; }
    if ((!strcmp(op, "STEP") || !strcmp(op, "CONT")) && !n && basic_running) {
        basic_debug_steps = !strcmp(op, "STEP") ? 1 : -1;
        basic_debug_skip = 1; basic_debug_paused = 0;
        SHELL_PRINTF("[TDBG RUNNING]\n"); return;
    }
    if (!strcmp(op, "SNAP") && !n && basic_debug_paused) {
        basic_debug_snapshot(basic_pc); return;
    }
    if (!strcmp(op, "BREAK")) {
        unsigned long line = 0;
        if (!strcmp(arg, "CLEAR")) {
            memset(basic_debug_breaks, 0, sizeof basic_debug_breaks); return;
        }
        if (!n) { goto bad; }
        for (i = 0; i < (int)n; i++) {
            if (arg[i] < '0' || arg[i] > '9' || line > 6553) { goto bad; }
            line = line * 10 + (unsigned)(arg[i] - '0');
        }
        if (!line || line >= 65534) { goto bad; }
        for (i = 0; i < BASIC_DEBUG_BREAKS; i++) {
            if (basic_debug_breaks[i] == line) { return; }
            if (!basic_debug_breaks[i]) {
                basic_debug_breaks[i] = (uint16_t)line;
                return;
            }
        }
    }
    if (!strcmp(op, "WATCH")) {
        if (!strcmp(arg, "CLEAR")) {
            memset(basic_debug_watches, 0, sizeof basic_debug_watches); return;
        }
        if (!n || n > TIKU_BASIC_NAMEDVAR_LEN ||
            arg[0] < 'A' || arg[0] > 'Z') { goto bad; }
        for (i = 1; i < (int)n; i++) {
            if (!((arg[i] >= 'A' && arg[i] <= 'Z') ||
                  (arg[i] >= '0' && arg[i] <= '9') || arg[i] == '_' ||
                  (arg[i] == '$' && i == (int)n - 1))) { goto bad; }
        }
        if (n >= TIKU_BASIC_NAMEDVAR_LEN && arg[n - 1] != '$') { goto bad; }
        for (i = 0; i < BASIC_DEBUG_WATCHES; i++) {
            if (!strcmp(basic_debug_watches[i], arg)) { return; }
            if (!basic_debug_watches[i][0]) {
                memcpy(basic_debug_watches[i], arg, n + 1); return;
            }
        }
    }
bad:
    SHELL_PRINTF("[TDBG ERROR invalid-command-or-limit]\n");
}
#else
static void basic_debug_reset(void) { }
static void basic_debug_begin(void) { }
static void basic_debug_end(void) { }
static int basic_debug_before_step(void) { return 0; }
static void basic_debug_command(const char *p)
{ (void)p; SHELL_PRINTF("[TDBG ERROR debugger-disabled]\n"); }
static int basic_debug_parked(void) { return 0; }
#endif
