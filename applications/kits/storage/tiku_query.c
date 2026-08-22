/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_query.c - predicates, matching, and walking the namespace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_query.h"
#include "tiku_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define RESULTS_MAX 512
#define QUEUE_MAX   256

/** @brief Unit named by a relative date, in seconds. */
static int64_t
relative_unit(const char *unit)
{
    if (strcasecmp(unit, "second") == 0 ||
        strcasecmp(unit, "seconds") == 0) {
        return 1;
    }
    if (strcasecmp(unit, "minute") == 0 ||
        strcasecmp(unit, "minutes") == 0) {
        return 60;
    }
    if (strcasecmp(unit, "hour") == 0 ||
        strcasecmp(unit, "hours") == 0) {
        return 3600;
    }
    if (strcasecmp(unit, "day") == 0 ||
        strcasecmp(unit, "days") == 0) {
        return 86400;
    }
    return 0;
}

/** @brief Whether a date spelling moves when the wall clock moves. */
static int
relative_date(const char *text)
{
    long long amount;
    char unit[16], direction[16], tail[16];
    int n;

    if (text == NULL) {
        return 0;
    }
    if (strcasecmp(text, "now") == 0 || strcasecmp(text, "today") == 0 ||
        strcasecmp(text, "yesterday") == 0 ||
        strcasecmp(text, "tomorrow") == 0) {
        return 1;
    }
    n = sscanf(text, "%lld %15s %15s %15s", &amount, unit, direction,
               tail);
    if (n == 3 && relative_unit(unit) != 0 &&
        strcasecmp(direction, "ago") == 0) {
        return 1;
    }
    return n == 4 && relative_unit(unit) != 0 &&
           strcasecmp(direction, "from") == 0 &&
           strcasecmp(tail, "now") == 0;
}

void
tiku_query_init(tiku_query_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof *q);
        q->max_results = RESULTS_MAX;
    }
}

void
tiku_query_set_name(tiku_query_t *q, const char *text)
{
    if (q == NULL) {
        return;
    }
    q->mode = TIKU_QMODE_NAME;
    q->term_count = 0;
    /* The star decides the operator, and it is looked for ANYWHERE in the
     * typed text rather than at either end: "*.txt" and "a*b" are both
     * whole-name patterns, "txt" is a substring. */
    (void)tiku_query_add_term(q, TIKU_QF_NAME,
                                  (text != NULL && strchr(text, '*') != NULL)
                                      ? TIKU_OP_IS
                                      : TIKU_OP_CONTAINS,
                                  text, TIKU_JOIN_AND);
}

/** @brief Map the formula's field vocabulary back to the model enum. */
static int
formula_field(const char *s, size_t n, tiku_qfield_t *out)
{
    static const struct {
        const char *name;
        tiku_qfield_t field;
    } fields[] = {
        { "name", TIKU_QF_NAME }, { "path", TIKU_QF_PATH },
        { "kind", TIKU_QF_KIND }, { "value", TIKU_QF_VALUE },
        { "size", TIKU_QF_SIZE }, { "cap", TIKU_QF_CAP },
        { "unit", TIKU_QF_UNIT },
        { "writable", TIKU_QF_WRITABLE },
        { "type", TIKU_QF_TYPE },
        { "modified", TIKU_QF_MODIFIED }
    };
    size_t i;

    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (strlen(fields[i].name) == n &&
            strncmp(fields[i].name, s, n) == 0) {
            *out = fields[i].field;
            return 1;
        }
    }
    return 0;
}

int
tiku_query_set_formula(tiku_query_t *q, const char *formula)
{
    const char *p = formula;
    tiku_query_t parsed;
    tiku_qjoin_t join = TIKU_JOIN_AND;

    if (q == NULL) {
        return -1;
    }
    /* Leave a positively invalid query on every failure path.  Otherwise a
     * bad edit could leave the previously parsed formula live, or become an
     * empty predicate that matches every node. */
    tiku_query_init(q);
    q->mode = TIKU_QMODE_FORMULA;
    q->invalid = 1;
    if (formula == NULL) {
        return -1;
    }
    tiku_query_init(&parsed);
    parsed.mode = TIKU_QMODE_FORMULA;
    while (*p != '\0') {
        const char *field, *value;
        size_t fn, vn;
        tiku_qfield_t f;
        tiku_qop_t op;
        int quoted = 0, at;

        while (*p == ' ' || *p == '\t') { p++; }
        if (*p++ != '(') { return -1; }
        field = p;
        while ((*p >= 'a' && *p <= 'z') || *p == '_') { p++; }
        fn = (size_t)(p - field);
        if (!formula_field(field, fn, &f)) { return -1; }
        if (strncmp(p, "==", 2) == 0) {
            op = TIKU_OP_IS; p += 2;
        } else if (strncmp(p, "!=", 2) == 0) {
            op = TIKU_OP_IS_NOT; p += 2;
        } else if (*p == '>') {
            op = TIKU_OP_GREATER; p++;
        } else if (*p == '<') {
            op = TIKU_OP_LESS; p++;
        } else {
            return -1;
        }
        if (*p == '"') {
            quoted = 1;
            value = ++p;
            while (*p != '\0' && *p != '"') { p++; }
            if (*p != '"') { return -1; }
            vn = (size_t)(p - value);
            p++;
        } else {
            value = p;
            while (*p != '\0' && *p != ')') { p++; }
            vn = (size_t)(p - value);
        }
        if (*p++ != ')' || vn >= TIKU_QUERY_VALUE_MAX) { return -1; }
        {
            char text[TIKU_QUERY_VALUE_MAX];

            memcpy(text, value, vn);
            text[vn] = '\0';
            at = tiku_query_add_term(&parsed, f, op, text, join);
        }
        if (at < 0) { return -1; }
        parsed.term[at].pattern_ready = quoted;
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }
        if (strncmp(p, "&&", 2) == 0) {
            join = TIKU_JOIN_AND;
        } else if (strncmp(p, "||", 2) == 0) {
            join = TIKU_JOIN_OR;
        } else {
            return -1;
        }
        p += 2;
    }
    if (parsed.term_count == 0) { return -1; }
    parsed.invalid = 0;
    *q = parsed;
    return 0;
}

