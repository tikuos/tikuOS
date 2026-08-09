/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cam.c - "cam" command: capture a frame and show it.
 *
 * The whole camera path in one command: sensor over SCCB, MIPI CSI-2 into
 * the VIN, a QVGA RGB565 frame in memory, and that frame pixel-doubled onto
 * the panel the "panel" command owns.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_cam.h"

#include "../tiku_shell_config.h"

#if TIKU_SHELL_CMD_CAM

#include "../tiku_shell_io.h"
#if (TIKU_HAS_DISPLAY + 0)
#include "tiku_shell_cmd_panel.h"
#endif
#include <arch/ra8p1/tiku_camera_arch.h>
#include <arch/ra8p1/tiku_vin_arch.h>
#include <arch/ra8p1/tiku_cache_arch.h>
#include <arch/ra8p1/tiku_cpu_common.h>
#include <kernel/memory/tiku_mem.h>

/**
 * @brief Claim the camera's frame buffer, 64-byte aligned for the VIN.
 *
 * @return Base address, or NULL when no tier has room
 */
static void *
cam_claim(void)
{
    static tiku_arena_t arena;
    static void *buf;
    uint32_t bytes = (uint32_t)TIKU_CAM_QVGA_W * TIKU_CAM_QVGA_H * 2U + 64U;
    uint8_t *raw;

    if (buf != 0) {
        return buf;
    }
    if (tiku_tier_init() != TIKU_MEM_OK ||
        tiku_tier_arena_create(&arena, TIKU_MEM_SRAM, bytes, 70)
            != TIKU_MEM_OK) {
        return 0;
    }
    raw = (uint8_t *)tiku_arena_alloc(&arena, bytes);
    if (raw == 0) {
        return 0;
    }
    buf = (void *)(((uintptr_t)raw + 63U) & ~(uintptr_t)63U);
    return buf;
}

#if (TIKU_HAS_DISPLAY + 0)
/**
 * @brief Show the captured frame: each pixel doubled, centred on the panel.
 *
 * @param d    Screen to draw on
 * @param src  QVGA RGB565 frame
 */
static void
cam_show(tiku_display_t *d, const uint16_t *src)
{
    uint32_t ox = ((uint32_t)d->w - TIKU_CAM_QVGA_W * 2U) / 2U;
    uint32_t oy = ((uint32_t)d->h - TIKU_CAM_QVGA_H * 2U) / 2U;
    uint8_t *rows = (uint8_t *)d->fb + (uint32_t)oy * d->stride;
    uint32_t x, y;

    for (y = 0U; y < TIKU_CAM_QVGA_H; y++) {
        const uint16_t *in = src + (uint32_t)y * TIKU_CAM_QVGA_W;
        uint16_t *out0 = (uint16_t *)(void *)
            ((uint8_t *)d->fb + ((uint32_t)oy + y * 2U) * d->stride) + ox;
        uint16_t *out1 = (uint16_t *)(void *)((uint8_t *)out0 + d->stride);

        for (x = 0U; x < TIKU_CAM_QVGA_W; x++) {
            uint16_t px = in[x];

            out0[x * 2U]      = px;
            out0[x * 2U + 1U] = px;
            out1[x * 2U]      = px;
            out1[x * 2U + 1U] = px;
        }
    }
    /* The controller scans memory; the CPU's rows have to reach it. */
    tiku_ra8p1_dcache_clean(rows, (uint32_t)TIKU_CAM_QVGA_H * 2U * d->stride);
}
#endif /* TIKU_HAS_DISPLAY */

void
tiku_shell_cmd_cam(uint8_t argc, const char *argv[])
{
    const uint32_t frame_bytes =
        (uint32_t)TIKU_CAM_QVGA_W * TIKU_CAM_QVGA_H * 2U;
#if (TIKU_HAS_DISPLAY + 0)
    tiku_display_t *d;
#endif
    uint16_t id = 0U;
    void *buf;
    uint32_t i, lit;
    int rc;

    (void)argc; (void)argv;

    rc = tiku_camera_arch_power_on();
    if (rc != TIKU_CAM_OK) {
        SHELL_PRINTF("cam: power-on failed (%d)\r\n", rc);
        return;
    }
    rc = tiku_camera_arch_read_id(&id);
    if (rc != TIKU_CAM_OK) {
        SHELL_PRINTF("cam: no OV5640 (rc=%d id=%04x)\r\n", rc, (unsigned)id);
        return;
    }

#if (TIKU_HAS_DISPLAY + 0)
    d = tiku_shell_cmd_panel_display();
    if (d == 0) {
        SHELL_PRINTF("cam: panel did not come up\r\n");
        return;
    }
#endif
    buf = cam_claim();
    if (buf == 0) {
        SHELL_PRINTF("cam: no memory for a frame\r\n");
        return;
    }

    rc = tiku_camera_arch_setup_qvga();
    if (rc != TIKU_CAM_OK) {
        SHELL_PRINTF("cam: sensor setup failed (%d)\r\n", rc);
        return;
    }
    /* Hold the sensor's output while the receive side comes up, so the
     * first frame the VIN sees is a whole one. */
    (void)tiku_camera_arch_stream(0);
    (void)tiku_camera_arch_write_reg(0x3008U, 0x42U);

    tiku_ra8p1_dcache_invalidate(buf, frame_bytes);
    rc = tiku_vin_arch_init(buf, TIKU_CAM_QVGA_W, TIKU_CAM_QVGA_H,
                            TIKU_CAM_UI_PS);
    if (rc != TIKU_VIN_OK) {
        SHELL_PRINTF("cam: capture pipeline failed (%d)\r\n", rc);
        return;
    }
    (void)tiku_vin_arch_start();

    (void)tiku_camera_arch_write_reg(0x3008U, 0x02U);
    tiku_cpu_ra8p1_delay_ms(20U);
    (void)tiku_camera_arch_stream(1);

    rc = tiku_vin_arch_frame_wait();
    if (rc != TIKU_VIN_OK) {
        SHELL_PRINTF("cam: no frame (rc=%d, vin status %08lx)\r\n",
                     rc, (unsigned long)tiku_vin_arch_status());
        return;
    }

    /* The VIN wrote memory behind the cache; drop any lines covering it,
     * then prove the frame is real data before showing it. */
    tiku_ra8p1_dcache_invalidate(buf, frame_bytes);
    lit = 0U;
    for (i = 0U; i < (uint32_t)TIKU_CAM_QVGA_W * TIKU_CAM_QVGA_H; i++) {
        if (((const uint16_t *)buf)[i] != 0U) {
            lit++;
        }
    }
#if (TIKU_HAS_DISPLAY + 0)
    cam_show(d, (const uint16_t *)buf);
    SHELL_PRINTF("cam: frame on panel (%lu of %lu pixels lit)\r\n",
                 (unsigned long)lit,
                 (unsigned long)TIKU_CAM_QVGA_W * TIKU_CAM_QVGA_H);
#else
    SHELL_PRINTF("cam: frame captured (%lu of %lu pixels lit; no display "
                 "in this build)\r\n", (unsigned long)lit,
                 (unsigned long)TIKU_CAM_QVGA_W * TIKU_CAM_QVGA_H);
#endif
}

#endif /* TIKU_SHELL_CMD_CAM */
