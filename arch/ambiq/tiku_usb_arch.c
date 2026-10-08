/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_arch.c - Apollo510 USB device controller: bring-up and enumeration.
 *
 * Bring-up, enumeration, a CDC-ACM console and bulk-only mass storage.  The
 * MUSB registers are accessed at their true widths, never through the CMSIS
 * bitfields on CFG0..CFG2, which pack read-to-clear interrupt status.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_USB_ENABLE + 0)

#include "tiku_usb_arch.h"
#include <services/usb/tiku_usbd_msc.h>
#include "tiku_gpio_arch.h"
#include "tiku_cpu_common.h"
#include "apollo510.h"
#include <kernel/shell/tiku_shell_io.h>   /* the console backend, below     */
#include <kernel/shell/tiku_shell.h>      /* tiku_shell_add_pump()          */
#include <kernel/vfs/tiku_vfs.h>          /* TIKU_VFS_CAP_ALL               */
#include <kernel/cpu/tiku_hang.h>         /* check-in in blocking loops     */
#include "hal/tiku_cpu.h"                 /* dcache maintenance for ADMA    */
#if (TIKU_DRV_BLE_EM9305_ENABLE + 0)
#include "tiku_em9305.h"        /* HS needs the die's 12 MHz -- see below   */
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
#include "tiku_emmc_arch.h"     /* MSC's eMMC backing store                */
#endif

/*---------------------------------------------------------------------------*/
/* REGISTER ACCESS AT TRUE WIDTHS (table 1)                                  */
/*---------------------------------------------------------------------------*/
/*
 * Each macro is one MUSB register at its true width.  CMSIS packs these
 * into wider words, and a 32-bit access there clears read-to-clear status
 * (table 1 in the header).
 */
#define R8(off)    (*(volatile uint8_t  *)(USB_BASE + (off)))
#define R16(off)   (*(volatile uint16_t *)(USB_BASE + (off)))

#define USB_FADDR       R8(0x00)
#define USB_POWER       R8(0x01)
#define USB_INTRTX      R16(0x02)   /* read-to-clear */
#define USB_INTRRX      R16(0x04)   /* read-to-clear */
#define USB_INTRTXE     R16(0x06)
#define USB_INTRRXE     R16(0x08)
#define USB_INTRUSB     R8(0x0A)    /* read-to-clear */
#define USB_INTRUSBE    R8(0x0B)
#define USB_FRAME       R16(0x0C)
#define USB_INDEX       R8(0x0E)
#define USB_TESTMODE    R8(0x0F)
#define USB_TXMAXP      R16(0x10)
#define USB_CSR0        R16(0x12)   /* when INDEX == 0                       */
#define USB_TXCSR       R16(0x12)   /* when INDEX  > 0                       */
#define USB_RXMAXP      R16(0x14)
#define USB_RXCSR       R16(0x16)
#define USB_COUNT0      R16(0x18)   /* when INDEX == 0                       */
#define USB_FIFO(n)     (*(volatile uint32_t *)(USB_BASE + 0x20u + 4u * (n)))

/* POWER (0x01) */
#define POWER_ENSUSPM   (1u << 0)
#define POWER_SUSPENDM  (1u << 1)
#define POWER_RESUME    (1u << 2)
#define POWER_RESET     (1u << 3)
#define POWER_HSMODE    (1u << 4)
#define POWER_HSENAB    (1u << 5)
#define POWER_SOFTCONN  (1u << 6)   /* CMSIS calls this "AMSPECIFIC"         */
#define POWER_ISOUPDATE (1u << 7)

/*
 * The 12 MHz high-speed reference of the Blue board (header table 5), which
 * has no HS crystal.  The EM9305 BLE die in the package supplies it:
 *
 *   GP136  out  asserted to request the die's 12 MHz output
 *               (AM_BSP_GPIO_AP5_12M_CLKREQ)
 *   GP15   in   funcsel 10 (REFCLK_EXT), where it arrives
 *
 * The PHY expects 24 MHz and multiplies by 40; at 12 MHz it multiplies by 20
 * (USBPHY REG14 bit BF55).  A wrong multiplier runs the PHY at the wrong rate
 * and the chirp fails.
 */
/* Only boards whose HS reference is the EM9305 define these pads; a board
 * with its own HS crystal defines neither (see the green EVB header).  A
 * board that declares the EM9305 reference without them fails to compile. */
#if (TIKU_BOARD_HAS_USBHS_CLK_EM9305 + 0)
#if !defined(TIKU_BOARD_USB_PAD_CLKREQ) || !defined(TIKU_BOARD_USB_PAD_REFCLK)
#error "Board declares USBHS_CLK_EM9305 but no TIKU_BOARD_USB_PAD_CLKREQ / \
_REFCLK. The 12 MHz reference has to arrive on some pad -- name it."
#endif
#define TIKU_USB_PAD_CLKREQ   TIKU_BOARD_USB_PAD_CLKREQ
#define TIKU_USB_PAD_REFCLK   TIKU_BOARD_USB_PAD_REFCLK
#endif
#define PAD_FNCSEL_REFCLK      10u
#define TIKU_USB_REFCLK_SETTLE_US 1500u   /* the HAL's stabilisation wait   */

/* INTRUSB / INTRUSBE (0x0A / 0x0B) */
#define INTRUSB_SUSPEND (1u << 0)
#define INTRUSB_RESUME  (1u << 1)
#define INTRUSB_RESET   (1u << 2)
#define INTRUSB_SOF     (1u << 3)

/* CSR0 (INDEX == 0): the EP0 meanings of the shared CSR bits.  CMSIS names
 * each bit for both its EP0 and its EP1-5 role. */
#define CSR0_RXPKTRDY       (1u << 0)
#define CSR0_TXPKTRDY       (1u << 1)
#define CSR0_SENTSTALL      (1u << 2)
#define CSR0_DATAEND        (1u << 3)
#define CSR0_SETUPEND       (1u << 4)
#define CSR0_SENDSTALL      (1u << 5)
#define CSR0_SERVICEDRXPKTRDY (1u << 6)
#define CSR0_SERVICEDSETUPEND (1u << 7)
#define CSR0_FLUSHFIFO      (1u << 8)

/* TXCSR (INDEX > 0) -- the EP1-5 meanings of the same shared CSR bits. */
#define TXCSR_TXPKTRDY      (1u << 0)
#define TXCSR_FIFONOTEMPTY  (1u << 1)
#define TXCSR_UNDERRUN      (1u << 2)
#define TXCSR_FLUSHFIFO     (1u << 3)
#define TXCSR_SENDSTALL     (1u << 4)
#define TXCSR_SENTSTALL     (1u << 5)
#define TXCSR_CLRDATATOG    (1u << 6)
#define TXCSR_DPKTBUFDIS    (1u << 9)
#define TXCSR_MODE          (1u << 13)

/* RXCSR (INDEX > 0) */
#define RXCSR_RXPKTRDY      (1u << 0)
#define RXCSR_FIFOFULL      (1u << 1)
#define RXCSR_OVERRUN       (1u << 2)
#define RXCSR_DATAERROR     (1u << 3)
#define RXCSR_FLUSHFIFO     (1u << 4)
#define RXCSR_SENDSTALL     (1u << 5)
#define RXCSR_SENTSTALL     (1u << 6)
#define RXCSR_CLRDATATOG    (1u << 7)
#define RXCSR_DPKTBUFDIS    (1u << 9)

/* IDX2 upper bytes hold the FIFO size codes; FIFOADD the addresses. */
#define USB_INFIFOSZ    R8(0x1A)
#define USB_OUTFIFOSZ   R8(0x1B)
#define USB_INFIFOADD   R16(0x1C)
#define USB_OUTFIFOADD  R16(0x1E)

/*---------------------------------------------------------------------------*/
/* CDC-ACM: ENDPOINTS AND THE SPEED THAT DECIDES THEIR SIZE                  */
/*---------------------------------------------------------------------------*/
/*
 * EP1 carries the data pipes and EP2 the notification the class requires but
 * this driver never sends.  USB 2.0 fixes bulk max-packet at 512 for high
 * speed and allows at most 64 at full speed, so the configuration descriptor
 * differs between the two and is patched at request time, once the chirp has
 * settled the speed.
 */
#define CDC_EP_DATA     1u          /* bulk IN + bulk OUT                    */
#define CDC_EP_NOTIFY   2u          /* interrupt IN, declared, never used    */
#define CDC_NOTIFY_MPS  8u
#define CDC_BULK_MPS_FS 64u
#define CDC_BULK_MPS_HS 512u

/*---------------------------------------------------------------------------*/
/* DESCRIPTORS                                                               */
/*---------------------------------------------------------------------------*/
/*
 * VID/PID 0x1209/0x0001 is pid.codes' reserved testing-and-development pair,
 * not for use in a product.
 */
#define TIKU_USB_VID   0x1209u
#define TIKU_USB_PID   0x0001u

#define DESC_DEVICE     1u
#define DESC_CONFIG     2u
#define DESC_STRING     3u
#define DESC_INTERFACE  4u
#define DESC_ENDPOINT   5u
#define DESC_QUALIFIER  6u
#define DESC_OTHERSPEED 7u

static uint8_t s_desc_qualifier[10] = {
    10, DESC_QUALIFIER, 0x00, 0x02,
    0x02, 0x00, 0x00, TIKU_USB_EP0_MAXPACKET, 1, 0
};

static uint8_t s_desc_device[18] = {
    18, DESC_DEVICE,
    0x00, 0x02,             /* bcdUSB 2.00                                   */
    0x02, 0x00, 0x00,       /* class 02 = Communications, at device level    */
    TIKU_USB_EP0_MAXPACKET,
    (uint8_t)(TIKU_USB_VID & 0xFFu), (uint8_t)(TIKU_USB_VID >> 8),
    (uint8_t)(TIKU_USB_PID & 0xFFu), (uint8_t)(TIKU_USB_PID >> 8),
    0x06, 0x00,             /* bcdDevice 0.06 -- the OS version              */
    1, 2, 3,                /* iManufacturer, iProduct, iSerialNumber        */
    1                       /* bNumConfigurations                            */
};

/*
 * CDC-ACM: a Communications interface carrying the class's functional
 * descriptors and a notification endpoint, plus a Data interface carrying the
 * two bulk pipes that move the console.  67 bytes total.
 *
 * The two wMaxPacketSize fields of the bulk endpoints are patched at request
 * time (see ep0_get_descriptor) because their legal value depends on the
 * negotiated speed.  A descriptor edit that moves them makes the patch
 * corrupt a neighbouring field: the device enumerates but carries no data.
 */
#define CFG_LEN            67u
#define CFG_OFF_BULK_OUT_MPS  (9u + 9u + 5u + 5u + 4u + 5u + 7u + 9u + 4u)
#define CFG_OFF_BULK_IN_MPS   (CFG_OFF_BULK_OUT_MPS + 7u)

