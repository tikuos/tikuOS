/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_msc.c - nRF54LM20 USB mass storage (Bulk-Only Transport).
 *
 * A second face for the DWC2 core: one SCSI disk over the console's own
 * bring-up, EP0 engine and 512-byte bulk endpoint.  RAM disk; the wire
 * format and SCSI replies come from the core (kernel/usb).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_usbhs_arch.h"

#include <arch/nordic/tiku_nordic_core.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/usb/tiku_usbd_msc.h>

#include <string.h>

#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)

/*---------------------------------------------------------------------------*/
/* CORE REGISTER BITS (shared with the CDC face, copied verbatim)            */
/*---------------------------------------------------------------------------*/

#define GAHBCFG_GLBLINTRMSK (1u << 0)
#define GAHBCFG_DMAEN       (1u << 5)
#define GAHBCFG_HBSTLEN_I4  (3u << 1)
#define GUSBCFG_FORCEDEVMODE (1ul << 30)
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
#define DEPCTL_TYPE_BULK    (2ul << DEPCTL_EPTYPE_SHIFT)
#define DAINT_IN(n)         (1ul << (n))
#define DAINT_OUT(n)        (1ul << (16u + (n)))
#define DOEPTSIZ_XFER_MASK  0x7FFFFul

#define EP_BULK_OUT         2u
#define EP_BULK_IN          3u
#define EP_BULK_IN_FIFO     3u
#define USB_EP0_MPS         64u
#define USB_BULK_MPS        64u
#define USB_BULK_MPS_HS     512u

#define FIFO_RX_WORDS       384u
#define FIFO_EP0IN_WORDS    64u
#define FIFO_EP1IN_WORDS    64u
#define FIFO_EP3IN_WORDS    256u
#define USBHS_SPINS         2000000u
#define EP0_IN_CAP          128u

/*---------------------------------------------------------------------------*/
/* THE DISK                                                                  */
/*---------------------------------------------------------------------------*/

/* A RAM disk sized to fit SRAM with room to spare: the medium the host reads
 * and writes.  A durable backing (the carved NVM region) is a swap of these
 * two accessors and nothing else -- the whole point of routing every block
 * through the controller-independent core. */
#define MSC_DISK_BLOCKS  128u                       /* 64 KB                */
#define MSC_DISK_BYTES   (MSC_DISK_BLOCKS * TIKU_USBD_MSC_BLOCK)
static uint8_t msc_disk[MSC_DISK_BYTES] __attribute__((aligned(4)));

static tiku_usbd_msc_t msc_medium = {
    MSC_DISK_BLOCKS, "TikuOS RAMDisk", 0u, 0u
};

/*---------------------------------------------------------------------------*/
/* DESCRIPTORS                                                               */
/*---------------------------------------------------------------------------*/

/* pid.codes 1209:0002 -- a distinct product from the console (0001), so a
 * host tells the two faces apart. */
static const uint8_t dev_desc[18] = {
    18, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00,           /* class at the interface, not the device   */
    USB_EP0_MPS,
    0x09, 0x12,                 /* idVendor  0x1209                          */
    0x02, 0x00,                 /* idProduct 0x0002                          */
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01
};

#define CONF_TOTAL_LEN 32
static uint8_t conf_desc[CONF_TOTAL_LEN] = {
    9, 0x02, CONF_TOTAL_LEN, 0x00, 0x01, 0x01, 0x00, 0x80, 50,
    /* Interface 0: mass storage / SCSI transparent / Bulk-Only Transport */
    9, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    /* Bulk OUT (EP2) then bulk IN (EP3) */
    7, 0x05, 0x02, 0x02, USB_BULK_MPS, 0x00, 0x00,
    7, 0x05, 0x83, 0x02, USB_BULK_MPS, 0x00, 0x00
};

static const uint8_t str_lang[4]  = { 4, 0x03, 0x09, 0x04 };
static const uint8_t str_mfr[14]  = { 14, 0x03, 'T',0,'i',0,'k',0,'u',0,
                                      'O',0,'S',0 };
static const uint8_t str_prod[24] = { 24, 0x03, 'T',0,'i',0,'k',0,'u',0,'O',0,
                                      'S',0,' ',0,'D',0,'i',0,'s',0,'k',0 };
