/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_strings.c - the words the interface is made of (see header).
 *
 * The reader is a small one: an XML file is a stack of
 * <string name="key">value</string>, and that is all of the format it
 * knows.  The files are dropped in a folder, which is to say they come
 * from anywhere, so every read is bounds-checked against the file's end
 * the way the font readers are -- a translation must never be a way in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The baked English, generated from strings/en.xml by tools/genstrings.py.
 * It is sorted by key, so a lookup is a binary search. */
#include "tiku_strings_default.h"

#define STR_MAX_FILE   (1 << 20)    /* a catalogue past a megabyte is not one */
#define STR_MAX_VALUE  4096

/* A translation loaded over the baked English: its entries win. */
typedef struct {
    char *key;
    char *value;
} str_entry_t;

static str_entry_t *g_over;
static int g_over_count;
static int g_over_cap;
static char g_lang[16] = "en";

/* --------------------------------------------------------------- lookup */

/** @brief Compare a key against a baked entry, for bsearch. */
static int
def_cmp(const void *key, const void *entry)
{
    return strcmp((const char *)key,
                  ((const tiku_str_def_t *)entry)->key);
}

/** @brief The baked English for @p key, or NULL. */
static const char *
builtin_of(const char *key)
{
    const tiku_str_def_t *hit = bsearch(key, tiku_str_defaults,
        (size_t)tiku_str_default_count, sizeof tiku_str_defaults[0],
        def_cmp);

    return (hit != NULL) ? hit->value : NULL;
}

/** @brief The loaded translation for @p key, or NULL. */
static const char *
override_of(const char *key)
{
    int lo = 0, hi = g_over_count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(key, g_over[mid].key);

        if (c == 0) {
            return g_over[mid].value;
        }
        if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

const char *
tiku_str(const char *key)
{
    const char *v;

    if (key == NULL) {
        return "";
    }
    v = override_of(key);
    if (v != NULL) {
        return v;
    }
    v = builtin_of(key);
    if (v != NULL) {
        return v;
    }
    /* Nobody has this line.  Its name is worse than the words, but far
     * better than a blank: a person can see what is missing and add it. */
    return key;
}

int
tiku_strings_builtin_count(void)
{
    return tiku_str_default_count;
}

/* ------------------------------------------------------------- the file */

/**
 * @brief The sequence of conversions in @p fmt, e.g. "ds" for "%d of %s".
 *
 * A translation is passed to snprintf as a FORMAT where the English was;
 * if it does not carry the same conversions, in the same order, it could
 * read arguments that are not there.  So the signature is compared before
 * a translation is let in, and a mismatch keeps the English (which the
 * code was written against) rather than the translator's guess.
 */
static void
format_sig(const char *fmt, char *out, size_t max)
{
    size_t at = 0;

    if (fmt == NULL) {
        if (max > 0) { out[0] = '\0'; }
        return;
    }
    while (*fmt != '\0' && at + 1 < max) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == '%') {          /* a literal percent is not one */
                fmt++;
                continue;
            }
            /* skip flags, width, precision, length -- keep the conversion */
            while (*fmt != '\0' && strchr("-+ #0123456789.*hlLqjzt", *fmt)) {
                fmt++;
            }
            if (*fmt != '\0') {
                out[at++] = *fmt++;
            }
        } else {
            fmt++;
        }
    }
    out[at] = '\0';
}

/** @brief Whether a translation's specifiers match the baked English's. */
static int
format_ok(const char *key, const char *value)
{
    const char *en = builtin_of(key);
    char a[32], b[32];

    if (en == NULL) {
        return 1;               /* a key with no English to disagree with */
    }
    format_sig(en, a, sizeof a);
    format_sig(value, b, sizeof b);
    return strcmp(a, b) == 0;
}

/** @brief Put @p key -> @p value into the override table, replacing. */
static void
put(char *key, char *value)
{
    int i;

    if (!format_ok(key, value)) {
        /* A translation that would misread its arguments is no better
         * than the English it fails to replace: keep the English. */
        free(key);
        free(value);
        return;
    }
    for (i = 0; i < g_over_count; i++) {
        if (strcmp(g_over[i].key, key) == 0) {
            free(g_over[i].value);
            g_over[i].value = value;
            free(key);
            return;
        }
    }
    if (g_over_count >= g_over_cap) {
        int want = g_over_cap ? g_over_cap * 2 : 64;
        str_entry_t *grown = realloc(g_over, (size_t)want * sizeof *grown);

        if (grown == NULL) {
            free(key);
            free(value);
            return;
        }
        g_over = grown;
        g_over_cap = want;
    }
    g_over[g_over_count].key = key;
    g_over[g_over_count].value = value;
    g_over_count++;
}