int
tiku_query_add_term(tiku_query_t *q, tiku_qfield_t field,
                        tiku_qop_t op, const char *value,
                        tiku_qjoin_t join)
{
    tiku_qterm_t *t;

    if (q == NULL || q->term_count >= TIKU_QUERY_TERMS_MAX) {
        return -1;
    }
    t = &q->term[q->term_count];
    memset(t, 0, sizeof *t);
    t->field = field;
    t->op = op;
    t->join = join;
    snprintf(t->value, sizeof t->value, "%s", (value != NULL) ? value : "");
    if (field == TIKU_QF_MODIFIED && relative_date(t->value)) {
        q->dynamic_date = 1;
    }
    return q->term_count++;
}

int
tiku_query_add_scope(tiku_query_t *q, const char *path)
{
    char norm[TIKU_PATH_MAX];
    int i;
    size_t n;

    if (q == NULL || path == NULL || path[0] == '\0' ||
        q->scope_count >= TIKU_QUERY_SCOPES_MAX) {
        return 0;
    }
    /* Normalise BEFORE comparing: a trailing separator is appended so the
     * prefix test cannot match a sibling whose name merely starts the same
     * way (/dev must not admit /devices), and comparing the raw path against
     * an already-normalised one would never find the duplicate. */
    snprintf(norm, sizeof norm, "%s", path);
    n = strlen(norm);
    if (n > 0u && norm[n - 1u] != '/' && n + 1u < sizeof norm) {
        norm[n] = '/';
        norm[n + 1u] = '\0';
    }
    for (i = 0; i < q->scope_count; i++) {
        if (strcmp(q->scope[i], norm) == 0) {
            return 0;
        }
    }
    memcpy(q->scope[q->scope_count], norm, strlen(norm) + 1u);
    q->scope_count++;
    return 1;
}

void
tiku_query_clear_scope(tiku_query_t *q)
{
    if (q != NULL) {
        q->scope_count = 0;
    }
}

/** @brief Turn the normalised stored path back into a stat-able path. */
static void
scope_path(const char *stored, char *out, size_t max)
{
    size_t n;

    snprintf(out, max, "%s", stored);
    n = strlen(out);
    if (n > 1u && out[n - 1u] == '/') {
        out[n - 1u] = '\0';
    }
}

void
tiku_query_bind_scopes(tiku_query_t *q,
                           tiku_backend_t *backend)
{
    int i;

    if (q == NULL || backend == NULL || backend->ops->stat == NULL) {
        return;
    }
    for (i = 0; i < q->scope_count; i++) {
        tiku_model_t m;
        char path[TIKU_PATH_MAX];

        if (q->scope_was[i].dev != 0u) {
            continue;           /* keep the identity the record named */
        }
        scope_path(q->scope[i], path, sizeof path);
        if (backend->ops->stat(backend, path, &m) != 0 ||
            !tiku_model_is_container(&m)) {
            continue;
        }
        snprintf(q->scope_was[i].path, sizeof q->scope_was[i].path,
                 "%s", q->scope[i]);
        q->scope_was[i].dev = m.facts.dev;
        snprintf(q->scope_was[i].volume, sizeof q->scope_was[i].volume,
                 "%s", backend->devid[0] != '\0' ? backend->devid
                                                   : m.name);
    }
}

/** @brief Whether a saved scope still names the container it was bound to. */
static int
scope_available(const tiku_query_t *q, int at,
                tiku_backend_t *backend)
{
    tiku_model_t m;
    char path[TIKU_PATH_MAX];

    if (backend->ops->stat == NULL) {
        return 1;               /* this backend cannot revalidate */
    }
    scope_path(q->scope[at], path, sizeof path);
    if (backend->ops->stat(backend, path, &m) != 0) {
        return 0;
    }
    return q->scope_was[at].dev == 0u || m.facts.dev == 0u ||
           q->scope_was[at].dev == m.facts.dev;
}

int
tiku_query_in_scope(const tiku_query_t *q, const char *path)
{
    int i;

    if (q == NULL || q->scope_count == 0) {
        return 1;                     /* no filter: the whole namespace   */
    }
    for (i = 0; i < q->scope_count; i++) {
        size_t n = strlen(q->scope[i]);
        size_t prefix_n = n;

        /* Scopes are stored with a trailing separator so child prefixes are
         * unambiguous.  A file scope has no child and must still admit the
         * exact node itself (PVN-070). */
        if (prefix_n > 1u && q->scope[i][prefix_n - 1u] == '/') {
            prefix_n--;
        }

        if ((strncmp(q->scope[i], path, n) == 0) ||
            (strncmp(q->scope[i], path, prefix_n) == 0 &&
             path[prefix_n] == '\0')) {
            return 1;
        }
    }
    return 0;
}

int
tiku_query_allows_path(const tiku_query_t *q,
                           const tiku_model_t *m)
{
    size_t n;

    if (q == NULL || m == NULL || q->include_trash) {
        return 1;
    }
    if (m->kind == TIKU_KIND_TRASH) {
        return 0;
    }
    if (q->trash_path[0] == '\0') {
        return 1;
    }
    n = strlen(q->trash_path);
    return !(strncmp(m->path, q->trash_path, n) == 0 &&
             (m->path[n] == '\0' || m->path[n] == '/'));
}

