/*
 * The desktop toolkit for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_theme.c - the one table every semantic colour reads through.
 *
 * The default is the BeOS R5 look, and it is a DEFAULT rather than a
 * constant: an application (or one day the system) hands in another table
 * and every widget recolours, because nothing paints a semantic colour
 * except through here.  Layout never moves with the theme -- that is the
 * whole bargain (the Appearance Manager lesson).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"

static const tiku_theme_t tiku_theme_r5 = {
    /* panel    */ TIKU_RGB(216, 216, 216),
    /* doc      */ TIKU_RGB(255, 255, 255),
    /* text     */ TIKU_RGB(0, 0, 0),
    /* tab      */ TIKU_RGB(255, 203, 0),
    /* tab_idle */ TIKU_RGB(232, 232, 232),
    /* select   */ TIKU_RGB(51, 102, 152),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(0, 0, 229),
    /* backdrop */ TIKU_RGB(51, 102, 152),
    /* danger   */ TIKU_RGB(203, 48, 48),
    /* note     */ TIKU_RGB(255, 255, 200),
    /* tab_text */ TIKU_RGB(0, 0, 0),
};

/*
 * Dusk, not negation: the surfaces darken and the ink lightens, and the
 * identity survives in shape and accent rather than in brightness -- the
 * tab keeps its AMBER, deepened so a dark screen carries no glowing bar
 * (these surfaces live on devices, where a bright strip is a torch), the
 * selection keeps its Be blue, and every derived bevel follows
 * automatically because bevels are tints of the panel.
 */
static const tiku_theme_t tiku_theme_dusk = {
    /* panel    */ TIKU_RGB(58, 58, 58),
    /* doc      */ TIKU_RGB(30, 30, 30),
    /* text     */ TIKU_RGB(222, 222, 222),
    /* tab      */ TIKU_RGB(110, 88, 12),
    /* tab_idle */ TIKU_RGB(70, 70, 70),
    /* select   */ TIKU_RGB(66, 113, 166),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(96, 96, 255),
    /* backdrop */ TIKU_RGB(28, 42, 58),
    /* danger   */ TIKU_RGB(224, 82, 82),
    /* note     */ TIKU_RGB(72, 72, 46),
    /* tab_text */ TIKU_RGB(235, 221, 170),
};

const tiku_theme_t *
tiku_theme_dark(void)
{
    return &tiku_theme_dusk;
}

/*
 * No hue at all.  Not a filter over one of the tables above -- a table of
 * its own, because draining the colour out of a palette chosen FOR its
 * colour gives you the greys that happen to fall out, and several of them
 * land on top of each other: the Be blue and the amber tab are close in
 * luminance, so a desaturated desktop loses the difference between the
 * thing that is selected and the window that is active.  Chosen here
 * instead, by the job each role does.
 *
 * "Black and white" and not one bit deep: every bevel in this interface is
 * a TINT of the panel, so a two-value palette would flatten every control
 * in it -- the raised look would simply stop existing.  The greys are what
 * keeps the shapes, and no colour appears anywhere.
 */
static const tiku_theme_t tiku_theme_grey = {
    /* panel    */ TIKU_RGB(216, 216, 216),
    /* doc      */ TIKU_RGB(255, 255, 255),
    /* text     */ TIKU_RGB(0, 0, 0),
    /* tab      */ TIKU_RGB(140, 140, 140),
    /* tab_idle */ TIKU_RGB(232, 232, 232),
    /* select   */ TIKU_RGB(40, 40, 40),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(64, 64, 64),
    /* backdrop */ TIKU_RGB(112, 112, 112),
    /* danger   */ TIKU_RGB(64, 64, 64),
    /* note     */ TIKU_RGB(240, 240, 240),
    /* tab_text */ TIKU_RGB(0, 0, 0),
};

const tiku_theme_t *
tiku_theme_mono(void)
{
    return &tiku_theme_grey;
}

static const tiku_theme_t *tiku_theme_live = &tiku_theme_r5;
static int tiku_theme_live_achromatic;

/**
 * @brief Whether a table names no hue anywhere.
 *
 * DERIVED from the table rather than declared beside it, so there is no
 * second thing to keep in agreement: a palette is without hue when every
 * role in it is, and a table added later gets the right answer without
 * anybody remembering to say so.
 */
static int
achromatic(const tiku_theme_t *t)
{
    const tiku_rgb_t *role = (const tiku_rgb_t *)t;
    size_t n = sizeof *t / sizeof *role;
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned r = (role[i] >> 16) & 0xFFu;
        unsigned g = (role[i] >> 8) & 0xFFu;
        unsigned b = role[i] & 0xFFu;

        if (r != g || g != b) {
            return 0;
        }
    }
    return 1;
}

int
tiku_theme_achromatic(void)
{
    return tiku_theme_live_achromatic;
}

tiku_rgb_t
tiku_grey(tiku_rgb_t c)
{
    /* Rec. 601 luminance: the weights the eye actually uses, so a red and
     * a blue of the same nominal brightness do not come out the same
     * grey the way a plain average makes them. */
    unsigned r = (c >> 16) & 0xFFu;
    unsigned g = (c >> 8) & 0xFFu;
    unsigned b = c & 0xFFu;
    unsigned y = (299u * r + 587u * g + 114u * b) / 1000u;

    if (y > 255u) {
        y = 255u;
    }
    return TIKU_RGB(y, y, y);
}

const tiku_theme_t *
tiku_theme(void)
{
    return tiku_theme_live;
}

void
tiku_theme_set(const tiku_theme_t *theme)
{
    tiku_theme_live = (theme != NULL) ? theme : &tiku_theme_r5;
    tiku_theme_live_achromatic = achromatic(tiku_theme_live);
}
