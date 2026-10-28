/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp.c - the host table: the namespace as resources, the read, write,
 * ls, shell and basic tools, /data/prompt.* as prompts, path completion;
 * the process that turns watches into updates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"
#include "tiku_mcp_consent.h"
#include "tiku_mcp_job.h"

#include <stdio.h>
#include <string.h>

#include "tiku.h"
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tiku_vfs.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include <shell/tiku_shell_parser.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/* One page of a node's text, or of the manifest, on its way to a reply. */
static char scratch[TIKU_MCP_SCRATCH];

/* The server's instructions, filled at init with the board's name. */
static char instructions[1024];

/* serverInfo: /sys/device/name, read again before each request, and the
 * device model. */
static char board_name[40];
static char board_title[48];

/* Prompt files taken from one /data listing, read after it ends. */
static char prompt_names[TIKU_MCP_PROMPTS_MAX][TIKU_MCP_NAME_MAX];

/* /data, watched for the resource and prompt lists, and what each held. */
static const tiku_vfs_node_t *data_dir;
static uint32_t data_names_hash, prompts_hash;

/* Where the shell tool's captured output goes while a command runs. */
static tinymcp_sink_t *capture;

/* The client a tool runs for, while it runs; NULL is the board itself. */
static const char *actor;

/* A watch the library asked for: the stream and slot it reports under. */
typedef struct {
    tinymcp_t             *stream;
    const tiku_vfs_node_t *node;
    char                   path[TIKU_VFS_PATH_MAX];
    uint8_t                slot;
    uint8_t                used;
} watch_t;

static watch_t watches[TIKU_MCP_WATCH_MAX];

TIKU_PROCESS(tiku_mcp_process, "tinymcp");

#define URI_ROOT      "tiku://"            /* the path follows, slash first */
#define URI_ROOT_LEN  7u
#define URI_COMMANDS  "tiku://shell/commands"
#define DATA_CURSOR   0x40000000u          /* cursors from here index /data */
#define PROMPT_PREFIX "prompt."            /* /data/prompt.<name> is a prompt */
#define PROMPT_PREFIX_LEN 7u

/*---------------------------------------------------------------------------*/
/* TEXT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief The node's text from line @p first, at most @p bytes when set,
 *        into the sink, page by page; the first page's start into @p head
 *        when it is set.
 * @return 0, or the first negative TIKU_VFS_* status
 */
static int
node_text(const char *path, unsigned first, unsigned bytes,
          tinymcp_sink_t *text, char *head, size_t head_cap)
{
    size_t limit = bytes ? (size_t)bytes : (size_t)-1;
    size_t total = 0u;

    for (;;) {
        int n = tiku_vfs_read_lines(path, first, scratch, sizeof scratch);
        unsigned lines = 0u;
        size_t take;
        int i;

        if (n < 0) {
            return n;
        }
        if (n == 0) {
            return 0;
        }
        for (i = 0; i < n; i++) {
            if (scratch[i] == '\n') {
                lines++;
            }
        }
        if (head != (char *)0 && total == 0u) {
            size_t k = (size_t)n < head_cap ? (size_t)n : head_cap - 1u;

            memcpy(head, scratch, k);
            head[k] = 0;
        }
        take = (size_t)n;
        if (total + take > limit) {
            take = limit - total;
        }
        (void)tinymcp_sink_write(text, scratch, take);
        total += take;
        /* A page that does not end on a newline is the node's tail. */
        if (text->cut || total >= limit || lines == 0u ||
            scratch[n - 1] != '\n') {
            return 0;
        }
        first += lines;
    }
}

/** @brief One line per child of a listing: name, d or f, perms. */
static void
ls_entry(const tiku_vfs_node_t *node, void *ctx)
{
    tinymcp_sink_t *text = ctx;
    const char *perm = (node->read && node->write) ? "rw"
                     : node->read               ? "r-"
                     : node->write              ? "-w"
                     :                            "--";

    (void)tinymcp_sink_puts(text, node->name);
    (void)tinymcp_sink_puts(text, node->type == TIKU_VFS_DIR ? " d " : " f ");
    (void)tinymcp_sink_puts(text, perm);
    tinymcp_sink_putc(text, '\n');
}

/** @brief The shell's command table as "name  help" lines. */
static void
commands_text(tinymcp_sink_t *text)
{
    const tiku_shell_cmd_t *c;

    for (c = tiku_shell_get_commands(); c->name != (const char *)0; c++) {
        if (c->handler == (tiku_shell_handler_t)0) {
            continue;
        }
        (void)tinymcp_sink_puts(text, c->name);
        (void)tinymcp_sink_puts(text, "  ");
        (void)tinymcp_sink_puts(text, c->help);
        tinymcp_sink_putc(text, '\n');
    }
}

