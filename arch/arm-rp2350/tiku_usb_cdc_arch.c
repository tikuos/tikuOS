/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_cdc_arch.c - RP2350 native USB CDC-ACM console backend
 *
 * A minimal, polled USB 1.1 full-speed device stack presenting one CDC-ACM
 * virtual serial port on the Pico 2's USB connector. See the header for the
 * rationale and caveats. The flow mirrors the canonical RP2040/RP2350
 * "dev_lowlevel" device example: PLL_USB -> clk_usb -> controller -> DPRAM
 * endpoint setup -> EP0 enumeration -> bulk IN/OUT data.
 *
 * HARDWARE BRING-UP NOTE: the USB *controller* and *DPRAM* register offsets
 * and bit positions below follow the RP2040 USB device controller (the
 * RP2350 device-mode block is the same IP). They are marked and grouped so
 * they can be checked against the RP2350 datasheet "USB" section during
 * first bring-up; the enumeration/descriptor logic above them is silicon-
 * independent. USB enumeration always needs a host-side dmesg / analyzer
 * pass on first silicon -- this driver is written correct-by-construction
 * but has not been validated on hardware here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>
#include <arch/arm-rp2350/tiku_rp2350_regs.h>
#include <kernel/vfs/tiku_vfs.h>   /* TIKU_VFS_CAP_ALL for the console backend */
#include <stdarg.h>
#include <stddef.h>

/* Bring-up trace -> UART directly (NOT TIKU_PRINTF, which would recurse back
 * through the USB putc). Enable with -DTIKU_USB_CDC_DEBUG=1. */
#ifdef TIKU_USB_CDC_DEBUG
#include <arch/arm-rp2350/tiku_uart_arch.h>
#define UDBG(...) tiku_uart_printf(__VA_ARGS__)
#else
#define UDBG(...) do { } while (0)
#endif

/*===========================================================================*/
/* RP2350 USB controller + DPRAM registers (verify vs datasheet on bring-up) */
/*===========================================================================*/

#define USB_REGS_BASE        0x50110000UL
#define USB_DPRAM_BASE       0x50100000UL

/* USBCTRL_REGS offsets */
#define USB_ADDR_ENDP        0x00U   /* DEV_ADDR [6:0]                       */
#define USB_MAIN_CTRL        0x40U
#define USB_SIE_CTRL         0x4CU
#define USB_SIE_STATUS       0x50U
#define USB_BUFF_STATUS      0x58U
#define USB_EP_STALL_ARM     0x68U
#define USB_USB_MUXING       0x74U
#define USB_USB_PWR          0x78U
#define USB_INTE             0x90U   /* (unused: we poll, no NVIC IRQ)       */

/* MAIN_CTRL */
#define USB_MAIN_CTRL_CONTROLLER_EN  (1U << 0)
#define USB_MAIN_CTRL_HOST_NDEVICE   (1U << 1)
/* SIE_CTRL */
#define USB_SIE_CTRL_EP0_INT_1BUF    (1U << 29)
#define USB_SIE_CTRL_PULLUP_EN       (1U << 16)
/* SIE_STATUS (write-1-to-clear for the latched bits) */
#define USB_SIE_STATUS_SETUP_REC     (1U << 17)
#define USB_SIE_STATUS_BUS_RESET     (1U << 19)
/* USB_MUXING / USB_PWR */
#define USB_MUXING_TO_PHY            (1U << 0)
#define USB_MUXING_SOFTCON           (1U << 3)
#define USB_PWR_VBUS_DETECT          (1U << 2)
#define USB_PWR_VBUS_DETECT_OVR_EN   (1U << 3)

/* DPRAM register/buffer offsets (relative to USB_DPRAM_BASE) */
#define DP_SETUP_PKT         0x00U   /* 8-byte SETUP packet                  */
#define DP_EP1_IN_EPCTRL     0x08U   /* endpoint control (ep1..15)           */
#define DP_EP2_OUT_EPCTRL    0x14U
#define DP_EP3_IN_EPCTRL     0x18U
#define DP_EP0_IN_BUFCTRL    0x80U   /* buffer control (ep0..15)             */
#define DP_EP0_OUT_BUFCTRL   0x84U
#define DP_EP1_IN_BUFCTRL    0x88U
#define DP_EP2_OUT_BUFCTRL   0x94U
#define DP_EP3_IN_BUFCTRL    0x98U
#define DP_EP0_BUF           0x100U  /* data buffers (64 B each)             */
#define DP_EP1_BUF           0x140U
#define DP_EP2_BUF           0x180U
#define DP_EP3_BUF           0x1C0U

