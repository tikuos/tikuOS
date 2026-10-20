/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_msc.c - ESP32-P4 USB mass storage (Bulk-Only Transport).
 *
 * A 64 KB RAM disk holding an empty FAT12 volume, on the DWC2 core in
 * buffer-DMA device mode.  The core's DMA bypasses the L1 cache, so buffers
 * are cleaned before it reads them and invalidated around its writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <string.h>
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/memory/tiku_mem.h>
#include <services/usb/tiku_usbd_ctrl.h>
#include <services/usb/tiku_usbd_msc.h>
#include "tiku_usbhs_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"

#define USB_DWC             0x50000000u
#define GAHBCFG             (USB_DWC + 0x008u)
#define GUSBCFG             (USB_DWC + 0x00Cu)
#define GRSTCTL             (USB_DWC + 0x010u)
#define GINTSTS             (USB_DWC + 0x014u)
#define GINTMSK             (USB_DWC + 0x018u)
#define GRXFSIZ             (USB_DWC + 0x024u)
#define GNPTXFSIZ           (USB_DWC + 0x028u)
#define GHWCFG3             (USB_DWC + 0x04Cu)
#define GDFIFOCFG           (USB_DWC + 0x05Cu)
#define DIEPTXF(n)          (USB_DWC + 0x100u + 4u * (n))
#define DCFG                (USB_DWC + 0x800u)
#define DCTL                (USB_DWC + 0x804u)
#define DSTS                (USB_DWC + 0x808u)
#define DIEPMSK             (USB_DWC + 0x810u)
#define DOEPMSK             (USB_DWC + 0x814u)
#define DAINT               (USB_DWC + 0x818u)
#define DAINTMSK            (USB_DWC + 0x81Cu)
#define DIEPCTL(n)          (USB_DWC + 0x900u + 0x20u * (n))
#define DIEPINT(n)          (USB_DWC + 0x908u + 0x20u * (n))
#define DIEPTSIZ(n)         (USB_DWC + 0x910u + 0x20u * (n))
#define DIEPDMA(n)          (USB_DWC + 0x914u + 0x20u * (n))
#define DOEPCTL(n)          (USB_DWC + 0xB00u + 0x20u * (n))
#define DOEPINT(n)          (USB_DWC + 0xB08u + 0x20u * (n))
#define DOEPTSIZ(n)         (USB_DWC + 0xB10u + 0x20u * (n))
#define DOEPDMA(n)          (USB_DWC + 0xB14u + 0x20u * (n))

#define GAHBCFG_GLBLINTRMSK (1u << 0)
#define GAHBCFG_DMAEN       (1u << 5)
#define GAHBCFG_HBSTLEN_I4  (3u << 1)
#define GUSBCFG_FORCEDEVMODE (1u << 30)
#define GRSTCTL_TXFFLSH     (1u << 5)
#define GRSTCTL_RXFFLSH     (1u << 4)
#define GRSTCTL_TXFNUM_ALL  (0x10u << 6)
#define GINT_USBSUSP        (1u << 11)
#define GINT_USBRST         (1u << 12)
#define GINT_ENUMDONE       (1u << 13)
#define GINT_IEPINT         (1u << 18)
#define GINT_OEPINT         (1u << 19)
#define GINT_WKUPINT        (1u << 31)
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
#define DEPCTL_EPDIS        (1u << 30)
#define DEPCTL_EPENA        (1u << 31)
#define DEPCTL_TXFNUM_SHIFT 22
#define DEPCTL_TYPE_BULK    (2u << 18)
#define DEPINT_XFERCOMPL    (1u << 0)
#define DOEPINT_SETUP       (1u << 3)
#define TSIZ_PKTCNT_SHIFT   19
#define DOEPTSIZ_SUPCNT_SHIFT 29
#define TSIZ_XFER_MASK      0x7FFFFu
#define DAINT_IN(n)         (1u << (n))
#define DAINT_OUT(n)        (1u << (16u + (n)))

#define EP_BULK_OUT         2u
#define EP_BULK_IN          3u
#define EP_BULK_IN_FIFO     3u
#define USB_EP0_MPS         64u
#define USB_BULK_MPS        64u
#define USB_BULK_MPS_HS     512u
/* FIFO layout in words: the receive FIFO shared by every OUT endpoint, then
 * one transmit FIFO per IN endpoint. */
