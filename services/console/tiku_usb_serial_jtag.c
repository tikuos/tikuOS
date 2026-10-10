/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_usb_serial_jtag.c - bounded FIFO access for the C5 native console.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_usb_serial_jtag.h"
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#include <arch/esp32c5/tiku_timer_arch.h>
#include <kernel/timers/tiku_clock.h>

/* Bytes loaded into the IN endpoint while no host collects them stay there:
 * the endpoint never reports free again and every later write is lost.  The
 * host's start-of-frame token advances the frame counter once a millisecond
 * while the link is enumerated, so a write loads the endpoint only when the
 * counter has moved within this many clock ticks and otherwise reports its
 * bytes as sent.  The window that matters is boot after an RTC-watchdog
 * reset: the unit re-enumerates and the kernel prints before the host has
 * finished.  The clock stands still until tiku_clock_init(); a counter that
 * moved while it stood still counts as moved now; the diagnostic image has
 * no clock at all and treats every moment that way. */
#define TIKU_C5_USB_LINK_HORIZON TIKU_CLOCK_MS_TO_TICKS(16)
/* Reads of the frame counter at init before a link that has not moved it is
 * taken as dead: on a live link the counter moves within a millisecond and
 * the first byte is loaded; bounded by reads, each an APB access, so that
 * no clock is needed before the kernel's runs. */
#define TIKU_C5_USB_LINK_PROBE 50000u

static uint8_t needs_zlp;
static uint32_t frame_last;
static tiku_clock_time_t frame_moved;
static uint8_t frame_seen;

#if (TIKU_MINIMAL + 0)
#define link_now() ((tiku_clock_time_t)0)
#else
#define link_now() tiku_clock_time()
#endif

static int link_live(void)
{
    uint32_t frame = TIKU_C5_REG_READ(TIKU_C5_USB_FRAM_NUM);
    tiku_clock_time_t now = link_now();

    if (frame != frame_last) {
        frame_last = frame;
        frame_moved = now;
        frame_seen = 1;
    }
    return frame_seen &&
           (tiku_clock_time_t)(now - frame_moved) <= TIKU_C5_USB_LINK_HORIZON;
}

void tiku_usb_serial_jtag_init(void)
{
    uint32_t probe;

    needs_zlp = 0;
    frame_last = TIKU_C5_REG_READ(TIKU_C5_USB_FRAM_NUM);
    frame_seen = 0;
    for (probe = 0; probe < TIKU_C5_USB_LINK_PROBE; probe++) {
        if (link_live()) {
            break;
        }
    }
    TIKU_C5_REG_WRITE(TIKU_C5_USB_INT_ENA, 0u);
}

void tiku_usb_serial_jtag_poll(void)
{
    if (needs_zlp &&
        (TIKU_C5_REG_READ(TIKU_C5_USB_EP1_CONF) & TIKU_C5_USB_TX_FREE)) {
        TIKU_C5_REG_WRITE(TIKU_C5_USB_EP1_CONF, TIKU_C5_USB_WR_DONE);
        needs_zlp = 0;
    }
}

size_t tiku_usb_serial_jtag_write(const uint8_t *data, size_t length)
{
    size_t sent = 0;

    if (data == NULL) {
        return 0;
    }
    if (length > TIKU_C5_USB_PACKET_SIZE) {
        length = TIKU_C5_USB_PACKET_SIZE;
    }
    if (length != 0 && !link_live()) {
        return length;
    }
    while (sent < length &&
           (TIKU_C5_REG_READ(TIKU_C5_USB_EP1_CONF) & TIKU_C5_USB_TX_FREE)) {
        TIKU_C5_REG_WRITE(TIKU_C5_USB_EP1, data[sent++]);
    }
    if (sent != 0) {
        /* A full packet auto-flushes; USB requires a later short packet or ZLP.
         */
        needs_zlp = (sent == TIKU_C5_USB_PACKET_SIZE);
        TIKU_C5_REG_WRITE(TIKU_C5_USB_EP1_CONF, TIKU_C5_USB_WR_DONE);
    }
    return sent;
}

int tiku_usb_serial_jtag_getc(void)
{
    if (!(TIKU_C5_REG_READ(TIKU_C5_USB_EP1_CONF) & TIKU_C5_USB_RX_AVAIL)) {
        return -1;
    }
    return (int)(TIKU_C5_REG_READ(TIKU_C5_USB_EP1) & 0xFFu);
}
