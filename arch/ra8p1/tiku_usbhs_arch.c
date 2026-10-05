/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbhs_arch.c - EK-RA8P1 USB 2.0 high-speed device controller.
 *
 * Bring-up follows UM Figure 38.2, an ordered sequence with two waits.  EP0
 * enumeration runs in the interrupt; mass storage runs in process context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_usbhs_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include <kernel/usb/tiku_usbd_msc.h>
#include "tiku_store_arch.h"

/*
 * High speed runs the PHY's own PLL from the EXTAL pin at 12, 20, 24 or
 * 48 MHz, with PHYSET.HSEB = 0.  CL-only mode (HSEB = 1) has no high speed
 * (UM 38.3.3), and USBCLK is needed only in CL-only mode (UM 9.10.16), so
 * USBCKCR is not touched.  This board has a 24 MHz crystal on EXTAL: the
 * clock setup is CLKSEL = 24 MHz and a wait for the PLL to lock.
 */
#define USBHS_CLKSEL_BOARD  RA8P1_PHYSET_CLKSEL_24M

/*
 * PD07 is USBHS_VBUSEN, which switches 5 V onto the connector in host mode.
 * It is held low as a GPIO output, as the board manual asks in device mode,
 * so the controller cannot source VBUS against a host.
 */
#define USBHS_PORT_VBUSEN   0xDU
#define USBHS_PIN_VBUSEN    7U

/*
 * PD04 is USBHS_ID, the OTG role pin: low is A-device (host), high is
 * B-device (device).  It is muxed as an input so SYSSTS0.IDMON shows what the
 * board's "USBHS Role Toggle" switch selects; an unwired pin can float, so
 * the reading is not conclusive.
 */
#define USBHS_PORT_ID       0xDU
#define USBHS_PIN_ID        4U
#define USBHS_PFS_PSEL_USBHS 0x14U   /* 10100b, per the port function tables */

static uint8_t usbhs_up;

/** @brief Bounded spin for the PLL lock flag, in ~1 us units. */
#define USBHS_PLL_TIMEOUT_US  10000U

/** @brief Drive VBUSEN low as a GPIO output and mux ID as a USBHS input. */
static void usbhs_vbusen_low(void)
{
    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    /* Output, driving low, peripheral mux off. */
    TIKU_REG32(RA8P1_PFS(USBHS_PORT_VBUSEN, USBHS_PIN_VBUSEN)) =
        RA8P1_PFS_PDR;
    /* ID as a peripheral input, so SYSSTS0.IDMON reports the role strap. */
    TIKU_REG32(RA8P1_PFS(USBHS_PORT_ID, USBHS_PIN_ID)) =
        ((uint32_t)USBHS_PFS_PSEL_USBHS << RA8P1_PFS_PSEL_SHIFT) |
        RA8P1_PFS_PMR;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
    __asm__ volatile ("dsb" ::: "memory");
}

int tiku_ra8p1_usbhs_up(int want_high)
{
    uint16_t v;
    uint32_t spins;

    if (usbhs_up) {
        return TIKU_RA8P1_USBHS_OK;
    }

    /* Module stop is released first: no register answers while it is set. */
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPB_USBHS;
    (void)TIKU_REG32(RA8P1_MSTPCRB);

    usbhs_vbusen_low();

    /*
     * Operation mode.  DCFM = 0 selects the device controller.  DRPD, the
     * host's D+/D- pull-downs, resets to 1 and is cleared here.
     *
     * CNEN enables the single-ended receivers, which watch D+ and D- one at a
     * time and so detect SE0, the bus reset.  With CNEN clear the
     * differential receiver still works, but SYSSTS0.LNST reads 0 and bus
     * resets go unseen.
     */
    v = TIKU_REG16(RA8P1_USBHS_SYSCFG);
    v &= (uint16_t)~(RA8P1_SYSCFG_DCFM | RA8P1_SYSCFG_DRPD |
                     RA8P1_SYSCFG_DPRPU | RA8P1_SYSCFG_USBE);
    v |= RA8P1_SYSCFG_CNEN;
    if (want_high) {
        v |= RA8P1_SYSCFG_HSE;
    } else {
        v &= (uint16_t)~RA8P1_SYSCFG_HSE;
    }
    TIKU_REG16(RA8P1_USBHS_SYSCFG) = v;

    /*
     * PHY clock: HSEB clear (not CL-only), reference from EXTAL.  HSEB and
     * CLKSEL are written together, before the waits below: CLKSEL is ignored
     * while CL-only is selected and the PLL is stopped.
     */
    v = TIKU_REG16(RA8P1_USBHS_PHYSET);
    v &= (uint16_t)~(RA8P1_PHYSET_HSEB | RA8P1_PHYSET_CLKSEL_MASK);
    v |= RA8P1_PHYSET_CLKSEL(USBHS_CLKSEL_BOARD);
    TIKU_REG16(RA8P1_USBHS_PHYSET) = v;
    tiku_cpu_ra8p1_delay_us(2U);                 /* manual asks for 1 us */

    /* Power the transceiver up: DIRPD resets to 1, which holds the PHY
     * powered down. */
    TIKU_REG16(RA8P1_USBHS_PHYSET) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_PHYSET) & ~RA8P1_PHYSET_DIRPD);
    tiku_cpu_ra8p1_delay_us(1000U);              /* manual asks for 1 ms */

    /*
     * Release the PLL.  The manual does not guarantee operation if PLLRESET
     * is set back to 1 after being cleared, so down() powers the PHY down
     * with DIRPD and leaves PLLRESET clear.
     */
    TIKU_REG16(RA8P1_USBHS_PHYSET) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_PHYSET) & ~RA8P1_PHYSET_PLLRESET);

    /* Start the UTMI clock, then wait for PLLLOCK. */
    TIKU_REG16(RA8P1_USBHS_LPSTS) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_LPSTS) | RA8P1_LPSTS_SUSPENDM);

    for (spins = 0U; spins < USBHS_PLL_TIMEOUT_US; spins++) {
        if ((TIKU_REG16(RA8P1_USBHS_PLLSTA) & RA8P1_PLLSTA_PLLLOCK) != 0U) {
            break;
        }
        tiku_cpu_ra8p1_delay_us(1U);
    }
    if (spins >= USBHS_PLL_TIMEOUT_US) {
        /* No lock: the PHY is powered down again and the controller stays
         * disabled. */
        TIKU_REG16(RA8P1_USBHS_PHYSET) =
            (uint16_t)(TIKU_REG16(RA8P1_USBHS_PHYSET) | RA8P1_PHYSET_DIRPD);
        return TIKU_RA8P1_USBHS_ERR_CLOCK;
    }

    /*
     * Enable the controller, still detached.  SYSCFG's own note allows this
     * only once CLKSEL is set and PLLLOCK reads 1, which is what the wait
     * above establishes.
     */
    TIKU_REG16(RA8P1_USBHS_SYSCFG) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_SYSCFG) | RA8P1_SYSCFG_USBE);

    /*
     * EP0 runs from the interrupt, since the host sets enumeration's
     * deadlines.  Only the control-stage, device-state and DCP buffer-empty
     * interrupts are enabled; the bulk pipes run in process context, where a
     * SCSI command may take milliseconds.
     */
    TIKU_REG16(RA8P1_USBHS_BEMPENB) = 0U;
    TIKU_REG16(RA8P1_USBHS_INTENB0) =
        (uint16_t)(RA8P1_INTENB0_CTRE | RA8P1_INTENB0_DVSE |
                   RA8P1_INTENB0_BEMPE);

    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_USBHS)) =
        RA8P1_EVENT_USBHS_USBIR;
    /* Read back before unmasking: the write crosses into the ICU's clock
     * domain, and an NVIC enable that overtakes it arms the slot while it
     * still carries its previous event. */
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_USBHS));
    TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_USBHS / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_USBHS % 32U));

    usbhs_up = 1U;
    return TIKU_RA8P1_USBHS_OK;
}

