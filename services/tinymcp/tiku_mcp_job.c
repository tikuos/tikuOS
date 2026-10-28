/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_job.c - one BASIC job at a time: its output in a ring, its
 * progress to the call that waits, its end as that call's result, and the
 * /sys/mcp/job nodes for a job left to run in the background.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_job.h"

#include <stdio.h>
#include <string.h>

#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"
#include <kernel/timers/tiku_clock.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#if TIKU_SHELL_CMD_BASIC
#include <basic/tiku_basic.h>
#endif

#define LINE_MAX_LEN 96u           /* a printed line kept for a progress note */

static struct {
    char              out[TIKU_MCP_JOB_OUT]; /* the last bytes, a ring */
    uint32_t          written;               /* bytes ever put in the ring */
    char              line[LINE_MAX_LEN];    /* the line being printed */
    char              last[LINE_MAX_LEN];    /* the last whole line */
    uint8_t           line_len;
    uint8_t           esc;                   /* inside an ANSI escape: 1, 2 */
    uint32_t          lines;                 /* whole lines printed */
    uint32_t          lines_noted;           /* lines when progress last went */
    uint8_t           dirty;                 /* output since the last note */
    uint8_t           state;                 /* TIKU_BASIC_JOB_*, as last seen */
    uint8_t           timed_out;             /* its call's budget ended it */
    uint32_t          seq;                   /* the job's number */
    char              by[TINYMCP_CLIENT_MAX];
    tinymcp_t        *stream;                /* the stream of the waiting call */
    unsigned          call;                  /* that call, 0: none waits */
    unsigned long     started;               /* seconds at the start */
    unsigned long     budget;                /* seconds it may run, 0: no end */
    tiku_clock_time_t noted;                 /* ticks at the last note */
} job;

static const char *const state_names[] = {
    "idle", "running", "done", "error", "stopped"
};

/*---------------------------------------------------------------------------*/
/* OUTPUT                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief One byte of the job's output; ANSI escapes are dropped. */
static void
job_putc(char c)
{
    if (job.esc == 1u) {
        job.esc = c == '[' ? 2u : 0u;
        return;
    }
    if (job.esc == 2u) {
        if (c >= 0x40 && c <= 0x7E) {
            job.esc = 0u;
        }
        return;
    }
    if (c == 0x1B) {
        job.esc = 1u;
        return;
    }
    if (c == '\r') {
        return;
    }
    job.out[job.written % TIKU_MCP_JOB_OUT] = c;
    job.written++;
    job.dirty = 1u;
    if (c == '\n') {
        job.line[job.line_len] = 0;
        (void)memcpy(job.last, job.line, (size_t)job.line_len + 1u);
        job.line_len = 0u;
        job.lines++;
    } else if (job.line_len + 1u < LINE_MAX_LEN) {
        job.line[job.line_len++] = c;
    }
}

static uint8_t
job_rx_ready(void)
{
    return 0u;
}

static int
job_getc(void)
{
    return -1;
}

/* A job has no input and no authority over the system's nodes: the clock,
 * power, the layout and the watchdog refuse its writes. */
static const tiku_shell_io_t job_io = {
    job_putc, job_rx_ready, job_getc, 0u,
    (uint8_t)(TIKU_VFS_CAP_ALL & ~TIKU_VFS_CAP_SYS)
};

/**
 * @brief The output from byte @p offset of what the ring holds, oldest
 *        first, into @p buf.
 */
static int
out_at(uint32_t offset, char *buf, size_t max, uint32_t *total)
{
    uint32_t kept = job.written < TIKU_MCP_JOB_OUT ? job.written
                                                   : TIKU_MCP_JOB_OUT;
    uint32_t first = job.written - kept;
    size_t got = 0u;

    while (offset + got < kept && got < max) {
        buf[got] = job.out[(first + offset + got) % TIKU_MCP_JOB_OUT];
        got++;
    }
    *total = kept;
    return (int)got;
}

/** @brief Every byte the ring holds into @p text. */
static void
out_to(tinymcp_sink_t *text)
{
    char chunk[64];
    uint32_t total, at = 0u;
    int n;

    while ((n = out_at(at, chunk, sizeof chunk, &total)) > 0) {
        (void)tinymcp_sink_write(text, chunk, (size_t)n);
        at += (uint32_t)n;
    }
}

