/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dl.h - what was drawn, rather than what it came out as.
 *
 * A session hands its window over as PIXELS: a frame copied, or a surface
 * shared when both ends are on one machine.  That is right for a machine
 * talking to itself and wrong for the thing this interface is being built
 * for.  A 372x302 window is 449,376 bytes; down a 115200 line that is
 * thirty-nine seconds, and the shared-surface path cannot help -- sharing
 * a surface means one machine, which a serial line denies.
 *
 * So: a third way to say what a window looks like.  Not the pixels but
 * the CALLS -- twenty-one of them for that same window, around six
 * hundred bytes, which is the difference between a tenth of a second and
 * most of a minute.
 *
 * It is deliberately NOT an app_server.  Nothing here draws a line or a
 * glyph on anybody's behalf: the commands are the ones the interface
 * already has, semantic where the interface is semantic -- "a button,
 * here, disabled, labelled this" -- so the stream is short because the
 * vocabulary is high, not because it was compressed.
 *
 * And measuring stays where it is.  Every window in this interface lays
 * itself out by asking how wide a piece of text is; a stream that had to
 * ask the far end would pay a round trip per label, which is the mistake
 * that made remote interfaces a byword.  The face tables are small and
 * both ends carry them.
 *
 * The recorded form IS the wire form -- appending a command writes the
 * bytes that will be sent -- so flattening is a copy and there is one
 * description of a command rather than two that must be kept agreeing.
 *
 *   per command:  [u16 op][u16 length][payload]
 *
 * The length is what lets a player step over an op it does not know, so
 * a newer end may draw with something an older one has never heard of
 * and the older one draws the rest.  Same bargain as tiku_msg, for the
 * same reason.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DL_H_
#define TIKU_DL_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_gfx.h"

/**
 * @brief Which face a piece of text is in; the far end resolves it.
 *
 * Four names, and then the ladder: any value past these is a PLAIN face
 * at that many pixels, which works because the smallest face is ten.  So
 * a status line set at eleven arrives at eleven rather than in whatever
 * the body size happens to be.
 */
typedef enum {
    TIKU_DL_PLAIN = 0,
    TIKU_DL_BOLD  = 1,
    TIKU_DL_MONO  = 2,
    TIKU_DL_MONO_BOLD = 3
} tiku_dl_face_t;

typedef struct tiku_dl tiku_dl_t;

/** @brief An empty list.  NULL if there is no room for one. */
tiku_dl_t *tiku_dl_new(void);
void tiku_dl_free(tiku_dl_t *dl);

/** @brief Empty it, keeping the room it has already taken. */
void tiku_dl_clear(tiku_dl_t *dl);

/** @brief How many commands are in it. */
int tiku_dl_count(const tiku_dl_t *dl);

/**
 * @brief Note that something was drawn which this cannot carry.
 *
 * A recording surface calls it for a control with no command of its own.
 * The list stays usable -- what IS recordable is still in it -- but a
 * caller must not send it in place of the pixels, because it no longer
 * describes the whole window.  Better than dropping the call silently
 * and better than refusing to record at all: the decision belongs to
 * whoever is about to put it on a wire.
 */
void tiku_dl_miss(tiku_dl_t *dl);

/** @brief How many such calls there were.  0 means the list is whole. */
int tiku_dl_misses(const tiku_dl_t *dl);

/*---------------------------------------------------------------------------*/
/* Recording.  Each mirrors the drawing call of the same name, and each     */
/* returns 1 when it was written down and 0 when there was no room.          */
/*---------------------------------------------------------------------------*/

int tiku_dl_fill(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c);
int tiku_dl_frame(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c);
int tiku_dl_bevel(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t light,
                  tiku_rgb_t shadow);
int tiku_dl_hline(tiku_dl_t *dl, int x, int y, int w, tiku_rgb_t c);
int tiku_dl_vline(tiku_dl_t *dl, int x, int y, int h, tiku_rgb_t c);
int tiku_dl_text(tiku_dl_t *dl, tiku_dl_face_t face, int x, int y,
                 const char *text, tiku_rgb_t c);
int tiku_dl_text_centered(tiku_dl_t *dl, tiku_dl_face_t face, tiku_rect_t r,
                          const char *text, tiku_rgb_t c);
int tiku_dl_clip_set(tiku_dl_t *dl, tiku_rect_t r);
int tiku_dl_clip_reset(tiku_dl_t *dl);

/* The controls.  These are why the stream is short: one command carries
 * a button, bevels, label, state and all. */
