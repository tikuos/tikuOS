/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * fontfuzz.c - bend real fonts and see what the reader does.
 *
 * A DEVELOPMENT tool, not part of the build or the suite.  Fonts arrive
 * by being dropped in a folder, which is to say from anywhere, and a
 * charstring is a program: this takes every face on the machine, cuts it
 * short twelve ways and bends its bytes twelve more, then draws every
 * letter of each.  It found a crash the suite could not have: a single
 * byte of an 88 KB file, three bytes in the whole file reaching it.
 *
 * Build it against the sanitisers, which is the point of it:
 *
 *   cc -g -fsanitize=address,undefined -I../../kits/interface \
 *      -o fontfuzz fontfuzz.c ../../kits/interface/tiku_ttf.c \
 *      ../../kits/interface/tiku_cff.c \
 *      ../../kits/interface/tiku_glyphpath.c
 *   ./fontfuzz $(find /usr/share/fonts -name '*.ttf' -o -name '*.otf')
 *
 * The seed is fixed, so a failure is one anybody can have again.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_ttf.h"

static unsigned long seed = 12345u;
static unsigned long rnd(void){ seed = seed*6364136223846793005ul + 1442695040888963407ul; return seed >> 33; }

static int try_file(const char *path, int *drew) {
    tiku_ttf_t *t = tiku_ttf_open(path);
    unsigned cp;
    if (!t) return 0;
    for (cp = 32; cp < 0x300; cp++) {
        tiku_ttf_glyph_t g;
        if (tiku_ttf_render(t, cp, 14, &g)) { if (g.cover) (*drew)++; tiku_ttf_free_glyph(&g); }
    }
    for (cp = 0x4e00; cp < 0x4e40; cp++) {
        tiku_ttf_glyph_t g;
        if (tiku_ttf_render(t, cp, 14, &g)) { if (g.cover) (*drew)++; tiku_ttf_free_glyph(&g); }
    }
    tiku_ttf_close(t);
    return 1;
}

int main(int argc, char **argv) {
    int i, opened = 0, drew = 0, cases = 0;
    for (i = 1; i < argc; i++) {
        unsigned char *whole; long size; FILE *f = fopen(argv[i], "rb"); int k;
        if (!f) continue;
        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        whole = malloc((size_t)size);
        if (!whole || fread(whole, 1, (size_t)size, f) != (size_t)size) { free(whole); fclose(f); continue; }
        fclose(f);
        for (k = 1; k <= 24; k++) {
            char tmp[] = "/tmp/trk_fuzzXXXXXX";
            int fd = mkstemp(tmp); FILE *w; long keep;
            unsigned char *copy;
            if (fd < 0) continue;
            copy = malloc((size_t)size);
            memcpy(copy, whole, (size_t)size);
            if (k <= 12) {                       /* truncation */
                keep = size * k / 13;
            } else {                             /* bytes bent in place */
                int b;
                keep = size;
                for (b = 0; b < 40; b++) copy[rnd() % (unsigned long)size] = (unsigned char)(rnd() & 0xff);
            }
            w = fdopen(fd, "wb");
            if (w) { fwrite(copy, 1, (size_t)keep, w); fclose(w); opened += try_file(tmp, &drew); cases++; }
            free(copy);
            remove(tmp);
        }
        free(whole);
    }
    printf("bent %d copies of real fonts; %d still opened; %ld glyphs drawn; no crash\n",
           cases, opened, (long)drew);
    return 0;
}
