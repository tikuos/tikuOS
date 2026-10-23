/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cam_p4.c - "cam" command on the ESP32-P4: the IMX219
 * identified and set to a mode, one frame captured into PSRAM with its luma
 * statistics and the receiver's errors, and the frame dumped, decimated, in
 * base64 for the host to turn into a picture.
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

#define ARENA_ID    86u
#define FRAME_BYTES (1640u * 1232u * 2u)        /* the largest mode */
#define DUMP_WIDTH  320u                        /* the widest decimated dump */

static tiku_arena_t arena;
static uint8_t have_arena, have_frame;

static void *frame(void)
{
    if (!have_arena) {
        if (tiku_tier_arena_create_span(&arena, TIKU_MEM_PSRAM, 0, FRAME_BYTES,
                                        64u, ARENA_ID) != TIKU_MEM_OK) {
            SHELL_PRINTF("cam: no %lu bytes of PSRAM\n",
                         (unsigned long)FRAME_BYTES);
            return NULL;
        }
        have_arena = 1u;
    }
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

/** @brief The frame's errors, timing and luma statistics. */
static void report(const struct tiku_p4_cam_frame *f, int rc, void *buf)
{
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    struct tiku_p4_cam_luma l;
    uint32_t lines, code;
    unsigned i;

    SHELL_PRINTF("cam: csi stop 0x%lx main 0x%lx phy 0x%lx/0x%lx pkt 0x%lx "
                 "frame 0x%lx crc 0x%lx id 0x%lx ecc 0x%lx; isp 0x%lx bridge "
                 "0x%lx\n", (unsigned long)f->stop_before,
                 (unsigned long)f->csi.main, (unsigned long)f->csi.phy_fatal,
                 (unsigned long)f->csi.phy, (unsigned long)f->csi.pkt_fatal,
                 (unsigned long)f->csi.frame_fatal, (unsigned long)f->csi.pld_crc,
                 (unsigned long)f->csi.data_id, (unsigned long)f->csi.ecc_corrected,
                 (unsigned long)f->isp.isp_errors,
                 (unsigned long)f->isp.bridge_errors);
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
        report(&f, tiku_p4_cam_capture(buf, NULL, &f), buf);
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
        SHELL_PRINTF("cam: auto %lu frames, white balance r %u/1024 b %u/1024, "
                     "gamma 1/2.2\n", (unsigned long)used, t.gain_r, t.gain_b);
    }
    report(&f, rc, buf);
}

/** @brief The last frame, every @p step-th pixel of every @p step-th row,
 *         as RGB565 little-endian bytes in base64. */
static void dump(uint32_t step)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const struct tiku_p4_cam_mode *m =
        tiku_p4_cam_mode((unsigned)tiku_p4_cam_current_mode());
    const uint16_t *px = (const uint16_t *)arena.buf;
    uint32_t w, h, x, y, total, i = 0u, v = 0u, n = 0u;
    char line[77];
    unsigned k = 0u;

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
            uint8_t b2[2];
            unsigned j;

            b2[0] = (uint8_t)p;
            b2[1] = (uint8_t)(p >> 8);
            for (j = 0u; j < 2u; j++) {
                v = (v << 8) | b2[j];
                if (++k == 3u) {
                    line[n++] = b64[(v >> 18) & 63u];
                    line[n++] = b64[(v >> 12) & 63u];
                    line[n++] = b64[(v >> 6) & 63u];
                    line[n++] = b64[v & 63u];
                    k = 0u;
                    v = 0u;
                    if (n == 76u) {
                        line[n] = '\0';
                        SHELL_PRINTF("%s\n", line);
                        n = 0u;
                    }
                }
                i++;
            }
        }
    }
    if (k != 0u) {
        v <<= 8u * (3u - k);
        line[n++] = b64[(v >> 18) & 63u];
        line[n++] = b64[(v >> 12) & 63u];
        line[n++] = k > 1u ? b64[(v >> 6) & 63u] : '=';
        line[n++] = '=';
    }
    if (n != 0u) {
        line[n] = '\0';
        SHELL_PRINTF("%s\n", line);
    }
    SHELL_PRINTF("cam: end %lu\n", (unsigned long)i);
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
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "info")) {
        tiku_p4_isp_counts(&frames, &failed);
        SHELL_PRINTF("cam: mode %d; ISP frames %lu, failed %lu\n",
                     tiku_p4_cam_current_mode(), (unsigned long)frames,
                     (unsigned long)failed);
    } else {
        SHELL_PRINTF("usage: cam id | mode <0 640x480 | 1 1640x1232> | snap |"
                     " auto [mean] | exposure <lines> | gain <0..232> |"
                     " dump [step] | info\n");
    }
}

#endif /* TIKU_SHELL_CMD_CAM */