/*---------------------------------------------------------------------------*/
/* NOTES                                                                     */
/*---------------------------------------------------------------------------*/

static void
notify(const char *path)
{
    tiku_vfs_notify(tiku_vfs_resolve(path));
}

/** @brief A journal entry "job #<seq>: <what>" under @p who. */
static void
journal_job(const char *who, const char *what)
{
    char line[TIKU_MCP_JOURNAL_TEXT];

    (void)snprintf(line, sizeof line, "job #%lu: %s", (unsigned long)job.seq,
                   what);
    tiku_mcp_journal(who, line);
}

/*---------------------------------------------------------------------------*/
/* THE TOOL                                                                  */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_BASIC
/** @brief A tool error with @p why. */
static int
refuse(tinymcp_sink_t *text, const char *why)
{
    (void)tinymcp_sink_puts(text, why);
    return TINYMCP_TOOL_ERROR;
}

int
tiku_mcp_job_tool(tinymcp_t *stream, const tinymcp_args_t *args,
                  tinymcp_sink_t *text)
{
    const char *program = tinymcp_arg_str(args, "program");
    int background = tinymcp_arg_bool(args, "background", 0);
    long seconds = tinymcp_arg_int(args, "seconds",
                                   background ? 0L
                                              : (long)TIKU_MCP_JOB_BUDGET_S);
    unsigned call = 0u;
    char line[96];

    if (program == (const char *)0 || seconds < 0L ||
        seconds > TIKU_MCP_JOB_SECONDS_MAX) {
        return TINYMCP_EINVAL;
    }
    if (job.state == TIKU_BASIC_JOB_RUNNING) {
        (void)snprintf(line, sizeof line, "job #%lu is running; one job runs "
                       "at a time, and /sys/mcp/job/stop ends it",
                       (unsigned long)job.seq);
        return refuse(text, line);
    }
    if (tiku_basic_mode_active()) {
        return refuse(text, "BASIC is in use at the console");
    }
    if (!background) {
        call = tinymcp_defer(stream);
        if (call == 0u) {
            return refuse(text, "too many calls wait already; run it with "
                                "background true");
        }
    }
    job.written = 0u;
    job.line_len = 0u;
    job.last[0] = 0;
    job.esc = 0u;
    job.lines = 0u;
    job.lines_noted = 0u;
    job.dirty = 0u;
    job.timed_out = 0u;
    if (tiku_basic_job_start(program, &job_io) != 0) {
        /* The reason is in the ring; the call is answered now. */
        out_to(text);
        return TINYMCP_TOOL_ERROR;
    }
    job.seq++;
    (void)snprintf(job.by, sizeof job.by, "%s", tiku_mcp_actor());
    job.state = TIKU_BASIC_JOB_RUNNING;
    job.stream = stream;
    job.call = call;
    job.started = tiku_clock_seconds();
    job.budget = (unsigned long)seconds;
    job.noted = tiku_clock_time();
    journal_job(tiku_mcp_actor(), "started");
    notify("/sys/mcp/job/seq");
    notify("/sys/mcp/job/state");
    if (call != 0u) {
        return TINYMCP_LATER;
    }
    (void)snprintf(line, sizeof line, "job #%lu runs", (unsigned long)job.seq);
    (void)tinymcp_sink_puts(text, line);
    (void)tinymcp_sink_puts(text, "; tiku:///sys/mcp/job/out holds its "
                                  "output, /sys/mcp/job/state says when it "
                                  "ends, and a write to /sys/mcp/job/stop "
                                  "ends it");
    return TINYMCP_OK;
}

void
tiku_mcp_job_cancel(unsigned call)
{
    if (call == 0u || call != job.call) {
        return;
    }
    job.call = 0u;
    if (job.state == TIKU_BASIC_JOB_RUNNING) {
        tiku_basic_job_stop();
        journal_job(tiku_mcp_actor(), "cancelled");
    }
}