/* Endpoint control register bits */
#define EP_CTRL_ENABLE               (1U << 31)
#define EP_CTRL_INT_1BUF             (1U << 29)
#define EP_CTRL_TYPE_LSB             26          /* 0 ctrl 1 iso 2 bulk 3 int */
/* Buffer control register bits */
#define BUF_CTRL_FULL                (1U << 15)
#define BUF_CTRL_LAST                (1U << 14)
#define BUF_CTRL_DATA1_PID           (1U << 13)
#define BUF_CTRL_RESET               (1U << 12)
#define BUF_CTRL_STALL               (1U << 11)
#define BUF_CTRL_AVAIL               (1U << 10)
#define BUF_CTRL_LEN_MASK            0x3FFU

/* BUFF_STATUS bit per endpoint/dir: (epnum*2 + dir), dir 0=IN 1=OUT */
#define BUFF_STATUS_EP0_IN           (1U << 0)
#define BUFF_STATUS_EP0_OUT          (1U << 1)
#define BUFF_STATUS_EP2_OUT          (1U << 5)
#define BUFF_STATUS_EP3_IN           (1U << 6)

/* clk_usb routing bits (mirror the CLK_PERI pattern in tiku_rp2350_regs.h) */
#define CLK_USB_CTRL_ENABLE          (1U << 11)
#define CLK_USB_CTRL_AUXSRC_PLL_USB  (0U << 5)

#define USB_EP_PKT_MAX               64U

static inline uint32_t usb_rd(uint32_t off) {
    return _RP2350_REG(USB_REGS_BASE + off);
}
static inline void usb_wr(uint32_t off, uint32_t v) {
    _RP2350_REG(USB_REGS_BASE + off) = v;
}
static inline void usb_set(uint32_t off, uint32_t m) {
    _RP2350_REG_SET(USB_REGS_BASE + off, m);
}
static inline uint32_t dp_rd(uint32_t off) {
    return _RP2350_REG(USB_DPRAM_BASE + off);
}
static inline void dp_wr(uint32_t off, uint32_t v) {
    _RP2350_REG(USB_DPRAM_BASE + off) = v;
}
static inline volatile uint8_t *dp_buf(uint32_t off) {
    return (volatile uint8_t *)(USB_DPRAM_BASE + off);
}

/*===========================================================================*/
/* USB / CDC descriptors                                                     */
/*===========================================================================*/

static const uint8_t dev_desc[] = {
    18, 0x01,               /* bLength, DEVICE                              */
    0x00, 0x02,             /* bcdUSB 2.00                                  */
    0xEF, 0x02, 0x01,       /* class MISC / subclass 2 / proto 1 (IAD)      */
    USB_EP_PKT_MAX,         /* bMaxPacketSize0                              */
    0x8A, 0x2E,             /* idVendor 0x2E8A (Raspberry Pi)               */
    0x09, 0x00,             /* idProduct 0x0009                             */
    0x00, 0x01,             /* bcdDevice 1.00                               */
    0x01, 0x02, 0x03,       /* iManufacturer / iProduct / iSerial           */
    0x01                    /* bNumConfigurations                           */
};

#define CONF_TOTAL_LEN 75
static const uint8_t conf_desc[CONF_TOTAL_LEN] = {
    /* Configuration */
    9, 0x02, CONF_TOTAL_LEN, 0x00, 0x02, 0x01, 0x00, 0x80, 50,
    /* Interface Association: CDC, 2 interfaces from #0 */
    8, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
    /* Interface 0: Communications, ACM, 1 endpoint */
    9, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00,
    /* CDC Header functional */
    5, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management functional (no data) */
    5, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM functional (supports Set/Get Line Coding + Line State) */
    4, 0x24, 0x02, 0x02,
    /* CDC Union functional (control 0, subordinate 1) */
    5, 0x24, 0x06, 0x00, 0x01,
    /* Endpoint EP1 IN, interrupt, 8 bytes, interval 16 ms */
    7, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
    /* Interface 1: CDC Data, 2 endpoints */
    9, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* Endpoint EP2 OUT, bulk, 64 bytes */
    7, 0x05, 0x02, 0x02, USB_EP_PKT_MAX, 0x00, 0x00,
    /* Endpoint EP3 IN, bulk, 64 bytes */
    7, 0x05, 0x83, 0x02, USB_EP_PKT_MAX, 0x00, 0x00
};

