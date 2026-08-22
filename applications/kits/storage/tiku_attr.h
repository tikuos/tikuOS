/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_attr.h - attributes, and the text a column shows for one.
 *
 * Tracker decides a column's text by looking first at the attribute's NAME,
 * then at the "display as" hint the type registry gave it, and only then
 * falling back to a generic reader.  A TikuOS namespace node carries that
 * hint itself: the manifest's meta column is a typed descriptor
 * (vtype,unit,freshness,cost[,lo..hi]), so the registry Tracker had to
 * consult is already attached to the node.
 *
 * That descriptor is what lets a reading be shown as "1.2 MiB" or "15h 57m"
 * instead of a bare integer, and what lets a value be REFUSED before it is
 * sent: writing "banana" to a u32, or 5 to a node declared 0..1, is a
 * mistake the descriptor can catch without troubling the device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ATTR_H_
#define TIKU_ATTR_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_model.h"

/** @brief What a node's value IS. */
typedef enum {
    TIKU_VT_NONE = 0,        /* untyped: the text is the value        */
    TIKU_VT_BOOL,
    TIKU_VT_U32,
    TIKU_VT_I32,
    TIKU_VT_STR
} tiku_vtype_t;

/** @brief What the number MEANS, which is what picks the formatter. */
typedef enum {
    TIKU_UNIT_NONE = 0,
    TIKU_UNIT_BOOL,
    TIKU_UNIT_BYTES,
    TIKU_UNIT_HZ,
    TIKU_UNIT_SECONDS,
    TIKU_UNIT_COUNT,
    TIKU_UNIT_OTHER          /* known name, no formatter of its own   */
} tiku_unit_t;

/** @brief How current a reading is, which says whether it needs re-reading. */
typedef enum {
    TIKU_FRESH_UNKNOWN = 0,
    TIKU_FRESH_STATIC,       /* fixed for the life of the boot        */
    TIKU_FRESH_CACHED,
    TIKU_FRESH_LIVE
} tiku_fresh_t;

#define TIKU_UNIT_NAME_MAX 12

/** @brief A parsed typed descriptor. */
typedef struct {
    tiku_vtype_t vtype;
    tiku_unit_t  unit;
    tiku_fresh_t fresh;
    char             unit_name[TIKU_UNIT_NAME_MAX];  /* verbatim      */
    char             cost[TIKU_UNIT_NAME_MAX];
    int              has_range;
    long             lo, hi;
} tiku_desc_t;

/**
 * @brief Parse a manifest meta field.
 *
 * Accepts "-" and anything unrecognised, which yields an untyped descriptor
 * rather than an error: a node the device describes in a way we do not know
 * yet must still be listed, just without a specialised formatter.
 *
 * @return 1 when a type was recognised, 0 when the result is untyped.
 */
int tiku_desc_parse(const char *meta, tiku_desc_t *out);

/**
 * @brief Format @p raw for display, by what the descriptor says it means.
 *
 * The text a cell too narrow to hold it is fitted by the caller's own
 * measurer, the same as any other cell: what a value means does not depend
 * on how much room the column has.
 *
 * @return Length written.
 */
int tiku_attr_format(const tiku_desc_t *d, const char *raw,
                         char *out, size_t max);

/**
 * @brief Whether this node's value can be edited in place.
 *
 * Writable and typed as something a text field can produce; a node with no
 * declared type is still editable, since its value is text.
 */
int tiku_attr_editable(const tiku_model_t *m,
                           const tiku_desc_t *d);

/**
 * @brief Check @p text before it is sent to the device.
 *
 * @param err Receives the message the user should read; the wording is the
 *            reason, not "invalid input".
 * @return 1 when acceptable.
 */
int tiku_attr_validate(const tiku_desc_t *d, const char *text,
                           char *err, size_t errmax);

/**
 * @brief Canonical text to send for @p text.
 *
 * "on", "true" and "1" all mean the same thing to a boolean node, but the
 * device is sent one spelling.
 */