static uint8_t serial_buf[26] __attribute__((aligned(4)));

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint32_t setup_buf[6];
static uint8_t  ep0_in_buf[EP0_IN_CAP] __attribute__((aligned(4)));
static uint8_t  cbw_buf[64] __attribute__((aligned(4)));
static uint8_t  csw_buf[TIKU_USBD_MSC_CSW_LEN] __attribute__((aligned(4)));
static uint8_t  reply_buf[TIKU_USBD_MSC_REPLY_MAX] __attribute__((aligned(4)));

static uint8_t  s_started;
static uint8_t  s_address;
static uint8_t  s_configured;
static uint32_t s_bulk_mps = USB_BULK_MPS;

/* The command pump's phase: what the next bulk completion means. */
enum { BOT_CBW = 0, BOT_DATA_IN, BOT_DATA_OUT, BOT_CSW };
static uint8_t  s_bot;
static uint32_t s_csw_tag, s_csw_residue;
static uint8_t  s_csw_status;

static uint32_t n_cbw, n_rd, n_wr, n_bad, n_irq;

/*---------------------------------------------------------------------------*/
/* EP0 ENGINE (copied verbatim from the proven console face)                 */
/*---------------------------------------------------------------------------*/

static void ep0_tx(const void *data, uint32_t len)
{
    uint32_t pkts;

    if (len > EP0_IN_CAP) {
        len = EP0_IN_CAP;
    }
    if (len > 0u && data != (const void *)0) {
        memcpy(ep0_in_buf, data, len);
    }
    pkts = (len + USB_EP0_MPS - 1u) / USB_EP0_MPS;
    if (pkts == 0u) {
        pkts = 1u;
    }
    NRF_USBHSCORE_S->DIEPTSIZ0 = (pkts << DIEPTSIZ_PKTCNT_SHIFT) | len;
    NRF_USBHSCORE_S->DIEPDMA0  = (uint32_t)ep0_in_buf;
    NRF_USBHSCORE_S->DIEPCTL0 |= DEPCTL_EPENA | DEPCTL_CNAK;
}

static void ep0_arm_setup(void)
{
    NRF_USBHSCORE_S->DOEPTSIZ0 = (3ul << DOEPTSIZ_SUPCNT_SHIFT) |
                                 (1ul << DOEPTSIZ_PKTCNT_SHIFT) | 24ul;
    NRF_USBHSCORE_S->DOEPDMA0  = (uint32_t)setup_buf;
    NRF_USBHSCORE_S->DOEPCTL0 |= DEPCTL_EPENA | DEPCTL_CNAK;
}

static void ep0_stall(void)
{
    NRF_USBHSCORE_S->DIEPCTL0 |= DEPCTL_STALL;
    NRF_USBHSCORE_S->DOEPCTL0 |= DEPCTL_STALL;
    ep0_arm_setup();
}

static void ep0_in_reset(void)
{
    uint32_t spin;

    if ((NRF_USBHSCORE_S->DIEPCTL0 & DEPCTL_EPENA) != 0u) {
        NRF_USBHSCORE_S->DIEPCTL0 |= DEPCTL_EPDIS | DEPCTL_SNAK;
        for (spin = 0u; spin < USBHS_SPINS; spin++) {
            if ((NRF_USBHSCORE_S->DIEPCTL0 & DEPCTL_EPENA) == 0u) {
                break;
            }
        }
    }
    NRF_USBHSCORE_S->GRSTCTL = GRSTCTL_TXFFLSH;
    for (spin = 0u; spin < USBHS_SPINS; spin++) {
        if ((NRF_USBHSCORE_S->GRSTCTL & GRSTCTL_TXFFLSH) == 0u) {
            break;
        }
    }
    NRF_USBHSCORE_S->DIEPINT0 = 0xFFFFFFFFul;
}

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

/*---------------------------------------------------------------------------*/
/* BULK-ONLY TRANSPORT PUMP                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Arm bulk OUT for @p bytes into @p dst (a CBW, or a WRITE payload). */
static void bulk_out_arm(void *dst, uint32_t bytes)
{
    uint32_t pkts = (bytes + s_bulk_mps - 1u) / s_bulk_mps;

    if (pkts == 0u) {
        pkts = 1u;
    }
    NRF_USBHSCORE_S->DOEPTSIZ2 = (pkts << DOEPTSIZ_PKTCNT_SHIFT) | bytes;
    NRF_USBHSCORE_S->DOEPDMA2  = (uint32_t)dst;
    NRF_USBHSCORE_S->DOEPCTL2 |= DEPCTL_EPENA | DEPCTL_CNAK;
}

