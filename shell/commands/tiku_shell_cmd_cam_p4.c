/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cam_p4.c - "cam" command on the ESP32-P4: the IMX219
 * identified and set to a mode, one frame captured into PSRAM with its luma
 * statistics and the receiver's errors, frames streamed, a RAW10 frame
 * taken past the ISP, a frame through the JPEG encoder and frames through
 * the H.264 encoder, each dumped in base64 for the host to save.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <stdint.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_cam.h"
#include "tiku_shell_cmd_util.h"
#include "../tiku_shell_config.h"

#if TIKU_SHELL_CMD_CAM

#include <kernel/memory/tiku_mem.h>
#include <arch/esp32p4/tiku_camera_arch.h>
#include <arch/esp32p4/tiku_jpeg_arch.h>
#include <arch/esp32p4/tiku_h264_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>

#define ARENA_ID    86u
#define FRAME_BYTES (1640u * 1232u * 2u)        /* the largest RGB565 mode */
#define BUFFERS     2u                          /* a stream's ping-pong pair */
#define RAW_BYTES   (3264u * 2464u * 5u / 4u)   /* the RAW10 mode's frame */
#define ARENA_BYTES (RAW_BYTES > FRAME_BYTES * BUFFERS ? RAW_BYTES \
                                                       : FRAME_BYTES * BUFFERS)
#define CODED       (FRAME_BYTES * BUFFERS)     /* the encoders' output, the
                                                 * rest of the arena */
#define STORE       (FRAME_BYTES + 512u * 1024u) /* the H.264 reference */
#define SR_ID       88u                         /* the H.264 SRAM work */
#define H264_FPS    10u                         /* a capture, a conversion
                                                 * and an encode: ~100 ms */
#define DUMP_WIDTH  320u                        /* the widest decimated dump */

static tiku_arena_t arena;
static uint8_t have_arena, have_frame;
static struct tiku_p4_cam_tune tune;    /* what cam snap applies */
static uint8_t *shown;                  /* the frame cam dump sends */
static uint32_t raw_bytes;              /* the RAW frame in the arena, 0 for
                                         * none */
static uint32_t coded_bytes;            /* the JPEG file or H.264 stream at
                                         * CODED, 0 for none */
static uint8_t coded_h264;              /* 1: it is the H.264 stream */
static tiku_arena_t sr_arena;
static uint32_t sr_bytes;

static void *frame(void)
{
    if (!have_arena) {
        if (tiku_tier_arena_create_span(&arena, TIKU_MEM_PSRAM, 0,
                                        ARENA_BYTES, 64u,
                                        ARENA_ID) != TIKU_MEM_OK) {
            SHELL_PRINTF("cam: no %lu bytes of PSRAM\n",
                         (unsigned long)ARENA_BYTES);
            return NULL;
        }
        have_arena = 1u;
    }
    shown = arena.buf;
    raw_bytes = 0u;
    coded_bytes = 0u;
    return arena.buf;
}

static void say_rc(const char *what, int rc)
{
    static const char *const names[] = {
        "ok", "bad argument", "no IMX219 at 0x10", "bus error",
        "receiver did not start", "no frame in time", "frame fault",
        "no mode set (cam mode <n>)",
    };
    unsigned i = rc < 0 ? (unsigned)(-rc) : 0u;

    SHELL_PRINTF("cam: %s: %s\n", what, i < sizeof names / sizeof names[0]
                                            ? names[i] : "error");
}

/** @brief The receiver's, the ISP's and the bridge's errors in @p f. */
static void errors(const struct tiku_p4_cam_frame *f)
{
    SHELL_PRINTF("cam: csi stop 0x%lx main 0x%lx phy 0x%lx/0x%lx pkt 0x%lx "
                 "frame 0x%lx crc 0x%lx id 0x%lx ecc 0x%lx; isp 0x%lx bridge "
                 "0x%lx\n", (unsigned long)f->stop_before,
                 (unsigned long)f->csi.main, (unsigned long)f->csi.phy_fatal,
                 (unsigned long)f->csi.phy, (unsigned long)f->csi.pkt_fatal,
                 (unsigned long)f->csi.frame_fatal, (unsigned long)f->csi.pld_crc,
                 (unsigned long)f->csi.data_id, (unsigned long)f->csi.ecc_corrected,
                 (unsigned long)f->isp.isp_errors,
                 (unsigned long)f->isp.bridge_errors);
}

