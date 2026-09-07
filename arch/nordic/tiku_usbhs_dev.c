/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_dev.c - nRF54LM20 USB device mode on the DWC2 core.
 *
 * Device configuration, the FIFO layout the core's own GHWCFG3 allows, and
 * the EP0 control engine that carries enumeration.  The core moves packets
 * by internal DMA, so every buffer here is word-aligned.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_usbhs_arch.h"

#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <kernel/cpu/tiku_common.h>

#include <string.h>

/*---------------------------------------------------------------------------*/
/* CORE REGISTER BITS                                                        */
/*---------------------------------------------------------------------------*/

#define GAHBCFG_GLBLINTRMSK (1u << 0)
#define GAHBCFG_DMAEN       (1u << 5)
#define GAHBCFG_HBSTLEN_I4  (3u << 1)      /* INCR4 bursts                  */

#define GUSBCFG_PHYIF16      (1u << 3)
#define GUSBCFG_FORCEDEVMODE (1ul << 30)
#define GUSBCFG_TRDTIM_SHIFT 10
#define GUSBCFG_TRDTIM_MASK  (0xFul << 10)

#define GRSTCTL_TXFFLSH     (1u << 5)
#define GRSTCTL_RXFFLSH     (1u << 4)
#define GRSTCTL_TXFNUM_ALL  (0x10u << 6)
#define GRSTCTL_AHBIDLE     (1ul << 31)

#define GINT_USBSUSP        (1u << 11)
#define GINT_USBRST         (1u << 12)
#define GINT_ENUMDONE       (1u << 13)
#define GINT_IEPINT         (1u << 18)
#define GINT_OEPINT         (1u << 19)
#define GINT_WKUPINT        (1ul << 31)

#define DCFG_NZSTSOUTHSHK   (1u << 2)
#define DCFG_DEVADDR_SHIFT  4
#define DCFG_DEVADDR_MASK   (0x7Fu << 4)

#define DCTL_SFTDISCON      (1u << 1)
#define DCTL_CGNPINNAK      (1u << 8)
#define DCTL_CGOUTNAK       (1u << 10)

#define DEPCTL_USBACTEP     (1u << 15)
#define DEPCTL_STALL        (1u << 21)
#define DEPCTL_CNAK         (1u << 26)
#define DEPCTL_SNAK         (1u << 27)
#define DEPCTL_SETD0PID     (1u << 28)
#define DEPCTL_EPDIS        (1ul << 30)
#define DEPCTL_EPENA        (1ul << 31)
#define DEPCTL_TXFNUM_SHIFT 22
#define DEPCTL_EPTYPE_SHIFT 18

#define DEPINT_XFERCOMPL    (1u << 0)
#define DOEPINT_SETUP       (1u << 3)

#define DIEPTSIZ_PKTCNT_SHIFT 19
#define DOEPTSIZ_PKTCNT_SHIFT 19
#define DOEPTSIZ_SUPCNT_SHIFT 29

/* EP0's packet size is an enumeration in the control register: 0 is 64. */
#define DEPCTL0_MPS_64      0u

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

#define USB_EP0_MPS         64u
#define USB_BULK_MPS        64u

/* FIFO layout in words, inside the 3040 the core reports.  The receive FIFO
 * is shared by every OUT endpoint; each IN endpoint gets its own. */
#define FIFO_RX_WORDS       256u
#define FIFO_EP0IN_WORDS    64u
#define FIFO_EP1IN_WORDS    64u
#define FIFO_EP3IN_WORDS    128u

#define USBHS_SPINS         2000000u

/*---------------------------------------------------------------------------*/
/* DESCRIPTORS                                                               */
/*---------------------------------------------------------------------------*/

/* pid.codes 1209:0001, as the other two TikuOS device stacks use. */
static const uint8_t dev_desc[18] = {
    18, 0x01,
    0x00, 0x02,                 /* USB 2.00                                 */
    0xEF, 0x02, 0x01,           /* misc / common class / interface assoc.   */
    USB_EP0_MPS,
    0x09, 0x12,                 /* idVendor  0x1209                         */
    0x01, 0x00,                 /* idProduct 0x0001                         */
    0x00, 0x01,                 /* bcdDevice 1.00                           */
    0x01, 0x02, 0x03,
    0x01
};

