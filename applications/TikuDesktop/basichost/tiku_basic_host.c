/*
 * TikuOS -- Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_host.c - the TikuOS BASIC interpreter, run on the host.
 *
 * The SAME interpreter a board runs -- tiku_basic.c compiled unchanged,
 * every peripheral gated off -- with the kernel it expects provided
 * from libc: the console is stdin/stdout, the clock is the wall clock,
 * and the VFS is a sandbox directory, so a program's VFSWRITE lands in
 * a real file somebody can look at.
 *
 * What this is FOR: the desktop's editor runs a program by running
 * this, which means what the editor shows is what a board would do --
 * not a re-implementation that agrees with the board on the days both
 * are right.
 *
 *   tiku-basic <program.bas> [sandbox-dir]
 *
 * The program is fed to the interpreter's own REPL a line at a time
 * (numbered lines store, exactly as typing them would), then RUN, then
 * the mode is pumped until the program ends.  Output is stdout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include <kernel/shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/vfs/tiku_vfs.h>
#include "../../../kernel/shell/basic/tiku_basic.h"

/*---------------------------------------------------------------------------*/
/* The kernel, as libc                                                       */
/*---------------------------------------------------------------------------*/

/* Nonzero once the prompt has come back after RUN: the program ended. */
static int host_prompted;

/* Nonzero once the program has said anything: the newlines the line
 * editor echoes BEFORE that are the feeding, not the output. */
static int host_spoke;

void
tiku_basic_host_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* The prompt and the banner are the REPL talking to a person at a
     * keyboard; a batch run has neither, and an editor's output pane
     * wants what the PROGRAM said.  The prompt still MEANS something --
     * it is how the harness hears that the program finished. */
    if (strcmp(buf, "ok> ") == 0) {
        host_prompted = 1;
        return;
    }
    if (strstr(buf, "ready.") != NULL) {
        return;
    }
    if (!host_spoke && strcmp(buf, "\n") == 0) {
        return;
    }
    host_spoke = 1;
    fputs(buf, stdout);
}

unsigned long
tiku_clock_seconds(void);

uint32_t
tiku_clock_time(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

unsigned long
tiku_clock_seconds(void)
{
    return (unsigned long)(tiku_clock_time() / 1000u);
}

/* The console: a batch run never has keys waiting, and INPUT answers
 * end-of-file rather than hanging a build machine. */
int tiku_shell_io_rx_ready(void) { return 0; }
int tiku_shell_io_getc(void)     { return -1; }
int tiku_shell_io_has_echo(void) { return 0; }
int tiku_shell_net_getc(void)    { return -1; }
void tiku_shell_net_pump(void)   {}
void tiku_shell_pump_net(void)   {}
void tiku_shell_pump(void)       {}
int tiku_shell_process(const char *line) { (void)line; return 0; }

/*---------------------------------------------------------------------------*/
/* The memory the arena expects, as malloc                                   */
/*---------------------------------------------------------------------------*/

#include <kernel/memory/tiku_mem.h>

tiku_mem_err_t
tiku_tier_init(void)
{
    return TIKU_MEM_OK;
}

tiku_mem_err_t
tiku_tier_arena_create(tiku_arena_t *arena, tiku_mem_tier_t tier,
                       tiku_mem_arch_size_t size, uint8_t id)
{
    uint8_t *buf = malloc(size);

    if (arena == NULL || buf == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    memset(arena, 0, sizeof *arena);
    arena->buf = buf;
    arena->capacity = size;
    arena->id = id;
    arena->active = 1;
    arena->tier = tier;
    return TIKU_MEM_OK;
}

void *
tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size)
{
    void *p;

    if (arena == NULL || !arena->active ||
        arena->offset + size > arena->capacity) {
        if (arena != NULL) {
            arena->fail++;
        }
        return NULL;
    }
    p = arena->buf + arena->offset;
    arena->offset += size;
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }
    arena->count++;
    return p;
}

tiku_mem_err_t
tiku_arena_reset(tiku_arena_t *arena)
{
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    arena->offset = 0;
    arena->count = 0;
    return TIKU_MEM_OK;
}

/* The persist store: volatile on the host, exactly as the config's own
 * comment promises -- a run is a run, and durability is the board's. */
uint16_t tiku_mpu_unlock_nvm(void) { return 0; }
void tiku_mpu_lock_nvm(uint16_t saved) { (void)saved; }

tiku_mem_err_t
tiku_persist_init(tiku_persist_store_t *store)
{
    (void)store;
    return TIKU_MEM_OK;
}

tiku_mem_err_t
tiku_persist_register(tiku_persist_store_t *store, const char *key,
                      uint8_t *fram_buf, tiku_mem_arch_size_t capacity)
{
    (void)store;
    (void)key;
    (void)fram_buf;
    (void)capacity;
    return TIKU_MEM_OK;
}