/*---------------------------------------------------------------------------*/
/* EP0 ENUMERATION                                                           */
/*---------------------------------------------------------------------------*/
/*
 * The controller decodes the setup packet itself: on receiving one it sets
 * INTSTS0.VALID, forces DCPCTR.PID to NAK, clears CCPL, and publishes
 * bmRequestType/bRequest/wValue/wIndex/wLength in four registers.
 *
 * Software clears VALID (while it is set, the pipe cannot be set to BUF and
 * the transfer cannot be ended), moves any data through the CFIFO port, then
 * sets CCPL with PID at BUF; the hardware runs the status stage.
 *
 * The hardware answers a well-formed SET_ADDRESS, and software answers every
 * other request.  For SET_ADDRESS this code only clears VALID: setting CCPL
 * as well would complete the transfer twice.
 */

/* 1209:0001 is the pid.codes ID reserved for prototypes. */
#define USBD_VID          0x1209U
#define USBD_PID          0x0001U

#define USBD_DESC_DEVICE      1U
#define USBD_DESC_CONFIG      2U
#define USBD_DESC_STRING      3U
#define USBD_DESC_QUALIFIER   6U
#define USBD_DESC_OTHERSPEED  7U

#define USBD_REQ_GET_STATUS        0x00U
#define USBD_REQ_CLEAR_FEATURE     0x01U
#define USBD_REQ_SET_FEATURE       0x03U
#define USBD_REQ_SET_ADDRESS       0x05U
#define USBD_REQ_GET_DESCRIPTOR    0x06U
#define USBD_REQ_GET_CONFIGURATION 0x08U
#define USBD_REQ_SET_CONFIGURATION 0x09U
#define USBD_REQ_GET_INTERFACE     0x0AU
#define USBD_REQ_SET_INTERFACE     0x0BU

/*
 * Bulk-Only Transport's two interface requests.  MSC_RESET is the host's way
 * out of a wedged transfer: a device that stalls it leaves the host
 * reissuing the failed command indefinitely.
 */
#define USBD_REQ_MSC_GET_MAX_LUN   0xFEU
#define USBD_REQ_MSC_RESET         0xFFU

/* Standard feature selector 0 is ENDPOINT_HALT; recipient 2 is an endpoint. */
#define USBD_FEATURE_EP_HALT       0x00U
#define USBD_RECIP_ENDPOINT        0x02U

#define USBD_EP0_MAXPACKET  64U

#define USBD_CLASS_MSC      0x08U
#define USBD_SUBCLASS_SCSI  0x06U
#define USBD_PROTO_BOT      0x50U

static const uint8_t desc_device[18] = {
    18U, USBD_DESC_DEVICE,
    0x00U, 0x02U,               /* bcdUSB 2.00                              */
    0x00U, 0x00U, 0x00U,        /* class/subclass/protocol: at the interface */
    USBD_EP0_MAXPACKET,
    (uint8_t)USBD_VID, (uint8_t)(USBD_VID >> 8),
    (uint8_t)USBD_PID, (uint8_t)(USBD_PID >> 8),
    0x06U, 0x00U,               /* bcdDevice 0.06                           */
    1U, 2U, 3U,                 /* manufacturer, product, serial            */
    1U                          /* one configuration                        */
};

/*
 * Mass storage, bulk-only transport, SCSI transparent command set.  The two
 * bulk endpoints carry every command and every byte of data; the control
 * pipe carries only enumeration and the two class requests.
 */
static const uint8_t desc_config[32] = {
    9U, USBD_DESC_CONFIG, 32U, 0U, 1U, 1U, 0U,
    0xC0U,                      /* self-powered                             */
    50U,                        /* 100 mA                                   */
    9U, 4U, 0U, 0U, 2U, USBD_CLASS_MSC, USBD_SUBCLASS_SCSI,
    USBD_PROTO_BOT, 0U,
    7U, 5U, 0x81U, 0x02U, 0x00U, 0x02U, 0U,   /* EP1 IN  bulk 512           */
    7U, 5U, 0x02U, 0x02U, 0x00U, 0x02U, 0U    /* EP2 OUT bulk 512           */
};

/* A high-speed capable device answers these two; a device that stalls them
 * enumerates at full speed. */
static const uint8_t desc_qualifier[10] = {
    10U, USBD_DESC_QUALIFIER, 0x00U, 0x02U,
    0x00U, 0x00U, 0x00U, USBD_EP0_MAXPACKET, 1U, 0U
};

static const uint8_t desc_other_speed[32] = {
    9U, USBD_DESC_OTHERSPEED, 32U, 0U, 1U, 1U, 0U, 0xC0U, 50U,
    9U, 4U, 0U, 0U, 2U, USBD_CLASS_MSC, USBD_SUBCLASS_SCSI,
    USBD_PROTO_BOT, 0U,
    7U, 5U, 0x81U, 0x02U, 0x40U, 0x00U, 0U,   /* full speed: 64-byte bulk   */
    7U, 5U, 0x02U, 0x02U, 0x40U, 0x00U, 0U
};

static const uint8_t desc_lang[4] = { 4U, USBD_DESC_STRING, 0x09U, 0x04U };

/*
 * What the hardware reported for the first sixteen SETUPs since boot, enough
 * for one enumeration.  Failures on this bus raise no error; the host only
 * asks again.
 */
typedef struct {
    uint16_t req, val, len, ctsq;
    uint16_t dcp_wr, ctr_wr;    /* DCPCTR/CFIFOCTR once the bytes are in   */
    uint16_t spins;             /* FRDY handshake cost, 0xFFFF if unreached */
    uint16_t dcp_pre, dcp_post; /* DCPCTR either side of CCPL              */
    uint8_t  sel_ok, sent_ok;
} ep0_trace_t;

static ep0_trace_t ep0_trace[16];
static uint8_t     ep0_trace_n;
static ep0_trace_t *tr;      /* the entry being filled, or NULL */