int tiku_attr_canonical(const tiku_desc_t *d, const char *text,
                            char *out, size_t max);

/**
 * @brief Order two values of the same descriptor.
 *
 * Numbers compare as numbers even though the column shows them formatted --
 * sorting a byte column by its displayed text would put "9 B" after "10 KiB".
 *
 * @return <0, 0 or >0.
 */
int tiku_attr_compare(const tiku_desc_t *d, const char *a,
                          const char *b);

/** @brief Where an over-long string loses its middle, or its end. */
typedef enum {
    TIKU_TRUNC_MIDDLE = 0,   /* names and generic values              */
    TIKU_TRUNC_END,          /* sizes, and the open-with relation     */
    TIKU_TRUNC_BEGIN         /* type-ahead: the newest characters win */
} tiku_trunc_t;

/**
 * @brief Fit @p in to @p width pixels, eliding with a single ellipsis.
 *
 * Names elide in the MIDDLE, which is what keeps both ends of
 * "gpio_bank_1_direction" readable where a trailing elision would leave a
 * column of identical prefixes.  The mark is one U+2026, not three periods.
 *
 * @param measure Returns the pixel width of a string.
 * @return Length written.
 */
int tiku_text_fit(const char *in, int width, tiku_trunc_t how,
                      int (*measure)(const char *, void *), void *ctx,
                      char *out, size_t max);

/**
 * @brief The longest date+time form that fits @p width (MA-025).
 *
 * Tried longest first -- long date with medium time, long with short,
 * medium with short, short with short -- and the first that fits wins.  If
 * none do, the date alone; if that still does not, it is elided.  A column
 * therefore says as much as it has room for rather than the same thing at
 * every width.
 *
 * @param when Seconds since the epoch.  Zero means the store has no wall
 *             clock behind it, and the field reads as unknown rather than
 *             as 1970.
 * @return Length written.
 */
int tiku_attr_time_fit(int64_t when, int width,
                           int (*measure)(const char *, void *), void *ctx,
                           char *out, size_t max);

/**
 * @brief A human-readable size that degrades with the column (MA-023).
 *
 * A negative @p bytes is an UNKNOWN size and reads "-": a listing that
 * spelled it "0 B" would be claiming a fact it does not have.  Below a
 * kibibyte the plain byte count is the readable form; above it the value is
 * scaled and an insignificant trailing zero dropped, and if that still does
 * not fit, the bare number, and only then an end ellipsis.
 */
int tiku_attr_size_fit(int64_t bytes, int width,
                           int (*measure)(const char *, void *), void *ctx,
                           char *out, size_t max);

/**
 * @brief Five stars out of ten, with halves (MA-037).
 *
 * The thresholds are Tracker's: with five cells over a maximum of ten, cell
 * n is full above n+1, half above n, and empty otherwise.  The marks are
 * ASCII because the drawing floor's font is single-byte, so a star glyph
 * would render as nothing at all.
 */
int tiku_attr_rating(long value, char *out, size_t max);

/**
 * @brief The same cell arithmetic over a declared range.
 *
 * A rating is a gauge with a fixed range; a node that declares one (a
 * battery, a signal strength) wants the identical rendering, which is why
 * this is the general form and the rating is a call into it.
 *
 * @param cells How many cells to draw.
 * @return Length written, or 0 when the descriptor declares no range.
 */
int tiku_attr_gauge(const tiku_desc_t *d, const char *raw, int cells,
                        char *out, size_t max);

/**
 * @brief A display name that tells same-named neighbours apart (NeXT UIG
 *        ch8): the leaf alone when it is unique among @p others, otherwise
 *        "leaf -- .../suffix" with the MINIMUM trailing path that
 *        distinguishes it -- computed against the rows actually shown,
 *        not against the whole namespace.
 *
 * @return the length written.
 */
int tiku_attr_disambiguate(const char *path,
                               const char *const *others, int n,
                               char *out, size_t max);

#endif /* TIKU_ATTR_H_ */