/** @brief The frame's errors, timing and luma statistics. */
static void report(const struct tiku_p4_cam_frame *f, int rc, void *buf)
{
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    struct tiku_p4_cam_luma l;
    uint32_t lines, code;
    unsigned i;

    errors(f);
    if (rc != TIKU_P4_CAM_OK) {
        say_rc("capture", rc);
        return;
    }
    have_frame = 1u;
    tiku_p4_cam_luma(buf, m->width, m->height, 2u, &l);
    tiku_p4_cam_exposure_now(&lines, &code);
    SHELL_PRINTF("cam: %ux%u RGB565 frame in %lu us; luma mean %u min %u "
                 "max %u; exposure %lu lines, gain code %lu\n", m->width,
                 m->height, (unsigned long)f->us, l.mean, l.min, l.max,
                 (unsigned long)lines, (unsigned long)code);
    SHELL_PRINTF("cam: hist");
    for (i = 0u; i < 16u; i++) {
        SHELL_PRINTF(" %lu", (unsigned long)(l.hist[i] * 1000u / l.pixels));
    }
    SHELL_PRINTF(" (per mille, 16 bins)\ncam: ae");
    for (i = 0u; i < 25u; i++) {
        SHELL_PRINTF(" %u", f->isp.ae_mean[i]);
    }
    SHELL_PRINTF("; awb white %lu r %lu g %lu b %lu\n",
                 (unsigned long)f->isp.awb_white, (unsigned long)f->isp.awb_sum_r,
                 (unsigned long)f->isp.awb_sum_g, (unsigned long)f->isp.awb_sum_b);
}

/** @brief A buffer, and mode 0 when none is set; NULL after a message. */
static void *ready(void)
{
    void *buf = frame();
    int rc;

    if (buf != NULL && tiku_p4_cam_current_mode() < 0 &&
        (rc = tiku_p4_cam_configure(0u)) != TIKU_P4_CAM_OK) {
        say_rc("mode 0", rc);
        return NULL;
    }
    return buf;
}

static void snap(void)
{
    struct tiku_p4_cam_frame f;
    void *buf = ready();

    if (buf != NULL) {
        report(&f, tiku_p4_cam_capture(buf, &tune, &f), buf);
    }
}

static void autoexpose(uint32_t target)
{
    struct tiku_p4_cam_frame f;
    struct tiku_p4_cam_tune t;
    uint32_t used = 0u;
    void *buf = ready();
    int rc;

    if (buf == NULL) {
        return;
    }
    rc = tiku_p4_cam_auto(buf, target, 6u, &t, &f, &used);
    if (rc == TIKU_P4_CAM_OK) {
        tune = t;
        SHELL_PRINTF("cam: auto %lu frames, white balance r %u/1024 b %u/1024, "
                     "black %u, saturation %u/128, gamma 1/2.2\n",
                     (unsigned long)used, t.gain_r, t.gain_b, t.black,
                     t.saturation);
    }
    report(&f, rc, buf);
}

/** @brief Base64 of a byte stream, printed in lines of 76 characters. */
struct b64 {
    uint32_t v;             /* the bytes of the group so far */
    unsigned k, n;          /* bytes in the group, characters in the line */
    char line[77];
};

static const char b64_digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_put(struct b64 *e, uint8_t byte)
{
    e->v = (e->v << 8) | byte;
    if (++e->k == 3u) {
        e->line[e->n++] = b64_digits[(e->v >> 18) & 63u];
        e->line[e->n++] = b64_digits[(e->v >> 12) & 63u];
        e->line[e->n++] = b64_digits[(e->v >> 6) & 63u];
        e->line[e->n++] = b64_digits[e->v & 63u];
        e->k = 0u;
        e->v = 0u;
        if (e->n == 76u) {
            e->line[e->n] = '\0';
            SHELL_PRINTF("%s\n", e->line);
            e->n = 0u;
        }
    }
}

