/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_async.c - row-scoped cache publication and polled handoff.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_display_async.h"
#include <stddef.h>

enum { IDLE, PUBLISH, SWITCH, WAIT, STOP };

void tiku_display_async_reset(tiku_display_async_t *s)
{
    uint64_t last = s->last_token;
    *s = (tiku_display_async_t){0};
    s->last_token = last;
}

int tiku_display_async_begin(tiku_display_async_t *s, tiku_display_t *d,
    uint64_t token, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!s || !d) return TIKU_DISPLAY_ERR_INVALID;
    if (s->phase) return TIKU_DISPLAY_ERR_STATE;
    if (!token || token <= s->last_token) return TIKU_DISPLAY_ERR_TOKEN;
    if (!d->fb || !d->front || d->fmt != TIKU_DISPLAY_FMT_RGB565 ||
        !w || !h || x >= d->w || y >= d->h || w > d->w - x || h > d->h - y ||
        d->stride < (uint32_t)d->w * 2 || (d->stride & 63u) ||
        ((uintptr_t)d->fb & 63u) || ((uintptr_t)d->front & 63u)) return TIKU_DISPLAY_ERR_INVALID;
    uintptr_t a = (uintptr_t)d->fb, b = (uintptr_t)d->front;
    uint32_t bytes = (uint32_t)d->stride * d->h;
    if (a > UINTPTR_MAX - bytes || b > UINTPTR_MAX - bytes ||
        (a >= b ? a - b < bytes : b - a < bytes)) return TIKU_DISPLAY_ERR_INVALID;
    s->display = d; s->buffer = d->fb; s->previous = d->front;
    s->last_token = token;
    s->row_start = ((uint32_t)x * 2) & ~63u;
    s->row_bytes = (((uint32_t)(x + w) * 2 + 63u) & ~63u) - s->row_start;
    s->row = y; s->rows = h;
    s->offset = (uint32_t)y * d->stride + s->row_start;
    s->end = s->offset + s->row_bytes;
    if (s->row_bytes == d->stride) {
        s->end += (uint32_t)(h - 1) * d->stride;
        s->rows = 1;
    }
    s->phase = PUBLISH;
    return TIKU_DISPLAY_PENDING;
}

/** @brief Begin retirement without releasing either framebuffer. */
static int stop(tiku_display_async_t *s, const tiku_display_async_ops_t *ops)
{
    ops->stop_start();
    s->deadline = ops->now_us();
    s->phase = STOP;
    return TIKU_DISPLAY_PENDING;
}

int tiku_display_async_step(tiku_display_async_t *s, tiku_display_t *d,
    uint64_t token, const tiku_display_async_ops_t *ops)
{
    if (!s || !ops || !d) return TIKU_DISPLAY_ERR_INVALID;
    if (!s->phase || d != s->display || token != s->last_token)
        return TIKU_DISPLAY_ERR_TOKEN;
    if (s->phase == PUBLISH) {
        uint32_t n = s->end - s->offset;
        if (n > 4096) n = 4096;
        if (ops->publish(s->buffer, s->offset, n)) return stop(s, ops);
        s->offset += n;
        if (s->offset == s->end) {
            if (--s->rows) {
                ++s->row;
                s->offset = (uint32_t)s->row * d->stride + s->row_start;
                s->end = s->offset + s->row_bytes;
            } else s->phase = SWITCH;
        }
        return TIKU_DISPLAY_PENDING;
    }
    if (s->phase == SWITCH) {
        ops->switch_start(s->buffer);
        s->deadline = ops->now_us();
        s->phase = WAIT;
        return TIKU_DISPLAY_PENDING;
    }
    if (s->phase == WAIT) {
        int result = ops->switch_poll(s->buffer);
        if (result == TIKU_DISPLAY_OK) {
            d->front = s->buffer; d->fb = s->previous; d->dirty = 0;
            tiku_display_async_reset(s);
            return TIKU_DISPLAY_OK;
        }
        if (result < 0 || (uint32_t)(ops->now_us() - s->deadline) >= 500000)
            return stop(s, ops);
        return TIKU_DISPLAY_PENDING;
    }
    if (ops->stop_poll() == TIKU_DISPLAY_OK) {
        tiku_display_async_reset(s);
        return TIKU_DISPLAY_ERR_RETIRED;
    }
    if ((uint32_t)(ops->now_us() - s->deadline) >= 500000) ops->fail_stop();
    return TIKU_DISPLAY_PENDING;
}