/*---------------------------------------------------------------------------*/
/* RESOURCES                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Add the node at @p path, named by its path, to the listing. */
static int
add_node(tinymcp_list_t *list, const char *path, const char *desc)
{
    char uri[TIKU_VFS_PATH_MAX + URI_ROOT_LEN];

    if (strlen(path) >= TIKU_VFS_PATH_MAX) {
        return 0;
    }
    memcpy(uri, URI_ROOT, URI_ROOT_LEN);
    (void)strcpy(uri + URI_ROOT_LEN, path);
    return tinymcp_list_add(list, uri, path, desc);
}

/**
 * @brief Add one manifest line's node when it is a readable file.
 *
 * A line is "path type perms meta cap id", tab-separated; the meta column,
 * the packed descriptor with its read policy, is the resource's description.
 * @return 0, or TINYMCP_EFULL
 */
static int
add_manifest_line(tinymcp_list_t *list, char *line)
{
    char *type = strchr(line, '\t');
    char *perms, *meta, *end;

    if (type == (char *)0) {
        return 0;
    }
    *type++ = 0;
    perms = strchr(type, '\t');
    if (perms == (char *)0) {
        return 0;
    }
    *perms++ = 0;
    meta = strchr(perms, '\t');
    if (meta == (char *)0) {
        return 0;
    }
    *meta++ = 0;
    end = strchr(meta, '\t');
    if (end != (char *)0) {
        *end = 0;
    }
    if (type[0] != 'f' || perms[0] != 'r' || strstr(meta, ";secret")) {
        return 0;
    }
    return add_node(list, line, strcmp(meta, "-") ? meta : (const char *)0);
}

typedef struct {
    tinymcp_list_t *list;
    unsigned        index;   /* /data files seen */
    unsigned        want;    /* the first index to add */
    unsigned        next;    /* the index that did not fit */
    uint8_t         full;
} data_list_t;

/** @brief A /data file as a resource; its static children are in the manifest. */
static void
data_entry(const tiku_vfs_node_t *node, void *ctx)
{
    data_list_t *d = ctx;
    char path[TIKU_VFS_PATH_MAX];

    if (d->full || node->type != TIKU_VFS_FILE) {
        return;
    }
    (void)snprintf(path, sizeof path, "/data/%s", node->name);
    if (tiku_vfs_resolve(path) != (const tiku_vfs_node_t *)0) {
        return;
    }
    if (d->index++ < d->want) {
        return;
    }
    if (add_node(d->list, path, (const char *)0) != 0) {
        d->full = 1u;
        d->next = d->index - 1u;
    }
}

/**
 * @brief The resources from @p cursor on: the command table, every
 *        readable file of the manifest, then the /data files.
 *
 * Cursors below DATA_CURSOR are manifest lines (the header is line 0);
 * from DATA_CURSOR on they index the /data files.
 */
static int
list_resources(void *ctx, unsigned cursor, tinymcp_list_t *list)
{
    data_list_t d;
    unsigned line;

    (void)ctx;
    if (cursor == 0u) {
        if (tinymcp_list_add(list, URI_COMMANDS, "shell/commands",
                             "the shell's commands: name and help, one per "
                             "line") != 0) {
            return TINYMCP_EFULL;
        }
        cursor = 1u;
    }
    for (line = cursor; line < DATA_CURSOR;) {
        int n = tiku_vfs_manifest_from(scratch, sizeof scratch, line);
        char *p = scratch, *end;
        unsigned consumed = 0u;

        if (n <= 0) {
            break;
        }
        if (n >= (int)sizeof scratch) {
            n = (int)sizeof scratch - 1;
        }
        scratch[n] = 0;
        while ((end = strchr(p, '\n')) != (char *)0) {
            *end = 0;
            if (add_manifest_line(list, p) != 0) {
                return (int)(line + consumed);
            }
            consumed++;
            p = end + 1;
        }
        line += consumed ? consumed : 1u;   /* a line past the page: skipped */
    }
    d.list = list;
    d.index = 0u;
    d.want = cursor >= DATA_CURSOR ? cursor - DATA_CURSOR : 0u;
    d.next = 0u;
    d.full = 0u;
    (void)tiku_vfs_list("/data", data_entry, &d);
    return d.full ? (int)(DATA_CURSOR + d.next) : 0;
}

/** @brief The path a tiku:///path URI names, or NULL. */
static const char *
uri_path(const char *uri)
{
    if (strncmp(uri, URI_ROOT "/", URI_ROOT_LEN + 1u) != 0) {
        return (const char *)0;
    }
    return uri + URI_ROOT_LEN;
}

/** @brief A host status for the library from a VFS one. */
static int
from_status(int status)
{
    switch (status) {
    case TIKU_VFS_OK:     return TINYMCP_OK;
    case TIKU_VFS_ENOENT:
    case TIKU_VFS_EACCES: return TINYMCP_ENOENT;
    case TIKU_VFS_EPERM:  return TINYMCP_EEFFECT;
    default:              return TINYMCP_EIO;
    }
}