/** @brief Send @p bytes from @p src on bulk IN (a reply, a READ, or the CSW). */
static void bulk_in_send(const void *src, uint32_t bytes)
{
    uint32_t pkts = (bytes + s_bulk_mps - 1u) / s_bulk_mps;

    if (pkts == 0u) {
        pkts = 1u;                          /* a zero-length packet         */
    }
    NRF_USBHSCORE_S->DIEPTSIZ3 = (pkts << DIEPTSIZ_PKTCNT_SHIFT) | bytes;
    NRF_USBHSCORE_S->DIEPDMA3  = (uint32_t)src;
    NRF_USBHSCORE_S->DIEPCTL3 |= DEPCTL_EPENA | DEPCTL_CNAK;
}

static void bot_send_csw(void)
{
    tiku_usbd_msc_build_csw(csw_buf, s_csw_tag, s_csw_residue, s_csw_status);
    s_bot = BOT_CSW;
    bulk_in_send(csw_buf, TIKU_USBD_MSC_CSW_LEN);
}

/** @brief A command wrapper arrived; act on it and set up its data phase. */
static void bot_on_cbw(uint32_t got)
{
    tiku_usbd_msc_cbw_t cbw;
    tiku_usbd_msc_cmd_t cmd;

    if (!tiku_usbd_msc_parse_cbw(cbw_buf, (uint16_t)got, &cbw)) {
        n_bad++;
        bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);   /* resync on the next */
        s_bot = BOT_CBW;
        return;
    }
    n_cbw++;
    tiku_usbd_msc_decode(&msc_medium, &cbw, reply_buf, &cmd);
    s_csw_tag     = cbw.tag;
    s_csw_residue = cmd.residue;
    s_csw_status  = cmd.status;

    switch (cmd.action) {
    case TIKU_USBD_MSC_ACT_READ:
        n_rd++;
        s_bot = BOT_DATA_IN;
        bulk_in_send(&msc_disk[cmd.lba * TIKU_USBD_MSC_BLOCK], cmd.bytes);
        break;
    case TIKU_USBD_MSC_ACT_WRITE:
        n_wr++;
        s_bot = BOT_DATA_OUT;
        bulk_out_arm(&msc_disk[cmd.lba * TIKU_USBD_MSC_BLOCK], cmd.bytes);
        break;
    case TIKU_USBD_MSC_ACT_REPLY:
        s_bot = BOT_DATA_IN;
        bulk_in_send(reply_buf, cmd.len);
        break;
    default:
        bot_send_csw();
        break;
    }
}

/** @brief Open the two bulk endpoints and wait for the first command. */
static void msc_endpoints_open(void)
{
    NRF_USBHSCORE_S->DOEPCTL2 = DEPCTL_USBACTEP | DEPCTL_TYPE_BULK |
                                DEPCTL_SETD0PID | s_bulk_mps;
    NRF_USBHSCORE_S->DIEPCTL3 = DEPCTL_USBACTEP | DEPCTL_TYPE_BULK |
                                DEPCTL_SETD0PID | s_bulk_mps |
                                ((uint32_t)EP_BULK_IN_FIFO << DEPCTL_TXFNUM_SHIFT);
    NRF_USBHSCORE_S->DAINTMSK |= DAINT_OUT(EP_BULK_OUT) | DAINT_IN(EP_BULK_IN);
    s_bot = BOT_CBW;
    bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
}

/*---------------------------------------------------------------------------*/
/* EP0 CONTROL TRANSFERS                                                     */
/*---------------------------------------------------------------------------*/