/*
 * PIPE1 carries EP1 IN and PIPE2 carries EP2 OUT, as the descriptors say.
 * Each is a 512-byte double-buffered bulk pipe taking (512/64) * 2 = 16
 * blocks of the controller's 8.5 KB buffer, at blocks 8 and 24; block 0x00
 * is left to the DCP and 0x04-0x07 to the interrupt pipes that own them.
 */
#define MSC_PIPE_IN     1U          /* device -> host */
#define MSC_PIPE_OUT    2U          /* host -> device */
#define MSC_MPS         512U
#define MSC_BUFSIZE     ((MSC_MPS / 64U) - 1U)
#define MSC_BUFNMB_IN   0x08U
#define MSC_BUFNMB_OUT  0x18U

static uint8_t  ep0_buf[80];
/* Recovery counters, raised from EP0: the class reset arrives on the
 * control pipe. */
static uint32_t n_msc_reset, n_csw_fail, n_ep_halt, n_ep_unhalt;
static volatile uint32_t n_irq, n_dvst;
static uint8_t  last_ops[4];
static uint8_t  last_op_i;
static void pipe_config(uint8_t pipe, uint8_t epnum, int dir_in,
                        uint8_t bufnmb);
static void pipe_pid(uint8_t pipe, uint16_t pid);
static uint8_t  usbhs_config;
static uint32_t n_setup, n_stall;
static uint16_t last_req;

/** @brief Render up to 36 ASCII characters as a UTF-16LE string descriptor. */
static uint16_t string_desc(const char *s, uint8_t *out)
{
    uint16_t n = 0U;

    out[1] = USBD_DESC_STRING;
    while (*s != '\0' && n < 36U) {
        out[2U + (n * 2U)] = (uint8_t)*s++;
        out[3U + (n * 2U)] = 0U;
        n++;
    }
    out[0] = (uint8_t)(2U + (n * 2U));
    return out[0];
}

/** @brief Serial-number descriptor: two unique-ID words as 16 hex digits. */
static uint16_t serial_desc(uint8_t *out)
{
    static const char hex[] = "0123456789ABCDEF";
    char t[17];
    unsigned i;
    uint32_t w0 = TIKU_REG32(RA8P1_UIDR(0));
    uint32_t w1 = TIKU_REG32(RA8P1_UIDR(1));

    for (i = 0U; i < 8U; i++) {
        t[i] = hex[(w0 >> (28U - (4U * i))) & 0xFU];
        t[8U + i] = hex[(w1 >> (28U - (4U * i))) & 0xFU];
    }
    t[16] = '\0';
    return string_desc(t, out);
}

/**
 * @brief Point the CFIFO port at the control pipe and wait for FRDY.
 *
 * @param writing  Non-zero to select the write direction (ISEL)
 * @return 1 when the port is ready, 0 on timeout
 */
static int dcp_select(int writing)
{
    const uint16_t want =
        (uint16_t)(RA8P1_CFIFOSEL_CURPIPE(0U) | RA8P1_CFIFOSEL_MBW_8 |
                   (writing ? RA8P1_CFIFOSEL_ISEL : 0U));
    const uint16_t keep =
        (uint16_t)(RA8P1_CFIFOSEL_ISEL | RA8P1_CFIFOSEL_CURPIPE(0xFU));
    uint32_t spins;

    /*
     * The selection is written, then confirmed by reading it back.  It
     * crosses into the USB clock domain, and FRDY reports for the selection
     * in effect: read straight after the write, FRDY can belong to the
     * previous pipe, and the data written next lands there.
     */
    for (spins = 0U; spins < 1000U; spins++) {
        TIKU_REG16(RA8P1_USBHS_CFIFOSEL) = want;
        if ((TIKU_REG16(RA8P1_USBHS_CFIFOSEL) & keep) == (want & keep)) {
            break;
        }
    }
    if (spins >= 1000U) {
        return 0;
    }

    /*
     * FRDY now reports for the selected pipe.  The bound is short: this runs
     * in the ISR with the buffer already free, so FRDY is a clock-domain
     * handshake, not a wait on the host, and a timeout stalls the request.
     */
    for (spins = 0U; spins < 2000U; spins++) {
        if ((TIKU_REG16(RA8P1_USBHS_CFIFOCTR) & RA8P1_CFIFOCTR_FRDY) != 0U) {
            if (tr != NULL) {
                tr->spins = (uint16_t)spins;
            }
            return 1;
        }
    }
    return 0;
}

/** @brief Set the DCP's response PID, keeping DCPCTR's other bits. */
static void dcp_pid(uint16_t pid)
{
    TIKU_REG16(RA8P1_USBHS_DCPCTR) =
        (uint16_t)((TIKU_REG16(RA8P1_USBHS_DCPCTR) &
                    ~(uint16_t)RA8P1_DCPCTR_PID_MASK) | pid);
}

/** @brief Refuse the request with a STALL handshake. */
static void ep0_stall(void)
{
    dcp_pid(RA8P1_DCPCTR_PID_STALL);
    n_stall++;
}

/** @brief Finish a control transfer that has no data stage. */
static void ep0_ack(void)
{
    dcp_pid(RA8P1_DCPCTR_PID_BUF);
    TIKU_REG16(RA8P1_USBHS_DCPCTR) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_DCPCTR) | RA8P1_DCPCTR_CCPL);
}

/*
 * The data stage runs in the ISR and does not block: a queued packet waits
 * for the host's next IN token.  ep0_send() pushes one packet and returns,
 * and each buffer-empty interrupt pushes the next from this cursor, so the
 * caller's buffer must outlive the transfer.  Every caller passes a const
 * descriptor or the static ep0_buf.
 */
static const uint8_t *ep0_tx_p;
static uint16_t       ep0_tx_left;
static uint8_t        ep0_tx_busy;

/** @brief Put one packet in the DCP buffer and hand it to the controller. */
static void ep0_tx_push(void)
{
    uint16_t n, i;

    if (!dcp_select(1)) {
        ep0_stall();
        ep0_tx_busy = 0U;
        return;
    }
    n = (ep0_tx_left > USBD_EP0_MAXPACKET) ? USBD_EP0_MAXPACKET : ep0_tx_left;
    for (i = 0U; i < n; i++) {
        TIKU_REG8(RA8P1_USBHS_CFIFOHH) = ep0_tx_p[i];
    }
    ep0_tx_p    += n;
    ep0_tx_left  = (uint16_t)(ep0_tx_left - n);

    /* A packet shorter than the maximum must be marked valid to be sent; a
     * full one commits by itself when the buffer fills. */
    if (n < USBD_EP0_MAXPACKET) {
        TIKU_REG16(RA8P1_USBHS_CFIFOCTR) =
            (uint16_t)(TIKU_REG16(RA8P1_USBHS_CFIFOCTR) |
                       RA8P1_CFIFOCTR_BVAL);
    }
    dcp_pid(RA8P1_DCPCTR_PID_BUF);

    if (tr != NULL) {
        tr->dcp_wr = TIKU_REG16(RA8P1_USBHS_DCPCTR);
        tr->ctr_wr = TIKU_REG16(RA8P1_USBHS_CFIFOCTR);
    }
}

