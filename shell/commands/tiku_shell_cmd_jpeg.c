/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_jpeg.c - "jpeg" command: draw the test pattern (red across,
 * green down, a 16-pixel blue checker), optionally turn or scale it on the
 * PPA, encode it on the JPEG codec, and hand the file over in base64.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <stdint.h>
#include <string.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_jpeg.h"
#include "tiku_shell_cmd_util.h"
#include <kernel/memory/tiku_mem.h>
#include <arch/esp32p4/tiku_jpeg_arch.h>
#include <arch/esp32p4/tiku_ppa_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>

/* PSRAM, taken at the first use and kept: the pattern, the picture after
 * the PPA, the file, the file decoded back. */
#define REGION      (1024u * 1024u)
#define ARENA_ID    78u

static tiku_arena_t arena;
static uint8_t have_arena;
static tiku_p4_picture_t source;            /* what was encoded */
static uint32_t file_len;

static uint8_t *region(unsigned n)
{
    return (uint8_t *)arena.buf + n * REGION;
}

static uint64_t now16(void)
{
    uint64_t t = 0u;
    (void)tiku_p4_systimer_read(&t);
    return t;
}

/** @brief Draw the pattern into @p p: B, G, R bytes, RGB565, or grey. */
static void draw(const tiku_p4_picture_t *p)
{
    uint32_t x, y, r, g, b;

    for (y = 0u; y < p->height; y++) {
        for (x = 0u; x < p->width; x++) {
            uint8_t *q = (uint8_t *)p->base + (y * p->width + x) * p->bpp;
            r = (x * 255u) / (p->width - 1u);
            g = (y * 255u) / (p->height - 1u);
            b = (((x >> 4) ^ (y >> 4)) & 1u) ? 255u : 64u;
            if (p->bpp == 3u) {
                q[0] = (uint8_t)b;
                q[1] = (uint8_t)g;
                q[2] = (uint8_t)r;
            } else if (p->bpp == 2u) {
                uint32_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                q[0] = (uint8_t)v;
                q[1] = (uint8_t)(v >> 8);
            } else {
                q[0] = (uint8_t)((299u * r + 587u * g + 114u * b + 500u) /
                                 1000u);
            }
        }
    }
}

static void jpeg_pattern(uint8_t argc, const char *argv[])
{
    tiku_p4_picture_t pat;
    uint32_t w = tiku_cmd_parse_u32(argv[2]), h = tiku_cmd_parse_u32(argv[3]);
    uint32_t sampling = TIKU_P4_JPEG_420, quality = 85u, turn = 0u, scale = 16u;
    uint8_t bpp = 3u, i = 4u;
    uint16_t ow, oh;
    uint64_t t0, t1;
    int rc;

    if (i < argc && tiku_cmd_streq(argv[i], "rgb565")) {
        bpp = 2u;
        i++;
    } else if (i < argc && tiku_cmd_streq(argv[i], "grey")) {
        bpp = 1u;
        i++;
    } else if (i < argc && tiku_cmd_streq(argv[i], "rgb888")) {
        i++;
    }
    if (i < argc && (tiku_cmd_streq(argv[i], "444") ||
                     tiku_cmd_streq(argv[i], "422") ||
                     tiku_cmd_streq(argv[i], "420"))) {
        sampling = argv[i][1] == '4' ? TIKU_P4_JPEG_444
                 : argv[i][2] == '2' ? TIKU_P4_JPEG_422 : TIKU_P4_JPEG_420;
        i++;
    }
    if (i < argc && argv[i][0] >= '0' && argv[i][0] <= '9') {
        quality = tiku_cmd_parse_u32(argv[i++]);
    }
    for (; i + 1u < argc; i += 2u) {
        if (tiku_cmd_streq(argv[i], "turn")) {
            turn = tiku_cmd_parse_u32(argv[i + 1u]);
        } else if (tiku_cmd_streq(argv[i], "scale")) {
            scale = tiku_cmd_parse_u32(argv[i + 1u]);
        }
    }
    file_len = 0u;
    if (w < 2u || h < 2u || w * h * bpp > REGION) {
        SHELL_PRINTF("jpeg: the pattern is 2x2 to %lu bytes\n",
                     (unsigned long)REGION);
        return;
    }
    if (!have_arena) {
        if (tiku_tier_arena_create_span(&arena, TIKU_MEM_PSRAM, 0,
                                        4u * REGION, 64u, ARENA_ID) !=
            TIKU_MEM_OK) {
            SHELL_PRINTF("jpeg: no 4 MB of PSRAM\n");
            return;
        }
        have_arena = 1u;
    }
    pat.base = region(0);
    pat.width = (uint16_t)w;
    pat.height = (uint16_t)h;
    pat.bpp = bpp;
    draw(&pat);
    source = pat;
    if ((turn != 0u || scale != 16u) && bpp == 1u) {
        SHELL_PRINTF("jpeg: the PPA turns and scales colour pictures only\n");
        return;
    }
    if (turn != 0u || scale != 16u) {
        tiku_p4_ppa_srm_size(pat.width, pat.height, (uint16_t)scale,
                             (uint16_t)scale, turn, &ow, &oh);
        source.base = region(1);
        source.width = ow;
        source.height = oh;
        if ((uint32_t)ow * oh * bpp > REGION) {
            SHELL_PRINTF("jpeg: the turned or scaled picture is over %lu "
                         "bytes\n", (unsigned long)REGION);
            return;
        }
        rc = tiku_p4_ppa_srm(&source, 0u, 0u, &pat, 0u, 0u, pat.width,
                             pat.height, (uint16_t)scale, (uint16_t)scale,
                             turn, 0u);
        if (rc != TIKU_P4_PPA_OK) {
            SHELL_PRINTF("jpeg: ppa %d\n", rc);
            return;
        }
    }
    t0 = now16();
    rc = tiku_p4_jpeg_encode(&source, sampling, quality, region(2), REGION,
                             &file_len);
    t1 = now16();
    if (rc != TIKU_P4_JPEG_OK) {
        SHELL_PRINTF("jpeg: encode %d (codec %lx)\n", rc,
                     (unsigned long)tiku_p4_jpeg_errors());
        return;
    }
    SHELL_PRINTF("jpeg: %ux%u %s %s q%lu: %lu bytes in %lu us\n",
                 source.width, source.height,
                 bpp == 3u ? "rgb888" : bpp == 2u ? "rgb565" : "grey",
                 bpp == 1u ? "grey" : sampling == TIKU_P4_JPEG_444 ? "4:4:4"
                 : sampling == TIKU_P4_JPEG_422 ? "4:2:2" : "4:2:0",
                 (unsigned long)quality, (unsigned long)file_len,
                 (unsigned long)((t1 - t0) / 16u));
}