static int
read_resource(void *ctx, const char *uri, unsigned line, unsigned bytes,
              tinymcp_sink_t *text)
{
    const char *path;
    const tiku_vfs_node_t *n;

    (void)ctx;
    if (strcmp(uri, URI_COMMANDS) == 0) {
        commands_text(text);
        return TINYMCP_OK;
    }
    path = uri_path(uri);
    if (path == (const char *)0) {
        return TINYMCP_ENOENT;
    }
    n = tiku_vfs_resolve(path);
    if (n != (const tiku_vfs_node_t *)0) {
        if (n->type == TIKU_VFS_DIR) {
            return tiku_vfs_list(path, ls_entry, text) == 0 ? TINYMCP_OK
                                                             : TINYMCP_ENOENT;
        }
        if (n->desc != (const tiku_vfs_desc_t *)0) {
            if (n->desc->flags & TIKU_VFS_DF_SECRET) {
                return TINYMCP_ENOENT;
            }
            if (n->desc->flags & (TIKU_VFS_DF_READ_EFFECT |
                                  TIKU_VFS_DF_READ_CONSUMES)) {
                return TINYMCP_EEFFECT;
            }
        }
        if (n->read == (tiku_vfs_read_fn)0) {
            return TINYMCP_ENOENT;
        }
    }
    return from_status(node_text(path, line, bytes, text, (char *)0, 0u));
}

/*---------------------------------------------------------------------------*/
/* PROMPTS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The prompt's name when @p file is a prompt file, else NULL. */
static const char *
prompt_of(const char *file)
{
    if (strncmp(file, PROMPT_PREFIX, PROMPT_PREFIX_LEN) != 0 ||
        file[PROMPT_PREFIX_LEN] == 0) {
        return (const char *)0;
    }
    return file + PROMPT_PREFIX_LEN;
}

/**
 * @brief Read /data/prompt.<name> into the scratch, terminated.
 * @return Its length, at most the scratch less one, or -1
 */
static int
prompt_text(const char *name)
{
    char path[TIKU_VFS_PATH_MAX];
    uint32_t total;
    int n;

    if (strchr(name, '/') != (char *)0 ||
        snprintf(path, sizeof path, "/data/" PROMPT_PREFIX "%s", name) >=
            (int)sizeof path) {
        return -1;
    }
    n = tiku_vfs_read_at(path, 0u, scratch, sizeof scratch - 1u, &total);
    if (n < 0) {
        return -1;
    }
    scratch[n] = 0;
    return n;
}

typedef struct {
    unsigned skip;      /* prompt files passed over before the first kept */
    unsigned seen;      /* prompt files listed */
    unsigned kept;      /* names in prompt_names */
    uint32_t names;     /* FNV-1a of every /data name */
} prompt_scan_t;

/** @brief FNV-1a of @p n bytes into @p h. */
static uint32_t
fnv(uint32_t h, const char *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        h = (h ^ (uint8_t)p[i]) * 16777619u;
    }
    return h;
}

/** @brief One /data child: hash its name; keep it when it is a prompt. */
static void
prompt_scan_entry(const tiku_vfs_node_t *node, void *ctx)
{
    prompt_scan_t *ps = ctx;

    ps->names = fnv(ps->names, node->name, strlen(node->name) + 1u);
    if (node->type != TIKU_VFS_FILE || prompt_of(node->name) == (char *)0 ||
        strlen(node->name) >= TIKU_MCP_NAME_MAX) {
        return;
    }
    if (ps->seen++ < ps->skip || ps->kept == TIKU_MCP_PROMPTS_MAX) {
        return;
    }
    (void)strcpy(prompt_names[ps->kept++], node->name);
}

/** @brief The /data listing into @p ps, prompts from the @p skip-th on. */
static void
prompt_scan(prompt_scan_t *ps, unsigned skip)
{
    ps->skip = skip;
    ps->seen = 0u;
    ps->kept = 0u;
    ps->names = 2166136261u;
    (void)tiku_vfs_list("/data", prompt_scan_entry, ps);
}

/**
 * @brief The prompts from @p cursor on: each prompt file, its first line
 *        the description, its {{placeholders}} its required arguments.
 */
