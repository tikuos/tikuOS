/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gallery.c - every control in every state, on one surface.
 *
 * S1's exit test: this image is what gets held against an R5 screenshot, so it
 * shows the states side by side rather than a pretty arrangement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_ui.h"
#include "tiku_tabs.h"
#include "tiku_slider.h"
#include "tiku_alert.h"
#include "tiku_syntax.h"

#ifndef TIKU_NO_X11
int tiku_x11_show(const tiku_surface_t *s, const char *title);
#endif

static const char *const MENUS[] = { "File", "Edit", "View", "Window" };
static const char *const MENUITEMS[] = {
    "New Window", "Open...", "-", "Get Info", "Duplicate", "-", "Close"
};
static const char *const COLS[] = { "Name", "Type", "Value" };
static const int COLW[] = { 150, 90, 110 };
static const char *const ROWS[][3] = {
    { "uptime",     "u32",  "27996"    },
    { "boot_count", "u32",  "182"      },
    { "led0",       "bool", "1"        },
    { "state",      "text", "up"       },
    { "nvmfree",    "u32",  "32768"    },
};

/** @brief Draw the gallery into @p s. */
static void
gallery(tiku_surface_t *s)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t win, c;
    int y;

    tiku_fill(s, (tiku_rect_t){ 0, 0, s->w, s->h },
                   TIKU_C_BACKDROP);

    win = (tiku_rect_t){ 20, 20, s->w - 40, s->h - 40 };
    c = tiku_ui_window(s, win, "Widget Gallery", 1);

    tiku_ui_menubar(s, (tiku_rect_t){ c.x, c.y, c.w, 18 },
                         MENUS, 4, 1);
    y = c.y + 30;

    /* Buttons: normal, pressed, default, focused, disabled. */
    tiku_text(s, f, c.x, y + f->ascent, "Buttons", TIKU_C_TEXT);
    y += 20;
    tiku_ui_button(s, (tiku_rect_t){ c.x, y, 80, 24 }, "Cancel",
                        TIKU_S_NORMAL);
    tiku_ui_button(s, (tiku_rect_t){ c.x + 90, y, 80, 24 },
                        "Pressed", TIKU_S_PRESSED);
    tiku_ui_button(s, (tiku_rect_t){ c.x + 180, y - 3, 86, 30 },
                        "Default", TIKU_S_DEFAULT);
    tiku_ui_button(s, (tiku_rect_t){ c.x + 276, y, 80, 24 },
                        "Focused", TIKU_S_FOCUS);
    tiku_ui_button(s, (tiku_rect_t){ c.x + 366, y, 80, 24 },
                        "Disabled", TIKU_S_DISABLED);
    y += 40;

    /* Checkboxes and radios. */
    tiku_text(s, f, c.x, y + f->ascent, "Controls", TIKU_C_TEXT);
    y += 20;
    tiku_ui_checkbox(s, (tiku_rect_t){ c.x, y, 120, 16 },
                          "Show hidden", TIKU_S_ON);
    tiku_ui_checkbox(s, (tiku_rect_t){ c.x + 130, y, 120, 16 },
                          "Live update", TIKU_S_NORMAL);
    tiku_ui_checkbox(s, (tiku_rect_t){ c.x + 260, y, 120, 16 },
                          "Focused", TIKU_S_FOCUS | TIKU_S_ON);
    y += 22;
    tiku_ui_radio(s, (tiku_rect_t){ c.x, y, 100, 16 },
                       "Icon view", TIKU_S_ON);
    tiku_ui_radio(s, (tiku_rect_t){ c.x + 130, y, 100, 16 },
                       "List view", TIKU_S_NORMAL);
    tiku_ui_radio(s, (tiku_rect_t){ c.x + 260, y, 100, 16 },
                       "Disabled", TIKU_S_DISABLED);
    y += 32;

    /* Text fields. */
    tiku_ui_textfield(s, (tiku_rect_t){ c.x, y, 200, 20 },
                           "/sys/device/name", -1, TIKU_S_NORMAL);
    tiku_ui_textfield(s, (tiku_rect_t){ c.x + 210, y, 160, 20 },
                           "tiku", 4, TIKU_S_FOCUS);
    tiku_ui_button(s, (tiku_rect_t){ c.x + 380, y - 2, 66, 24 },
                        "Write", TIKU_S_NORMAL);
    y += 34;

    /* A list view with headers, a selection and both scrollbars. */
    {
        tiku_rect_t lv = { c.x, y, 350, 130 };
        tiku_rect_t body = { lv.x, lv.y + 18, lv.w - 15, lv.h - 18 - 15 };
        int rowh = f->height + 4, i;

        tiku_ui_list_header(s, (tiku_rect_t){ lv.x, lv.y,
                                                        lv.w - 15, 18 },
                                 COLS, COLW, 3, 0);
        tiku_ui_sunken(s, body, TIKU_C_DOC);
        tiku_clip_set(s, tiku_inset(body, 2));
        for (i = 0; i < 5; i++) {
            tiku_rect_t row = { body.x + 2, body.y + 2 + i * rowh,
                                     body.w - 4, rowh };
            int sel = (i == 2);
            int cx = row.x, k;

            tiku_fill(s, row, sel ? TIKU_C_SELECT
                                       : TIKU_C_DOC);
            for (k = 0; k < 3; k++) {
                tiku_text(s, f, cx + 5,
                               row.y + (rowh - f->height) / 2 + f->ascent,
                               ROWS[i][k],
                               sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
                cx += COLW[k];
            }
        }
        tiku_clip_reset(s);
        tiku_ui_scrollbar(s, (tiku_rect_t){ lv.x + lv.w - 15,
                                                      lv.y + 18, 15,
                                                      lv.h - 18 - 15 },
                               0.25f, 0.55f, 0);
        tiku_ui_scrollbar(s, (tiku_rect_t){ lv.x, lv.y + lv.h - 15,
                                                      lv.w - 15, 15 },
                               0.0f, 0.7f, 1);
    }

    /* A tab strip: the current tab lit and open to the panel below it. */
    {
        static tiku_tabs_t tabs;
        tiku_rect_t strip = { c.x, y + 140, 350, 22 };
        tiku_rect_t below = { strip.x, strip.y + strip.h, strip.w, 30 };

        if (tiku_tabs_count(&tabs) == 0) {
            tiku_tabs_init(&tabs);
            (void)tiku_tabs_add(&tabs, "Information");
            (void)tiku_tabs_add(&tabs, "Permissions");
            (void)tiku_tabs_add(&tabs, "Attributes");
            (void)tiku_tabs_select(&tabs, 1);
        }
        tiku_tabs_draw(&tabs, s, strip);
        tiku_ui_sunken(s, below, TIKU_C_PANEL);
    }

    /* A gauge, a slider, a stepper and a tip: the quantities. */
    {
        static tiku_slider_t vol, num;
        tiku_rect_t g = { c.x + 380, y + 150, 150, 10 };
        tiku_rect_t sr = { c.x + 380, y + 168, 150, 16 };
        tiku_rect_t st = { c.x + 380, y + 192, 70, 20 };

        if (vol.max == 0) {
            tiku_slider_init(&vol, 0, 100, 65, 5);
            tiku_slider_init(&num, 1, 99, 12, 1);
        }
        tiku_ui_gauge(s, g, 0.4f);
        tiku_slider_draw(&vol, s, sr);
        tiku_stepper_draw(&num, s, st);
        {
            const char *tip = "the whole of what would not fit";
            tiku_rect_t tr = tiku_ui_tip_size(tip);

            tr.x = c.x + 380;
            tr.y = y + 218;
            tiku_ui_tip(s, tr, tip);
        }
    }

    /* A dropped menu, to show the panel and its highlight. */
    tiku_ui_menu(s, (tiku_rect_t){ c.x + 380, y + 4, 150,
                                             7 * (f->height + 6) + 4 },
                      MENUITEMS, 7, 3);

    /* The question: the kit's alert, drawn small over the corner. */
    {
        static tiku_alert_t ask;
        tiku_rect_t af = { c.x + 470, c.y + c.h - 130, 300, 110 };

        if (!ask.open) {
            tiku_alert_open(&ask, TIKU_ALERT_WARN, 0,
                            "Discard unsaved changes? "
                            "Two voices: summary bold, detail plain.",
                            "Cancel|Discard");
        }
        tiku_alert_draw(&ask, s, af);
    }

    /* A line of a program, so the code inks can be held against the
     * rest of the palette: the four meanings on the document ground,
     * beside the ordinary ink they have to stay apart from. */
    {
        static const char *const CODE[] = {
            "10 REM a line of each kind",
            "20 PRINT \"reserved, number, quoted, remark\"",
            "30 LET A = &HFF : GOTO done"
        };
        tiku_rect_t page = { c.x + 12, c.y + c.h - 96, 170, 86 };
        int k;

        tiku_ui_sunken(s, page, TIKU_C_DOC);
        for (k = 0; k < 3; k++) {
            tiku_span_t span[64];
            int n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, CODE[k], span,
                                      (int)(sizeof span / sizeof span[0]));

            (void)tiku_ui_text_spans(s, f, page.x + 4,
                                     page.y + 4 + k * (f->height + 2) +
                                         f->ascent,
                                     CODE[k], span, n);
        }
    }

    /* An inactive window, for the tab comparison. */
    {
        tiku_rect_t w2 = { c.x + 190, c.y + c.h - 96, 250, 86 };
        tiku_rect_t c2 = tiku_ui_window(s, w2, "Inactive", 0);
        tiku_text(s, f, c2.x + 6, c2.y + 4 + f->ascent,
                       "An unfocused window wears", TIKU_C_TEXT);
        tiku_text(s, f, c2.x + 6, c2.y + 4 + f->height + f->ascent,
                       "the grey tab.", TIKU_C_TEXT);
    }
}

int
main(int argc, char **argv)
{
    tiku_surface_t *s = tiku_surface_new(760, 560,
                                                   TIKU_C_BACKDROP);
    const char *png = NULL;
    int show = 1, i;

    if (s == NULL) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { png = argv[++i]; }
        else if (strcmp(argv[i], "-q") == 0)            { show = 0; }
    }
    gallery(s);

    if (png != NULL && tiku_surface_png(s, png) == 0) {
        printf("wrote %s (%dx%d)\n", png, s->w, s->h);
    }
#ifndef TIKU_NO_X11
    if (show) {
        (void)tiku_x11_show(s, "Tiku Desktop -- Widget Gallery");
    }
#else
    (void)show;
#endif
    tiku_surface_free(s);
    return 0;
}
