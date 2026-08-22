/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_cff.c - the other kind of outline (see the header).
 *
 * Two halves.  The container is INDEXes and DICTs: counted arrays of byte
 * ranges, and operand-then-operator dictionaries whose numbers are packed
 * six different ways.  The charstring is a stack machine whose operators
 * draw, and whose first stack-clearing operator may carry the glyph's
 * width as one extra argument nobody announces.
 *
 * Every read is bounds-checked against the table.  These files arrive by
 * being dropped in a folder, and a charstring is a program: it can call
 * itself, index past its subroutines, and ask for more stack than it has.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_cff.h"

#include <stdlib.h>
#include <string.h>

#define CFF_STACK      520      /* the operand stack (CFF2 blends are wide) */
#define CFF_MAX_DEPTH  10       /* subroutine calls within calls */
#define CFF_MAX_STEMS  96
#define CFF_MAX_OPS    100000   /* a charstring that never ends */
#define CFF2_MAX_VS    64       /* variation subtables we keep a count for */

typedef struct {
    const unsigned char *offsets;
    const unsigned char *data;  /* one BEFORE the first element */
    int                  count;
    int                  offsize;
    size_t               end;   /* first byte after the whole index */
} cff_index_t;

struct tiku_desk_cff {
    const unsigned char *data;
    size_t               len;
    cff_index_t          charstrings;
    cff_index_t          gsubrs;
    cff_index_t          lsubrs;    /* the top-level Private DICT's */
    float                upem;
    /* CID-keyed fonts keep a Private DICT per group of glyphs. */
    int                  cid;
    size_t               fdarray;
    size_t               fdselect;
    cff_index_t          fdsubrs[16];
    int                  fdcount;
    /*
     * A CFF2 face is the same machine with the outlines left variable.
     * We draw the DEFAULT instance: the blend operator's deltas all fall
     * away, and all that is needed of the variation store is how many of
     * them each subtable carries, so the stack stays in step.
     */
    int                  cff2;
    int                  region[CFF2_MAX_VS];
    int                  nregion;
};

/* ------------------------------------------------------------ the bytes */

static unsigned
rd8(const unsigned char *p)
{
    return p[0];
}

static unsigned
rd16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long
rd32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

/** @brief Whether @p need bytes at @p off are inside the table. */
static int
fits(const tiku_desk_cff_t *cff, size_t off, size_t need)
{
    return off <= cff->len && need <= cff->len - off;
}

