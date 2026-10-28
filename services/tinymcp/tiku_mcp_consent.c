/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_consent.c - which actions ask the owner first, and the one
 * question for consent open at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mcp_consent.h"

#include <stdio.h>
#include <string.h>

#include "tiku_mcp.h"
#include "tiku_mcp_ask.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/vfs/tiku_vfs.h>
#include <shell/tiku_shell.h>

/*---------------------------------------------------------------------------*/
/* WHAT ASKS                                                                 */
/*---------------------------------------------------------------------------*/

/* Writes that ask, by path prefix. */
static const struct {
    const char *prefix;
    const char *desc;
} writes[] = {
    { "/sys/cpu/freq",           "change the CPU clock" },
    { "/sys/mem/layout/stage",   "change the memory layout, which can erase "
                                 "the store" },
    { "/sys/mem/layout/recover", "recover a memory layout over the store" },
};

const char *
tiku_mcp_consent_write(const char *path)
{
    unsigned i;

    for (i = 0; i < sizeof writes / sizeof writes[0]; i++) {
        if (strncmp(path, writes[i].prefix, strlen(writes[i].prefix)) == 0) {
            return writes[i].desc;
        }
    }
    return (const char *)0;
}

#define WORDS_MAX 12u

/** @brief Split @p line at spaces into @p w, in @p buf; the word count. */
static unsigned
words(const char *line, char *buf, size_t cap, const char *w[WORDS_MAX])
{
    unsigned n = 0u;
    char *p;

    (void)strncpy(buf, line, cap - 1u);
    buf[cap - 1u] = 0;
    for (p = buf; *p != 0 && n < WORDS_MAX;) {
        while (*p == ' ') {
            *p++ = 0;
        }
        if (*p == 0) {
            break;
        }
        w[n++] = p;
        while (*p != 0 && *p != ' ') {
            p++;
        }
    }
    return n;
}

/** @brief The phrase for the command in w[0..n), or NULL. */
static const char *
command_desc(const char *const *w, unsigned n)
{
    unsigned i;

    if (n == 0u) {
        return (const char *)0;
    }
    if (strcmp(w[0], "reboot") == 0) {
        return "reboot the board";
    }
    if (strcmp(w[0], "sleep") == 0 && n > 1u) {
        return "change the low-power sleep mode";
    }
    if (strcmp(w[0], "power") == 0) {
        for (i = 1u; i < n; i++) {
            if (strcmp(w[i], "off") == 0 || strcmp(w[i], "down") == 0) {
                return "power a part of the board down";
            }
        }
        return (const char *)0;
    }
    if (strcmp(w[0], "freq") == 0 && n > 1u && strcmp(w[1], "probe") != 0) {
        return "change the CPU clock";
    }
    if (strcmp(w[0], "layout") == 0 && n > 1u &&
        (strcmp(w[1], "stage") == 0 || strcmp(w[1], "recover") == 0)) {
        return "change the memory layout, which can erase the store";
    }
    if (strcmp(w[0], "write") == 0 && n > 1u) {
        return tiku_mcp_consent_write(w[1]);
    }
    /* A command run later by a job or a rule is judged as itself. */
    if ((strcmp(w[0], "once") == 0 || strcmp(w[0], "every") == 0) && n > 2u) {
        return command_desc(w + 2, n - 2u);
    }
    if (strcmp(w[0], "on") == 0 && n > 1u) {
        unsigned at = strcmp(w[1], "changed") == 0 ? 3u : 4u;

        return n > at ? command_desc(w + at, n - at) : (const char *)0;
    }
    return (const char *)0;
}

const char *
tiku_mcp_consent_line(const char *line)
{
    char buf[TIKU_SHELL_LINE_SIZE];
    const char *w[WORDS_MAX];

    return command_desc(w, words(line, buf, sizeof buf, w));
}

/*---------------------------------------------------------------------------*/
/* THE OPEN QUESTION                                                         */
/*---------------------------------------------------------------------------*/

#define ACTION_MAX (TIKU_SHELL_LINE_SIZE + 8u)

static struct {
    char          action[ACTION_MAX];      /* "w <path> <value>" or "s <line>" */
    char          client[TINYMCP_CLIENT_MAX];
    char          state[12];               /* "c<nonce>": the form's state */
    const char   *desc;
    unsigned long token;                   /* the handshake era's code */
    unsigned long since;                   /* seconds when it was asked */
    uint8_t       used;
    uint8_t       granted;                 /* the token came back */
} pending;

static unsigned long serial;               /* questions asked since boot */

/* The form: one checkbox. */
static const char schema[] =
    "{\"type\":\"object\",\"properties\":{\"confirm\":{\"type\":\"boolean\","
    "\"title\":\"Allow it\"}},\"required\":[\"confirm\"]}";
