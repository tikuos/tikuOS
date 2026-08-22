/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * glyphdump.c - what our rasteriser makes of one glyph.
 *
 * A DEVELOPMENT tool, not part of the build or the suite: it prints a
 * glyph's metrics and coverage so tools/compare_freetype.py can hold them
 * against what freetype makes of the same glyph.  We ship no font library
 * and the suite depends on none; this is how we check our own work
 * against one anyway, on a machine that happens to have it.
 *
 *   cc -I../src -o glyphdump glyphdump.c ../build/libtiku_desk_ui.a \
 *      ../build/libtiku_desk_runtime.a -lm
 *   ./glyphdump <font-file> <px> <codepoint-hex> [<codepoint-hex>...]
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_ttf.h"

int
main(int argc, char **argv)
{
    tiku_ttf_t *ttf;
    int px, i;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <font> <px> <cp-hex>...\n", argv[0]);
        return 2;
    }
    ttf = tiku_ttf_open(argv[1]);
    if (ttf == NULL) {
        printf("REFUSED %s\n", argv[1]);
        return 1;
    }
    px = atoi(argv[2]);
    printf("FAMILY %s\n", tiku_ttf_family(ttf));
    {
        int ascent = 0, height = 0;

        tiku_ttf_metrics(ttf, px, &ascent, &height);
        printf("METRICS %d %d\n", ascent, height);
    }
    for (i = 3; i < argc; i++) {
        unsigned cp = (unsigned)strtoul(argv[i], NULL, 16);
        tiku_ttf_glyph_t g;
        int x, y;

        memset(&g, 0, sizeof g);
        if (!tiku_ttf_render(ttf, cp, px, &g)) {
            printf("GLYPH %x MISSING\n", cp);
            continue;
        }
        printf("GLYPH %x %d %d %d %d %d\n", cp, g.adv, g.w, g.h, g.ox, g.oy);
        for (y = 0; y < g.h; y++) {
            for (x = 0; x < g.w; x++) {
                printf("%d%s", g.cover[y * g.w + x],
                       (x + 1 < g.w) ? " " : "");
            }
            printf("\n");
        }
        tiku_ttf_free_glyph(&g);
    }
    tiku_ttf_close(ttf);
    return 0;
}