/** @brief An offset of @p size bytes, big-endian. */
static size_t
rdoff(const unsigned char *p, int size)
{
    size_t v = 0;
    int i;

    for (i = 0; i < size; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* ---------------------------------------------------------- the INDEXes */

/**
 * @brief Read the INDEX at @p off.
 *
 * @return 1 when it is one; @c out->end is where the next thing starts.
 */
static int
index_read_inner(const tiku_desk_cff_t *cff, size_t off, cff_index_t *out)
{
    size_t last;

    /* CFF2 counts its elements in FOUR bytes where CFF counts them in
     * two -- the one change the container makes to an INDEX. */
    size_t head = cff->cff2 ? 4u : 2u;

    memset(out, 0, sizeof *out);
    if (!fits(cff, off, head)) {
        return 0;
    }
    out->count = cff->cff2 ? (int)rd32(cff->data + off)
                           : (int)rd16(cff->data + off);
    if (out->count <= 0) {
        out->end = off + head;  /* an empty index is its count and no more */
        out->count = 0;
        return 1;
    }
    if (!fits(cff, off + head, 1u)) {
        return 0;
    }
    out->offsize = (int)rd8(cff->data + off + head);
    if (out->offsize < 1 || out->offsize > 4) {
        return 0;
    }
    if (!fits(cff, off + head + 1u,
              (size_t)(out->count + 1) * (size_t)out->offsize)) {
        return 0;
    }
    out->offsets = cff->data + off + head + 1u;
    /* Element offsets count from ONE, so the data pointer is one before
     * the first byte: element i runs [data+off(i), data+off(i+1)). */
    out->data = cff->data + off + head + 1u +
                (size_t)(out->count + 1) * (size_t)out->offsize - 1u;
    last = rdoff(out->offsets + (size_t)out->count * (size_t)out->offsize,
                 out->offsize);
    if (last < 1u) {
        return 0;
    }
    out->end = (size_t)(out->data - cff->data) + last;
    if (out->end > cff->len) {
        return 0;
    }
    return 1;
}

/**
 * @brief Read the INDEX at @p off, leaving NOTHING behind on failure.
 *
 * A half-read index with its count already taken from the file and its
 * offsets still unset is the shape of a crash: the count says there are
 * elements and the pointer says they are at address nothing.  Callers
 * that ignore the return -- and a Private DICT's Subrs is one -- must
 * find an empty index rather than a lying one.
 */
static int
index_read(const tiku_desk_cff_t *cff, size_t off, cff_index_t *out)
{
    if (!index_read_inner(cff, off, out)) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    return 1;
}

/** @brief Element @p i of @p idx.  @return its length, 0 when absent. */
static size_t
index_get(const tiku_desk_cff_t *cff, const cff_index_t *idx, int i,
          const unsigned char **out)
{
    size_t a, b;

    if (idx == NULL || i < 0 || i >= idx->count ||
        idx->offsets == NULL || idx->data == NULL) {
        return 0;
    }
    a = rdoff(idx->offsets + (size_t)i * (size_t)idx->offsize, idx->offsize);
    b = rdoff(idx->offsets + (size_t)(i + 1) * (size_t)idx->offsize,
              idx->offsize);
    if (b <= a) {
        return 0;
    }
    if ((size_t)(idx->data - cff->data) + b > cff->len) {
        return 0;
    }
    *out = idx->data + a;
    return b - a;
}

/* ------------------------------------------------------------ the DICTs */

#define DICT_MAX_OPERANDS 48

typedef struct {
    float operand[DICT_MAX_OPERANDS];
    int   count;
} cff_dict_args_t;

/**
 * @brief Walk a DICT, handing each operator and its operands to @p fn.
 *
 * Operators are 0-21, with 12 as an escape: 1200 + the second byte.
 */
static void
dict_walk(const tiku_desk_cff_t *cff, size_t off, size_t len,
          void (*fn)(int op, const cff_dict_args_t *args, void *ctx),
          void *ctx)
{
    cff_dict_args_t args;
    size_t i = off, end = off + len;

    if (!fits(cff, off, len)) {
        return;
    }
    args.count = 0;
    while (i < end) {
        unsigned b0 = rd8(cff->data + i);

        /* Operators run 0-21 in CFF, with 12 the escape; CFF2 spends
         * some of the bytes CFF left reserved -- vstore is 24 -- so the
         * whole 0-27 band is operators here.  The operand encodings all
         * begin at 28, so they are never mistaken for one. */
        if (b0 <= 27u) {
            int op = (int)b0;

            i++;
            if (b0 == 12u) {
                if (i >= end) {
                    return;
                }
                op = 1200 + (int)rd8(cff->data + i);
                i++;
            }
            fn(op, &args, ctx);
            args.count = 0;
            continue;
        }
        if (b0 == 28u) {
            if (i + 3u > end) { return; }
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] =
                    (float)(short)rd16(cff->data + i + 1u);
            }
            i += 3u;
        } else if (b0 == 29u) {
            if (i + 5u > end) { return; }
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] =
                    (float)(long)(int32_t)rd32(cff->data + i + 1u);
            }
            i += 5u;
        } else if (b0 == 30u) {
            /* A real number, nibble by nibble; we only need its value
             * roughly, for the FontMatrix. */
            char text[64];
            int at = 0;
            int done = 0;

            i++;
            while (i < end && !done) {
                unsigned byte = rd8(cff->data + i);
                int half;

                i++;
                for (half = 0; half < 2; half++) {
                    unsigned nib = (half == 0) ? (byte >> 4) : (byte & 0x0Fu);

                    if (nib <= 9u && at < (int)sizeof text - 2) {
                        text[at++] = (char)('0' + (int)nib);
                    } else if (nib == 0x0Au && at < (int)sizeof text - 2) {
                        text[at++] = '.';
                    } else if (nib == 0x0Bu && at < (int)sizeof text - 2) {
                        text[at++] = 'E';
                    } else if (nib == 0x0Cu && at < (int)sizeof text - 3) {
                        text[at++] = 'E';
                        text[at++] = '-';
                    } else if (nib == 0x0Eu && at < (int)sizeof text - 2) {
                        text[at++] = '-';
                    } else if (nib == 0x0Fu) {
                        done = 1;
                        break;
                    }
                }
            }
            text[at] = '\0';
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] = (float)atof(text);
            }
        } else if (b0 >= 32u && b0 <= 246u) {
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] = (float)((int)b0 - 139);
            }
            i++;
        } else if (b0 >= 247u && b0 <= 250u) {
            if (i + 2u > end) { return; }
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] =
                    (float)(((int)b0 - 247) * 256 +
                            (int)rd8(cff->data + i + 1u) + 108);
            }
            i += 2u;
        } else if (b0 >= 251u && b0 <= 254u) {
            if (i + 2u > end) { return; }
            if (args.count < DICT_MAX_OPERANDS) {
                args.operand[args.count++] =
                    (float)(-((int)b0 - 251) * 256 -
                            (int)rd8(cff->data + i + 1u) - 108);
            }
            i += 2u;
        } else {
            return;             /* reserved: the DICT is not one */
        }
    }
}

/**
 * @brief An operand as an offset, or 0 when it cannot be one.
 *
 * These are numbers out of the file, and a float too big or below zero
 * has no defined conversion to size_t at all -- so the range is checked
 * before the cast rather than after it.
 */