/**
 * @brief Begin a control-IN data stage; the ISR carries it to completion.
 *
 * @param p    bytes to send; must outlive the transfer
 * @param len  how many exist
 * @param wlen how many the host asked for
 */
static void ep0_send(const uint8_t *p, uint16_t len, uint16_t wlen)
{
    /* At most wLength bytes: a host that asks for eight bytes of the device
     * descriptor is sizing EP0 and reads no more. */
    if (len > wlen) {
        len = wlen;
    }
    ep0_tx_p    = p;
    ep0_tx_left = len;
    ep0_tx_busy = 1U;

    /* Buffer-empty marks a packet as sent. */
    TIKU_REG16(RA8P1_USBHS_BEMPSTS) = (uint16_t)~1U;
    TIKU_REG16(RA8P1_USBHS_BEMPENB) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_BEMPENB) | 1U);
    ep0_tx_push();
}

/** @brief A DCP packet has gone: send the next, or finish the transfer. */
static void ep0_tx_done(void)
{
    if (!ep0_tx_busy) {
        return;
    }
    if (ep0_tx_left != 0U) {
        ep0_tx_push();
        return;
    }
    ep0_tx_busy = 0U;
    TIKU_REG16(RA8P1_USBHS_BEMPENB) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_BEMPENB) & ~1U);

    /* Data delivered; the hardware runs the status stage.  CCPL takes
     * effect only while PID is BUF, so both are written together. */
    if (tr != NULL) {
        tr->dcp_pre = TIKU_REG16(RA8P1_USBHS_DCPCTR);
    }
    TIKU_REG16(RA8P1_USBHS_DCPCTR) =
        (uint16_t)((TIKU_REG16(RA8P1_USBHS_DCPCTR) &
                    ~(uint16_t)RA8P1_DCPCTR_PID_MASK) |
                   RA8P1_DCPCTR_PID_BUF | RA8P1_DCPCTR_CCPL);
    if (tr != NULL) {
        tr->dcp_post = TIKU_REG16(RA8P1_USBHS_DCPCTR);
    }
}

/** @brief Answer GET_DESCRIPTOR; unknown types and string indexes stall. */
static void ep0_get_descriptor(uint8_t type, uint8_t idx, uint16_t wlen)
{
    uint16_t n;

    switch (type) {
    case USBD_DESC_DEVICE:
        ep0_send(desc_device, sizeof(desc_device), wlen);
        return;
    case USBD_DESC_CONFIG:
        ep0_send(desc_config, sizeof(desc_config), wlen);
        return;
    case USBD_DESC_QUALIFIER:
        ep0_send(desc_qualifier, sizeof(desc_qualifier), wlen);
        return;
    case USBD_DESC_OTHERSPEED:
        ep0_send(desc_other_speed, sizeof(desc_other_speed), wlen);
        return;
    case USBD_DESC_STRING:
        if (idx == 0U) {
            ep0_send(desc_lang, sizeof(desc_lang), wlen);
        } else if (idx == 1U) {
            n = string_desc("TikuOS", ep0_buf);
            ep0_send(ep0_buf, n, wlen);
        } else if (idx == 2U) {
            n = string_desc("EK-RA8P1 USB-HS", ep0_buf);
            ep0_send(ep0_buf, n, wlen);
        } else if (idx == 3U) {
            n = serial_desc(ep0_buf);
            ep0_send(ep0_buf, n, wlen);
        } else {
            ep0_stall();
        }
        return;
    default:
        ep0_stall();
        return;
    }
}

/** @brief Decode and answer the request a control-stage interrupt raised. */
static void ep0_on_setup(uint16_t sts)
{
    uint16_t ctsq, req, val, idx, len;
    uint8_t type, request;

    /*
     * The trigger is the stage transition, CTRT, not VALID.  VALID sets when
     * the setup packet arrives, but the request registers are stored "when
     * the USBHS receives a data packet following a setup packet", so they can
     * be read before they hold the request.  By CTRT, CTSQ names the stage
     * and the four request registers are filled.
     */
    ctsq = (uint16_t)(sts & RA8P1_INTSTS0_CTSQ_MASK);

    if (ctsq == 6U) {
        /* CTSQ 6 is a control-sequence error; the defined answer is a
         * stall. */
        ep0_stall();
        return;
    }
    /* 1 = control read data, 3 = control write data, 5 = no-data status.
     * Any other stage is the hardware moving through a transfer already
     * answered, and carries no new request. */
    if (ctsq != 1U && ctsq != 3U && ctsq != 5U) {
        return;
    }

    req     = TIKU_REG16(RA8P1_USBHS_USBREQ);
    val     = TIKU_REG16(RA8P1_USBHS_USBVAL);
    idx     = TIKU_REG16(RA8P1_USBHS_USBINDX);
    len     = TIKU_REG16(RA8P1_USBHS_USBLENG);
    type    = (uint8_t)(req & 0xFFU);          /* bmRequestType */
    request = (uint8_t)(req >> 8);             /* bRequest      */
    n_setup++;
    last_req = (uint16_t)((request << 8) | type);

    tr = (ep0_trace_n < 16U) ? &ep0_trace[ep0_trace_n++] : NULL;
    if (tr != NULL) {
        tr->req      = req;
        tr->val      = val;
        tr->len      = len;
        tr->ctsq     = ctsq;
        tr->sel_ok   = 0xFFU;
        tr->sent_ok  = 0xFFU;
        tr->dcp_wr   = 0U;
        tr->ctr_wr   = 0U;
        tr->spins    = 0xFFFFU;
        tr->dcp_pre  = 0U;
        tr->dcp_post = 0U;
    }

    /* Clear VALID before responding: while it is set the pipe cannot be put
     * into BUF and the transfer cannot be ended. */
    TIKU_REG16(RA8P1_USBHS_INTSTS0) = (uint16_t)~RA8P1_INTSTS0_VALID;

    /* Answered in hardware; setting CCPL here would complete it twice. */
    if (request == USBD_REQ_SET_ADDRESS && type == 0x00U) {
        return;
    }

    switch (request) {
    case USBD_REQ_GET_DESCRIPTOR:
        ep0_get_descriptor((uint8_t)(val >> 8), (uint8_t)(val & 0xFFU), len);
        break;

    case USBD_REQ_SET_CONFIGURATION:
        usbhs_config = (uint8_t)(val & 0xFFU);
        if (usbhs_config != 0U) {
            /* Endpoints only exist once a configuration is selected, and the
             * host expects them freshly reset when it is. */
            pipe_config(MSC_PIPE_IN, 1U, 1, MSC_BUFNMB_IN);
            pipe_config(MSC_PIPE_OUT, 2U, 0, MSC_BUFNMB_OUT);
            pipe_pid(MSC_PIPE_OUT, RA8P1_PIPECTR_PID_BUF);
        }
        ep0_ack();
        break;

    case USBD_REQ_GET_CONFIGURATION:
        ep0_buf[0] = usbhs_config;
        ep0_send(ep0_buf, 1U, len);
        break;

    case USBD_REQ_GET_STATUS:
        /* Device status, self-powered with no remote wakeup; the same two
         * bytes go to every recipient. */
        ep0_buf[0] = 0x01U;
        ep0_buf[1] = 0x00U;
        ep0_send(ep0_buf, 2U, len);
        break;

    case USBD_REQ_GET_INTERFACE:
        ep0_buf[0] = 0U;
        ep0_send(ep0_buf, 1U, len);
        break;

    case USBD_REQ_CLEAR_FEATURE:
    case USBD_REQ_SET_FEATURE:
        /*
         * Bulk-only recovery: after a stalled command the host clears the
         * halt on each pipe before retrying.  Clearing it also resets the
         * data toggle to DATA0 with SQCLR, since the host restarts its own at
         * DATA0; a pipe whose toggle is out of step ignores every packet.
         */
        if ((type & 0x1FU) == USBD_RECIP_ENDPOINT &&
            val == USBD_FEATURE_EP_HALT) {
            uint8_t pipe = ((idx & 0x0FU) == 1U) ? MSC_PIPE_IN
                                                 : MSC_PIPE_OUT;

            if (request == USBD_REQ_CLEAR_FEATURE) {
                pipe_pid(pipe, RA8P1_PIPECTR_PID_NAK);
                TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) =
                    (uint16_t)(RA8P1_PIPECTR_SQCLR | RA8P1_PIPECTR_PID_NAK);
                pipe_pid(pipe, RA8P1_PIPECTR_PID_BUF);
                n_ep_unhalt++;
            } else {
                pipe_pid(pipe, RA8P1_PIPECTR_PID_STALL);
                n_ep_halt++;
            }
        }
        ep0_ack();
        break;

    case USBD_REQ_SET_INTERFACE:
        ep0_ack();
        break;

    case USBD_REQ_MSC_GET_MAX_LUN:
        /* Class request, device-to-host: one LUN, so the highest index is 0. */
        if (type == 0xA1U) {
            ep0_buf[0] = 0U;
            ep0_send(ep0_buf, 1U, len);
        } else {
            ep0_stall();
        }
        break;

    case USBD_REQ_MSC_RESET:
        if (type == 0x21U) {
            /* Both pipes return to their SET_CONFIGURATION state, buffers
             * emptied and data toggles cleared; the host resumes at DATA0. */
            pipe_config(MSC_PIPE_IN, 1U, 1, MSC_BUFNMB_IN);
            pipe_config(MSC_PIPE_OUT, 2U, 0, MSC_BUFNMB_OUT);
            pipe_pid(MSC_PIPE_OUT, RA8P1_PIPECTR_PID_BUF);
            n_msc_reset++;
            ep0_ack();
        } else {
            ep0_stall();
        }
        break;

    default:
        ep0_stall();
        break;
    }
}

