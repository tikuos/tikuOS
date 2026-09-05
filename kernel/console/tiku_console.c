/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console.c - the one SLIP decoder and encoder under every console user.
 *
 * An END is a delimiter, never a parity toggle: the byte after it decides
 * whether a frame opens and for whom, so a stray or doubled END can neither
 * strand the decoder mid-frame nor divert typed text into a frame buffer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_console.h"
#include "tiku.h"

#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t  value;                 /**< first byte, under mask */
    uint8_t  mask;
    uint8_t  keep_first;            /**< the first byte is payload */
    uint8_t  used;
    tiku_console_frame_fn fn;
    void    *ctx;
    uint8_t *buf;                   /**< the channel's reassembly buffer */
    size_t   cap;
} channel_t;

/* The SLIP wire follows the console: the USB CDC port is the only wired
 * serial on an RP2350 native-USB build, and the UART everywhere else. */
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
static const tiku_console_wire_t boot_wire = {
    tiku_usb_cdc_putc, tiku_usb_cdc_rx_ready, tiku_usb_cdc_getc, 1u
};
#else
static const tiku_console_wire_t boot_wire = {
    tiku_uart_putc, tiku_uart_rx_ready, tiku_uart_getc, 1u
};
#endif

static const tiku_console_wire_t *wire = &boot_wire;
static channel_t channels[TIKU_CONSOLE_CHANNELS];
static uint8_t   n_channels;
static tiku_console_text_fn text_fn;
static void     *text_ctx;
static tiku_console_stats_t stats;

/* The decoder.  cur is the channel the open frame belongs to. */
static struct {
    uint8_t    armed;       /**< an END was seen; the next byte decides */
    uint8_t    in_frame;
    uint8_t    esc;
    uint8_t    overflow;    /**< the frame outgrew its buffer: drop at END */
    uint8_t    closing;     /**< the arming END closed a frame */
    channel_t *cur;
    size_t     len;
    tiku_clock_time_t opened;
} rx;

/*---------------------------------------------------------------------------*/
/* THE WIRE                                                                  */
/*---------------------------------------------------------------------------*/

void
tiku_console_set_wire(const tiku_console_wire_t *w)
{
    wire = (w != (const tiku_console_wire_t *)0) ? w : &boot_wire;
}

const tiku_console_wire_t *
tiku_console_wire(void)
{
    return wire;
}

/*---------------------------------------------------------------------------*/
/* CHANNELS                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief The channel whose (value, mask) claims @p b, or NULL. */
static channel_t *
match(uint8_t b)
{
    uint8_t i;

    for (i = 0; i < TIKU_CONSOLE_CHANNELS; i++) {
        if (channels[i].used && (b & channels[i].mask) == channels[i].value) {
            return &channels[i];
        }
    }
    return (channel_t *)0;
}

int
tiku_console_add_channel(uint8_t value, uint8_t mask, uint8_t keep_first,
                         tiku_console_frame_fn fn, void *ctx,
                         uint8_t *buf, size_t cap)
{
    uint8_t i;

    if (fn == (tiku_console_frame_fn)0 || buf == (uint8_t *)0 || cap == 0u) {
        return -1;
    }
    for (i = 0; i < TIKU_CONSOLE_CHANNELS; i++) {
        if (channels[i].used && channels[i].value == value &&
            channels[i].mask == mask) {
            break;                  /* re-registered: the entry is refreshed */
        }
    }
    if (i == TIKU_CONSOLE_CHANNELS) {
        for (i = 0; i < TIKU_CONSOLE_CHANNELS && channels[i].used; i++) {
            ;
        }
        if (i == TIKU_CONSOLE_CHANNELS) {
            return -1;
        }
        n_channels++;
    }
    channels[i].value = value;
    channels[i].mask = mask;
    channels[i].keep_first = keep_first;
    channels[i].fn = fn;
    channels[i].ctx = ctx;
    channels[i].buf = buf;
    channels[i].cap = cap;
    channels[i].used = 1u;
    return 0;
}

void
tiku_console_remove_channel(uint8_t value, uint8_t mask)
{
    uint8_t i;

    for (i = 0; i < TIKU_CONSOLE_CHANNELS; i++) {
        if (channels[i].used && channels[i].value == value &&
            channels[i].mask == mask) {
            channels[i].used = 0u;
            n_channels--;
            if (rx.cur == &channels[i]) {
                /* Its open frame has nowhere to go. */
                rx.in_frame = 0u;
                rx.cur = (channel_t *)0;
                rx.len = 0u;
                rx.esc = 0u;
            }
        }
    }
}

int
tiku_console_channel_at(uint8_t slot, uint8_t *value, uint8_t *mask,
                        size_t *cap)
{
    if (slot >= TIKU_CONSOLE_CHANNELS || !channels[slot].used) {
        return -1;
    }
    *value = channels[slot].value;
    *mask = channels[slot].mask;
    *cap = channels[slot].cap;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* OUT                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief One byte to the wire. */
static void
put(uint8_t b)
{
    wire->putc((char)b);
}

/** @brief @p len bytes of @p p to the wire, escaped the SLIP way. */
static void
put_escaped(const uint8_t *p, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        uint8_t b = p[i];

        if (b == TIKU_CONSOLE_END) {
            put(TIKU_CONSOLE_ESC);
            put(TIKU_CONSOLE_ESC_END);
        } else if (b == TIKU_CONSOLE_ESC) {
            put(TIKU_CONSOLE_ESC);
            put(TIKU_CONSOLE_ESC_ESC);
        } else if (b == 0u && wire->escape_nul) {
            put(TIKU_CONSOLE_ESC);
            put(TIKU_CONSOLE_ESC_NUL);
        } else {
            put(b);
        }
    }
}