/** @brief The last group padded with '=', the last line printed. */
static void b64_end(struct b64 *e)
{
    if (e->k != 0u) {
        e->v <<= 8u * (3u - e->k);
        e->line[e->n++] = b64_digits[(e->v >> 18) & 63u];
        e->line[e->n++] = b64_digits[(e->v >> 12) & 63u];
        e->line[e->n++] = e->k > 1u ? b64_digits[(e->v >> 6) & 63u] : '=';
        e->line[e->n++] = '=';
    }
    if (e->n != 0u) {
        e->line[e->n] = '\0';
        SHELL_PRINTF("%s\n", e->line);
    }
}

/** @brief The last frame, every @p step-th pixel of every @p step-th row,
 *         as RGB565 little-endian bytes in base64. */
static void dump(uint32_t step)
{
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    const uint16_t *px = (const uint16_t *)shown;
    struct b64 e = {0};
    uint32_t w, h, x, y, total, i = 0u;

    if (!have_frame || m == NULL) {
        SHELL_PRINTF("cam: no frame (cam snap)\n");
        return;
    }
    if (step == 0u) {
        step = (m->width + DUMP_WIDTH - 1u) / DUMP_WIDTH;
    }
    w = (m->width + step - 1u) / step;
    h = (m->height + step - 1u) / step;
    total = w * h * 2u;
    SHELL_PRINTF("cam: dump %lu %lu rgb565le %lu bytes\n", (unsigned long)w,
                 (unsigned long)h, (unsigned long)total);
    for (y = 0u; y < m->height; y += step) {
        for (x = 0u; x < m->width; x += step) {
            uint16_t p = px[y * m->width + x];

            b64_put(&e, (uint8_t)p);
            b64_put(&e, (uint8_t)(p >> 8));
            i += 2u;
        }
    }
    b64_end(&e);
    SHELL_PRINTF("cam: end %lu\n", (unsigned long)i);
}

/** @brief A RAW10 frame of the configured mode past the ISP, with the
 *         errors and the 10-bit level of every 16th pixel group's first
 *         pixel. */
static void raw(void)
{
    const struct tiku_p4_cam_mode *m;
    struct tiku_p4_cam_frame f;
    uint8_t *buf = ready();
    uint32_t bytes, i, v, n = 0u, sum = 0u, lo = 1023u, hi = 0u;
    int rc;

    if (buf == NULL) {
        return;
    }
    m = tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    bytes = (uint32_t)m->width * m->height * 5u / 4u;
    have_frame = 0u;
    rc = tiku_p4_cam_raw(buf, &f);
    errors(&f);
    if (rc != TIKU_P4_CAM_OK) {
        say_rc("raw", rc);
        return;
    }
    raw_bytes = bytes;
    for (i = 0u; i + 5u <= bytes; i += 80u) {
        v = ((uint32_t)buf[i] << 2) | (buf[i + 4u] & 3u);
        sum += v;
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        n++;
    }
    SHELL_PRINTF("cam: %ux%u RAW10 rggb frame in %lu us, %lu bytes; level mean "
                 "%lu min %lu max %lu of 1023\n", m->width, m->height,
                 (unsigned long)f.us, (unsigned long)bytes,
                 (unsigned long)(sum / n), (unsigned long)lo, (unsigned long)hi);
}

static uint32_t now_us(void)
{
    uint64_t t = 0u;

    (void)tiku_p4_systimer_read(&t);
    return (uint32_t)(t / 16u);
}

/** @brief The last frame through the JPEG encoder at @p quality: 4:2:0
 *         when both sides are whole 16-pixel MCUs, else 4:4:4. */