static uint8_t s_desc_config[CFG_LEN] = {
    /* Configuration */
    9, DESC_CONFIG, CFG_LEN, 0, 2, 1, 0, 0x80, 250,

    /* Interface 0: Communications, ACM, no protocol */
    9, DESC_INTERFACE, 0, 0, 1, 0x02, 0x02, 0x00, 0,
    /* CDC Header functional, bcdCDC 1.10 */
    5, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management: no call management, data interface 1 */
    5, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM functional: supports Set/Get Line Coding + Control Line State */
    4, 0x24, 0x02, 0x02,
    /* CDC Union: control interface 0, subordinate 1 */
    5, 0x24, 0x06, 0x00, 0x01,
    /* Notification endpoint: interrupt IN, EP2 */
    7, DESC_ENDPOINT, 0x80 | CDC_EP_NOTIFY, 0x03,
       (uint8_t)CDC_NOTIFY_MPS, 0, 16,

    /* Interface 1: CDC Data */
    9, DESC_INTERFACE, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    /* Bulk OUT (host -> device) */
    7, DESC_ENDPOINT, CDC_EP_DATA, 0x02,
       (uint8_t)CDC_BULK_MPS_FS, 0, 0,
    /* Bulk IN (device -> host) */
    7, DESC_ENDPOINT, 0x80 | CDC_EP_DATA, 0x02,
       (uint8_t)CDC_BULK_MPS_FS, 0, 0
};

/* The patch offsets must point at the wMaxPacketSize of an endpoint
 * descriptor (bDescriptorType 5); these asserts check only that they lie
 * inside the descriptor. */
_Static_assert(CFG_OFF_BULK_OUT_MPS + 1u < CFG_LEN, "bulk OUT mps offset");
_Static_assert(CFG_OFF_BULK_IN_MPS  + 1u < CFG_LEN, "bulk IN mps offset");

/*
 * MSC: one interface and two bulk endpoints.  Mass storage carries its
 * command set inside the bulk pipes and has no class-specific descriptors.
 */
#define MSC_CFG_LEN       32u
#define MSC_OFF_OUT_MPS   (9u + 9u + 4u)
#define MSC_OFF_IN_MPS    (MSC_OFF_OUT_MPS + 7u)

static uint8_t s_desc_config_msc[MSC_CFG_LEN] = {
    9, DESC_CONFIG, MSC_CFG_LEN, 0, 1, 1, 0, 0x80, 250,
    /* Interface 0: Mass Storage / SCSI transparent / Bulk-Only Transport */
    9, DESC_INTERFACE, 0, 0, 2, 0x08, 0x06, 0x50, 0,
    /* Bulk OUT then bulk IN, both on EP1 */
    7, DESC_ENDPOINT, CDC_EP_DATA,        0x02, (uint8_t)CDC_BULK_MPS_FS, 0, 0,
    7, DESC_ENDPOINT, 0x80 | CDC_EP_DATA, 0x02, (uint8_t)CDC_BULK_MPS_FS, 0, 0
};

/* A device presenting mass storage claims no class at device level; the
 * interface carries it.  CDC claims class 02 there.  ep0_get_descriptor()
 * copies these three bytes into the device descriptor when MSC is up. */
static uint8_t s_dev_class_msc[3] = { 0x00, 0x00, 0x00 };

/* String descriptors, UTF-16LE.  Index 0 is the language list. */
static const uint8_t s_str_lang[4]  = { 4, DESC_STRING, 0x09, 0x04 };
static const uint8_t s_str_mfr[]    = { 14, DESC_STRING,
    'T',0, 'i',0, 'k',0, 'u',0, 'O',0, 'S',0 };
static const uint8_t s_str_prod[]   = { 36, DESC_STRING,
    'T',0, 'i',0, 'k',0, 'u',0, 'O',0, 'S',0, ' ',0,
    'A',0, 'p',0, 'o',0, 'l',0, 'l',0, 'o',0, '5',0, '1',0, '0',0, 'B',0 };
static const uint8_t s_str_serial[] = { 18, DESC_STRING,
    'U',0, '1',0, '-',0, '0',0, '0',0, '0',0, '1',0, '\0',0 };

/*---------------------------------------------------------------------------*/
/* MSC: BULK-ONLY TRANSPORT OVER A RAM DISK                                  */
/*---------------------------------------------------------------------------*/
/*
 * Mass storage is backed by a RAM disk in SSRAM, or by the eMMC.  The RAM
 * disk answers a read with a pointer, so its whole transport runs in the ISR.
 * The eMMC takes milliseconds per chunk, so in eMMC mode the data phase runs
 * in process context (see "MSC over the eMMC" below).
 */

/*
 * The wire format (wrappers, SCSI replies, the sense latch, the range check)
 * lives in services/usb/tiku_usbd_msc.c, which host tests exercise.  This file
 * moves the bytes through the MUSB FIFOs.
 */
#define CBW_LEN   TIKU_USBD_MSC_CBW_LEN
#define CSW_LEN   TIKU_USBD_MSC_CSW_LEN

#define MSC_BLOCK_SIZE   TIKU_USBD_MSC_BLOCK
/* Overridable: eMMC mode uses only MSC_BOUNCE_BYTES of it, so the megabyte is
 * the RAM disk's size.  A build that only presents the card can shrink it to
 * MSC_BOUNCE_BYTES (asserted below). */
#ifndef MSC_DISK_BYTES
#define MSC_DISK_BYTES   (1024u * 1024u)
#endif
#define MSC_DISK_BLOCKS  (MSC_DISK_BYTES / MSC_BLOCK_SIZE)

/*
 * One buffer, two roles: the disk in RAM-disk mode, the bounce buffer between
 * the card and the bulk pipes in eMMC mode.  The backing store is chosen at
 * bring-up and cannot change while USB is up, so the roles never overlap.
 */
static uint8_t s_disk[MSC_DISK_BYTES] __attribute__((section(".ssram")));

/** Bounce chunk for eMMC mode. */
#define MSC_BOUNCE_BYTES  (64u * 1024u)
/* The bounce chunk bounds every remaining-space expression in the eMMC data
 * path, and it exceeds a uint16_t: those expressions must be 32-bit. */
_Static_assert(MSC_BOUNCE_BYTES > 65535u,
               "bounce exceeds 16 bits -- remaining-space vars must be 32-bit");
_Static_assert(MSC_BOUNCE_BYTES <= MSC_DISK_BYTES,
               "bounce must fit the shared store");

/** @brief Which store MSC presents. */
typedef enum { MSC_STORE_RAM = 0, MSC_STORE_EMMC } msc_store_t;
static msc_store_t s_store = MSC_STORE_RAM;

/**
 * @brief The medium as the host sees it: capacity, product name, sense latch.
 *
 * tiku_usb_up_full() sets the capacity and the name for the backing store.
 */
static tiku_usbd_msc_t s_msc = { MSC_DISK_BLOCKS, "RAM Disk", 0u, 0u };

typedef enum {
    BOT_CBW = 0,     /**< waiting for a command wrapper                     */
    BOT_DATA_IN,     /**< streaming data to the host                        */
    BOT_DATA_OUT,    /**< receiving data from the host                      */
    BOT_CSW          /**< status wrapper queued                             */
} bot_state_t;

static bot_state_t s_bot;
static uint32_t s_bot_tag;
static uint32_t s_bot_residue;
static uint8_t  s_bot_status;        /**< 0 pass, 1 fail                    */
static uint8_t *s_bot_ptr;           /**< cursor into the disk or a reply   */
static uint32_t s_bot_left;          /**< bytes still owed in the data phase*/
static uint8_t  s_bot_reply[TIKU_USBD_MSC_REPLY_MAX];
_Static_assert(sizeof(s_bot_reply) >= TIKU_USBD_MSC_CSW_LEN,
               "the reply buffer also carries the status wrapper");
static volatile uint32_t s_n_cbw, s_n_rd, s_n_wr;

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

typedef enum {
    EP0_IDLE = 0,
    EP0_TX,          /**< control-IN data stage in progress                  */
    EP0_RX,          /**< control-OUT data stage in progress                 */
} ep0_state_t;

static uint8_t     s_up;
static uint8_t     s_attached;
static ep0_state_t s_ep0;
static const uint8_t *s_tx;      /**< remaining control-IN payload           */
static uint16_t    s_tx_len;
static uint8_t     s_pending_addr;   /**< see SET_ADDRESS below              */
static uint16_t    s_rx_expect;      /**< bytes due in a control-OUT stage   */
static uint8_t     s_addr;
static uint8_t     s_config;
static tiku_usb_speed_t s_speed;   /**< negotiated, read from POWER.HSMode  */
static tiku_usb_speed_t s_want;    /**< requested at bring-up               */
static tiku_usb_class_t s_class = TIKU_USB_CLASS_CDC;
static uint8_t     s_em9305_up;    /**< the HS clock source has been booted  */

/* Observability counters: the ISR does not print, so it counts. */
static volatile uint32_t s_n_reset, s_n_setup, s_n_irq, s_n_stall,
                         s_n_suspend, s_n_resume, s_n_setupend;
static volatile uint16_t s_last_req;   /**< bRequest<<8 | bmRequestType      */

/*
 * The last four refused requests, as bRequest<<8 | (wValue >> 8); for
 * GET_DESCRIPTOR the low byte is the descriptor type.
 */
static volatile uint16_t s_stalled[4];
static volatile uint8_t  s_stall_wr;
static uint8_t s_cur_req, s_cur_type;

/*---------------------------------------------------------------------------*/
/* CDC RINGS AND FLOW CONTROL                                                */
/*---------------------------------------------------------------------------*/
/*
 * Two rings sit between the ISR and the shell process.  If the RX ring cannot
 * take the received packet, the ISR leaves it in the FIFO and masks the
 * endpoint's interrupt: the controller NAKs, the host retries, and the
 * transfer paces itself to the shell with no byte dropped.  The drain side
 * re-enables the interrupt and pumps the FIFO once space appears.
 *
 * The rings sit in SSRAM.  No DMA touches them (the FIFO is PIO), so there is
 * no cache coherency to manage.
 */
#define CDC_TX_RING  4096u
#define CDC_RX_RING  4096u

static uint8_t s_txbuf[CDC_TX_RING] __attribute__((section(".ssram")));
static uint8_t s_rxbuf[CDC_RX_RING] __attribute__((section(".ssram")));
static volatile uint16_t s_tx_head, s_tx_tail;
static volatile uint16_t s_rx_head, s_rx_tail;
static volatile uint8_t  s_tx_busy;      /**< a packet is in the IN FIFO     */
static volatile uint8_t  s_rx_stalled;   /**< RX interrupt masked for space  */
static volatile uint32_t s_n_tx_bytes, s_n_rx_bytes, s_n_tx_drop, s_n_nak;
static uint8_t  s_configured;
static uint8_t  s_dtr;                   /**< host has opened the terminal   */
static uint16_t s_bulk_mps = CDC_BULK_MPS_FS;
static uint8_t  s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };

/* Defined below with the CDC and MSC endpoint code; ep0_setup() calls
 * cdc_endpoints_open() above its definition. */
static void cdc_endpoints_open(void);
static void cdc_tx_fill(void);
static void cdc_rx_pump(void);
static void cdc_rx_resume(void);
static void msc_rx_packet(void);
static void msc_tx_done(void);
/* Defined below with the MSC transport code; adma_run() calls msc_tx_wait()
 * and tiku_usb_msc_poll() calls msc_poll_one() above their definitions. */
static int  msc_tx_wait(void);
static void msc_tx_raw(const uint8_t *p, uint16_t n);
static void msc_csw_poll(uint8_t status, uint32_t residue);
static void msc_scsi(const tiku_usbd_msc_cbw_t *cbw);
static int  msc_poll_one(void);

/** @brief Bytes in use in a ring of @p sz with head @p h and tail @p t. */
static inline uint16_t ring_used(uint16_t h, uint16_t t, uint16_t sz)
{
    return (uint16_t)((h >= t) ? (h - t) : (uint16_t)(sz - t + h));
}