#define CONF_TOTAL_LEN 75
static const uint8_t conf_desc[CONF_TOTAL_LEN] = {
    9, 0x02, CONF_TOTAL_LEN, 0x00, 0x02, 0x01, 0x00, 0x80, 50,
    /* CDC association, two interfaces from #0 */
    8, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
    /* Interface 0: communications, abstract control model */
    9, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 0x01,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 0x00, 0x01,
    /* Notification endpoint, IN 1 */
    7, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
    /* Interface 1: the data class, two bulk endpoints */
    9, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    7, 0x05, 0x02, 0x02, USB_BULK_MPS, 0x00, 0x00,
    7, 0x05, 0x83, 0x02, USB_BULK_MPS, 0x00, 0x00
};

static const uint8_t str_lang[4]  = { 4, 0x03, 0x09, 0x04 };
static const uint8_t str_mfr[14]  = { 14, 0x03, 'T',0,'i',0,'k',0,'u',0,
                                      'O',0,'S',0 };
static const uint8_t str_prod[30] = { 30, 0x03, 'T',0,'i',0,'k',0,'u',0,'O',0,
                                      'S',0,' ',0,'C',0,'o',0,'n',0,'s',0,
                                      'o',0,'l',0,'e',0 };

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/* The core writes these by DMA, so they are word-aligned and never on the
 * stack.  The setup buffer holds the three packets the core is armed for. */
static uint32_t setup_buf[6];
static uint8_t  ep0_in_buf[USB_EP0_MPS] __attribute__((aligned(4)));
static uint8_t  serial_buf[26] __attribute__((aligned(4)));

static uint8_t  s_started;
static uint8_t  s_address;      /* the address the host assigned            */
static uint8_t  s_pending_addr; /* applied after the status stage           */
static uint8_t  s_configured;
static uint32_t s_n_setup, s_n_reset, s_n_enum, s_speed;
static uint32_t s_n_tx, s_n_in_done, s_n_out_done;
static uint8_t  s_last_setup[8];
static uint32_t s_last_dieptsiz, s_last_diepctl, s_last_diepint;
static uint32_t s_tsiz_after;      /* the counter once the core was done   */
static uint8_t  s_armed_bytes[8];  /* what the buffer held at arming       */
static uint32_t s_armed_len;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

static void fifo_flush(void)
{
    uint32_t spin;

    NRF_USBHSCORE_S->GRSTCTL = GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL;
    for (spin = 0u; spin < USBHS_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & GRSTCTL_TXFFLSH) == 0u) {
            break;
        }
    }
    NRF_USBHSCORE_S->GRSTCTL = GRSTCTL_RXFFLSH;
    for (spin = 0u; spin < USBHS_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & GRSTCTL_RXFFLSH) == 0u) {
            break;
        }
    }
}

/** @brief Arm EP0 OUT for the next SETUP packet the host sends. */
static void ep0_arm_setup(void)
{
    NRF_USBHSCORE_S->DOEPTSIZ0 = (3ul << DOEPTSIZ_SUPCNT_SHIFT) |
                                 (1ul << DOEPTSIZ_PKTCNT_SHIFT) | 24ul;
    NRF_USBHSCORE_S->DOEPDMA0  = (uint32_t)setup_buf;
    NRF_USBHSCORE_S->DOEPCTL0 |= DEPCTL_EPENA | DEPCTL_CNAK;
}

/** @brief Queue @p len bytes on EP0 IN; a zero length is the status stage. */
static void ep0_tx(const void *data, uint32_t len)
{
    if (len > USB_EP0_MPS) {
        len = USB_EP0_MPS;
    }
    if (len > 0u && data != (const void *)0) {
        memcpy(ep0_in_buf, data, len);
    }
    NRF_USBHSCORE_S->DIEPTSIZ0 = (1ul << DIEPTSIZ_PKTCNT_SHIFT) | len;
    NRF_USBHSCORE_S->DIEPDMA0  = (uint32_t)ep0_in_buf;
    NRF_USBHSCORE_S->DIEPCTL0 |= DEPCTL_EPENA | DEPCTL_CNAK;
    s_n_tx++;
    if (len > 0u) {
        memcpy(s_armed_bytes, ep0_in_buf, (len < 8u) ? len : 8u);
        s_armed_len = len;
    }
    s_last_dieptsiz = NRF_USBHSCORE_S->DIEPTSIZ0;
    s_last_diepctl  = NRF_USBHSCORE_S->DIEPCTL0;
}