tiku_mem_err_t
tiku_persist_read(tiku_persist_store_t *store, const char *key,
                  uint8_t *buf, tiku_mem_arch_size_t buf_size,
                  tiku_mem_arch_size_t *out_len)
{
    (void)store;
    (void)key;
    (void)buf;
    (void)buf_size;
    if (out_len != NULL) {
        *out_len = 0;
    }
    return TIKU_MEM_ERR_INVALID;    /* nothing saved: a fresh boot */
}

tiku_mem_err_t
tiku_persist_write(tiku_persist_store_t *store, const char *key,
                   const uint8_t *data, tiku_mem_arch_size_t data_len)
{
    (void)store;
    (void)key;
    (void)data;
    (void)data_len;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* The VFS, as a sandbox directory                                           */
/*---------------------------------------------------------------------------*/

static char vfs_root[512] = ".";

/** @brief The sandbox file a VFS path names.  Absolute paths keep their
 *         shape under the root, so /led becomes <root>/led. */
static void
vfs_at(const char *path, char *out, size_t max)
{
    snprintf(out, max, "%s/%s", vfs_root,
             (path != NULL && path[0] == '/') ? path + 1 : path);
}

const char *
tiku_vfs_strerror(int status)
{
    return (status == 0) ? "ok" : "no such node";
}

const tiku_vfs_node_t *
tiku_vfs_resolve(const char *path)
{
    /* The host tree has no node table; a path that names a real file
     * resolves to a marker, which is all the interpreter asks of it. */
    static tiku_vfs_node_t here;
    char at[600];
    struct stat sb;

    vfs_at(path, at, sizeof at);
    return (stat(at, &sb) == 0) ? &here : NULL;
}

int
tiku_vfs_read(const char *path, char *buf, size_t max)
{
    char at[600];
    FILE *f;
    size_t n;

    vfs_at(path, at, sizeof at);
    f = fopen(at, "rb");
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, (max > 0u) ? max - 1u : 0u, f);
    (void)fclose(f);
    if (max > 0u) {
        buf[n] = '\0';
    }
    return (int)n;
}

int
tiku_vfs_write(const char *path, const char *data, size_t len)
{
    char at[600];
    FILE *f;

    vfs_at(path, at, sizeof at);
    f = fopen(at, "wb");
    if (f == NULL) {
        return -1;
    }
    if (len > 0u && fwrite(data, 1, len, f) != len) {
        (void)fclose(f);
        return -1;
    }
    return (fclose(f) == 0) ? 0 : -1;
}

int
tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx)
{
    (void)path;
    (void)callback;
    (void)ctx;
    return 0;
}

int8_t
tiku_vfs_watch(const char *path, struct tiku_process *p)
{
    (void)path;
    (void)p;
    return -1;                  /* a batch run outlives no watches */
}

struct tiku_tfs;
struct tiku_tfs *
tiku_vfs_tree_data_store(void)
{
    return NULL;                /* no durable store: SAVE is the file */
}

/*---------------------------------------------------------------------------*/
/* The run                                                                   */
/*---------------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
    FILE *f;
    char line[256];
    int guard;

    if (argc < 2) {
        fprintf(stderr, "usage: tiku-basic <program.bas> [sandbox-dir]\n");
        return 2;
    }
    if (argc > 2) {
        snprintf(vfs_root, sizeof vfs_root, "%s", argv[2]);
    }
    f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "tiku-basic: cannot read %s\n", argv[1]);
        return 2;
    }
    tiku_basic_mode_enter();
    /* Each line through the interpreter's own front door, exactly as
     * typing it would go -- numbered lines store, and nothing here
     * re-implements what a line means. */
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line;

        while (*p != '\0') {
            if (*p != '\r') {
                tiku_basic_mode_feed_char(*p);
            }
            p++;
        }
    }
    (void)fclose(f);
    host_prompted = 0;
    tiku_basic_mode_feed_char('R');
    tiku_basic_mode_feed_char('U');
    tiku_basic_mode_feed_char('N');
    tiku_basic_mode_feed_char('\n');
    /*
     * Pump until the program ends.  BOUNDED, because a program is a
     * loop somebody may have written wrong, and a build machine must
     * see it FAIL rather than sit in it: ~30 seconds of batches at the
     * mode's own pace is more BASIC than any test needs.
     */
    for (guard = 0; guard < 3000000 && tiku_basic_mode_active() &&
                    !host_prompted; guard++) {
        tiku_basic_mode_tick();
    }
    fflush(stdout);
    if (guard >= 3000000) {
        fprintf(stderr, "tiku-basic: still running after the guard -- "
                        "stopped rather than trusted\n");
        return 3;
    }
    return 0;
}
