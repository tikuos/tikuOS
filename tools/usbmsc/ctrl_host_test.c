/*
 * Tiku Operating System v0.06
 * ctrl_host_test.c - exercise kernel/usb/tiku_usbd_ctrl.c on a Linux host.
 * Build: tools/usbmsc/Makefile     Run: ./ctrl_host_test
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The oracle is the byte sequence the nRF54LM20 console and disk faces
 * shipped (commits 4d65dd3 and d7a001d), typed in here verbatim: the
 * builders must produce exactly those bytes, so moving the ports onto the
 * core changes nothing a host can see.  The decision engine is then walked
 * through every request the four ports answer, including the ones that
 * must be refused.
 */

#include <stdio.h>
#include <string.h>
#include "kernel/usb/tiku_usbd_ctrl.h"

static int g_pass, g_fail;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; } else { g_fail++; }
    printf("  %s  %s\n", cond ? "pass" : "FAIL", what);
}

/* The LM20 console's configuration as shipped (75 bytes, bulk 64). */
static const uint8_t lm20_cdc_conf[75] = {
    9, 0x02, 75, 0x00, 0x02, 0x01, 0x00, 0x80, 50,
    8, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
    9, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 0x01,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 0x00, 0x01,
    7, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
    9, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    7, 0x05, 0x02, 0x02, 64, 0x00, 0x00,
    7, 0x05, 0x83, 0x02, 64, 0x00, 0x00
};
/* The LM20 console's device descriptor as shipped. */
static const uint8_t lm20_cdc_dev[18] = {
    18, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, 64,
    0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};
/* The LM20 disk's configuration and device descriptors as shipped. */
static const uint8_t lm20_msc_conf[32] = {
    9, 0x02, 32, 0x00, 0x01, 0x01, 0x00, 0x80, 50,
    9, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    7, 0x05, 0x02, 0x02, 64, 0x00, 0x00,
    7, 0x05, 0x83, 0x02, 64, 0x00, 0x00
};
static const uint8_t lm20_msc_dev[18] = {
    18, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
    0x09, 0x12, 0x02, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};
static const uint8_t lm20_str_prod[30] = { 30, 0x03, 'T',0,'i',0,'k',0,'u',0,
    'O',0,'S',0,' ',0,'C',0,'o',0,'n',0,'s',0,'o',0,'l',0,'e',0 };

static void test_builders(void)
{
    uint8_t buf[80];
    uint16_t n;

    printf("builders reproduce the shipped bytes:\n");
    tiku_usbd_device_desc(buf, TIKU_USBD_PID_CONSOLE, 1, 64u);
    ok(memcmp(buf, lm20_cdc_dev, 18) == 0, "device descriptor, console");
    tiku_usbd_device_desc(buf, TIKU_USBD_PID_DISK, 0, 64u);
    ok(memcmp(buf, lm20_msc_dev, 18) == 0, "device descriptor, disk");
    tiku_usbd_cdc_config(buf, 1u, 2u, 3u, 64u);
    ok(memcmp(buf, lm20_cdc_conf, 75) == 0, "CDC configuration, 75 bytes");
    tiku_usbd_msc_config(buf, 2u, 3u, 64u);
    ok(memcmp(buf, lm20_msc_conf, 32) == 0, "MSC configuration, 32 bytes");
    n = tiku_usbd_string_ascii(buf, sizeof buf, "TikuOS Console");
    ok(n == 30u && memcmp(buf, lm20_str_prod, 30) == 0, "product string");
    {
        static const uint8_t id[6] = { 0x0D, 0x16, 0xD8, 0x16, 0x38, 0xB9 };

        n = tiku_usbd_string_serial(buf, sizeof buf, id, 6u);
        ok(n == 26u && buf[0] == 26u && buf[1] == 3u && buf[2] == '0' &&
           buf[4] == 'D' && buf[24] == '9', "serial from a 6-byte id");
    }
    /* The high-speed patch touches only BULK endpoints. */
    tiku_usbd_cdc_config(buf, 1u, 2u, 3u, 64u);
    tiku_usbd_desc_set_bulk_mps(buf, 75u, 512u);
    ok(buf[49] == 0x08 && buf[50] == 0x00, "interrupt endpoint untouched");
    ok(buf[65] == 0x00 && buf[66] == 0x02 && buf[72] == 0x00 && buf[73] == 0x02,
       "both bulk endpoints now 512");
    tiku_usbd_desc_set_bulk_mps(buf, 75u, 64u);
    ok(memcmp(buf, lm20_cdc_conf, 75) == 0, "and back to 64, byte-exact");
}

static tiku_usbd_desc_set_t g_set;
static uint8_t g_dev[18], g_conf[80], g_prod[40];