/** @brief Order the overrides by key, so a lookup can bisect them. */
static int
entry_cmp(const void *a, const void *b)
{
    return strcmp(((const str_entry_t *)a)->key,
                  ((const str_entry_t *)b)->key);
}

/**
 * @brief One XML entity, written into @p out.  @return bytes written.
 *
 * @p p points just past the '&'; on return @p end_out points past the ';'.
 * An entity we do not know is left as it was, '&' and all, rather than
 * dropped -- a translator's stray ampersand should show, not vanish.
 */
static int
entity(const char *p, const char *end, char *out, const char **after)
{
    const char *semi = p;
    unsigned cp = 0;

    while (semi < end && *semi != ';' && semi - p < 10) {
        semi++;
    }
    if (semi >= end || *semi != ';') {
        out[0] = '&';
        *after = p;
        return 1;
    }
    if (p[0] == '#') {
        const char *d = p + 1;
        int hex = (*d == 'x' || *d == 'X');

        if (hex) {
            d++;
        }
        for (; d < semi; d++) {
            if (hex) {
                cp = cp * 16u + (unsigned)((*d >= '0' && *d <= '9') ? *d - '0'
                    : (*d | 32) >= 'a' && (*d | 32) <= 'f' ? (*d | 32) - 'a' + 10
                    : 0);
            } else if (*d >= '0' && *d <= '9') {
                cp = cp * 10u + (unsigned)(*d - '0');
            }
        }
    } else if (semi - p == 3 && memcmp(p, "amp", 3) == 0) {
        cp = '&';
    } else if (semi - p == 2 && memcmp(p, "lt", 2) == 0) {
        cp = '<';
    } else if (semi - p == 2 && memcmp(p, "gt", 2) == 0) {
        cp = '>';
    } else if (semi - p == 4 && memcmp(p, "quot", 4) == 0) {
        cp = '"';
    } else if (semi - p == 4 && memcmp(p, "apos", 4) == 0) {
        cp = '\'';
    } else {
        out[0] = '&';           /* an entity we do not know: leave it be */
        *after = p;
        return 1;
    }
    *after = semi + 1;
    /* The code point as UTF-8, which is what the rest of the interface
     * draws: a translation is bytes on the way to the font. */
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/** @brief Copy the inner text [p,close) into a fresh string, decoded. */
static char *
decode(const char *p, const char *close)
{
    size_t room = (size_t)(close - p) + 5u;
    char *out = malloc(room);
    size_t at = 0;

    if (out == NULL) {
        return NULL;
    }
    while (p < close && at + 4 < room) {
        if (*p == '&') {
            const char *after = p + 1;

            at += (size_t)entity(p + 1, close, out + at, &after);
            p = after;
        } else {
            out[at++] = *p++;
        }
    }
    out[at] = '\0';
    return out;
}

/** @brief Whether @p c ends an attribute name or separates tokens. */
static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * @brief The value of attribute @p name inside the tag [p,tagend).
 *
 * A proper walk, attribute by attribute: read a NAME, an '=', a quoted
 * VALUE, and match only when the WHOLE name equals @p name.  Anything
 * else -- the letters of @p name inside another attribute's value, an
 * attribute called something-name, a value whose quote never closes --
 * is stepped over rather than mistaken for the one we want.
 */
static char *
attr(const char *p, const char *tagend, const char *name)
{
    size_t nlen = strlen(name);

    while (p < tagend) {
        const char *ns, *ne;
        char quote;

        while (p < tagend && is_space(*p)) {
            p++;
        }
        ns = p;
        while (p < tagend && *p != '=' && *p != '/' && !is_space(*p)) {
            p++;
        }
        ne = p;
        if (ne == ns) {
            p++;                /* a stray '=' or '/' : step past it, on */
            continue;
        }
        while (p < tagend && is_space(*p)) {
            p++;
        }
        if (p >= tagend || *p != '=') {
            continue;           /* an attribute with no value: skip it */
        }
        p++;
        while (p < tagend && is_space(*p)) {
            p++;
        }
        if (p >= tagend || (*p != '"' && *p != '\'')) {
            return NULL;        /* value not quoted: the tag is malformed */
        }
        quote = *p++;
        {
            const char *vstart = p;

            while (p < tagend && *p != quote) {
                p++;
            }
            if (p >= tagend) {
                return NULL;    /* the quote never closes: reject it */
            }
            if ((size_t)(ne - ns) == nlen && memcmp(ns, name, nlen) == 0) {
                return decode(vstart, p);
            }
            p++;                /* past the closing quote, to the next */
        }
    }
    return NULL;
}

/** @brief Parse @p text as a catalogue.  @return how many lines it held. */
static int
parse(const char *text, size_t len)
{
    const char *p = text;
    const char *end = text + len;
    int loaded = 0;

    while (p < end) {
        const char *tag, *tagend, *close;
        char *key, *value;

        if (*p != '<') {
            p++;
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            const char *stop = p + 4;

            while (stop + 3 <= end && memcmp(stop, "-->", 3) != 0) {
                stop++;
            }
            p = (stop + 3 <= end) ? stop + 3 : end;
            continue;
        }
        if (!(end - p >= 8 && memcmp(p, "<string", 7) == 0 &&
              (p[7] == ' ' || p[7] == '\t' || p[7] == '\n'))) {
            p++;
            continue;
        }
        tag = p + 7;
        tagend = tag;
        while (tagend < end && *tagend != '>') {
            tagend++;
        }
        if (tagend >= end) {
            break;
        }
        key = attr(tag, tagend, "name");
        /* The text runs from just past '>' to the next '<'. */
        close = tagend + 1;
        while (close < end && *close != '<') {
            close++;
        }
        value = (key != NULL) ? decode(tagend + 1, close) : NULL;
        if (key != NULL && value != NULL && key[0] != '\0') {
            put(key, value);
            loaded++;
        } else {
            free(key);
            free(value);
        }
        p = close;
    }
    if (g_over_count > 1) {
        qsort(g_over, (size_t)g_over_count, sizeof *g_over, entry_cmp);
    }
    return loaded;
}

int
tiku_strings_load_file(const char *path)
{
    FILE *f;
    long size;
    char *text;
    int n;

    if (path == NULL) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;               /* no file is not an error: English stands */
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        size > STR_MAX_FILE || fseek(f, 0L, SEEK_SET) != 0) {
        (void)fclose(f);
        return 0;
    }
    text = malloc((size_t)size);
    if (text == NULL || fread(text, 1u, (size_t)size, f) != (size_t)size) {
        free(text);
        (void)fclose(f);
        return 0;
    }
    (void)fclose(f);
    n = parse(text, (size_t)size);
    free(text);
    return n;
}