static void jpeg(uint32_t quality)
{
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    tiku_p4_picture_t pic;
    unsigned sampling;
    uint32_t len = 0u, t0;
    int rc;

    if (!have_frame || m == NULL) {
        SHELL_PRINTF("cam: no frame (cam snap)\n");
        return;
    }
    sampling = (m->width % 16u) == 0u && (m->height % 16u) == 0u
                   ? TIKU_P4_JPEG_420 : TIKU_P4_JPEG_444;
    pic.base = shown;
    pic.width = m->width;
    pic.height = m->height;
    pic.bpp = 2u;
    coded_bytes = 0u;
    t0 = now_us();
    rc = tiku_p4_jpeg_encode(&pic, sampling, quality,
                             (uint8_t *)arena.buf + CODED, ARENA_BYTES - CODED,
                             &len);
    t0 = now_us() - t0;
    if (rc != TIKU_P4_JPEG_OK) {
        SHELL_PRINTF("cam: jpeg: encoder error %d\n", rc);
        return;
    }
    coded_bytes = len;
    coded_h264 = 0u;
    SHELL_PRINTF("cam: jpeg %ux%u %s q%lu, %lu bytes in %lu us\n", m->width,
                 m->height, sampling == TIKU_P4_JPEG_420 ? "4:2:0" : "4:4:4",
                 (unsigned long)quality, (unsigned long)len, (unsigned long)t0);
}

/** @brief An RGB565 frame as the H.264 encoder reads it: for each pair of
 *         pixels a chroma byte (U on even rows, V on odd) and two luma
 *         bytes, BT.601 limited range. */
static void to_yuv(const uint16_t *rgb, uint8_t *out, uint32_t w, uint32_t h)
{
    uint32_t x, y, k;

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x += 2u) {
            int32_t r[2], g[2], b[2], c;

            for (k = 0u; k < 2u; k++) {
                uint32_t v = rgb[y * w + x + k];

                r[k] = (int32_t)(((v >> 11) & 31u) * 255u / 31u);
                g[k] = (int32_t)(((v >> 5) & 63u) * 255u / 63u);
                b[k] = (int32_t)((v & 31u) * 255u / 31u);
            }
            c = (y & 1u) ? 128 + ((112 * (r[0] + r[1]) - 94 * (g[0] + g[1]) -
                                   18 * (b[0] + b[1]) + 256) >> 9)
                         : 128 + ((-38 * (r[0] + r[1]) - 74 * (g[0] + g[1]) +
                                   112 * (b[0] + b[1]) + 256) >> 9);
            *out++ = (uint8_t)c;
            for (k = 0u; k < 2u; k++) {
                *out++ = (uint8_t)(16 + ((66 * r[k] + 129 * g[k] + 25 * b[k] +
                                          128) >> 8));
            }
        }
    }
}

/** @brief @p frames 640x480 frames, each captured with the tune, converted
 *         and encoded into one Annex B stream at QP 28, an IDR every 15. */