int tiku_dl_panel(tiku_dl_t *dl, tiku_rect_t r);
int tiku_dl_raised(tiku_dl_t *dl, tiku_rect_t r);
int tiku_dl_sunken(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t face);
int tiku_dl_button(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                   unsigned state);
int tiku_dl_checkbox(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                     unsigned state);
int tiku_dl_radio(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                  unsigned state);
/**
 * @brief A gauge: how full it is, not the two rectangles it is made of.
 *
 * @p per_mille is the fill, 0..1000 -- a whole number because the wire
 * carries no floats and a thousandth is finer than a bar can show.
 */
int tiku_dl_gauge(tiku_dl_t *dl, tiku_rect_t r, int per_mille);

/** @brief A tip: the note box and the whole of what it says. */
int tiku_dl_tip(tiku_dl_t *dl, tiku_rect_t r, const char *text);

/**
 * @brief A text field: where it is, what is in it, and its state.
 *
 * The noun an agent most needs after a button -- a place to type, and
 * what is already typed there.
 */
int tiku_dl_textfield(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                      unsigned state);

/**
 * @brief A scrollbar, as its two numbers rather than its twenty-five
 *        rectangles: where it sits and how much of the whole is shown.
 */
int tiku_dl_scrollbar(tiku_dl_t *dl, tiku_rect_t r, int pos_per_mille,
                      int frac_per_mille, int horiz);

/**
 * @brief A slider: the track, and the value on its own scale.
 */
int tiku_dl_slider(tiku_dl_t *dl, tiku_rect_t r, int min, int max,
                   int value);

/**
 * @brief A menu field: the choice it is showing, and its state.
 *
 * Its marker is a stroked triangle drawn pixel by pixel, and pixels are
 * the one thing the list does not carry: recorded as its parts, the
 * field arrived with no marker at all and nothing said so.
 */
int tiku_dl_menufield(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                      unsigned state);

/**
 * @brief A tab strip: the tabs by NAME, and which one is current.
 *
 * @p labels is @p count NUL-terminated strings one after another, which
 * is how the strip already holds them.
 */
int tiku_dl_tabs(tiku_dl_t *dl, tiku_rect_t r, int count, int current,
                 const char *labels, size_t labels_len);

/**
 * @brief The alert's icon, as WHICH icon rather than as its spans.
 *
 * The disc is drawn a horizontal line at a time; a warning arriving as
 * sixty-six of those is the clearest case in the whole vocabulary of a
 * picture the far end could have drawn itself from a single word.
 */
int tiku_dl_alert_icon(tiku_dl_t *dl, int cx, int cy, int kind);

int tiku_dl_list_row(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                     int selected);

/*---------------------------------------------------------------------------*/
/* Pictures with a definition                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief How many distinct pieces of art one list may define.
 *
 * A folder window has half a dozen; the cap is headroom, not a target.
 * Past it the recorder marks the list not-whole rather than dropping
 * art on the floor.
 */
#define TIKU_DL_ART_MAX 32

/**
 * @brief Record one icon: art defined once, then placed by reference.
 *
 * An icon is the one picture that does NOT have to travel as pixels,
 * because it has a definition -- the HVIF blob -- and both ends carry
 * the rasteriser.  So the art crosses once per list, a few hundred
 * bytes, and every further placement of the same art is sixteen.  The
 * receiver rasterises at its own end, which is what keeps the stream
 * resolution-independent: a doubled screen draws a crisp icon beside
 * its crisp text instead of blowing up the sender's pixels.
 *
 * The art's identity is a hash of the BLOB, not its name: a directory
 * may replace a baked icon per device (tiku_icons_load_dir), and two
 * different pictures under one name must not alias.
 *
 * The wash is the whole of the icon states: plain is no wash, dim is
 * the caller's wash at the caller's strength, and selected is black at
 * 87/255 -- the numbers tiku_iconpaint already draws with.
 *
 * @return 1 recorded whole.  0 means it was NOT recorded and the list
 *         has been marked not-whole -- never neither, because a blit
 *         whose definition was dropped is a hole in a list that still
 *         claims to describe the window.
 */
int tiku_dl_icon(tiku_dl_t *dl, const void *hvif, size_t hlen,
                 int x, int y, int size, unsigned mix, tiku_rgb_t wash);

/** @brief How many icon placements the list carries.  0 means none. */
int tiku_dl_icons(const tiku_dl_t *dl);

/**
 * @brief How the far end draws an icon command; @p mix/@p wash as above.
 *
 * Injected rather than called, because the rasteriser lives a kit ABOVE
 * this one and the dependency only goes downward: whoever links both
 * registers the painter once, the way the shells inject icon painters
 * into their menus already.  With none registered an icon command is
 * stepped over, which is the format's ordinary answer to what an end
 * cannot do.
 */
