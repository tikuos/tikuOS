/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_arch.c - Apollo510 USB device controller: bring-up and enumeration.
 *
 * Never uses a CMSIS bitfield accessor on CFG0/CFG1/CFG2: those words pack
 * read-to-clear interrupt status, so a read-modify-write silently discards pending
 * interrupts.  Interrupt-driven by necessity -- a polled device would drop off the bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(PLATFORM_AMBIQ) && (TIKU_DRV_USB_ENABLE + 0)

#include "tiku_usb_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_cpu_common.h"
#include "apollo510.h"
#include <kernel/shell/tiku_shell_io.h>   /* the console backend, below     */
#include <kernel/shell/tiku_shell.h>      /* tiku_shell_add_pump(): S4      */
#include <kernel/vfs/tiku_vfs.h>          /* TIKU_VFS_CAP_ALL               */
#include <kernel/cpu/tiku_hang.h>         /* the sink blocks on purpose     */
#include "hal/tiku_cpu.h"                 /* dcache maintenance for ADMA    */
#if (TIKU_DRV_BLE_EM9305_ENABLE + 0)
#include "tiku_em9305.h"        /* HS needs the die's 12 MHz -- see below   */
#endif
#if (TIKU_DRV_EMMC_ENABLE + 0)
#include "tiku_emmc_arch.h"     /* MSC's real backing store (U4)           */
#endif

/*---------------------------------------------------------------------------*/
/* REGISTER ACCESS -- TRUE WIDTHS ONLY (table 1)                             */
/*---------------------------------------------------------------------------*/
/*
 * Every one of these is a real MUSB register that CMSIS happens to have
 * packed into a wider word.  Going through these macros rather than the
 * CMSIS struct is not a style preference; it is the difference between a
 * driver that works and one that enumerates nine times in ten.
 */
#define R8(off)    (*(volatile uint8_t  *)(USB_BASE + (off)))
#define R16(off)   (*(volatile uint16_t *)(USB_BASE + (off)))

#define USB_FADDR       R8(0x00)
#define USB_POWER       R8(0x01)
#define USB_INTRTX      R16(0x02)   /* READ-TO-CLEAR */
#define USB_INTRRX      R16(0x04)   /* READ-TO-CLEAR */
#define USB_INTRTXE     R16(0x06)
#define USB_INTRRXE     R16(0x08)
#define USB_INTRUSB     R8(0x0A)    /* READ-TO-CLEAR */
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
 * THE 12 MHz REFERENCE, AND WHERE IT COMES FROM (see the header's table 5,
 * corrected in U1b).  High speed needs a PHY reference this board does not
 * have a crystal for -- but it does have a 12 MHz clock, and the BSP names
 * its request line outright: AM_BSP_GPIO_AP5_12M_CLKREQ = 136.  The source
 * is the EM9305 BLE die in the package:
 *
 *   GP136  out  assert to ASK the die for its 12 MHz output
 *   GP15   in   funcsel 10 (REFCLK_EXT) -- where it arrives
 *
 * The PHY expects 24 MHz and multiplies by 40; at 12 MHz it must multiply by
 * 20 instead, which is USBPHY REG14 bit BF55.  Getting that wrong would not
 * fail cleanly -- it would run the PHY at the wrong rate and fail the chirp.
 */
/* Only boards whose HS reference is the EM9305 have these wires at all; a
 * board with its own HS crystal deliberately defines neither (see the green
 * EVB header).  Guarded so a missing definition is a compile error naming the
 * board contract, not a pad number invented at 2 a.m. */
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

/* CSR0 (INDEX == 0).  The EP0 meanings of the shared CSR bits -- see the
 * header: CMSIS names each bit for BOTH its EP0 and its EP1-5 role. */
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
 * this driver never sends.  Bulk max-packet is NOT a free choice: USB 2.0
 * fixes it at 512 for high speed and allows at most 64 at full speed, so the
 * configuration descriptor genuinely differs between the two and is patched
 * at request time, once the chirp has settled the speed.
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
 * VID/PID: 0x1209/0x0001 is pid.codes' explicitly reserved
 * TESTING-AND-DEVELOPMENT pair.  It must NOT ship in a product -- a real
 * allocation is a Tiku AB line item, recorded in the plan.
 */
#define TIKU_USB_VID   0x1209u
#define TIKU_USB_PID   0x0001u

#define DESC_DEVICE     1u
#define DESC_CONFIG     2u
#define DESC_STRING     3u
#define DESC_INTERFACE  4u
#define DESC_ENDPOINT   5u

