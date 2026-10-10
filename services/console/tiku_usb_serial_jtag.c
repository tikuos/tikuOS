/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_usb_serial_jtag.c - bounded FIFO access for the C5 native console.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_usb_serial_jtag.h"
#include <arch/esp32c5/tiku_esp32c5_regs.h>

static uint8_t needs_zlp;

void tiku_usb_serial_jtag_init(void)
{
    needs_zlp = 0;
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