/*
 * USBHS_USBIR aggregates the VBUS, resume, frame, device-state, control-stage
 * and buffer interrupts, so one edge can stand for several causes, and a
 * cause left pending raises no new edge.  The handler drains INTSTS0 for up
 * to 16 passes, so a source that cannot be cleared costs a bounded burst.
 */
void tiku_ra8p1_usbhs_handler(void)
{
    unsigned guard;

    for (guard = 0U; guard < 16U; guard++) {
        uint16_t sts = TIKU_REG16(RA8P1_USBHS_INTSTS0);

        if ((sts & RA8P1_INTSTS0_DVST) != 0U) {
            /* Write-0-to-clear: zero the flag being cleared, ones elsewhere. */
            TIKU_REG16(RA8P1_USBHS_INTSTS0) = (uint16_t)~RA8P1_INTSTS0_DVST;
            n_dvst++;
            /* A bus reset abandons any EP0 transfer and the configuration;
             * the cursor is cleared so a new request is not answered with
             * the tail of the old one. */
            if (((sts & RA8P1_INTSTS0_DVSQ_MASK) >>
                 RA8P1_INTSTS0_DVSQ_SHIFT) <= 1U) {
                ep0_tx_busy = 0U;
                ep0_tx_left = 0U;
                usbhs_config = 0U;
                TIKU_REG16(RA8P1_USBHS_BEMPENB) =
                    (uint16_t)(TIKU_REG16(RA8P1_USBHS_BEMPENB) & ~1U);
            }
            continue;
        }
        if (ep0_tx_busy &&
            (TIKU_REG16(RA8P1_USBHS_BEMPSTS) & 1U) != 0U) {
            TIKU_REG16(RA8P1_USBHS_BEMPSTS) = (uint16_t)~1U;
            ep0_tx_done();
            continue;
        }
        if ((sts & RA8P1_INTSTS0_CTRT) != 0U) {
            TIKU_REG16(RA8P1_USBHS_INTSTS0) = (uint16_t)~RA8P1_INTSTS0_CTRT;
            ep0_on_setup(sts);
            continue;
        }
        break;
    }
    n_irq++;

    /* Clear the ICU latch and read it back before returning: a clear that
     * has not retired re-pends the NVIC on an event already handled. */
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_USBHS)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_USBHS));
    __asm__ volatile ("dsb" ::: "memory");
}

uint8_t tiku_ra8p1_usbhs_address(void)
{
    if (!usbhs_up) {
        return 0U;
    }
    return (uint8_t)(TIKU_REG16(RA8P1_USBHS_USBADDR) & 0x7FU);
}

uint8_t tiku_ra8p1_usbhs_configured(void)
{
    return usbhs_config;
}

unsigned tiku_ra8p1_usbhs_ep0_trace(unsigned i, uint16_t *out9)
{
    if (i >= ep0_trace_n || out9 == NULL) {
        return 0U;
    }
    out9[0] = ep0_trace[i].req;
    out9[1] = ep0_trace[i].val;
    out9[2] = ep0_trace[i].len;
    out9[3] = ep0_trace[i].ctsq;
    out9[4] = ep0_trace[i].dcp_wr;
    out9[5] = ep0_trace[i].ctr_wr;
    out9[6] = ep0_trace[i].spins;
    out9[7] = ep0_trace[i].dcp_pre;
    out9[8] = ep0_trace[i].dcp_post;
    return ep0_trace_n;
}

void tiku_ra8p1_usbhs_ep0_stats(uint32_t *setup, uint32_t *stall,
                                uint16_t *last)
{
    if (setup != NULL) { *setup = n_setup; }
    if (stall != NULL) { *stall = n_stall; }
    if (last  != NULL) { *last  = last_req; }
}

/*---------------------------------------------------------------------------*/
/* BULK PIPES AND MASS STORAGE                                               */
/*---------------------------------------------------------------------------*/
/*
 * Bulk data moves through D0FIFO, so bulk and control traffic never contend
 * for one port, 32 bits at a time: that width is valid at the port's own
 * offset, where 8-bit access is not (UM Table 38.8).
 */