static size_t
as_offset(float v)
{
    if (!(v >= 0.0f) || v > 1.0e9f) {
        return 0;               /* NaN fails the first test too */
    }
    return (size_t)v;
}

typedef struct {
    size_t charstrings;
    size_t private_off, private_size;
    size_t fdarray, fdselect;
    size_t vstore;
    float  matrix0;
    int    cid;
    int    charstring_type;
} cff_top_t;

static void
top_op(int op, const cff_dict_args_t *args, void *ctx)
{
    cff_top_t *top = ctx;

    switch (op) {
    case 17:                    /* CharStrings */
        if (args->count >= 1) {
            top->charstrings = as_offset(args->operand[0]);
        }
        break;
    case 18:                    /* Private: size then offset */
        if (args->count >= 2) {
            top->private_size = as_offset(args->operand[0]);
            top->private_off = as_offset(args->operand[1]);
        }
        break;
    case 1206:                  /* CharstringType */
        if (args->count >= 1) {
            top->charstring_type = (int)args->operand[0];
        }
        break;
    case 1207:                  /* FontMatrix */
        if (args->count >= 1 && args->operand[0] > 0.0f) {
            top->matrix0 = args->operand[0];
        }
        break;
    case 1230:                  /* ROS: this is a CID-keyed font */
        top->cid = 1;
        break;
    case 1236:                  /* FDArray */
        if (args->count >= 1) { top->fdarray = as_offset(args->operand[0]); }
        break;
    case 1237:                  /* FDSelect */
        if (args->count >= 1) {
            top->fdselect = as_offset(args->operand[0]);
        }
        break;
    case 24:                    /* vstore (CFF2 variation store) */
        if (args->count >= 1) {
            top->vstore = as_offset(args->operand[0]);
        }
        break;
    default:
        break;
    }
}

typedef struct {
    size_t subrs;               /* relative to the Private DICT */
    float  nominal, dflt;
} cff_private_t;

static void
private_op(int op, const cff_dict_args_t *args, void *ctx)
{
    cff_private_t *pv = ctx;

    switch (op) {
    case 19:                    /* Subrs, offset from the Private DICT */
        if (args->count >= 1) { pv->subrs = as_offset(args->operand[0]); }
        break;
    case 20:
        if (args->count >= 1) { pv->dflt = args->operand[0]; }
        break;
    case 21:
        if (args->count >= 1) { pv->nominal = args->operand[0]; }
        break;
    default:
        break;
    }
}

/**
 * @brief Read a Private DICT for the local subroutines it names.
 *
 * The width it also carries is not read: the charstring's idea of a
 * glyph's advance and hmtx's are the same number in a well-made face,
 * and hmtx is the one the rest of the sfnt is measured against.
 */
static void
read_private(tiku_desk_cff_t *cff, size_t off, size_t size,
             cff_index_t *subrs)
{
    cff_private_t pv;

    memset(subrs, 0, sizeof *subrs);
    pv.subrs = 0;
    pv.nominal = 0.0f;
    pv.dflt = 0.0f;
    if (size == 0u || !fits(cff, off, size)) {
        return;
    }
    dict_walk(cff, off, size, private_op, &pv);
    if (pv.subrs != 0u) {
        (void)index_read(cff, off + pv.subrs, subrs);
    }
}

/* ------------------------------------------------------- the charstring */

typedef struct {
    const tiku_desk_cff_t *cff;
    tiku_desk_path_t      *path;
    const tiku_desk_hint_t *hint;
    float                  stack[CFF_STACK];
    int                    sp;
    float                  x, y;
    int                    stems;
    int                    width_done;
    int                    open;
    int                    depth;
    long                   ops;
    const cff_index_t     *lsubrs;
    int                    lbias, gbias;
    int                    cff2;
    int                    cur_regions;     /* deltas per blended value */
    float                  trans[32];
} cff_run_t;

/**
 * @brief Run a charstring.
 *
 * @return 0 malformed, 1 reached its end or returned, 2 saw an endchar --
 *         which finishes the GLYPH, not merely the subroutine it was in.
 */
static int run_charstring(cff_run_t *run, const unsigned char *code,
                          size_t len);

/** @brief The bias a subroutine index is counted from. */
static int
bias_of(int count)
{
    if (count < 1240) {
        return 107;
    }
    if (count < 33900) {
        return 1131;
    }
    return 32768;
}

static void
moveto(cff_run_t *run, float x, float y)
{
    run->x = x;
    run->y = y;
    if (run->open) {
        tiku_desk_path_close(run->path);
    }
    tiku_desk_path_move(run->path, tiku_desk_hint_x(run->hint, x),
                        tiku_desk_hint_y(run->hint, y));
    run->open = 1;
}

static void
lineto(cff_run_t *run, float x, float y)
{
    if (!run->open) {
        /* No moveto yet: the contour starts where the pen stands, which
         * is the origin.  Better a closed contour than a stray edge. */
        moveto(run, run->x, run->y);
    }
    run->x = x;
    run->y = y;
    tiku_desk_path_line(run->path, tiku_desk_hint_x(run->hint, x),
                        tiku_desk_hint_y(run->hint, y));
}

