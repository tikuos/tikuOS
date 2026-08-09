/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_panel.c - "panel" command: drive the parallel RGB display.
 *
 * Everything here goes through interfaces/display, so the command exercises
 * the same path a portable caller would rather than the RA8P1 registers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_panel.h"

#include "../tiku_shell_config.h"

#if TIKU_SHELL_CMD_PANEL

#include "../tiku_shell_io.h"
#include <string.h>
#include <interfaces/display/tiku_display.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>

static tiku_display_t panel_disp;
static uint8_t        panel_up;

/** @brief Named colours, so the gate is "is it red" rather than a hex dump. */
static const struct { const char *name; uint32_t argb; } panel_colours[] = {
    { "red",   0x00FF0000u }, { "green", 0x0000FF00u },
    { "blue",  0x000000FFu }, { "white", 0x00FFFFFFu },
    { "black", 0x00000000u },
};

/*
 * An 8x8 cell per glyph, one bit per pixel, high bit leftmost.  Only the
 * letters this command draws are carried; a full font belongs in a kit, not
 * in a shell command.
 */
static const char panel_text[] = "TikuOS";
static const uint8_t panel_glyphs[6][8] = {
    { 0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },  /* T */
    { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 },  /* i */
    { 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00 },  /* k */
    { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00 },  /* u */
    { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },  /* O */
    { 0x3E, 0x60, 0x60, 0x3C, 0x06, 0x06, 0x7C, 0x00 },  /* S */
};

/**
 * @brief Claim a framebuffer of the screen's own size.
 *
 * @return Base address, or NULL when no tier has room
 */
static void *
panel_claim(void)
{
    static tiku_arena_t arena;
    static void *fb;
    uint16_t w, h;
    uint32_t bytes;

    if (fb != 0) {
        return fb;
    }
    tiku_display_geometry(&w, &h);
    bytes = (uint32_t)w * h * tiku_display_bpp();

    if (tiku_tier_init() != TIKU_MEM_OK ||
        tiku_tier_arena_create(&arena, TIKU_MEM_SRAM, bytes, 70)
            != TIKU_MEM_OK) {
        return 0;
    }
    fb = tiku_arena_alloc(&arena, bytes);
    return fb;
}

/**
 * @brief Bring the screen up once, on a framebuffer from the tier.
 *
 * @return Non-zero when the screen is ready to draw on
 */
static int
panel_ready(void)
{
    uint16_t w, h;
    void *fb;

    if (panel_up) {
        return 1;
    }
    fb = panel_claim();
    if (fb == 0) {
        return 0;
    }
    tiku_display_geometry(&w, &h);
    if (tiku_display_init(&panel_disp, fb, w, h) != TIKU_DISPLAY_OK) {
        return 0;
    }
    panel_up = 1u;
    return 1;
}

/**
 * @brief Draw the name in blocks, one rectangle per run of set bits.
 *
 * @param scale   Pixels per glyph bit
 * @param colour  Ink colour as ARGB8888
 * @return Rectangles drawn
 */
static uint32_t
panel_draw_name(uint16_t scale, uint32_t colour)
{
    const uint32_t nchar = sizeof panel_text - 1u;
    const uint32_t cell = 8u;
    uint32_t textw = nchar * cell * scale;
    uint32_t texth = cell * scale;
    int32_t ox = ((int32_t)panel_disp.w - (int32_t)textw) / 2;
    int32_t oy = ((int32_t)panel_disp.h - (int32_t)texth) / 2;
    uint32_t g, row, col, rects = 0u;

    for (g = 0u; g < nchar; g++) {
        for (row = 0u; row < cell; row++) {
            uint8_t bits = panel_glyphs[g][row];

            col = 0u;
            while (col < cell) {
                uint32_t run;

                if ((bits & (0x80u >> col)) == 0u) {
                    col++;
                    continue;
                }
                /* One rectangle per horizontal run, not per bit: the engine
                 * is quick but each render still costs a register setup. */
                run = 0u;
                while ((col + run) < cell &&
                       (bits & (0x80u >> (col + run))) != 0u) {
                    run++;
                }
                (void)tiku_display_fill_rect(&panel_disp,
                        (int16_t)(ox + (int32_t)((g * cell + col) * scale)),
                        (int16_t)(oy + (int32_t)(row * scale)),
                        (uint16_t)(run * scale), scale, colour);
                rects++;
                col += run;
            }
        }
    }
    return rects;
}

void
tiku_shell_cmd_panel(int argc, char **argv)
{
    uint32_t colour = 0x00FF0000u;
    unsigned c;

    if (!panel_ready()) {
        SHELL_PRINTF("panel: no framebuffer\r\n");
        return;
    }

    if (argc > 1 && strcmp(argv[1], "text") == 0) {
        uint32_t rects;

        (void)tiku_display_clear(&panel_disp, 0x00101820u);
        rects = panel_draw_name(10u, 0x0000E0FFu);
        (void)tiku_display_flush(&panel_disp);
        SHELL_PRINTF("panel: \"%s\" in %lu rectangles\r\n",
                     panel_text, (unsigned long)rects);
        return;
    }
    if (argc > 1 && strcmp(argv[1], "circle") == 0) {
        (void)tiku_display_clear(&panel_disp, 0x00101820u);
        (void)tiku_display_fill_circle(&panel_disp,
                                       (int16_t)(panel_disp.w / 2u),
                                       (int16_t)(panel_disp.h / 2u),
                                       200u, 0x00FF8000u);
        (void)tiku_display_flush(&panel_disp);
        SHELL_PRINTF("panel: circle\r\n");
        return;
    }
    if (argc > 1) {
        for (c = 0u; c < (sizeof panel_colours / sizeof panel_colours[0]);
             c++) {
            if (strcmp(argv[1], panel_colours[c].name) == 0) {
                colour = panel_colours[c].argb;
                break;
            }
        }
        if (c == (sizeof panel_colours / sizeof panel_colours[0])) {
            SHELL_PRINTF("panel: red green blue white black text circle\r\n");
            return;
        }
    }

    (void)tiku_display_clear(&panel_disp, colour);
    (void)tiku_display_flush(&panel_disp);
    SHELL_PRINTF("panel: %ux%u colour %06lx\r\n",
                 (unsigned)panel_disp.w, (unsigned)panel_disp.h,
                 (unsigned long)colour);
}

#endif /* TIKU_SHELL_CMD_PANEL */