/* String descriptors: 0 = langid, 1 = mfr, 2 = product, 3 = serial */
static const uint8_t str_lang[]   = { 4, 0x03, 0x09, 0x04 };
static const uint8_t str_mfr[]    = { 14, 0x03, 'T',0, 'i',0, 'k',0, 'u',0,
                                      'O',0, 'S',0 };
static const uint8_t str_prod[]   = { 30, 0x03, 'T',0,'i',0,'k',0,'u',0,'O',0,
                                      'S',0,' ',0,'C',0,'o',0,'n',0,'s',0,
                                      'o',0,'l',0,'e',0 };
static const uint8_t str_serial[] = { 10, 0x03, '0',0,'0',0,'0',0,'1',0 };

/*===========================================================================*/
/* Driver state                                                              */
/*===========================================================================*/

#define TX_RING_SIZE   512U   /* power of two */
#define RX_RING_SIZE   256U   /* power of two */
#define TX_RING_MASK   (TX_RING_SIZE - 1U)
#define RX_RING_MASK   (RX_RING_SIZE - 1U)
/* Bounded wait for a free TX slot when the ring is full: ~2 USB full-speed
 * frames, long enough for a reading host to drain a bulk-IN packet, short
 * enough that a not-reading host only stalls once (then drop-fast latches). */
#define TX_FULL_WAIT_US  2000U

/* EP0 control-transfer phase. A control transfer is SETUP, then an optional
 * DATA stage (IN for reads / OUT for writes), then an opposite-direction
 * zero-length STATUS stage. Conflating these is what breaks enumeration. */
enum { PH_IDLE = 0, PH_DATA_IN, PH_STATUS_OUT, PH_DATA_OUT, PH_STATUS_IN };

static struct {
    uint8_t  inited;           /* init done -- guards USB-register access    */
    uint8_t  configured;       /* SET_CONFIGURATION received                */
    uint8_t  dtr;              /* host asserted DTR (port opened)           */
    uint8_t  pending_addr;     /* device address to apply after status      */
    uint8_t  ep0_pid;          /* next EP0 data PID (DATA0/DATA1)           */
    uint8_t  ep0_phase;        /* PH_* control-transfer phase               */
    uint8_t  ep3_pid;          /* next bulk-IN PID                          */
    uint8_t  ep2_pid;          /* next bulk-OUT PID                         */
    uint8_t  ep3_busy;         /* bulk-IN transfer in flight                */
    const uint8_t *in_ptr;     /* remaining EP0 control-IN payload          */
    uint16_t in_rem;
    /* TX (device->host) and RX (host->device) byte rings */
    volatile uint8_t  tx[TX_RING_SIZE];
    volatile uint16_t tx_head, tx_tail;
    volatile uint8_t  tx_stalled;  /* full-ring wait timed out: host isn't
                                    * reading -> drop-fast (no per-byte wait)
                                    * until it drains again (see putc)       */
    volatile uint8_t  rx[RX_RING_SIZE];
    volatile uint16_t rx_head, rx_tail;
    volatile uint16_t overrun;
} u;

/*===========================================================================*/
/* Low-level endpoint helpers                                                */
/*===========================================================================*/

/* Arm a buffer-control register: write info, brief settle, then AVAIL (the
 * controller requires a delay between the descriptor write and AVAIL). */
static void ep_arm(uint32_t bufctrl_off, uint32_t val) {
    dp_wr(bufctrl_off, val);
    __asm volatile("nop\nnop\nnop");
    dp_wr(bufctrl_off, val | BUF_CTRL_AVAIL);
}