/* --------------------------------------------------------------- init */

void
tiku_strings_reset(void)
{
    int i;

    for (i = 0; i < g_over_count; i++) {
        free(g_over[i].key);
        free(g_over[i].value);
    }
    free(g_over);
    g_over = NULL;
    g_over_count = 0;
    g_over_cap = 0;
    snprintf(g_lang, sizeof g_lang, "%s", "en");
}

/** @brief The two-letter language out of a locale like "fr_FR.UTF-8". */
static void
short_lang(const char *locale, char *out, size_t max)
{
    size_t i = 0;

    if (locale == NULL) {
        return;
    }
    while (locale[i] != '\0' && locale[i] != '_' && locale[i] != '.' &&
           i + 1 < max) {
        out[i] = locale[i];
        i++;
    }
    out[i] = '\0';
}

const char *
tiku_strings_init(const char *dir, const char *lang)
{
    char chosen[16] = "";
    char path[1024];
    const char *home;

    tiku_strings_reset();
    if (lang != NULL && lang[0] != '\0') {
        short_lang(lang, chosen, sizeof chosen);
    } else {
        const char *env = getenv("TIKU_LANG");

        if (env == NULL || env[0] == '\0') {
            env = getenv("LANG");
        }
        short_lang(env, chosen, sizeof chosen);
    }
    if (chosen[0] == '\0' || strcmp(chosen, "en") == 0 ||
        strcmp(chosen, "C") == 0 || strcmp(chosen, "POSIX") == 0) {
        return "en";           /* the baked English is already the answer */
    }

    /* Where the catalogues live: told, then the resource dir, then the
     * user's own config, then alongside the system's. */
    home = getenv("HOME");
    if (dir != NULL && dir[0] != '\0') {
        snprintf(path, sizeof path, "%s/%s.xml", dir, chosen);
        if (tiku_strings_load_file(path) > 0) {
            snprintf(g_lang, sizeof g_lang, "%s", chosen);
            return g_lang;
        }
    }
    {
        const char *env = getenv("TIKU_STRINGS");

        if (env != NULL && env[0] != '\0') {
            snprintf(path, sizeof path, "%s/%s.xml", env, chosen);
            if (tiku_strings_load_file(path) > 0) {
                snprintf(g_lang, sizeof g_lang, "%s", chosen);
                return g_lang;
            }
        }
    }
    if (home != NULL && home[0] != '\0') {
        snprintf(path, sizeof path, "%s/.config/tracker/strings/%s.xml",
                 home, chosen);
        if (tiku_strings_load_file(path) > 0) {
            snprintf(g_lang, sizeof g_lang, "%s", chosen);
            return g_lang;
        }
    }
    snprintf(path, sizeof path, "/usr/share/tiku-tracker/strings/%s.xml",
             chosen);
    if (tiku_strings_load_file(path) > 0) {
        snprintf(g_lang, sizeof g_lang, "%s", chosen);
        return g_lang;
    }
    return "en";               /* asked for a language we do not carry */
}