#define FIFO_RX_WORDS       384u
#define FIFO_EP0IN_WORDS    64u
#define FIFO_EP1IN_WORDS    64u
#define FIFO_EP3IN_WORDS    256u
#define FIFO_WORDS          (FIFO_RX_WORDS + FIFO_EP0IN_WORDS + \
                             FIFO_EP1IN_WORDS + FIFO_EP3IN_WORDS)
#define USB_SPINS           2000000u
#define EP0_IN_CAP          128u
#define LINE                64u              /* the L1 line the buffers fill */
#define SPEED_NONE          7u               /* no enumeration yet */

#define MSC_DISK_BLOCKS     128u             /* 64 KB */
#define MSC_DISK_BYTES      (MSC_DISK_BLOCKS * TIKU_USBD_MSC_BLOCK)
#define MSC_ARENA_ID        74u

/* The disk's memory, from the SRAM tier while the disk is presented. */
static tiku_arena_t disk_arena;
static uint8_t *msc_disk;
static tiku_usbd_msc_t msc_medium = {MSC_DISK_BLOCKS, "TikuOS RAMDisk", 0u, 0u};

static uint8_t dev_desc[TIKU_USBD_DEVICE_LEN];
static uint8_t conf_desc[TIKU_USBD_MSC_CONFIG_LEN];
static uint8_t str_mfr[16], str_prod[32];
static uint8_t serial_buf[26];
static tiku_usbd_desc_set_t s_set;
static tiku_usbd_ctrl_t s_ctrl;

/* What the core reads and writes by DMA: whole cache lines each. */
static uint32_t setup_buf[LINE / 4u] __attribute__((aligned(LINE)));
static uint8_t ep0_in_buf[EP0_IN_CAP] __attribute__((aligned(LINE)));
static uint8_t cbw_buf[TIKU_USBD_MSC_BLOCK] __attribute__((aligned(LINE)));
static uint8_t csw_buf[LINE] __attribute__((aligned(LINE)));
static uint8_t reply_buf[LINE] __attribute__((aligned(LINE)));

static uint8_t s_started, s_address, s_configured;
static uint32_t s_bulk_mps = USB_BULK_MPS, s_speed = SPEED_NONE;

/* The command pump's phase: what the next bulk completion means. */
enum { BOT_CBW = 0, BOT_DATA_IN, BOT_DATA_OUT, BOT_CSW };
static uint8_t s_bot;
static uint8_t *s_out_buf;
static uint32_t s_out_len;
static uint32_t s_csw_tag, s_csw_residue;
static uint8_t s_csw_status;
static uint32_t n_cbw, n_rd, n_wr, n_bad, n_irq;

/** @brief Wait for the bits of a GRSTCTL flush to clear (bounded). */
static void flush_wait(uint32_t bits)
{
    uint32_t n;

    for (n = 0; n < USB_SPINS && (TIKU_P4_REG_READ(GRSTCTL) & bits); n++) {
    }
}

/** @brief Flush every transmit FIFO, then the receive FIFO. */
static void fifo_flush(void)
{
    TIKU_P4_REG_WRITE(GRSTCTL, GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL);
    flush_wait(GRSTCTL_TXFFLSH);
    TIKU_P4_REG_WRITE(GRSTCTL, GRSTCTL_RXFFLSH);
    flush_wait(GRSTCTL_RXFFLSH);
}

/** @brief Set bits of an endpoint control register. */
static void ep_set(uint32_t reg, uint32_t bits)
{
    TIKU_P4_REG_WRITE(reg, TIKU_P4_REG_READ(reg) | bits);
}

/** @brief Arm EP0 OUT for the next SETUP packet the host sends. */
static void ep0_arm_setup(void)
{
    tiku_cpu_dcache_invalidate(setup_buf, sizeof setup_buf);
    TIKU_P4_REG_WRITE(DOEPTSIZ(0), (3u << DOEPTSIZ_SUPCNT_SHIFT) |
                                       (1u << TSIZ_PKTCNT_SHIFT) | 24u);
    TIKU_P4_REG_WRITE(DOEPDMA(0), (uint32_t)(uintptr_t)setup_buf);
    ep_set(DOEPCTL(0), DEPCTL_EPENA | DEPCTL_CNAK);
}