/* Send up to 64 bytes on EP0 IN with the current PID, toggling it. */
static void ep0_in(const uint8_t *data, uint16_t len) {
    if (len > USB_EP_PKT_MAX) len = USB_EP_PKT_MAX;
    for (uint16_t i = 0; i < len; i++) dp_buf(DP_EP0_BUF)[i] = data[i];
    uint32_t v = BUF_CTRL_FULL | BUF_CTRL_LAST | (len & BUF_CTRL_LEN_MASK);
    if (u.ep0_pid) v |= BUF_CTRL_DATA1_PID;
    u.ep0_pid ^= 1U;
    ep_arm(DP_EP0_IN_BUFCTRL, v);
}

/* Zero-length EP0 IN status (the device's ack of a no-data/write transfer). */
static void ep0_status_in(void) {
    u.ep0_pid = 1U;                 /* status stage is always DATA1 */
    u.ep0_phase = PH_STATUS_IN;
    ep0_in((const uint8_t *)0, 0);
}

/* Begin a (possibly multi-packet) EP0 control-IN (read) of `len` bytes from
 * `data`, capped at the host's wLength. */
static void ep0_send(const uint8_t *data, uint16_t len, uint16_t wlen) {
    if (len > wlen) len = wlen;
    u.ep0_pid = 1U;                 /* first data packet after SETUP = DATA1 */
    u.ep0_phase = PH_DATA_IN;
    if (len <= USB_EP_PKT_MAX) {
        u.in_ptr = (const uint8_t *)0;
        u.in_rem = 0;
        ep0_in(data, len);
    } else {
        ep0_in(data, USB_EP_PKT_MAX);
        u.in_ptr = data + USB_EP_PKT_MAX;
        u.in_rem = len - USB_EP_PKT_MAX;
    }
}

/* Enable an endpoint by writing its endpoint-control register in DPRAM. */
static void ep_enable(uint32_t epctrl_off, uint32_t buf_off, uint32_t type) {
    dp_wr(epctrl_off, EP_CTRL_ENABLE | EP_CTRL_INT_1BUF |
                      (type << EP_CTRL_TYPE_LSB) | buf_off);
}

/* Arm the bulk-OUT endpoint to receive a packet. */
static void rx_arm(void) {
    uint32_t v = USB_EP_PKT_MAX;
    if (u.ep2_pid) v |= BUF_CTRL_DATA1_PID;
    ep_arm(DP_EP2_OUT_BUFCTRL, v);
}

/*===========================================================================*/
/* TX path (bulk IN, device -> host)                                         */
/*===========================================================================*/

static void tx_kick(void) {
    if (!u.configured || u.ep3_busy) return;
    uint16_t n = 0;
    while (n < USB_EP_PKT_MAX && u.tx_tail != u.tx_head) {
        dp_buf(DP_EP3_BUF)[n++] = u.tx[u.tx_tail];
        u.tx_tail = (u.tx_tail + 1U) & TX_RING_MASK;
    }
    if (n == 0) return;
    uint32_t v = BUF_CTRL_FULL | BUF_CTRL_LAST | (n & BUF_CTRL_LEN_MASK);
    if (u.ep3_pid) v |= BUF_CTRL_DATA1_PID;
    u.ep3_pid ^= 1U;
    u.ep3_busy = 1U;
    ep_arm(DP_EP3_IN_BUFCTRL, v);
}

/*===========================================================================*/
/* EP0 control / enumeration                                                 */
/*===========================================================================*/

