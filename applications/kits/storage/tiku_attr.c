/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_attr.c - typed descriptors, formatting and validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_attr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/** @brief Copy one comma-separated field, stopping at ',' or end. */
static const char *
field(const char *p, char *out, size_t max)
{
    size_t n = 0;

    while (*p != '\0' && *p != ',') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return (*p == ',') ? p + 1 : p;
}

int
tiku_desc_parse(const char *meta, tiku_desc_t *out)
{
    char tok[24];
    const char *p;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    if (meta == NULL || meta[0] == '\0' || strcmp(meta, "-") == 0) {
        return 0;
    }
    p = field(meta, tok, sizeof tok);
    if (strcmp(tok, "bool") == 0)      { out->vtype = TIKU_VT_BOOL; }
    else if (strcmp(tok, "u32") == 0)  { out->vtype = TIKU_VT_U32; }
    else if (strcmp(tok, "i32") == 0)  { out->vtype = TIKU_VT_I32; }
    else if (strcmp(tok, "str") == 0)  { out->vtype = TIKU_VT_STR; }
    else                               { return 0; }

    p = field(p, out->unit_name, sizeof out->unit_name);
    if (strcmp(out->unit_name, "bool") == 0)       { out->unit = TIKU_UNIT_BOOL; }
    else if (strcmp(out->unit_name, "bytes") == 0) { out->unit = TIKU_UNIT_BYTES; }
    else if (strcmp(out->unit_name, "Hz") == 0)    { out->unit = TIKU_UNIT_HZ; }
    else if (strcmp(out->unit_name, "s") == 0)     { out->unit = TIKU_UNIT_SECONDS; }
    else if (strcmp(out->unit_name, "count") == 0) { out->unit = TIKU_UNIT_COUNT; }
    else if (out->unit_name[0] == '\0' ||
             strcmp(out->unit_name, "-") == 0)     { out->unit = TIKU_UNIT_NONE; }
    else                                           { out->unit = TIKU_UNIT_OTHER; }

    p = field(p, tok, sizeof tok);
    if (strcmp(tok, "static") == 0)      { out->fresh = TIKU_FRESH_STATIC; }
    else if (strcmp(tok, "cached") == 0) { out->fresh = TIKU_FRESH_CACHED; }
    else if (strcmp(tok, "live") == 0)   { out->fresh = TIKU_FRESH_LIVE; }

    p = field(p, out->cost, sizeof out->cost);

    /* The range is optional and comes last as "lo..hi". */
    if (*p != '\0') {
        char rng[32];
        char *dots;

        (void)field(p, rng, sizeof rng);
        dots = strstr(rng, "..");
        if (dots != NULL) {
            *dots = '\0';
            out->lo = strtol(rng, NULL, 0);
            out->hi = strtol(dots + 2, NULL, 0);
            out->has_range = 1;
        }
    }
    return 1;
}

/** @brief Read @p raw as a number.  @return 1 when it is one. */
static int
as_long(const char *raw, long *v)
{
    char *end;

    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    *v = strtol(raw, &end, 0);
    while (*end == ' ' || *end == '\r' || *end == '\n') {
        end++;
    }
    return (*end == '\0');
}

/**
 * @brief Bytes, with the binary prefixes and one decimal above the first.
 *
 * Under a kibibyte the count is exact and carries its unit word, because
 * "512 bytes" is worth more than "0.5 KiB" at that size.
 */
static void
fmt_bytes(long v, char *out, size_t max)
{
    static const char *suffix[] = { "KiB", "MiB", "GiB", "TiB" };
    double d = (double)v;
    int i = -1;

    if (v < 1024) {
        snprintf(out, max, "%ld byte%s", v, (v == 1) ? "" : "s");
        return;
    }
    while (d >= 1024.0 && i < 3) {
        d /= 1024.0;
        i++;
    }
    snprintf(out, max, "%.1f %s", d, suffix[i]);
}

/** @brief An interval, dropping the smallest part first as room runs out. */
static void
fmt_seconds(long v, char *out, size_t max)
{
    /* Split the MAGNITUDE and carry the sign separately: dividing a negative
     * total truncates each part toward zero, so -3600 comes out as every
     * field zero and reads as "0:00" -- an hour ago shown as now. */
    const char *sign = (v < 0) ? "-" : "";
    long a = (v < 0) ? -v : v;
    long h = a / 3600, m = (a % 3600) / 60, s = a % 60;

    /* Hours accumulate rather than rolling into days: a duration is one
     * span, and a reader comparing two of them compares the leading
     * number.  Under the hour the hours field goes rather than sit at
     * zero (MA-035). */
    if (h > 0) {
        snprintf(out, max, "%s%ld:%02ld:%02ld", sign, h, m, s);
    } else {
        snprintf(out, max, "%s%ld:%02ld", sign, m, s);
    }
}