/**
 * @brief Mask the USB interrupt around process-context register access.
 *
 * INDEX and the CSR window it selects are shared mutable state, so a
 * process-context sequence that sets INDEX and then uses it must not be
 * interrupted by an ISR that sets INDEX to something else.
 *
 * @note Process context only; never call it from the ISR.
 * @return Previous enable state, so nesting cannot wrongly re-enable.
 */
static inline uint32_t usb_lock(void)
{
    uint32_t was = NVIC_GetEnableIRQ(USB0_IRQn);
    NVIC_DisableIRQ(USB0_IRQn);
    __DSB(); __ISB();
    return was;
}
/** @brief Re-enable the USB interrupt if usb_lock() found it enabled. */
static inline void usb_unlock(uint32_t was)
{
    if (was) { NVIC_EnableIRQ(USB0_IRQn); }
}

/*---------------------------------------------------------------------------*/
/* FIFO HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read @p n bytes out of an endpoint FIFO.
 *
 * The FIFO port is a 32-bit window onto a byte FIFO: a byte-width access pops
 * one byte, as control transfers need for 8-byte SETUP packets and
 * odd-length descriptors; a word access pops four.
 */
static void fifo_read(unsigned ep, uint8_t *dst, uint16_t n)
{
    volatile uint8_t *port = (volatile uint8_t *)&USB_FIFO(ep);
    uint16_t i;
    for (i = 0u; i < n; i++) { dst[i] = *port; }
}

/** @brief Write @p n bytes into an endpoint FIFO, byte at a time. */
static void fifo_write(unsigned ep, const uint8_t *src, uint16_t n)
{
    volatile uint8_t *port = (volatile uint8_t *)&USB_FIFO(ep);
    uint16_t i;
    for (i = 0u; i < n; i++) { *port = src[i]; }
}

/*---------------------------------------------------------------------------*/
/* EP0: the control endpoint state machine                                   */
/*---------------------------------------------------------------------------*/

/** @brief Stall EP0 and go idle -- the answer to anything unsupported. */
static void ep0_stall(void)
{
    s_stalled[s_stall_wr & 3u] =
        (uint16_t)(((uint16_t)s_cur_req << 8) | s_cur_type);
    s_stall_wr++;
    USB_INDEX = 0u;
    USB_CSR0 = CSR0_SERVICEDRXPKTRDY | CSR0_SENDSTALL;
    s_ep0 = EP0_IDLE;
    s_tx_len = 0u;
    s_n_stall++;
}

/**
 * @brief Push the next packet of a control-IN transfer.
 *
 * DataEnd travels with the last packet: a short packet is itself the
 * end-of-transfer signal, so DataEnd is set whenever this packet exhausts the
 * payload, which the caller has already clamped to wLength.
 */
static void ep0_tx_next(void)
{
    uint16_t n = (s_tx_len > TIKU_USB_EP0_MAXPACKET)
                 ? TIKU_USB_EP0_MAXPACKET : s_tx_len;

    USB_INDEX = 0u;
    if (n) { fifo_write(0u, s_tx, n); }
    s_tx     += n;
    s_tx_len -= n;

    if (s_tx_len == 0u) {
        USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
        s_ep0 = EP0_IDLE;
    } else {
        USB_CSR0 = CSR0_TXPKTRDY;
    }
}

/** @brief Begin a control-IN data stage of @p len bytes from @p p. */
static void ep0_reply(const uint8_t *p, uint16_t len, uint16_t wLength)
{
    if (len > wLength) { len = wLength; }   /* never send more than asked   */
    s_tx     = p;
    s_tx_len = len;
    s_ep0    = EP0_TX;
    /* The SETUP packet must be acknowledged before the data stage may run;
     * ep0_tx_next() then writes TxPktRdy in a separate access. */
    USB_INDEX = 0u;
    USB_CSR0 = CSR0_SERVICEDRXPKTRDY;
    ep0_tx_next();
}

/** @brief Acknowledge a no-data-stage request and let the status stage run. */
static void ep0_ack(void)
{
    USB_INDEX = 0u;
    USB_CSR0 = CSR0_SERVICEDRXPKTRDY | CSR0_DATAEND;
    s_ep0 = EP0_IDLE;
}

/** @brief GET_DESCRIPTOR dispatch. */
static void ep0_get_descriptor(uint8_t type, uint8_t idx, uint16_t wLength)
{
    switch (type) {
    case DESC_DEVICE:
        /* Class lives at device level for CDC and at interface level for
         * MSC; the same device descriptor serves both with three bytes
         * patched. */
        if (s_class == TIKU_USB_CLASS_MSC) {
            s_desc_device[4] = s_dev_class_msc[0];
            s_desc_device[5] = s_dev_class_msc[1];
            s_desc_device[6] = s_dev_class_msc[2];
        } else {
            s_desc_device[4] = 0x02u; s_desc_device[5] = 0x00u;
            s_desc_device[6] = 0x00u;
        }
        ep0_reply(s_desc_device, sizeof s_desc_device, wLength);
        return;
    case DESC_QUALIFIER:
        /* A bring-up at full speed clears HSEnab, so the device is
         * full-speed only and USB 2.0 9.6.2 has it stall the qualifier and
         * the other-speed configuration. */
        if (s_want != TIKU_USB_SPEED_HIGH) {
            break;
        }
        s_desc_qualifier[4] = (s_class == TIKU_USB_CLASS_MSC) ? 0u : 2u;
        ep0_reply(s_desc_qualifier, sizeof s_desc_qualifier, wLength);
        return;
    case DESC_OTHERSPEED:
        if (s_want != TIKU_USB_SPEED_HIGH) {
            break;
        }
        /* fall through */
    case DESC_CONFIG:
        /* Patch the bulk max-packet to the speed the chirp settled on.  The
         * host asks for the configuration after the bus reset, when the
         * speed is known. */
        {
            uint16_t mps = (type == DESC_OTHERSPEED)
                         ? ((s_bulk_mps == 512u) ? 64u : 512u) : s_bulk_mps;
            uint8_t lo = (uint8_t)(mps & 0xFFu);
            uint8_t hi = (uint8_t)(mps >> 8);
            s_desc_config_msc[1] = type;
            s_desc_config[1] = type;
            if (s_class == TIKU_USB_CLASS_MSC) {
                s_desc_config_msc[MSC_OFF_OUT_MPS]      = lo;
                s_desc_config_msc[MSC_OFF_OUT_MPS + 1u] = hi;
                s_desc_config_msc[MSC_OFF_IN_MPS]       = lo;
                s_desc_config_msc[MSC_OFF_IN_MPS  + 1u] = hi;
                ep0_reply(s_desc_config_msc, sizeof s_desc_config_msc,
                          wLength);
            } else {
                s_desc_config[CFG_OFF_BULK_OUT_MPS]      = lo;
                s_desc_config[CFG_OFF_BULK_OUT_MPS + 1u] = hi;
                s_desc_config[CFG_OFF_BULK_IN_MPS]       = lo;
                s_desc_config[CFG_OFF_BULK_IN_MPS  + 1u] = hi;
                ep0_reply(s_desc_config, sizeof s_desc_config, wLength);
            }
        }
        return;
    case DESC_STRING:
        switch (idx) {
        case 0: ep0_reply(s_str_lang,   sizeof s_str_lang,   wLength); return;
        case 1: ep0_reply(s_str_mfr,    sizeof s_str_mfr,    wLength); return;
        case 2: ep0_reply(s_str_prod,   sizeof s_str_prod,   wLength); return;
        case 3: ep0_reply(s_str_serial, sizeof s_str_serial, wLength); return;
        default: break;
        }
        break;
    default:
        /* Unsupported descriptor type. */
        break;
    }
    ep0_stall();
}

/** @brief Decode one 8-byte SETUP packet and start whatever it asks for. */
static void ep0_setup(const uint8_t *p)
{
    uint8_t  bmRequestType = p[0];
    uint8_t  bRequest      = p[1];
    uint16_t wValue        = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
    uint16_t wLength       = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));
    static const uint8_t zero16[2] = { 0, 0 };

    s_last_req = (uint16_t)((uint16_t)bRequest << 8 | bmRequestType);
    s_cur_req  = bRequest;
    s_cur_type = (uint8_t)(wValue >> 8);
    s_n_setup++;

    /*
     * Class requests (bmRequestType type field 1).  MSC answers GET_MAX_LUN
     * and the Bulk-Only reset.  CDC answers the three line requests, which
     * the host issues before it opens the port: a device that stalls
     * SET_LINE_CODING never gets a terminal.
     */
    if ((bmRequestType & 0x60u) == 0x20u) {
        if (s_class == TIKU_USB_CLASS_MSC) {
            static const uint8_t zero_lun = 0u;
            switch (bRequest) {
            case 0xFE:  /* GET_MAX_LUN -- one logical unit, so 0.  A host
                         * issues this before it will mount anything; a
                         * stall is legal but makes Linux retry. */
                ep0_reply(&zero_lun, 1u, wLength);
                return;
            case 0xFF:  /* BULK-ONLY MASS STORAGE RESET */
                s_bot = BOT_CBW;
                s_bot_left = 0u;
                ep0_ack();
                return;
            default:
                ep0_stall();
                return;
            }
        }
        switch (bRequest) {
        case 0x20:  /* SET_LINE_CODING -- 7 bytes of baud/parity that are
                     * ignored, but must be accepted: this is a real
                     * control-OUT stage. */
            s_ep0 = EP0_RX;
            s_rx_expect = (wLength > sizeof s_line_coding)
                          ? (uint16_t)sizeof s_line_coding : wLength;
            USB_INDEX = 0u;
            USB_CSR0 = CSR0_SERVICEDRXPKTRDY;   /* no DataEnd: data follows  */
            return;
        case 0x21:  /* GET_LINE_CODING */
            ep0_reply(s_line_coding, (uint16_t)sizeof s_line_coding, wLength);
            return;
        case 0x22:  /* SET_CONTROL_LINE_STATE: bit 0 is DTR, i.e. "the
                     * terminal is open".  Output is gated on it so that a
                     * board printing into a closed port does not block. */
            s_dtr = (uint8_t)(wValue & 0x01u);
            ep0_ack();
            return;
        default:
            ep0_stall();
            return;
        }
    }

    /* Any other non-standard request is stalled, a legal answer. */
    if ((bmRequestType & 0x60u) != 0x00u) { ep0_stall(); return; }

    switch (bRequest) {
    case 0x05:  /* SET_ADDRESS */
        /*
         * The address must not take effect until the host has seen the
         * status stage, because until then the host is still talking to
         * address 0.  MUSB's FADDR applies the moment it is written, so the
         * write waits for the next EP0 interrupt, after the zero-length
         * status packet has gone out.
         */
        s_addr = (uint8_t)(wValue & 0x7Fu);
        s_pending_addr = 1u;
        ep0_ack();
        return;

    case 0x06:  /* GET_DESCRIPTOR */
        ep0_get_descriptor((uint8_t)(wValue >> 8), (uint8_t)(wValue & 0xFFu),
                           wLength);
        return;

    case 0x09:  /* SET_CONFIGURATION */
        s_config = (uint8_t)(wValue & 0xFFu);
        if (s_config) {
            cdc_endpoints_open();
            s_configured = 1u;
        } else {
            s_configured = 0u;
        }
        ep0_ack();
        return;

    case 0x08:  /* GET_CONFIGURATION */
        ep0_reply(&s_config, 1u, wLength);
        return;

    case 0x00:  /* GET_STATUS -- bus powered, no remote wakeup */
        ep0_reply(zero16, 2u, wLength);
        return;

    case 0x0A:  /* GET_INTERFACE */
        ep0_reply(zero16, 1u, wLength);
        return;

    case 0x01:  /* CLEAR_FEATURE  */
    case 0x03:  /* SET_FEATURE    */
    case 0x0B:  /* SET_INTERFACE  */
        ep0_ack();
        return;

    default:
        ep0_stall();
        return;
    }
}