static int
list_prompts(void *ctx, unsigned cursor, tinymcp_list_t *list)
{
    static char ph[TINYMCP_ARGS_MAX][TINYMCP_PNAME_MAX];
    static tinymcp_prompt_arg_t args[TINYMCP_ARGS_MAX];
    char desc[96];
    prompt_scan_t ps;
    unsigned i, j, nargs;
    int n;

    (void)ctx;
    prompt_scan(&ps, cursor);
    for (i = 0; i < ps.kept; i++) {
        const char *name = prompt_of(prompt_names[i]);

        n = prompt_text(name);
        if (n < 0) {
            continue;
        }
        (void)snprintf(desc, sizeof desc, "%.*s",
                       (int)strcspn(scratch, "\r\n"), scratch);
        nargs = tinymcp_placeholders(scratch, (size_t)n, ph, TINYMCP_ARGS_MAX);
        for (j = 0; j < nargs; j++) {
            args[j].name = ph[j];
            args[j].description = (const char *)0;
            args[j].required = 1u;
        }
        if (tinymcp_prompt_add(list, name, desc[0] ? desc : (const char *)0,
                               args, nargs) != 0) {
            return (cursor + i) != 0u ? (int)(cursor + i) : TINYMCP_EFULL;
        }
    }
    /* Prompt files past the names kept start the next page. */
    return ps.seen > cursor + ps.kept ? (int)(cursor + ps.kept) : 0;
}

static int
get_prompt(void *ctx, const char *name, const tinymcp_args_t *args,
           tinymcp_sink_t *text)
{
    int n;

    (void)ctx;
    n = prompt_text(name);
    if (n < 0) {
        return TINYMCP_ENOENT;
    }
    return tinymcp_render(text, scratch, (size_t)n, args);
}

/*---------------------------------------------------------------------------*/
/* COMPLETION                                                                */
/*---------------------------------------------------------------------------*/

typedef struct {
    tinymcp_completion_t *c;
    const char           *dir;      /* the value up to its last slash */
    size_t                dir_len;
    const char           *prefix;   /* the last component typed so far */
    size_t                prefix_len;
} path_complete_t;

/** @brief A child whose name starts with the prefix, as a whole value. */
static void
path_entry(const tiku_vfs_node_t *node, void *ctx)
{
    path_complete_t *pc = ctx;
    char value[TIKU_VFS_PATH_MAX + 1];
    size_t n = strlen(node->name);
    int dir = node->type == TIKU_VFS_DIR && n > 0u && node->name[n - 1u] != '/';

    if (strncmp(node->name, pc->prefix, pc->prefix_len) != 0 ||
        pc->dir_len + n + 2u > sizeof value) {
        return;
    }
    memcpy(value, pc->dir, pc->dir_len);
    memcpy(value + pc->dir_len, node->name, n);
    value[pc->dir_len + n] = '/';
    value[pc->dir_len + n + (dir ? 1u : 0u)] = 0;
    (void)tinymcp_complete_add(pc->c, value);
}

/**
 * @brief The children of the directory @p value names, whose names start
 *        with its last component; a value without a leading slash is a
 *        template's path and its completions have none either.
 */
static void
complete_path(const char *value, tinymcp_completion_t *c)
{
    char dir[TIKU_VFS_PATH_MAX];
    const char *slash = strrchr(value, '/');
    path_complete_t pc;
    size_t dl;

    pc.c = c;
    pc.dir = value;
    pc.dir_len = slash ? (size_t)(slash - value) + 1u : 0u;
    pc.prefix = value + pc.dir_len;
    pc.prefix_len = strlen(pc.prefix);
    dl = pc.dir_len;
    if (value[0] == '/') {
        if (dl >= sizeof dir) {
            return;
        }
        memcpy(dir, value, dl);
    } else {
        if (dl + 1u >= sizeof dir) {
            return;
        }
        dir[0] = '/';
        memcpy(dir + 1, value, dl);
        dl++;
    }
    if (dl > 1u) {
        dl--;                          /* the directory without its slash */
    }
    dir[dl] = 0;
    (void)tiku_vfs_list(dir, path_entry, &pc);
}

static int
complete(void *ctx, int is_prompt, const char *ref, const char *arg,
         const char *value, const tinymcp_args_t *context,
         tinymcp_completion_t *c)
{
    (void)ctx;
    (void)context;
    if (is_prompt) {
        if (prompt_text(ref) < 0) {
            return TINYMCP_ENOENT;
        }
    } else if (strcmp(ref, URI_ROOT "/{+path}") != 0 &&
               strcmp(ref, URI_ROOT "/{+path}{?line,bytes}") != 0) {
        return TINYMCP_ENOENT;
    }
    if (strcmp(arg, "path") == 0) {
        complete_path(value, c);
    }
    return TINYMCP_OK;
}

/*---------------------------------------------------------------------------*/
/* TOOLS                                                                     */
/*---------------------------------------------------------------------------*/