/** @brief Queue @p len bytes on EP0 IN; a zero length is the status stage. */
static void ep0_tx(const void *data, uint32_t len)
{
    uint32_t pkts;

    if (len > EP0_IN_CAP) {
        len = EP0_IN_CAP;
    }
    if (len > 0u && data != NULL) {
        memcpy(ep0_in_buf, data, len);
        tiku_cpu_dcache_clean(ep0_in_buf, len);
    }
    pkts = (len + USB_EP0_MPS - 1u) / USB_EP0_MPS;
    if (pkts == 0u) {
        pkts = 1u;
    }
    TIKU_P4_REG_WRITE(DIEPTSIZ(0), (pkts << TSIZ_PKTCNT_SHIFT) | len);
    TIKU_P4_REG_WRITE(DIEPDMA(0), (uint32_t)(uintptr_t)ep0_in_buf);
    ep_set(DIEPCTL(0), DEPCTL_EPENA | DEPCTL_CNAK);
}

/** @brief Stall both EP0 directions and re-arm for the next SETUP. */
static void ep0_stall(void)
{
    ep_set(DIEPCTL(0), DEPCTL_STALL);
    ep_set(DOEPCTL(0), DEPCTL_STALL);
    ep0_arm_setup();
}

/** @brief Disable EP0 IN if it is still enabled and flush its FIFO: an IN
 *         armed on top of an enabled one wedges. */
static void ep0_in_reset(void)
{
    uint32_t n;

    if (TIKU_P4_REG_READ(DIEPCTL(0)) & DEPCTL_EPENA) {
        ep_set(DIEPCTL(0), DEPCTL_EPDIS | DEPCTL_SNAK);
        for (n = 0; n < USB_SPINS &&
                    (TIKU_P4_REG_READ(DIEPCTL(0)) & DEPCTL_EPENA); n++) {
        }
    }
    TIKU_P4_REG_WRITE(GRSTCTL, GRSTCTL_TXFFLSH);
    flush_wait(GRSTCTL_TXFFLSH);
    TIKU_P4_REG_WRITE(DIEPINT(0), 0xFFFFFFFFu);
}

/** @brief Arm bulk OUT for @p bytes into @p dst (a CBW or a WRITE payload). */
static void bulk_out_arm(uint8_t *dst, uint32_t bytes)
{
    uint32_t pkts = (bytes + s_bulk_mps - 1u) / s_bulk_mps;

    if (pkts == 0u) {
        pkts = 1u;
    }
    tiku_cpu_dcache_invalidate(dst, bytes);
    s_out_buf = dst;
    s_out_len = bytes;
    TIKU_P4_REG_WRITE(DOEPTSIZ(EP_BULK_OUT), (pkts << TSIZ_PKTCNT_SHIFT) | bytes);
    TIKU_P4_REG_WRITE(DOEPDMA(EP_BULK_OUT), (uint32_t)(uintptr_t)dst);
    ep_set(DOEPCTL(EP_BULK_OUT), DEPCTL_EPENA | DEPCTL_CNAK);
}

/** @brief Send @p bytes from @p src on bulk IN (reply, READ data or CSW). */
static void bulk_in_send(const uint8_t *src, uint32_t bytes)
{
    uint32_t pkts = (bytes + s_bulk_mps - 1u) / s_bulk_mps;

    if (pkts == 0u) {
        pkts = 1u;                          /* a zero-length packet */
    }
    if (bytes != 0u) {
        tiku_cpu_dcache_clean(src, bytes);
    }
    TIKU_P4_REG_WRITE(DIEPTSIZ(EP_BULK_IN), (pkts << TSIZ_PKTCNT_SHIFT) | bytes);
    TIKU_P4_REG_WRITE(DIEPDMA(EP_BULK_IN), (uint32_t)(uintptr_t)src);
    ep_set(DIEPCTL(EP_BULK_IN), DEPCTL_EPENA | DEPCTL_CNAK);
}

/** @brief Send the command status wrapper for the current command. */
static void bot_send_csw(void)
{
    tiku_usbd_msc_build_csw(csw_buf, s_csw_tag, s_csw_residue, s_csw_status);
    s_bot = BOT_CSW;
    bulk_in_send(csw_buf, TIKU_USBD_MSC_CSW_LEN);
}

/** @brief A command wrapper of @p got bytes arrived; act on it and set up
 *         its data phase. */