/** @brief EP0 interrupt: drive the control state machine one step. */
static void ep0_irq(void)
{
    uint16_t csr;

    USB_INDEX = 0u;
    csr = USB_CSR0;

    /*
     * SetupEnd means the host abandoned the previous control transfer before
     * it finished.  It is not an error and it is not rare (a host that only
     * wanted the first 8 bytes of a config descriptor does this).
     * Acknowledge, drop whatever was in flight, and carry on.
     */
    if (csr & CSR0_SETUPEND) {
        USB_CSR0 = CSR0_SERVICEDSETUPEND;
        s_ep0 = EP0_IDLE;
        s_tx_len = 0u;
        s_n_setupend++;
        csr = USB_CSR0;
    }

    if (csr & CSR0_SENTSTALL) {
        USB_CSR0 = (uint16_t)(csr & (uint16_t)~CSR0_SENTSTALL);
        s_ep0 = EP0_IDLE;
        return;
    }

    /*
     * Apply a deferred SET_ADDRESS now: reaching this interrupt means the
     * status stage of the request that set it has completed.
     */
    if (s_pending_addr && s_ep0 == EP0_IDLE) {
        USB_FADDR = s_addr;
        s_pending_addr = 0u;
    }

    if (csr & CSR0_RXPKTRDY) {
        if (s_ep0 == EP0_RX) {
            /* Control-OUT data stage.  The only one this driver accepts is
             * SET_LINE_CODING, whose seven bytes are kept solely so that
             * GET_LINE_CODING can hand them back consistently -- the console
             * has no baud rate to set. */
            uint8_t scratch[TIKU_USB_EP0_MAXPACKET];
            uint16_t n = USB_COUNT0;
            if (n > sizeof scratch) { n = sizeof scratch; }
            fifo_read(0u, scratch, n);
            if (n > s_rx_expect) { n = s_rx_expect; }
            for (uint16_t k = 0u; k < n; k++) { s_line_coding[k] = scratch[k]; }
            USB_CSR0 = CSR0_SERVICEDRXPKTRDY | CSR0_DATAEND;
            s_ep0 = EP0_IDLE;
        } else {
            uint8_t setup[8];
            if (USB_COUNT0 == 8u) {
                fifo_read(0u, setup, 8u);
                ep0_setup(setup);
            } else {
                ep0_stall();
            }
        }
        return;
    }

    /* Not RxPktRdy: if a control-IN is in flight and the FIFO has drained,
     * push the next packet. */
    if (s_ep0 == EP0_TX && !(csr & CSR0_TXPKTRDY)) {
        ep0_tx_next();
    }
}

/*---------------------------------------------------------------------------*/
/* CDC ENDPOINTS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure EP1 (bulk in + out) and EP2 (interrupt in).
 *
 * FIFO space is allocated by hand, cumulatively in units of 8 bytes, from the
 * end of EP0's 64 bytes.  Runs from SET_CONFIGURATION, after a bus reset has
 * discarded the previous allocation.
 */
static void cdc_endpoints_open(void)
{
    uint16_t addr = TIKU_USB_FIFO_FIRST;   /* units; EP0 owns 0..7           */
    uint8_t  sz;
    int      dbuf;

    /* size code = log2(maxpacket / 8) */
    sz = (s_bulk_mps == CDC_BULK_MPS_HS) ? 6u : 3u;   /* 512 -> 6, 64 -> 3   */

    /*
     * The bulk pipes are double-buffered in eMMC mode.  Single-buffered, the
     * endpoint holds one packet and the pump waits for the host to collect
     * each one: at high speed one 512 B packet per 125 us microframe, or
     * 4.1 MB/s.  Double-buffered, the pump fills one packet while the host
     * drains the other, at twice the FIFO space.
     */
    dbuf = (s_class == TIKU_USB_CLASS_MSC && s_store == MSC_STORE_EMMC);

    /* --- EP1 IN (bulk, device -> host) --- */
    USB_INDEX = CDC_EP_DATA;
    USB_TXMAXP = s_bulk_mps;
    USB_INFIFOSZ = (uint8_t)(sz | (dbuf ? 0x10u : 0u));
    USB_INFIFOADD = addr;
    addr = (uint16_t)(addr + (dbuf ? 2u : 1u) *
                             (s_bulk_mps / TIKU_USB_FIFO_UNIT));
    USB_TXCSR = (uint16_t)(TXCSR_CLRDATATOG | TXCSR_FLUSHFIFO | TXCSR_MODE |
                           (dbuf ? 0u : TXCSR_DPKTBUFDIS));

    /* --- EP1 OUT (bulk, host -> device) --- */
    USB_RXMAXP = s_bulk_mps;
    USB_OUTFIFOSZ = (uint8_t)(sz | (dbuf ? 0x10u : 0u));
    USB_OUTFIFOADD = addr;
    addr = (uint16_t)(addr + (dbuf ? 2u : 1u) *
                             (s_bulk_mps / TIKU_USB_FIFO_UNIT));
    USB_RXCSR = (uint16_t)(RXCSR_CLRDATATOG | RXCSR_FLUSHFIFO |
                           (dbuf ? 0u : RXCSR_DPKTBUFDIS));

    /* --- EP2 IN (interrupt notification; declared, never sent) --- */
    USB_INDEX = CDC_EP_NOTIFY;
    USB_TXMAXP = CDC_NOTIFY_MPS;
    USB_INFIFOSZ = 0u;                      /* 8 bytes                      */
    USB_INFIFOADD = addr;
    USB_TXCSR = TXCSR_CLRDATATOG | TXCSR_FLUSHFIFO | TXCSR_DPKTBUFDIS
                | TXCSR_MODE;

    USB_INDEX = 0u;

    s_tx_head = s_tx_tail = 0u;
    s_rx_head = s_rx_tail = 0u;
    s_tx_busy = 0u;
    s_rx_stalled = 0u;

    /* Arm only the data endpoint; the notification pipe never interrupts
     * because nothing is ever queued on it. */
    if (s_class == TIKU_USB_CLASS_MSC && s_store == MSC_STORE_EMMC) {
        /* The pump owns EP1, with its interrupts masked; the ISR keeps EP0
         * and the bus events. */
        USB_INTRTXE = 0x0001u;
        USB_INTRRXE = 0x0000u;
    } else {
        USB_INTRTXE = (uint16_t)(0x0001u | (1u << CDC_EP_DATA));
        USB_INTRRXE = (uint16_t)(1u << CDC_EP_DATA);
    }

    s_bot = BOT_CBW;
    s_bot_left = 0u;
    s_msc.sense_key = 0u; s_msc.sense_asc = 0u;
}

/** @brief Fill the IN FIFO from the TX ring and hand it to the host. */
static void cdc_tx_fill(void)
{
    uint16_t n = 0u;

    USB_INDEX = CDC_EP_DATA;
    if (USB_TXCSR & TXCSR_TXPKTRDY) { return; }   /* still in flight         */

    while (n < s_bulk_mps && s_tx_tail != s_tx_head) {
        *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA) = s_txbuf[s_tx_tail];
        s_tx_tail = (uint16_t)((s_tx_tail + 1u) % CDC_TX_RING);
        n++;
    }
    if (n) {
        USB_TXCSR = TXCSR_TXPKTRDY | TXCSR_MODE | TXCSR_DPKTBUFDIS;
        s_tx_busy = 1u;
        s_n_tx_bytes += n;
    } else {
        s_tx_busy = 0u;
    }
}

/**
 * @brief Move one received packet into the RX ring, or leave it and back off.
 *
 * A packet the ring cannot hold stays in the FIFO, so the controller NAKs
 * and the host slows down; no byte is dropped.
 */
static void cdc_rx_pump(void)
{
    uint16_t cnt, free_sp, i;

    USB_INDEX = CDC_EP_DATA;
    if (!(USB_RXCSR & RXCSR_RXPKTRDY)) { return; }

    cnt = (uint16_t)(R16(0x18) & 0x1FFFu);       /* RXCOUNT                  */
    free_sp = (uint16_t)(CDC_RX_RING - 1u -
                    ring_used(s_rx_head, s_rx_tail, CDC_RX_RING));
    if (cnt > free_sp) {
        /* No room: do not touch the FIFO.  Mask this endpoint's interrupt to
         * avoid re-entry and let the host be NAKed until the shell drains.
         * cdc_rx_resume() undoes this. */
        USB_INTRRXE = (uint16_t)(USB_INTRRXE &
                                 (uint16_t)~(1u << CDC_EP_DATA));
        s_rx_stalled = 1u;
        s_n_nak++;
        return;
    }

    for (i = 0u; i < cnt; i++) {
        s_rxbuf[s_rx_head] = *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA);
        s_rx_head = (uint16_t)((s_rx_head + 1u) % CDC_RX_RING);
    }
    s_n_rx_bytes += cnt;
    USB_RXCSR = RXCSR_DPKTBUFDIS;                /* clears RxPktRdy          */
}

/**
 * @brief Re-arm reception after the shell has made room.
 *
 * Process context.  INTRRX is read-to-clear and the ISR has consumed the bit
 * for the packet left in the FIFO, so re-enabling the interrupt raises no new
 * one; this function pumps that packet itself.
 */
static void cdc_rx_resume(void)
{
    uint32_t was;
    if (!s_rx_stalled) { return; }
    /*
     * cdc_getc() calls this after every byte it pops, so the NVIC lock, with
     * its barriers and pipeline flush, is taken only once a whole packet
     * fits.
     */
    if ((uint16_t)(CDC_RX_RING - 1u -
                   ring_used(s_rx_head, s_rx_tail, CDC_RX_RING)) < s_bulk_mps) {
        return;
    }
    was = usb_lock();
    if (s_rx_stalled) {
        s_rx_stalled = 0u;
        USB_INTRRXE = (uint16_t)(USB_INTRRXE | (1u << CDC_EP_DATA));
        cdc_rx_pump();
        USB_INDEX = 0u;
    }
    usb_unlock(was);
}

/*---------------------------------------------------------------------------*/
/* MSC: THE BULK-ONLY TRANSPORT STATE MACHINE                                */
/*---------------------------------------------------------------------------*/

/*
 * The endianness helpers, the range check and the SCSI replies live in
 * services/usb/tiku_usbd_msc.c.  Below is the transport: FIFOs, packets and
 * the BOT state machine.
 */

/** @brief Push one packet of the IN data phase (or the CSW) to the host. */
static void msc_tx_packet(void)
{
    uint16_t n = 0u;

    USB_INDEX = CDC_EP_DATA;
    if (USB_TXCSR & TXCSR_TXPKTRDY) { return; }

    while (n < s_bulk_mps && s_bot_left) {
        *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA) = *s_bot_ptr++;
        s_bot_left--;
        n++;
    }
    USB_TXCSR = TXCSR_TXPKTRDY | TXCSR_MODE | TXCSR_DPKTBUFDIS;
    s_n_tx_bytes += n;
}

