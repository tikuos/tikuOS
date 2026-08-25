/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dl.c - what was drawn, rather than what it came out as.
 *
 * The whole of it is one growing byte buffer.  Recording appends the
 * bytes that go on the wire, so flattening is a copy; playing walks the
 * same bytes and calls the drawing functions the recording mirrors.
 * There is therefore exactly one description of what a command looks
 * like, in the two switch-shaped places below, rather than an encoder
 * and a decoder that must be kept agreeing about a third.
 *
 * Coordinates go as i16.  A pixel further than 32767 from the origin is
 * not a thing this interface draws, and halving every rectangle is worth
 * more here than the reach: the point of the exercise is the size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_slider.h"
#include "tiku_alert.h"
#include "tiku_tabs.h"

/* The ops.  The number is part of the wire format; gaps are deliberate,
 * so the primitives and the controls stay legible as two groups. */
#define OP_FILL          1u
#define OP_FRAME         2u
#define OP_BEVEL         3u
#define OP_HLINE         4u
#define OP_VLINE         5u
#define OP_TEXT          6u
#define OP_TEXT_CENTERED 7u
#define OP_CLIP_SET      8u
#define OP_CLIP_RESET    9u

#define OP_PANEL        16u
#define OP_RAISED       17u
#define OP_SUNKEN       18u
#define OP_BUTTON       19u
#define OP_CHECKBOX     20u
#define OP_RADIO        21u
#define OP_LIST_ROW     22u

/* Art once, placements by reference: [u32 id][u16 len][hvif], then
 * [u32 id][i16 x][i16 y][i16 size][u8 mix][u8 0][u32 wash]. */
#define OP_ICON_ART     23u
#define OP_ICON         24u

/*
 * The second round of control ops.  Every one of these replaces a run of
 * rectangles that said only what a thing LOOKED like: the far end draws
 * them from the kit it already has, and a reader that is not drawing at
 * all -- an agent, a screen reader -- gets the noun instead of the
 * spans.  A gauge was arriving as one grey box with no bar in it, and an
 * alert's warning disc as sixty-six horizontal lines.
 */
#define OP_GAUGE        25u
#define OP_TIP          26u
#define OP_TEXTFIELD    27u
#define OP_SCROLLBAR    28u
#define OP_SLIDER       29u
#define OP_ALERT_ICON   30u
#define OP_TABS         31u
#define OP_MENUFIELD    32u

struct tiku_dl {
    unsigned char *b;
    size_t         n, cap;
    int            count;
    int            missed;
    int            icons;                    /* OP_ICON placements     */
    int            arts;                     /* distinct OP_ICON_ARTs  */
    uint32_t       art[TIKU_DL_ART_MAX];     /* their ids, for once-ness */
};

/** @brief The far end's rasteriser, injected by whoever links one. */
static tiku_dl_icon_fn icon_painter;

void
tiku_dl_set_icon_painter(tiku_dl_icon_fn fn)
{
    icon_painter = fn;
}

/** @brief FNV-1a of the blob: the art's identity is its bytes. */
static uint32_t
art_id_of(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < n; i++) {
        h = (h ^ (uint32_t)p[i]) * 16777619u;
    }
    return h;
}

/*---------------------------------------------------------------------------*/
/* Bytes                                                                     */
/*---------------------------------------------------------------------------*/

static void
put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void
put32(unsigned char *p, uint32_t v)
{
    put16(p, (uint16_t)(v & 0xFFFFu));
    put16(p + 2, (uint16_t)((v >> 16) & 0xFFFFu));
}