static char message[160];

/** @brief Drop the question once its time has passed. */
static void
expire(void)
{
    if (pending.used &&
        tiku_clock_seconds() - pending.since > TIKU_MCP_CONSENT_TTL_S) {
        pending.used = 0u;
    }
}

/** @brief A journal entry "<verb>: <desc>" under @p who. */
static void
journal_consent(const char *who, const char *verb, const char *desc)
{
    char line[TIKU_MCP_JOURNAL_TEXT];

    (void)snprintf(line, sizeof line, "%s: %s", verb, desc);
    tiku_mcp_journal(who, line);
}

/** @brief Open a new question for @p action from @p who. */
static void
open_question(const char *action, const char *desc, const char *who)
{
    serial++;
    (void)strncpy(pending.action, action, sizeof pending.action - 1u);
    pending.action[sizeof pending.action - 1u] = 0;
    (void)strncpy(pending.client, who, sizeof pending.client - 1u);
    pending.client[sizeof pending.client - 1u] = 0;
    (void)snprintf(pending.state, sizeof pending.state, "c%lu", serial);
    /* Six digits from the clock and the count: a code the person relays,
     * not a secret from the client, which reads it in the refusal. */
    pending.token = (((unsigned long)tiku_clock_time() * 2654435761ul) ^
                     (serial * 40503ul)) % 900000ul + 100000ul;
    pending.desc = desc;
    pending.since = tiku_clock_seconds();
    pending.granted = 0u;
    pending.used = 1u;
}

int
tiku_mcp_consent(tinymcp_t *stream, const char *action, const char *desc,
                 tinymcp_sink_t *text)
{
    const char *who = tinymcp_client(stream)[0] ? tinymcp_client(stream)
                                                : "agent";
    int same;

    expire();
    same = pending.used && strcmp(pending.action, action) == 0 &&
           strcmp(pending.client, who) == 0;
    if (tinymcp_client_elicits(stream)) {
        char state[sizeof pending.state];
        int act = tinymcp_input_action(stream, "confirm");

        if (same && tinymcp_request_state(stream, state, sizeof state) >= 0 &&
            strcmp(state, pending.state) == 0) {
            if (act == TINYMCP_INPUT_ACCEPT &&
                tinymcp_input_bool(stream, "confirm", "confirm") == 1) {
                pending.used = 0u;
                journal_consent(who, "allowed", desc);
                return TIKU_MCP_CONSENT_GO;
            }
            if (act == TINYMCP_INPUT_DECLINE || act == TINYMCP_INPUT_CANCEL) {
                pending.used = 0u;
                journal_consent(who, "refused", desc);
                (void)tinymcp_sink_puts(text, "the owner said no; nothing "
                                              "was done");
                return TINYMCP_TOOL_ERROR;
            }
        }
        open_question(action, desc, who);
        (void)snprintf(message, sizeof message, "%s asks to %s.  Allow it?",
                       who, desc);
        (void)tinymcp_elicit(stream, "confirm", message, schema,
                             pending.state);
        journal_consent(who, "asked consent", desc);
        return TINYMCP_NEED_INPUT;
    }
    if (same && pending.granted) {
        pending.used = 0u;
        journal_consent(who, "allowed", desc);
        return TIKU_MCP_CONSENT_GO;
    }
    open_question(action, desc, who);
    {
        char line[200];

        (void)snprintf(line, sizeof line,
                       "%s needs the owner's yes, and this client cannot ask "
                       "through MCP.  Ask the person; with their yes, write "
                       "%lu to /sys/mcp/consent and make the same call again "
                       "within %u s.", desc, pending.token,
                       (unsigned)TIKU_MCP_CONSENT_TTL_S);
        (void)tinymcp_sink_puts(text, line);
    }
    journal_consent(who, "asked consent", desc);
    return TINYMCP_TOOL_ERROR;
}

int
tiku_mcp_consent_read(char *buf, size_t max)
{
    expire();
    if (!pending.used) {
        return snprintf(buf, max, "none\n");
    }
    return snprintf(buf, max, "%s asks to %s%s\n", pending.client,
                    pending.desc, pending.granted ? " (allowed)" : "");
}

int
tiku_mcp_consent_grant(const char *buf, size_t len)
{
    unsigned long token = 0ul;
    size_t i;

    expire();
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
    }
    for (i = 0u; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return TIKU_VFS_EINVAL;
        }
        token = token * 10ul + (unsigned long)(buf[i] - '0');
    }
    if (len == 0u || !pending.used || token != pending.token) {
        return TIKU_VFS_EINVAL;
    }
    pending.granted = 1u;
    journal_consent(tiku_mcp_actor(), "granted", pending.desc);
    return 0;
}