/** @brief Queue the 13-byte status wrapper that ends every command. */
static void msc_send_csw(void)
{
    tiku_usbd_msc_build_csw(s_bot_reply, s_bot_tag, s_bot_residue,
                            s_bot_status);
    s_bot_ptr  = s_bot_reply;
    s_bot_left = CSW_LEN;
    s_bot      = BOT_CSW;
    msc_tx_packet();
}

/** @brief Latch a sense condition and fail the command in flight. */
static void msc_fail(uint8_t key, uint8_t asc)
{
    tiku_usbd_msc_fail(&s_msc, key, asc);
    s_bot_status = 1u;
}

/**
 * @brief Set up whatever data phase the decoded command asks for.
 *
 * tiku_usbd_msc_decode() decides which opcode, which bytes and what residue;
 * this function picks where those bytes live and which FIFO moves them.
 */
static void msc_scsi(const tiku_usbd_msc_cbw_t *cbw)
{
    tiku_usbd_msc_cmd_t c;

    tiku_usbd_msc_decode(&s_msc, cbw, s_bot_reply, &c);
    s_bot_status  = c.status;
    s_bot_residue = c.residue;

    switch (c.action) {
    case TIKU_USBD_MSC_ACT_READ:
        s_n_rd++;
        s_bot_ptr  = &s_disk[c.lba * MSC_BLOCK_SIZE];
        s_bot_left = c.bytes;
        s_bot      = BOT_DATA_IN;
        msc_tx_packet();
        return;

    case TIKU_USBD_MSC_ACT_WRITE:
        s_n_wr++;
        s_bot_ptr  = &s_disk[c.lba * MSC_BLOCK_SIZE];
        s_bot_left = c.bytes;
        s_bot      = BOT_DATA_OUT;   /* packets arrive on the OUT endpoint  */
        return;

    case TIKU_USBD_MSC_ACT_REPLY:
        s_bot_ptr  = s_bot_reply;
        s_bot_left = c.len;
        s_bot      = BOT_DATA_IN;
        msc_tx_packet();
        return;

    default:
        msc_send_csw();
        return;
    }
}

/** @brief A packet arrived on the bulk OUT endpoint. */
static void msc_rx_packet(void)
{
    uint16_t cnt, i;
    uint8_t cbw[CBW_LEN];
    tiku_usbd_msc_cbw_t c;

    USB_INDEX = CDC_EP_DATA;
    if (!(USB_RXCSR & RXCSR_RXPKTRDY)) { return; }
    cnt = (uint16_t)(R16(0x18) & 0x1FFFu);

    if (s_bot == BOT_DATA_OUT) {
        for (i = 0u; i < cnt; i++) {
            uint8_t b = *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA);
            if (s_bot_left) { *s_bot_ptr++ = b; s_bot_left--; }
        }
        s_n_rx_bytes += cnt;
        USB_RXCSR = RXCSR_DPKTBUFDIS;
        if (s_bot_left == 0u) { msc_send_csw(); }
        return;
    }

    /* Otherwise this must be a command wrapper. */
    if (cnt != CBW_LEN) {
        for (i = 0u; i < cnt; i++) {
            (void)*(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA);
        }
        USB_RXCSR = RXCSR_DPKTBUFDIS;
        return;
    }
    for (i = 0u; i < CBW_LEN; i++) {
        cbw[i] = *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA);
    }
    USB_RXCSR = RXCSR_DPKTBUFDIS;

    if (!tiku_usbd_msc_parse_cbw(cbw, CBW_LEN, &c)) { return; }  /* ignore */
    s_n_cbw++;
    s_bot_tag = c.tag;
    msc_scsi(&c);
}

/** @brief The bulk IN endpoint has emptied: continue, or finish. */
static void msc_tx_done(void)
{
    if (s_bot == BOT_DATA_IN) {
        if (s_bot_left) { msc_tx_packet(); }
        else            { msc_send_csw(); }
    } else if (s_bot == BOT_CSW) {
        if (s_bot_left) { msc_tx_packet(); }
        else            { s_bot = BOT_CBW; }
    }
}

/*---------------------------------------------------------------------------*/
/* MSC OVER THE eMMC: PROCESS-CONTEXT DATA PHASE                             */
/*---------------------------------------------------------------------------*/
/*
 * The eMMC takes milliseconds per chunk, far too long to hold an interrupt
 * while the console and the tick wait.  So in eMMC mode the ISR keeps EP0 and
 * the bus events, EP1's interrupts stay masked, and a process-context pump
 * owns the bulk endpoints, polling them directly.
 *
 * The pump runs from the shell's pump list and serves commands until the
 * host goes quiet.  Each command is bounded by the host's
 * dCBWDataTransferLength; a large read blocks the shell for its own transfer.
 *
 * INDEX is still shared with the ISR, which sets it to 0 for EP0, so every
 * sequence below takes the lock around selecting the endpoint and touching
 * its registers, and selects it again after any wait.
 */

#define MSC_WAIT_SPINS  2000000u   /* poll bound of each wait below         */

/*---------------------------------------------------------------------------*/
/* ADMA: DMA FOR THE eMMC-MODE DATA PHASE                                    */
/*---------------------------------------------------------------------------*/
/*
 * The controller has ten DMA channels.  They are not interchangeable: each
 * is bound to an endpoint half by ADMAEP, per the HAL:
 *      IN  endpoint n  ->  channel n - 1     (EP1 IN  = channel 0)
 *      OUT endpoint n  ->  channel n + 4     (EP1 OUT = channel 5)
 *
 * The per-channel registers are four banks of ten at stride 4, at the
 * offsets of the CMSIS struct, which names them one by one
 * (ADMATOTCOUNT0..9); the macros below index the bank.
 */
#define ADMA_BANK(off, ch) (*(volatile uint32_t *)(USB_BASE + (off) + 4u*(ch)))
#define ADMA_TOTCOUNT(ch)  ADMA_BANK(0x2100u, ch)
#define ADMA_TARGADDR(ch)  ADMA_BANK(0x2200u, ch)
#define ADMA_EPNUM(ch)     ADMA_BANK(0x2300u, ch)
#define ADMA_REQSIZE(ch)   ADMA_BANK(0x2400u, ch)

#define ADMA_CH_IN(ep)     ((unsigned)(ep) - 1u)
#define ADMA_CH_OUT(ep)    ((unsigned)(ep) + 4u)

/* Endpoint CSR bits that hand the FIFO to the DMA engine. */
#define TXCSR_DMAREQMODE   (1u << 10)
#define TXCSR_DMAREQENAB   (1u << 12)
#define TXCSR_AUTOSET      (1u << 15)
#define RXCSR_DMAREQMODE   (1u << 11)
#define RXCSR_DMAREQENAB   (1u << 13)
#define RXCSR_AUTOCLEAR    (1u << 15)

/*
 * ADMA is off at boot; `power usb adma on` turns it on at run time.
 */
static uint8_t  s_adma;               /**< runtime switch, for measuring    */
static volatile uint32_t s_n_adma, s_n_adma_err;

/**
 * @brief Run one ADMA transfer and wait for it.  Non-zero on success.
 *
 * @p bytes must be a whole number of max packets, which every MSC data phase
 * is (512-byte blocks, 512 or 64 byte packets): DMA mode 1 cannot express a
 * trailing short packet.
 *
 * @note Process context only; it blocks.
 */
static int adma_run(unsigned ep, int is_in, uint8_t *buf, uint32_t bytes)
{
    unsigned ch = is_in ? ADMA_CH_IN(ep) : ADMA_CH_OUT(ep);
    uint32_t mask = 1u << ch;
    uint32_t spins;
    uint32_t was;

    if (bytes == 0u || bytes > 0xFFFFFFu) { return 0; }   /* 24-bit count   */
    if ((bytes % s_bulk_mps) != 0u)       { return 0; }

    /* The engine moves bytes on the bus; the D-cache is not on that path.
     * Clean either way (so no dirty line is written back over the transfer),
     * and invalidate after an OUT so the CPU sees what the engine wrote. */
    tiku_cpu_dcache_clean(buf, bytes);

    was = usb_lock();
    USB_INDEX = (uint8_t)ep;
    if (is_in) {
        USB_TXCSR = (uint16_t)(TXCSR_MODE | TXCSR_AUTOSET |
                               TXCSR_DMAREQENAB | TXCSR_DMAREQMODE);
    } else {
        USB_RXCSR = (uint16_t)(RXCSR_AUTOCLEAR | RXCSR_DMAREQENAB |
                               RXCSR_DMAREQMODE);
    }

    USB->ADMACTRL |= 1u;                       /* select ADMA mode          */
    if (is_in) { USB->ADMADIR |=  mask; }      /* 1 = device -> host        */
    else       { USB->ADMADIR &= ~mask; }
    USB->ADMAPRI &= ~mask;
    USB->ADMACMPINTCLR = mask;                 /* clear any stale status    */
    USB->ADMAERRINTCLR = mask;
    USB->ADMACMPINTEN |= mask;
    USB->ADMAERRINTEN |= mask;

    /* Plain assignment: this driver reuses one channel for every chunk of
     * every transfer, and an OR into these registers keeps bits from the
     * channel's previous use. */
    ADMA_TARGADDR(ch) = (uint32_t)buf;
    ADMA_EPNUM(ch)    = (uint32_t)ep & 0x7u;
    ADMA_REQSIZE(ch)  = (uint32_t)s_bulk_mps & 0xFFFu;
    ADMA_TOTCOUNT(ch) = bytes & 0xFFFFFFu;

    USB->ADMAEN |= mask;                       /* start                     */
    USB->DMACTRL |= USB_DMACTRL_DMAEN_Msk;
    USB_INDEX = 0u;
    usb_unlock(was);

    for (spins = 0u; spins < MSC_WAIT_SPINS; spins++) {
        uint32_t done = USB->ADMACMPINTSTAT & mask;
        uint32_t err  = USB->ADMAERRINTSTAT & mask;
        if (err) {
            uint32_t w2;
            USB->ADMAERRINTCLR = mask;
            USB->ADMAEN &= ~mask;
            w2 = usb_lock();
            USB_INDEX = (uint8_t)ep;
            if (is_in) { USB_TXCSR = TXCSR_MODE; }
            else       { USB_RXCSR = 0u; }
            USB_INDEX = 0u;
            usb_unlock(w2);
            s_n_adma_err++;
            return 0;
        }
        if (done) {
            USB->ADMACMPINTCLR = mask;
            USB->ADMAEN &= ~mask;
            /*
             * Hand the endpoint back to PIO.  With DMAReqEnab/DMAReqMode
             * left set, the next PIO access (the status wrapper or the next
             * command wrapper) meets a CSR still expecting a DMA request.
             */
            {
                uint32_t w2 = usb_lock();
                USB_INDEX = (uint8_t)ep;
                if (is_in) { USB_TXCSR = TXCSR_MODE; }
                else       { USB_RXCSR = 0u; }
                USB_INDEX = 0u;
                usb_unlock(w2);
            }
            s_n_adma++;
            if (!is_in) { tiku_cpu_dcache_invalidate(buf, bytes); }
            else {
                /* The engine has emptied the buffer into the FIFO; the last
                 * packet may still be on its way out, so wait for it before
                 * the status wrapper. */
                (void)msc_tx_wait();
            }
            return 1;
        }
        tiku_hang_checkin();
    }
    USB->ADMAEN &= ~mask;
    s_n_adma_err++;
    return 0;
}

