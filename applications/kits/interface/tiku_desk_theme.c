/*
 * The desktop toolkit for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_theme.c - the one table every semantic colour reads through.
 *
 * The default is the BeOS R5 look, and it is a DEFAULT rather than a
 * constant: an application (or one day the system) hands in another table
 * and every widget recolours, because nothing paints a semantic colour
 * except through here.  Layout never moves with the theme -- that is the
 * whole bargain (the Appearance Manager lesson).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desk_gfx.h"

static const tiku_desk_theme_t tiku_desk_theme_r5 = {
    /* panel    */ TIKU_DESK_RGB(216, 216, 216),
    /* doc      */ TIKU_DESK_RGB(255, 255, 255),
    /* text     */ TIKU_DESK_RGB(0, 0, 0),
    /* tab      */ TIKU_DESK_RGB(255, 203, 0),
    /* tab_idle */ TIKU_DESK_RGB(232, 232, 232),
    /* select   */ TIKU_DESK_RGB(51, 102, 152),
    /* seltext  */ TIKU_DESK_RGB(255, 255, 255),
    /* focus    */ TIKU_DESK_RGB(0, 0, 229),
    /* backdrop */ TIKU_DESK_RGB(51, 102, 152),
    /* danger   */ TIKU_DESK_RGB(203, 48, 48),
    /* note     */ TIKU_DESK_RGB(255, 255, 200),
    /* tab_text */ TIKU_DESK_RGB(0, 0, 0),
};

/*
 * Dusk, not negation: the surfaces darken and the ink lightens, and the
 * identity survives in shape and accent rather than in brightness -- the
 * tab keeps its AMBER, deepened so a dark screen carries no glowing bar
 * (these surfaces live on devices, where a bright strip is a torch), the
 * selection keeps its Be blue, and every derived bevel follows
 * automatically because bevels are tints of the panel.
 */
static const tiku_desk_theme_t tiku_desk_theme_dusk = {
    /* panel    */ TIKU_DESK_RGB(58, 58, 58),
    /* doc      */ TIKU_DESK_RGB(30, 30, 30),
    /* text     */ TIKU_DESK_RGB(222, 222, 222),
    /* tab      */ TIKU_DESK_RGB(110, 88, 12),
    /* tab_idle */ TIKU_DESK_RGB(70, 70, 70),
    /* select   */ TIKU_DESK_RGB(66, 113, 166),
    /* seltext  */ TIKU_DESK_RGB(255, 255, 255),
    /* focus    */ TIKU_DESK_RGB(96, 96, 255),
    /* backdrop */ TIKU_DESK_RGB(28, 42, 58),
    /* danger   */ TIKU_DESK_RGB(224, 82, 82),
    /* note     */ TIKU_DESK_RGB(72, 72, 46),
    /* tab_text */ TIKU_DESK_RGB(235, 221, 170),
};

const tiku_desk_theme_t *
tiku_desk_theme_dark(void)
{
    return &tiku_desk_theme_dusk;
}

static const tiku_desk_theme_t *tiku_desk_theme_live = &tiku_desk_theme_r5;

const tiku_desk_theme_t *
tiku_desk_theme(void)
{
    return tiku_desk_theme_live;
}

void
tiku_desk_theme_set(const tiku_desk_theme_t *theme)
{
    tiku_desk_theme_live = (theme != NULL) ? theme : &tiku_desk_theme_r5;
}