static void handle_setup(void) {
    uint8_t  bmRequestType = dp_buf(DP_SETUP_PKT)[0];
    uint8_t  bRequest      = dp_buf(DP_SETUP_PKT)[1];
    uint16_t wValue        = (uint16_t)(dp_buf(DP_SETUP_PKT)[2] |
                                        (dp_buf(DP_SETUP_PKT)[3] << 8));
    uint16_t wLength       = (uint16_t)(dp_buf(DP_SETUP_PKT)[6] |
                                        (dp_buf(DP_SETUP_PKT)[7] << 8));
    u.ep0_pid = 1U;
    u.ep0_phase = PH_IDLE;          /* dispatch below sets the real phase   */
    UDBG("[u]S t%02x r%02x v%x l%u\n", bmRequestType, bRequest, wValue, wLength);

    if ((bmRequestType & 0x60U) == 0x00U) {           /* standard requests */
        switch (bRequest) {
        case 0x06: {                                  /* GET_DESCRIPTOR     */
            uint8_t type = (uint8_t)(wValue >> 8);
            uint8_t idx  = (uint8_t)(wValue & 0xFFU);
            if (type == 0x01) {
                ep0_send(dev_desc, sizeof dev_desc, wLength);
            } else if (type == 0x02) {
                ep0_send(conf_desc, sizeof conf_desc, wLength);
            } else if (type == 0x03) {                /* STRING             */
                const uint8_t *s = str_lang;
                uint16_t l = sizeof str_lang;
                if (idx == 1) { s = str_mfr;    l = sizeof str_mfr; }
                else if (idx == 2) { s = str_prod;   l = sizeof str_prod; }
                else if (idx == 3) { s = str_serial; l = sizeof str_serial; }
                ep0_send(s, l, wLength);
            } else {                                  /* unsupported (e.g.  */
                UDBG("[u]STALL d%02x\n", type);       /*  BOS 0x0F): stall  */
                dp_wr(DP_EP0_IN_BUFCTRL, BUF_CTRL_STALL);
                usb_set(USB_EP_STALL_ARM, 0x1U);      /* arm EP0 IN stall   */
                u.ep0_phase = PH_IDLE;                /* next SETUP clears  */
            }
            break;
        }
        case 0x05:                                    /* SET_ADDRESS        */
            u.pending_addr = (uint8_t)(wValue & 0x7FU);
            ep0_status_in();                             /* addr applied later */
            break;
        case 0x09:                                    /* SET_CONFIGURATION  */
            u.configured = 1U;
            u.ep2_pid = u.ep3_pid = 0U;
            u.ep3_busy = 0U;
            ep_enable(DP_EP1_IN_EPCTRL, DP_EP1_BUF, 3U);   /* notify (int) */
            ep_enable(DP_EP2_OUT_EPCTRL, DP_EP2_BUF, 2U);  /* bulk OUT     */
            ep_enable(DP_EP3_IN_EPCTRL, DP_EP3_BUF, 2U);   /* bulk IN      */
            rx_arm();
            ep0_status_in();
            break;
        case 0x08: {                                  /* GET_CONFIGURATION  */
            uint8_t cfg = u.configured ? 1U : 0U;
            ep0_send(&cfg, 1U, wLength);
            break;
        }
        case 0x00: {                                  /* GET_STATUS         */
            static const uint8_t z[2] = { 0, 0 };
            ep0_send(z, 2U, wLength);
            break;
        }
        default:                                      /* SET/CLEAR_FEATURE… */
            ep0_status_in();
            break;
        }
    } else if ((bmRequestType & 0x60U) == 0x20U) {    /* CDC class requests */
        switch (bRequest) {
        case 0x22:                                    /* SET_CONTROL_LINE_  */
            u.dtr = (uint8_t)(wValue & 0x1U);         /*   STATE: bit0=DTR  */
            ep0_status_in();
            break;
        case 0x20:                                    /* SET_LINE_CODING    */
            /* 7-byte OUT data stage follows; accept it (we don't use it) and
             * the IN status is sent once the data arrives (PH_DATA_OUT). */
            u.ep0_phase = PH_DATA_OUT;
            ep_arm(DP_EP0_OUT_BUFCTRL, BUF_CTRL_DATA1_PID | 7U);
            break;
        case 0x21: {                                  /* GET_LINE_CODING    */
            static const uint8_t lc[7] = { 0x00, 0xC2, 0x01, 0x00,
                                           0x00, 0x00, 0x08 }; /* 115200 8N1 */
            ep0_send(lc, sizeof lc, wLength);
            break;
        }
        default:
            ep0_status_in();
            break;
        }
    } else {
        ep0_status_in();
    }
}

/*===========================================================================*/
/* Buffer-status (transfer-complete) handling                               */
/*===========================================================================*/