/** @brief Wait until the IN endpoint has room: 1 when it has, 0 after
 *         MSC_WAIT_SPINS polls. */
static int msc_tx_wait(void)
{
    uint32_t spins;
    for (spins = 0u; spins < MSC_WAIT_SPINS; spins++) {
        uint32_t was = usb_lock();
        uint16_t csr;
        USB_INDEX = CDC_EP_DATA;
        csr = USB_TXCSR;
        USB_INDEX = 0u;
        usb_unlock(was);
        if (!(csr & TXCSR_TXPKTRDY)) { return 1; }
        tiku_hang_checkin();
    }
    return 0;
}

/** @brief Wait until a packet has arrived on the OUT endpoint. */
static int msc_rx_wait(void)
{
    uint32_t spins;
    for (spins = 0u; spins < MSC_WAIT_SPINS; spins++) {
        uint32_t was = usb_lock();
        uint16_t csr;
        USB_INDEX = CDC_EP_DATA;
        csr = USB_RXCSR;
        USB_INDEX = 0u;
        usb_unlock(was);
        if (csr & RXCSR_RXPKTRDY) { return 1; }
        tiku_hang_checkin();
    }
    return 0;
}

/** @brief Push @p n bytes out of the IN endpoint as one packet. */
static void msc_tx_raw(const uint8_t *p, uint16_t n)
{
    uint32_t was = usb_lock();
    uint16_t i;
    USB_INDEX = CDC_EP_DATA;
    for (i = 0u; i < n; i++) {
        *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA) = p[i];
    }
    USB_TXCSR = TXCSR_TXPKTRDY | TXCSR_MODE;
    USB_INDEX = 0u;
    usb_unlock(was);
    s_n_tx_bytes += n;
}

/**
 * @brief Pull one packet from the OUT endpoint; returns bytes taken.
 *
 * @p cap is uint32_t: the caller's remaining-space expression reaches
 * MSC_BOUNCE_BYTES (65536), one more than a uint16_t holds.
 */
static uint16_t msc_rx_raw(uint8_t *p, uint32_t cap)
{
    uint32_t was = usb_lock();
    uint16_t cnt, i;
    USB_INDEX = CDC_EP_DATA;
    cnt = (uint16_t)(R16(0x18) & 0x1FFFu);
    if ((uint32_t)cnt > cap) { cnt = (uint16_t)cap; }
    for (i = 0u; i < cnt; i++) {
        p[i] = *(volatile uint8_t *)&USB_FIFO(CDC_EP_DATA);
    }
    USB_RXCSR = 0u;                        /* clears RxPktRdy               */
    USB_INDEX = 0u;
    usb_unlock(was);
    s_n_rx_bytes += cnt;
    return cnt;
}

/** @brief Send the status wrapper from process context. */
static void msc_csw_poll(uint8_t status, uint32_t residue)
{
    uint8_t csw[CSW_LEN];

    tiku_usbd_msc_build_csw(csw, s_bot_tag, residue, status);
    if (msc_tx_wait()) { msc_tx_raw(csw, CSW_LEN); }
}

/** @brief Stream a READ(10) from the card, a bounce-buffer chunk at a time. */
static void msc_emmc_read(uint32_t lba, uint32_t bytes, uint32_t host_len)
{
    uint32_t done = 0u;

    while (done < bytes) {
        uint32_t chunk = bytes - done;
        uint32_t nblk, off;
        if (chunk > MSC_BOUNCE_BYTES) { chunk = MSC_BOUNCE_BYTES; }
        nblk = chunk / MSC_BLOCK_SIZE;
#if (TIKU_DRV_EMMC_ENABLE + 0)
        if (tiku_emmc_read_blocks(lba + (done / MSC_BLOCK_SIZE), nblk,
                                  s_disk) != TIKU_EMMC_OK)
#else
        if (1)
#endif
        {
            msc_fail(0x04u, 0x00u);            /* HARDWARE ERROR            */
            msc_csw_poll(1u, host_len - done);
            return;
        }
        if (s_adma && adma_run(CDC_EP_DATA, 1, s_disk, chunk)) {
            s_n_tx_bytes += chunk;
        } else {
            for (off = 0u; off < chunk; off += s_bulk_mps) {
                uint16_t n = (uint16_t)((chunk - off) < s_bulk_mps
                                        ? (chunk - off) : s_bulk_mps);
                if (!msc_tx_wait()) { return; }
                msc_tx_raw(&s_disk[off], n);
            }
        }
        done += chunk;
        tiku_hang_checkin();
    }
    msc_csw_poll(0u, host_len - bytes);
}

/** @brief Stream a WRITE(10) into the card the same way. */
static void msc_emmc_write(uint32_t lba, uint32_t bytes, uint32_t host_len)
{
    uint32_t done = 0u;

    while (done < bytes) {
        uint32_t chunk = bytes - done;
        uint32_t got = 0u, nblk;
        if (chunk > MSC_BOUNCE_BYTES) { chunk = MSC_BOUNCE_BYTES; }
        if (s_adma && adma_run(CDC_EP_DATA, 0, s_disk, chunk)) {
            got = chunk;
            s_n_rx_bytes += chunk;
        } else {
            while (got < chunk) {
                if (!msc_rx_wait()) { return; }
                got += msc_rx_raw(&s_disk[got], chunk - got);
                tiku_hang_checkin();
            }
        }
        nblk = chunk / MSC_BLOCK_SIZE;
        /* force=1: in MSC mode the host owns the medium.  The scratch
         * region lies outside the capacity the host was given, so the host
         * cannot reach it. */
#if (TIKU_DRV_EMMC_ENABLE + 0)
        if (tiku_emmc_write_blocks(lba + (done / MSC_BLOCK_SIZE), nblk,
                                   s_disk, 1) != TIKU_EMMC_OK)
#else
        if (1)
#endif
        {
            msc_fail(0x04u, 0x00u);
            msc_csw_poll(1u, host_len - done);
            return;
        }
        done += chunk;
    }
    msc_csw_poll(0u, host_len - bytes);
}

/**
 * @brief Process-context pump: serve SCSI commands until the host goes quiet.
 *
 * Runs from the shell's pump list.  Returns at once unless MSC is up over the
 * eMMC.
 */
void tiku_usb_msc_poll(void)
{
    /*
     * Serve commands until the host stops asking, at most 256 per call, so a
     * busy host cannot starve the shell indefinitely.
     */
    unsigned burst, idle;

    /*
     * Return at once unless MSC is up over the eMMC, so in any other mode a
     * shell pass does not spend the 2 ms turnaround wait below.
     */
    if (!s_up || !s_configured || s_class != TIKU_USB_CLASS_MSC ||
        s_store != MSC_STORE_EMMC) {
        return;
    }

    for (burst = 0u; burst < 256u; burst++) {
        if (msc_poll_one()) { continue; }
        /*
         * Bulk-Only Transport is ping-pong at the command level: the host
         * sends the next CBW only after the CSW arrives.  The pump waits up
         * to 2 ms (400 x 5 us) for that CBW before it returns; the next shell
         * poll comes TIKU_SHELL_POLL_TICKS (about 47 ms) later.
         */
        for (idle = 0u; idle < 400u; idle++) {
            tiku_cpu_ambiq_delay_us(5u);
            if (msc_poll_one()) { break; }
        }
        if (idle == 400u) { return; }   /* idle: let the shell run          */
    }
}