static void bot_on_cbw(uint32_t got)
{
    tiku_usbd_msc_cbw_t cbw;
    tiku_usbd_msc_cmd_t cmd;

    if (!tiku_usbd_msc_parse_cbw(cbw_buf, (uint16_t)got, &cbw)) {
        n_bad++;
        s_bot = BOT_CBW;
        bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);   /* resync on the next */
        return;
    }
    n_cbw++;
    tiku_usbd_msc_decode(&msc_medium, &cbw, reply_buf, &cmd);
    s_csw_tag = cbw.tag;
    s_csw_residue = cmd.residue;
    s_csw_status = cmd.status;
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
    TIKU_P4_REG_WRITE(DOEPCTL(EP_BULK_OUT), DEPCTL_USBACTEP | DEPCTL_TYPE_BULK |
                                                DEPCTL_SETD0PID | s_bulk_mps);
    TIKU_P4_REG_WRITE(DIEPCTL(EP_BULK_IN),
                      DEPCTL_USBACTEP | DEPCTL_TYPE_BULK | DEPCTL_SETD0PID |
                          s_bulk_mps | (EP_BULK_IN_FIFO << DEPCTL_TXFNUM_SHIFT));
    TIKU_P4_REG_WRITE(DAINTMSK, TIKU_P4_REG_READ(DAINTMSK) |
                                    DAINT_OUT(EP_BULK_OUT) | DAINT_IN(EP_BULK_IN));
    s_bot = BOT_CBW;
    bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
}

/** @brief Answer the SETUP in setup_buf through services/usb, applying its
 *         effects first; a request it does not answer is stalled. */
static void ep0_setup(void)
{
    const uint8_t *p = (const uint8_t *)setup_buf;
    tiku_usbd_ctrl_out_t d;

    tiku_cpu_dcache_invalidate(setup_buf, sizeof setup_buf);
    ep0_in_reset();
    tiku_usbd_ctrl_setup(&s_ctrl, p, &d);
    if (d.effects & TIKU_USBD_FX_ADDRESS) {       /* before the status stage */
        s_address = d.address;
        TIKU_P4_REG_WRITE(DCFG, (TIKU_P4_REG_READ(DCFG) & ~DCFG_DEVADDR_MASK) |
                                    ((uint32_t)s_address << DCFG_DEVADDR_SHIFT));
    }
    if (d.effects & TIKU_USBD_FX_CONFIG) {
        s_configured = d.config;
        if (s_configured != 0u) {
            msc_endpoints_open();
        }
    }
    if (d.effects & TIKU_USBD_FX_CLASS_RESET) {
        s_bot = BOT_CBW;
        bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
    }
    switch (d.action) {
    case TIKU_USBD_CTRL_REPLY:
        ep0_tx(d.data, d.len);
        break;
    case TIKU_USBD_CTRL_STATUS:
    case TIKU_USBD_CTRL_ACCEPT_OUT:              /* no data the disk keeps */
        ep0_tx(NULL, 0u);
        break;
    default:
        ep0_stall();
        return;
    }
    ep0_arm_setup();
}