static void ep0_setup(void)
{
    const uint8_t *p = (const uint8_t *)setup_buf;
    uint8_t  req_type = p[0], req = p[1];
    uint16_t value = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    uint16_t length = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));

    ep0_in_reset();

    if ((req_type & 0x60u) == 0x20u) {       /* mass-storage class requests */
        switch (req) {
        case 0xFF:                           /* Bulk-Only Mass Storage Reset */
            s_bot = BOT_CBW;
            bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
            ep0_tx((const void *)0, 0u);
            break;
        case 0xFE: {                         /* Get Max LUN: one LUN, so 0   */
            static const uint8_t max_lun = 0u;

            ep0_tx(&max_lun, (length < 1u) ? length : 1u);
            break;
        }
        default:
            ep0_stall();
            return;
        }
        ep0_arm_setup();
        return;
    }
    if ((req_type & 0x60u) != 0u) {
        ep0_stall();
        return;
    }

    switch (req) {
    case 0x05:                               /* SET_ADDRESS (before status) */
        s_address = (uint8_t)(value & 0x7Fu);
        NRF_USBHSCORE_S->DCFG = (NRF_USBHSCORE_S->DCFG & ~DCFG_DEVADDR_MASK) |
                                ((uint32_t)s_address << DCFG_DEVADDR_SHIFT);
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
        if (s_configured != 0u) {
            msc_endpoints_open();
        }
        ep0_tx((const void *)0, 0u);
        break;
    case 0x00: {                             /* GET_STATUS                  */
        static const uint8_t st[2] = { 0u, 0u };

        ep0_tx(st, (length < 2u) ? length : 2u);
        break;
    }
    default:
        ep0_stall();
        return;
    }
    ep0_arm_setup();
}

/*---------------------------------------------------------------------------*/
/* THE INTERRUPT: arch.c dispatches here when this face is started           */
/*---------------------------------------------------------------------------*/

void tiku_nordic_usbhs_dev_irq(void)
{
    uint32_t sts = NRF_USBHSCORE_S->GINTSTS & NRF_USBHSCORE_S->GINTMSK;

    n_irq++;

    if ((sts & GINT_USBRST) != 0u) {
        NRF_USBHSCORE_S->GINTSTS = GINT_USBRST;
        s_address = 0u;
        s_configured = 0u;
        s_bot = BOT_CBW;
        NRF_USBHSCORE_S->DCFG &= ~DCFG_DEVADDR_MASK;
        NRF_USBHSCORE_S->DCTL |= DCTL_CGNPINNAK | DCTL_CGOUTNAK;
        fifo_flush();
        ep0_arm_setup();
    }
    if ((sts & GINT_ENUMDONE) != 0u) {
        uint32_t speed;

        NRF_USBHSCORE_S->GINTSTS = GINT_ENUMDONE;
        speed = (NRF_USBHSCORE_S->DSTS >> 1) & 0x3u;
        NRF_USBHSCORE_S->DIEPCTL0 = (NRF_USBHSCORE_S->DIEPCTL0 & ~3ul);
        s_bulk_mps = (speed == 0u) ? USB_BULK_MPS_HS : USB_BULK_MPS;
    }
    if ((sts & GINT_OEPINT) != 0u) {
        uint32_t daint = NRF_USBHSCORE_S->DAINT;

        if ((daint & DAINT_OUT(0)) != 0u) {
            uint32_t oi = NRF_USBHSCORE_S->DOEPINT0;

            NRF_USBHSCORE_S->DOEPINT0 = oi;
            if ((oi & DOEPINT_SETUP) != 0u) {
                ep0_setup();
            } else if ((oi & DEPINT_XFERCOMPL) != 0u) {
                ep0_arm_setup();
            }
        }
        if ((daint & DAINT_OUT(EP_BULK_OUT)) != 0u) {
            uint32_t oi = NRF_USBHSCORE_S->DOEPINT2;

            NRF_USBHSCORE_S->DOEPINT2 = oi;
            if ((oi & DEPINT_XFERCOMPL) != 0u) {
                if (s_bot == BOT_CBW) {
                    uint32_t left = NRF_USBHSCORE_S->DOEPTSIZ2 &
                                    DOEPTSIZ_XFER_MASK;

                    bot_on_cbw(TIKU_USBD_MSC_BLOCK - left);
                } else if (s_bot == BOT_DATA_OUT) {
                    bot_send_csw();          /* the WRITE payload landed    */
                }
            }
        }
    }
    if ((sts & GINT_IEPINT) != 0u) {
        uint32_t daint = NRF_USBHSCORE_S->DAINT;

        if ((daint & DAINT_IN(0)) != 0u) {
            uint32_t ii = NRF_USBHSCORE_S->DIEPINT0;

            NRF_USBHSCORE_S->DIEPINT0 = ii;
        }
        if ((daint & DAINT_IN(EP_BULK_IN)) != 0u) {
            uint32_t ii = NRF_USBHSCORE_S->DIEPINT3;

            NRF_USBHSCORE_S->DIEPINT3 = ii;
            if ((ii & DEPINT_XFERCOMPL) != 0u) {
                if (s_bot == BOT_DATA_IN) {
                    bot_send_csw();          /* the READ or reply is out    */
                } else if (s_bot == BOT_CSW) {
                    s_bot = BOT_CBW;         /* the command is finished     */
                    bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
                }
            }
        }
    }
    if ((sts & (GINT_USBSUSP | GINT_WKUPINT)) != 0u) {
        NRF_USBHSCORE_S->GINTSTS = GINT_USBSUSP | GINT_WKUPINT;
    }
}