/** @brief Handle at most one SCSI command.  1 if one was handled. */
static int msc_poll_one(void)
{
    uint8_t cbw[CBW_LEN];
    uint16_t got;
    uint32_t was;
    uint16_t csr;
    tiku_usbd_msc_cbw_t c;
    tiku_usbd_msc_cmd_t cmd;

    if (!s_up || !s_configured || s_class != TIKU_USB_CLASS_MSC ||
        s_store != MSC_STORE_EMMC) {
        return 0;
    }

    was = usb_lock();
    USB_INDEX = CDC_EP_DATA;
    csr = USB_RXCSR;
    USB_INDEX = 0u;
    usb_unlock(was);
    if (!(csr & RXCSR_RXPKTRDY)) { return 0; }

    got = msc_rx_raw(cbw, CBW_LEN);
    if (!tiku_usbd_msc_parse_cbw(cbw, got, &c)) { return 1; }
    s_n_cbw++;
    s_bot_tag = c.tag;

    /* The decoder the ISR path uses as well. */
    tiku_usbd_msc_decode(&s_msc, &c, s_bot_reply, &cmd);

    if (cmd.action == TIKU_USBD_MSC_ACT_READ) {
        s_n_rd++;
        msc_emmc_read(cmd.lba, cmd.bytes, c.host_len);
        return 1;
    }
    if (cmd.action == TIKU_USBD_MSC_ACT_WRITE) {
        s_n_wr++;
        msc_emmc_write(cmd.lba, cmd.bytes, c.host_len);
        return 1;
    }

    /* Everything else is memory-resident: ship the reply, then the status.
     * A refused command arrives here too, with len 0 and status 1. */
    if (cmd.len && msc_tx_wait()) { msc_tx_raw(s_bot_reply, cmd.len); }
    msc_csw_poll(cmd.status, cmd.residue);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* BUS EVENTS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bus reset: put everything back the way the host expects to find it.
 *
 * The controller does not reset its own state (table 4 in the header); stale
 * state here shows as a device that enumerates once and fails on replug.
 */
static void bus_reset(void)
{
    s_n_reset++;

    s_ep0    = EP0_IDLE;
    s_tx_len = 0u;
    s_pending_addr = 0u;
    s_addr   = 0u;
    s_config = 0u;

    /* The configuration is gone and so is the FIFO allocation; endpoints are
     * re-opened only when the host configures the device again. */
    s_configured = 0u;
    s_dtr = 0u;
    s_tx_busy = 0u;
    s_rx_stalled = 0u;
    s_tx_head = s_tx_tail = 0u;
    s_rx_head = s_rx_tail = 0u;

    USB_FADDR = 0u;
    USB_INDEX = 0u;
    USB_TXMAXP = TIKU_USB_EP0_MAXPACKET;

    /* Per-endpoint interrupts are re-enabled by SET_CONFIGURATION; only EP0
     * is armed here (EP0 lives in the TX enable register, bit 0). */
    USB_INTRTXE = 0x0001u;
    USB_INTRRXE = 0x0000u;

    /* Speed is the hardware's answer to the chirp handshake, read back from
     * POWER.HSMode. */
    s_speed = (USB_POWER & POWER_HSMODE) ? TIKU_USB_SPEED_HIGH
                                         : TIKU_USB_SPEED_FULL;
    /* Bulk max-packet is fixed by the spec at each speed, so the descriptor
     * the host asks for next depends on the speed read here. */
    s_bulk_mps = (s_speed == TIKU_USB_SPEED_HIGH) ? CDC_BULK_MPS_HS
                                                  : CDC_BULK_MPS_FS;
}

/**
 * @brief The USB interrupt.  Bounded, allocation-free, and it never prints.
 *
 * Read each status register exactly once.  They are read-to-clear; a second
 * read returns zero and loses whatever arrived in between.  Everything below
 * consults the locals, never the registers again.
 */
void tiku_ambiq_usb_isr(void)
{
    uint8_t  intrusb = USB_INTRUSB;    /* read-to-clear: once, into a local  */
    uint16_t intrtx  = USB_INTRTX;     /* read-to-clear                      */
    uint16_t intrrx  = USB_INTRRX;     /* read-to-clear                      */

    s_n_irq++;

    if (intrusb & INTRUSB_RESET) {
        bus_reset();
        intrusb &= (uint8_t)~INTRUSB_SUSPEND;
    }
    if (intrusb & INTRUSB_RESUME)  { s_n_resume++; }
    if (intrusb & INTRUSB_SUSPEND) { s_n_suspend++; }

    if (intrtx & 0x0001u) { ep0_irq(); }

    /* EP1 carries whichever class is presented; route by that. */
    if (intrtx & (1u << CDC_EP_DATA)) {
        if (s_class == TIKU_USB_CLASS_MSC) { msc_tx_done(); }
        else                               { cdc_tx_fill(); }
    }
    if (intrrx & (1u << CDC_EP_DATA)) {
        if (s_class == TIKU_USB_CLASS_MSC) { msc_rx_packet(); }
        else                               { cdc_rx_pump(); }
    }
    USB_INDEX = 0u;   /* INDEX is left on EP0 between accesses             */
}

/*---------------------------------------------------------------------------*/
/* BRING-UP (table 2)                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Drive one of the external USB rail switches. */
static void rail(uint32_t pad, int on)
{
    /* Push-pull output, 0.5x drive -- the same pad recipe the other Ambiq
     * drivers use for a plain GPIO output. */
    tiku_ambiq_gpio_pad_config(pad, 3u | (1u << 10) | (1u << 8) | (1u << 4));
    tiku_ambiq_gpio_set(pad, on ? 1u : 0u);
}

tiku_usb_err_t tiku_usb_up(tiku_usb_speed_t want)
{
    return tiku_usb_up_as(want, TIKU_USB_CLASS_CDC);
}

tiku_usb_err_t tiku_usb_up_as(tiku_usb_speed_t want, tiku_usb_class_t cls)
{
    return tiku_usb_up_full(want, cls, 0);
}

/**
 * @brief Bring up, choosing the MSC backing store as well.
 *
 * In eMMC mode the host is told the card is SCRATCH_BLOCKS shorter than it is,
 * so the scratch region is outside the medium as far as the host can tell and
 * no partition table or format it writes can reach it.
 */
tiku_usb_err_t tiku_usb_up_full(tiku_usb_speed_t want, tiku_usb_class_t cls,
                                int use_emmc)
{
    uint32_t spins;
    const int hs = (want == TIKU_USB_SPEED_HIGH);

    if (s_up) { return TIKU_USB_OK; }
    if (cls != TIKU_USB_CLASS_CDC && cls != TIKU_USB_CLASS_MSC) {
        return TIKU_USB_ERR_ARG;
    }
    s_class = cls;
    s_store = MSC_STORE_RAM;
    s_msc.blocks  = MSC_DISK_BLOCKS;
    s_msc.product = "RAM Disk";
    if (cls == TIKU_USB_CLASS_MSC && use_emmc) {
#if (TIKU_DRV_EMMC_ENABLE + 0)
        uint32_t cap = tiku_emmc_capacity_blocks();
        if (cap <= TIKU_EMMC_SCRATCH_BLOCKS) {
            /* Card not identified, or no larger than the scratch region. */
            return TIKU_USB_ERR_STATE;
        }
        s_store = MSC_STORE_EMMC;
        s_msc.blocks  = cap - TIKU_EMMC_SCRATCH_BLOCKS;
        s_msc.product = "eMMC 8GB";
#else
        return TIKU_USB_ERR_ARG;
#endif
    }
    if (want != TIKU_USB_SPEED_FULL && want != TIKU_USB_SPEED_HIGH) {
        return TIKU_USB_ERR_ARG;
    }
    s_want = want;

    /* 1+2. The controller and the PHY power domains. */
    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENUSB_Msk |
                         PWRCTRL_DEVPWREN_PWRENUSBPHY_Msk;
    __DSB();
    for (spins = 0u; spins < 100000u; spins++) {
        uint32_t want = PWRCTRL_DEVPWRSTATUS_PWRSTUSB_Msk |
                        PWRCTRL_DEVPWRSTATUS_PWRSTUSBPHY_Msk;
        if ((PWRCTRL->DEVPWRSTATUS & want) == want) { break; }
    }
    if (spins == 100000u) { return TIKU_USB_ERR_POWER; }

    /* 3. The undocumented FIFO-SRAM trim, with the vendor's values. */
    USB->SRAMCTRL = (1u  << USB_SRAMCTRL_WABL_Pos)  |
                    (1u  << USB_SRAMCTRL_WABLM_Pos) |
                    (1u  << USB_SRAMCTRL_RAWL_Pos)  |
                    (2u  << USB_SRAMCTRL_RAWLM_Pos) |
                    (0u  << USB_SRAMCTRL_EMAW_Pos)  |
                    (0u  << USB_SRAMCTRL_EMAS_Pos)  |
                    (3u  << USB_SRAMCTRL_EMA_Pos)   |
                    (1u  << USB_SRAMCTRL_RET1N_Pos);
    __DSB();

    /* 4. Hold the PHY in reset: clearing these bits holds it (the vendor's
     *    enable_phy_reset_override). */
    MCUCTRL->USBRSTCTRL &= ~(MCUCTRL_USBRSTCTRL_USBRSTENABLE_Msk |
                             MCUCTRL_USBRSTCTRL_USBPORRSTRELEASE_Msk |
                             MCUCTRL_USBRSTCTRL_USBUTMIRSTRELEASE_Msk);
    __DSB();

    /* 5+6. The external rails, then the settle wait.  With the rails off
     *      the registers read back normally and the host sees no device. */
    rail(TIKU_USB_PAD_VDDUSB33, 1);
    rail(TIKU_USB_PAD_VDDUSB0P9, 1);
    tiku_cpu_ambiq_delay_us(TIKU_USB_RAIL_SETTLE_MS * 1000u);

    /* 7. Disconnect battery-charger detection from D+/D-.  Left connected,
     *    enumeration fails with no error reported anywhere. */
    USB->BCDETCRTL1 = (1u << USB_BCDETCRTL1_USBSWRESET_Pos);
    __DSB();

    /* 8. Release the PHY. */
    MCUCTRL->USBRSTCTRL |= (MCUCTRL_USBRSTCTRL_USBRSTENABLE_Msk |
                            MCUCTRL_USBRSTCTRL_USBPORRSTRELEASE_Msk |
                            MCUCTRL_USBRSTCTRL_USBUTMIRSTRELEASE_Msk);
    __DSB();
    tiku_cpu_ambiq_delay_us(1000u);

    /* 9. PHY reference clock; the two speeds take different sources. */
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_USB, CLKGEN_MISC_FRCHFRC_Msk);
    __DSB();
    if (hs) {
        /*
         * Without its own HS crystal the board needs the EM9305 die out of
         * reset: the die's crystal is the 12 MHz source, and without a
         * reference the PHY cannot present the pull-up, so the host sees
         * nothing.  The die is booted once; the clock stays up while the die
         * does.
         */
#if (TIKU_BOARD_HAS_USBHS_CLK_XTAL + 0)
        /*
         * Boards with their own high-speed crystal take it directly: 48 MHz
         * halved to the PHY's 24 MHz reference (table 5: XTAL_HS 48 MHz ->
         * XTALHS_DIV2), with no EM9305, clock request, external pad or x20
         * multiplier.
         */
        USB->CLKCTRL = ((uint32_t)USB_CLKCTRL_PHYREFCLKSEL_XTALHS_DIV2
                        << USB_CLKCTRL_PHYREFCLKSEL_Pos);
        __DSB();
#else
        if (!s_em9305_up) {
#if (TIKU_DRV_BLE_EM9305_ENABLE + 0)
            /* Taken, not reset: a die the BLE stack already runs keeps its
             * links, and `bt off` leaves it powered for this clock. */
            if (tiku_em9305_acquire(TIKU_EM9305_USER_USB) != 0) {
                return TIKU_USB_ERR_CLOCK;
            }
            s_em9305_up = 1u;
#else
            /* Without the EM9305 driver the clock source cannot start, so
             * high speed is refused. */
            return TIKU_USB_ERR_CLOCK;
#endif
        }

        /* Ask the EM9305 die for its 12 MHz and open the pin it arrives on.
         * Writing CLKCTRL first ungates the PHY's APB clock, without which
         * the USBPHY register below is not reachable. */
        tiku_ambiq_gpio_pad_config(TIKU_USB_PAD_CLKREQ,
                                   3u | (1u << 10) | (1u << 8) | (1u << 4));
        tiku_ambiq_gpio_set(TIKU_USB_PAD_CLKREQ, 1u);
        tiku_ambiq_gpio_pad_config(TIKU_USB_PAD_REFCLK,
                                   PAD_FNCSEL_REFCLK | (1u << 4));
        tiku_cpu_ambiq_delay_us(TIKU_USB_REFCLK_SETTLE_US);

        USB->CLKCTRL = ((uint32_t)USB_CLKCTRL_PHYREFCLKSEL_EXTREFCLK
                        << USB_CLKCTRL_PHYREFCLKSEL_Pos);
        __DSB();
        /* x20 rather than the default x40: the reference is 12 MHz, not 24.
         * USBPHY is a separate peripheral with no read-to-clear fields, so
         * the CMSIS accessor is safe here in a way it is not for USB. */
        USBPHY->REG14_b.BF55 = 1u;
        __DSB();
#endif  /* TIKU_BOARD_HAS_USBHS_CLK_XTAL */
    } else {
        /* Full speed takes HFRC at 24 MHz: internal, always present, no
         * crystal, no radio die, no request line. */
        USB->CLKCTRL = ((uint32_t)USB_CLKCTRL_PHYREFCLKSEL_HFRC_24MHz
                        << USB_CLKCTRL_PHYREFCLKSEL_Pos);
        __DSB();
    }
    tiku_cpu_ambiq_delay_us(1000u);

    /* 10. Speed.  HSEnab only allows high speed: the chirp handshake in
     *     hardware decides, and POWER.HSMode reports the result after the
     *     bus reset.  Byte-width read-modify-write (table 1). */
    if (hs) {
        USB_POWER = (uint8_t)(USB_POWER | POWER_HSENAB | POWER_ENSUSPM);
    } else {
        USB_POWER = (uint8_t)((USB_POWER & (uint8_t)~POWER_HSENAB)
                              | POWER_ENSUSPM);
    }

    /* 11. Arm bus-event interrupts and EP0; clear anything stale by reading
     *     the read-to-clear registers once. */
    (void)USB_INTRUSB;
    (void)USB_INTRTX;
    (void)USB_INTRRX;
    USB_INTRUSBE = INTRUSB_RESET | INTRUSB_RESUME | INTRUSB_SUSPEND;
    USB_INTRTXE  = 0x0001u;
    USB_INTRRXE  = 0x0000u;

    USB_INDEX = 0u;
    USB_TXMAXP = TIKU_USB_EP0_MAXPACKET;
    s_ep0 = EP0_IDLE;
    s_speed = TIKU_USB_SPEED_NONE;

    NVIC_SetPriority(USB0_IRQn, 4u);
    NVIC_ClearPendingIRQ(USB0_IRQn);
    NVIC_EnableIRQ(USB0_IRQn);

    s_up = 1u;

    /*
     * Register the MSC pump with the shell here, so a build that never brings
     * USB up never runs it; tiku_usb_down() removes it.  Registration is
     * idempotent.
     */
    (void)tiku_shell_add_pump(tiku_usb_msc_poll);
    return TIKU_USB_OK;
}

