/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbd_ctrl.c - control transfers, descriptors and class requests.
 *
 * Pure functions over buffers plus one small context, exercised on the
 * build machine (tools/usbmsc) against the bytes the ports shipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_usbd_ctrl.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* DESCRIPTOR BUILDERS                                                       */
/*---------------------------------------------------------------------------*/

#define DESC_DEVICE     0x01u
#define DESC_CONFIG     0x02u
#define DESC_STRING     0x03u
#define DESC_INTERFACE  0x04u
#define DESC_ENDPOINT   0x05u
#define DESC_IAD        0x0Bu
#define DESC_CS_IFACE   0x24u

void tiku_usbd_device_desc(uint8_t *out, uint16_t pid, int composite,
                           uint8_t ep0_mps)
{
    static const uint8_t head[4] = { TIKU_USBD_DEVICE_LEN, DESC_DEVICE,
                                     0x00, 0x02 };        /* USB 2.00     */

    memcpy(out, head, 4u);
    if (composite) {
        out[4] = 0xEFu; out[5] = 0x02u; out[6] = 0x01u;   /* misc/common/IAD */
    } else {
        out[4] = 0x00u; out[5] = 0x00u; out[6] = 0x00u;   /* at the interface */
    }
    out[7]  = ep0_mps;
    out[8]  = (uint8_t)(TIKU_USBD_VID & 0xFFu);
    out[9]  = (uint8_t)(TIKU_USBD_VID >> 8);
    out[10] = (uint8_t)(pid & 0xFFu);
    out[11] = (uint8_t)(pid >> 8);
    out[12] = 0x00u; out[13] = 0x01u;                     /* bcdDevice 1.00 */
    out[14] = 1u; out[15] = 2u; out[16] = 3u;             /* string indices */
    out[17] = 1u;                                         /* one configuration */
}

static uint8_t *put_endpoint(uint8_t *p, uint8_t addr, uint8_t attr,
                             uint16_t mps, uint8_t interval)
{
    p[0] = 7u; p[1] = DESC_ENDPOINT; p[2] = addr; p[3] = attr;
    p[4] = (uint8_t)(mps & 0xFFu); p[5] = (uint8_t)(mps >> 8);
    p[6] = interval;
    return p + 7;
}

static uint8_t *put_interface(uint8_t *p, uint8_t num, uint8_t n_ep,
                              uint8_t klass, uint8_t sub, uint8_t proto)
{
    p[0] = 9u; p[1] = DESC_INTERFACE; p[2] = num; p[3] = 0u; p[4] = n_ep;
    p[5] = klass; p[6] = sub; p[7] = proto; p[8] = 0u;
    return p + 9;
}

void tiku_usbd_cdc_config(uint8_t *out, uint8_t ep_notify, uint8_t ep_out,
                          uint8_t ep_in, uint16_t bulk_mps)
{
    uint8_t *p = out;
    static const uint8_t cs[] = {
        5, DESC_CS_IFACE, 0x00, 0x10, 0x01,   /* header, CDC 1.10          */
        5, DESC_CS_IFACE, 0x01, 0x00, 0x01,   /* call management           */
        4, DESC_CS_IFACE, 0x02, 0x02,         /* ACM capabilities          */
        5, DESC_CS_IFACE, 0x06, 0x00, 0x01    /* union: comm 0, data 1     */
    };

    p[0] = 9u; p[1] = DESC_CONFIG;
    p[2] = TIKU_USBD_CDC_CONFIG_LEN; p[3] = 0u;
    p[4] = 2u; p[5] = 1u; p[6] = 0u; p[7] = 0x80u; p[8] = 50u;
    p += 9;
    /* Association: two interfaces from #0, communications / ACM. */
    p[0] = 8u; p[1] = DESC_IAD; p[2] = 0u; p[3] = 2u;
    p[4] = 0x02u; p[5] = 0x02u; p[6] = 0x00u; p[7] = 0u;
    p += 8;
    p = put_interface(p, 0u, 1u, 0x02u, 0x02u, 0x00u);
    memcpy(p, cs, sizeof cs);
    p += sizeof cs;
    p = put_endpoint(p, (uint8_t)(0x80u | ep_notify), 0x03u, 8u, 0x10u);
    p = put_interface(p, 1u, 2u, 0x0Au, 0x00u, 0x00u);
    p = put_endpoint(p, ep_out, 0x02u, bulk_mps, 0u);
    p = put_endpoint(p, (uint8_t)(0x80u | ep_in), 0x02u, bulk_mps, 0u);
    (void)p;
}