/*---------------------------------------------------------------------------*/
/* LIFECYCLE (the arch ISR calls _started; MSC provides it in this build)    */
/*---------------------------------------------------------------------------*/

uint8_t tiku_nordic_usbhs_dev_started(void)
{
    return s_started;
}

int tiku_nordic_usbhs_msc_start(void)
{
    uint32_t base;

    if ((NRF_USBHSCORE_S->GRSTCTL & GRSTCTL_AHBIDLE) == 0u) {
        return -1;
    }
    NRF_USBHSCORE_S->GUSBCFG |= GUSBCFG_FORCEDEVMODE;
    NRF_USBHSCORE_S->GAHBCFG = GAHBCFG_DMAEN | GAHBCFG_HBSTLEN_I4;
    NRF_USBHSCORE_S->DCFG = DCFG_NZSTSOUTHSHK;

    NRF_USBHSCORE_S->GRXFSIZ = FIFO_RX_WORDS;
    base = FIFO_RX_WORDS;
    NRF_USBHSCORE_S->GNPTXFSIZ = (FIFO_EP0IN_WORDS << 16) | base;
    base += FIFO_EP0IN_WORDS;
    NRF_USBHSCORE_S->DIEPTXF[0] = (FIFO_EP1IN_WORDS << 16) | base;
    base += FIFO_EP1IN_WORDS;
    NRF_USBHSCORE_S->DIEPTXF[2] = (FIFO_EP3IN_WORDS << 16) | base;
    base += FIFO_EP3IN_WORDS;
    NRF_USBHSCORE_S->GDFIFOCFG =
        (base << 16) | ((NRF_USBHSCORE_S->GHWCFG3 >> 16) & 0xFFFFu);
    fifo_flush();

    NRF_USBHSCORE_S->DIEPMSK  = DEPINT_XFERCOMPL;
    NRF_USBHSCORE_S->DOEPMSK  = DEPINT_XFERCOMPL | DOEPINT_SETUP;
    NRF_USBHSCORE_S->DAINTMSK = (1ul << 0) | (1ul << 16);

    NRF_USBHSCORE_S->GINTSTS = 0xFFFFFFFFul;
    NRF_USBHSCORE_S->GINTMSK = GINT_USBRST | GINT_ENUMDONE | GINT_IEPINT |
                               GINT_OEPINT | GINT_USBSUSP | GINT_WKUPINT;
    NRF_USBHSCORE_S->GAHBCFG |= GAHBCFG_GLBLINTRMSK;

    ep0_arm_setup();
    NRF_USBHSCORE_S->DCTL &= ~DCTL_SFTDISCON;
    s_started = 1u;
    return 0;
}

void tiku_nordic_usbhs_msc_stop(void)
{
    NRF_USBHSCORE_S->DCTL |= DCTL_SFTDISCON;
    NRF_USBHSCORE_S->GINTMSK = 0u;
    s_started = 0u;
    s_configured = 0u;
    s_address = 0u;
}

uint32_t tiku_nordic_usbhs_msc_peek(uint32_t lba, uint8_t *dst, uint32_t n)
{
    uint32_t off = lba * TIKU_USBD_MSC_BLOCK;

    if (lba >= MSC_DISK_BLOCKS) {
        return 0u;
    }
    if (n > TIKU_USBD_MSC_BLOCK) {
        n = TIKU_USBD_MSC_BLOCK;
    }
    memcpy(dst, &msc_disk[off], n);
    return n;
}

void tiku_nordic_usbhs_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                                 uint32_t *bad, uint32_t *irq, uint8_t *cfg)
{
    if (cbw != (uint32_t *)0) { *cbw = n_cbw; }
    if (rd  != (uint32_t *)0) { *rd  = n_rd;  }
    if (wr  != (uint32_t *)0) { *wr  = n_wr;  }
    if (bad != (uint32_t *)0) { *bad = n_bad; }
    if (irq != (uint32_t *)0) { *irq = n_irq; }
    if (cfg != (uint8_t *)0)  { *cfg = s_configured; }
}

#endif /* NRF54LM20A/B */