static const tinymcp_tool_t tools[] = {
    { "read",
      "Read a node of the board's namespace as text, for example /sys/uptime; "
      "a long node from line, within bytes.  Reads a node even when reading "
      "it acts.  A typed node's value comes as structured content too: "
      "value, a number with its scale applied, and unit.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
      "\"line\":{\"type\":\"integer\"},\"bytes\":{\"type\":\"integer\"}},"
      "\"required\":[\"path\"]}", (const char *)0 },
    { "write",
      "Write a value to a node, as the shell's write does: write /dev/led0 1 "
      "lights the LED; a path under /data creates or replaces a file.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
      "\"value\":{\"type\":\"string\"}},\"required\":[\"path\",\"value\"]}",
      (const char *)0 },
    { "ls",
      "List a directory of the namespace: one line per child, name, d or f, "
      "and perms.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}", (const char *)0 },
    { "shell",
      "Run one command line as a person types it at tikuOS:/> and return "
      "what it printed.  The vocabulary is the resource tiku://shell/commands; "
      "commands that stay in a mode (watch, basic, ping, ntp, dns, mqtt, slip) "
      "are refused.",
      "{\"type\":\"object\",\"properties\":{\"line\":{\"type\":\"string\"}},"
      "\"required\":[\"line\"]}", (const char *)0 },
#if TIKU_SHELL_CMD_BASIC
    { "basic",
      "Run a Tiku BASIC program, numbered lines one per line of text, as "
      "the board's one job.  The call answers when it ends, with its output, "
      "and stops it after seconds (60); progress carries each line it prints. "
      "With background true it answers at once and the job runs on: "
      "tiku:///sys/mcp/job/out holds its output, /sys/mcp/job/state says when "
      "it ends, a write to /sys/mcp/job/stop ends it.  INPUT, REBOOT and POKE "
      "are errors in a job, and it cannot write the system's nodes.",
      "{\"type\":\"object\",\"properties\":{\"program\":{\"type\":\"string\"},"
      "\"background\":{\"type\":\"boolean\"},\"seconds\":{\"type\":"
      "\"integer\"}},\"required\":[\"program\"]}", (const char *)0 },
#endif
};

static const tinymcp_template_t templates[] = {
    { URI_ROOT "/{+path}", "a node",
      "a node of the namespace, by the path the board resolves" },
    { URI_ROOT "/{+path}{?line,bytes}", "a page of a node",
      "a long node from line, at most bytes" },
};

/* Commands that take the shell into a mode the tool could not leave. */
static const char *const mode_commands[] = {
    "watch", "basic", "ping", "ntp", "dns", "mqtt", "slip", "telnet"
};

static void capture_putc(char c)
{
    if (capture != (tinymcp_sink_t *)0) {
        tinymcp_sink_putc(capture, c);
    }
}

static uint8_t capture_rx_ready(void)
{
    return 0u;
}

static int capture_getc(void)
{
    return -1;
}

/* The shell's output while a tool runs a line; the cable's authority. */
static const tiku_shell_io_t capture_io = {
    capture_putc, capture_rx_ready, capture_getc, 0u, TIKU_VFS_CAP_ALL
};

/** @brief Run @p line through the shell with its output in @p text. */
static void
run_line(const char *line, tinymcp_sink_t *text)
{
    const tiku_shell_io_t *prev = tiku_shell_io_get_backend();
    char copy[TIKU_SHELL_LINE_SIZE];

    (void)strncpy(copy, line, sizeof copy - 1u);
    copy[sizeof copy - 1u] = 0;
    capture = text;
    tiku_shell_io_set_backend(&capture_io);
    tiku_shell_parser_execute(copy);
    tiku_shell_io_set_backend(prev);
    capture = (tinymcp_sink_t *)0;
}

/** @brief The first word of @p line, when it is a mode command. */
static const char *
mode_command(const char *line)
{
    size_t i, n;

    while (*line == ' ') {
        line++;
    }
    n = strcspn(line, " ");
    for (i = 0; i < sizeof mode_commands / sizeof mode_commands[0]; i++) {
        if (strlen(mode_commands[i]) == n &&
            strncmp(mode_commands[i], line, n) == 0) {
            return mode_commands[i];
        }
    }
    return (const char *)0;
}

const char *
tiku_mcp_actor(void)
{
    return actor != (const char *)0 ? actor : "board";
}

static int run_tool(tinymcp_t *stream, const char *name,
                    const tinymcp_args_t *args, tinymcp_sink_t *text);

/** @brief "<raw * 10^scale>" as a JSON number into @p buf. */
static void
fixed_text(char *buf, size_t cap, int32_t raw, int scale)
{
    unsigned long mag = raw < 0 ? 0ul - (unsigned long)raw : (unsigned long)raw;
    unsigned long div = 1ul;
    int k;

    if (scale >= 0) {
        for (k = 0; k < scale && k < 9; k++) {
            mag *= 10ul;
        }
        (void)snprintf(buf, cap, "%s%lu", raw < 0 ? "-" : "", mag);
        return;
    }
    for (k = 0; k < -scale && k < 9; k++) {
        div *= 10ul;
    }
    (void)snprintf(buf, cap, "%s%lu.%0*lu", raw < 0 ? "-" : "", mag / div,
                   k, mag % div);
}