static void h264(uint32_t frames)
{
    const struct tiku_p4_cam_mode *m;
    struct tiku_p4_cam_frame f;
    tiku_p4_h264_cfg_t c;
    tiku_p4_h264_work_t k;
    uint8_t *buf = ready(), *yuv, *out;
    uint32_t i, len, need, done = 0u, idrs = 0u, t, cap_us = 0u, enc_us = 0u;
    int key, rc = TIKU_P4_CAM_OK, erc = TIKU_P4_H264_OK;

    if (buf == NULL) {
        return;
    }
    m = tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    if (m->width != 640u || frames == 0u || frames > 255u) {
        SHELL_PRINTF("cam: h264 takes 1 to 255 frames of mode 0 (640x480)\n");
        return;
    }
    need = tiku_p4_h264_sram_bytes(640u);
    if (sr_bytes < need) {
        if (sr_bytes != 0u) {
            (void)tiku_arena_destroy(&sr_arena);
            sr_bytes = 0u;
        }
        if (tiku_tier_arena_create_span(&sr_arena, TIKU_MEM_SRAM, 0, need, 64u,
                                        SR_ID) != TIKU_MEM_OK) {
            SHELL_PRINTF("cam: no %lu bytes of SRAM for the encoder\n",
                         (unsigned long)need);
            return;
        }
        sr_bytes = need;
    }
    yuv = buf + FRAME_BYTES;
    out = buf + CODED;
    c.width = 640u;
    c.height = 480u;
    c.fps = H264_FPS;
    c.gop = 15u;
    c.qp_min = 28u;
    c.qp_max = 28u;
    c.bitrate = 0u;
    k.sram = (uint8_t *)sr_arena.buf;
    k.sram_bytes = sr_bytes;
    k.store = buf + STORE;
    k.store_bytes = tiku_p4_h264_store_bytes(640u, 480u);
    if (STORE + k.store_bytes > CODED ||
        tiku_p4_h264_open(&c, &k) != TIKU_P4_H264_OK) {
        SHELL_PRINTF("cam: h264: the encoder did not open\n");
        return;
    }
    coded_bytes = 0u;
    coded_h264 = 1u;
    for (i = 0u; i < frames; i++) {
        t = now_us();
        rc = tiku_p4_cam_capture(buf, &tune, &f);
        if (rc != TIKU_P4_CAM_OK) {
            break;
        }
        to_yuv((const uint16_t *)buf, yuv, 640u, 480u);
        cap_us += now_us() - t;
        /* Each frame starts on the 8-byte boundary the encoder needs; the
         * bytes skipped are the zeros it wrote after the last frame. */
        coded_bytes = (coded_bytes + 7u) & ~7u;
        t = now_us();
        erc = tiku_p4_h264_encode(yuv, out + coded_bytes,
                                  ARENA_BYTES - CODED - coded_bytes, &len, &key);
        enc_us += now_us() - t;
        if (erc != TIKU_P4_H264_OK) {
            break;
        }
        coded_bytes += len;
        idrs += key ? 1u : 0u;
        done++;
    }
    tiku_p4_h264_close();
    have_frame = done != 0u;
    SHELL_PRINTF("cam: h264 640x480 %lu frames, %lu IDR, %lu bytes; capture "
                 "and convert %lu us, encode %lu us a frame\n",
                 (unsigned long)done, (unsigned long)idrs,
                 (unsigned long)coded_bytes,
                 (unsigned long)(done ? cap_us / done : 0u),
                 (unsigned long)(done ? enc_us / done : 0u));
    if (rc != TIKU_P4_CAM_OK) {
        say_rc("h264 capture", rc);
    } else if (erc != TIKU_P4_H264_OK) {
        SHELL_PRINTF("cam: h264: encoder error %d at frame %lu\n", erc,
                     (unsigned long)done);
    }
}

/** @brief The JPEG file or the H.264 stream in base64. */
static void coded_dump(int want_h264)
{
    const uint8_t *p = (const uint8_t *)arena.buf + CODED;
    struct b64 e = {0};
    uint32_t i;

    if (coded_bytes == 0u || coded_h264 != (uint8_t)want_h264) {
        SHELL_PRINTF("cam: nothing encoded (cam %s)\n",
                     want_h264 ? "h264 <frames>" : "jpeg");
        return;
    }
    SHELL_PRINTF("cam: %s %lu bytes\n", want_h264 ? "h264" : "jpeg",
                 (unsigned long)coded_bytes);
    for (i = 0u; i < coded_bytes; i++) {
        b64_put(&e, p[i]);
    }
    b64_end(&e);
    SHELL_PRINTF("cam: end %lu\n", (unsigned long)coded_bytes);
}

/** @brief The RAW frame's bytes in base64. */
static void raw_dump(void)
{
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    struct b64 e = {0};
    uint32_t i;

    if (raw_bytes == 0u || m == NULL ||
        (uint32_t)m->width * m->height * 5u / 4u != raw_bytes) {
        SHELL_PRINTF("cam: no RAW frame (cam raw)\n");
        return;
    }
    SHELL_PRINTF("cam: raw %u %u raw10 rggb %lu bytes\n", m->width, m->height,
                 (unsigned long)raw_bytes);
    for (i = 0u; i < raw_bytes; i++) {
        b64_put(&e, ((const uint8_t *)arena.buf)[i]);
    }
    b64_end(&e);
    SHELL_PRINTF("cam: end %lu\n", (unsigned long)raw_bytes);
}