/*---------------------------------------------------------------------------*/
/* matching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Scalar syntax used by typed attributes, including K/M/G. */
static int
parse_scalar(const char *text, long double *out)
{
    char *end;
    long double value, scale = 1.0L;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    value = strtold(text, &end);
    if (end == text) {
        return 0;
    }
    if (*end == 'K' || *end == 'k') {
        scale = 1024.0L; end++;
    } else if (*end == 'M' || *end == 'm') {
        scale = 1024.0L * 1024.0L; end++;
    } else if (*end == 'G' || *end == 'g') {
        scale = 1024.0L * 1024.0L * 1024.0L; end++;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *out = value * scale;
    return 1;
}

/** @brief Tracker's permissive boolean conversion, normalized to 0/1. */
static int
parse_bool(const char *text)
{
    char *end;
    long value;

    if (text == NULL || text[0] == '\0' || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0) {
        return 0;
    }
    if (strcasecmp(text, "true") == 0 || strcasecmp(text, "on") == 0 ||
        strcasecmp(text, "yes") == 0) {
        return 1;
    }
    value = strtol(text, &end, 0);
    return (end != text && (value % 2) != 0) ? 1 : 0;
}

/** @brief Local midnight, shifted by calendar days rather than 86400 s. */
static int64_t
day_start(int64_t now, int days)
{
    time_t source = (time_t)now;
    struct tm tmv;
    time_t answer;

    if (localtime_r(&source, &tmv) == NULL) {
        return now;
    }
    tmv.tm_hour = tmv.tm_min = tmv.tm_sec = 0;
    tmv.tm_mday += days;
    tmv.tm_isdst = -1;
    answer = mktime(&tmv);
    return (answer == (time_t)-1) ? now : (int64_t)answer;
}

/** @brief Parse the absolute and relative spellings accepted by Modified. */
static int
parse_date(const char *text, int64_t now, int64_t *out)
{
    long long amount;
    char unit[16], direction[16], tail[16];
    int year, month, day, used = 0, n;
    int64_t step;

    if (text == NULL || out == NULL) {
        return 0;
    }
    if (strcasecmp(text, "now") == 0) {
        *out = now; return 1;
    }
    if (strcasecmp(text, "today") == 0) {
        *out = day_start(now, 0); return 1;
    }
    if (strcasecmp(text, "yesterday") == 0) {
        *out = day_start(now, -1); return 1;
    }
    if (strcasecmp(text, "tomorrow") == 0) {
        *out = day_start(now, 1); return 1;
    }
    n = sscanf(text, "%lld %15s %15s %15s", &amount, unit, direction,
               tail);
    step = (n >= 3) ? relative_unit(unit) : 0;
    if (n == 3 && step != 0 && strcasecmp(direction, "ago") == 0) {
        *out = now - (int64_t)amount * step; return 1;
    }
    if (n == 4 && step != 0 && strcasecmp(direction, "from") == 0 &&
        strcasecmp(tail, "now") == 0) {
        *out = now + (int64_t)amount * step; return 1;
    }
    if (sscanf(text, "%d-%d-%d%n", &year, &month, &day, &used) == 3 &&
        text[used] == '\0') {
        struct tm tmv;
        time_t answer;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = year - 1900;
        tmv.tm_mon = month - 1;
        tmv.tm_mday = day;
        tmv.tm_isdst = -1;
        answer = mktime(&tmv);
        if (answer != (time_t)-1) {
            *out = (int64_t)answer; return 1;
        }
    }
    return 0;
}

static int
is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int
lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int
tiku_query_pattern(const char *value, tiku_qop_t op, char *out,
                       size_t max)
{
    size_t o = 0;
    const char *p = value;

    if (out == NULL || max == 0u) {
        return 0;
    }
    /* Substring and suffix searches are anchored by a leading star; the
     * value itself never carries one. */
    if ((op == TIKU_OP_CONTAINS || op == TIKU_OP_ENDS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    for (; p != NULL && *p != '\0'; p++) {
        if (*p == ' ') {
            /* A space matches any run, which is why "foo bar" also finds
             * "foo-bar" and "foo.bar". */
            if (o + 1u < max) { out[o++] = '*'; }
            continue;
        }
        if (is_alpha((unsigned char)*p)) {
            /* Case-insensitivity is spelled as a class per letter rather
             * than as a flag on the comparison. */
            if (o + 4u < max) {
                out[o++] = '[';
                out[o++] = (char)lower((unsigned char)*p);
                out[o++] = (char)(lower((unsigned char)*p) - 32);
                out[o++] = ']';
            }
            continue;
        }
        if (o + 1u < max) { out[o++] = *p; }
    }
    if ((op == TIKU_OP_CONTAINS || op == TIKU_OP_BEGINS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    out[o] = '\0';
    return (int)o;
}

int
tiku_query_glob(const char *pattern, const char *name)
{
    const char *star = NULL, *retry = NULL;

    if (pattern == NULL || name == NULL) {
        return 0;
    }
    while (*name != '\0') {
        if (*pattern == '[' && strchr(pattern, ']') != NULL) {
            /* A '[' with no ']' after it is not a class -- it is a literal
             * bracket in a name.  Treating it as an unterminated class let
             * "[abc" match "a", because the scan ran to the terminator and
             * the pattern was then considered spent. */
            const char *cl = pattern + 1;
            int neg = 0, hit = 0;

            if (*cl == '!' || *cl == '^') { neg = 1; cl++; }
            for (; *cl != '\0' && *cl != ']'; cl++) {
                if (cl[1] == '-' && cl[2] != '\0' && cl[2] != ']') {
                    if ((unsigned char)*name >= (unsigned char)cl[0] &&
                        (unsigned char)*name <= (unsigned char)cl[2]) {
                        hit = 1;
                    }
                    cl += 2;
                } else if (*cl == *name) {
                    hit = 1;
                }
            }
            if (hit != neg) {
                pattern = (*cl == ']') ? cl + 1 : cl;
                name++;
                continue;
            }
        } else if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
            continue;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
            continue;
        }
        if (star != NULL) {
            /* Backtrack: the star swallows one more character. */
            pattern = star + 1;
            name = ++retry;
            continue;
        }
        return 0;
    }
    while (*pattern == '*') {
        pattern++;
    }
    return (*pattern == '\0');
}

/** @brief The text a term tests against. */
static const char *
field_text(tiku_qfield_t f, const tiku_model_t *m,
           const tiku_desc_t *d, const char *value, char *buf,
           size_t max)
{
    switch (f) {
    case TIKU_QF_PATH:  return m->path;
    case TIKU_QF_KIND:  return tiku_model_kind_string(m);
    case TIKU_QF_VALUE: return (value != NULL) ? value : "";
    case TIKU_QF_CAP:   return m->facts.req_cap;
    case TIKU_QF_UNIT:  return (d != NULL) ? d->unit_name : "";
    case TIKU_QF_SIZE:
        snprintf(buf, max, "%lld", (long long)m->facts.size);
        return buf;
    case TIKU_QF_MODIFIED:
        snprintf(buf, max, "%lld", (long long)m->facts.mtime);
        return buf;
    case TIKU_QF_WRITABLE:
        return tiku_model_is_writable(m) ? "yes" : "no";
    case TIKU_QF_TYPE:  return m->type;
    default:
        return m->name;
    }
}

/** @brief Whether a field is compared as a number rather than as text. */
static int
numeric_field(tiku_qfield_t f, const tiku_desc_t *d)
{
    if (f == TIKU_QF_SIZE) {
        return 1;
    }
    return (f == TIKU_QF_VALUE && d != NULL &&
            d->vtype != TIKU_VT_NONE && d->vtype != TIKU_VT_STR);
}

static int
term_matches(const tiku_qterm_t *t, const tiku_model_t *m,
             const tiku_desc_t *d, const char *value, int64_t now)
{
    char buf[64], pat[TIKU_QUERY_VALUE_MAX * 4 + 8];
    const char *text = field_text(t->field, m, d, value, buf, sizeof buf);

    if (!t->pattern_ready && t->field == TIKU_QF_TYPE &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        /* A type is not a string to compare: "text/" IS the type of
         * "text/x-source-code", which no text operator can say.  The other
         * operators are left as text, so "contains vnd" still works. */
        int yes = tiku_model_type_is(text, t->value);

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (!t->pattern_ready &&
        (t->field == TIKU_QF_WRITABLE ||
         (t->field == TIKU_QF_VALUE && d != NULL &&
          d->vtype == TIKU_VT_BOOL)) &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        int yes = parse_bool(text) == parse_bool(t->value);

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (t->field == TIKU_QF_MODIFIED &&
        (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS)) {
        int64_t boundary;

        if (!parse_date(t->value, now, &boundary)) {
            return 0;
        }
        return (t->op == TIKU_OP_GREATER)
                   ? m->facts.mtime > boundary : m->facts.mtime < boundary;
    }
    if (!t->pattern_ready && numeric_field(t->field, d) &&
        (t->op == TIKU_OP_IS || t->op == TIKU_OP_IS_NOT)) {
        long double a, b;
        int yes = parse_scalar(text, &a) && parse_scalar(t->value, &b) &&
                  a == b;

        return (t->op == TIKU_OP_IS) ? yes : !yes;
    }
    if (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS) {
        long double a, b;
        int text_num = parse_scalar(text, &a);
        int want_num = parse_scalar(t->value, &b);

        /* Tracker takes the type from the attribute registry and simply does
         * not offer an ordering operator on anything untyped.  Most nodes
         * here ARE untyped, so refusing would make the operator useless;
         * instead the values decide.  Both numeric compares as numbers --
         * without which "30" > "1000" is true, because '3' > '1'. */
        if (want_num && text_num) {
            return (t->op == TIKU_OP_GREATER) ? (a > b) : (a < b);
        }
        /* Asked for a number and given a name, the answer is no: a device
         * name is not greater than 1000, nor less than it. */
        if (want_num != text_num) {
            return 0;
        }
        if (numeric_field(t->field, d)) {
            return 0;
        }
        return (t->op == TIKU_OP_GREATER)
                   ? (strcmp(text, t->value) > 0)
                   : (strcmp(text, t->value) < 0);
    }
    if (t->pattern_ready) {
        snprintf(pat, sizeof pat, "%s", t->value);
    } else {
        (void)tiku_query_pattern(t->value, t->op, pat, sizeof pat);
    }
    {
        int hit = tiku_query_glob(pat, (text != NULL) ? text : "");

        return (t->op == TIKU_OP_IS_NOT) ? !hit : hit;
    }
}

int
tiku_query_matches_at(const tiku_query_t *q,
                          const tiku_model_t *m,
                          const tiku_desc_t *d, const char *value,
                          int64_t now)
{
    int i, acc = 0;

    if (q == NULL || m == NULL || q->term_count == 0) {
        return 1;
    }
    /* Terms fold left, each joining the accumulated result the way its own
     * join says -- the same shape as the panel, where the and/or control
     * sits on the row ABOVE the one it governs. */
    acc = term_matches(&q->term[0], m, d, value, now);
    for (i = 1; i < q->term_count; i++) {
        int r = term_matches(&q->term[i], m, d, value, now);

        acc = (q->term[i].join == TIKU_JOIN_OR) ? (acc || r) : (acc && r);
    }
    return acc;
}

int
tiku_query_matches(const tiku_query_t *q, const tiku_model_t *m,
                       const tiku_desc_t *d, const char *value)
{
    return tiku_query_matches_at(q, m, d, value,
                                     (int64_t)time(NULL));
}

/** @brief Case-insensitive substring without locale-dependent allocation. */
static int
contains_ci(const char *text, const char *needle)
{
    size_t n = strlen(needle);

    for (; text != NULL && *text != '\0'; text++) {
        if (strncasecmp(text, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int64_t
tiku_query_next_refresh(const tiku_query_t *q, int64_t now)
{
    time_t source = (time_t)now;
    struct tm tmv;
    int i, grain = 86400;
    time_t answer;

    if (q == NULL || !q->dynamic_date || localtime_r(&source, &tmv) == NULL) {
        return 0;
    }
    for (i = 0; i < q->term_count; i++) {
        const char *value = q->term[i].value;

        if (q->term[i].field != TIKU_QF_MODIFIED) {
            continue;
        }
        if (contains_ci(value, "second") || contains_ci(value, "minute") ||
            strcasecmp(value, "now") == 0) {
            grain = 60;
            break;
        }
        if (contains_ci(value, "hour")) {
            grain = 3600;
        }
    }
    tmv.tm_isdst = -1;
    tmv.tm_sec = 0;
    if (grain == 60) {
        tmv.tm_min++;
    } else if (grain == 3600) {
        tmv.tm_min = 0;
        tmv.tm_hour++;
    } else {
        tmv.tm_min = 0;
        tmv.tm_hour = 0;
        tmv.tm_mday++;
    }
    answer = mktime(&tmv);
    return (answer == (time_t)-1) ? now + grain : (int64_t)answer;
}

/*---------------------------------------------------------------------------*/
/* rendering                                                                 */
/*---------------------------------------------------------------------------*/

static const char *
field_name(tiku_qfield_t f)
{
    switch (f) {
    case TIKU_QF_PATH:     return "path";
    case TIKU_QF_KIND:     return "kind";
    case TIKU_QF_VALUE:    return "value";
    case TIKU_QF_SIZE:     return "size";
    case TIKU_QF_CAP:      return "cap";
    case TIKU_QF_UNIT:     return "unit";
    case TIKU_QF_WRITABLE: return "writable";
    case TIKU_QF_TYPE:     return "type";
    case TIKU_QF_MODIFIED: return "modified";
    default:                   return "name";
    }
}

int
tiku_query_predicate(const tiku_query_t *q, char *out, size_t max)
{
    int i;
    size_t o = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; q != NULL && i < q->term_count; i++) {
        const tiku_qterm_t *t = &q->term[i];
        char pat[TIKU_QUERY_VALUE_MAX * 4 + 8];
        const char *op = "==";
        char frag[TIKU_QUERY_VALUE_MAX * 4 + 64];

        if (i > 0) {
            o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s",
                                  (t->join == TIKU_JOIN_OR) ? "||" : "&&");
        }
        switch (t->op) {
        case TIKU_OP_IS_NOT:  op = "!="; break;
        case TIKU_OP_GREATER: op = ">";  break;
        case TIKU_OP_LESS:    op = "<";  break;
        default:                  op = "=="; break;
        }
        if (t->op == TIKU_OP_GREATER || t->op == TIKU_OP_LESS) {
            snprintf(frag, sizeof frag, "(%s%s%s)", field_name(t->field), op,
                     t->value);
        } else {
            /* The pattern is what is really compared, so it is what the
             * formula shows: an editable predicate that lies about what it
             * will do is worse than none. */
            if (t->pattern_ready) {
                snprintf(pat, sizeof pat, "%s", t->value);
            } else {
                (void)tiku_query_pattern(t->value, t->op, pat,
                                              sizeof pat);
            }
            snprintf(frag, sizeof frag, "(%s%s\"%s\")", field_name(t->field),
                     op, pat);
        }
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s", frag);
        if (o >= max) {
            break;
        }
    }
    return (int)strlen(out);
}

/*---------------------------------------------------------------------------*/
/* running                                                                   */
/*---------------------------------------------------------------------------*/

struct tiku_qrun {
    tiku_backend_t *backend;
    tiku_query_t    q;
    tiku_query_t    requested;      /* identities before validation   */
    char                queue[QUEUE_MAX][TIKU_PATH_MAX];
    int                 head, tail;
    tiku_model_t    single[TIKU_QUERY_SCOPES_MAX];
    int                 single_count, single_at;
    tiku_model_t   *results;
    int                 count;
    int                 visited;
    int                 done;
    /* Live: the snapshot taken at the start of a refresh, and how many of
     * the current results are new since it. */
    unsigned char      *fresh;
    int                 refreshes;
};

static void
enqueue(tiku_qrun_t *r, const char *path)
{
    int next = (r->tail + 1) % QUEUE_MAX;

    if (next == r->head) {
        return;                /* the queue is full; this subtree is cut  */
    }
    snprintf(r->queue[r->tail], TIKU_PATH_MAX, "%s", path);
    r->tail = next;
}

/** @brief Receives one entry during the walk. */
static int
visit(const tiku_model_t *m, void *ctx)
{
    tiku_qrun_t *r = ctx;
    tiku_desc_t d;
    char value[128];

    /* Do this before enqueueing: excluding only the Trash pose but walking
     * its children would still put deleted items in the result set. */
    if (!tiku_query_allows_path(&r->q, m)) {
        return 0;
    }
    if (tiku_model_is_container(m) &&
        m->kind != TIKU_KIND_SYMLINK) {
        /* Descend regardless of whether the directory itself matches: what
         * is being searched for is usually inside it.  Only REAL
         * directories are descended, as the source pushes only
         * B_DIRECTORY_NODE entries: a symlink is an entry, not a place --
         * and following links is how /proc's cycles make a depth-first
         * walk never come home. */
        enqueue(r, m->path);
    }
    if (r->count >= r->q.max_results) {
        return 1;
    }
    (void)tiku_desc_parse(m->facts.meta, &d);
    value[0] = '\0';
    /* A term on the value has to read it, which costs a round trip -- so it
     * is only paid when a term actually asks for one. */
    {
        int i, wants = 0;

        for (i = 0; i < r->q.term_count; i++) {
            if (r->q.term[i].field == TIKU_QF_VALUE) {
                wants = 1;
            }
        }
        if (wants && !tiku_model_is_container(m) &&
            r->backend->ops->read != NULL) {
            int n = r->backend->ops->read(r->backend, m->path, value,
                                          sizeof value - 1u);
            if (n > 0) {
                value[n] = '\0';
                while (n > 0 && (value[n - 1] == '\n' ||
                                 value[n - 1] == '\r')) {
                    value[--n] = '\0';
                }
            }
        }
    }
    if (tiku_query_in_scope(&r->q, m->path) &&
        tiku_query_matches(&r->q, m, &d, value)) {
        r->results[r->count++] = *m;
    }
    return 0;
}

tiku_qrun_t *
tiku_query_start(tiku_backend_t *b, const tiku_query_t *q)
{
    tiku_qrun_t *r;
    int i, requested_scopes;

    if (b == NULL || q == NULL || q->invalid) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->results = calloc(RESULTS_MAX, sizeof *r->results);
    if (r->results == NULL) {
        free(r);
        return NULL;
    }
    r->backend = b;
    r->q = *q;
    r->requested = *q;
    requested_scopes = r->q.scope_count;
    if (requested_scopes > 0) {
        int kept = 0;

        for (i = 0; i < requested_scopes; i++) {
            if (!scope_available(&r->q, i, b)) {
                continue;
            }
            if (kept != i) {
                memcpy(r->q.scope[kept], r->q.scope[i],
                       sizeof r->q.scope[kept]);
                r->q.scope_was[kept] = r->q.scope_was[i];
            }
            kept++;
        }
        r->q.scope_count = kept;
    }
    if (r->q.max_results <= 0 || r->q.max_results > RESULTS_MAX) {
        r->q.max_results = RESULTS_MAX;
    }
    /* Walking starts at the scopes when there are any, so a scoped search
     * does not read the whole namespace and then throw most of it away. */
    if (r->q.scope_count > 0) {
        for (i = 0; i < r->q.scope_count; i++) {
            char start[TIKU_PATH_MAX];
            size_t n;
            tiku_model_t m;

            snprintf(start, sizeof start, "%s", r->q.scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            /* A query scope may name a file, not only a directory.  Keep
             * that one node as a pending visit instead of feeding it to
             * list(), whose opendir failure otherwise makes the result
             * silently empty (PVN-070). */
            if (b->ops->stat != NULL && b->ops->stat(b, start, &m) == 0 &&
                !tiku_model_is_container(&m)) {
                if (r->single_count < TIKU_QUERY_SCOPES_MAX) {
                    r->single[r->single_count++] = m;
                }
            } else {
                enqueue(r, start);
            }
        }
    } else if (requested_scopes == 0) {
        enqueue(r, "/");
    } else {
        /* Every saved target is absent or now names a leaf.  Searching the
         * whole namespace here would silently turn a narrow query broad. */
        r->done = 1;
    }
    return r;
}

void
tiku_query_stop(tiku_qrun_t *r)
{
    if (r != NULL) {
        free(r->fresh);
        free(r->results);
        free(r);
    }
}

int
tiku_query_step(tiku_qrun_t *r, int budget)
{
    int before;

    if (r == NULL || r->done) {
        return -1;
    }
    before = r->count;
    while (budget-- > 0 && r->single_at < r->single_count) {
        (void)visit(&r->single[r->single_at++], r);
        if (r->count >= r->q.max_results) {
            break;
        }
    }
    while (budget-- > 0 && r->head != r->tail) {
        char dir[TIKU_PATH_MAX];

        /* The most recently found directory first: a STACK, so the walk
         * descends before it moves on, in the source's depth-first order
         * (PVN-069).  The no-snapshot semantics are unchanged -- a folder
         * already passed still misses later creations. */
        r->tail = (r->tail + QUEUE_MAX - 1) % QUEUE_MAX;
        snprintf(dir, sizeof dir, "%s", r->queue[r->tail]);
        r->visited++;
        (void)r->backend->ops->list(r->backend, dir, visit, r);
        if (r->count >= r->q.max_results) {
            break;
        }
    }
    if ((r->head == r->tail && r->single_at >= r->single_count) ||
        r->count >= r->q.max_results) {
        r->done = 1;
    }
    return r->count - before;
}

int
tiku_query_count(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->count : 0;
}

const tiku_model_t *
tiku_query_at(const tiku_qrun_t *r, int i)
{
    if (r == NULL || i < 0 || i >= r->count) {
        return NULL;
    }
    return &r->results[i];
}

int
tiku_query_is_new(const tiku_qrun_t *r, int i)
{
    if (r == NULL || r->fresh == NULL || i < 0 || i >= r->count) {
        return 0;
    }
    return r->fresh[i];
}

int
tiku_query_refresh(tiku_qrun_t *r, tiku_qdiff_t *out)
{
    tiku_model_t *old;
    unsigned char *seen;
    int old_count, i, j;

    if (r == NULL) {
        return -1;
    }
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    /* Snapshot, then re-walk.  The view is NOT cleared: a result that is
     * still there keeps its row, so nothing flickers and no selection is
     * lost merely because a value moved. */
    old = calloc((size_t)(r->count > 0 ? r->count : 1), sizeof *old);
    seen = calloc((size_t)(r->count > 0 ? r->count : 1), 1);
    if (old == NULL || seen == NULL) {
        free(old);
        free(seen);
        return -1;
    }
    old_count = r->count;
    for (i = 0; i < old_count; i++) {
        old[i] = r->results[i];
    }

    r->count = 0;
    r->visited = 0;
    r->head = r->tail = 0;
    r->single_count = r->single_at = 0;
    r->done = 0;
    if (r->q.scope_count > 0) {
        for (i = 0; i < r->q.scope_count; i++) {
            char start[TIKU_PATH_MAX];
            size_t n;
            tiku_model_t m;

            snprintf(start, sizeof start, "%s", r->q.scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            if (r->backend->ops->stat != NULL &&
                r->backend->ops->stat(r->backend, start, &m) == 0 &&
                !tiku_model_is_container(&m)) {
                if (r->single_count < TIKU_QUERY_SCOPES_MAX) {
                    r->single[r->single_count++] = m;
                }
            } else {
                enqueue(r, start);
            }
        }
    } else {
        enqueue(r, "/");
    }
    while (!r->done) {
        if (tiku_query_step(r, 16) < 0) {
            break;
        }
    }

    if (r->fresh != NULL) {
        free(r->fresh);
    }
    r->fresh = calloc((size_t)(r->count > 0 ? r->count : 1), 1);

    /* Mark survivors in the snapshot as they are re-found.  Leaving them
     * marked unseen is the hazard the original carries: everything still in
     * the snapshot at the end is treated as gone, so a result that never
     * left would be deleted and re-added on every refresh. */
    for (i = 0; i < r->count; i++) {
        int survived = 0;

        for (j = 0; j < old_count; j++) {
            if (!seen[j] && strcmp(old[j].path, r->results[i].path) == 0) {
                seen[j] = 1;
                survived = 1;
                break;
            }
        }
        if (!survived) {
            if (r->fresh != NULL) {
                r->fresh[i] = 1;
            }
            if (out != NULL) {
                out->added++;
            }
        }
    }
    for (j = 0; j < old_count; j++) {
        tiku_model_t probe;

        if (seen[j]) {
            continue;
        }
        if (out != NULL) {
            out->removed++;
            /* Why it left is only knowable now: ask whether the node is
             * still there at all.  Afterwards the two cases are
             * indistinguishable. */
            if (r->backend->ops->stat == NULL ||
                r->backend->ops->stat(r->backend, old[j].path, &probe) != 0) {
                out->vanished++;
            }
        }
    }
    r->refreshes++;
    free(old);
    free(seen);
    return 0;
}

int
tiku_query_visited(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->visited : 0;
}

int
tiku_query_done(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->done : 1;
}

int
tiku_query_targets_device(const tiku_qrun_t *r, uint64_t dev)
{
    int i;

    if (r == NULL || dev == 0u) {
        return 0;
    }
    if (r->requested.scope_count == 0) {
        return 1;               /* the whole namespace includes every disk */
    }
    for (i = 0; i < r->requested.scope_count; i++) {
        if (r->requested.scope_was[i].dev == dev) {
            return 1;
        }
    }
    return 0;
}

const char *
tiku_query_search_type(const tiku_qrun_t *r)
{
    return (r != NULL) ? r->requested.type : "";
}

/*---------------------------------------------------------------------------*/
/* Saved queries                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serialise a query to one line per field.
 *
 * Text rather than a struct dump: a saved query outlives the build that
 * wrote it, and a layout change must not silently reinterpret an old one.
 */
static int
serialise(const tiku_query_t *q, char *out, size_t max)
{
    size_t o = 0;
    int i;

    /* v2 added what a record has to carry to be re-openable and sweepable;
     * v3 adds formula patterns and Include Trash; v4 adds dynamic dates:
     * what it IS, whether anyone meant to keep it, when it was written, and
     * what its scopes were.  A v1 record still reads -- the fields it does
     * not carry simply keep their defaults. */
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "v4\nmode %d\nkind %d %d %lld\n", (int)q->mode,
                          (int)q->kind, q->temporary,
                          (long long)q->saved_at);
    /* Persist the choice, not this machine's resolved Trash path. */
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "o %d\n", q->include_trash);
    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "r %d\n", q->dynamic_date);
    if (q->type[0] != '\0') {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "y %s\n", q->type);
    }
    for (i = 0; i < q->term_count; i++) {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "t %d %d %d %d %s\n", (int)q->term[i].field,
                              (int)q->term[i].op, (int)q->term[i].join,
                              q->term[i].pattern_ready,
                              q->term[i].value);
    }
    for (i = 0; i < q->scope_count; i++) {
        /* The identity goes on the SAME line as the path, so a reader that
         * only understands "s <path>" still gets the path. */
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "s %s\n", q->scope[i]);
        if (q->scope_was[i].dev != 0u || q->scope_was[i].volume[0] != '\0') {
            o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                                  "d %llu %s\n",
                                  (unsigned long long)q->scope_was[i].dev,
                                  q->scope_was[i].volume);
        }
    }
    return (o < max) ? (int)o : -1;
}

static int
deserialise(tiku_query_t *q, const char *in)
{
    const char *p = in;

    tiku_query_init(q);
    if (in == NULL ||
        (strncmp(in, "v1\n", 3) != 0 && strncmp(in, "v2\n", 3) != 0 &&
         strncmp(in, "v3\n", 3) != 0 && strncmp(in, "v4\n", 3) != 0)) {
        return -1;              /* an unknown version is not guessed at   */
    }
    p += 3;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        char line[TIKU_PATH_MAX + 64];
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (n >= sizeof line) {
            n = sizeof line - 1u;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        if (strncmp(line, "mode ", 5) == 0) {
            q->mode = (tiku_qmode_t)atoi(line + 5);
        } else if (line[0] == 't' && line[1] == ' ') {
            int f = 0, op = 0, j = 0, ready = 0, used = 0;

            if (in[1] >= '3' && sscanf(line + 2, "%d %d %d %d %n", &f,
                                       &op, &j, &ready, &used) >= 4) {
                int at = tiku_query_add_term(q, (tiku_qfield_t)f,
                           (tiku_qop_t)op, line + 2 + used,
                           (tiku_qjoin_t)j);
                if (at >= 0) { q->term[at].pattern_ready = ready; }
            } else if (sscanf(line + 2, "%d %d %d %n", &f, &op, &j,
                              &used) >= 3) {
                (void)tiku_query_add_term(q, (tiku_qfield_t)f,
                    (tiku_qop_t)op, line + 2 + used,
                    (tiku_qjoin_t)j);
            }
        } else if (strncmp(line, "kind ", 5) == 0) {
            int k = 0, t = 0;
            long long at = 0;

            if (sscanf(line + 5, "%d %d %lld", &k, &t, &at) >= 2) {
                q->kind = (tiku_qrec_t)k;
                q->temporary = t;
                q->saved_at = (int64_t)at;
            }
        } else if (line[0] == 'y' && line[1] == ' ') {
            snprintf(q->type, sizeof q->type, "%.*s",
                     (int)(sizeof q->type - 1u), line + 2);
        } else if (line[0] == 'o' && line[1] == ' ') {
            int include = 0, used = 0;

            if (sscanf(line + 2, "%d %n", &include, &used) >= 1) {
                q->include_trash = include ? 1 : 0;
                snprintf(q->trash_path, sizeof q->trash_path, "%.*s",
                         (int)(sizeof q->trash_path - 1u), line + 2 + used);
            }
        } else if (line[0] == 'r' && line[1] == ' ') {
            q->dynamic_date = atoi(line + 2) ? 1 : 0;
        } else if (line[0] == 's' && line[1] == ' ') {
            (void)tiku_query_add_scope(q, line + 2);
        } else if (line[0] == 'd' && line[1] == ' ') {
            /* Belongs to the scope just read: the writer emits it directly
             * after, and a stray one with no scope before it is dropped
             * rather than attached to the wrong place. */
            unsigned long long dev = 0ull;
            int used = 0;

            if (q->scope_count <= 0) {
                q->dropped++;   /* nothing for it to describe */
            } else if (sscanf(line + 2, "%llu %n", &dev, &used) >= 1) {
                tiku_qscope_t *sc = &q->scope_was[q->scope_count - 1];

                sc->dev = (uint64_t)dev;
                memcpy(sc->path, q->scope[q->scope_count - 1],
                       sizeof sc->path);
                snprintf(sc->volume, sizeof sc->volume, "%.*s",
                         (int)(sizeof sc->volume - 1u), line + 2 + used);
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

int
tiku_query_serialise(const tiku_query_t *q, char *buf, size_t max)
{
    if (q == NULL || buf == NULL) {
        return -1;
    }
    return serialise(q, buf, max);
}

int
tiku_query_save(const tiku_query_t *q, const char *name,
                    struct tiku_store *store)
{
    char buf[TIKU_QUERY_PRED_MAX * 2];
    char index[2048];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    if (serialise(q, buf, sizeof buf) < 0) {
        return -1;
    }
    if (tiku_state_write(store, name, TIKU_QUERY_ATTR, buf,
                             strlen(buf)) != 0) {
        return -1;
    }
    /* The index is what makes saved queries findable; without it a name has
     * to be remembered to be re-opened. */
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        n = 0;
    }
    index[n] = '\0';
    if (strstr(index, name) == NULL) {
        /* Newest first, so the list reads as a history. */
        char merged[2048];

        /* A name is bounded so the oldest entries fall off the end of the
         * index rather than the newest failing to be written. */
        snprintf(merged, sizeof merged, "%.127s\n%.1900s", name, index);
        (void)tiku_state_write(store, TIKU_QUERY_STORE_NODE,
                                   TIKU_QUERY_INDEX_ATTR, merged,
                                   strlen(merged));
    }
    return 0;
}

int
tiku_query_load(tiku_query_t *q, const char *name,
                    struct tiku_store *store)
{
    char buf[TIKU_QUERY_PRED_MAX * 2];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_state_read(store, name, TIKU_QUERY_ATTR, buf,
                            sizeof buf - 1u);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return -1;              /* emptied: the record has been reclaimed */
    }
    buf[n] = '\0';
    return deserialise(q, buf);
}

int
tiku_query_list(struct tiku_store *store, char names[][128], int max)
{
    char index[2048];
    const char *p;
    int n, count = 0;

    if (store == NULL || names == NULL) {
        return 0;
    }
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return 0;
    }
    index[n] = '\0';
    for (p = index; *p != '\0' && count < max; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u) {
            if (len > 127u) {
                len = 127u;
            }
            memcpy(names[count], p, len);
            names[count][len] = '\0';
            count++;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return count;
}

int
tiku_query_list_kind(struct tiku_store *store, tiku_qrec_t kind,
                         char names[][128], int max)
{
    char all[TIKU_QUERY_LIST_MAX][128];
    int n, i, count = 0;

    if (store == NULL || names == NULL || max <= 0) {
        return 0;
    }
    n = tiku_query_list(store, all, TIKU_QUERY_LIST_MAX);
    for (i = 0; i < n && count < max; i++) {
        tiku_query_t q;

        if (tiku_query_load(&q, all[i], store) != 0) {
            /* Indexed but not readable: the index is a list of names, and
             * a name whose record has gone is not one of this kind or of
             * any other. */
            continue;
        }
        if (q.kind != kind) {
            continue;
        }
        snprintf(names[count], 128, "%s", all[i]);
        count++;
    }
    return count;
}

int
tiku_query_sweep(struct tiku_store *store, int64_t now,
                     int64_t up_for, const char *const *open, int nopen)
{
    char all[TIKU_QUERY_LIST_MAX][128];
    int n, i, k, went = 0;

    if (store == NULL) {
        return -1;
    }
    if (up_for < TIKU_QUERY_SWEEP_WAIT) {
        /* Not yet.  Nothing here is urgent, and a machine that has just
         * started has better things to do than delete week-old records. */
        return -1;
    }
    n = tiku_query_list(store, all, TIKU_QUERY_LIST_MAX);
    for (i = 0; i < n; i++) {
        tiku_query_t q;
        int shown = 0;

        if (tiku_query_load(&q, all[i], store) != 0) {
            continue;
        }
        if (!q.temporary || q.kind == TIKU_QREC_TEMPLATE) {
            continue;           /* named, or a template: kept */
        }
        if (q.saved_at == 0 || now - q.saved_at < TIKU_QUERY_STALE_SEC) {
            continue;           /* young enough to still be wanted */
        }
        for (k = 0; k < nopen; k++) {
            if (open != NULL && open[k] != NULL &&
                strcmp(open[k], all[i]) == 0) {
                shown = 1;
                break;
            }
        }
        if (shown) {
            /* A window is showing it.  Deleting the record under an open
             * window would leave the window unable to say what it is. */
            continue;
        }
        if (tiku_query_forget(all[i], store) == 0) {
            went++;
        }
    }
    return went;
}

int
tiku_query_forget(const char *name, struct tiku_store *store)
{
    char index[2048], merged[2048];
    const char *p;
    size_t o = 0;
    int n;

    if (name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_state_read(store, TIKU_QUERY_STORE_NODE,
                            TIKU_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return -1;
    }
    index[n] = '\0';
    merged[0] = '\0';
    for (p = index; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u && (len != strlen(name) ||
                         strncmp(p, name, len) != 0)) {
            o += (size_t)snprintf(merged + o,
                                  (o < sizeof merged) ? sizeof merged - o : 0u,
                                  "%.*s\n", (int)len, p);
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    /* The RECORD goes too, not only its line in the index: a name dropped
     * from the index while its fields stay behind is a query that cannot
     * be found and cannot be reclaimed either (Q-037). */
    (void)tiku_state_write(store, name, TIKU_QUERY_ATTR, "", 0);
    return tiku_state_write(store, TIKU_QUERY_STORE_NODE,
                                TIKU_QUERY_INDEX_ATTR, merged,
                                strlen(merged));
}