tiku_usb_err_t tiku_usb_attach(int on)
{
    if (!s_up) { return TIKU_USB_ERR_STATE; }
    /* Byte-width read-modify-write: a 32-bit access here clears INTRTX. */
    if (on) { USB_POWER = (uint8_t)(USB_POWER |  POWER_SOFTCONN); }
    else    { USB_POWER = (uint8_t)(USB_POWER & (uint8_t)~POWER_SOFTCONN); }
    s_attached = on ? 1u : 0u;
    return TIKU_USB_OK;
}

int tiku_usb_uses_em9305_clock(void)
{
#if (TIKU_BOARD_HAS_USBHS_CLK_XTAL + 0)
    return 0;
#else
    return s_up && s_want == TIKU_USB_SPEED_HIGH;
#endif
}

void tiku_usb_down(void)
{
    /* Stop being pumped before tearing anything down, so the shell cannot
     * call into a half-released controller on its next pass. */
    tiku_shell_remove_pump(tiku_usb_msc_poll);

    if (s_up) {
        (void)tiku_usb_attach(0);
        NVIC_DisableIRQ(USB0_IRQn);
        MCUCTRL->USBRSTCTRL &= ~(MCUCTRL_USBRSTCTRL_USBRSTENABLE_Msk |
                                 MCUCTRL_USBRSTCTRL_USBPORRSTRELEASE_Msk |
                                 MCUCTRL_USBRSTCTRL_USBUTMIRSTRELEASE_Msk);
        rail(TIKU_USB_PAD_VDDUSB0P9, 0);
        rail(TIKU_USB_PAD_VDDUSB33, 0);
    }
    PWRCTRL->DEVPWREN &= ~(PWRCTRL_DEVPWREN_PWRENUSB_Msk |
                           PWRCTRL_DEVPWREN_PWRENUSBPHY_Msk);
    __DSB();
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_USB, 0u);
    s_up = 0u; s_attached = 0u; s_speed = TIKU_USB_SPEED_NONE;
}

/*---------------------------------------------------------------------------*/
/* THE CONSOLE BACKEND                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The CDC console as a tiku_shell_io_t backend.
 */

/**
 * @brief Queue one byte for the host.
 *
 * Gated on DTR, so output to a port nobody has opened neither blocks nor
 * fills the ring.  With the terminal open, a full ring is waited on for up to
 * ~2 s, then the byte is dropped and counted for `power usb state`.
 */
void tiku_usb_cdc_putc(char c)
{
    uint16_t next;
    uint32_t guard = 0u;

    if (!s_configured || !s_dtr) { return; }

    next = (uint16_t)((s_tx_head + 1u) % CDC_TX_RING);
    while (next == s_tx_tail) {
        if (++guard > 200000u) { s_n_tx_drop++; return; }
        /* The ISR drains; give it the chance to. */
        tiku_cpu_ambiq_delay_us(10u);
    }
    s_txbuf[s_tx_head] = (uint8_t)c;
    s_tx_head = next;

    if (!s_tx_busy) {
        uint32_t was = usb_lock();
        if (!s_tx_busy) { cdc_tx_fill(); USB_INDEX = 0u; }
        usb_unlock(was);
    }
}

/** @brief Non-zero when a received byte is waiting. */
static uint8_t cdc_rx_ready(void)
{
    if (s_rx_head == s_rx_tail) {
        /* Empty ring is also the moment to check whether a packet is sitting
         * in the FIFO because the pump previously ran out of room. */
        cdc_rx_resume();
    }
    return (uint8_t)(s_rx_head != s_rx_tail);
}

/** @brief Pop one received byte, or -1. */
static int cdc_getc(void)
{
    int c;
    if (s_rx_head == s_rx_tail) { cdc_rx_resume(); }
    if (s_rx_head == s_rx_tail) { return -1; }
    c = (int)s_rxbuf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % CDC_RX_RING);
    /* Space has just appeared: if reception was backed off, resume it. */
    cdc_rx_resume();
    return c;
}

const tiku_shell_io_t tiku_shell_io_usbcdc = {
    tiku_usb_cdc_putc,
    cdc_rx_ready,
    cdc_getc,
    TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO,
    TIKU_VFS_CAP_ALL          /* a cable in the board is physical presence  */
};

int tiku_usb_cdc_ready(void)
{
    return (s_configured && s_dtr) ? 1 : 0;
}

/**
 * @brief Drain the CDC receive pipe until @p ms pass with no data, counting
 *        bytes and interpreting none of them.
 *
 * Uses the ring and flow-control path the shell uses, so the bytes the host
 * sent must equal the count returned.
 */
uint32_t tiku_usb_cdc_sink(uint32_t ms)
{
    uint32_t got = 0u, idle = 0u;

    while (idle < ms) {
        if (cdc_rx_ready()) {
            /* Drain what is already in the ring without re-checking the
             * resume condition per byte; cdc_rx_ready() does that once. */
            while (s_rx_tail != s_rx_head) {
                s_rx_tail = (uint16_t)((s_rx_tail + 1u) % CDC_RX_RING);
                got++;
            }
            cdc_rx_resume();
            idle = 0u;
        } else {
            tiku_cpu_ambiq_delay_us(1000u);
            idle++;
            tiku_hang_checkin();
        }
    }
    return got;
}

void tiku_usb_cdc_stats(uint32_t *tx, uint32_t *rx, uint32_t *drop,
                        uint32_t *nak)
{
    if (tx)   { *tx   = s_n_tx_bytes; }
    if (rx)   { *rx   = s_n_rx_bytes; }
    if (drop) { *drop = s_n_tx_drop;  }
    if (nak)  { *nak  = s_n_nak;      }
}

/*---------------------------------------------------------------------------*/
/* OBSERVABILITY                                                             */
/*---------------------------------------------------------------------------*/

int tiku_usb_powered(void)
{
    return ((PWRCTRL->DEVPWRSTATUS & PWRCTRL_DEVPWRSTATUS_PWRSTUSB_Msk) != 0u)
           ? 1 : 0;
}

int              tiku_usb_attached(void) { return s_attached ? 1 : 0; }
tiku_usb_speed_t tiku_usb_speed(void)    { return s_speed; }
tiku_usb_speed_t tiku_usb_want(void)     { return s_want;  }
tiku_usb_class_t tiku_usb_class(void)    { return s_class; }

int tiku_usb_msc_owns_emmc(void)
{
    return (s_up && s_class == TIKU_USB_CLASS_MSC &&
            s_store == MSC_STORE_EMMC) ? 1 : 0;
}

void tiku_usb_msc_adma(int on) { s_adma = on ? 1u : 0u; }
int  tiku_usb_msc_adma_on(void) { return s_adma ? 1 : 0; }

void tiku_usb_msc_dma_stats(uint32_t *xfers, uint32_t *errs)
{
    if (xfers) { *xfers = s_n_adma; }
    if (errs)  { *errs  = s_n_adma_err; }
}

void tiku_usb_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                        uint32_t *blocks)
{
    if (cbw)    { *cbw    = s_n_cbw; }
    if (rd)     { *rd     = s_n_rd;  }
    if (wr)     { *wr     = s_n_wr;  }
    if (blocks) { *blocks = s_msc.blocks; }
}

/**
 * @brief Exercise the LBA bounds check on cases it must accept and refuse.
 *
 * The refused cases include two lba + nblk sums that wrap past 2^32; the
 * check guards the memory after the RAM disk.
 *
 * @return 0 if every case behaved; otherwise a bitmask with bit n set for
 *         each case n that did not.
 */
uint32_t tiku_usb_msc_selftest(void)
{
    uint32_t bad = 0u;
    tiku_usbd_msc_t disk = s_msc;
    disk.blocks = MSC_DISK_BLOCKS;
    /* must be accepted */
    if (!tiku_usbd_msc_lba_ok(&disk, 0u, 1u))                        { bad |= 1u << 0; }
    if (!tiku_usbd_msc_lba_ok(&disk, MSC_DISK_BLOCKS - 1u, 1u))      { bad |= 1u << 1; }
    if (!tiku_usbd_msc_lba_ok(&disk, 0u, MSC_DISK_BLOCKS))           { bad |= 1u << 2; }
    /* must be refused */
    if (tiku_usbd_msc_lba_ok(&disk, MSC_DISK_BLOCKS, 1u))            { bad |= 1u << 3; }
    if (tiku_usbd_msc_lba_ok(&disk, MSC_DISK_BLOCKS - 1u, 2u))       { bad |= 1u << 4; }
    if (tiku_usbd_msc_lba_ok(&disk, 0u, MSC_DISK_BLOCKS + 1u))       { bad |= 1u << 5; }
    /* the overflow pair: lba + nblk wraps to 0 */
    if (tiku_usbd_msc_lba_ok(&disk, 0xFFFFFF00u, 0x100u))            { bad |= 1u << 6; }
    if (tiku_usbd_msc_lba_ok(&disk, 0x80000000u, 0x80000000u))       { bad |= 1u << 7; }
    return bad;
}

uint32_t tiku_usb_msc_hash_blocks(uint32_t nblocks)
{
    return (nblocks == 0u || nblocks > MSC_DISK_BLOCKS)
         ? MSC_DISK_BLOCKS : nblocks;
}

/** @brief FNV-1a over the first @p nblocks of the RAM disk (0 or too many =
 *         all). */
uint32_t tiku_usb_msc_hash(uint32_t nblocks)
{
    uint32_t h = 2166136261u, i, n;
    nblocks = tiku_usb_msc_hash_blocks(nblocks);
    n = nblocks * MSC_BLOCK_SIZE;
    for (i = 0u; i < n; i++) { h = (h ^ s_disk[i]) * 16777619u; }
    return h;
}
uint8_t          tiku_usb_address(void)  { return s_addr; }
uint8_t          tiku_usb_config(void)   { return s_config; }

void tiku_usb_counters(tiku_usb_counters_t *out)
{
    unsigned i;
    if (!out) { return; }
    out->irq      = s_n_irq;
    out->reset    = s_n_reset;
    out->setup    = s_n_setup;
    out->stall    = s_n_stall;
    out->setupend = s_n_setupend;
    out->suspend  = s_n_suspend;
    out->resume   = s_n_resume;
    out->last_req = s_last_req;
    for (i = 0u; i < 4u; i++) { out->stalled[i] = s_stalled[i]; }
}

void tiku_usb_regs(uint32_t *out, unsigned n)
{
    unsigned i;

    /* Reading an unpowered peripheral stalls the APB and hangs the CPU with
     * no fault, so only DEVPWRSTATUS is read while the controller is down. */
    for (i = 0u; i < n; i++) { out[i] = 0xDEADDEADu; }
    if (n > 0u) { out[0] = PWRCTRL->DEVPWRSTATUS; }
    if (!tiku_usb_powered()) { return; }
    if (n > 1u) { out[1] = MCUCTRL->USBRSTCTRL; }
    if (n > 2u) { out[2] = USB->CLKCTRL; }
    if (n > 3u) { out[3] = USB_POWER; }
    if (n > 4u) { out[4] = USB_FADDR; }
    if (n > 5u) { out[5] = USB_INTRUSBE; }
    if (n > 6u) { out[6] = USB_INTRTXE; }
    if (n > 7u) { out[7] = USB_FRAME; }
    if (n > 8u) { out[8] = USBPHY->REG14; }
    /* Not dumped: INTRUSB, INTRTX, INTRRX.  They are read-to-clear, and a
     * read here takes events from the ISR. */
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_USB_ENABLE */