static void bind(uint8_t klass, uint8_t self_powered)
{
    uint16_t ll;

    if (klass == TIKU_USBD_CLASS_CDC) {
        tiku_usbd_device_desc(g_dev, TIKU_USBD_PID_CONSOLE, 1, 64u);
        tiku_usbd_cdc_config(g_conf, 1u, 2u, 3u, 64u);
        g_set.config_len = TIKU_USBD_CDC_CONFIG_LEN;
    } else {
        tiku_usbd_device_desc(g_dev, TIKU_USBD_PID_DISK, 0, 64u);
        tiku_usbd_msc_config(g_conf, 2u, 3u, 64u);
        g_set.config_len = TIKU_USBD_MSC_CONFIG_LEN;
    }
    g_set.device = g_dev; g_set.config = g_conf;
    g_set.string[0] = tiku_usbd_string_lang(&ll); g_set.string_len[0] = ll;
    g_set.string[2] = g_prod;
    g_set.string_len[2] = tiku_usbd_string_ascii(g_prod, sizeof g_prod,
                                                  "TikuOS Console");
    g_set.klass = klass; g_set.self_powered = self_powered;
}

static void setup(tiku_usbd_ctrl_t *c, tiku_usbd_ctrl_out_t *o,
                  uint8_t t, uint8_t r, uint16_t v, uint16_t i, uint16_t l)
{
    uint8_t p[8] = { t, r, (uint8_t)v, (uint8_t)(v >> 8),
                     (uint8_t)i, (uint8_t)(i >> 8),
                     (uint8_t)l, (uint8_t)(l >> 8) };

    tiku_usbd_ctrl_setup(c, p, o);
}