static void ep0_stall(void)
{
    NRF_USBHSCORE_S->DIEPCTL0 |= DEPCTL_STALL;
    NRF_USBHSCORE_S->DOEPCTL0 |= DEPCTL_STALL;
    ep0_arm_setup();
}

/** @brief The device's serial string, built from the factory device id. */
static uint16_t serial_desc(void)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t id[6];
    uint16_t n = 2u;
    uint8_t i;

    tiku_common_unique_id(id, 6u);
    for (i = 0u; i < 6u; i++) {
        serial_buf[n++] = (uint8_t)hex[(id[i] >> 4) & 0xFu];
        serial_buf[n++] = 0u;
        serial_buf[n++] = (uint8_t)hex[id[i] & 0xFu];
        serial_buf[n++] = 0u;
    }
    serial_buf[0] = (uint8_t)n;
    serial_buf[1] = 0x03u;
    return n;
}

/*---------------------------------------------------------------------------*/
/* EP0 CONTROL                                                               */
/*---------------------------------------------------------------------------*/

/* A standard request is answered here; anything else is stalled, which is
 * how a host learns a device does not implement it. */
static void ep0_setup(void)
{
    const uint8_t *p = (const uint8_t *)setup_buf;
    uint8_t  req_type = p[0], req = p[1];
    uint16_t value = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    uint16_t length = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));

    s_n_setup++;
    memcpy(s_last_setup, p, 8u);

    if ((req_type & 0x60u) != 0u) {          /* class or vendor: later      */
        if ((req_type & 0x80u) == 0u && length == 0u) {
            ep0_tx((const void *)0, 0u);     /* accept and say nothing      */
            ep0_arm_setup();
            return;
        }
        ep0_stall();
        return;
    }

    switch (req) {
    case 0x05:                               /* SET_ADDRESS                 */
        s_pending_addr = (uint8_t)(value & 0x7Fu);
        ep0_tx((const void *)0, 0u);
        break;
    case 0x06: {                             /* GET_DESCRIPTOR              */
        const uint8_t *d = (const uint8_t *)0;
        uint16_t n = 0u;

        switch (value >> 8) {
        case 1: d = dev_desc;  n = sizeof dev_desc;  break;
        case 2: d = conf_desc; n = sizeof conf_desc; break;
        case 3:
            switch (value & 0xFFu) {
            case 0: d = str_lang; n = sizeof str_lang; break;
            case 1: d = str_mfr;  n = sizeof str_mfr;  break;
            case 2: d = str_prod; n = sizeof str_prod; break;
            case 3: n = serial_desc(); d = serial_buf; break;
            default: break;
            }
            break;
        default: break;
        }
        if (d == (const uint8_t *)0) {
            ep0_stall();
            return;
        }
        if (n > length) {
            n = length;
        }
        ep0_tx(d, n);
        break;
    }
    case 0x08:                               /* GET_CONFIGURATION           */
        ep0_tx(&s_configured, 1u);
        break;
    case 0x09:                               /* SET_CONFIGURATION           */
        s_configured = (uint8_t)(value & 0xFFu);
        ep0_tx((const void *)0, 0u);
        break;
    case 0x00: {                             /* GET_STATUS                  */
        static const uint8_t zero[2] = { 0u, 0u };

        ep0_tx(zero, 2u);
        break;
    }
    default:
        ep0_stall();
        return;
    }
    ep0_arm_setup();
}

/*---------------------------------------------------------------------------*/
/* INTERRUPT                                                                 */
/*---------------------------------------------------------------------------*/