static void
curveto(cff_run_t *run, float ax, float ay, float bx, float by, float x,
        float y)
{
    if (!run->open) {
        moveto(run, run->x, run->y);
    }
    run->x = x;
    run->y = y;
    tiku_desk_path_cubic(run->path,
                         tiku_desk_hint_x(run->hint, ax),
                         tiku_desk_hint_y(run->hint, ay),
                         tiku_desk_hint_x(run->hint, bx),
                         tiku_desk_hint_y(run->hint, by),
                         tiku_desk_hint_x(run->hint, x),
                         tiku_desk_hint_y(run->hint, y));
}

/**
 * @brief Note the width, if this operator's odd argument is one.
 *
 * The first stack-clearing operator in a charstring may carry the glyph's
 * width as an extra leading argument.  Nothing marks it: it is there when
 * there is one more argument than the operator takes.
 *
 * @param even How many arguments the operator itself wants, or -1 when
 *             it takes any even number.
 * @return how many leading arguments to skip.
 */
static int
take_width(cff_run_t *run, int even)
{
    int skip = 0;

    if (run->cff2) {
        return 0;               /* CFF2 charstrings carry no width */
    }
    if (!run->width_done) {
        run->width_done = 1;
        if (even < 0) {
            skip = (run->sp & 1) ? 1 : 0;
        } else if (run->sp > even) {
            skip = 1;
        }
    }
    return skip;
}

/** @brief hstem and friends: count them, and note the width. */
static void
do_stems(cff_run_t *run)
{
    int skip = take_width(run, -1);

    run->stems += (run->sp - skip) / 2;
    if (run->stems > CFF_MAX_STEMS) {
        run->stems = CFF_MAX_STEMS;
    }
    run->sp = 0;
}