static void handle_buff_status(void) {
    uint32_t bs = usb_rd(USB_BUFF_STATUS);

    if (bs & BUFF_STATUS_EP0_IN) {
        usb_wr(USB_BUFF_STATUS, BUFF_STATUS_EP0_IN);
        if (u.ep0_phase == PH_DATA_IN) {
            if (u.in_rem) {                          /* more read data       */
                uint16_t n = (u.in_rem > USB_EP_PKT_MAX) ? USB_EP_PKT_MAX
                                                         : u.in_rem;
                ep0_in(u.in_ptr, n);
                u.in_ptr += n;
                u.in_rem -= (uint16_t)n;
            } else {                                 /* read data done       */
                u.ep0_phase = PH_STATUS_OUT;         /*  -> host status OUT  */
                ep_arm(DP_EP0_OUT_BUFCTRL, BUF_CTRL_DATA1_PID);
            }
        } else if (u.ep0_phase == PH_STATUS_IN) {    /* no-data/write done   */
            if (u.pending_addr) {                    /* SET_ADDRESS applies  */
                UDBG("[u]ADDR=%u\n", u.pending_addr);
                usb_wr(USB_ADDR_ENDP, u.pending_addr);  /*  after the status */
                u.pending_addr = 0U;
            }
            u.ep0_phase = PH_IDLE;
        }
    }
    if (bs & BUFF_STATUS_EP0_OUT) {
        usb_wr(USB_BUFF_STATUS, BUFF_STATUS_EP0_OUT);
        if (u.ep0_phase == PH_STATUS_OUT) {          /* read transfer done   */
            u.ep0_phase = PH_IDLE;
        } else if (u.ep0_phase == PH_DATA_OUT) {     /* write data received  */
            ep0_status_in();                         /*  -> send IN status   */
        }
    }
    if (bs & BUFF_STATUS_EP2_OUT) {   /* bulk OUT: bytes from host          */
        usb_wr(USB_BUFF_STATUS, BUFF_STATUS_EP2_OUT);
        uint32_t ctrl = dp_rd(DP_EP2_OUT_BUFCTRL);
        uint16_t len = (uint16_t)(ctrl & BUF_CTRL_LEN_MASK);
        for (uint16_t i = 0; i < len; i++) {
            uint16_t nxt = (uint16_t)((u.rx_head + 1U) & RX_RING_MASK);
            if (nxt != u.rx_tail) {
                u.rx[u.rx_head] = dp_buf(DP_EP2_BUF)[i];
                u.rx_head = nxt;
            } else {
                u.overrun++;
            }
        }
        u.ep2_pid ^= 1U;
        rx_arm();
    }
    if (bs & BUFF_STATUS_EP3_IN) {    /* bulk IN: host took our packet      */
        usb_wr(USB_BUFF_STATUS, BUFF_STATUS_EP3_IN);
        u.ep3_busy = 0U;
        tx_kick();
    }
}

/*===========================================================================*/
/* Public: poll / init                                                       */
/*===========================================================================*/

void tiku_usb_cdc_poll(void) {
    if (!u.inited) return;          /* don't touch USB regs before init */
    uint32_t st = usb_rd(USB_SIE_STATUS);
    if (st & USB_SIE_STATUS_BUS_RESET) {
        UDBG("[u]RESET\n");
        usb_wr(USB_SIE_STATUS, USB_SIE_STATUS_BUS_RESET);
        usb_wr(USB_ADDR_ENDP, 0U);
        u.configured = u.dtr = 0U;
        u.pending_addr = 0U;
        u.ep3_busy = 0U;
        u.ep0_phase = PH_IDLE;
    }
    if (st & USB_SIE_STATUS_SETUP_REC) {
        usb_wr(USB_SIE_STATUS, USB_SIE_STATUS_SETUP_REC);
        handle_setup();
    }
    if (usb_rd(USB_BUFF_STATUS)) {
        handle_buff_status();
    }
    tx_kick();
}