void tiku_usbd_msc_config(uint8_t *out, uint8_t ep_out, uint8_t ep_in,
                          uint16_t bulk_mps)
{
    uint8_t *p = out;

    p[0] = 9u; p[1] = DESC_CONFIG;
    p[2] = TIKU_USBD_MSC_CONFIG_LEN; p[3] = 0u;
    p[4] = 1u; p[5] = 1u; p[6] = 0u; p[7] = 0x80u; p[8] = 50u;
    p += 9;
    /* Mass storage / SCSI transparent / Bulk-Only Transport. */
    p = put_interface(p, 0u, 2u, 0x08u, 0x06u, 0x50u);
    p = put_endpoint(p, ep_out, 0x02u, bulk_mps, 0u);
    p = put_endpoint(p, (uint8_t)(0x80u | ep_in), 0x02u, bulk_mps, 0u);
    (void)p;
}

void tiku_usbd_desc_set_bulk_mps(uint8_t *config, uint16_t len, uint16_t mps)
{
    uint16_t i = 0u;

    while (i + 7u <= len) {
        uint8_t dl = config[i];

        if (dl < 2u) {
            break;
        }
        if (dl == 7u && config[i + 1u] == DESC_ENDPOINT &&
            (config[i + 3u] & 0x03u) == 0x02u) {
            config[i + 4u] = (uint8_t)(mps & 0xFFu);
            config[i + 5u] = (uint8_t)(mps >> 8);
        }
        i = (uint16_t)(i + dl);
    }
}

const uint8_t *tiku_usbd_string_lang(uint16_t *len)
{
    static const uint8_t lang[4] = { 4, DESC_STRING, 0x09, 0x04 };

    if (len != (uint16_t *)0) {
        *len = 4u;
    }
    return lang;
}

uint16_t tiku_usbd_string_ascii(uint8_t *out, size_t cap, const char *s)
{
    uint16_t n = 2u;

    while (*s != '\0' && (size_t)n + 2u <= cap) {
        out[n++] = (uint8_t)*s++;
        out[n++] = 0u;
    }
    out[0] = (uint8_t)n;
    out[1] = DESC_STRING;
    return n;
}

uint16_t tiku_usbd_string_serial(uint8_t *out, size_t cap,
                                 const uint8_t *id, uint8_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    uint16_t k = 2u;
    uint8_t i;

    for (i = 0u; i < n && (size_t)k + 4u <= cap; i++) {
        out[k++] = (uint8_t)hex[(id[i] >> 4) & 0xFu];
        out[k++] = 0u;
        out[k++] = (uint8_t)hex[id[i] & 0xFu];
        out[k++] = 0u;
    }
    out[0] = (uint8_t)k;
    out[1] = DESC_STRING;
    return k;
}

/*---------------------------------------------------------------------------*/
/* DECISIONS                                                                 */
/*---------------------------------------------------------------------------*/

const uint8_t *tiku_usbd_cdc_line_coding(void)
{
    static const uint8_t lc[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };

    return lc;                              /* 115200, 1 stop, none, 8   */
}

void tiku_usbd_ctrl_init(tiku_usbd_ctrl_t *c, const tiku_usbd_desc_set_t *set)
{
    memset(c, 0, sizeof *c);
    c->set = set;
}

static void reply(tiku_usbd_ctrl_out_t *out, const uint8_t *d, uint16_t n,
                  uint16_t asked)
{
    out->action = TIKU_USBD_CTRL_REPLY;
    out->data   = d;
    out->len    = (n < asked) ? n : asked;
}