/** @brief The waiting call's result: the output, then how the job ended. */
static void
finish(int state)
{
    tinymcp_sink_t text;
    char line[96];

    if (tinymcp_result_begin(job.stream, job.call, &text) != 0) {
        return;                         /* in a request: the next tick sends */
    }
    out_to(&text);
    if (job.timed_out) {
        (void)snprintf(line, sizeof line, "[stopped: the call's %lu s ran "
                       "out; background true runs it longer]",
                       job.budget);
        (void)tinymcp_sink_puts(&text, line);
    }
    tinymcp_result_end(job.stream, job.call, &text,
                       state != TIKU_BASIC_JOB_DONE);
    job.call = 0u;
}

void
tiku_mcp_job_tick(void)
{
    int state;

    if (job.state != TIKU_BASIC_JOB_RUNNING) {
        if (job.call != 0u) {
            finish(job.state);
        }
        return;
    }
    state = tiku_basic_job_state();
    if (state == TIKU_BASIC_JOB_RUNNING && job.budget != 0u &&
        tiku_clock_seconds() - job.started >= job.budget) {
        job.timed_out = 1u;
        tiku_basic_job_stop();
        state = tiku_basic_job_state();
    }
    if (state == TIKU_BASIC_JOB_RUNNING) {
        if (job.dirty && (tiku_clock_time_t)(tiku_clock_time() - job.noted) >=
                             TIKU_CLOCK_MS_TO_TICKS(TIKU_MCP_JOB_NOTE_MS)) {
            job.dirty = 0u;
            job.noted = tiku_clock_time();
            if (job.call != 0u && job.lines != job.lines_noted) {
                job.lines_noted = job.lines;
                (void)tinymcp_progress(job.stream, job.call, job.lines, 0ul,
                                       job.last);
            }
            notify("/sys/mcp/job/out");
        }
        return;
    }
    job.state = (uint8_t)state;
    if (state == TIKU_BASIC_JOB_DONE) {
        journal_job("board", "done");
    } else if (state == TIKU_BASIC_JOB_ERROR) {
        journal_job("board", "error");
    } else if (job.timed_out) {
        journal_job("board", "its call's time ran out");
    }
    notify("/sys/mcp/job/out");
    notify("/sys/mcp/job/state");
    if (job.call != 0u) {
        finish(state);
    }
}
#else
int
tiku_mcp_job_tool(tinymcp_t *stream, const tinymcp_args_t *args,
                  tinymcp_sink_t *text)
{
    (void)stream;
    (void)args;
    (void)tinymcp_sink_puts(text, "this build has no BASIC");
    return TINYMCP_TOOL_ERROR;
}

void
tiku_mcp_job_cancel(unsigned call)
{
    (void)call;
}

void
tiku_mcp_job_tick(void)
{
}
#endif

/*---------------------------------------------------------------------------*/
/* /sys/mcp/job                                                              */
/*---------------------------------------------------------------------------*/

static int
seq_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)job.seq);
}

static int
state_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", state_names[job.state < 5u ? job.state
                                                                  : 0u]);
}

static int
by_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", job.by);
}

static int
out_read(char *buf, size_t max)
{
    uint32_t total;
    int n;

    if (max == 0u) {
        return 0;
    }
    n = out_at(0u, buf, max - 1u, &total);
    buf[n] = 0;
    return (int)total;
}

static const tiku_vfs_desc_t out_desc = TIKU_VFS_DESC_STREAM(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE,
    out_at);

/** Any write ends the running job; with none running it is EINVAL. */
static int
stop_write(const char *buf, size_t len)
{
    (void)buf;
    (void)len;
#if TIKU_SHELL_CMD_BASIC
    if (job.state == TIKU_BASIC_JOB_RUNNING &&
        tiku_basic_job_state() == TIKU_BASIC_JOB_RUNNING) {
        tiku_basic_job_stop();
        journal_job(tiku_mcp_actor(), "stopped");
        return 0;
    }
#endif
    return TIKU_VFS_EINVAL;
}

const tiku_vfs_node_t tiku_mcp_job_nodes[TIKU_MCP_JOB_NODES] = {
    { .name = "seq",   .type = TIKU_VFS_FILE, .read = seq_read },
    { .name = "state", .type = TIKU_VFS_FILE, .read = state_read },
    { .name = "by",    .type = TIKU_VFS_FILE, .read = by_read },
    { .name = "out",   .type = TIKU_VFS_FILE, .read = out_read,
      .desc = &out_desc },
    { .name = "stop",  .type = TIKU_VFS_FILE, .write = stop_write },
};