/* The staging disk is the 64 MB SDRAM window, where a model sits before it
 * is written to flash. */
#define MSC_DISK_BASE   0x68000000UL
#define MSC_DISK_BYTES  (64UL * 1024UL * 1024UL)

static tiku_usbd_msc_t msc_medium = {
    (uint32_t)(MSC_DISK_BYTES / TIKU_USBD_MSC_BLOCK), "SDRAM Stage", 0U, 0U
};
static uint8_t  msc_reply[TIKU_USBD_MSC_REPLY_MAX];
static uint32_t n_cbw, n_rd, n_wr, n_bad;
static uint32_t n_pkt_out, n_stall_out, n_refused;
static uint32_t last_wr_lba, last_wr_blocks;
static uint16_t cfg_in, buf_in, maxp_in, cfg_out, buf_out, maxp_out;
static uint16_t last_dtln;

/** @brief Configure one bulk pipe; it is left answering NAK. */
static void pipe_config(uint8_t pipe, uint8_t epnum, int dir_in,
                        uint8_t bufnmb)
{
    TIKU_REG16(RA8P1_USBHS_PIPESEL) = (uint16_t)pipe;

    /* The registers below may be written only while the pipe answers NAK. */
    TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) = RA8P1_PIPECTR_PID_NAK;

    TIKU_REG16(RA8P1_USBHS_PIPECFG) =
        (uint16_t)(RA8P1_PIPECFG_TYPE_BULK | RA8P1_PIPECFG_DBLB |
                   (dir_in ? RA8P1_PIPECFG_DIR_IN : 0U) |
                   RA8P1_PIPECFG_EPNUM(epnum));
    TIKU_REG16(RA8P1_USBHS_PIPEBUF) =
        (uint16_t)(RA8P1_PIPEBUF_BUFSIZE(MSC_BUFSIZE) |
                   RA8P1_PIPEBUF_BUFNMB(bufnmb));
    TIKU_REG16(RA8P1_USBHS_PIPEMAXP) = RA8P1_PIPEMAXP_MXPS(MSC_MPS);

    /* Toggle ACLRM to empty both buffers, and reset the data toggle: the
     * host restarts every pipe at DATA0 after SET_CONFIGURATION. */
    TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) =
        (uint16_t)(RA8P1_PIPECTR_ACLRM | RA8P1_PIPECTR_PID_NAK);
    TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) = RA8P1_PIPECTR_PID_NAK;
    TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) =
        (uint16_t)(RA8P1_PIPECTR_SQCLR | RA8P1_PIPECTR_PID_NAK);

    /* The geometry is read back while the pipe is selected, for
     * tiku_ra8p1_usbhs_pipe_regs(): a pipe whose MXPS did not take still
     * runs, at 64 bytes a packet against the descriptor's 512. */
    if (pipe == MSC_PIPE_IN) {
        cfg_in  = TIKU_REG16(RA8P1_USBHS_PIPECFG);
        buf_in  = TIKU_REG16(RA8P1_USBHS_PIPEBUF);
        maxp_in = TIKU_REG16(RA8P1_USBHS_PIPEMAXP);
    } else {
        cfg_out  = TIKU_REG16(RA8P1_USBHS_PIPECFG);
        buf_out  = TIKU_REG16(RA8P1_USBHS_PIPEBUF);
        maxp_out = TIKU_REG16(RA8P1_USBHS_PIPEMAXP);
    }

    TIKU_REG16(RA8P1_USBHS_PIPESEL) = 0U;
}

/** @brief Set a pipe's response PID, keeping PIPECTR's other bits. */
static void pipe_pid(uint8_t pipe, uint16_t pid)
{
    TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) =
        (uint16_t)((TIKU_REG16(RA8P1_USBHS_PIPECTR(pipe)) &
                    ~(uint16_t)RA8P1_PIPECTR_PID_MASK) | pid);
}

/**
 * @brief Point D0FIFO at @p pipe, confirm the selection and wait for FRDY.
 *
 * @return 1 when the port is ready, 0 on timeout
 */
static int d0_select(uint8_t pipe)
{
    const uint16_t want =
        (uint16_t)(RA8P1_CFIFOSEL_CURPIPE(pipe) | RA8P1_CFIFOSEL_MBW_32);
    uint32_t spins;

    for (spins = 0U; spins < 1000U; spins++) {
        TIKU_REG16(RA8P1_USBHS_D0FIFOSEL) = want;
        if ((TIKU_REG16(RA8P1_USBHS_D0FIFOSEL) & 0xFU) == (uint16_t)pipe) {
            break;
        }
    }
    if (spins >= 1000U) {
        return 0;
    }
    for (spins = 0U; spins < 200000U; spins++) {
        if ((TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) & RA8P1_CFIFOCTR_FRDY) != 0U) {
            return 1;
        }
    }
    return 0;
}

/** @brief Non-zero when the OUT pipe has a packet waiting. */
static int pipe_out_ready(void)
{
    return ((TIKU_REG16(RA8P1_USBHS_PIPECTR(MSC_PIPE_OUT)) &
             RA8P1_PIPECTR_BSTS) != 0U) ? 1 : 0;
}

/**
 * @brief Read one packet from the OUT pipe.
 *
 * @param dst destination
 * @param cap room available
 * @return bytes taken
 */
static uint32_t pipe_read(uint8_t *dst, uint32_t cap)
{
    uint32_t avail, n, i;

    if (!d0_select(MSC_PIPE_OUT)) {
        return 0U;
    }
    avail = (uint32_t)(TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) &
                       RA8P1_CFIFOCTR_DTLN_MASK);

    /*
     * A zero-length packet cannot be read, only cleared with BCLR, which
     * hands its plane back.
     */
    if (avail == 0U) {
        TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) = RA8P1_CFIFOCTR_BCLR;
        return 0U;
    }

    last_dtln = (uint16_t)avail;
    n = (avail > cap) ? cap : avail;
    for (i = 0U; (i + 4U) <= n; i += 4U) {
        uint32_t w = TIKU_REG32(RA8P1_USBHS_D0FIFO);
        dst[i]     = (uint8_t)w;
        dst[i + 1] = (uint8_t)(w >> 8);
        dst[i + 2] = (uint8_t)(w >> 16);
        dst[i + 3] = (uint8_t)(w >> 24);
    }
    if (i < n) {
        uint32_t w = TIKU_REG32(RA8P1_USBHS_D0FIFO);
        while (i < n) {
            dst[i] = (uint8_t)w;
            w >>= 8;
            i++;
        }
    }

    /*
     * Reading all of a plane releases it: with RCNT = 0 the controller holds
     * DTLN "until the CPU has read all of the received data in the FIFO
     * buffer (or until it has read a single plane in double buffer mode)".
     * A BCLR after a complete read discards the next plane, under double
     * buffering a packet already received, so BCLR is issued only to discard
     * what does not fit.
     */
    if (n < avail) {
        TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) = RA8P1_CFIFOCTR_BCLR;
    }
    return n;
}