/**
 * @brief {"value":..,"unit":".."} for a typed node whose text @p head
 *        decodes, from the text already read; NULL for any other.
 */
static const char *
typed_value(const tiku_vfs_node_t *node, const char *head)
{
    static char json[64];
    tiku_vfs_val_t v;
    const char *unit;
    char num[24];

    if (tiku_vfs_val_from_text(node, head, &v) != TIKU_VFS_OK) {
        return (const char *)0;
    }
    switch (v.vtype) {
    case TIKU_VFS_T_U32:
        (void)snprintf(num, sizeof num, "%lu", (unsigned long)v.as.u);
        break;
    case TIKU_VFS_T_I32:
        (void)snprintf(num, sizeof num, "%ld", (long)v.as.i);
        break;
    case TIKU_VFS_T_BOOL:
        (void)snprintf(num, sizeof num, "%s", v.as.u ? "true" : "false");
        break;
    case TIKU_VFS_T_FIXED:
        fixed_text(num, sizeof num, v.as.i, v.scale);
        break;
    default:
        return (const char *)0;
    }
    unit = tiku_vfs_unit_name(v.unit);
    if (unit[0] == 0 || v.unit == TIKU_VFS_U_BOOL) {
        (void)snprintf(json, sizeof json, "{\"value\":%s}", num);
    } else {
        (void)snprintf(json, sizeof json, "{\"value\":%s,\"unit\":\"%s\"}",
                       num, unit);
    }
    return json;
}

/** @brief A journal entry for a tool that changes the board. */
static void
journal_tool(const char *verb, const char *a, const char *b)
{
    char line[TIKU_MCP_JOURNAL_TEXT];

    /* The ask nodes and the journal write their own entries. */
    if (strncmp(a, "/sys/mcp/", 9u) == 0) {
        return;
    }
    (void)snprintf(line, sizeof line, "%s %s%s%s", verb, a, b ? " " : "",
                   b ? b : "");
    tiku_mcp_journal(tiku_mcp_actor(), line);
}

static int
call_tool(void *ctx, const char *name, const tinymcp_args_t *args,
          tinymcp_sink_t *text)
{
    const char *client = tinymcp_client((const tinymcp_t *)ctx);
    int rc;

    actor = client[0] ? client : "agent";
    rc = run_tool((tinymcp_t *)ctx, name, args, text);
    actor = (const char *)0;
    return rc;
}

/**
 * @brief The owner's yes for @p desc, when there is one to ask for.
 * @return TIKU_MCP_CONSENT_GO, or the tool's return code with its reason
 */
static int
consent(tinymcp_t *stream, char kind, const char *a, const char *b,
        const char *desc, tinymcp_sink_t *text)
{
    char action[TIKU_SHELL_LINE_SIZE + 8u];

    if (desc == (const char *)0) {
        return TIKU_MCP_CONSENT_GO;
    }
    (void)snprintf(action, sizeof action, "%c %s%s%s", kind, a,
                   b ? " " : "", b ? b : "");
    return tiku_mcp_consent(stream, action, desc, text);
}

static int
run_tool(tinymcp_t *stream, const char *name, const tinymcp_args_t *args,
         tinymcp_sink_t *text)
{
    const char *path = tinymcp_arg_str(args, "path");
    int rc;

    if (strcmp(name, "shell") == 0) {
        const char *line = tinymcp_arg_str(args, "line");
        const char *mode;

        if (line == (const char *)0) {
            return TINYMCP_EINVAL;
        }
        if (strlen(line) >= TIKU_SHELL_LINE_SIZE) {
            (void)tinymcp_sink_puts(text, "line too long for the shell");
            return TINYMCP_TOOL_ERROR;
        }
        mode = mode_command(line);
        if (mode != (const char *)0) {
            (void)tinymcp_sink_puts(text, mode);
            (void)tinymcp_sink_puts(text, strcmp(mode, "basic") == 0
                                    ? " stays in a mode; the basic tool runs "
                                      "a program"
                                    : " stays in a mode; subscribe to the "
                                      "node instead of watch");
            return TINYMCP_TOOL_ERROR;
        }
        rc = consent(stream, 's', line, (const char *)0,
                     tiku_mcp_consent_line(line), text);
        if (rc != TIKU_MCP_CONSENT_GO) {
            return rc;
        }
        journal_tool("shell", line, (const char *)0);
        run_line(line, text);
        return TINYMCP_OK;
    }
    if (strcmp(name, "basic") == 0) {
        return tiku_mcp_job_tool(stream, args, text);
    }
    if (path == (const char *)0 || path[0] != '/') {
        return TINYMCP_EINVAL;
    }
    if (strcmp(name, "read") == 0) {
        unsigned line = (unsigned)tinymcp_arg_int(args, "line", 0);
        unsigned bytes = (unsigned)tinymcp_arg_int(args, "bytes", 0);
        char head[24];

        rc = node_text(path, line, bytes, text, head, sizeof head);
        if (rc == TIKU_VFS_OK && line == 0u && bytes == 0u) {
            const char *json = typed_value(tiku_vfs_resolve(path), head);

            if (json != (const char *)0) {
                tinymcp_structured(stream, json);
            }
        }
    } else if (strcmp(name, "write") == 0) {
        const char *value = tinymcp_arg_str(args, "value");

        if (value == (const char *)0) {
            return TINYMCP_EINVAL;
        }
        rc = consent(stream, 'w', path, value, tiku_mcp_consent_write(path),
                     text);
        if (rc != TIKU_MCP_CONSENT_GO) {
            return rc;
        }
        rc = tiku_vfs_write(path, value, strlen(value));
        if (rc == TIKU_VFS_OK) {
            journal_tool("write", path, value);
        }
    } else if (strcmp(name, "ls") == 0) {
        rc = tiku_vfs_list(path, ls_entry, text) == 0 ? TIKU_VFS_OK
                                                       : TIKU_VFS_ENOENT;
    } else {
        return TINYMCP_EINVAL;
    }
    if (rc != TIKU_VFS_OK) {
        (void)tinymcp_sink_puts(text, tiku_vfs_strerror(rc));
        return TINYMCP_TOOL_ERROR;
    }
    return TINYMCP_OK;
}