static uint8_t s_desc_device[18] = {
    18, DESC_DEVICE,
    0x00, 0x02,             /* bcdUSB 2.00                                   */
    0x02, 0x00, 0x00,       /* class 02 = Communications, at DEVICE level    */
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
 * two bulk pipes that actually move the console.  67 bytes total.
 *
 * The two wMaxPacketSize fields of the BULK endpoints are patched at request
 * time (see ep0_get_descriptor) because their legal value depends on the
 * negotiated speed.  Their offsets are asserted below rather than trusted:
 * a descriptor edit that shifted them would otherwise corrupt a neighbouring
 * field and produce a device that enumerates but will not carry data.
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

/* The patch offsets must land on the wMaxPacketSize of an ENDPOINT
 * descriptor whose bDescriptorType is 5.  Checked at compile time. */
_Static_assert(CFG_OFF_BULK_OUT_MPS + 1u < CFG_LEN, "bulk OUT mps offset");
_Static_assert(CFG_OFF_BULK_IN_MPS  + 1u < CFG_LEN, "bulk IN mps offset");

/*
 * MSC: one interface, two bulk endpoints, nothing else.  Markedly simpler
 * than CDC because mass storage carries its command set INSIDE the bulk
 * pipes rather than in class-specific descriptors.
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

/* A device presenting mass storage must NOT claim a class at device level;
 * the interface carries it.  CDC does the opposite.  Patched at request. */
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
 * U3 deliberately backs mass storage with SSRAM rather than the eMMC.  The
 * two have entirely separate failure modes and debugging them together is the
 * entangled-variables mistake `emmcdiag` was written to escape: a host that
 * will not mount could be a CBW parser bug or a block driver bug, and there
 * would be no way to tell.  With a RAM disk, a mount failure has exactly one
 * suspect.  U4 swaps the backing store and nothing else.
 *
 * *** THE SEAM U4 HAS TO BREAK. ***  A RAM disk answers a read with a
 * pointer, so the whole transport can live in the ISR.  The eMMC answers in
 * MILLISECONDS -- 14.5 ms for a 512 KB chunk, and even one block is far too
 * long to hold an interrupt.  U4 must therefore move the data phase into
 * process context and leave only the packet handshake here.  That is a
 * planned change, not a surprise; it is written down so it is not discovered.
 */

/* Bulk-Only Transport wrappers */
#define CBW_SIG   0x43425355u      /* "USBC" */
#define CSW_SIG   0x53425355u      /* "USBS" */
#define CBW_LEN   31u
#define CSW_LEN   13u

/* SCSI opcodes -- the subset a host actually issues to mount a disk */
#define SCSI_TEST_UNIT_READY   0x00u
#define SCSI_REQUEST_SENSE     0x03u
#define SCSI_INQUIRY           0x12u
#define SCSI_MODE_SENSE6       0x1Au
#define SCSI_START_STOP        0x1Bu
#define SCSI_PREVENT_ALLOW     0x1Eu
#define SCSI_READ_CAPACITY10   0x25u
#define SCSI_READ10            0x28u
#define SCSI_WRITE10           0x2Au
#define SCSI_MODE_SENSE10      0x5Au
#define SCSI_SYNC_CACHE10      0x35u

#define MSC_BLOCK_SIZE   512u
#define MSC_DISK_BYTES   (1024u * 1024u)
#define MSC_DISK_BLOCKS  (MSC_DISK_BYTES / MSC_BLOCK_SIZE)

/*
 * ONE BUFFER, TWO ROLES, NEVER BOTH AT ONCE.  In RAM-disk mode this IS the
 * disk; in eMMC mode it is the bounce buffer between the card and the bulk
 * pipes.  The modes are mutually exclusive by construction (the backing store
 * is chosen at bring-up and cannot change while attached), so sharing the
 * megabyte is honest rather than a trick -- and it keeps SSRAM for the tier.
 */
static uint8_t s_disk[MSC_DISK_BYTES] __attribute__((section(".ssram")));

/** Bounce chunk for eMMC mode: 64 KB was E3's efficiency knee (34.5 MB/s). */
#define MSC_BOUNCE_BYTES  (64u * 1024u)
/* The bounce chunk is the bound on every remaining-space expression in the
 * eMMC data path.  It exceeds a uint16_t, which is exactly how the write
 * path broke once; keep that fact in the build rather than in memory. */
_Static_assert(MSC_BOUNCE_BYTES > 65535u,
               "bounce exceeds 16 bits -- remaining-space vars must be 32-bit");
_Static_assert(MSC_BOUNCE_BYTES <= MSC_DISK_BYTES,
               "bounce must fit the shared store");

/** @brief Which store MSC presents. */
typedef enum { MSC_STORE_RAM = 0, MSC_STORE_EMMC } msc_store_t;
static msc_store_t s_store = MSC_STORE_RAM;

/** @brief Blocks the host is told about -- see msc_capacity(). */
static uint32_t s_msc_blocks = MSC_DISK_BLOCKS;

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
static uint8_t  s_bot_reply[64];     /**< INQUIRY / sense / capacity        */
static uint8_t  s_sense_key, s_sense_asc;
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

/* Observability counters -- an ISR cannot print, so it counts.  These are
 * the whole diagnostic surface for U1 and they are enough: a host that never
 * enumerates leaves a very specific fingerprint in them. */
static volatile uint32_t s_n_reset, s_n_setup, s_n_irq, s_n_stall,
                         s_n_suspend, s_n_resume, s_n_setupend;
static volatile uint16_t s_last_req;   /**< bRequest<<8 | bmRequestType      */

/*
 * WHICH requests are refused, not merely how many.  A stall count alone cannot
 * distinguish "correctly declining DEVICE_QUALIFIER on a full-speed device"
 * from "failing to implement something the host needs", and those look
 * identical from the outside until enumeration breaks.  The ring records the
 * last four, as bRequest<<8 | (wValue >> 8) -- for GET_DESCRIPTOR the low byte
 * is the descriptor TYPE, which is the informative half.
 */
static volatile uint16_t s_stalled[4];
static volatile uint8_t  s_stall_wr;
static uint8_t s_cur_req, s_cur_type;

/*---------------------------------------------------------------------------*/
/* CDC RINGS -- and the flow control that makes a 1 MB paste survivable      */
/*---------------------------------------------------------------------------*/
/*
 * Two rings sit between the ISR and the shell process.  The RX one is where
 * flow control lives, and it is the reason this class is worth building: the
 * UART console drops bytes under a large paste (79 of them, on record) because
 * a UART has nowhere to push back from.  USB does.
 *
 * If the RX ring cannot take a whole max-packet, the ISR does NOT unload the
 * FIFO -- it masks the endpoint's interrupt and leaves the packet where it
 * is.  The controller then NAKs, the host retries, and the transfer simply
 * paces itself to whatever rate the shell can consume.  Nothing is dropped
 * because nothing is accepted that cannot be stored.  The drain side
 * re-enables the interrupt and pumps the FIFO once space appears.
 *
 * In SSRAM rather than DTCM: 8 KB of console buffering is not worth spending
 * the tightly-coupled bank on, and no DMA touches these (the FIFO is PIO), so
 * there is no coherency question.
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

/* Defined below with the endpoint code; used by the EP0 state machine above
 * it, which is the natural order to READ the file in even though it is not
 * the order C requires. */
static void cdc_endpoints_open(void);
static void cdc_tx_fill(void);
static void cdc_rx_pump(void);
static void cdc_rx_resume(void);
static void msc_rx_packet(void);
static void msc_tx_done(void);
/* The eMMC transport lives further down but its helpers are used by the
 * shared SCSI reply builder above it. */
static int  msc_tx_wait(void);
static void msc_tx_raw(const uint8_t *p, uint16_t n);
static void msc_csw_poll(uint8_t status, uint32_t residue);
static int  msc_poll_one(void);

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
 * @note PROCESS CONTEXT ONLY -- never call from the ISR.
 * @return Previous enable state, so nesting cannot wrongly re-enable.
 */
static inline uint32_t usb_lock(void)
{
    uint32_t was = NVIC_GetEnableIRQ(USB0_IRQn);
    NVIC_DisableIRQ(USB0_IRQn);
    __DSB(); __ISB();
    return was;
}
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
 * The FIFO port is a 32-bit window onto a byte FIFO: byte-width accesses pop
 * one byte each, which is what a control transfer needs (8-byte SETUP packets
 * and odd-length descriptors).  Word accesses would be faster and wrong.
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
 * DataEnd travels WITH the last packet: a short packet is itself the
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
        /* Class lives at DEVICE level for CDC and at INTERFACE level for
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
    case DESC_CONFIG:
        /* Patch the bulk max-packet to whatever the chirp settled on.  This
         * is done HERE, at request time, because the host asks for the
         * configuration only after the bus reset -- so the speed is known,
         * and a descriptor built at compile time could only ever be right
         * for one of the two. */
        {
            uint8_t lo = (uint8_t)(s_bulk_mps & 0xFFu);
            uint8_t hi = (uint8_t)(s_bulk_mps >> 8);
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
        /* DEVICE_QUALIFIER (6) and OTHER_SPEED (7) are ASKED FOR BY LINUX on
         * a 2.00 device and MUST be stalled by a full-speed-only device.
         * Stalling them is the correct answer, not a failure -- answering
         * them would claim a high-speed capability U1 does not have. */
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
     * CLASS requests (bmRequestType type field == 1) belong to CDC.  Three of
     * them matter and the host WILL issue them before it will open the port:
     * a device that stalls SET_LINE_CODING never gets a terminal.
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
                     * ignored, but must be ACCEPTED: this is a real
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

    /* Anything else non-standard stalls, which is a legal and informative
     * answer rather than a failure. */
    if ((bmRequestType & 0x60u) != 0x00u) { ep0_stall(); return; }

    switch (bRequest) {
    case 0x05:  /* SET_ADDRESS */
        /*
         * THE CLASSIC TRAP.  The address must not take effect until the host
         * has seen the status stage, because until then the host is still
         * talking to address 0.  MUSB's FADDR applies the moment it is
         * written, so the write is DEFERRED to the next EP0 interrupt --
         * i.e. after the zero-length status packet has gone out.
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
     * wanted the first 8 bytes of a config descriptor does exactly this).
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
 * FIFO space is allocated by hand and CUMULATIVELY, in units of 8 bytes, past
 * the 64 EP0 owns.  A bus reset resets the running pointer, so this runs from
 * SET_CONFIGURATION only -- allocating twice would overlap two endpoints.
 */
static void cdc_endpoints_open(void)
{
    uint16_t addr = TIKU_USB_FIFO_FIRST;   /* units; EP0 owns 0..7           */
    uint8_t  sz;
    int      dbuf;

    /* size code = log2(maxpacket / 8) */
    sz = (s_bulk_mps == CDC_BULK_MPS_HS) ? 6u : 3u;   /* 512 -> 6, 64 -> 3   */

    /*
     * DOUBLE-BUFFER THE BULK PIPES IN eMMC MODE, and the reason is arithmetic
     * rather than taste.  Single-buffered, the endpoint holds ONE packet, so
     * the pump must send and then WAIT for the host to collect before sending
     * again -- at high speed that is one packet per 125 us microframe, i.e.
     * 512 B / 125 us = 4.1 MB/s, which is almost exactly the 5.4 MB/s ceiling
     * that survived both a 2.6x CPU clock increase and burst command
     * servicing.  With two packets in flight the pump can fill one while the
     * host drains the other.  The cost is twice the FIFO space per endpoint,
     * which this core has room for.
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
        /* The pump owns EP1; the ISR keeps only EP0 and the bus events.
         * Masking rather than arbitrating is what removes the races. */
        USB_INTRTXE = 0x0001u;
        USB_INTRRXE = 0x0000u;
    } else {
        USB_INTRTXE = (uint16_t)(0x0001u | (1u << CDC_EP_DATA));
        USB_INTRRXE = (uint16_t)(1u << CDC_EP_DATA);
    }

    s_bot = BOT_CBW;
    s_bot_left = 0u;
    s_sense_key = 0u; s_sense_asc = 0u;
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
 * Unloading a packet the ring cannot hold would drop bytes; leaving it in the
 * FIFO makes the controller NAK and the host slow down instead.  That is the
 * difference between this console and the UART one.
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
 * Process context.  Re-enabling the interrupt is not enough on its own:
 * INTRRX is read-to-clear and the ISR already consumed the pending bit, so a
 * packet sitting in the FIFO would never announce itself.  It is pumped here.
 */
static void cdc_rx_resume(void)
{
    uint32_t was;
    if (!s_rx_stalled) { return; }
    /*
     * Only interrupt the ISR when a WHOLE packet will now fit.  Without this
     * test the drain path takes the NVIC lock once per byte while stalled --
     * a disable/DSB/ISB/enable and its pipeline flush for every character --
     * which measured 0.35 MB/s on a 1 MB transfer that was otherwise
     * flawless.  The check is two loads and a compare; the lock is not.
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

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/**
 * @brief Is this READ/WRITE range inside the disk?
 *
 * The order of the tests matters: `(lba + nblk) > MSC_DISK_BLOCKS` is wrong
 * because the host controls both values and an lba near 2^32 wraps the sum
 * small.  Subtracting cannot overflow: lba is bounded first, then the count.
 */
static int msc_lba_ok(uint32_t lba, uint32_t nblk)
{
    if (nblk == 0u)           { return 1; }
    if (lba >= s_msc_blocks)  { return 0; }
    return (nblk <= (s_msc_blocks - lba)) ? 1 : 0;
}

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
    uint8_t *p = s_bot_reply;
    p[0] = 0x55; p[1] = 0x53; p[2] = 0x42; p[3] = 0x53;   /* "USBS" LE      */
    p[4] = (uint8_t)s_bot_tag;         p[5] = (uint8_t)(s_bot_tag >> 8);
    p[6] = (uint8_t)(s_bot_tag >> 16); p[7] = (uint8_t)(s_bot_tag >> 24);
    p[8]  = (uint8_t)s_bot_residue;
    p[9]  = (uint8_t)(s_bot_residue >> 8);
    p[10] = (uint8_t)(s_bot_residue >> 16);
    p[11] = (uint8_t)(s_bot_residue >> 24);
    p[12] = s_bot_status;
    s_bot_ptr  = s_bot_reply;
    s_bot_left = CSW_LEN;
    s_bot      = BOT_CSW;
    msc_tx_packet();
}

/** @brief Record a sense condition for the REQUEST SENSE that will follow. */
static void msc_fail(uint8_t key, uint8_t asc)
{
    s_sense_key = key;
    s_sense_asc = asc;
    s_bot_status = 1u;
}

/**
 * @brief Build the reply for a small, memory-resident SCSI command.
 *
 * Shared by both transports: the ISR-driven RAM-disk path and the
 * process-context eMMC pump ask the same question and must not drift apart,
 * so there is one implementation and two callers.
 *
 * @return bytes placed in s_bot_reply; 0 when the command carries no data.
 *         Sets s_bot_status / sense on an unsupported opcode.
 */
static uint16_t msc_small_reply(const uint8_t *cb, uint32_t host_len)
{
    uint8_t *r = s_bot_reply;
    unsigned i;
    uint16_t len = 0u;

    switch (cb[0]) {
    case SCSI_TEST_UNIT_READY:
    case SCSI_START_STOP:
    case SCSI_PREVENT_ALLOW:
    case SCSI_SYNC_CACHE10:
        return 0u;

    case SCSI_INQUIRY: {
        static const char vid[8]  = { 'T','i','k','u','O','S',' ',' ' };
        for (i = 0u; i < 36u; i++) { r[i] = 0u; }
        r[0] = 0x00; r[1] = 0x80; r[2] = 0x04; r[3] = 0x02; r[4] = 31;
        for (i = 0u; i < 8u; i++) { r[8 + i] = (uint8_t)vid[i]; }
        if (s_store == MSC_STORE_EMMC) {
            static const char pe[16] = { 'e','M','M','C',' ','8','G','B',
                                         ' ',' ',' ',' ',' ',' ',' ',' ' };
            for (i = 0u; i < 16u; i++) { r[16 + i] = (uint8_t)pe[i]; }
        } else {
            static const char pr[16] = { 'R','A','M',' ','D','i','s','k',
                                         ' ',' ',' ',' ',' ',' ',' ',' ' };
            for (i = 0u; i < 16u; i++) { r[16 + i] = (uint8_t)pr[i]; }
        }
        r[32] = '0'; r[33] = '.'; r[34] = '0'; r[35] = '6';
        len = 36u;
        break;
    }

    case SCSI_REQUEST_SENSE:
        for (i = 0u; i < 18u; i++) { r[i] = 0u; }
        r[0] = 0x70; r[2] = s_sense_key; r[7] = 10; r[12] = s_sense_asc;
        s_sense_key = 0u; s_sense_asc = 0u;
        len = 18u;
        break;

    case SCSI_READ_CAPACITY10:
        put_be32(&r[0], s_msc_blocks - 1u);   /* LAST LBA, not the count   */
        put_be32(&r[4], MSC_BLOCK_SIZE);
        len = 8u;
        break;

    case SCSI_MODE_SENSE6:
        r[0] = 3; r[1] = 0; r[2] = 0; r[3] = 0;
        len = 4u;
        break;

    case SCSI_MODE_SENSE10:
        for (i = 0u; i < 8u; i++) { r[i] = 0u; }
        r[1] = 6;
        len = 8u;
        break;

    default:
        msc_fail(0x05u, 0x20u);               /* ILLEGAL REQUEST / opcode  */
        return 0u;
    }
    if (len > host_len) { len = (uint16_t)host_len; }
    return len;
}

/** @brief Poll-path counterpart: ship a small reply, then the status. */
static void msc_scsi_reply(const uint8_t *cb, uint32_t host_len)
{
    uint16_t len = msc_small_reply(cb, host_len);
    if (len && msc_tx_wait()) { msc_tx_raw(s_bot_reply, len); }
    msc_csw_poll(s_bot_status, host_len - len);
}

/**
 * @brief Decode one SCSI command and set up whatever data phase it needs.
 *
 * The subset is what a host issues to mount, read and write a disk.  Anything
 * else is failed with ILLEGAL REQUEST rather than ignored -- a command
 * silently treated as success is how a host comes to believe a write landed.
 */
static void msc_scsi(const uint8_t *cb, uint32_t host_len, int host_wants_in)
{
    uint8_t op = cb[0];
    uint32_t lba, nblk, bytes;

    s_bot_status = 0u;
    s_bot_residue = host_len;

    switch (op) {
    case SCSI_READ10:
    case SCSI_WRITE10:
        lba  = be32(&cb[2]);
        nblk = (uint32_t)cb[7] << 8 | cb[8];
        bytes = nblk * MSC_BLOCK_SIZE;
        /* Range check BEFORE handing out a pointer: an out-of-range LBA
         * would otherwise index straight off the end of the disk array. */
        if (!msc_lba_ok(lba, nblk)) {
            msc_fail(0x05u, 0x21u);       /* ILLEGAL REQUEST / LBA range   */
            s_bot_residue = host_len;
            msc_send_csw();
            return;
        }
        if (bytes > host_len) { bytes = host_len; }
        s_bot_ptr  = &s_disk[lba * MSC_BLOCK_SIZE];
        s_bot_left = bytes;
        s_bot_residue = host_len - bytes;
        if (op == SCSI_READ10) {
            s_n_rd++;
            s_bot = BOT_DATA_IN;
            msc_tx_packet();
        } else {
            s_n_wr++;
            s_bot = BOT_DATA_OUT;   /* packets arrive on the OUT endpoint  */
        }
        (void)host_wants_in;
        return;

    default: {
        /* Everything else is small and memory-resident: one shared builder
         * so the two transports cannot drift apart. */
        uint16_t len = msc_small_reply(cb, host_len);
        if (len) {
            s_bot_ptr = s_bot_reply;
            s_bot_left = len;
            s_bot_residue = host_len - len;
            s_bot = BOT_DATA_IN;
            msc_tx_packet();
        } else {
            s_bot_residue = host_len;
            msc_send_csw();
        }
        return;
    }
    }
}

/** @brief A packet arrived on the bulk OUT endpoint. */
static void msc_rx_packet(void)
{
    uint16_t cnt, i;
    uint8_t cbw[CBW_LEN];

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

    if (le32(&cbw[0]) != CBW_SIG) { return; }   /* not a CBW: ignore        */
    s_n_cbw++;
    s_bot_tag = le32(&cbw[4]);
    msc_scsi(&cbw[15], le32(&cbw[8]), (cbw[12] & 0x80u) ? 1 : 0);
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
/* MSC OVER THE eMMC: THE DATA PHASE LEAVES INTERRUPT CONTEXT                */
/*---------------------------------------------------------------------------*/
/*
 * U3 said this change would be needed and here it is.  A RAM disk answers a
 * read with a POINTER, so the whole transport fitted in the ISR.  The eMMC
 * answers in MILLISECONDS -- E3 measured 14.5 ms for a 512 KB chunk, and even
 * a single block is orders of magnitude too long to hold an interrupt while
 * the console, the tick and everything else wait behind it.
 *
 * So in eMMC mode the ISR keeps EP0 and the bus events and gives up EP1
 * entirely: its interrupts are MASKED and a process-context pump owns the
 * bulk endpoints, polling them directly.  That removes every race between the
 * two contexts rather than trying to arbitrate one.
 *
 * The pump runs from the scheduler's idle hook and handles ONE complete SCSI
 * command per call.  A command is bounded (the host's dCBWDataTransferLength),
 * so the shell stays responsive between commands even though a single large
 * read blocks for the duration of its own transfer -- which is exactly the
 * ownership rule the plan asked for, made structural.
 *
 * INDEX is still shared with the ISR, which sets it to 0 for EP0.  Every
 * sequence below therefore takes the lock around "select the endpoint and
 * touch its registers", and re-selects after any wait.
 */

#define MSC_WAIT_SPINS  2000000u   /* bounded like every wait in this port  */

/*---------------------------------------------------------------------------*/
/* ADMA -- taking the CPU out of the byte path                                */
/*---------------------------------------------------------------------------*/
/*
 * The controller has TEN DMA channels for exactly this, and until now this
 * driver copied every byte through a volatile pointer.  U4 measured 6.9 MB/s
 * reading and showed it was neither CPU-bound (2.6x the clock moved it 1.02x)
 * nor cadence-bound (bursting commands changed nothing); double buffering
 * lifted a real ping-pong limit but not the rest.  ADMA removes the CPU from
 * the transfer entirely, which is the remaining lever the hardware offers.
 *
 * CHANNEL MAPPING, read from the HAL rather than guessed -- the channels are
 * not interchangeable, each is bound to an endpoint by ADMAEP:
 *      IN  endpoint n  ->  channel n - 1     (EP1 IN  = channel 0)
 *      OUT endpoint n  ->  channel n + 4     (EP1 OUT = channel 5)
 *
 * The per-channel registers are four banks of ten, each stride 4, at fixed
 * offsets confirmed with offsetof() against the CMSIS struct.  CMSIS names
 * them individually (ADMATOTCOUNT0..9), which would mean a ten-way switch;
 * indexing the bank is the same thing without the copy-paste.
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
 * ADMA defaults OFF so that flashing this build changes exactly ONE thing
 * against the proven U4 image (the pump moving to process context).  Turn it
 * on at runtime with `power usb adma on` once the board is known good.  A
 * bisect you can perform without reflashing is worth a one-line default.
 */
static uint8_t  s_adma;               /**< runtime switch, for measuring    */
static volatile uint32_t s_n_adma, s_n_adma_err;

/**
 * @brief Run one ADMA transfer and wait for it.  Non-zero on success.
 *
 * Process context only -- it blocks.  @p bytes must be a whole number of max
 * packets, which every MSC data phase is (512-byte blocks, 512 or 64 byte
 * packets), because DMA mode 1 has no way to express a trailing short packet.
 */
static int adma_run(unsigned ep, int is_in, uint8_t *buf, uint32_t bytes)
{
    unsigned ch = is_in ? ADMA_CH_IN(ep) : ADMA_CH_OUT(ep);
    uint32_t mask = 1u << ch;
    uint32_t spins;
    uint32_t was;

    if (bytes == 0u || bytes > 0xFFFFFFu) { return 0; }   /* 24-bit count   */
    if ((bytes % s_bulk_mps) != 0u)       { return 0; }

    /* The engine moves bytes on the BUS; the D-cache is not on that path.
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

    /* Plain assignment, NOT |=.  The vendor ORs these, which is harmless the
     * first time and accumulates stale bits on reuse -- and this driver
     * reuses one channel for every chunk of every transfer. */
    ADMA_TARGADDR(ch) = (uint32_t)buf;
    ADMA_EPNUM(ch)    = (uint32_t)ep & 0x7u;
    ADMA_REQSIZE(ch)  = (uint32_t)s_bulk_mps & 0xFFFu;
    ADMA_TOTCOUNT(ch) = bytes & 0xFFFFFFu;

    USB->ADMAEN |= mask;                       /* GO                        */
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
             * HAND THE ENDPOINT BACK TO PIO.  Arming DMAReqEnab/DMAReqMode
             * puts the FIFO under the engine's control; leaving it there
             * means the very next PIO access -- the status wrapper, or the
             * next command wrapper -- meets a CSR that is still expecting a
             * DMA request.  Restore the plain configuration explicitly
             * rather than relying on a later whole-register write to clear
             * it by accident.
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
                /* The engine has emptied the buffer into the FIFO; the LAST
                 * packet may still be on its way out.  Do not send the status
                 * wrapper past it. */
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

/** @brief Wait until the IN endpoint has room, then return non-zero. */
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
 * @p cap IS uint32_t AND MUST STAY THAT WAY.  It was uint16_t, and the
 * caller's remaining-space expression reaches MSC_BOUNCE_BYTES = 65536 --
 * which truncates to ZERO.  The call then consumed a packet, copied none of
 * it, and returned 0, so the loop neither advanced nor terminated: it spun
 * discarding the host's data until the wait expired.  It presented as writes
 * failing while reads worked perfectly (4407 reads, 5 writes) and the host
 * resetting the device.
 *
 * Same family as the uint8_t-loop-counter bug already on record: a type too
 * narrow for a bound that is set by configuration rather than by the type.
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
    csw[0] = 0x55; csw[1] = 0x53; csw[2] = 0x42; csw[3] = 0x53;
    csw[4] = (uint8_t)s_bot_tag;         csw[5] = (uint8_t)(s_bot_tag >> 8);
    csw[6] = (uint8_t)(s_bot_tag >> 16); csw[7] = (uint8_t)(s_bot_tag >> 24);
    csw[8]  = (uint8_t)residue;          csw[9]  = (uint8_t)(residue >> 8);
    csw[10] = (uint8_t)(residue >> 16);  csw[11] = (uint8_t)(residue >> 24);
    csw[12] = status;
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
        /* force=1: in MSC mode the HOST owns the medium.  The scratch-region
         * default-deny protects the card from unattended on-board tests, not
         * from the person who deliberately plugged it into a PC -- and the
         * scratch blocks are unreachable anyway, being outside the reported
         * capacity (see msc_capacity). */
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
 * @brief Process-context pump: handle at most one SCSI command per call.
 *
 * Called from the scheduler idle hook.  Returns immediately unless MSC is
 * backed by the card and a command wrapper is actually waiting.
 */
void tiku_usb_msc_poll(void)
{
    /*
     * SERVICE COMMANDS UNTIL THE HOST STOPS ASKING, not one per call.
     *
     * Handling a single command per idle-hook invocation made throughput a
     * function of the scheduler's cadence rather than of the hardware:
     * measured ~10 KB per command, so per-command overhead dominated, and
     * raising the CPU clock 2.6x moved the number by 1.02x -- the signature
     * of a structural limit rather than a compute one.  The bound keeps a
     * busy host from starving everything else indefinitely.
     */
    unsigned burst, idle;

    /*
     * Cheap exit when there is nothing to serve.  Without it, a build with the
     * USB driver compiled in but MSC not up paid the full turnaround wait
     * below on EVERY shell pass: msc_poll_one() returned 0 immediately, then
     * this function spun 400 x 5 us = 2 ms doing nothing, once per ~47 ms
     * tick.  The hot path is unaffected -- during an active transfer s_up and
     * s_configured are true, so this test is false and the turnaround wait
     * still runs, which is the thing that took formatting from 207 s to 10 s.
     */
    if (!s_up || !s_configured || s_class != TIKU_USB_CLASS_MSC ||
        s_store != MSC_STORE_EMMC) {
        return;
    }

    for (burst = 0u; burst < 256u; burst++) {
        if (msc_poll_one()) { continue; }
        /*
         * NOTHING WAITING IS NOT THE SAME AS NOTHING COMING.
         *
         * Bulk-Only Transport is strictly ping-pong at the command level: the
         * host will not send the next CBW until the CSW arrives.  Returning
         * the moment the queue looks empty hands control back to a shell that
         * only polls every TIKU_SHELL_POLL_TICKS -- about 47 ms -- so the
         * transfer runs at roughly ONE COMMAND PER 47 ms.  Formatting the card
         * took 207 s that way instead of 10.
         *
         * So wait briefly for the host to come back before giving up.  A few
         * hundred microseconds covers the turnaround; the bound keeps an idle
         * device from spinning here instead of running the shell.
         */
        for (idle = 0u; idle < 400u; idle++) {
            tiku_cpu_ambiq_delay_us(5u);
            if (msc_poll_one()) { break; }
        }
        if (idle == 400u) { return; }   /* genuinely idle: let the shell run */
    }
}

/** @brief Handle at most one SCSI command.  1 if one was handled. */
static int msc_poll_one(void)
{
    uint8_t cbw[CBW_LEN];
    uint16_t got;
    uint32_t host_len, lba, nblk, bytes;
    uint8_t op;
    uint32_t was;
    uint16_t csr;

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
    if (got != CBW_LEN || le32(&cbw[0]) != CBW_SIG) { return 1; }
    s_n_cbw++;
    s_bot_tag = le32(&cbw[4]);
    host_len  = le32(&cbw[8]);
    op        = cbw[15];

    if (op == SCSI_READ10 || op == SCSI_WRITE10) {
        lba  = be32(&cbw[17]);
        nblk = (uint32_t)cbw[22] << 8 | cbw[23];
        bytes = nblk * MSC_BLOCK_SIZE;
        if (!msc_lba_ok(lba, nblk)) {
            msc_fail(0x05u, 0x21u);
            msc_csw_poll(1u, host_len);
            return 1;
        }
        if (bytes > host_len) { bytes = host_len; }
        if (op == SCSI_READ10) {
            s_n_rd++;
            msc_emmc_read(lba, bytes, host_len);
        } else {
            s_n_wr++;
            msc_emmc_write(lba, bytes, host_len);
        }
        return 1;
    }

    /* Everything else is small and memory-resident: reuse the same SCSI
     * decoder the RAM-disk path uses, then ship its reply from here. */
    s_bot_status = 0u;
    msc_scsi_reply(&cbw[15], host_len);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* BUS EVENTS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bus reset: put everything back the way the host expects to find it.
 *
 * The controller does NOT tidy up on its own (table 4).  Getting this wrong is
 * precisely how a device enumerates once and then fails on replug, which is why
 * the acceptance gate is ten cycles rather than one.
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

    /* Speed is the hardware's answer to the chirp handshake, read back
     * rather than assumed. */
    s_speed = (USB_POWER & POWER_HSMODE) ? TIKU_USB_SPEED_HIGH
                                         : TIKU_USB_SPEED_FULL;
    /* Bulk max-packet is fixed by the spec at each speed, so the descriptor
     * the host is about to ask for depends on what just happened here. */
    s_bulk_mps = (s_speed == TIKU_USB_SPEED_HIGH) ? CDC_BULK_MPS_HS
                                                  : CDC_BULK_MPS_FS;
}

/**
 * @brief The USB interrupt.  Bounded, allocation-free, and it never prints.
 *
 * READ EACH STATUS REGISTER EXACTLY ONCE.  They are read-to-clear; a second
 * read returns zero and loses whatever arrived in between.  Everything below
 * consults the locals, never the registers again.
 */
void tiku_ambiq_usb_isr(void)
{
    uint8_t  intrusb = USB_INTRUSB;    /* read-to-clear: once, into a local  */
    uint16_t intrtx  = USB_INTRTX;     /* read-to-clear                      */
    uint16_t intrrx  = USB_INTRRX;     /* read-to-clear                      */

    s_n_irq++;

    if (intrusb & INTRUSB_RESET)   { bus_reset(); }
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
    USB_INDEX = 0u;   /* leave the window on EP0, where ep0_irq expects it   */
}

/*---------------------------------------------------------------------------*/
/* BRING-UP (table 2 -- and it is an ORDER, not a set)                       */
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
    s_msc_blocks = MSC_DISK_BLOCKS;
    if (cls == TIKU_USB_CLASS_MSC && use_emmc) {
#if (TIKU_DRV_EMMC_ENABLE + 0)
        uint32_t cap = tiku_emmc_capacity_blocks();
        if (cap <= TIKU_EMMC_SCRATCH_BLOCKS) {
            /* Not identified yet, or implausibly small: refuse rather than
             * present a disk whose size cannot be justified. */
            return TIKU_USB_ERR_STATE;
        }
        s_store = MSC_STORE_EMMC;
        s_msc_blocks = cap - TIKU_EMMC_SCRATCH_BLOCKS;
#else
        return TIKU_USB_ERR_ARG;
#endif
    }
    if (want != TIKU_USB_SPEED_FULL && want != TIKU_USB_SPEED_HIGH) {
        return TIKU_USB_ERR_ARG;
    }
    s_want = want;

    /* 1+2. TWO power domains.  One enable is never the whole story on this
     *      part -- SDIO0 taught that and it generalises. */
    PWRCTRL->DEVPWREN |= PWRCTRL_DEVPWREN_PWRENUSB_Msk |
                         PWRCTRL_DEVPWREN_PWRENUSBPHY_Msk;
    __DSB();
    for (spins = 0u; spins < 100000u; spins++) {
        uint32_t want = PWRCTRL_DEVPWRSTATUS_PWRSTUSB_Msk |
                        PWRCTRL_DEVPWRSTATUS_PWRSTUSBPHY_Msk;
        if ((PWRCTRL->DEVPWRSTATUS & want) == want) { break; }
    }
    if (spins == 100000u) { return TIKU_USB_ERR_POWER; }

    /* 3. The undocumented FIFO-SRAM trim.  Transcribed from the vendor,
     *    not derived -- an unwritten configuration register is exactly the
     *    kind of omission that costs a bring-up. */
    USB->SRAMCTRL = (1u  << USB_SRAMCTRL_WABL_Pos)  |
                    (1u  << USB_SRAMCTRL_WABLM_Pos) |
                    (1u  << USB_SRAMCTRL_RAWL_Pos)  |
                    (2u  << USB_SRAMCTRL_RAWLM_Pos) |
                    (0u  << USB_SRAMCTRL_EMAW_Pos)  |
                    (0u  << USB_SRAMCTRL_EMAS_Pos)  |
                    (3u  << USB_SRAMCTRL_EMA_Pos)   |
                    (1u  << USB_SRAMCTRL_RET1N_Pos);
    __DSB();

    /* 4. HOLD the PHY in reset.  Clearing these bits is what holds it -- the
     *    vendor's function for this is called "enable_phy_reset_override",
     *    whose name says the opposite of its effect. */
    MCUCTRL->USBRSTCTRL &= ~(MCUCTRL_USBRSTCTRL_USBRSTENABLE_Msk |
                             MCUCTRL_USBRSTCTRL_USBPORRSTRELEASE_Msk |
                             MCUCTRL_USBRSTCTRL_USBUTMIRSTRELEASE_Msk);
    __DSB();

    /* 5+6. The external rails, then wait.  Forget these and the registers
     *      all read back perfectly while the bus stays dead. */
    rail(TIKU_USB_PAD_VDDUSB33, 1);
    rail(TIKU_USB_PAD_VDDUSB0P9, 1);
    tiku_cpu_ambiq_delay_us(TIKU_USB_RAIL_SETTLE_MS * 1000u);

    /* 7. Disconnect battery-charger detection from D+/D-.  Left connected,
     *    enumeration fails with no error reported anywhere. */
    USB->BCDETCRTL1 = (1u << USB_BCDETCRTL1_USBSWRESET_Pos);
    __DSB();

    /* 8. RELEASE the PHY. */
    MCUCTRL->USBRSTCTRL |= (MCUCTRL_USBRSTCTRL_USBRSTENABLE_Msk |
                            MCUCTRL_USBRSTCTRL_USBPORRSTRELEASE_Msk |
                            MCUCTRL_USBRSTCTRL_USBUTMIRSTRELEASE_Msk);
    __DSB();
    tiku_cpu_ambiq_delay_us(1000u);

    /* 9. PHY reference clock.  The two speeds take different sources and
     *    this is the ONLY difference between them, which is exactly why U1
     *    and U1b are separate milestones. */
    CLKGEN->MISC |= CLKGEN_MISC_FRCHFRC_Msk;
    __DSB();
    if (hs) {
        /*
         * *** HIGH SPEED DEPENDS ON THE RADIO DIE BEING AWAKE. ***
         *
         * Measured, not assumed: requesting HS with the EM9305 still in
         * reset produced irq 0, reset 0 and a host that saw nothing at all.
         * Without a reference the PHY cannot even present the pull-up, so
         * the failure looks identical to an unplugged cable.  Booting the
         * die first (its crystal is the 12 MHz source) made the same code
         * enumerate at high speed immediately.
         *
         * So the driver does it rather than leaving it to whoever remembers.
         * Once is enough -- the clock stays up while the die does.
         */
#if (TIKU_BOARD_HAS_USBHS_CLK_XTAL + 0)
        /*
         * Boards with their own high-speed crystal take it directly: 48 MHz
         * halved to the PHY's 24 MHz reference, so no EM9305, no clock
         * request, no external pad, and no x20 multiplier (that exists only
         * because the Blue board's reference is 12 MHz).
         *
         * NOT HARDWARE-VERIFIED -- no green EVB has been on the bench.  The
         * selector value is read from table 5 (XTAL_HS 48 MHz -> XTALHS_DIV2),
         * not inferred, but "read from the table" is not "seen enumerate".
         * Re-run the U1/U2 gates when a green board is available.
         */
        USB->CLKCTRL = ((uint32_t)USB_CLKCTRL_PHYREFCLKSEL_XTALHS_DIV2
                        << USB_CLKCTRL_PHYREFCLKSEL_Pos);
        __DSB();
#else
        if (!s_em9305_up) {
#if (TIKU_DRV_BLE_EM9305_ENABLE + 0)
            if (tiku_em9305_reset() != 0) { return TIKU_USB_ERR_CLOCK; }
            s_em9305_up = 1u;
#else
            /* Fail CLOSED and for the right reason: with no way to start the
             * clock source, high speed is unreachable in this build. */
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

    /* 10. Speed.  HSEnab only ALLOWS high speed -- the chirp handshake in
     *     hardware decides, and POWER.HSMode reports the verdict after the
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
     * Hand the MSC transport to the shell rather than having the shell reach
     * in and call it.  Registered on the way UP so a build that never brings
     * USB up never pays for the pump; dropped again in tiku_usb_down().
     * Registration is idempotent, so repeated `usb up` is harmless.
     */
    (void)tiku_shell_add_pump(tiku_usb_msc_poll);
    return TIKU_USB_OK;
}

tiku_usb_err_t tiku_usb_attach(int on)
{
    if (!s_up) { return TIKU_USB_ERR_STATE; }
    /* Byte-width read-modify-write: a 32-bit access here would clear INTRTX. */
    if (on) { USB_POWER = (uint8_t)(USB_POWER |  POWER_SOFTCONN); }
    else    { USB_POWER = (uint8_t)(USB_POWER & (uint8_t)~POWER_SOFTCONN); }
    s_attached = on ? 1u : 0u;
    return TIKU_USB_OK;
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
    s_up = 0u; s_attached = 0u; s_speed = TIKU_USB_SPEED_NONE;
}

/*---------------------------------------------------------------------------*/
/* THE CONSOLE BACKEND                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The shell has been transport-agnostic since it was written; this is simply
 * another tiku_shell_io_t.  Nothing in the CLI changes.
 */

/**
 * @brief Queue one byte for the host.
 *
 * Gated on DTR: a board printing into a port nobody has opened must not block
 * or fill a ring that will never drain.  With the terminal open a full ring is
 * waited on briefly, then the byte is DROPPED and COUNTED for `power usb state`.
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
    TIKU_VFS_CAP_ALL          /* a cable in the board IS physical presence  */
};

int tiku_usb_cdc_ready(void)
{
    return (s_configured && s_dtr) ? 1 : 0;
}

/**
 * @brief Drain the CDC receive pipe for @p ms, counting bytes, interpreting
 *        none of them.
 *
 * The gate has to measure the transport, so this drains the same ring and
 * flow-control path the shell uses and simply counts; bytes in must equal bytes
 * out.  Runs on whichever channel is NOT under test, so the shell stays usable.
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
    if (blocks) { *blocks = s_msc_blocks; }
}

/**
 * @brief FNV-1a over the whole RAM disk.
 *
 * The gate for U3 is not "the host mounted it" but "what the host wrote is
 * what the board holds".  Hashing the disk on the board and comparing with a
 * hash of the same bytes on the PC is the only way to say that.
 */
/**
 * @brief Exercise the LBA bounds check with values that must be REFUSED.
 *
 * The guard stands between a hostile or buggy host and the memory after the
 * disk.  The cases include the overflow pair that defeats the naive form of
 * the check, so this is a regression test rather than a formality.
 *
 * @return 0 if every case behaved; otherwise a bitmask of the ones that did
 *         not, so a failure names itself.
 */
uint32_t tiku_usb_msc_selftest(void)
{
    uint32_t bad = 0u;
    /* must be ACCEPTED */
    if (!msc_lba_ok(0u, 1u))                        { bad |= 1u << 0; }
    if (!msc_lba_ok(MSC_DISK_BLOCKS - 1u, 1u))      { bad |= 1u << 1; }
    if (!msc_lba_ok(0u, MSC_DISK_BLOCKS))           { bad |= 1u << 2; }
    /* must be REFUSED */
    if (msc_lba_ok(MSC_DISK_BLOCKS, 1u))            { bad |= 1u << 3; }
    if (msc_lba_ok(MSC_DISK_BLOCKS - 1u, 2u))       { bad |= 1u << 4; }
    if (msc_lba_ok(0u, MSC_DISK_BLOCKS + 1u))       { bad |= 1u << 5; }
    /* the overflow pair: lba + nblk wraps to 0, which the naive check
     * would have waved through */
    if (msc_lba_ok(0xFFFFFF00u, 0x100u))            { bad |= 1u << 6; }
    if (msc_lba_ok(0x80000000u, 0x80000000u))       { bad |= 1u << 7; }
    return bad;
}

uint32_t tiku_usb_msc_hash(uint32_t nblocks)
{
    uint32_t h = 2166136261u, i, n;
    if (nblocks == 0u || nblocks > MSC_DISK_BLOCKS) {
        nblocks = MSC_DISK_BLOCKS;
    }
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

    /* POWER-SAFE, by construction.  Reading an unpowered peripheral stalls
     * the APB and hangs the CPU with no fault; it cost a board wedge during
     * the NOR bring-up and the lesson is applied here up front. */
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
    /* Deliberately NOT dumped: INTRUSB, INTRTX, INTRRX.  They are
     * read-to-clear, and a diagnostic that steals interrupts from the ISR is
     * a diagnostic that causes the bug it is looking for. */
}

#endif /* PLATFORM_AMBIQ && TIKU_DRV_USB_ENABLE */
