/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_ask.c - /sys/mcp/ask and /sys/mcp/journal: a question written by
 * the board, an answer written by an agent, BASIC's ASK, and a RAM ring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_ask.h"

#include <stdio.h>
#include <string.h>

#include "tiku_mcp.h"
#include <kernel/timers/tiku_clock.h>
#include <shell/tiku_shell.h>
#if TIKU_SHELL_CMD_BASIC
#include <basic/tiku_basic_ext.h>
#endif

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static char                 ask_q[TIKU_MCP_ASK_MAX];
static char                 ask_a[TIKU_MCP_ASK_MAX];
static char                 asked_by[TINYMCP_CLIENT_MAX];
static char                 answered_by[TINYMCP_CLIENT_MAX];
static uint32_t             ask_seq;
static tiku_mcp_ask_state_t ask_state;

/** One journal entry: when, who, what. */
typedef struct {
    uint32_t secs;
    char     who[TINYMCP_CLIENT_MAX];
    char     text[TIKU_MCP_JOURNAL_TEXT];
} journal_t;

static journal_t journal[TIKU_MCP_JOURNAL_MAX];
static uint8_t   journal_head;     /* the next entry written */
static uint8_t   journal_count;

/*---------------------------------------------------------------------------*/
/* JOURNAL                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Copy at most @p cap - 1 bytes of @p s, terminated. */
static void
copy_cut(char *dst, size_t cap, const char *s, size_t n)
{
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, s, n);
    dst[n] = 0;
}

/** @brief Append an entry; the oldest goes when the ring is full. */
static void
journal_put(const char *who, const char *text, size_t len)
{
    journal_t *e = &journal[journal_head];

    e->secs = (uint32_t)tiku_clock_seconds();
    copy_cut(e->who, sizeof e->who, who, strlen(who));
    copy_cut(e->text, sizeof e->text, text, len);
    journal_head = (uint8_t)((journal_head + 1u) % TIKU_MCP_JOURNAL_MAX);
    if (journal_count < TIKU_MCP_JOURNAL_MAX) {
        journal_count++;
    }
}

void
tiku_mcp_journal(const char *who, const char *text)
{
    journal_put(who, text, strlen(text));
    /* Subscribers re-read it; the node is /sys/mcp's, so it is looked up. */
    tiku_vfs_notify(tiku_vfs_resolve("/sys/mcp/journal"));
}

/**
 * @brief The journal's text from byte @p offset: one "secs who text" line
 *        per entry, oldest first.
 */
static int
journal_at(uint32_t offset, char *buf, size_t max, uint32_t *total)
{
    char line[TINYMCP_CLIENT_MAX + TIKU_MCP_JOURNAL_TEXT + 16u];
    uint32_t pos = 0u;
    size_t got = 0u;
    unsigned i;

    for (i = 0; i < journal_count; i++) {
        const journal_t *e = &journal[(journal_head + TIKU_MCP_JOURNAL_MAX -
                                       journal_count + i) %
                                      TIKU_MCP_JOURNAL_MAX];
        int n = snprintf(line, sizeof line, "%lu %s %s\n",
                         (unsigned long)e->secs, e->who, e->text);
        uint32_t lo, hi;

        if (n < 0) {
            continue;
        }
        if ((size_t)n >= sizeof line) {
            n = (int)sizeof line - 1;
        }
        lo = pos > offset ? pos : offset;
        hi = pos + (uint32_t)n;
        if (hi > offset + max) {
            hi = offset + (uint32_t)max;
        }
        if (hi > lo) {
            memcpy(buf + (lo - offset), line + (lo - pos), hi - lo);
            got += hi - lo;
        }
        pos += (uint32_t)n;
    }
    *total = pos;
    return (int)got;
}

int
tiku_mcp_journal_read(char *buf, size_t max)
{
    uint32_t total;
    int n;

    if (max == 0u) {
        return 0;
    }
    n = journal_at(0u, buf, max - 1u, &total);
    buf[n] = 0;
    return (int)total;
}

/** A write adds one entry under the writer, a newline at its end dropped. */
int
tiku_mcp_journal_write(const char *buf, size_t len)
{
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len == 0u) {
        return TIKU_VFS_EINVAL;
    }
    journal_put(tiku_mcp_actor(), buf, len);
    return 0;
}

const tiku_vfs_desc_t tiku_mcp_journal_desc = TIKU_VFS_DESC_STREAM(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE,
    journal_at);

/*---------------------------------------------------------------------------*/
/* THE QUESTION                                                              */
/*---------------------------------------------------------------------------*/

/** @brief A journal entry "<verb> #<seq>: <text>". */
static void
journal_ask(const char *who, const char *verb, const char *text)
{
    char line[TIKU_MCP_JOURNAL_TEXT];
    int n = snprintf(line, sizeof line, "%s #%lu: ", verb,
                     (unsigned long)ask_seq);

    /* The text fills what the prefix leaves; a long one is cut. */
    if (n > 0 && (size_t)n < sizeof line) {
        copy_cut(line + n, sizeof line - (size_t)n, text, strlen(text));
    }
    tiku_mcp_journal(who, line);
}

static int
question_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", ask_q);
}