/*---------------------------------------------------------------------------*/
/* WATCHES                                                                   */
/*---------------------------------------------------------------------------*/

static watch_t *
watch_of(const tinymcp_t *stream, unsigned slot)
{
    unsigned i;

    for (i = 0; i < TIKU_MCP_WATCH_MAX; i++) {
        if (watches[i].used && watches[i].stream == stream &&
            watches[i].slot == slot) {
            return &watches[i];
        }
    }
    return (watch_t *)0;
}

static int
watch(void *ctx, const char *uri, unsigned slot)
{
    tinymcp_t *stream = ctx;
    const char *path = uri_path(uri);
    const tiku_vfs_node_t *n;
    watch_t *w;
    unsigned i;

    if (path == (const char *)0 || strlen(path) >= TIKU_VFS_PATH_MAX) {
        return -1;
    }
    n = tiku_vfs_resolve(path);
    if (n == (const tiku_vfs_node_t *)0 || n->type != TIKU_VFS_FILE ||
        (n->desc != (const tiku_vfs_desc_t *)0 &&
         (n->desc->flags & TIKU_VFS_DF_SECRET))) {
        return -1;
    }
    w = watch_of(stream, slot);
    for (i = 0; w == (watch_t *)0 && i < TIKU_MCP_WATCH_MAX; i++) {
        if (!watches[i].used) {
            w = &watches[i];
        }
    }
    if (w == (watch_t *)0) {
        return -1;
    }
    if (tiku_vfs_watch(path, &tiku_mcp_process) < 0) {
        return -1;
    }
    w->stream = stream;
    w->slot = (uint8_t)slot;
    w->node = n;
    (void)strcpy(w->path, path);
    w->used = 1u;
    return 0;
}

static void
unwatch(void *ctx, unsigned slot)
{
    watch_t *w = watch_of(ctx, slot);
    unsigned i, others = 0u;

    if (w == (watch_t *)0) {
        return;
    }
    w->used = 0u;
    for (i = 0; i < TIKU_MCP_WATCH_MAX; i++) {
        if (watches[i].used && watches[i].node == w->node) {
            others++;
        }
    }
    if (others == 0u) {
        (void)tiku_vfs_unwatch(w->path, &tiku_mcp_process);
    }
}

/**
 * @brief Hash /data's names into @p names and its prompt files' names and
 *        text into @p prompts.
 */
static void
data_hashes(uint32_t *names, uint32_t *prompts)
{
    prompt_scan_t ps;
    uint32_t h = 2166136261u;
    unsigned i;
    int n;

    prompt_scan(&ps, 0u);
    for (i = 0; i < ps.kept; i++) {
        h = fnv(h, prompt_names[i], strlen(prompt_names[i]) + 1u);
        n = prompt_text(prompt_of(prompt_names[i]));
        if (n > 0) {
            h = fnv(h, scratch, (size_t)n);
        }
    }
    *names = ps.names;
    *prompts = fnv(h, (const char *)&ps.seen, sizeof ps.seen);
}

/** @brief /data changed: tell the streams which lists did. */
static void
data_changed(void)
{
    uint32_t names, prompts;

    data_hashes(&names, &prompts);
    if (names != data_names_hash) {
        tinymcp_listchanged(tiku_mcp_console_stream());
    }
    if (prompts != prompts_hash) {
        tinymcp_prompts_changed(tiku_mcp_console_stream());
    }
    data_names_hash = names;
    prompts_hash = prompts;
}