static void pll_usb_48mhz(void) {
    /* Mirror rp2350_pll_sys_init for PLL_USB: 12 MHz ref * 100 = 1200 MHz
     * VCO, /5 /5 = 48 MHz. */
    rp2350_unreset(RP2350_RESETS_PLL_USB);
    _RP2350_REG(RP2350_PLL_USB_BASE + RP2350_PLL_CS) = 1U;          /* REFDIV 1 */
    _RP2350_REG(RP2350_PLL_USB_BASE + RP2350_PLL_FBDIV_INT) = 100U; /* VCO 1200 */
    _RP2350_REG_CLR(RP2350_PLL_USB_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD);
    for (uint32_t i = 0; i < 1000000U; i++) {
        if (_RP2350_REG(RP2350_PLL_USB_BASE + RP2350_PLL_CS) &
            RP2350_PLL_CS_LOCK) break;
    }
    _RP2350_REG(RP2350_PLL_USB_BASE + RP2350_PLL_PRIM) =
        (5U << RP2350_PLL_PRIM_POSTDIV1_S) | (5U << RP2350_PLL_PRIM_POSTDIV2_S);
    _RP2350_REG_CLR(RP2350_PLL_USB_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_POSTDIVPD);
    /* Route clk_usb from PLL_USB at 48 MHz (DIV left at reset = /1). */
    _RP2350_REG(RP2350_CLK_USB_CTRL) =
        CLK_USB_CTRL_AUXSRC_PLL_USB | CLK_USB_CTRL_ENABLE;
}

void tiku_usb_cdc_init(void) {
    for (uint16_t i = 0; i < sizeof u; i++) ((volatile uint8_t *)&u)[i] = 0U;

    pll_usb_48mhz();
    rp2350_unreset(RP2350_RESETS_USBCTRL);

    /* Clear the 4 KB USB DPRAM. */
    for (uint32_t off = 0; off < 0x1000U; off += 4U) dp_wr(off, 0U);

    /* Connect the controller to the on-chip PHY and force VBUS detect (the
     * Pico's USB pins are wired straight to the connector). */
    usb_wr(USB_USB_MUXING, USB_MUXING_TO_PHY | USB_MUXING_SOFTCON);
    usb_wr(USB_USB_PWR, USB_PWR_VBUS_DETECT | USB_PWR_VBUS_DETECT_OVR_EN);

    /* Device mode, controller enabled, single-buffered EP0 interrupts. */
    usb_wr(USB_MAIN_CTRL, USB_MAIN_CTRL_CONTROLLER_EN);   /* HOST_NDEVICE=0 */
    usb_wr(USB_SIE_CTRL, USB_SIE_CTRL_EP0_INT_1BUF);

    /* Present the pull-up: the host now sees a device and starts enumerating. */
    usb_set(USB_SIE_CTRL, USB_SIE_CTRL_PULLUP_EN);
    u.inited = 1U;                  /* poll() may now touch the controller */

    /* Service enumeration synchronously. The host's reset / SETUP / SET_ADDRESS
     * sequence needs prompt EP0 responses, but this runs early in boot -- long
     * before the scheduler (and the idle-hook poll) exists. Spin-poll until the
     * device is configured (a host is present) or a bounded number of spins
     * elapse (no host -> just continue booting; idle-hook poll takes over). */
    for (uint32_t i = 0; i < 4000000U && !u.configured; i++) {
        tiku_usb_cdc_poll();
    }
}

uint8_t tiku_usb_cdc_connected(void) {
    return (uint8_t)(u.configured && u.dtr);
}

/*===========================================================================*/
/* Public: output                                                            */
/*===========================================================================*/

void tiku_usb_cdc_putc(char c) {
    uint16_t nxt = (uint16_t)((u.tx_head + 1U) & TX_RING_MASK);

    if (nxt == u.tx_tail && !u.tx_stalled) {
        /* Ring full and the host has been keeping up: give the bus a bounded
         * window to drain a slot rather than silently dropping the byte (the
         * host reads a bulk-IN packet ~once per 1 ms USB frame).  If it stays
         * full to the deadline the host isn't reading -- latch tx_stalled so
         * the rest of a burst drops fast instead of paying the wait per byte
         * and freezing the console. */
        uint32_t t0 = _RP2350_REG(RP2350_TIMER0_TIMERAWL);
        do {
            tiku_usb_cdc_poll();
        } while (nxt == u.tx_tail &&
                 (uint32_t)(_RP2350_REG(RP2350_TIMER0_TIMERAWL) - t0)
                     < TX_FULL_WAIT_US);
        u.tx_stalled = (uint8_t)(nxt == u.tx_tail);
    }

    if (nxt != u.tx_tail) {
        u.tx[u.tx_head] = (uint8_t)c;
        u.tx_head = nxt;
        u.tx_stalled = 0U;   /* took a slot cleanly: the host is draining */
    }
    /* Service the bus so the byte actually leaves and the ring drains. */
    tiku_usb_cdc_poll();
}

