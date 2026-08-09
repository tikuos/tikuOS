/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_panel.c - "panel" command: drive the parallel RGB display.
 *
 * The gate for the display bring-up is a person seeing the right colour, so
 * this exists to put one on the glass rather than to report a number.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_panel.h"

#include "../tiku_shell_config.h"

#if TIKU_SHELL_CMD_PANEL

#include "../tiku_shell_io.h"
#include <string.h>
#include <arch/ra8p1/tiku_glcdc_arch.h>
#include <arch/ra8p1/tiku_cache_arch.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/timers/tiku_clock.h>

#define PANEL_PIX  (TIKU_GLCDC_PANEL_W * TIKU_GLCDC_PANEL_H)

static uint16_t *panel_fb;
static uint8_t   panel_up;

/** @brief Named colours, so the gate is "is it red" rather than a hex dump. */
static const struct { const char *name; uint16_t rgb; } panel_colours[] = {
    { "red",   0xF800U }, { "green", 0x07E0U }, { "blue",  0x001FU },
    { "white", 0xFFFFU }, { "black", 0x0000U },
};

/**
 * @brief Claim the framebuffer from the SRAM tier on first use.
 *
 * @return Non-zero on success
 */
static int
panel_claim(void)
{
    static tiku_arena_t arena;

    if (panel_fb != 0) {
        return 1;
    }
    if (tiku_tier_init() != TIKU_MEM_OK) {
        return 0;
    }
    if (tiku_tier_arena_create(&arena, TIKU_MEM_SRAM,
                               PANEL_PIX * 2U, 70) != TIKU_MEM_OK) {
        return 0;
    }
    panel_fb = (uint16_t *)tiku_arena_alloc(&arena, PANEL_PIX * 2U);
    return panel_fb != 0;
}

void
tiku_shell_cmd_panel(int argc, char **argv)
{
    uint16_t colour = 0xF800U;
    uint32_t i;
    unsigned c;

    if (!panel_claim()) {
        SHELL_PRINTF("panel: no room for a %ux%u framebuffer\r\n",
                     (unsigned)TIKU_GLCDC_PANEL_W,
                     (unsigned)TIKU_GLCDC_PANEL_H);
        return;
    }

    if (argc > 1) {
        for (c = 0U; c < (sizeof panel_colours / sizeof panel_colours[0]); c++) {
            if (strcmp(argv[1], panel_colours[c].name) == 0) {
                colour = panel_colours[c].rgb;
                break;
            }
        }
        if (c == (sizeof panel_colours / sizeof panel_colours[0])) {
            SHELL_PRINTF("panel: red green blue white black\r\n");
            return;
        }
    }

    for (i = 0U; i < PANEL_PIX; i++) {
        panel_fb[i] = colour;
    }
    /* The controller reads memory, not the CPU's cache. */
    tiku_ra8p1_dcache_clean(panel_fb, PANEL_PIX * 2U);

    if (!panel_up) {
        int rc = tiku_glcdc_arch_panel_start(panel_fb);

        if (rc != TIKU_GLCDC_OK) {
            SHELL_PRINTF("panel: start failed (%d)\r\n", rc);
            return;
        }
        panel_up = 1U;
    }
    /* Measured, not derived: the divider table is ambiguous enough that the
     * only trustworthy statement of the refresh rate is counting frames. */
    {
        tiku_clock_time_t end = tiku_clock_time() + TIKU_CLOCK_SECOND;
        uint32_t frames = 0U;

        (void)tiku_glcdc_arch_vpos_take();
        while ((long)(end - tiku_clock_time()) > 0) {
            if (tiku_glcdc_arch_vpos_take()) {
                frames++;
            }
        }
        SHELL_PRINTF("panel: %ux%u, colour %04x, underflow %d, %lu fps\r\n",
                     (unsigned)TIKU_GLCDC_PANEL_W,
                     (unsigned)TIKU_GLCDC_PANEL_H,
                     (unsigned)colour, tiku_glcdc_arch_underflow(),
                     (unsigned long)frames);
    }
}

#endif /* TIKU_SHELL_CMD_PANEL */