void tiku_p4_usbhs_msc_irq(void)
{
    uint32_t sts = TIKU_P4_REG_READ(GINTSTS) & TIKU_P4_REG_READ(GINTMSK);

    n_irq++;
    if (sts & GINT_USBRST) {
        TIKU_P4_REG_WRITE(GINTSTS, GINT_USBRST);
        tiku_usbd_ctrl_init(&s_ctrl, &s_set);
        s_address = 0u;
        s_configured = 0u;
        s_bot = BOT_CBW;
        TIKU_P4_REG_WRITE(DCFG, TIKU_P4_REG_READ(DCFG) & ~DCFG_DEVADDR_MASK);
        ep_set(DCTL, DCTL_CGNPINNAK | DCTL_CGOUTNAK);
        fifo_flush();
        ep0_arm_setup();
    }
    if (sts & GINT_ENUMDONE) {
        TIKU_P4_REG_WRITE(GINTSTS, GINT_ENUMDONE);
        s_speed = (TIKU_P4_REG_READ(DSTS) >> 1) & 3u;
        TIKU_P4_REG_WRITE(DIEPCTL(0), TIKU_P4_REG_READ(DIEPCTL(0)) & ~3u);
        s_bulk_mps = s_speed == 0u ? USB_BULK_MPS_HS : USB_BULK_MPS;
        tiku_usbd_desc_set_bulk_mps(conf_desc, sizeof conf_desc,
                                    (uint16_t)s_bulk_mps);
    }
    if (sts & GINT_OEPINT) {
        uint32_t daint = TIKU_P4_REG_READ(DAINT);

        if (daint & DAINT_OUT(0)) {
            uint32_t oi = TIKU_P4_REG_READ(DOEPINT(0));

            TIKU_P4_REG_WRITE(DOEPINT(0), oi);
            if (oi & DOEPINT_SETUP) {
                ep0_setup();
            } else if (oi & DEPINT_XFERCOMPL) {
                ep0_arm_setup();
            }
        }
        if (daint & DAINT_OUT(EP_BULK_OUT)) {
            uint32_t oi = TIKU_P4_REG_READ(DOEPINT(EP_BULK_OUT));

            TIKU_P4_REG_WRITE(DOEPINT(EP_BULK_OUT), oi);
            if (oi & DEPINT_XFERCOMPL) {
                uint32_t left = TIKU_P4_REG_READ(DOEPTSIZ(EP_BULK_OUT)) &
                                TSIZ_XFER_MASK;

                tiku_cpu_dcache_invalidate(s_out_buf, s_out_len);
                if (s_bot == BOT_CBW) {
                    bot_on_cbw(TIKU_USBD_MSC_BLOCK - left);
                } else if (s_bot == BOT_DATA_OUT) {
                    bot_send_csw();              /* the WRITE payload landed */
                }
            }
        }
    }
    if (sts & GINT_IEPINT) {
        uint32_t daint = TIKU_P4_REG_READ(DAINT);

        if (daint & DAINT_IN(0)) {
            TIKU_P4_REG_WRITE(DIEPINT(0), TIKU_P4_REG_READ(DIEPINT(0)));
        }
        if (daint & DAINT_IN(EP_BULK_IN)) {
            uint32_t ii = TIKU_P4_REG_READ(DIEPINT(EP_BULK_IN));

            TIKU_P4_REG_WRITE(DIEPINT(EP_BULK_IN), ii);
            if (ii & DEPINT_XFERCOMPL) {
                if (s_bot == BOT_DATA_IN) {
                    bot_send_csw();              /* the READ or reply is out */
                } else if (s_bot == BOT_CSW) {
                    s_bot = BOT_CBW;             /* the command is finished */
                    bulk_out_arm(cbw_buf, TIKU_USBD_MSC_BLOCK);
                }
            }
        }
    }
    if (sts & (GINT_USBSUSP | GINT_WKUPINT)) {
        TIKU_P4_REG_WRITE(GINTSTS, GINT_USBSUSP | GINT_WKUPINT);
    }
}

/** @brief Build the descriptors and the control core's identity. */
static void identity(void)
{
    uint8_t id[6];
    uint16_t n;

    tiku_usbd_device_desc(dev_desc, TIKU_USBD_PID_DISK, 0, USB_EP0_MPS);
    tiku_usbd_msc_config(conf_desc, EP_BULK_OUT, EP_BULK_IN, USB_BULK_MPS);
    s_set.device = dev_desc;
    s_set.config = conf_desc;
    s_set.config_len = sizeof conf_desc;
    s_set.string[0] = tiku_usbd_string_lang(&n);
    s_set.string_len[0] = n;
    s_set.string[1] = str_mfr;
    s_set.string_len[1] = tiku_usbd_string_ascii(str_mfr, sizeof str_mfr, "TikuOS");
    s_set.string[2] = str_prod;
    s_set.string_len[2] = tiku_usbd_string_ascii(str_prod, sizeof str_prod,
                                                 "TikuOS Disk");
    tiku_common_unique_id(id, 6u);
    s_set.string[3] = serial_buf;
    s_set.string_len[3] = tiku_usbd_string_serial(serial_buf, sizeof serial_buf,
                                                  id, 6u);
    s_set.klass = TIKU_USBD_CLASS_MSC;
    s_set.self_powered = 0u;
    tiku_usbd_ctrl_init(&s_ctrl, &s_set);
}