/**
 * @brief A changed node to every stream and slot that watches it.
 *
 * The first run, the INIT event, comes after the boot has assembled the
 * tree, so /data is watched from here.
 */
TIKU_PROCESS_THREAD(tiku_mcp_process, ev, data)
{
    TIKU_PROCESS_BEGIN();
    data_dir = tiku_vfs_resolve("/data");
    if (data_dir != (const tiku_vfs_node_t *)0 &&
        tiku_vfs_watch("/data", &tiku_mcp_process) >= 0) {
        data_hashes(&data_names_hash, &prompts_hash);
    } else {
        data_dir = (const tiku_vfs_node_t *)0;
    }
    for (;;) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_EVENT_VFS) {
            const tiku_vfs_node_t *changed = tiku_event_node(ev, data);
            unsigned i;

            if (changed == data_dir && data_dir != (const tiku_vfs_node_t *)0) {
                data_changed();
            }
            for (i = 0; i < TIKU_MCP_WATCH_MAX; i++) {
                if (watches[i].used && watches[i].node == changed) {
                    tinymcp_updated(watches[i].stream, watches[i].slot);
                }
            }
        }
    }
    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* THE HOST                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief A cancelled call: its job stops, journaled under its client. */
static void
cancel_call(void *ctx, unsigned call)
{
    const char *client = tinymcp_client((const tinymcp_t *)ctx);

    actor = client[0] ? client : "agent";
    tiku_mcp_job_cancel(call);
    actor = (const char *)0;
}

static int
send_part(void *ctx, const char *bytes, size_t len, int last)
{
    if (ctx == (void *)tiku_mcp_console_stream()) {
        return tiku_mcp_console_send_part(bytes, len, last);
    }
    return -1;
}

static int
send(void *ctx, const char *msg, size_t len)
{
    if (ctx == (void *)tiku_mcp_console_stream()) {
        return tiku_mcp_console_send(msg, len);
    }
    return -1;
}

const tinymcp_host_t tiku_mcp_host = {
    board_name, TIKU_VERSION, instructions,
    tools, sizeof tools / sizeof tools[0],
    templates, sizeof templates / sizeof templates[0],
    list_resources, read_resource, call_tool, watch, unwatch, send,
    board_title, list_prompts, get_prompt, complete, cancel_call, send_part
};

void
tiku_mcp_identity(void)
{
    int n = tiku_vfs_read("/sys/device/name", board_name, sizeof board_name);

    while (n > 0 && (board_name[n - 1] == '\n' || board_name[n - 1] == '\r')) {
        board_name[--n] = 0;
    }
    if (n <= 0) {
        (void)strcpy(board_name, "tiku");
    }
}

void
tiku_mcp_init(void)
{
    (void)snprintf(board_title, sizeof board_title, "tikuOS on %s",
                   TIKU_DEVICE_NAME);
    (void)snprintf(instructions, sizeof instructions,
                   "tikuOS on %s.  The namespace is the API: a resource "
                   "tiku:///<path> is a node of the board, read as text; "
                   "subscribe to one to hear when it changes.  The read tool "
                   "pages a long node (line, bytes); write changes a node or "
                   "a /data file; ls lists a directory; shell runs one "
                   "command line, and tiku://shell/commands is its "
                   "vocabulary.  The owner's prompts are the files "
                   "/data/prompt.<name>; paths complete.  The board asks: "
                   "subscribe to tiku:///sys/mcp/ask/question, read it when "
                   "it changes, and answer by writing /sys/mcp/ask/answer; "
                   "/sys/mcp/journal records who asked, answered and "
                   "changed what.  Rebooting, sleep, powering down, the CPU "
                   "clock and the memory layout ask the owner first.  The "
                   "basic tool runs a BASIC program as a job, waiting for its "
                   "end with progress, or in the background under "
                   "/sys/mcp/job.",
                   TIKU_DEVICE_NAME);
    (void)strcpy(board_name, "tiku");
    tiku_mcp_ask_init();
    tiku_mcp_console_init();
    tiku_process_start(&tiku_mcp_process, (tiku_event_data_t)0);
}

void
tiku_mcp_status(tiku_mcp_status_t *out)
{
    const tinymcp_t *s = tiku_mcp_console_stream();
    unsigned i;

    out->requests = s->requests;
    out->errors = s->errors;
    out->dropped = s->dropped;
    out->busy = tiku_mcp_console_busy();
    out->subs = 0u;
    for (i = 0; i < TIKU_MCP_WATCH_MAX; i++) {
        if (watches[i].used) {
            out->subs++;
        }
    }
    out->era = tinymcp_era_name(s);
    out->transport = "console";
}