static void jpeg_dump(void)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *f = region(2);
    char line[77];
    uint32_t i, n = 0u;

    SHELL_PRINTF("jpeg: %lu bytes\n", (unsigned long)file_len);
    for (i = 0u; i < file_len; i += 3u) {
        uint32_t v = (uint32_t)f[i] << 16;
        unsigned k = file_len - i;

        v |= k > 1u ? (uint32_t)f[i + 1u] << 8 : 0u;
        v |= k > 2u ? f[i + 2u] : 0u;
        line[n++] = b64[(v >> 18) & 63u];
        line[n++] = b64[(v >> 12) & 63u];
        line[n++] = k > 1u ? b64[(v >> 6) & 63u] : '=';
        line[n++] = k > 2u ? b64[v & 63u] : '=';
        if (n == 76u || i + 3u >= file_len) {
            line[n] = '\0';
            SHELL_PRINTF("%s\n", line);
            n = 0u;
        }
    }
    SHELL_PRINTF("jpeg: end\n");
}

static void jpeg_check(void)
{
    tiku_p4_jpeg_info_t info;
    tiku_p4_picture_t back;
    uint64_t err = 0u, t0, t1;
    uint32_t i, n;
    int rc;

    rc = tiku_p4_jpeg_info(region(2), file_len, &info);
    if (rc != TIKU_P4_JPEG_OK) {
        SHELL_PRINTF("jpeg: info %d\n", rc);
        return;
    }
    back.base = region(3);
    back.width = info.pad_width;
    back.height = info.pad_height;
    back.bpp = source.bpp;
    t0 = now16();
    rc = tiku_p4_jpeg_decode(region(2), file_len, &back);
    t1 = now16();
    if (rc != TIKU_P4_JPEG_OK) {
        SHELL_PRINTF("jpeg: decode %d (codec %lx)\n", rc,
                     (unsigned long)tiku_p4_jpeg_errors());
        return;
    }
    /* The sides are whole MCUs, so the decoded picture is the source's;
     * RGB565 is compared a channel at a time. */
    if (source.bpp == 2u) {
        const uint16_t *a = back.base, *b = source.base;
        n = (uint32_t)source.width * source.height;
        for (i = 0u; i < n; i++) {
            int32_t dr = (int32_t)(a[i] >> 11) - (int32_t)(b[i] >> 11);
            int32_t dg = (int32_t)((a[i] >> 5) & 63u) -
                         (int32_t)((b[i] >> 5) & 63u);
            int32_t db = (int32_t)(a[i] & 31u) - (int32_t)(b[i] & 31u);
            err += (uint64_t)(64 * dr * dr + 16 * dg * dg + 64 * db * db);
        }
        n *= 3u;
    } else {
        n = (uint32_t)source.width * source.height * source.bpp;
        for (i = 0u; i < n; i++) {
            int32_t d = (int32_t)((uint8_t *)back.base)[i] -
                        (int32_t)((uint8_t *)source.base)[i];
            err += (uint64_t)(d * d);
        }
    }
    SHELL_PRINTF("jpeg: decoded %ux%u in %lu us, mse %lu\n", back.width,
                 back.height, (unsigned long)((t1 - t0) / 16u),
                 (unsigned long)(err / n));
}

void tiku_shell_cmd_jpeg(uint8_t argc, const char *argv[])
{
    if (argc >= 4u && tiku_cmd_streq(argv[1], "pattern")) {
        jpeg_pattern(argc, argv);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "dump") && file_len) {
        jpeg_dump();
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "check") && file_len) {
        jpeg_check();
    } else {
        SHELL_PRINTF("Usage: jpeg pattern <w> <h> [rgb888|rgb565|grey] "
                     "[444|422|420] [quality] [turn <0-3>] [scale <n/16>]\n"
                     "       jpeg dump | check   (the last file)\n");
    }
}