/** @brief `cam stream <frames | Ns> [auto]`: frames back to back into the
 *         two buffers, steering exposure and white balance with auto. */
static void stream(const char *amount, int steer)
{
    struct tiku_p4_cam_stream st;
    void *bufs[BUFFERS];
    uint8_t *buf = ready();
    uint32_t n = tiku_cmd_parse_u32(amount), i, fps;
    uint32_t len = 0u;
    int rc;

    if (buf == NULL) {
        return;
    }
    while (amount[len] != '\0') {
        len++;
    }
    for (i = 0u; i < BUFFERS; i++) {
        bufs[i] = buf + i * FRAME_BYTES;
    }
    if (steer && tune.black == 0u) {
        tune.black = TIKU_P4_CAM_BLACK;
        tune.saturation = TIKU_P4_CAM_SATURATION;
        tune.gamma = 1u;
    }
    rc = len > 1u && amount[len - 1u] == 's'
             ? tiku_p4_cam_stream(bufs, BUFFERS, 0u, n * 1000000u, &tune,
                                  steer ? 50u : 0u, &st)
             : tiku_p4_cam_stream(bufs, BUFFERS, n, 0u, &tune,
                                  steer ? 50u : 0u, &st);
    fps = st.span_us ? (uint32_t)(((uint64_t)(st.frames + st.faults - 1u) *
                                   10000000u) / st.span_us) : 0u;
    SHELL_PRINTF("cam: stream %lu frames, %lu faults, %lu dropped; %lu.%lu fps; "
                 "first %lu us, gaps %lu..%lu us; isp 0x%lx bridge 0x%lx\n",
                 (unsigned long)st.frames, (unsigned long)st.faults,
                 (unsigned long)st.dropped, (unsigned long)(fps / 10u),
                 (unsigned long)(fps % 10u), (unsigned long)st.first_us,
                 (unsigned long)st.gap_min_us, (unsigned long)st.gap_max_us,
                 (unsigned long)st.isp_errors, (unsigned long)st.bridge_errors);
    if (st.frames != 0u) {
        shown = (uint8_t *)bufs[st.last];
    }
    if (rc != TIKU_P4_CAM_OK) {
        say_rc("stream", rc);
        return;
    }
    report(&st.frame, rc, shown);
}

/** @brief Show or change what cam snap applies: `cam tune [off | on |
 *         black <n> | sat <n> | gamma <0|1> | wb <r> <b>]`. */
static void set_tune(uint8_t argc, const char *argv[])
{
    uint32_t v = argc >= 4u ? tiku_cmd_parse_u32(argv[3]) : 0u;

    if (argc == 3u && tiku_cmd_streq(argv[2], "off")) {
        tune = (struct tiku_p4_cam_tune){0};
    } else if (argc == 3u && tiku_cmd_streq(argv[2], "on")) {
        tune.black = TIKU_P4_CAM_BLACK;
        tune.saturation = TIKU_P4_CAM_SATURATION;
        tune.gamma = 1u;
    } else if (argc == 4u && tiku_cmd_streq(argv[2], "black") && v < 256u) {
        tune.black = (uint8_t)v;
    } else if (argc == 4u && tiku_cmd_streq(argv[2], "sat") && v < 256u) {
        tune.saturation = (uint8_t)v;
    } else if (argc == 4u && tiku_cmd_streq(argv[2], "gamma")) {
        tune.gamma = v != 0u;
    } else if (argc == 5u && tiku_cmd_streq(argv[2], "wb") && v < 4096u &&
               tiku_cmd_parse_u32(argv[4]) < 4096u) {
        tune.gain_r = (uint16_t)v;
        tune.gain_b = (uint16_t)tiku_cmd_parse_u32(argv[4]);
    } else if (argc != 2u) {
        SHELL_PRINTF("usage: cam tune [off | on | black <0..255> |"
                     " sat <0..255> | gamma <0|1> | wb <r> <b>]\n");
        return;
    }
    SHELL_PRINTF("cam: tune black %u wb r %u b %u sat %u gamma %u\n",
                 tune.black, tune.gain_r, tune.gain_b, tune.saturation,
                 tune.gamma);
}