void tiku_nordic_usbhs_dev_irq(void)
{
    uint32_t sts = NRF_USBHSCORE_S->GINTSTS & NRF_USBHSCORE_S->GINTMSK;

    if ((sts & GINT_USBRST) != 0u) {
        NRF_USBHSCORE_S->GINTSTS = GINT_USBRST;
        s_n_reset++;
        s_address = 0u;
        s_pending_addr = 0u;
        s_configured = 0u;
        NRF_USBHSCORE_S->DCFG &= ~DCFG_DEVADDR_MASK;
        NRF_USBHSCORE_S->DCTL |= DCTL_CGNPINNAK | DCTL_CGOUTNAK;
        fifo_flush();
        ep0_arm_setup();
    }
    if ((sts & GINT_ENUMDONE) != 0u) {
        NRF_USBHSCORE_S->GINTSTS = GINT_ENUMDONE;
        s_n_enum++;
        s_speed = (NRF_USBHSCORE_S->DSTS >> 1) & 0x3u;
        NRF_USBHSCORE_S->DIEPCTL0 =
            (NRF_USBHSCORE_S->DIEPCTL0 & ~3ul) | DEPCTL0_MPS_64;
    }
    if ((sts & GINT_OEPINT) != 0u) {
        uint32_t oi = NRF_USBHSCORE_S->DOEPINT0;

        NRF_USBHSCORE_S->DOEPINT0 = oi;
        if ((oi & DOEPINT_SETUP) != 0u) {
            ep0_setup();
        } else if ((oi & DEPINT_XFERCOMPL) != 0u) {
            s_n_out_done++;
            ep0_arm_setup();
        }
    }
    if ((sts & GINT_IEPINT) != 0u) {
        uint32_t ii = NRF_USBHSCORE_S->DIEPINT0;

        NRF_USBHSCORE_S->DIEPINT0 = ii;
        s_last_diepint = ii;
        if ((ii & DEPINT_XFERCOMPL) != 0u) {
            s_n_in_done++;
            s_tsiz_after = NRF_USBHSCORE_S->DIEPTSIZ0;
            /* The address takes effect only once its status stage is on the
             * wire; applying it earlier loses the host. */
            if (s_pending_addr != 0u) {
                NRF_USBHSCORE_S->DCFG =
                    (NRF_USBHSCORE_S->DCFG & ~DCFG_DEVADDR_MASK) |
                    ((uint32_t)s_pending_addr << DCFG_DEVADDR_SHIFT);
                s_address = s_pending_addr;
                s_pending_addr = 0u;
            }
        }
    }
    if ((sts & (GINT_USBSUSP | GINT_WKUPINT)) != 0u) {
        NRF_USBHSCORE_S->GINTSTS = GINT_USBSUSP | GINT_WKUPINT;
    }
}

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

int tiku_nordic_usbhs_dev_start_cfg(int phyif16, uint32_t trdtim,
                                    uint32_t devspd)
{
    uint32_t base;

    /* The core is already powered and out of reset (tiku_nordic_usbhs_up). */
    if ((NRF_USBHSCORE_S->GRSTCTL & GRSTCTL_AHBIDLE) == 0u) {
        return -1;
    }

    /* The PHY's data width is selectable on this integration, and the
     * turnaround time must match it: an 8-bit PHY wants 9, a 16-bit one 5.
     * Both are arguments because the pairing is not derivable from the
     * core's own report, which says only "selectable". */
    {
        uint32_t cfg = NRF_USBHSCORE_S->GUSBCFG;

        cfg |= GUSBCFG_FORCEDEVMODE;
        if (phyif16 >= 0) {
            if (phyif16 != 0) {
                cfg |= GUSBCFG_PHYIF16;
            } else {
                cfg &= ~GUSBCFG_PHYIF16;
            }
        }
        if (trdtim <= 15u) {
            cfg = (cfg & ~GUSBCFG_TRDTIM_MASK) |
                  (trdtim << GUSBCFG_TRDTIM_SHIFT);
        }
        NRF_USBHSCORE_S->GUSBCFG = cfg;
    }

    NRF_USBHSCORE_S->GAHBCFG = GAHBCFG_DMAEN | GAHBCFG_HBSTLEN_I4;

    /* 0 negotiates high speed, 1 forces full speed on the same PHY.  A
     * console needs no more than full speed, but the PHY is a high-speed
     * one and its full-speed path is not the integration's default. */
    NRF_USBHSCORE_S->DCFG = DCFG_NZSTSOUTHSHK | (devspd & 3ul);

    /* The receive FIFO first, then one transmit FIFO per IN endpoint, each
     * starting where the last ended. */
    NRF_USBHSCORE_S->GRXFSIZ = FIFO_RX_WORDS;
    base = FIFO_RX_WORDS;
    NRF_USBHSCORE_S->GNPTXFSIZ = (FIFO_EP0IN_WORDS << 16) | base;
    base += FIFO_EP0IN_WORDS;
    NRF_USBHSCORE_S->DIEPTXF[0] = (FIFO_EP1IN_WORDS << 16) | base;
    base += FIFO_EP1IN_WORDS;
    NRF_USBHSCORE_S->DIEPTXF[2] = (FIFO_EP3IN_WORDS << 16) | base;
    fifo_flush();

    NRF_USBHSCORE_S->DIEPMSK  = DEPINT_XFERCOMPL;
    NRF_USBHSCORE_S->DOEPMSK  = DEPINT_XFERCOMPL | DOEPINT_SETUP;
    NRF_USBHSCORE_S->DAINTMSK = (1ul << 0) | (1ul << 16);   /* EP0 in + out */

    NRF_USBHSCORE_S->GINTSTS = 0xFFFFFFFFul;
    NRF_USBHSCORE_S->GINTMSK = GINT_USBRST | GINT_ENUMDONE | GINT_IEPINT |
                               GINT_OEPINT | GINT_USBSUSP | GINT_WKUPINT;
    NRF_USBHSCORE_S->GAHBCFG |= GAHBCFG_GLBLINTRMSK;

    ep0_arm_setup();

    /* Present the pull-up: from here the host sees a device and starts. */
    NRF_USBHSCORE_S->DCTL &= ~DCTL_SFTDISCON;
    s_started = 1u;
    return 0;
}