static int
run_op(cff_run_t *run, int op, const unsigned char **code, size_t *left)
{
    int i, skip;

    switch (op) {
    case 1:                     /* hstem */
    case 3:                     /* vstem */
    case 18:                    /* hstemhm */
    case 23:                    /* vstemhm */
        do_stems(run);
        return 1;
    case 19:                    /* hintmask */
    case 20: {                  /* cntrmask */
        /* Arguments still on the stack here are an implied vstemhm: the
         * format lets a charstring leave the operator out. */
        size_t bytes;

        do_stems(run);
        bytes = (size_t)((run->stems + 7) / 8);
        if (bytes > *left) {
            return 0;
        }
        *code += bytes;
        *left -= bytes;
        return 1;
    }
    case 21:                    /* rmoveto */
        skip = take_width(run, 2);
        if (run->sp - skip >= 2) {
            moveto(run, run->x + run->stack[skip],
                   run->y + run->stack[skip + 1]);
        }
        run->sp = 0;
        return 1;
    case 22:                    /* hmoveto */
        skip = take_width(run, 1);
        if (run->sp - skip >= 1) {
            moveto(run, run->x + run->stack[skip], run->y);
        }
        run->sp = 0;
        return 1;
    case 4:                     /* vmoveto */
        skip = take_width(run, 1);
        if (run->sp - skip >= 1) {
            moveto(run, run->x, run->y + run->stack[skip]);
        }
        run->sp = 0;
        return 1;
    case 5:                     /* rlineto */
        for (i = 0; i + 1 < run->sp; i += 2) {
            lineto(run, run->x + run->stack[i], run->y + run->stack[i + 1]);
        }
        run->sp = 0;
        return 1;
    case 6:                     /* hlineto */
    case 7:                     /* vlineto */
        {
            int horizontal = (op == 6);

            for (i = 0; i < run->sp; i++) {
                if (horizontal) {
                    lineto(run, run->x + run->stack[i], run->y);
                } else {
                    lineto(run, run->x, run->y + run->stack[i]);
                }
                horizontal = !horizontal;
            }
        }
        run->sp = 0;
        return 1;
    case 8:                     /* rrcurveto */
        for (i = 0; i + 5 < run->sp; i += 6) {
            float ax = run->x + run->stack[i];
            float ay = run->y + run->stack[i + 1];
            float bx = ax + run->stack[i + 2];
            float by = ay + run->stack[i + 3];

            curveto(run, ax, ay, bx, by, bx + run->stack[i + 4],
                    by + run->stack[i + 5]);
        }
        run->sp = 0;
        return 1;
    case 24:                    /* rcurveline */
        for (i = 0; i + 5 < run->sp - 2; i += 6) {
            float ax = run->x + run->stack[i];
            float ay = run->y + run->stack[i + 1];
            float bx = ax + run->stack[i + 2];
            float by = ay + run->stack[i + 3];

            curveto(run, ax, ay, bx, by, bx + run->stack[i + 4],
                    by + run->stack[i + 5]);
        }
        if (i + 1 < run->sp) {
            lineto(run, run->x + run->stack[i], run->y + run->stack[i + 1]);
        }
        run->sp = 0;
        return 1;
    case 25:                    /* rlinecurve */
        for (i = 0; i + 1 < run->sp - 6; i += 2) {
            lineto(run, run->x + run->stack[i], run->y + run->stack[i + 1]);
        }
        if (i + 5 < run->sp) {
            float ax = run->x + run->stack[i];
            float ay = run->y + run->stack[i + 1];
            float bx = ax + run->stack[i + 2];
            float by = ay + run->stack[i + 3];

            curveto(run, ax, ay, bx, by, bx + run->stack[i + 4],
                    by + run->stack[i + 5]);
        }
        run->sp = 0;
        return 1;
    case 26:                    /* vvcurveto */
    case 27: {                  /* hhcurveto */
        int at = 0;
        float lead = 0.0f;

        if ((run->sp & 1) != 0) {
            lead = run->stack[0];
            at = 1;
        }
        for (; at + 3 < run->sp; at += 4) {
            float ax, ay, bx, by, ex, ey;

            if (op == 26) {
                ax = run->x + lead;
                ay = run->y + run->stack[at];
                bx = ax + run->stack[at + 1];
                by = ay + run->stack[at + 2];
                ex = bx;
                ey = by + run->stack[at + 3];
            } else {
                ax = run->x + run->stack[at];
                ay = run->y + lead;
                bx = ax + run->stack[at + 1];
                by = ay + run->stack[at + 2];
                ex = bx + run->stack[at + 3];
                ey = by;
            }
            lead = 0.0f;        /* only the first curve gets it */
            curveto(run, ax, ay, bx, by, ex, ey);
        }
        run->sp = 0;
        return 1;
    }
    case 30:                    /* vhcurveto */
    case 31: {                  /* hvcurveto */
        int horizontal = (op == 31);
        int at = 0;

        while (at + 3 < run->sp) {
            float ax, ay, bx, by, ex, ey;
            int last = (run->sp - at == 5);

            if (horizontal) {
                ax = run->x + run->stack[at];
                ay = run->y;
                bx = ax + run->stack[at + 1];
                by = ay + run->stack[at + 2];
                ey = by + run->stack[at + 3];
                ex = last ? bx + run->stack[at + 4] : bx;
            } else {
                ax = run->x;
                ay = run->y + run->stack[at];
                bx = ax + run->stack[at + 1];
                by = ay + run->stack[at + 2];
                ex = bx + run->stack[at + 3];
                ey = last ? by + run->stack[at + 4] : by;
            }
            curveto(run, ax, ay, bx, by, ex, ey);
            at += last ? 5 : 4;
            horizontal = !horizontal;
        }
        run->sp = 0;
        return 1;
    }
    case 10:                    /* callsubr */
    case 29: {                  /* callgsubr */
        const cff_index_t *idx = (op == 10) ? run->lsubrs
                                            : &run->cff->gsubrs;
        int bias = (op == 10) ? run->lbias : run->gbias;
        const unsigned char *body;
        size_t body_len;
        int n;

        if (run->sp < 1 || idx == NULL) {
            return 0;
        }
        n = (int)run->stack[--run->sp] + bias;
        body_len = index_get(run->cff, idx, n, &body);
        if (body_len == 0u) {
            return 1;           /* an empty or absent subr draws nothing */
        }
        if (run->depth >= CFF_MAX_DEPTH) {
            return 0;
        }
        run->depth++;
        n = run_charstring(run, body, body_len);
        run->depth--;
        if (n == 0) {
            return 0;
        }
        if (n == 2) {
            return 3;           /* the subroutine ended the glyph */
        }
        return 1;
    }
    case 15:                    /* vsindex (CFF2): which deltas to expect */
        if (run->cff2 && run->sp >= 1) {
            int idx = (int)run->stack[run->sp - 1];

            if (idx >= 0 && idx < run->cff->nregion) {
                run->cur_regions = run->cff->region[idx];
            }
        }
        run->sp = 0;
        return 1;
    case 16:                    /* blend (CFF2): keep the default, drop deltas */
        if (run->cff2 && run->sp >= 1) {
            int n = (int)run->stack[--run->sp];
            int drop = n * run->cur_regions;

            /* The n default values sit BELOW n*regions deltas; dropping
             * the deltas leaves the defaults, which is the instance we
             * draw.  A well-made charstring never underflows this. */
            if (n < 0 || drop < 0) {
                run->sp = 0;
            } else {
                if (drop > run->sp) {
                    drop = run->sp;
                }
                run->sp -= drop;
            }
        } else {
            run->sp = 0;
        }
        return 1;
    case 11:                    /* return */
        return 2;
    case 14:                    /* endchar */
        (void)take_width(run, 0);
        if (run->open) {
            tiku_desk_path_close(run->path);
            run->open = 0;
        }
        run->sp = 0;
        return 3;
    case 1235: {                /* flex */
        if (run->sp >= 13) {
            float ax = run->x + run->stack[0], ay = run->y + run->stack[1];
            float bx = ax + run->stack[2], by = ay + run->stack[3];
            float cx = bx + run->stack[4], cy = by + run->stack[5];
            float dx, dy, ex, ey, fx, fy;

            curveto(run, ax, ay, bx, by, cx, cy);
            dx = run->x + run->stack[6];
            dy = run->y + run->stack[7];
            ex = dx + run->stack[8];
            ey = dy + run->stack[9];
            fx = ex + run->stack[10];
            fy = ey + run->stack[11];
            curveto(run, dx, dy, ex, ey, fx, fy);
        }
        run->sp = 0;
        return 1;
    }
    case 1234: {                /* hflex */
        if (run->sp >= 7) {
            float y0 = run->y;
            float ax = run->x + run->stack[0], ay = run->y;
            float bx = ax + run->stack[1], by = ay + run->stack[2];
            float cx = bx + run->stack[3], cy = by;
            float dx, dy, ex, ey, fx;

            curveto(run, ax, ay, bx, by, cx, cy);
            dx = run->x + run->stack[4];
            dy = run->y;
            ex = dx + run->stack[5];
            ey = y0;
            fx = ex + run->stack[6];
            curveto(run, dx, dy, ex, ey, fx, y0);
        }
        run->sp = 0;
        return 1;
    }
    case 1236: {                /* hflex1 */
        if (run->sp >= 9) {
            float y0 = run->y;
            float ax = run->x + run->stack[0], ay = run->y + run->stack[1];
            float bx = ax + run->stack[2], by = ay + run->stack[3];
            float cx = bx + run->stack[4], cy = by;
            float dx, dy, ex, ey, fx;

            curveto(run, ax, ay, bx, by, cx, cy);
            dx = run->x + run->stack[5];
            dy = run->y;
            ex = dx + run->stack[6];
            ey = dy + run->stack[7];
            fx = ex + run->stack[8];
            curveto(run, dx, dy, ex, ey, fx, y0);
        }
        run->sp = 0;
        return 1;
    }
    case 1237: {                /* flex1 */
        if (run->sp >= 11) {
            float x0 = run->x, y0 = run->y;
            float sumx = 0.0f, sumy = 0.0f;
            float ax, ay, bx, by, cx, cy, dx, dy, ex, ey;

            for (i = 0; i < 10; i += 2) {
                sumx += run->stack[i];
                sumy += run->stack[i + 1];
            }
            ax = run->x + run->stack[0];
            ay = run->y + run->stack[1];
            bx = ax + run->stack[2];
            by = ay + run->stack[3];
            cx = bx + run->stack[4];
            cy = by + run->stack[5];
            curveto(run, ax, ay, bx, by, cx, cy);
            dx = run->x + run->stack[6];
            dy = run->y + run->stack[7];
            ex = dx + run->stack[8];
            ey = dy + run->stack[9];
            /* The last point closes back to where the flex began in
             * whichever direction moved least. */
            if ((sumx < 0.0f ? -sumx : sumx) >
                (sumy < 0.0f ? -sumy : sumy)) {
                curveto(run, dx, dy, ex, ey, ex + run->stack[10], y0);
            } else {
                curveto(run, dx, dy, ex, ey, x0, ey + run->stack[10]);
            }
        }
        run->sp = 0;
        return 1;
    }
    default:
        /* The arithmetic and storage operators, which type designers do
         * not use and which we must still not choke on. */
        run->sp = 0;
        return 1;
    }
}