static int
question_write(const char *buf, size_t len)
{
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len == 0u) {
        return TIKU_VFS_EINVAL;
    }
    copy_cut(ask_q, sizeof ask_q, buf, len);
    ask_a[0] = 0;
    answered_by[0] = 0;
    copy_cut(asked_by, sizeof asked_by, tiku_mcp_actor(),
             strlen(tiku_mcp_actor()));
    ask_seq++;
    ask_state = TIKU_MCP_ASK_ASKED;
    journal_ask(asked_by, "asked", ask_q);
    return 0;
}

static int
answer_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", ask_a);
}

/**
 * An answer to the question waiting.  A leading "#<seq> " names the question
 * it answers, and a later question has replaced that one when they differ.
 */
static int
answer_write(const char *buf, size_t len)
{
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    if (len > 1u && buf[0] == '#') {
        unsigned long seq = 0ul;
        size_t i = 1u;

        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            seq = seq * 10ul + (unsigned long)(buf[i++] - '0');
        }
        if (i == 1u || seq != ask_seq) {
            return TIKU_VFS_EINVAL;
        }
        while (i < len && buf[i] == ' ') {
            i++;
        }
        buf += i;
        len -= i;
    }
    if (ask_state != TIKU_MCP_ASK_ASKED || len == 0u) {
        return TIKU_VFS_EINVAL;
    }
    copy_cut(ask_a, sizeof ask_a, buf, len);
    copy_cut(answered_by, sizeof answered_by, tiku_mcp_actor(),
             strlen(tiku_mcp_actor()));
    ask_state = TIKU_MCP_ASK_ANSWERED;
    journal_ask(answered_by, "answered", ask_a);
    return 0;
}

static int
seq_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)ask_seq);
}

static int
state_read(char *buf, size_t max)
{
    static const char *const names[] = { "idle", "asked", "answered" };

    return snprintf(buf, max, "%s\n", names[ask_state]);
}

static int
asked_by_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", asked_by);
}

static int
answered_by_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", answered_by);
}

const tiku_vfs_node_t tiku_mcp_ask_nodes[TIKU_MCP_ASK_NODES] = {
    { .name = "question", .type = TIKU_VFS_FILE, .read = question_read,
      .write = question_write },
    { .name = "answer", .type = TIKU_VFS_FILE, .read = answer_read,
      .write = answer_write },
    { .name = "seq", .type = TIKU_VFS_FILE, .read = seq_read },
    { .name = "state", .type = TIKU_VFS_FILE, .read = state_read },
    { .name = "asked_by", .type = TIKU_VFS_FILE, .read = asked_by_read },
    { .name = "answered_by", .type = TIKU_VFS_FILE, .read = answered_by_read },
};

int
tiku_mcp_ask(const char *q, size_t len)
{
    return tiku_vfs_write("/sys/mcp/ask/question", q, len);
}

uint32_t
tiku_mcp_ask_seq(void)
{
    return ask_seq;
}

tiku_mcp_ask_state_t
tiku_mcp_ask_state(void)
{
    return ask_state;
}

const char *
tiku_mcp_ask_answer(void)
{
    return ask_a;
}

/*---------------------------------------------------------------------------*/
/* BASIC: ASK q$, A$ [, seconds]                                             */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_BASIC

/* The question the running ASK waits on. */
static uint32_t basic_seq;

/** @brief The answer has come, or a later question replaced this one. */
static int
basic_ask_ready(void *ctx)
{
    (void)ctx;
    return ask_seq != basic_seq || ask_state == TIKU_MCP_ASK_ANSWERED;
}

/**
 * @brief ASK q$, A$ [, seconds]: ask q$ and wait for the answer in A$.
 *
 * The variable is empty while the program waits, and stays empty when the
 * seconds pass first or a later question replaces this one.  No seconds, or
 * 0, waits until the answer.
 */
static void
basic_ask(const char **p)
{
    char q[TIKU_MCP_ASK_MAX];
    int why = tiku_basic_ext_waited();
    const char *reply = "";
    const char *r;
    long seconds = 0;

    if (tiku_basic_ext_parse_strexpr(p, q, sizeof q) != 0 ||
        tiku_basic_ext_expect(p, ',') != 0) {
        return;
    }
    if (why == 1 && ask_seq == basic_seq &&
        ask_state == TIKU_MCP_ASK_ANSWERED) {
        reply = ask_a;
    }
    if (tiku_basic_ext_let_str(p, reply) != 0) {
        return;
    }
    for (r = *p; *r == ' '; r++) {
    }
    if (*r == ',') {
        *p = r + 1;
        if (tiku_basic_ext_parse_expr(p, &seconds) != 0) {
            return;
        }
    }
    if (why != 0) {
        return;
    }
    if (q[0] == 0) {
        tiku_basic_ext_error(1, "ASK needs a question");
        return;
    }
    if (tiku_basic_ext_wait(basic_ask_ready, (void *)0,
                            seconds > 0 ? (unsigned long)seconds : 0ul) != 0) {
        tiku_basic_ext_error(1, "ASK waits only as a statement of its own "
                                "in a running program");
        return;
    }
    (void)tiku_mcp_ask(q, strlen(q));
    basic_seq = ask_seq;
}

#endif /* TIKU_SHELL_CMD_BASIC */

void
tiku_mcp_ask_init(void)
{
#if TIKU_SHELL_CMD_BASIC
    (void)tiku_basic_register_stmt("ASK", basic_ask);
#endif
}