int tiku_nordic_usbhs_dev_start(void)
{
    return tiku_nordic_usbhs_dev_start_cfg(-1, 0xFFu, 0u);
}

void tiku_nordic_usbhs_dev_stop(void)
{
    NRF_USBHSCORE_S->DCTL |= DCTL_SFTDISCON;
    NRF_USBHSCORE_S->GINTMSK = 0u;
    s_started = 0u;
    s_configured = 0u;
    s_address = 0u;
}

void tiku_nordic_usbhs_dev_trace(uint8_t *setup8, uint32_t *tx,
                                 uint32_t *in_done, uint32_t *out_done,
                                 uint32_t *tsiz, uint32_t *ctl, uint32_t *iint)
{
    if (setup8 != (uint8_t *)0)  { memcpy(setup8, s_last_setup, 8u); }
    if (tx != (uint32_t *)0)     { *tx = s_n_tx; }
    if (in_done != (uint32_t *)0)  { *in_done = s_n_in_done; }
    if (out_done != (uint32_t *)0) { *out_done = s_n_out_done; }
    if (tsiz != (uint32_t *)0)   { *tsiz = s_last_dieptsiz; }
    if (ctl != (uint32_t *)0)    { *ctl = s_last_diepctl; }
    if (iint != (uint32_t *)0)   { *iint = s_last_diepint; }
}

void tiku_nordic_usbhs_dev_dma(uint32_t *addr, uint32_t *tsiz_after,
                               uint32_t *armed_len, uint8_t *armed8)
{
    if (addr != (uint32_t *)0)      { *addr = (uint32_t)ep0_in_buf; }
    if (tsiz_after != (uint32_t *)0){ *tsiz_after = s_tsiz_after; }
    if (armed_len != (uint32_t *)0) { *armed_len = s_armed_len; }
    if (armed8 != (uint8_t *)0)     { memcpy(armed8, s_armed_bytes, 8u); }
}

void tiku_nordic_usbhs_dev_stats(uint32_t *setup, uint32_t *reset,
                                 uint32_t *enum_done, uint32_t *speed,
                                 uint8_t *address, uint8_t *configured)
{
    if (setup != (uint32_t *)0)     { *setup = s_n_setup; }
    if (reset != (uint32_t *)0)     { *reset = s_n_reset; }
    if (enum_done != (uint32_t *)0) { *enum_done = s_n_enum; }
    if (speed != (uint32_t *)0)     { *speed = s_speed; }
    if (address != (uint8_t *)0)    { *address = s_address; }
    if (configured != (uint8_t *)0) { *configured = s_configured; }
}

uint8_t tiku_nordic_usbhs_dev_started(void)
{
    return s_started;
}