static int
run_charstring(cff_run_t *run, const unsigned char *code, size_t len)
{
    const unsigned char *p = code;
    size_t left = len;

    while (left > 0u) {
        unsigned b0;

        if (++run->ops > CFF_MAX_OPS) {
            return 0;
        }
        b0 = rd8(p);
        p++;
        left--;
        if (b0 >= 32u || b0 == 28u) {
            float v;

            if (b0 == 28u) {
                if (left < 2u) { return 0; }
                v = (float)(short)rd16(p);
                p += 2;
                left -= 2u;
            } else if (b0 <= 246u) {
                v = (float)((int)b0 - 139);
            } else if (b0 <= 250u) {
                if (left < 1u) { return 0; }
                v = (float)(((int)b0 - 247) * 256 + (int)rd8(p) + 108);
                p++;
                left--;
            } else if (b0 <= 254u) {
                if (left < 1u) { return 0; }
                v = (float)(-((int)b0 - 251) * 256 - (int)rd8(p) - 108);
                p++;
                left--;
            } else {
                if (left < 4u) { return 0; }
                v = (float)(long)(int32_t)rd32(p) / 65536.0f;
                p += 4;
                left -= 4u;
            }
            if (run->sp < CFF_STACK) {
                run->stack[run->sp++] = v;
            }
            continue;
        }
        {
            int op = (int)b0;
            int r;

            if (b0 == 12u) {
                if (left < 1u) { return 0; }
                op = 1200 + (int)rd8(p);
                p++;
                left--;
            }
            r = run_op(run, op, &p, &left);
            if (r == 0) {
                return 0;
            }
            if (r == 2) {
                return 1;       /* return: back to the caller */
            }
            if (r == 3) {
                return 2;       /* endchar: nothing after this draws */
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------- API */

/** @brief Which Private DICT glyph @p gid draws with, in a CID font. */
static const cff_index_t *
fd_subrs_for(const tiku_desk_cff_t *cff, unsigned gid)
{
    size_t off = cff->fdselect;
    unsigned fd = 0;

    if (off == 0u || !fits(cff, off, 1u)) {
        /* No map: CFF2 and CID both keep their subrs in the first (only)
         * Font DICT; plain CFF keeps them in the top-level Private. */
        if (cff->cff2 || cff->cid) {
            return (cff->fdcount > 0) ? &cff->fdsubrs[0] : &cff->lsubrs;
        }
        return &cff->lsubrs;
    }
    if (rd8(cff->data + off) == 0u) {
        if (!fits(cff, off + 1u + gid, 1u)) {
            return &cff->lsubrs;
        }
        fd = rd8(cff->data + off + 1u + gid);
    } else if (rd8(cff->data + off) == 3u) {
        unsigned ranges, i;

        if (!fits(cff, off + 1u, 2u)) {
            return &cff->lsubrs;
        }
        ranges = rd16(cff->data + off + 1u);
        if (!fits(cff, off + 3u, (size_t)ranges * 3u + 2u)) {
            return &cff->lsubrs;
        }
        for (i = 0; i < ranges; i++) {
            unsigned first = rd16(cff->data + off + 3u + (size_t)i * 3u);
            unsigned next = rd16(cff->data + off + 3u + (size_t)(i + 1) * 3u);

            if (gid >= first && gid < next) {
                fd = rd8(cff->data + off + 3u + (size_t)i * 3u + 2u);
                break;
            }
        }
    }
    if ((int)fd < cff->fdcount) {
        return &cff->fdsubrs[fd];
    }
    return &cff->lsubrs;
}

/** @brief Read the FDArray's Private DICTs, for a CID-keyed font. */
static void
read_fdarray(tiku_desk_cff_t *cff, size_t off)
{
    cff_index_t fds;
    int i;

    if (off == 0u || !index_read(cff, off, &fds)) {
        return;
    }
    for (i = 0; i < fds.count &&
                i < (int)(sizeof cff->fdsubrs / sizeof cff->fdsubrs[0]); i++) {
        const unsigned char *dict;
        size_t len = index_get(cff, &fds, i, &dict);
        cff_top_t sub;

        if (len == 0u) {
            continue;
        }
        memset(&sub, 0, sizeof sub);
        dict_walk(cff, (size_t)(dict - cff->data), len, top_op, &sub);
        read_private(cff, sub.private_off, sub.private_size,
                     &cff->fdsubrs[i]);
        cff->fdcount = i + 1;
    }
}

/**
 * @brief Read a CFF2 variation store for its region counts.
 *
 * The default instance is all we draw, so the deltas themselves are of
 * no use -- but a blend has to know how many of them to step over, and
 * that count is per variation subtable.  vsindex picks the subtable.
 */
static void
read_vstore(tiku_desk_cff_t *cff, size_t off)
{
    size_t ivs;
    unsigned ivd_count, i;

    if (off == 0u || !fits(cff, off, 2u)) {
        return;
    }
    ivs = off + 2u;             /* past the uint16 length prefix */
    if (!fits(cff, ivs, 8u) || rd16(cff->data + ivs) != 1u) {
        return;                 /* only format 1 is defined */
    }
    ivd_count = rd16(cff->data + ivs + 6u);
    for (i = 0; i < ivd_count && cff->nregion < CFF2_MAX_VS; i++) {
        size_t rec = ivs + 8u + (size_t)i * 4u;
        size_t ivd;

        if (!fits(cff, rec, 4u)) {
            break;
        }
        ivd = ivs + (size_t)rd32(cff->data + rec);
        if (!fits(cff, ivd, 6u)) {
            cff->region[cff->nregion++] = 0;
            continue;
        }
        /* itemCount, wordDeltaCount, then regionIndexCount -- the k a
         * blend against this subtable carries per value. */
        cff->region[cff->nregion++] = (int)rd16(cff->data + ivd + 4u);
    }
}

/**
 * @brief Set up a CFF2 face: header, a bare Top DICT, and the rest.
 *
 * @return the face, or NULL.
 */
static tiku_desk_cff_t *
cff2_open(tiku_desk_cff_t *cff)
{
    cff_top_t top;
    cff_index_t gsubrs;
    size_t hdr, td_len, td_off;

    cff->cff2 = 1;              /* before any INDEX: its count is 32-bit now */
    if (!fits(cff, 0u, 5u)) {
        free(cff);
        return NULL;
    }
    hdr = rd8(cff->data + 2);
    td_len = rd16(cff->data + 3);
    td_off = hdr;
    if (hdr < 5u || !fits(cff, td_off, td_len)) {
        free(cff);
        return NULL;
    }
    memset(&top, 0, sizeof top);
    top.charstring_type = 2;
    /* The Top DICT is a bare dictionary here, not the one-element INDEX
     * a plain CFF wraps it in. */
    dict_walk(cff, td_off, td_len, top_op, &top);
    /* The Global Subr INDEX follows the Top DICT. */
    if (!index_read(cff, td_off + td_len, &gsubrs)) {
        free(cff);
        return NULL;
    }
    cff->gsubrs = gsubrs;
    if (top.charstrings == 0u ||
        !index_read(cff, top.charstrings, &cff->charstrings) ||
        cff->charstrings.count == 0) {
        free(cff);
        return NULL;
    }
    cff->fdselect = top.fdselect;
    read_fdarray(cff, top.fdarray);     /* CFF2 always keeps one */
    read_vstore(cff, top.vstore);
    if (top.matrix0 > 0.0f) {
        float upem = 1.0f / top.matrix0;

        if (upem >= 16.0f && upem <= 16384.0f) {
            cff->upem = upem;
        }
    }
    return cff;
}

tiku_desk_cff_t *
tiku_desk_cff_open(const unsigned char *data, size_t len)
{
    tiku_desk_cff_t *cff;
    cff_index_t names, tops, strings;
    cff_top_t top;
    const unsigned char *dict;
    size_t dict_len, hdr;

    if (data == NULL || len < 4u) {
        return NULL;
    }
    cff = calloc(1u, sizeof *cff);
    if (cff == NULL) {
        return NULL;
    }
    cff->data = data;
    cff->len = len;
    cff->upem = 1000.0f;

    if (rd8(data) == 2u) {
        return cff2_open(cff);  /* major version 2: the variable kind */
    }
    hdr = rd8(data + 2);
    if (hdr < 4u || !fits(cff, hdr, 1u)) {
        free(cff);
        return NULL;
    }
    if (!index_read(cff, hdr, &names) ||
        !index_read(cff, names.end, &tops) ||
        !index_read(cff, tops.end, &strings) ||
        !index_read(cff, strings.end, &cff->gsubrs)) {
        free(cff);
        return NULL;
    }
    dict_len = index_get(cff, &tops, 0, &dict);
    if (dict_len == 0u) {
        free(cff);
        return NULL;
    }
    memset(&top, 0, sizeof top);
    top.charstring_type = 2;
    dict_walk(cff, (size_t)(dict - data), dict_len, top_op, &top);
    if (top.charstrings == 0u || top.charstring_type != 2) {
        free(cff);              /* Type 1 charstrings: a different machine */
        return NULL;
    }
    if (!index_read(cff, top.charstrings, &cff->charstrings) ||
        cff->charstrings.count == 0) {
        free(cff);
        return NULL;
    }
    read_private(cff, top.private_off, top.private_size, &cff->lsubrs);
    cff->cid = top.cid;
    cff->fdselect = top.fdselect;
    if (top.cid) {
        read_fdarray(cff, top.fdarray);
    }
    if (top.matrix0 > 0.0f) {
        /* The FontMatrix says how big a unit is; 0.001 is the usual.
         * Believe it only within reason: a matrix claiming a thousand
         * units to the pixel would scale every coordinate out of the
         * range an int can hold. */
        float upem = 1.0f / top.matrix0;

        if (upem >= 16.0f && upem <= 16384.0f) {
            cff->upem = upem;
        }
    }
    return cff;
}

void
tiku_desk_cff_close(tiku_desk_cff_t *cff)
{
    free(cff);
}

float
tiku_desk_cff_upem(const tiku_desk_cff_t *cff)
{
    return (cff != NULL) ? cff->upem : 1000.0f;
}

int
tiku_desk_cff_outline(const tiku_desk_cff_t *cff, unsigned gid,
                      const tiku_desk_hint_t *hint, tiku_desk_path_t *path)
{
    cff_run_t run;
    const unsigned char *code;
    size_t len;

    if (cff == NULL || path == NULL || hint == NULL) {
        return 0;
    }
    len = index_get(cff, &cff->charstrings, (int)gid, &code);
    if (len == 0u) {
        return 1;               /* an empty charstring: a space */
    }
    memset(&run, 0, sizeof run);
    run.cff = cff;
    run.path = path;
    run.hint = hint;
    run.lsubrs = fd_subrs_for(cff, gid);
    run.lbias = bias_of(run.lsubrs->count);
    run.gbias = bias_of(cff->gsubrs.count);
    run.cff2 = cff->cff2;
    run.cur_regions = (cff->nregion > 0) ? cff->region[0] : 0;
    if (run_charstring(&run, code, len) == 0) {
        return 0;
    }
    if (run.open) {
        tiku_desk_path_close(path);
    }
    return 1;
}
