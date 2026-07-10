/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_browse.inl - a tiny HTML-to-text renderer for the BASIC "browser".
 *
 * Shared by the STRIP$() string function (renders an HTML string into a BASIC
 * string) and the BROWSE statement (renders a fetched page straight to the
 * console).  It strips tags, skips <script>/<style> contents, decodes the
 * common entities, collapses runs of whitespace, and turns block-level tags
 * into line breaks.  Deliberately tiny -- a lynx-style text view of a simple
 * page, not a real HTML parser: unknown tags are dropped, attributes ignored,
 * no CSS/JS/layout.  Included before tiku_basic_string.inl so STRIP$ can use it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Case-insensitive: does @p p start with the lowercase literal @p lit? */
static int
basic_ci_starts(const char *p, const char *lit)
{
    while (*lit) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != *lit) return 0;
        p++; lit++;
    }
    return 1;
}

/* Does the tag at @p p (pointing at '<') open/close a block element, i.e.
 * should it become a line break in the text rendering? */
static int
basic_html_block(const char *p)
{
    char name[8];
    int  i = 0;

    p++;                                   /* skip '<' */
    if (*p == '/') p++;                     /* a closing tag counts too */
    while (i < 7 && ((*p >= 'a' && *p <= 'z') ||
                     (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9'))) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        name[i++] = c;
        p++;
    }
    name[i] = '\0';
    return !strcmp(name, "br")  || !strcmp(name, "p")   ||
           !strcmp(name, "div") || !strcmp(name, "li")  ||
           !strcmp(name, "tr")  || !strcmp(name, "hr")  ||
           !strcmp(name, "ul")  || !strcmp(name, "ol")  ||
           !strcmp(name, "h1")  || !strcmp(name, "h2")  ||
           !strcmp(name, "h3")  || !strcmp(name, "h4")  ||
           !strcmp(name, "h5")  || !strcmp(name, "h6")  ||
           !strcmp(name, "table") || !strcmp(name, "title");
}

/* Decode the HTML entity at @p p (pointing at '&'); set *adv to the bytes
 * consumed and return the decoded character.  Handles the common named
 * entities plus decimal &#NN;; anything else passes '&' through literally. */
static char
basic_html_entity(const char *p, int *adv)
{
    static const struct { const char *name; char ch; } ents[] = {
        { "amp;",  '&'  }, { "lt;",   '<' }, { "gt;",  '>'  },
        { "quot;", '"'  }, { "apos;", '\'' }, { "nbsp;", ' ' },
    };
    unsigned i;

    if (p[1] == '#') {                      /* numeric: &#NN; (decimal) */
        int v = 0, k = 2;
        while (p[k] >= '0' && p[k] <= '9') { v = v * 10 + (p[k] - '0'); k++; }
        if (p[k] == ';') k++;
        *adv = k;
        return (v >= 32 && v < 127) ? (char)v : ' ';
    }
    for (i = 0; i < sizeof ents / sizeof ents[0]; i++) {
        size_t len = strlen(ents[i].name);
        if (strncmp(p + 1, ents[i].name, len) == 0) {
            *adv = 1 + (int)len;
            return ents[i].ch;
        }
    }
    *adv = 1;                               /* unknown -> literal '&' */
    return '&';
}

/* Render HTML @p html to plain text.  If @p out is non-NULL, write up to
 * outcap-1 bytes there (NUL-terminated); otherwise print it via SHELL_PRINTF
 * (line-buffered).  Skips a leading HTTP header block if one is present. */
static void
basic_html_render(const char *html, char *out, size_t outcap)
{
    const char *p, *body;
    char   line[100];
    size_t li = 0, oi = 0;
    int    in_tag = 0, skip = 0, sp = 1;     /* sp: at a fresh line / after space */

    body = strstr(html, "\r\n\r\n");         /* drop HTTP headers if present */
    p = body ? body + 4 : html;

/* Emit one char to the active sink, flushing the print line on newline/full. */
#define PUT(chr) do {                                                       \
        char _c = (chr);                                                     \
        if (out) { if (oi + 1 < outcap) out[oi++] = _c; }                    \
        else {                                                               \
            line[li++] = _c;                                                 \
            if (_c == '\n' || li >= sizeof line - 1) {                       \
                line[li] = '\0'; SHELL_PRINTF("%s", line); li = 0;           \
            }                                                                \
        }                                                                    \
    } while (0)

    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c == '<') {
            if      (basic_ci_starts(p, "<script"))  skip = 1;
            else if (basic_ci_starts(p, "<style"))   skip = 1;
            else if (basic_ci_starts(p, "</script")) skip = 0;
            else if (basic_ci_starts(p, "</style"))  skip = 0;
            else if (!skip && !sp && basic_html_block(p)) { PUT('\n'); sp = 1; }
            in_tag = 1; p++; continue;
        }
        if (c == '>') { in_tag = 0; p++; continue; }
        if (in_tag || skip) { p++; continue; }

        if (c == '&') {
            int  adv;
            char e = basic_html_entity(p, &adv);
            p += adv;
            if (e == ' ') { if (!sp) { PUT(' '); sp = 1; } }
            else          { PUT(e); sp = 0; }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!sp) { PUT(' '); sp = 1; }
            p++; continue;
        }
        PUT((char)c); sp = 0; p++;
    }

    if (out) {
        out[oi] = '\0';
    } else {
        if (li) { line[li] = '\0'; SHELL_PRINTF("%s", line); }
        SHELL_PRINTF("\n");
    }
#undef PUT
}
