/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp.c - the host table: the namespace as resources and the read,
 * write, ls and shell tools; the process that turns watches into updates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp.h"

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
static char instructions[400];

/* Where the shell tool's captured output goes while a command runs. */
static tinymcp_sink_t *capture;

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

/*---------------------------------------------------------------------------*/
/* TEXT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief The node's text from line @p first, at most @p bytes when set,
 *        into the sink, page by page.
 * @return 0, or the first negative TIKU_VFS_* status
 */
static int
node_text(const char *path, unsigned first, unsigned bytes,
          tinymcp_sink_t *text)
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
    return from_status(node_text(path, line, bytes, text));
}

/*---------------------------------------------------------------------------*/
/* TOOLS                                                                     */
/*---------------------------------------------------------------------------*/

static const tinymcp_tool_t tools[] = {
    { "read",
      "Read a node of the board's namespace as text, for example /sys/uptime; "
      "a long node from line, within bytes.  Reads a node even when reading "
      "it acts.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
      "\"line\":{\"type\":\"integer\"},\"bytes\":{\"type\":\"integer\"}},"
      "\"required\":[\"path\"]}" },
    { "write",
      "Write a value to a node, as the shell's write does: write /dev/led0 1 "
      "lights the LED; a path under /data creates or replaces a file.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
      "\"value\":{\"type\":\"string\"}},\"required\":[\"path\",\"value\"]}" },
    { "ls",
      "List a directory of the namespace: one line per child, name, d or f, "
      "and perms.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},"
      "\"required\":[\"path\"]}" },
    { "shell",
      "Run one command line as a person types it at tikuOS:/> and return "
      "what it printed.  The vocabulary is the resource tiku://shell/commands; "
      "commands that stay in a mode (watch, basic, ping, ntp, dns, mqtt, slip) "
      "are refused.",
      "{\"type\":\"object\",\"properties\":{\"line\":{\"type\":\"string\"}},"
      "\"required\":[\"line\"]}" },
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

static int
call_tool(void *ctx, const char *name, const tinymcp_args_t *args,
          tinymcp_sink_t *text)
{
    const char *path = tinymcp_arg_str(args, "path");
    int rc;

    (void)ctx;
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
            (void)tinymcp_sink_puts(text, " stays in a mode; subscribe to the "
                                          "node instead of watch");
            return TINYMCP_TOOL_ERROR;
        }
        run_line(line, text);
        return TINYMCP_OK;
    }
    if (path == (const char *)0 || path[0] != '/') {
        return TINYMCP_EINVAL;
    }
    if (strcmp(name, "read") == 0) {
        rc = node_text(path, (unsigned)tinymcp_arg_int(args, "line", 0),
                       (unsigned)tinymcp_arg_int(args, "bytes", 0), text);
    } else if (strcmp(name, "write") == 0) {
        const char *value = tinymcp_arg_str(args, "value");

        if (value == (const char *)0) {
            return TINYMCP_EINVAL;
        }
        rc = tiku_vfs_write(path, value, strlen(value));
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

/** @brief A changed node to every stream and slot that watches it. */
TIKU_PROCESS_THREAD(tiku_mcp_process, ev, data)
{
    TIKU_PROCESS_BEGIN();
    for (;;) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_EVENT_VFS) {
            const tiku_vfs_node_t *changed = tiku_event_node(ev, data);
            unsigned i;

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

static int
send(void *ctx, const char *msg, size_t len)
{
    if (ctx == (void *)tiku_mcp_console_stream()) {
        return tiku_mcp_console_send(msg, len);
    }
    return -1;
}

const tinymcp_host_t tiku_mcp_host = {
    "tinymcp", TINYMCP_VERSION, instructions,
    tools, sizeof tools / sizeof tools[0],
    templates, sizeof templates / sizeof templates[0],
    list_resources, read_resource, call_tool, watch, unwatch, send
};

void
tiku_mcp_init(void)
{
    (void)snprintf(instructions, sizeof instructions,
                   "tikuOS on %s.  The namespace is the API: a resource "
                   "tiku:///<path> is a node of the board, read as text; "
                   "subscribe to one to hear when it changes.  The read tool "
                   "pages a long node (line, bytes); write changes a node or "
                   "a /data file; ls lists a directory; shell runs one "
                   "command line, and tiku://shell/commands is its "
                   "vocabulary.", TIKU_DEVICE_NAME);
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