void tiku_usb_cdc_puts(const char *s) {
    if (!s) return;
    while (*s) tiku_usb_cdc_putc(*s++);
}

void tiku_usb_cdc_flush(void) {
    /* Drive the TX ring empty AND wait out the last in-flight IN packet, so
     * the host has pulled every byte. Bounded so a vanished host (port
     * closed, cable out) can't wedge a reboot path forever. */
    uint32_t spins = 0U;
    while ((u.tx_head != u.tx_tail || u.ep3_busy) && spins < 4000000U) {
        tiku_usb_cdc_poll();
        spins++;
    }
}

/* --- lightweight formatter (mirrors tiku_uart_printf) --------------------- */
static void cdc_print_uint(unsigned long v, unsigned base, unsigned width,
                           char pad) {
    char tmp[20];
    int n = 0;
    if (v == 0UL) tmp[n++] = '0';
    while (v) {
        unsigned d = (unsigned)(v % base);
        tmp[n++] = (char)(d < 10U ? ('0' + d) : ('a' + d - 10U));
        v /= base;
    }
    for (unsigned w = (unsigned)n; w < width; w++) tiku_usb_cdc_putc(pad);
    while (n) tiku_usb_cdc_putc(tmp[--n]);
}

static void cdc_print_int(long v, unsigned width, char pad) {
    if (v < 0) { tiku_usb_cdc_putc('-'); cdc_print_uint((unsigned long)(-v),
                                                        10U, width, pad); }
    else cdc_print_uint((unsigned long)v, 10U, width, pad);
}

void tiku_usb_cdc_printf(const char *fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') tiku_usb_cdc_putc('\r');
            tiku_usb_cdc_putc(*fmt++);
            continue;
        }
        fmt++;
        unsigned width = 0U;
        char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10U) + (unsigned)(*fmt - '0');
            fmt++;
        }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        switch (*fmt) {
        case 'c': tiku_usb_cdc_putc((char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) tiku_usb_cdc_putc(*s++);
            break;
        }
        case 'd': cdc_print_int(is_long ? va_arg(ap, long) : va_arg(ap, int),
                                width, pad); break;
        case 'u': cdc_print_uint(is_long ? va_arg(ap, unsigned long)
                                         : va_arg(ap, unsigned), 10U,
                                 width, pad); break;
        case 'x': cdc_print_uint(is_long ? va_arg(ap, unsigned long)
                                         : va_arg(ap, unsigned), 16U,
                                 width, pad); break;
        case '%': tiku_usb_cdc_putc('%'); break;
        case '\0': va_end(ap); return;
        default:  tiku_usb_cdc_putc('%'); tiku_usb_cdc_putc(*fmt); break;
        }
        fmt++;
    }
    va_end(ap);
}

/*===========================================================================*/
/* Public: input                                                             */
/*===========================================================================*/

uint8_t tiku_usb_cdc_rx_ready(void) {
    tiku_usb_cdc_poll();
    return (uint8_t)(u.rx_head != u.rx_tail);
}

int tiku_usb_cdc_getc(void) {
    tiku_usb_cdc_poll();
    if (u.rx_head == u.rx_tail) return -1;
    uint8_t c = u.rx[u.rx_tail];
    u.rx_tail = (uint16_t)((u.rx_tail + 1U) & RX_RING_MASK);
    return (int)c;
}

uint16_t tiku_usb_cdc_overrun_count(void) { return u.overrun; }
void     tiku_usb_cdc_overrun_reset(void) { u.overrun = 0U; }

/*===========================================================================*/
/* Shell I/O backend                                                         */
/*===========================================================================*/

const tiku_shell_io_t tiku_shell_io_usbcdc = {
    tiku_usb_cdc_putc,
    tiku_usb_cdc_rx_ready,
    tiku_usb_cdc_getc,
    TIKU_SHELL_IO_ECHO | TIKU_SHELL_IO_CRLF,
    TIKU_VFS_CAP_ALL   /* native-USB console = full authority (like UART); without
                        * this the fail-closed default (CAP_NONE) would EPERM every
                        * HW/SYS/FS write from the local console */
};
