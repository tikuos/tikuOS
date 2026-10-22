/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_h264.c - "h264" command: frames of a moving test pattern
 * (two ramps, a checker, a bright square 8 pixels on a frame) through the
 * ESP32-P4 encoder into one stream, its sizes and timing printed, the
 * stream dumped in base64 for the host to decode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <stdint.h>
#include <string.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_h264.h"
#include "tiku_shell_cmd_util.h"
#include <kernel/memory/tiku_mem.h>
#include <arch/esp32p4/tiku_h264_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>

#define REGION      (2u * 1024u * 1024u)
#define PS_ID       82u
#define SR_ID       83u
#define MAX_W       1280u
#define MAX_H       1024u
#define MAX_PIXELS  (1280u * 768u)
#define SQUARE      48u

static tiku_arena_t ps_arena, sr_arena;
static uint8_t have_ps;
static uint32_t sr_bytes;
static uint32_t stream_len, stream_frames, stream_idrs, stream_us;
static uint16_t stream_w, stream_h;

static uint8_t *region(unsigned n)
{
    return (uint8_t *)ps_arena.buf + n * REGION;
}

static uint64_t now16(void)
{
    uint64_t t = 0u;
    (void)tiku_p4_systimer_read(&t);
    return t;
}

/** @brief The pattern's luma at (@p x, @p y) of frame @p t. */
static uint8_t luma(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t t)
{
    uint32_t base = ((x * 255u) / (w - 1u) + (y * 255u) / (h - 1u)) / 2u;
    uint32_t v = 16u + base * 219u / 255u;
    uint32_t sx = (t * 8u) % (w - SQUARE), sy = (t * 4u) % (h - SQUARE);

    if (((x >> 4) ^ (y >> 4)) & 1u) {
        v = 16u + (v - 16u) * 3u / 4u;
    }
    if (x >= sx && x < sx + SQUARE && y >= sy && y < sy + SQUARE) {
        v = 235u;
    }
    return (uint8_t)v;
}

/** @brief Frame @p t as the encoder reads it: a chroma byte (U on even
 *         rows, V on odd) then two luma bytes for every pair of pixels. */
static void draw(uint8_t *buf, uint32_t w, uint32_t h, uint32_t t)
{
    uint32_t x, y;
    uint8_t *p = buf;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x += 2u) {
            *p++ = (y & 1u) ? (uint8_t)(80u + (y * 96u) / (h - 1u))
                            : (uint8_t)(80u + (x * 96u) / (w - 1u));
            *p++ = luma(x, y, w, h, t);
            *p++ = luma(x + 1u, y, w, h, t);
        }
    }
}

/** @brief The work areas for @p w x @p h; @return 0 when memory is short. */
static int areas(uint32_t w, uint32_t h, tiku_p4_h264_work_t *k)
{
    uint32_t need = tiku_p4_h264_sram_bytes((uint16_t)w);

    if (!have_ps) {
        if (tiku_tier_arena_create_span(&ps_arena, TIKU_MEM_PSRAM, 0,
                                        3u * REGION, 64u, PS_ID) !=
            TIKU_MEM_OK) {
            SHELL_PRINTF("h264: no 6 MB of PSRAM\n");
            return 0;
        }
        have_ps = 1u;
    }
    if (sr_bytes < need) {
        if (sr_bytes) {
            (void)tiku_arena_destroy(&sr_arena);
            sr_bytes = 0u;
        }
        if (tiku_tier_arena_create_span(&sr_arena, TIKU_MEM_SRAM, 0, need,
                                        64u, SR_ID) != TIKU_MEM_OK) {
            SHELL_PRINTF("h264: no %lu bytes of SRAM for the encoder\n",
                         (unsigned long)need);
            return 0;
        }
        sr_bytes = need;
    }
    k->sram = (uint8_t *)sr_arena.buf;
    k->sram_bytes = sr_bytes;
    k->store = region(1);
    k->store_bytes = tiku_p4_h264_store_bytes((uint16_t)w, (uint16_t)h);
    return 1;
}