static void class_request(tiku_usbd_ctrl_t *c, uint8_t req_type, uint8_t req,
                          uint16_t value, uint16_t length,
                          tiku_usbd_ctrl_out_t *out)
{
    uint8_t klass = c->set->klass;

    if (klass == TIKU_USBD_CLASS_CDC) {
        switch (req) {
        case 0x22:                          /* SET_CONTROL_LINE_STATE     */
            c->dtr = (uint8_t)(value & 1u);
            out->dtr = c->dtr;
            out->effects |= TIKU_USBD_FX_LINE_STATE;
            out->action = TIKU_USBD_CTRL_STATUS;
            return;
        case 0x20:                          /* SET_LINE_CODING, 7 bytes    */
            out->action = TIKU_USBD_CTRL_ACCEPT_OUT;
            out->len = 7u;
            return;
        case 0x21:                          /* GET_LINE_CODING             */
            reply(out, tiku_usbd_cdc_line_coding(), 7u, length);
            return;
        default:
            break;
        }
    } else if (klass == TIKU_USBD_CLASS_MSC) {
        switch (req) {
        case 0xFE:                          /* GET_MAX_LUN: one, so 0      */
            if ((req_type & 0x80u) != 0u) {
                c->reply[0] = 0u;
                reply(out, c->reply, 1u, length);
                return;
            }
            break;
        case 0xFF:                          /* BULK-ONLY RESET             */
            if ((req_type & 0x80u) == 0u) {
                out->effects |= TIKU_USBD_FX_CLASS_RESET;
                out->action = TIKU_USBD_CTRL_STATUS;
                return;
            }
            break;
        default:
            break;
        }
    }
    /* A class request with no data it has no answer for: accept and say
     * nothing, as every port did; one asking for data is stalled. */
    if ((req_type & 0x80u) == 0u && length == 0u) {
        out->action = TIKU_USBD_CTRL_STATUS;
    } else {
        out->action = TIKU_USBD_CTRL_STALL;
    }
}

void tiku_usbd_ctrl_setup(tiku_usbd_ctrl_t *c, const uint8_t *p,
                          tiku_usbd_ctrl_out_t *out)
{
    uint8_t  req_type = p[0], req = p[1];
    uint16_t value  = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    uint16_t index  = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
    uint16_t length = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));
    const tiku_usbd_desc_set_t *s = c->set;

    memset(out, 0, sizeof *out);
    out->action = TIKU_USBD_CTRL_STALL;

    if ((req_type & 0x60u) == 0x20u) {
        class_request(c, req_type, req, value, length, out);
        return;
    }
    if ((req_type & 0x60u) != 0u) {         /* vendor: nothing to say      */
        return;
    }

    switch (req) {
    case 0x05:                               /* SET_ADDRESS                 */
        out->address = (uint8_t)(value & 0x7Fu);
        out->effects |= TIKU_USBD_FX_ADDRESS;
        out->action = TIKU_USBD_CTRL_STATUS;
        return;
    case 0x06: {                             /* GET_DESCRIPTOR              */
        uint8_t type = (uint8_t)(value >> 8), idx = (uint8_t)(value & 0xFFu);

        if (type == DESC_DEVICE) {
            reply(out, s->device, TIKU_USBD_DEVICE_LEN, length);
        } else if (type == DESC_CONFIG) {
            reply(out, s->config, s->config_len, length);
        } else if (type == DESC_STRING && idx < 4u &&
                   s->string[idx] != (const uint8_t *)0) {
            reply(out, s->string[idx], s->string_len[idx], length);
        }
        /* Qualifier, other-speed, BOS and the rest: a stall is the answer a
         * host expects from a device that has none, not a failure. */
        return;
    }
    case 0x09:                               /* SET_CONFIGURATION           */
        c->config = (uint8_t)(value & 0xFFu);
        out->config = c->config;
        out->effects |= TIKU_USBD_FX_CONFIG;
        out->action = TIKU_USBD_CTRL_STATUS;
        return;
    case 0x08:                               /* GET_CONFIGURATION           */
        c->reply[0] = c->config;
        reply(out, c->reply, 1u, length);
        return;
    case 0x00:                               /* GET_STATUS                  */
        c->reply[0] = ((req_type & 0x1Fu) == 0u && s->self_powered) ? 1u : 0u;
        c->reply[1] = 0u;
        reply(out, c->reply, 2u, length);
        return;
    case 0x0A:                               /* GET_INTERFACE: alternate 0  */
        c->reply[0] = 0u;
        reply(out, c->reply, 1u, length);
        return;
    case 0x01:                               /* CLEAR_FEATURE               */
    case 0x03:                               /* SET_FEATURE                 */
        if ((req_type & 0x1Fu) == 0x02u && value == 0u) {   /* ENDPOINT_HALT */
            out->ep = (uint8_t)(index & 0xFFu);
            out->effects |= (req == 0x01u) ? TIKU_USBD_FX_UNHALT_EP
                                           : TIKU_USBD_FX_HALT_EP;
        }
        out->action = TIKU_USBD_CTRL_STATUS;
        return;
    case 0x0B:                               /* SET_INTERFACE: only 0       */
        out->action = TIKU_USBD_CTRL_STATUS;
        return;
    default:
        return;
    }
}