static void
fmt_hz(long v, char *out, size_t max)
{
    if (v >= 1000000) {
        snprintf(out, max, "%.6g MHz", (double)v / 1000000.0);
    } else if (v >= 1000) {
        snprintf(out, max, "%.6g kHz", (double)v / 1000.0);
    } else {
        snprintf(out, max, "%ld Hz", v);
    }
}

/** @brief Thousands separated, so a seven-digit count is readable at a glance. */
static void
fmt_count(long v, char *out, size_t max)
{
    char digits[24];
    size_t n, i, o = 0;
    int neg = (v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

    n = (size_t)snprintf(digits, sizeof digits, "%lu", u);
    if (neg && o + 1u < max) {
        out[o++] = '-';
    }
    for (i = 0; i < n; i++) {
        if (i > 0 && ((n - i) % 3u) == 0u && o + 1u < max) {
            out[o++] = ' ';
        }
        if (o + 1u < max) {
            out[o++] = digits[i];
        }
    }
    out[o] = '\0';
}

int
tiku_attr_format(const tiku_desc_t *d, const char *raw,
                     char *out, size_t max)
{
    long v;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (raw == NULL || raw[0] == '\0') {
        /* A node that has not been read yet is not a node whose value is
         * empty; the two must not look the same. */
        snprintf(out, max, "-");
        return (int)strlen(out);
    }
    if (d == NULL || d->vtype == TIKU_VT_NONE ||
        d->vtype == TIKU_VT_STR || !as_long(raw, &v)) {
        snprintf(out, max, "%s", raw);
        return (int)strlen(out);
    }
    switch (d->unit) {
    case TIKU_UNIT_BOOL:
        snprintf(out, max, "%s", (v != 0) ? "on" : "off");
        break;
    case TIKU_UNIT_BYTES:
        fmt_bytes(v, out, max);
        break;
    case TIKU_UNIT_HZ:
        fmt_hz(v, out, max);
        break;
    case TIKU_UNIT_SECONDS:
        fmt_seconds(v, out, max);
        break;
    case TIKU_UNIT_COUNT:
        fmt_count(v, out, max);
        break;
    default:
        if (d->vtype == TIKU_VT_BOOL) {
            snprintf(out, max, "%s", (v != 0) ? "on" : "off");
        } else if (d->unit_name[0] != '\0' &&
                   strcmp(d->unit_name, "-") != 0) {
            /* A unit we have no formatter for is still worth showing: the
             * reading means more with its unit than without. */
            snprintf(out, max, "%ld %s", v, d->unit_name);
        } else {
            snprintf(out, max, "%ld", v);
        }
        break;
    }
    return (int)strlen(out);
}

int
tiku_attr_editable(const tiku_model_t *m, const tiku_desc_t *d)
{
    (void)d;
    if (m == NULL) {
        return 0;
    }
    if (tiku_model_is_container(m)) {
        return 0;
    }
    return tiku_model_is_writable(m);
}

/** @brief Read a boolean spelling.  @return -1 when it is not one. */
static int
as_bool(const char *t)
{
    if (strcmp(t, "1") == 0 || strcmp(t, "on") == 0 ||
        strcmp(t, "true") == 0 || strcmp(t, "yes") == 0) {
        return 1;
    }
    if (strcmp(t, "0") == 0 || strcmp(t, "off") == 0 ||
        strcmp(t, "false") == 0 || strcmp(t, "no") == 0) {
        return 0;
    }
    return -1;
}

/**
 * @brief Read a scalar, accepting a k/m/g suffix.
 *
 * Typing "64k" into a byte field is how the size is spoken; the original
 * accepts it and so does this.  The multipliers are binary, matching the
 * prefixes the values are displayed with.
 *
 * @return 1 when the whole string was consumed.
 */
static int
parse_scalar(const char *s, long *v)
{
    char *end;
    long mul = 1;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    *v = strtol(s, &end, 0);
    if (end == s) {
        return 0;
    }
    switch (*end) {
    case 'k': case 'K': mul = 1024L;                 end++; break;
    case 'm': case 'M': mul = 1024L * 1024L;         end++; break;
    case 'g': case 'G': mul = 1024L * 1024L * 1024L; end++; break;
    default: break;
    }
    if (mul != 1 && (*end == 'b' || *end == 'B')) {
        end++;                    /* "64kb" as readily as "64k"           */
    }
    while (*end == ' ' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *v *= mul;
    return 1;
}

int
tiku_attr_validate(const tiku_desc_t *d, const char *text, char *err,
                       size_t errmax)
{
    long v;

    if (err != NULL && errmax > 0u) {
        err[0] = '\0';
    }
    if (text == NULL || text[0] == '\0') {
        /* A boolean reads an empty field as false -- clearing the box IS
         * the input -- where every other type has nothing to send. */
        if (d != NULL && d->vtype == TIKU_VT_BOOL) {
            return 1;
        }
        snprintf(err, errmax, "nothing to write");
        return 0;
    }
    if (d == NULL || d->vtype == TIKU_VT_NONE ||
        d->vtype == TIKU_VT_STR) {
        return 1;                 /* untyped: the device decides           */
    }
    if (d->vtype == TIKU_VT_BOOL) {
        if (as_bool(text) < 0) {
            snprintf(err, errmax, "\"%s\" is not on or off", text);
            return 0;
        }
        return 1;
    }
    if (!parse_scalar(text, &v)) {
        snprintf(err, errmax, "\"%s\" is not a number", text);
        return 0;
    }
    if (d->vtype == TIKU_VT_U32 && v < 0) {
        snprintf(err, errmax, "%ld is negative; this value cannot be", v);
        return 0;
    }
    /* A declared range is checked HERE rather than at the device, so a
     * refusal names the range instead of arriving as a bare error. */
    if (d->has_range && (v < d->lo || v > d->hi)) {
        snprintf(err, errmax, "%ld is outside %ld..%ld", v, d->lo, d->hi);
        return 0;
    }
    return 1;
}

int
tiku_attr_canonical(const tiku_desc_t *d, const char *text, char *out,
                        size_t max)
{
    if (out == NULL || max == 0u) {
        return 0;
    }
    if (d != NULL && d->vtype == TIKU_VT_BOOL) {
        int b = as_bool(text);

        snprintf(out, max, "%d", (b > 0) ? 1 : 0);
        return (int)strlen(out);
    }
    if (d != NULL && (d->vtype == TIKU_VT_U32 ||
                      d->vtype == TIKU_VT_I32)) {
        long v;

        /* A suffix is a convenience for typing, not something to send: the
         * device is given the number it means. */
        if (parse_scalar(text, &v)) {
            snprintf(out, max, "%ld", v);
            return (int)strlen(out);
        }
    }
    snprintf(out, max, "%s", (text != NULL) ? text : "");
    return (int)strlen(out);
}

int
tiku_attr_size_fit(int64_t bytes, int width,
                       int (*measure)(const char *, void *), void *ctx,
                       char *out, size_t max)
{
    static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double n = (double)bytes;
    int u = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (bytes < 0) {
        /* Not "0 bytes": an unknown size and an empty file are different
         * facts and a listing must not spell them the same way. */
        snprintf(out, max, "-");
        return (int)strlen(out);
    }
    if (bytes < 1024) {
        snprintf(out, max, "%lld B", (long long)bytes);
        return (int)strlen(out);
    }
    while (n >= 1024.0 && u < (int)(sizeof unit / sizeof unit[0]) - 1) {
        n /= 1024.0;
        u++;
    }
    snprintf(out, max, "%.2f %s", n, unit[u]);
    {   /* An insignificant trailing zero in the second decimal goes, so a
         * round number reads 1.0 KiB rather than 1.00 KiB. */
        char *dot = strchr(out, '.');

        if (dot != NULL && dot[1] != '\0' && dot[2] == '0') {
            memmove(dot + 2, dot + 3, strlen(dot + 3) + 1u);
        }
    }
    if (measure != NULL && measure(out, ctx) > width) {
        /* Too narrow for the formatted form: the plain byte count is
         * shorter for small values and honest for large ones. */
        char plain[40];

        snprintf(plain, sizeof plain, "%lld", (long long)bytes);
        if (measure(plain, ctx) <= width) {
            snprintf(out, max, "%s", plain);
        } else {
            return tiku_text_fit(out, width, TIKU_TRUNC_END, measure,
                                     ctx, out, max);
        }
    }
    return (int)strlen(out);
}

#define ELLIPSIS "\xe2\x80\xa6"     /* U+2026, one character            */

int
tiku_text_fit(const char *in, int width, tiku_trunc_t how,
                  int (*measure)(const char *, void *), void *ctx, char *out,
                  size_t max)
{
    size_t n;
    int keep;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (in == NULL) {
        out[0] = '\0';
        return 0;
    }
    snprintf(out, max, "%s", in);
    if (measure == NULL || measure(out, ctx) <= width) {
        return (int)strlen(out);
    }
    n = strlen(out);
    if (how == TIKU_TRUNC_END) {
        while (n > 0u) {
            char try[256];

            snprintf(try, sizeof try, "%.*s" ELLIPSIS, (int)--n, in);
            if (measure(try, ctx) <= width) {
                snprintf(out, max, "%s", try);
                return (int)strlen(out);
            }
        }
        out[0] = '\0';
        return 0;
    }
    if (how == TIKU_TRUNC_BEGIN) {
        /* From the BEGINNING: what survives is the tail, which for a
         * string being typed is the part still changing (TS-041). */
        size_t skip;

        for (skip = 1u; skip < n; skip++) {
            char try[256];

            snprintf(try, sizeof try, ELLIPSIS "%s", in + skip);
            if (measure(try, ctx) <= width) {
                snprintf(out, max, "%s", try);
                return (int)strlen(out);
            }
        }
        snprintf(out, max, ELLIPSIS);
        return (int)strlen(out);
    }
    /* Middle: give up a character from each side in turn, so the head and
     * the tail shrink together and both stay legible. */
    for (keep = (int)n - 1; keep > 0; keep--) {
        char try[256];
        int head = (keep + 1) / 2;
        int tail = keep - head;

        snprintf(try, sizeof try, "%.*s" ELLIPSIS "%s", head, in,
                 in + (int)n - tail);
        if (measure(try, ctx) <= width) {
            snprintf(out, max, "%s", try);
            return (int)strlen(out);
        }
    }
    snprintf(out, max, ELLIPSIS);
    return (int)strlen(out);
}

int
tiku_attr_compare(const tiku_desc_t *d, const char *a, const char *b)
{
    long x, y;

    if (a == NULL) { a = ""; }
    if (b == NULL) { b = ""; }
    /* An entry that has no value sorts LAST whichever way the column is
     * ordered -- it is not "the smallest value", it is the absence of one,
     * and letting the empty string compare as a value puts a column of
     * unread nodes above everything that has been read. */
    if (a[0] == '\0' || b[0] == '\0') {
        if (a[0] == '\0' && b[0] == '\0') {
            return 0;
        }
        return (a[0] == '\0') ? 1 : -1;
    }
    /* Numbers compare as numbers whatever the column shows: ordering by the
     * formatted text would sort "9 bytes" after "10.0 KiB". */
    if (d != NULL && d->vtype != TIKU_VT_NONE &&
        d->vtype != TIKU_VT_STR && as_long(a, &x) && as_long(b, &y)) {
        return (x < y) ? -1 : ((x > y) ? 1 : 0);
    }
    /* Text compares case-insensitively, as every other name comparison in
     * this port does: a column that sorted "Zebra" before "apple" would be
     * the only place capitals mattered. */
    return strcasecmp(a, b);
}

/*---------------------------------------------------------------------------*/
/* dates: the longest form that fits (MA-025)                                */
/*---------------------------------------------------------------------------*/

/** @brief One rung of the ladder: a date form and a time form. */
typedef struct {
    const char *date;
    const char *time;
} date_form_t;

int
tiku_attr_time_fit(int64_t when, int width,
                       int (*measure)(const char *, void *), void *ctx,
                       char *out, size_t max)
{
    /* Longest first, exactly as TruncTimeBase orders them: long date with
     * seconds, then without, then a shorter date, then the shortest. */
    static const date_form_t forms[] = {
        { "%B %e, %Y", "%l:%M:%S %p" },
        { "%B %e, %Y", "%l:%M %p"    },
        { "%b %e, %Y", "%l:%M %p"    },
        { "%m/%d/%y",  "%l:%M %p"    },
    };
    time_t t = (time_t)when;
    struct tm tmv;
    size_t i;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (when <= 0) {
        /* No wall clock behind this store.  Saying so beats printing a date
         * in 1970 that the user would read as a fact. */
        snprintf(out, max, "-");
        return (int)strlen(out);
    }
    if (localtime_r(&t, &tmv) == NULL) {
        snprintf(out, max, "-");
        return (int)strlen(out);
    }
    for (i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        char date[64], time_s[64], both[160];

        (void)strftime(date, sizeof date, forms[i].date, &tmv);
        (void)strftime(time_s, sizeof time_s, forms[i].time, &tmv);
        {   /* %e and %l pad with a space; a column of dates reads better
             * without the hole, and the width measurement should not pay
             * for a character that is not information. */
            char *p2 = date;
            while (*p2 == ' ') { p2++; }
            snprintf(both, sizeof both, "%s, %s", p2,
                     (time_s[0] == ' ') ? time_s + 1 : time_s);
        }
        if (measure == NULL || measure(both, ctx) <= width) {
            snprintf(out, max, "%s", both);
            return (int)strlen(out);
        }
    }
    {   /* Nothing fit: the date on its own, and failing that, elided. */
        char date[64];

        (void)strftime(date, sizeof date, "%m/%d/%y", &tmv);
        if (measure == NULL || measure(date, ctx) <= width) {
            snprintf(out, max, "%s", date);
            return (int)strlen(out);
        }
        return tiku_text_fit(date, width, TIKU_TRUNC_MIDDLE, measure,
                                 ctx, out, max);
    }
}

/*---------------------------------------------------------------------------*/
/* gauges (MA-037)                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Fill @p cells cells for @p value in 0..@p vmax. */
static int
gauge_cells(long value, long vmax, int cells, char *out, size_t max)
{
    long steps;
    int i;
    size_t at = 0;

    if (out == NULL || max == 0u || cells <= 0 || vmax <= 0) {
        return 0;
    }
    if (value > vmax) { value = vmax; }
    if (value < 0)    { value = 0; }
    steps = vmax / cells;
    if (steps <= 0) { steps = 1; }
    for (i = 0; i < cells && at + 1u < max; i++) {
        long n = (long)i * steps;

        /* Full above the cell's midpoint, half above its start: a reading
         * that has only just entered a cell must not fill it. */
        out[at++] = (value > n + steps / 2) ? '*' : (value > n) ? '+' : '.';
    }
    out[at] = '\0';
    return (int)at;
}

int
tiku_attr_rating(long value, char *out, size_t max)
{
    return gauge_cells(value, 10, 5, out, max);
}

int
tiku_attr_gauge(const tiku_desc_t *d, const char *raw, int cells,
                    char *out, size_t max)
{
    long v;

    if (d == NULL || !d->has_range || d->hi <= d->lo) {
        if (out != NULL && max > 0u) { out[0] = '\0'; }
        return 0;
    }
    if (!as_long(raw, &v)) {
        if (out != NULL && max > 0u) { out[0] = '\0'; }
        return 0;
    }
    return gauge_cells(v - d->lo, d->hi - d->lo, cells, out, max);
}

/** @brief The name after the last slash. */
static const char *
dis_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/** @brief The suffix of @p dir spanning its last @p depth components. */
static const char *
dis_suffix(const char *dir, int depth)
{
    const char *at = dir + strlen(dir);
    int seen = 0;

    while (at > dir) {
        if (at[-1] == '/') {
            seen++;
            if (seen >= depth) {
                return at - 1;
            }
        }
        at--;
    }
    return dir;
}

int
tiku_attr_disambiguate(const char *path, const char *const *others,
                           int n, char *out, size_t max)
{
    const char *leaf;
    char dir[TIKU_PATH_MAX];
    const char *slash;
    int i, depth, colliding = 0;

    if (path == NULL || out == NULL || max == 0u) {
        return 0;
    }
    leaf = dis_leaf(path);
    for (i = 0; i < n; i++) {
        if (others[i] != NULL && strcmp(others[i], path) != 0 &&
            strcmp(dis_leaf(others[i]), leaf) == 0) {
            colliding = 1;
        }
    }
    if (!colliding) {
        return snprintf(out, max, "%s", leaf);
    }
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    dir[(slash != NULL) ? (size_t)(slash - dir) : 0u] = '\0';
    /* The MINIMUM that tells them apart: one trailing component, then two,
     * until no colliding neighbour shares the suffix. */
    for (depth = 1; ; depth++) {
        const char *mine = dis_suffix(dir, depth);
        int clash = 0, whole = (mine == dir);

        for (i = 0; i < n; i++) {
            char odir[TIKU_PATH_MAX];
            const char *oslash;

            if (others[i] == NULL || strcmp(others[i], path) == 0 ||
                strcmp(dis_leaf(others[i]), leaf) != 0) {
                continue;
            }
            snprintf(odir, sizeof odir, "%s", others[i]);
            oslash = strrchr(odir, '/');
            odir[(oslash != NULL) ? (size_t)(oslash - odir) : 0u] = '\0';
            if (strcmp(dis_suffix(odir, depth), mine) == 0 &&
                !(whole && strcmp(odir, dir) == 0)) {
                clash = 1;
            }
        }
        if (!clash || whole) {
            /* Dots stand for what was left OUT; a suffix that is the
             * whole parent shows the parent as it is. */
            return whole
                ? snprintf(out, max, "%s -- %s", leaf,
                           dir[0] != '\0' ? dir : "/")
                : snprintf(out, max, "%s -- ...%s", leaf, mine);
        }
    }
}