static uint16_t
get16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t
get32(const unsigned char *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static int16_t
get_i16(const unsigned char *p)
{
    return (int16_t)get16(p);
}

/** @brief Room for @p more bytes.  @return 1 when there is. */
static int
room(tiku_dl_t *dl, size_t more)
{
    size_t want;
    unsigned char *bigger;

    if (dl->n + more <= dl->cap) {
        return 1;
    }
    want = (dl->cap > 0u) ? dl->cap * 2u : 256u;
    while (want < dl->n + more) {
        if (want > (size_t)-1 / 2u) {
            return 0;
        }
        want *= 2u;
    }
    bigger = (unsigned char *)realloc(dl->b, want);
    if (bigger == NULL) {
        return 0;
    }
    dl->b = bigger;
    dl->cap = want;
    return 1;
}

/**
 * @brief Open a command of @p op with @p len bytes of payload.
 *
 * @return where to write the payload, or NULL when there is no room --
 *         and then nothing has been written, so a list that runs out
 *         stays a list of whole commands.
 */
static unsigned char *
begin(tiku_dl_t *dl, uint16_t op, size_t len)
{
    unsigned char *at;

    if (dl == NULL || len > 0xFFFFu || !room(dl, 4u + len)) {
        return NULL;
    }
    at = dl->b + dl->n;
    put16(at, op);
    put16(at + 2, (uint16_t)len);
    dl->n += 4u + len;
    dl->count++;
    return at + 4;
}

static void
put_rect(unsigned char *p, tiku_rect_t r)
{
    put16(p, (uint16_t)(int16_t)r.x);
    put16(p + 2, (uint16_t)(int16_t)r.y);
    put16(p + 4, (uint16_t)(int16_t)r.w);
    put16(p + 6, (uint16_t)(int16_t)r.h);
}

static tiku_rect_t
get_rect(const unsigned char *p)
{
    tiku_rect_t r;

    r.x = get_i16(p);
    r.y = get_i16(p + 2);
    r.w = get_i16(p + 4);
    r.h = get_i16(p + 6);
    return r;
}

/*---------------------------------------------------------------------------*/
/* Making and unmaking                                                       */
/*---------------------------------------------------------------------------*/

tiku_dl_t *
tiku_dl_new(void)
{
    return (tiku_dl_t *)calloc(1u, sizeof(tiku_dl_t));
}

void
tiku_dl_free(tiku_dl_t *dl)
{
    if (dl != NULL) {
        free(dl->b);
        free(dl);
    }
}

void
tiku_dl_clear(tiku_dl_t *dl)
{
    if (dl != NULL) {
        dl->n = 0u;
        dl->count = 0;
        dl->missed = 0;
        dl->icons = 0;
        dl->arts = 0;
    }
}

int
tiku_dl_count(const tiku_dl_t *dl)
{
    return (dl != NULL) ? dl->count : 0;
}

void
tiku_dl_miss(tiku_dl_t *dl)
{
    if (dl != NULL) {
        dl->missed++;
    }
}

int
tiku_dl_misses(const tiku_dl_t *dl)
{
    return (dl != NULL) ? dl->missed : 0;
}

/*---------------------------------------------------------------------------*/
/* Recording                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief The shape most commands have: a rectangle and a colour. */
static int
rect_colour(tiku_dl_t *dl, uint16_t op, tiku_rect_t r, tiku_rgb_t c)
{
    unsigned char *p = begin(dl, op, 12u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put32(p + 8, c);
    return 1;
}

int
tiku_dl_fill(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c)
{
    return rect_colour(dl, OP_FILL, r, c);
}

int
tiku_dl_frame(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c)
{
    return rect_colour(dl, OP_FRAME, r, c);
}

int
tiku_dl_sunken(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t face)
{
    return rect_colour(dl, OP_SUNKEN, r, face);
}

int
tiku_dl_bevel(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t light,
              tiku_rgb_t shadow)
{
    unsigned char *p = begin(dl, OP_BEVEL, 16u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put32(p + 8, light);
    put32(p + 12, shadow);
    return 1;
}

/** @brief A line: origin, length along one axis, colour. */
static int
line(tiku_dl_t *dl, uint16_t op, int x, int y, int run, tiku_rgb_t c)
{
    unsigned char *p = begin(dl, op, 10u);

    if (p == NULL) {
        return 0;
    }
    put16(p, (uint16_t)(int16_t)x);
    put16(p + 2, (uint16_t)(int16_t)y);
    put16(p + 4, (uint16_t)(int16_t)run);
    put32(p + 6, c);
    return 1;
}

int
tiku_dl_hline(tiku_dl_t *dl, int x, int y, int w, tiku_rgb_t c)
{
    return line(dl, OP_HLINE, x, y, w, c);
}

int
tiku_dl_vline(tiku_dl_t *dl, int x, int y, int h, tiku_rgb_t c)
{
    return line(dl, OP_VLINE, x, y, h, c);
}

/** @brief How many bytes a string needs on the wire, terminator and all. */
static size_t
text_len(const char *s)
{
    return (s != NULL) ? strlen(s) + 1u : 1u;
}

static void
put_text(unsigned char *p, const char *s)
{
    if (s != NULL) {
        memcpy(p, s, strlen(s) + 1u);
    } else {
        p[0] = '\0';
    }
}

int
tiku_dl_text(tiku_dl_t *dl, tiku_dl_face_t face, int x, int y,
             const char *text, tiku_rgb_t c)
{
    size_t n = text_len(text);
    unsigned char *p = begin(dl, OP_TEXT, 9u + n);

    if (p == NULL) {
        return 0;
    }
    put16(p, (uint16_t)(int16_t)x);
    put16(p + 2, (uint16_t)(int16_t)y);
    put32(p + 4, c);
    p[8] = (unsigned char)face;
    put_text(p + 9, text);
    return 1;
}

int
tiku_dl_text_centered(tiku_dl_t *dl, tiku_dl_face_t face, tiku_rect_t r,
                      const char *text, tiku_rgb_t c)
{
    size_t n = text_len(text);
    unsigned char *p = begin(dl, OP_TEXT_CENTERED, 13u + n);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put32(p + 8, c);
    p[12] = (unsigned char)face;
    put_text(p + 13, text);
    return 1;
}

int
tiku_dl_clip_set(tiku_dl_t *dl, tiku_rect_t r)
{
    unsigned char *p = begin(dl, OP_CLIP_SET, 8u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    return 1;
}

int
tiku_dl_clip_reset(tiku_dl_t *dl)
{
    return begin(dl, OP_CLIP_RESET, 0u) != NULL;
}

/** @brief A rectangle on its own: the two plain panels. */
static int
rect_only(tiku_dl_t *dl, uint16_t op, tiku_rect_t r)
{
    unsigned char *p = begin(dl, op, 8u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    return 1;
}

int
tiku_dl_panel(tiku_dl_t *dl, tiku_rect_t r)
{
    return rect_only(dl, OP_PANEL, r);
}

int
tiku_dl_raised(tiku_dl_t *dl, tiku_rect_t r)
{
    return rect_only(dl, OP_RAISED, r);
}

/** @brief A control with a label and a state: the whole point of this. */
static int
labelled(tiku_dl_t *dl, uint16_t op, tiku_rect_t r, const char *label,
         unsigned state)
{
    size_t n = text_len(label);
    unsigned char *p = begin(dl, op, 12u + n);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put32(p + 8, (uint32_t)state);
    put_text(p + 12, label);
    return 1;
}

int
tiku_dl_button(tiku_dl_t *dl, tiku_rect_t r, const char *label,
               unsigned state)
{
    return labelled(dl, OP_BUTTON, r, label, state);
}

int
tiku_dl_gauge(tiku_dl_t *dl, tiku_rect_t r, int per_mille)
{
    unsigned char *p = begin(dl, OP_GAUGE, 10u);

    if (p == NULL) {
        return 0;
    }
    if (per_mille < 0) { per_mille = 0; }
    if (per_mille > 1000) { per_mille = 1000; }
    put_rect(p, r);
    put16(p + 8, (uint16_t)per_mille);
    return 1;
}

int
tiku_dl_tip(tiku_dl_t *dl, tiku_rect_t r, const char *text)
{
    return labelled(dl, OP_TIP, r, text, 0u);
}

int
tiku_dl_textfield(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                  unsigned state)
{
    return labelled(dl, OP_TEXTFIELD, r, text, state);
}

int
tiku_dl_scrollbar(tiku_dl_t *dl, tiku_rect_t r, int pos_per_mille,
                  int frac_per_mille, int horiz)
{
    unsigned char *p = begin(dl, OP_SCROLLBAR, 14u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put16(p + 8, (uint16_t)(pos_per_mille < 0 ? 0 : pos_per_mille));
    put16(p + 10, (uint16_t)(frac_per_mille < 0 ? 0 : frac_per_mille));
    put16(p + 12, (uint16_t)(horiz ? 1 : 0));
    return 1;
}

int
tiku_dl_slider(tiku_dl_t *dl, tiku_rect_t r, int min, int max, int value)
{
    unsigned char *p = begin(dl, OP_SLIDER, 14u);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put16(p + 8, (uint16_t)(int16_t)min);
    put16(p + 10, (uint16_t)(int16_t)max);
    put16(p + 12, (uint16_t)(int16_t)value);
    return 1;
}

int
tiku_dl_menufield(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                  unsigned state)
{
    return labelled(dl, OP_MENUFIELD, r, label, state);
}

int
tiku_dl_tabs(tiku_dl_t *dl, tiku_rect_t r, int count, int current,
             const char *labels, size_t labels_len)
{
    unsigned char *p;

    if (labels == NULL || labels_len == 0u || count <= 0) {
        return 0;
    }
    p = begin(dl, OP_TABS, 12u + labels_len);
    if (p == NULL) {
        return 0;
    }
    put_rect(p, r);
    put16(p + 8, (uint16_t)count);
    put16(p + 10, (uint16_t)(int16_t)current);
    memcpy(p + 12, labels, labels_len);
    return 1;
}

int
tiku_dl_alert_icon(tiku_dl_t *dl, int cx, int cy, int kind)
{
    unsigned char *p = begin(dl, OP_ALERT_ICON, 6u);

    if (p == NULL) {
        return 0;
    }
    put16(p, (uint16_t)(int16_t)cx);
    put16(p + 2, (uint16_t)(int16_t)cy);
    put16(p + 4, (uint16_t)kind);
    return 1;
}

int
tiku_dl_checkbox(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                 unsigned state)
{
    return labelled(dl, OP_CHECKBOX, r, label, state);
}

int
tiku_dl_radio(tiku_dl_t *dl, tiku_rect_t r, const char *label,
              unsigned state)
{
    return labelled(dl, OP_RADIO, r, label, state);
}

int
tiku_dl_list_row(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                 int selected)
{
    return labelled(dl, OP_LIST_ROW, r, text, selected ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* The wire                                                                  */
/*---------------------------------------------------------------------------*/

int
tiku_dl_icon(tiku_dl_t *dl, const void *hvif, size_t hlen,
             int x, int y, int size, unsigned mix, tiku_rgb_t wash)
{
    const unsigned char *blob = (const unsigned char *)hvif;
    uint32_t id;
    int i, have = 0;
    unsigned char *p;

    if (dl == NULL) {
        return 0;
    }
    /*
     * The contract is record-or-miss, never neither: every refusal below
     * marks the list not-whole, so whoever is about to put it on a wire
     * sends the frame instead of a window with a hole where art was.
     */
    if (blob == NULL || hlen == 0u || hlen > 0xFFF0u ||
        size <= 0 || size > 1024) {
        tiku_dl_miss(dl);
        return 0;
    }
    id = art_id_of(blob, hlen);
    for (i = 0; i < dl->arts; i++) {
        if (dl->art[i] == id) {
            have = 1;
        }
    }
    if (!have) {
        if (dl->arts >= TIKU_DL_ART_MAX) {
            tiku_dl_miss(dl);
            return 0;
        }
        p = begin(dl, OP_ICON_ART, 6u + hlen);
        if (p == NULL) {
            tiku_dl_miss(dl);
            return 0;
        }
        put32(p, id);
        put16(p + 4, (uint16_t)hlen);
        memcpy(p + 6, blob, hlen);
        dl->art[dl->arts++] = id;
    }
    p = begin(dl, OP_ICON, 16u);
    if (p == NULL) {
        /* The definition may already be in: harmless on its own, an
         * unplaced piece of art draws nothing.  The PLACEMENT is what
         * was lost, and that is a miss. */
        tiku_dl_miss(dl);
        return 0;
    }
    put32(p, id);
    put16(p + 4, (uint16_t)(int16_t)x);
    put16(p + 6, (uint16_t)(int16_t)y);
    put16(p + 8, (uint16_t)(int16_t)size);
    p[10] = (unsigned char)(mix > 255u ? 255u : mix);
    p[11] = 0;
    put32(p + 12, (uint32_t)wash);
    dl->icons++;
    return 1;
}

int
tiku_dl_icons(const tiku_dl_t *dl)
{
    return (dl != NULL) ? dl->icons : 0;
}

size_t
tiku_dl_flat_size(const tiku_dl_t *dl)
{
    return (dl != NULL) ? dl->n : 0u;
}

int
tiku_dl_flatten(const tiku_dl_t *dl, void *buf, size_t max, size_t *wrote)
{
    if (dl == NULL || (buf == NULL && dl->n > 0u) || dl->n > max) {
        return 0;
    }
    if (dl->n > 0u) {
        memcpy(buf, dl->b, dl->n);
    }
    if (wrote != NULL) {
        *wrote = dl->n;
    }
    return 1;
}

/** @brief How long a command's payload must be, or -1 when it varies. */
static int
fixed_payload(uint16_t op)
{
    switch (op) {
    case OP_FILL:
    case OP_FRAME:
    case OP_SUNKEN:      return 12;
    case OP_BEVEL:       return 16;
    case OP_HLINE:
    case OP_VLINE:       return 10;
    case OP_CLIP_SET:
    case OP_PANEL:
    case OP_RAISED:      return 8;
    case OP_ICON:        return 16;
    case OP_GAUGE:       return 10;
    case OP_SCROLLBAR:
    case OP_SLIDER:      return 14;
    case OP_ALERT_ICON:  return 6;
    case OP_CLIP_RESET:  return 0;
    default:             return -1;
    }
}

/** @brief The smallest a command carrying text can be, or -1. */
static int
least_payload(uint16_t op)
{
    switch (op) {
    case OP_TEXT:          return 10;   /* 9 + a terminator      */
    case OP_TEXT_CENTERED: return 14;   /* 13 + a terminator     */
    case OP_BUTTON:
    case OP_CHECKBOX:
    case OP_RADIO:
    case OP_LIST_ROW:
    case OP_TIP:
    case OP_TEXTFIELD:     return 13;   /* 12 + a terminator     */
    case OP_TABS:
    case OP_MENUFIELD:     return 13;   /* 12 + a terminator     */
    default:               return -1;
    }
}

/** @brief Where the text starts inside a command that carries some. */
static int
text_at(uint16_t op)
{
    switch (op) {
    case OP_TEXT:          return 9;
    case OP_TEXT_CENTERED: return 13;
    default:               return 12;
    }
}

tiku_dl_t *
tiku_dl_unflatten(const void *buf, size_t len, size_t *read)
{
    const unsigned char *p = (const unsigned char *)buf;
    tiku_dl_t *dl;
    size_t at = 0u;
    int count = 0;

    if (buf == NULL && len > 0u) {
        return NULL;
    }
    /* Walked once and checked before a byte is kept, so a list either
     * arrives whole or does not arrive. */
    while (at < len) {
        uint16_t op, n;
        int want;

        if (len - at < 4u) {
            return NULL;
        }
        op = get16(p + at);
        n = get16(p + at + 2);
        if (len - at - 4u < (size_t)n) {
            return NULL;
        }
        want = fixed_payload(op);
        if (want >= 0 && (int)n != want) {
            return NULL;            /* a command the wrong size */
        }
        if (op == OP_ICON_ART) {
            /* The length inside must agree with the length outside, or
             * the blob is not the bytes it claims: these arrive from a
             * wire. */
            if (n < 7u || (size_t)get16(p + at + 4u + 4u) != (size_t)n - 6u) {
                return NULL;
            }
        }
        want = least_payload(op);
        if (want >= 0) {
            int start = text_at(op);

            if ((int)n < want || p[at + 4u + (size_t)n - 1u] != '\0' ||
                start >= (int)n) {
                return NULL;        /* text that does not end */
            }
        }
        at += 4u + (size_t)n;
        count++;
    }

    dl = tiku_dl_new();
    if (dl == NULL) {
        return NULL;
    }
    if (len > 0u) {
        if (!room(dl, len)) {
            tiku_dl_free(dl);
            return NULL;
        }
        memcpy(dl->b, p, len);
        dl->n = len;
    }
    dl->count = count;
    /*
     * The counters are rebuilt from the bytes, so a list that crossed a
     * wire answers tiku_dl_icons() the same as the one that was recorded
     * -- the gate that asks is on the SENDING side, but a fact that is
     * true of a list should not depend on which end is holding it.
     */
    at = 0u;
    while (at + 4u <= len) {
        uint16_t op = get16(p + at);
        uint16_t n = get16(p + at + 2);

        if (op == OP_ICON) {
            dl->icons++;
        } else if (op == OP_ICON_ART && dl->arts < TIKU_DL_ART_MAX) {
            dl->art[dl->arts++] = get32(p + at + 4);
        }
        at += 4u + (size_t)n;
    }
    if (read != NULL) {
        *read = len;
    }
    return dl;
}

/*---------------------------------------------------------------------------*/
/* The far end                                                               */
/*---------------------------------------------------------------------------*/

/** @brief The face a stream names, resolved out of this end's own kit. */
static const tiku_font_t *
face_of(unsigned char id)
{
    switch (id) {
    case TIKU_DL_BOLD:      return tiku_font_bold();
    case TIKU_DL_MONO:      return tiku_font_mono(0);
    case TIKU_DL_MONO_BOLD: return tiku_font_mono(1);
    case TIKU_DL_PLAIN:     return tiku_font_plain();
    default:
        /* Past the four, the byte IS the size: the ladder starts at ten,
         * so the two meanings cannot be confused. */
        return tiku_font_at((int)id);
    }
}

int
tiku_dl_play(const tiku_dl_t *dl, tiku_surface_t *s)
{
    size_t at = 0u;
    int done = 0;
    /*
     * The art this list defined, id to bytes.  It points INTO the list,
     * which owns its buffer for the whole of the play, so nothing is
     * copied; and it is per-play, so a list is self-contained -- there
     * is no cache on this end whose absence the far end could guess
     * wrong about.
     */
    struct {
        uint32_t             id;
        const unsigned char *p;
        size_t               len;
    } art[TIKU_DL_ART_MAX];
    int arts = 0;

    if (dl == NULL || s == NULL) {
        return 0;
    }
    while (at + 4u <= dl->n) {
        uint16_t op = get16(dl->b + at);
        uint16_t n = get16(dl->b + at + 2);
        const unsigned char *a = dl->b + at + 4;

        if (dl->n - at - 4u < (size_t)n) {
            break;                  /* unflatten refuses these; belt */
        }
        at += 4u + (size_t)n;
        switch (op) {
        case OP_FILL:
            tiku_fill(s, get_rect(a), get32(a + 8));
            break;
        case OP_FRAME:
            tiku_frame(s, get_rect(a), get32(a + 8));
            break;
        case OP_BEVEL:
            tiku_bevel(s, get_rect(a), get32(a + 8), get32(a + 12));
            break;
        case OP_HLINE:
            tiku_hline(s, get_i16(a), get_i16(a + 2), get_i16(a + 4),
                       get32(a + 6));
            break;
        case OP_VLINE:
            tiku_vline(s, get_i16(a), get_i16(a + 2), get_i16(a + 4),
                       get32(a + 6));
            break;
        case OP_TEXT:
            tiku_text(s, face_of(a[8]), get_i16(a), get_i16(a + 2),
                      (const char *)(a + 9), get32(a + 4));
            break;
        case OP_TEXT_CENTERED:
            (void)tiku_text_centered(s, face_of(a[12]), get_rect(a),
                                     (const char *)(a + 13), get32(a + 8));
            break;
        case OP_CLIP_SET:
            tiku_clip_set(s, get_rect(a));
            break;
        case OP_CLIP_RESET:
            tiku_clip_reset(s);
            break;
        case OP_ICON_ART:
            /* A definition draws nothing; it is remembered for the
             * placements after it.  A duplicate id keeps the FIRST
             * definition, so a hostile list cannot redefine art that
             * commands earlier in the same list were drawn with. */
            if (n >= 7u && arts < TIKU_DL_ART_MAX) {
                uint32_t aid = get32(a);
                int k, dup = 0;

                for (k = 0; k < arts; k++) {
                    if (art[k].id == aid) {
                        dup = 1;
                    }
                }
                if (!dup) {
                    art[arts].id = aid;
                    art[arts].p = a + 6;
                    art[arts].len = (size_t)get16(a + 4);
                    arts++;
                }
            }
            break;
        case OP_ICON:
            /* By reference into what THIS list defined.  An id nothing
             * defined, or no rasteriser on this end, is stepped over --
             * the format's ordinary answer to what an end cannot do. */
            if (icon_painter != NULL) {
                uint32_t aid = get32(a);
                int k;

                for (k = 0; k < arts; k++) {
                    if (art[k].id == aid) {
                        (void)icon_painter(s, art[k].p, art[k].len,
                                           get_i16(a + 4), get_i16(a + 6),
                                           get_i16(a + 8), a[10],
                                           get32(a + 12));
                        break;
                    }
                }
                if (k == arts) {
                    continue;   /* undefined: not carried out */
                }
            } else {
                continue;       /* no rasteriser: not carried out */
            }
            break;
        case OP_PANEL:
            tiku_ui_panel(s, get_rect(a));
            break;
        case OP_RAISED:
            tiku_ui_raised(s, get_rect(a));
            break;
        case OP_SUNKEN:
            tiku_ui_sunken(s, get_rect(a), get32(a + 8));
            break;
        case OP_BUTTON:
            tiku_ui_button(s, get_rect(a), (const char *)(a + 12),
                           get32(a + 8));
            break;
        case OP_GAUGE:
            tiku_ui_gauge(s, get_rect(a),
                          (float)get16(a + 8) / 1000.0f);
            break;
        case OP_TIP:
            tiku_ui_tip(s, get_rect(a), (const char *)(a + 12));
            break;
        case OP_TEXTFIELD: {
            const char *text = (const char *)(a + 12);

            /* Played with the caret past the end: the stream carries what
             * the field SAYS, and where the caret was is the far end's
             * own business -- it is not typing into this copy. */
            tiku_ui_textfield(s, get_rect(a), text, (int)strlen(text),
                              get32(a + 8));
            break;
        }
        case OP_SCROLLBAR:
            tiku_ui_scrollbar(s, get_rect(a),
                              (float)get16(a + 8) / 1000.0f,
                              (float)get16(a + 10) / 1000.0f,
                              (int)get16(a + 12));
            break;
        case OP_SLIDER: {
            tiku_slider_t sl;

            tiku_slider_init(&sl, get_i16(a + 8), get_i16(a + 10),
                             get_i16(a + 12), 1);
            tiku_slider_draw(&sl, s, get_rect(a));
            break;
        }
        case OP_MENUFIELD:
            tiku_ui_menufield(s, get_rect(a), (const char *)(a + 12),
                              get32(a + 8));
            break;
        case OP_TABS: {
            tiku_tabs_t t;
            const char *at = (const char *)(a + 12);
            const unsigned char *end = a + n;
            int n = (int)get16(a + 8), k;

            tiku_tabs_init(&t);
            for (k = 0; k < n && (const unsigned char *)at < end; k++) {
                (void)tiku_tabs_add(&t, at);
                at += strlen(at) + 1u;
            }
            (void)tiku_tabs_select(&t, (int)get_i16(a + 10));
            tiku_tabs_draw(&t, s, get_rect(a));
            break;
        }
        case OP_ALERT_ICON:
            tiku_alert_icon_draw(s, get_i16(a), get_i16(a + 2),
                                 (tiku_alert_kind_t)get16(a + 4));
            break;
        case OP_CHECKBOX:
            tiku_ui_checkbox(s, get_rect(a), (const char *)(a + 12),
                             get32(a + 8));
            break;
        case OP_RADIO:
            tiku_ui_radio(s, get_rect(a), (const char *)(a + 12),
                          get32(a + 8));
            break;
        case OP_LIST_ROW:
            tiku_ui_list_row(s, get_rect(a), (const char *)(a + 12),
                             get32(a + 8) != 0u);
            break;
        default:
            continue;               /* not ours: stepped over, not counted */
        }
        done++;
    }
    return done;
}

/*---------------------------------------------------------------------------*
 * The reader                                                                 *
 *---------------------------------------------------------------------------*/

/** @brief The op's kind, for a reader that is not drawing. */
static tiku_dl_fact_kind_t
fact_kind(uint16_t op)
{
    switch (op) {
    case OP_TEXT:
    case OP_TEXT_CENTERED: return TIKU_DL_FACT_TEXT;
    case OP_BUTTON:        return TIKU_DL_FACT_BUTTON;
    case OP_CHECKBOX:      return TIKU_DL_FACT_CHECKBOX;
    case OP_RADIO:         return TIKU_DL_FACT_RADIO;
    case OP_LIST_ROW:      return TIKU_DL_FACT_LIST_ROW;
    case OP_GAUGE:         return TIKU_DL_FACT_GAUGE;
    case OP_TIP:           return TIKU_DL_FACT_TIP;
    case OP_TEXTFIELD:     return TIKU_DL_FACT_TEXTFIELD;
    case OP_SCROLLBAR:     return TIKU_DL_FACT_SCROLLBAR;
    case OP_SLIDER:        return TIKU_DL_FACT_SLIDER;
    case OP_ALERT_ICON:    return TIKU_DL_FACT_ALERT_ICON;
    case OP_TABS:          return TIKU_DL_FACT_TABS;
    case OP_MENUFIELD:     return TIKU_DL_FACT_MENUFIELD;
    case OP_ICON:          return TIKU_DL_FACT_ICON;
    default:               return TIKU_DL_FACT_PAINT;
    }
}

int
tiku_dl_read(const void *buf, size_t len, size_t *at, tiku_dl_fact_t *out)
{
    const unsigned char *b = (const unsigned char *)buf;
    const unsigned char *a;
    uint16_t op, n;

    if (buf == NULL || at == NULL || out == NULL || *at + 4u > len) {
        return 0;
    }
    op = get16(b + *at);
    n = get16(b + *at + 2);
    if (len - *at - 4u < (size_t)n) {
        return 0;                   /* a body the stream does not hold */
    }
    a = b + *at + 4;
    *at += 4u + (size_t)n;

    memset(out, 0, sizeof *out);
    out->kind = fact_kind(op);

    switch (op) {
    case OP_TEXT:
        if (n < 10u || memchr(a + 9, '\0', n - 9u) == NULL) {
            return 0;
        }
        out->rect = (tiku_rect_t){ get_i16(a), get_i16(a + 2), 0, 0 };
        out->text = (const char *)(a + 9);
        return 1;
    case OP_TEXT_CENTERED:
        if (n < 14u || memchr(a + 13, '\0', n - 13u) == NULL) {
            return 0;
        }
        out->rect = get_rect(a);
        out->text = (const char *)(a + 13);
        return 1;
    case OP_BUTTON:
    case OP_CHECKBOX:
    case OP_RADIO:
    case OP_LIST_ROW:
    case OP_TIP:
    case OP_TEXTFIELD:
    case OP_MENUFIELD:
        if (n < 13u || memchr(a + 12, '\0', n - 12u) == NULL) {
            return 0;
        }
        out->rect = get_rect(a);
        out->state = get32(a + 8);
        out->text = (const char *)(a + 12);
        return 1;
    case OP_GAUGE:
        if (n < 10u) {
            return 0;
        }
        out->rect = get_rect(a);
        out->v1 = (int)get16(a + 8);
        return 1;
    case OP_SCROLLBAR:
        if (n < 14u) {
            return 0;
        }
        out->rect = get_rect(a);
        out->v1 = (int)get16(a + 8);
        out->v2 = (int)get16(a + 10);
        out->v3 = (int)get16(a + 12);
        return 1;
    case OP_SLIDER:
        if (n < 14u) {
            return 0;
        }
        out->rect = get_rect(a);
        out->v1 = get_i16(a + 8);
        out->v2 = get_i16(a + 10);
        out->v3 = get_i16(a + 12);
        return 1;
    case OP_ALERT_ICON:
        if (n < 6u) {
            return 0;
        }
        out->rect = (tiku_rect_t){ get_i16(a), get_i16(a + 2), 0, 0 };
        out->v1 = (int)get16(a + 4);
        return 1;
    case OP_TABS:
        if (n < 13u || memchr(a + 12, '\0', n - 12u) == NULL) {
            return 0;
        }
        out->rect = get_rect(a);
        out->v1 = (int)get16(a + 8);
        out->v2 = get_i16(a + 10);
        out->text = (const char *)(a + 12);
        /* The names must all TERMINATE inside the payload, counted here
         * so the narrator and every other reader may walk them without
         * re-checking. */
        {
            const char *p = out->text;
            const char *end = (const char *)a + n;
            int k;

            for (k = 0; k < out->v1; k++) {
                const char *z = memchr(p, '\0', (size_t)(end - p));

                if (z == NULL) {
                    return 0;
                }
                p = z + 1;
            }
        }
        return 1;
    default:
        /* Paint, icons and anything newer than this reader: the rect
         * where one exists, and no further claim. */
        if (n >= 8u) {
            out->rect = get_rect(a);
        }
        return 1;
    }
}

/** @brief Append to the narration, stopping cleanly at the buffer's end. */
static void
say(char *out, size_t max, size_t *used, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*used >= max) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *used, max - *used, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *used += ((size_t)n < max - *used) ? (size_t)n : max - *used - 1u;
    }
}

size_t
tiku_dl_narrate(const void *buf, size_t len, char *out, size_t max)
{
    size_t at = 0, used = 0;
    tiku_dl_fact_t f;

    if (out == NULL || max == 0) {
        return 0;
    }
    out[0] = '\0';
    while (tiku_dl_read(buf, len, &at, &f)) {
        switch (f.kind) {
        case TIKU_DL_FACT_PAINT:
            break;                  /* strokes: nothing to act on */
        case TIKU_DL_FACT_TEXT:
            say(out, max, &used, "text \"%s\"\n", f.text);
            break;
        case TIKU_DL_FACT_BUTTON:
            say(out, max, &used, "button \"%s\"%s%s\n", f.text,
                (f.state & TIKU_S_DEFAULT) ? ", the default" : "",
                (f.state & TIKU_S_DISABLED) ? ", disabled" : "");
            break;
        case TIKU_DL_FACT_CHECKBOX:
            say(out, max, &used, "checkbox \"%s\", %s%s\n", f.text,
                (f.state & TIKU_S_ON) ? "on" : "off",
                (f.state & TIKU_S_DISABLED) ? ", disabled" : "");
            break;
        case TIKU_DL_FACT_RADIO:
            say(out, max, &used, "radio \"%s\", %s%s\n", f.text,
                (f.state & TIKU_S_ON) ? "chosen" : "not chosen",
                (f.state & TIKU_S_DISABLED) ? ", disabled" : "");
            break;
        case TIKU_DL_FACT_LIST_ROW:
            say(out, max, &used, "row \"%s\"%s\n", f.text,
                f.state ? ", selected" : "");
            break;
        case TIKU_DL_FACT_GAUGE:
            say(out, max, &used, "gauge at %d%%\n", (f.v1 + 5) / 10);
            break;
        case TIKU_DL_FACT_TIP:
            say(out, max, &used, "tip \"%s\"\n", f.text);
            break;
        case TIKU_DL_FACT_TEXTFIELD:
            say(out, max, &used, "text field \"%s\"%s%s\n", f.text,
                (f.state & TIKU_S_FOCUS) ? ", focused" : "",
                (f.state & TIKU_S_DISABLED) ? ", disabled" : "");
            break;
        case TIKU_DL_FACT_SCROLLBAR:
            say(out, max, &used,
                "scrollbar, %d%% along, %d%% of the whole shown\n",
                (f.v1 + 5) / 10, (f.v2 + 5) / 10);
            break;
        case TIKU_DL_FACT_SLIDER:
            say(out, max, &used, "slider at %d of %d..%d\n",
                f.v3, f.v1, f.v2);
            break;
        case TIKU_DL_FACT_ALERT_ICON:
            say(out, max, &used, "%s\n",
                (f.v1 == 2) ? "a stop"
                : (f.v1 == 1) ? "a warning" : "a notice");
            break;
        case TIKU_DL_FACT_TABS: {
            const char *p = f.text;
            int k;

            say(out, max, &used, "tabs:");
            for (k = 0; k < f.v1; k++) {
                say(out, max, &used, " \"%s\"", p);
                if (k == f.v2) {
                    say(out, max, &used, " (current)");
                }
                p += strlen(p) + 1u;
            }
            say(out, max, &used, "\n");
            break;
        }
        case TIKU_DL_FACT_MENUFIELD:
            say(out, max, &used, "menu field showing \"%s\"%s\n", f.text,
                (f.state & TIKU_S_DISABLED) ? ", disabled" : "");
            break;
        case TIKU_DL_FACT_ICON:
            say(out, max, &used, "an icon\n");
            break;
        }
    }
    return used;
}

size_t
tiku_dl_say(const tiku_dl_t *dl, char *out, size_t max)
{
    unsigned char *flat;
    size_t n, used = 0u;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (dl == NULL) {
        return 0u;
    }
    n = tiku_dl_flat_size(dl);
    flat = (n > 0u) ? malloc(n) : NULL;
    if (flat != NULL && tiku_dl_flatten(dl, flat, n, NULL)) {
        used = tiku_dl_narrate(flat, n, out, max);
    }
    free(flat);
    if (tiku_dl_misses(dl) > 0 && used + 1u < max) {
        /* Confessed, not omitted: a reader must be told there was more
         * than it heard. */
        used += (size_t)snprintf(out + used, max - used,
                                 "(%d marks this window drew, the wire "
                                 "could not carry)\n",
                                 tiku_dl_misses(dl));
    }
    return used;
}

/** @brief The fact kinds by the nouns the narrator already speaks. */
static const char *
fact_noun(int kind)
{
    switch (kind) {
    case TIKU_DL_FACT_TEXT:       return "text";
    case TIKU_DL_FACT_BUTTON:     return "button";
    case TIKU_DL_FACT_CHECKBOX:   return "checkbox";
    case TIKU_DL_FACT_RADIO:      return "radio";
    case TIKU_DL_FACT_LIST_ROW:   return "row";
    case TIKU_DL_FACT_GAUGE:      return "gauge";
    case TIKU_DL_FACT_TIP:        return "tip";
    case TIKU_DL_FACT_TEXTFIELD:  return "field";
    case TIKU_DL_FACT_SCROLLBAR:  return "scrollbar";
    case TIKU_DL_FACT_SLIDER:     return "slider";
    case TIKU_DL_FACT_ALERT_ICON: return "alert-icon";
    case TIKU_DL_FACT_TABS:       return "tabs";
    case TIKU_DL_FACT_MENUFIELD:  return "menufield";
    case TIKU_DL_FACT_ICON:       return "icon";
    default:                      return NULL;
    }
}

size_t
tiku_dl_facts(const tiku_dl_t *dl, char *out, size_t max)
{
    unsigned char *flat;
    size_t n, used = 0u;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (dl == NULL) {
        return 0u;
    }
    n = tiku_dl_flat_size(dl);
    flat = (n > 0u) ? malloc(n) : NULL;
    if (flat != NULL) {
        size_t at = 0u;
        tiku_dl_fact_t f;

        if (tiku_dl_flatten(dl, flat, n, NULL)) {
            while (tiku_dl_read(flat, n, &at, &f)) {
                const char *noun = fact_noun((int)f.kind);

                if (noun == NULL) {
                    continue;       /* paint: no meaning beyond itself */
                }
                say(out, max, &used, "%s\t\"%s\"\t%d %d %d %d\t%u\n",
                    noun, (f.text != NULL) ? f.text : "",
                    f.rect.x, f.rect.y, f.rect.w, f.rect.h, f.state);
            }
        }
    }
    free(flat);
    if (tiku_dl_misses(dl) > 0 && used + 1u < max) {
        used += (size_t)snprintf(out + used, max - used,
                                 "(%d marks this window drew, the wire "
                                 "could not carry)\n",
                                 tiku_dl_misses(dl));
    }
    return used;
}
