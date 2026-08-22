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
#include <stdlib.h>
#include <string.h>

#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_ui.h"

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

struct tiku_dl {
    unsigned char *b;
    size_t         n, cap;
    int            count;
    int            missed;
};

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
    case OP_LIST_ROW:      return 13;   /* 12 + a terminator     */
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