int
tiku_console_send_frame(uint8_t marker, const void *head, size_t hlen,
                        const void *body, size_t blen)
{
    if (wire->putc == (void (*)(char))0) {
        return -1;
    }
    put(TIKU_CONSOLE_END);
    put(marker);
    if (head != (const void *)0) {
        put_escaped((const uint8_t *)head, hlen);
    }
    if (body != (const void *)0) {
        put_escaped((const uint8_t *)body, blen);
    }
    put(TIKU_CONSOLE_END);
    return 0;
}

void
tiku_console_write(const void *bytes, size_t len)
{
    const uint8_t *p = (const uint8_t *)bytes;
    size_t i;

    if (wire->putc == (void (*)(char))0) {
        return;
    }
    for (i = 0; i < len; i++) {
        put(p[i]);
    }
}

/*---------------------------------------------------------------------------*/
/* IN                                                                        */
/*---------------------------------------------------------------------------*/

/** @brief The open frame is complete: deliver it, or drop it as a unit. */
static void
close_frame(void)
{
    if (rx.overflow) {
        stats.oversize++;
    } else if (rx.len > 0u) {
        stats.frames[rx.cur - channels]++;
        rx.cur->fn(rx.cur->ctx, rx.cur->buf, rx.len);
    }
}

/**
 * @brief Sort one wire byte into a frame or into the text.
 * @return 1 when the byte belonged to a frame, 0 when it is text
 */
static uint8_t
sort_byte(uint8_t b)
{
    if (n_channels == 0u) {
        return 0u;                  /* nobody frames: every byte is text */
    }
    if (b == TIKU_CONSOLE_END) {
        if (rx.in_frame) {
            close_frame();
        }
        rx.closing = rx.in_frame;
        rx.in_frame = 0u;
        rx.armed = 1u;              /* a frame may follow; the next byte decides */
        rx.len = 0u;
        rx.esc = 0u;
        rx.overflow = 0u;
        return 1u;
    }
    if (rx.armed) {
        rx.armed = 0u;
        rx.cur = match(b);
        if (rx.cur == (channel_t *)0) {
            /* An END that closed a frame is also the one before the text
             * that follows it; only an END met outside any frame is stray. */
            if (!rx.closing) {
                stats.stray_end++;
            }
            return 0u;              /* this byte is text */
        }
        rx.in_frame = 1u;
        rx.len = 0u;
        rx.esc = 0u;
        rx.overflow = 0u;
        rx.opened = tiku_clock_time();
        if (rx.cur->keep_first) {
            rx.cur->buf[rx.len++] = b;
        }
        return 1u;
    }
    if (!rx.in_frame) {
        return 0u;
    }
    if ((tiku_clock_time_t)(tiku_clock_time() - rx.opened)
        > (tiku_clock_time_t)TIKU_CONSOLE_FRAME_TTL) {
        /* The closing END was lost: the frame is debris, and this byte
         * and every later one is text again. */
        stats.phantom++;
        rx.in_frame = 0u;
        rx.esc = 0u;
        rx.len = 0u;
        return 0u;
    }
    if (rx.esc) {
        rx.esc = 0u;
        if (b == TIKU_CONSOLE_ESC_END) {
            b = TIKU_CONSOLE_END;
        } else if (b == TIKU_CONSOLE_ESC_ESC) {
            b = TIKU_CONSOLE_ESC;
        } else if (b == TIKU_CONSOLE_ESC_NUL) {
            b = 0u;
        }
    } else if (b == TIKU_CONSOLE_ESC) {
        rx.esc = 1u;
        return 1u;
    }
    if (rx.len < rx.cur->cap) {
        rx.cur->buf[rx.len++] = b;
    } else {
        rx.overflow = 1u;
    }
    return 1u;
}

int
tiku_console_getc(void)
{
    if (wire->getc == (int (*)(void))0) {
        return -1;
    }
    while (wire->rx_ready()) {
        int ch = wire->getc();

        if (ch < 0) {
            break;
        }
        if (!sort_byte((uint8_t)ch)) {
            return ch;
        }
    }
    return -1;
}

void
tiku_console_set_text_sink(tiku_console_text_fn fn, void *ctx)
{
    text_fn = fn;
    text_ctx = ctx;
}

void
tiku_console_pump(void)
{
    if (wire->getc == (int (*)(void))0) {
        return;
    }
    while (wire->rx_ready()) {
        int ch = wire->getc();

        if (ch < 0) {
            break;
        }
        if (!sort_byte((uint8_t)ch) && text_fn != (tiku_console_text_fn)0) {
            (void)text_fn(text_ctx, ch);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* OBSERVABILITY                                                             */
/*---------------------------------------------------------------------------*/

const tiku_console_stats_t *
tiku_console_stats(void)
{
    return &stats;
}

void
tiku_console_reset(void)
{
    uint8_t i;

    for (i = 0; i < TIKU_CONSOLE_CHANNELS; i++) {
        channels[i].used = 0u;
        stats.frames[i] = 0u;
    }
    n_channels = 0u;
    stats.stray_end = 0u;
    stats.oversize = 0u;
    stats.phantom = 0u;
    rx.armed = 0u;
    rx.in_frame = 0u;
    rx.esc = 0u;
    rx.overflow = 0u;
    rx.closing = 0u;
    rx.cur = (channel_t *)0;
    rx.len = 0u;
}