static void test_decisions(void)
{
    tiku_usbd_ctrl_t c;
    tiku_usbd_ctrl_out_t o;

    printf("standard requests:\n");
    bind(TIKU_USBD_CLASS_CDC, 0u);
    tiku_usbd_ctrl_init(&c, &g_set);

    setup(&c, &o, 0x80, 0x06, 0x0100, 0, 64);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.data == g_dev && o.len == 18,
       "GET_DESCRIPTOR device: the 18 bytes");
    setup(&c, &o, 0x80, 0x06, 0x0100, 0, 8);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 8,
       "GET_DESCRIPTOR device, first 8 asked: clamped to 8");
    setup(&c, &o, 0x80, 0x06, 0x0200, 0, 9);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.data == g_conf && o.len == 9,
       "GET_DESCRIPTOR config header: 9 of 75");
    setup(&c, &o, 0x80, 0x06, 0x0200, 0, 255);
    ok(o.len == 75, "GET_DESCRIPTOR config whole: 75");
    setup(&c, &o, 0x80, 0x06, 0x0300, 0, 255);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 4, "string 0: language");
    setup(&c, &o, 0x80, 0x06, 0x0302, 0x0409, 255);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.data == g_prod && o.len == 30,
       "string 2: product");
    setup(&c, &o, 0x80, 0x06, 0x0301, 0x0409, 255);
    ok(o.action == TIKU_USBD_CTRL_STALL, "string 1 unset: stall");
    setup(&c, &o, 0x80, 0x06, 0x0600, 0, 10);
    ok(o.action == TIKU_USBD_CTRL_STALL, "DEVICE_QUALIFIER: stall");
    setup(&c, &o, 0x80, 0x06, 0x0700, 0, 10);
    ok(o.action == TIKU_USBD_CTRL_STALL, "OTHER_SPEED: stall");
    setup(&c, &o, 0x80, 0x06, 0x0F00, 0, 5);
    ok(o.action == TIKU_USBD_CTRL_STALL, "BOS: stall");

    setup(&c, &o, 0x00, 0x05, 60, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && (o.effects & TIKU_USBD_FX_ADDRESS)
       && o.address == 60, "SET_ADDRESS 60: status + apply 60 (your timing)");
    setup(&c, &o, 0x00, 0x05, 0x1FF, 0, 0);
    ok(o.address == 0x7F, "SET_ADDRESS masks to 7 bits");

    setup(&c, &o, 0x80, 0x06, 0x0800 | 0x0100, 0, 1);
    setup(&c, &o, 0x80, 0x08, 0, 0, 1);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 1 && o.data[0] == 0,
       "GET_CONFIGURATION before any: 0");
    setup(&c, &o, 0x00, 0x09, 1, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && (o.effects & TIKU_USBD_FX_CONFIG)
       && o.config == 1, "SET_CONFIGURATION 1: status + open endpoints");
    setup(&c, &o, 0x80, 0x08, 0, 0, 1);
    ok(o.data[0] == 1, "GET_CONFIGURATION after: 1");
    setup(&c, &o, 0x00, 0x09, 0, 0, 0);
    ok((o.effects & TIKU_USBD_FX_CONFIG) && o.config == 0,
       "SET_CONFIGURATION 0: unconfigure");

    setup(&c, &o, 0x80, 0x00, 0, 0, 2);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 2 && o.data[0] == 0 &&
       o.data[1] == 0, "GET_STATUS device, bus powered: 0 0");
    bind(TIKU_USBD_CLASS_MSC, 1u);
    tiku_usbd_ctrl_init(&c, &g_set);
    setup(&c, &o, 0x80, 0x00, 0, 0, 2);
    ok(o.data[0] == 1 && o.data[1] == 0, "GET_STATUS device, self powered: 1 0");
    setup(&c, &o, 0x82, 0x00, 0, 0x83, 2);
    ok(o.data[0] == 0, "GET_STATUS endpoint: not halted");
    setup(&c, &o, 0x81, 0x0A, 0, 0, 1);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 1 && o.data[0] == 0,
       "GET_INTERFACE: alternate 0");
    setup(&c, &o, 0x01, 0x0B, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS, "SET_INTERFACE 0: accepted");
    setup(&c, &o, 0x02, 0x01, 0, 0x83, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && (o.effects & TIKU_USBD_FX_UNHALT_EP)
       && o.ep == 0x83, "CLEAR_FEATURE halt on 0x83: unhalt 0x83");
    setup(&c, &o, 0x02, 0x03, 0, 0x02, 0);
    ok((o.effects & TIKU_USBD_FX_HALT_EP) && o.ep == 0x02,
       "SET_FEATURE halt on 0x02: halt 0x02");
    setup(&c, &o, 0x00, 0x03, 1, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && o.effects == 0,
       "SET_FEATURE remote wakeup: accepted, nothing to do");
    setup(&c, &o, 0x40, 0x01, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STALL, "vendor request: stall");
    setup(&c, &o, 0x00, 0x0C, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STALL, "unknown standard request: stall");

    printf("CDC class:\n");
    bind(TIKU_USBD_CLASS_CDC, 0u);
    tiku_usbd_ctrl_init(&c, &g_set);
    setup(&c, &o, 0x21, 0x22, 0x0003, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && (o.effects & TIKU_USBD_FX_LINE_STATE)
       && o.dtr == 1, "SET_CONTROL_LINE_STATE DTR|RTS: dtr 1");
    setup(&c, &o, 0x21, 0x22, 0x0002, 0, 0);
    ok(o.dtr == 0 && c.dtr == 0, "SET_CONTROL_LINE_STATE RTS only: dtr 0");
    setup(&c, &o, 0x21, 0x20, 0, 0, 7);
    ok(o.action == TIKU_USBD_CTRL_ACCEPT_OUT && o.len == 7,
       "SET_LINE_CODING: take 7 bytes, then status");
    setup(&c, &o, 0xA1, 0x21, 0, 0, 7);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 7 && o.data[0] == 0x00 &&
       o.data[1] == 0xC2 && o.data[2] == 0x01 && o.data[6] == 8,
       "GET_LINE_CODING: 115200 8N1");
    setup(&c, &o, 0x21, 0x23, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS, "unknown class, no data: accepted");
    setup(&c, &o, 0xA1, 0x23, 0, 0, 4);
    ok(o.action == TIKU_USBD_CTRL_STALL, "unknown class asking data: stall");
    setup(&c, &o, 0xA1, 0xFE, 0, 0, 1);
    ok(o.action == TIKU_USBD_CTRL_STALL, "GET_MAX_LUN on a console: stall");

    printf("MSC class:\n");
    bind(TIKU_USBD_CLASS_MSC, 0u);
    tiku_usbd_ctrl_init(&c, &g_set);
    setup(&c, &o, 0xA1, 0xFE, 0, 0, 1);
    ok(o.action == TIKU_USBD_CTRL_REPLY && o.len == 1 && o.data[0] == 0,
       "GET_MAX_LUN: 0");
    setup(&c, &o, 0x21, 0xFF, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS &&
       (o.effects & TIKU_USBD_FX_CLASS_RESET), "BULK-ONLY RESET: status + reset");
    setup(&c, &o, 0x21, 0xFE, 0, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && o.len == 0 &&
       !(o.effects & TIKU_USBD_FX_CLASS_RESET),
       "GET_MAX_LUN sent host-to-device: not the reply, plain accept");
    setup(&c, &o, 0x21, 0x22, 1, 0, 0);
    ok(o.action == TIKU_USBD_CTRL_STATUS && !(o.effects & TIKU_USBD_FX_LINE_STATE),
       "line state on a disk: accepted, no CDC effect");
}

int main(void)
{
    test_builders();
    test_decisions();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
