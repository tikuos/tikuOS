/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.c - ESP32-P4X 1024 by 600 RGB565 scanout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <interfaces/display/tiku_display_arch.h>
#include <interfaces/display/tiku_display_async.h>
#include <hal/tiku_cpu.h>
#include "boards/tiku_p4x_panel.h"
#include "tiku_dsi_arch.h"
#include "tiku_dwgdma_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_timer_arch.h"

enum { WIDTH = 1024, HEIGHT = 600, FRAME_BYTES = WIDTH * HEIGHT * 2 };
static tiku_display_t *active;
static tiku_display_async_t request;
static int faulted;
static int queue_error;

static int writable(tiku_display_t *d)
{
    return d && active == d && !faulted && !request.phase && !tiku_p4_in_isr() &&
           d->w == WIDTH && d->h == HEIGHT && d->stride == WIDTH * 2 &&
           d->fmt == TIKU_DISPLAY_FMT_RGB565 && tiku_p4_dsi_valid_frame(d->fb);
}

static uint32_t now_us(void)
{
    uint64_t count;
    if (tiku_p4_systimer_read(&count)) tiku_p4_fatal("display timer");
    return (uint32_t)(count >> 4);
}

static int publish(void *buffer, uint32_t offset, uint32_t bytes)
{
    tiku_cpu_dcache_clean((uint8_t *)buffer + offset, bytes);
    return 0;
}

static void queue(void *buffer)
{
    queue_error = tiku_p4_dsi_queue(buffer);
}

static int switch_poll(void *buffer)
{
    int shown = tiku_p4_dsi_showing(buffer);
    if (queue_error || shown < 0 || tiku_p4_dsi_underrun()) return TIKU_DISPLAY_ERR_STATE;
    return shown ? TIKU_DISPLAY_OK : TIKU_DISPLAY_PENDING;
}

static void stop(void)
{
    faulted = 1;
    tiku_p4_dsi_stop();
}

static int stop_poll(void)
{
    return tiku_p4_dwgdma_chan_idle(2) ? TIKU_DISPLAY_OK : TIKU_DISPLAY_PENDING;
}

static void fail_stop(void)
{
    tiku_p4_fatal("display DMA retirement");
}

void tiku_p4_display_close(void)
{
    if (!active) return;
    stop();
    uint32_t begin = now_us();
    while (stop_poll() != TIKU_DISPLAY_OK)
        if ((uint32_t)(now_us() - begin) >= 500000u) fail_stop();
    tiku_display_async_reset(&request);
    active = 0;
}

int tiku_display_arch_init(tiku_display_t *d)
{
    if (active || tiku_p4_in_isr()) return TIKU_DISPLAY_ERR_STATE;
    if (!d || d->w != WIDTH || d->h != HEIGHT || !tiku_p4_dsi_valid_frame(d->fb))
        return TIKU_DISPLAY_ERR_INVALID;
    if (tiku_p4x_panel_init() || tiku_p4_dsi_start(d->fb)) return TIKU_DISPLAY_ERR_STATE;
    active = d;
    faulted = 0;
    tiku_p4x_panel_light();
    return TIKU_DISPLAY_OK;
}

uint32_t tiku_display_arch_caps(void)
{
    return TIKU_DISPLAY_CAP_FLIP | TIKU_DISPLAY_CAP_ASYNC;
}

tiku_display_fmt_t tiku_display_arch_format(void)
{
    return TIKU_DISPLAY_FMT_RGB565;
}

void tiku_display_arch_geometry(uint16_t *w, uint16_t *h)
{
    if (w) *w = WIDTH;
    if (h) *h = HEIGHT;
}

int tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, uint32_t argb)
{
    if (!writable(d)) return TIKU_DISPLAY_ERR_STATE;
    uint16_t rgb = (uint16_t)(((argb >> 8) & 0xf800u) |
                              ((argb >> 5) & 0x07e0u) | ((argb >> 3) & 31u));
    uint16_t *line = (uint16_t *)d->fb + y * WIDTH + x;
    for (unsigned row = 0; row < h; ++row, line += WIDTH)
        for (unsigned col = 0; col < w; ++col) line[col] = rgb;
    return TIKU_DISPLAY_OK;
}

int tiku_display_arch_fill_circle(tiku_display_t *d, int16_t x, int16_t y,
                                  uint16_t r, uint32_t colour)
{
    (void)d; (void)x; (void)y; (void)r; (void)colour;
    return TIKU_DISPLAY_ERR_UNSUPPORTED;
}

int tiku_display_arch_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                                        uint16_t w, uint16_t h, uint16_t r, uint32_t colour)
{
    (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)colour;
    return TIKU_DISPLAY_ERR_UNSUPPORTED;
}

int tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h)
{
    if (!writable(d)) return TIKU_DISPLAY_ERR_STATE;
    for (unsigned row = y; row < (unsigned)y + h; ++row)
        publish(d->fb, (row * WIDTH + x) * 2u, w * 2u);
    return TIKU_DISPLAY_OK;
}

int tiku_display_arch_set_scanout(tiku_display_t *d, void *fb)
{
    if (!writable(d)) return TIKU_DISPLAY_ERR_STATE;
    if (!tiku_p4_dsi_valid_frame(fb)) return TIKU_DISPLAY_ERR_INVALID;
    if (tiku_p4_dsi_showing(fb) == 1) return TIKU_DISPLAY_OK;
    publish(fb, 0, FRAME_BYTES);
    if (tiku_p4_dsi_queue(fb)) return TIKU_DISPLAY_ERR_STATE;
    uint32_t begin = now_us();
    while ((uint32_t)(now_us() - begin) < 500000u) {
        int shown = tiku_p4_dsi_showing(fb);
        if (shown == 1) return TIKU_DISPLAY_OK;
        if (shown < 0) break;
    }
    tiku_p4_display_close();
    return TIKU_DISPLAY_ERR_RETIRED;
}

int tiku_display_arch_submit(tiku_display_t *d, uint64_t token, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t h)
{
    if (!writable(d) || tiku_p4_dsi_showing(d->front) != 1) return TIKU_DISPLAY_ERR_STATE;
    return tiku_display_async_begin(&request, d, token, x, y, w, h);
}

int tiku_display_arch_poll(tiku_display_t *d, uint64_t token)
{
    static const tiku_display_async_ops_t ops = {
        publish, queue, switch_poll, stop, stop_poll, now_us, fail_stop
    };
    if (!active || active != d || tiku_p4_in_isr()) return TIKU_DISPLAY_ERR_STATE;
    return tiku_display_async_step(&request, d, token, &ops);
}