typedef int (*tiku_dl_icon_fn)(tiku_surface_t *s, const void *hvif,
                               size_t hlen, int x, int y, int size,
                               unsigned mix, tiku_rgb_t wash);
void tiku_dl_set_icon_painter(tiku_dl_icon_fn fn);

/*---------------------------------------------------------------------------*/
/* The wire, and the far end                                                 */
/*---------------------------------------------------------------------------*/

/** @brief How many bytes it would flatten to. */
size_t tiku_dl_flat_size(const tiku_dl_t *dl);

/**
 * @brief Write it into @p buf.
 *
 * @param wrote Filled with how much was used; may be NULL.
 * @return 1 when it fitted, 0 otherwise.
 */
int tiku_dl_flatten(const tiku_dl_t *dl, void *buf, size_t max, size_t *wrote);

/*---------------------------------------------------------------------------*
 * Reading the stream, without drawing it.
 *
 * The whole point of a semantic wire is that a reader who is not putting
 * pixels anywhere -- an agent, a screen reader, a test -- can take the
 * window as FACTS.  This is that reader: an iterator that hands back each
 * command as what it says, and a narrator that turns a stream into plain
 * lines a person or a program can take at face value.
 *---------------------------------------------------------------------------*/

/** @brief What kind of fact a command is, once read. */
typedef enum {
    TIKU_DL_FACT_PAINT = 0,     /* a stroke of paint: no meaning beyond it */
    TIKU_DL_FACT_TEXT,          /* words on the window                     */
    TIKU_DL_FACT_BUTTON,
    TIKU_DL_FACT_CHECKBOX,
    TIKU_DL_FACT_RADIO,
    TIKU_DL_FACT_LIST_ROW,
    TIKU_DL_FACT_GAUGE,
    TIKU_DL_FACT_TIP,
    TIKU_DL_FACT_TEXTFIELD,
    TIKU_DL_FACT_SCROLLBAR,
    TIKU_DL_FACT_SLIDER,
    TIKU_DL_FACT_ALERT_ICON,
    TIKU_DL_FACT_TABS,
    TIKU_DL_FACT_MENUFIELD,
    TIKU_DL_FACT_ICON
} tiku_dl_fact_kind_t;

/**
 * @brief One command, as the facts it carries.
 *
 * @p text points INTO the caller's buffer and lives as long as it does.
 * For TABS it is the first of @p v1 names laid end to end.  The three
 * numbers are the op's own: a gauge's fill in thousandths; a slider's
 * min, max and value; a scrollbar's position and fraction in thousandths
 * and whether it lies down; a tab strip's count and current.
 */
typedef struct {
    tiku_dl_fact_kind_t kind;
    tiku_rect_t         rect;
    const char         *text;
    unsigned            state;
    int                 v1, v2, v3;
} tiku_dl_fact_t;

/**
 * @brief Read the next command from a flattened stream.
 *
 * @p at is the cursor, 0 to start; it advances past what was read.  A
 * command whose payload is malformed -- text with no terminator, a body
 * shorter than its op requires -- stops the read rather than inventing a
 * fact.  @return 1 with @p out filled, 0 at the end or on malformation.
 */
int tiku_dl_read(const void *buf, size_t len, size_t *at,
                 tiku_dl_fact_t *out);

/**
 * @brief Say what the stream shows, one fact per line, into @p out.
 *
 * Paint is skipped: the narration is what a reader could ACT on -- the
 * words, the controls with their labels and states, the values.  This is
 * the window narrated from the wire alone, no pixels consulted, which is
 * the claim the whole stream exists to make good.
 *
 * @return the length written (truncated to @p max - 1 when it must be).
 */
size_t tiku_dl_narrate(const void *buf, size_t len, char *out, size_t max);

/**
 * @brief Read a list out of @p buf.
 *
 * Every length is checked against what is left of the buffer before it is
 * believed, and text is refused unless it ends where it says it does.
 * These bytes arrive from a wire.
 *
 * @return a list the caller frees, or NULL if the bytes are not one.
 */
tiku_dl_t *tiku_dl_unflatten(const void *buf, size_t len, size_t *read);

/**
 * @brief Draw it into @p s.
 *
 * @return how many commands were carried out; commands whose op this
 *         build does not know are stepped over and not counted.
 */
int tiku_dl_play(const tiku_dl_t *dl, tiku_surface_t *s);

#endif /* TIKU_DL_H_ */