/**
 * @brief Send @p len bytes on the IN pipe, in maximum-sized packets.
 *
 * @param src bytes to send
 * @param len how many
 * @return 1 on success, 0 if the host stopped collecting
 */
static int pipe_write(const uint8_t *src, uint32_t len)
{
    uint32_t sent = 0U;

    pipe_pid(MSC_PIPE_IN, RA8P1_PIPECTR_PID_BUF);

    do {
        uint32_t n, i, spins;

        if (!d0_select(MSC_PIPE_IN)) {
            return 0;
        }
        n = ((len - sent) > MSC_MPS) ? MSC_MPS : (len - sent);

        for (i = 0U; (i + 4U) <= n; i += 4U) {
            TIKU_REG32(RA8P1_USBHS_D0FIFO) =
                (uint32_t)src[sent + i] |
                ((uint32_t)src[sent + i + 1] << 8) |
                ((uint32_t)src[sent + i + 2] << 16) |
                ((uint32_t)src[sent + i + 3] << 24);
        }
        for (; i < n; i++) {
            TIKU_REG8(RA8P1_USBHS_D0FIFO + 3UL) = src[sent + i];
        }
        /* Short packets need marking valid; a full buffer commits itself. */
        if (n < MSC_MPS) {
            TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) =
                (uint16_t)(TIKU_REG16(RA8P1_USBHS_D0FIFOCTR) |
                           RA8P1_CFIFOCTR_BVAL);
        }
        sent += n;

        /* Wait for room again before refilling.  Double buffering means this
         * usually returns at once; the bound is for a host that stopped. */
        for (spins = 0U; spins < 4000000U; spins++) {
            if ((TIKU_REG16(RA8P1_USBHS_PIPECTR(MSC_PIPE_IN)) &
                 RA8P1_PIPECTR_BSTS) != 0U) {
                break;
            }
        }
        if (spins >= 4000000U) {
            return 0;
        }
    } while (sent < len);

    return 1;
}

/** @brief Stream a READ(10) out of the staging disk. */
static int msc_send_blocks(uint32_t lba, uint32_t bytes)
{
    const uint8_t *p = (const uint8_t *)(MSC_DISK_BASE +
                                         (lba * TIKU_USBD_MSC_BLOCK));
    return pipe_write(p, bytes);
}

/** @brief Take a WRITE(10) into the staging disk, a packet at a time. */
static int msc_recv_blocks(uint32_t lba, uint32_t bytes)
{
    uint8_t *p = (uint8_t *)(MSC_DISK_BASE + (lba * TIKU_USBD_MSC_BLOCK));
    uint32_t got = 0U;

    pipe_pid(MSC_PIPE_OUT, RA8P1_PIPECTR_PID_BUF);
    while (got < bytes) {
        uint32_t spins, n;

        for (spins = 0U; spins < 4000000U; spins++) {
            if (pipe_out_ready()) {
                break;
            }
        }
        if (spins >= 4000000U) {
            n_stall_out++;
            return 0;
        }
        n = pipe_read(&p[got], bytes - got);
        /*
         * A ready pipe that yields no data ends the command as failed;
         * looping on it would hang the CPU.
         */
        if (n == 0U) {
            n_stall_out++;
            return 0;
        }
        got += n;
        n_pkt_out++;
    }
    return 1;
}

void tiku_ra8p1_usbhs_msc_poll(void)
{
    uint8_t raw[TIKU_USBD_MSC_CBW_LEN];
    uint8_t csw[TIKU_USBD_MSC_CSW_LEN];
    tiku_usbd_msc_cbw_t cbw;
    tiku_usbd_msc_cmd_t cmd;
    uint32_t n;
    int ok = 1;

    if (!usbhs_up || usbhs_config == 0U || !pipe_out_ready()) {
        return;
    }

    n = pipe_read(raw, sizeof(raw));
    if (!tiku_usbd_msc_parse_cbw(raw, (uint16_t)n, &cbw)) {
        n_bad++;
        return;      /* not a wrapper; the next packet resyncs */
    }
    n_cbw++;
    last_ops[last_op_i & 3u] = cbw.cdb[0];
    /*
     * While an import reads the staging window, every command is refused
     * with TIKU_USBD_MSC_SENSE_NOTREADY, which a host retries: a write landing
     * in the window mid-import would be published as part of the model.
     */
    if (tiku_ra8p1_store_busy()) {
        tiku_usbd_msc_fail(&msc_medium, TIKU_USBD_MSC_SENSE_NOTREADY,
                           TIKU_USBD_MSC_ASC_NOT_READY);
        tiku_usbd_msc_build_csw(csw, cbw.tag, cbw.host_len, 1U);
        (void)pipe_write(csw, TIKU_USBD_MSC_CSW_LEN);
        n_refused++;
        return;
    }
    last_op_i++;

    /* SCSI decoding is the kernel's shared mass-storage decoder. */
    tiku_usbd_msc_decode(&msc_medium, &cbw, msc_reply, &cmd);

    switch (cmd.action) {
    case TIKU_USBD_MSC_ACT_READ:
        n_rd++;
        ok = msc_send_blocks(cmd.lba, cmd.bytes);
        break;
    case TIKU_USBD_MSC_ACT_WRITE:
        n_wr++;
        ok = msc_recv_blocks(cmd.lba, cmd.bytes);
        /* Recorded for tiku_ra8p1_usbhs_msc_last_write(). */
        last_wr_lba    = cmd.lba;
        last_wr_blocks = cmd.nblk;
        /* A write to the commit LBA starts an import, which runs from
         * tiku_ra8p1_store_step(); the CSW for this write is still owed to
         * the host. */
        if (cmd.lba == tiku_ra8p1_store_commit_lba()) {
            (void)tiku_ra8p1_store_begin(cmd.lba, cmd.nblk);
        }
        break;
    case TIKU_USBD_MSC_ACT_REPLY:
        ok = pipe_write(msc_reply, cmd.len);
        break;
    default:
        break;
    }
    if (!ok) {
        /*
         * A data phase that did not complete leaves the host waiting for
         * bytes.  The IN pipe is halted, the transport's defined answer, so
         * the host stops waiting and starts recovery.
         */
        n_bad++;
        pipe_pid(MSC_PIPE_IN, RA8P1_PIPECTR_PID_STALL);
    }

    tiku_usbd_msc_build_csw(csw, cbw.tag, cmd.residue, cmd.status);
    if (!pipe_write(csw, TIKU_USBD_MSC_CSW_LEN)) {
        /* n_csw_fail counts commands whose status wrapper was not
         * delivered; the host treats such a command as still running. */
        n_csw_fail++;
    }
}

void tiku_ra8p1_usbhs_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                                uint32_t *bad)
{
    if (cbw != NULL) { *cbw = n_cbw; }
    if (rd  != NULL) { *rd  = n_rd;  }
    if (wr  != NULL) { *wr  = n_wr;  }
    if (bad != NULL) { *bad = n_bad; }
}