static void encode(uint8_t argc, const char *argv[])
{
    tiku_p4_h264_cfg_t c;
    tiku_p4_h264_work_t k;
    uint32_t w, h, frames, i, len, idr_bytes = 0u, p_bytes = 0u, p_count = 0u;
    uint64_t t0;
    int key, rc;

    if (argc < 5u) {
        SHELL_PRINTF("usage: h264 encode <w> <h> <frames> [qp <n> | rate "
                     "<kbit/s>] [gop <n>]\n");
        return;
    }
    w = tiku_cmd_parse_u32(argv[2]);
    h = tiku_cmd_parse_u32(argv[3]);
    frames = tiku_cmd_parse_u32(argv[4]);
    c.width = (uint16_t)w;
    c.height = (uint16_t)h;
    c.fps = 30u;
    c.gop = 8u;
    c.qp_min = 28u;
    c.qp_max = 28u;
    c.bitrate = 0u;
    for (i = 5u; i + 1u < argc; i += 2u) {
        uint32_t v = tiku_cmd_parse_u32(argv[i + 1u]);

        if (tiku_cmd_streq(argv[i], "qp")) {
            c.qp_min = c.qp_max = (uint8_t)v;
        } else if (tiku_cmd_streq(argv[i], "rate")) {
            c.qp_min = 18u;
            c.qp_max = 44u;
            c.bitrate = v * 1000u;
        } else if (tiku_cmd_streq(argv[i], "gop")) {
            c.gop = (uint8_t)v;
        }
    }
    stream_len = 0u;
    if (w < TIKU_P4_H264_MIN_SIDE || h < TIKU_P4_H264_MIN_SIDE || w > MAX_W ||
        h > MAX_H || w * h > MAX_PIXELS || (w & 15u) || (h & 15u) ||
        frames == 0u || frames > 255u) {
        SHELL_PRINTF("h264: sides are whole macroblocks, 80 to 1280 x 1024 "
                     "within 1280x768, 1 to 255 frames\n");
        return;
    }
    if (!areas(w, h, &k)) {
        return;
    }
    rc = tiku_p4_h264_open(&c, &k);
    if (rc != TIKU_P4_H264_OK) {
        SHELL_PRINTF("h264: open failed (%d)\n", rc);
        return;
    }
    stream_w = (uint16_t)w;
    stream_h = (uint16_t)h;
    stream_frames = 0u;
    stream_idrs = 0u;
    stream_us = 0u;
    for (i = 0u; i < frames; i++) {
        draw(region(0), w, h, i);
        /* Each frame starts on the 8-byte boundary the encoder needs; the
         * bytes skipped are the zeros it wrote after the last frame. */
        stream_len = (stream_len + 7u) & ~7u;
        t0 = now16();
        rc = tiku_p4_h264_encode(region(0), region(2) + stream_len,
                                 REGION - stream_len, &len, &key);
        stream_us += (uint32_t)((now16() - t0) / 16u);
        if (rc != TIKU_P4_H264_OK) {
            SHELL_PRINTF("h264: frame %lu failed (%d)\n", (unsigned long)i, rc);
            break;
        }
        stream_len += len;
        stream_frames++;
        if (key) {
            stream_idrs++;
            idr_bytes += len;
        } else {
            p_bytes += len;
            p_count++;
        }
    }
    tiku_p4_h264_close();
    SHELL_PRINTF("h264: %lux%lu %lu frames: %lu IDR of %lu bytes, %lu P of "
                 "%lu bytes, %lu bytes in %lu us\n", (unsigned long)w,
                 (unsigned long)h, (unsigned long)stream_frames,
                 (unsigned long)stream_idrs,
                 (unsigned long)(stream_idrs ? idr_bytes / stream_idrs : 0u),
                 (unsigned long)p_count,
                 (unsigned long)(p_count ? p_bytes / p_count : 0u),
                 (unsigned long)stream_len, (unsigned long)stream_us);
}

static void dump(void)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *f = region(2);
    char line[77];
    uint32_t i, n = 0u;

    SHELL_PRINTF("h264: %lu bytes\n", (unsigned long)stream_len);
    for (i = 0u; i < stream_len; i += 3u) {
        uint32_t v = (uint32_t)f[i] << 16;
        unsigned k = stream_len - i;

        v |= k > 1u ? (uint32_t)f[i + 1u] << 8 : 0u;
        v |= k > 2u ? f[i + 2u] : 0u;
        line[n++] = b64[(v >> 18) & 63u];
        line[n++] = b64[(v >> 12) & 63u];
        line[n++] = k > 1u ? b64[(v >> 6) & 63u] : '=';
        line[n++] = k > 2u ? b64[v & 63u] : '=';
        if (n == 76u || i + 3u >= stream_len) {
            line[n] = '\0';
            SHELL_PRINTF("%s\n", line);
            n = 0u;
        }
    }
    SHELL_PRINTF("h264: end\n");
}

void tiku_shell_cmd_h264(uint8_t argc, const char *argv[])
{
    uint32_t f, kf, b, e;

    if (argc >= 2u && tiku_cmd_streq(argv[1], "encode")) {
        encode(argc, argv);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "dump") && stream_len) {
        dump();
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "info")) {
        tiku_p4_h264_counts(&f, &kf, &b, &e);
        SHELL_PRINTF("h264: last stream %ux%u, %lu frames (%lu IDR), %lu "
                     "bytes, %lu us; encoder totals %lu frames, %lu IDR, %lu "
                     "bytes, %lu errors\n", stream_w, stream_h,
                     (unsigned long)stream_frames, (unsigned long)stream_idrs,
                     (unsigned long)stream_len, (unsigned long)stream_us,
                     (unsigned long)f, (unsigned long)kf, (unsigned long)b,
                     (unsigned long)e);
    } else {
        SHELL_PRINTF("usage: h264 encode <w> <h> <frames> [qp <n> | rate "
                     "<kbit/s>] [gop <n>]\n"
                     "       h264 dump | info   (the last stream)\n");
    }
}