int tiku_p4_usbhs_msc_start(void)
{
    tiku_mem_request_t req = TIKU_MEM_REQUEST_DEFAULT;
    uint32_t base, depth, state;
    int rc;

    if (s_started) {
        return TIKU_P4_USBHS_OK;
    }
    req.alignment = LINE;
    if (msc_disk == NULL) {
        if (tiku_tier_arena_create_opts(&disk_arena, TIKU_MEM_SRAM,
                                        MSC_DISK_BYTES, MSC_ARENA_ID,
                                        &req) != TIKU_MEM_OK) {
            return TIKU_P4_USBHS_ERR_MEM;
        }
        msc_disk = disk_arena.buf;
        memset(msc_disk, 0, MSC_DISK_BYTES);
        (void)tiku_usbd_msc_format_fat12(msc_disk, MSC_DISK_BLOCKS, "TIKUOS");
    }
    rc = tiku_p4_usbhs_up();
    if (rc != TIKU_P4_USBHS_OK) {
        return rc;
    }
    depth = TIKU_P4_REG_READ(GHWCFG3) >> 16;
    if (FIFO_WORDS >= depth) {
        tiku_p4_usbhs_down();
        return TIKU_P4_USBHS_ERR_FIFO;
    }
    identity();
    n_cbw = n_rd = n_wr = n_bad = n_irq = 0u;
    s_bulk_mps = USB_BULK_MPS;
    s_speed = SPEED_NONE;
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(GUSBCFG, TIKU_P4_REG_READ(GUSBCFG) | GUSBCFG_FORCEDEVMODE);
    TIKU_P4_REG_WRITE(GAHBCFG, GAHBCFG_DMAEN | GAHBCFG_HBSTLEN_I4);
    TIKU_P4_REG_WRITE(DCFG, DCFG_NZSTSOUTHSHK);       /* high speed */
    TIKU_P4_REG_WRITE(GRXFSIZ, FIFO_RX_WORDS);
    base = FIFO_RX_WORDS;
    TIKU_P4_REG_WRITE(GNPTXFSIZ, (FIFO_EP0IN_WORDS << 16) | base);
    base += FIFO_EP0IN_WORDS;
    TIKU_P4_REG_WRITE(DIEPTXF(1), (FIFO_EP1IN_WORDS << 16) | base);
    base += FIFO_EP1IN_WORDS;
    TIKU_P4_REG_WRITE(DIEPTXF(3), (FIFO_EP3IN_WORDS << 16) | base);
    base += FIFO_EP3IN_WORDS;
    /* The endpoint-information RAM goes above the data FIFOs; left at its
     * reset value it can overlap them. */
    TIKU_P4_REG_WRITE(GDFIFOCFG, (base << 16) | depth);
    fifo_flush();
    TIKU_P4_REG_WRITE(DIEPMSK, DEPINT_XFERCOMPL);
    TIKU_P4_REG_WRITE(DOEPMSK, DEPINT_XFERCOMPL | DOEPINT_SETUP);
    TIKU_P4_REG_WRITE(DAINTMSK, DAINT_IN(0) | DAINT_OUT(0));
    TIKU_P4_REG_WRITE(GINTSTS, 0xFFFFFFFFu);
    TIKU_P4_REG_WRITE(GINTMSK, GINT_USBRST | GINT_ENUMDONE | GINT_IEPINT |
                                   GINT_OEPINT | GINT_USBSUSP | GINT_WKUPINT);
    TIKU_P4_REG_WRITE(GAHBCFG, TIKU_P4_REG_READ(GAHBCFG) | GAHBCFG_GLBLINTRMSK);
    ep0_arm_setup();
    s_started = 1u;
    TIKU_P4_IRQ_RESTORE(state);
    /* A detach the host can see before the pull-up, so a board restarted
     * with the cable in is enumerated afresh. */
    ep_set(DCTL, DCTL_SFTDISCON);
    tiku_common_delay_ms(20u);
    TIKU_P4_REG_WRITE(DCTL, TIKU_P4_REG_READ(DCTL) & ~DCTL_SFTDISCON);
    return TIKU_P4_USBHS_OK;
}

void tiku_p4_usbhs_msc_stop(void)
{
    tiku_p4_usbhs_down();
    s_started = 0u;
    s_configured = 0u;
    s_address = 0u;
    if (msc_disk != NULL) {
        msc_disk = NULL;
        (void)tiku_arena_destroy(&disk_arena);
    }
}

uint32_t tiku_p4_usbhs_msc_peek(uint32_t lba, uint8_t *dst, uint32_t n)
{
    if (msc_disk == NULL || lba >= MSC_DISK_BLOCKS || dst == NULL) {
        return 0u;
    }
    if (n > TIKU_USBD_MSC_BLOCK) {
        n = TIKU_USBD_MSC_BLOCK;
    }
    memcpy(dst, &msc_disk[lba * TIKU_USBD_MSC_BLOCK], n);
    return n;
}

void tiku_p4_usbhs_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                             uint32_t *bad, uint32_t *irq, uint8_t *cfg,
                             uint32_t *speed)
{
    if (cbw != NULL)   { *cbw = n_cbw; }
    if (rd != NULL)    { *rd = n_rd; }
    if (wr != NULL)    { *wr = n_wr; }
    if (bad != NULL)   { *bad = n_bad; }
    if (irq != NULL)   { *irq = n_irq; }
    if (cfg != NULL)   { *cfg = s_configured; }
    if (speed != NULL) { *speed = s_speed; }
}