void tiku_ra8p1_usbhs_msc_out_stats(uint32_t *pkts, uint32_t *stalls)
{
    if (pkts   != NULL) { *pkts   = n_pkt_out;   }
    if (stalls != NULL) { *stalls = n_stall_out; }
}

void tiku_ra8p1_usbhs_pipe_regs(uint16_t *out7)
{
    if (out7 == NULL) { return; }
    out7[0] = cfg_in;  out7[1] = buf_in;  out7[2] = maxp_in;
    out7[3] = cfg_out; out7[4] = buf_out; out7[5] = maxp_out;
    out7[6] = last_dtln;
}

uint32_t tiku_ra8p1_usbhs_msc_last_write(uint32_t *lba, uint32_t *blocks)
{
    if (lba    != NULL) { *lba    = last_wr_lba;    }
    if (blocks != NULL) { *blocks = last_wr_blocks; }
    return n_wr;
}

uint32_t tiku_ra8p1_usbhs_msc_trace(uint32_t *resets, uint32_t *cswfail)
{
    if (resets  != NULL) { *resets  = n_msc_reset; }
    if (cswfail != NULL) { *cswfail = n_csw_fail;  }
    return ((uint32_t)last_ops[0]) | ((uint32_t)last_ops[1] << 8) |
           ((uint32_t)last_ops[2] << 16) | ((uint32_t)last_ops[3] << 24);
}

uint32_t tiku_ra8p1_usbhs_msc_hash(uint32_t nblocks)
{
    const uint8_t *p = (const uint8_t *)MSC_DISK_BASE;
    uint32_t h = 2166136261u, i, n;

    if (nblocks == 0U || nblocks > msc_medium.blocks) {
        nblocks = msc_medium.blocks;
    }
    n = nblocks * TIKU_USBD_MSC_BLOCK;
    for (i = 0U; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

int tiku_ra8p1_usbhs_attach(int on)
{
    if (!usbhs_up) {
        return TIKU_RA8P1_USBHS_ERR_STATE;
    }
    if (on) {
        TIKU_REG16(RA8P1_USBHS_SYSCFG) =
            (uint16_t)(TIKU_REG16(RA8P1_USBHS_SYSCFG) | RA8P1_SYSCFG_DPRPU);
    } else {
        TIKU_REG16(RA8P1_USBHS_SYSCFG) =
            (uint16_t)(TIKU_REG16(RA8P1_USBHS_SYSCFG) & ~RA8P1_SYSCFG_DPRPU);
    }
    __asm__ volatile ("dsb" ::: "memory");
    return TIKU_RA8P1_USBHS_OK;
}

void tiku_ra8p1_usbhs_down(void)
{
    if (!usbhs_up) {
        return;
    }
    TIKU_REG16(RA8P1_USBHS_SYSCFG) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_SYSCFG) &
                   ~(RA8P1_SYSCFG_DPRPU | RA8P1_SYSCFG_USBE));
    TIKU_REG16(RA8P1_USBHS_LPSTS) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_LPSTS) & ~RA8P1_LPSTS_SUSPENDM);
    TIKU_REG16(RA8P1_USBHS_PHYSET) =
        (uint16_t)(TIKU_REG16(RA8P1_USBHS_PHYSET) | RA8P1_PHYSET_DIRPD);
    __asm__ volatile ("dsb" ::: "memory");
    usbhs_up = 0U;
    usbhs_config = 0U;
}

tiku_ra8p1_usbhs_speed_t tiku_ra8p1_usbhs_speed(void)
{
    uint16_t rhst;

    if (!usbhs_up) {
        return TIKU_RA8P1_USBHS_SPEED_NONE;
    }
    rhst = (uint16_t)(TIKU_REG16(RA8P1_USBHS_DVSTCTR0) &
                      RA8P1_DVSTCTR0_RHST_MASK);
    if (rhst == RA8P1_DVSTCTR0_RHST_HIGH) {
        return TIKU_RA8P1_USBHS_SPEED_HIGH;
    }
    if (rhst == RA8P1_DVSTCTR0_RHST_FULL) {
        return TIKU_RA8P1_USBHS_SPEED_FULL;
    }
    return TIKU_RA8P1_USBHS_SPEED_NONE;
}

tiku_ra8p1_usbhs_devstate_t tiku_ra8p1_usbhs_devstate(void)
{
    uint16_t dvsq;

    if (!usbhs_up) {
        return TIKU_RA8P1_USBHS_DEV_POWERED;
    }
    dvsq = (uint16_t)((TIKU_REG16(RA8P1_USBHS_INTSTS0) &
                       RA8P1_INTSTS0_DVSQ_MASK) >> RA8P1_INTSTS0_DVSQ_SHIFT);
    if (dvsq > 3U) {
        return TIKU_RA8P1_USBHS_DEV_SUSPEND;
    }
    return (tiku_ra8p1_usbhs_devstate_t)dvsq;
}

int tiku_ra8p1_usbhs_pll_locked(void)
{
    if (!usbhs_up) {
        return 0;
    }
    return ((TIKU_REG16(RA8P1_USBHS_PLLSTA) & RA8P1_PLLSTA_PLLLOCK) != 0U)
           ? 1 : 0;
}

int tiku_ra8p1_usbhs_id_high(void)
{
    if (!usbhs_up) {
        return -1;
    }
    return ((TIKU_REG16(RA8P1_USBHS_SYSSTS0) & RA8P1_SYSSTS0_IDMON) != 0U)
           ? 1 : 0;
}

void tiku_ra8p1_usbhs_irq_stats(uint32_t *irqs, uint32_t *dvst)
{
    if (irqs != NULL) { *irqs = n_irq;  }
    if (dvst != NULL) { *dvst = n_dvst; }
}

int tiku_ra8p1_usbhs_up_state(void)
{
    return (int)usbhs_up;
}

void tiku_ra8p1_usbhs_regs(uint16_t *out, unsigned n)
{
    static const uint32_t addr[] = {
        RA8P1_USBHS_SYSCFG,   RA8P1_USBHS_SYSSTS0,
        RA8P1_USBHS_PLLSTA,   RA8P1_USBHS_DVSTCTR0,
        RA8P1_USBHS_PHYSET,   RA8P1_USBHS_INTSTS0,
        RA8P1_USBHS_LPSTS,    RA8P1_USBHS_FRMNUM
    };
    unsigned i;

    if (out == NULL) {
        return;
    }
    for (i = 0U; i < n; i++) {
        if (i >= sizeof(addr) / sizeof(addr[0])) {
            out[i] = 0U;
        } else if (!usbhs_up &&
                   (TIKU_REG32(RA8P1_MSTPCRB) & RA8P1_MSTPB_USBHS) != 0UL) {
            /* A read of a module-stopped USBHS hangs the bus, so 0xDEAD is
             * reported instead. */
            out[i] = 0xDEADU;
        } else {
            out[i] = TIKU_REG16(addr[i]);
        }
    }
}