void tiku_shell_cmd_cam(uint8_t argc, const char *argv[])
{
    uint16_t model = 0u;
    uint32_t frames, failed;
    int rc;

    if (argc == 2u && tiku_cmd_streq(argv[1], "id")) {
        rc = tiku_p4_cam_identify(&model);
        if (rc == TIKU_P4_CAM_OK) {
            SHELL_PRINTF("cam: model 0x%04x at 0x%02x%s\n", model,
                         TIKU_P4_CAM_I2C_ADDR,
                         model == TIKU_P4_CAM_MODEL ? " (IMX219)" : "");
        } else {
            say_rc("id", rc);
        }
    } else if (argc == 3u && tiku_cmd_streq(argv[1], "mode")) {
        const struct tiku_p4_cam_mode *m =
            tiku_p4_cam_mode(tiku_cmd_parse_u32(argv[2]));

        rc = tiku_p4_cam_configure(tiku_cmd_parse_u32(argv[2]));
        if (rc == TIKU_P4_CAM_OK) {
            have_frame = 0u;
            SHELL_PRINTF("cam: mode %s, %ux%u RAW10, 2 lanes at %u Mbit/s\n",
                         argv[2], m->width, m->height, TIKU_P4_CAM_LANE_MBPS);
        } else {
            say_rc("mode", rc);
        }
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "snap")) {
        snap();
    } else if (argc >= 2u && argc <= 3u && tiku_cmd_streq(argv[1], "auto")) {
        autoexpose(argc == 3u ? tiku_cmd_parse_u32(argv[2]) : 50u);
    } else if (argc == 3u && tiku_cmd_streq(argv[1], "exposure")) {
        say_rc("exposure", tiku_p4_cam_set_exposure(tiku_cmd_parse_u32(argv[2])));
    } else if (argc == 3u && tiku_cmd_streq(argv[1], "gain")) {
        say_rc("gain", tiku_p4_cam_set_gain(tiku_cmd_parse_u32(argv[2])));
    } else if (argc >= 2u && argc <= 3u && tiku_cmd_streq(argv[1], "dump")) {
        dump(argc == 3u ? tiku_cmd_parse_u32(argv[2]) : 0u);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "raw")) {
        raw();
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "rawdump")) {
        raw_dump();
    } else if (argc >= 2u && argc <= 3u && tiku_cmd_streq(argv[1], "jpeg")) {
        jpeg(argc == 3u ? tiku_cmd_parse_u32(argv[2]) : 85u);
    } else if (argc == 3u && tiku_cmd_streq(argv[1], "h264")) {
        h264(tiku_cmd_parse_u32(argv[2]));
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "jpegdump")) {
        coded_dump(0);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "h264dump")) {
        coded_dump(1);
    } else if (argc >= 3u && argc <= 4u && tiku_cmd_streq(argv[1], "stream")) {
        stream(argv[2], argc == 4u && tiku_cmd_streq(argv[3], "auto"));
    } else if (argc >= 2u && tiku_cmd_streq(argv[1], "tune")) {
        set_tune(argc, argv);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "info")) {
        tiku_p4_isp_counts(&frames, &failed);
        SHELL_PRINTF("cam: mode %d; ISP frames %lu, failed %lu\n",
                     tiku_p4_cam_current_mode(), (unsigned long)frames,
                     (unsigned long)failed);
    } else {
        SHELL_PRINTF("usage: cam id | mode <0 640x480 | 1 1640x1232 | 2 "
                     "3264x2464 raw only> | snap | auto [mean] | exposure "
                     "<lines> | gain <0..232> | dump [step] | stream <n | Ns> "
                     "[auto] | raw | rawdump | jpeg [quality] | jpegdump | "
                     "h264 <frames> | h264dump | tune [...] | info\n");
    }
}

#endif /* TIKU_SHELL_CMD_CAM */
