/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_arch.h - Apollo510 USB 2.0 device controller.
 *
 * A Mentor/Inventra MUSB-class device core (identified by the POWER bit order,
 * the INDEX-selected per-endpoint CSR window and read-to-clear status) behind
 * a PHY, two power domains and an auto-DMA engine.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USB_ARCH_H_
#define TIKU_USB_ARCH_H_

#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>   /* tiku_shell_io_t, the CDC backend */
/* The rail pads below come from the board header, which this include pulls in
 * itself, so a file can include this header before tiku.h; without the board
 * macros the build stops at the #error below. */
#include "tiku_device_select.h" 

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- BOARD: WHICH SOCKET, WHICH RAILS, AND WHICH BOARD              */
/*---------------------------------------------------------------------------*/
/*
 * The EVB has two USB-C sockets and only one is the device port:
 *
 *   J18  "AP5 USB-C Connector"  -- the Apollo5 device port.
 *                                  nets USB0AP50P/N -> USB_AP5_P/N
 *   J16  "USB-C Connector"      -- the on-board J-Link.  A host plugged in
 *                                  here enumerates the debugger only.
 *
 * D+/D- are dedicated PHY pins with no pad configuration.  Two external supply
 * rails are switched on by GPIO, on different pads per board (per the BSP and
 * the Blue board's schematic):
 *
 *              | Apollo510B EVB (Blue)      | Apollo510 EVB (green)
 *   -----------+----------------------------+-----------------------
 *   VDDUSB33   | GP47                       | GP91
 *              | net VDDUSB33_AP5_ON_GP47   |
 *   VDDUSB0P9  | GP48                       | GP90
 *              | net VDDUSB0P9_AP5_ON_GP48  |
 *
 * Both are driven high to enable, and the PHY is touched 50 ms later, as the
 * vendor does.  With the rails off the PHY is unpowered: the controller's
 * registers read back normally and the host sees no device.
 */

/* The rail switches are board pads (Blue 47/48, green 91/90), from the board
 * header. */
#if !defined(TIKU_BOARD_USB_PAD_VDDUSB33)
#error "This board declares no USB rail pads (TIKU_BOARD_USB_PAD_VDDUSB*). \
The build system should not have compiled the USB driver for it -- see \
BOARD_CAPS/USB_RAILS in the Makefile."
#endif
#define TIKU_USB_PAD_VDDUSB33   TIKU_BOARD_USB_PAD_VDDUSB33   /**< 3.3 V rail */
#define TIKU_USB_PAD_VDDUSB0P9  TIKU_BOARD_USB_PAD_VDDUSB0P9  /**< 0.9 V rail */
#define TIKU_USB_RAIL_SETTLE_MS 50u   /**< wait after the rails switch on, ms */

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- THE REGISTER MAP AND THE CMSIS HAZARD                          */
/*---------------------------------------------------------------------------*/
/*
 * The CMSIS bitfield accessors on CFG0/CFG1/CFG2 are unsafe.  The hardware
 * registers are 8- and 16-bit MUSB registers, which CMSIS packs two or three
 * to a 32-bit word (CFG0..CFG3); three of the packed fields are read-to-clear
 * interrupt status:
 *   - `USB->CFG0_b.HSEnab = 1;` is a 32-bit read-modify-write whose read
 *     clears INTRTX, discarding every pending IN-endpoint interrupt;
 *   - CFG1 (INTRRX) and CFG2 (INTRUSB) behave the same way: a bus reset or a
 *     completed transfer is lost when a neighbouring field is written.
 * This driver, like the vendor HAL, accesses them as 8- and 16-bit volatile
 * registers at their byte offsets.
 *
 * The map, as byte offsets from USB_BASE.  Widths and read-to-clear behaviour
 * follow the HAL's accessor macros; the offsets match the CMSIS struct
 * (CFG0/1/2/3 = 0x00/04/08/0C, IDX0/1/2 = 0x10/14/18, FIFOADD = 0x1C,
 * FIFO0..5 = 0x20 + 4n) and the POWER bits match USB_CFG0_*_Pos:
 *
 *   off  w   name       notes
 *   ---  --  ---------  ------------------------------------------------
 *   0x00  8  FADDR      FuncAddr[6:0], Update[7]
 *   0x01  8  POWER      see below -- the MUSB signature register
 *   0x02 16  INTRTX     EPn IN complete, bit n.  read-to-clear
 *   0x04 16  INTRRX     EPn OUT complete, bit n. read-to-clear
 *   0x06 16  INTRTXE    IN interrupt enables
 *   0x08 16  INTRRXE    OUT interrupt enables
 *   0x0A  8  INTRUSB    bus events.             read-to-clear
 *   0x0B  8  INTRUSBE   bus event enables
 *   0x0C 16  FRAME      frame number
 *   0x0E  8  INDEX      selects the endpoint 0x10..0x1F refer to
 *   0x0F  8  TESTMODE   ForceHS / ForceFS / test packet
 *   0x10 16  TXMAXP     |
 *   0x12 16  CSR0/TXCSR | indexed by INDEX  (CMSIS calls the pair IDX0)
 *   0x14 16  RXMAXP     |
 *   0x16 16  RXCSR      | indexed by INDEX  (CMSIS: IDX1)
 *   0x18 16  COUNT0/RXCOUNT + INFIFOSZ[23:16] + OUTFIFOSZ[31:24]  (IDX2)
 *   0x1C     FIFOADD    INFIFOADD[12:0], OUTFIFOADD[28:16], units of 8 B
 *   0x20+4n  FIFO0..5   per-endpoint data ports
 *
 * POWER (0x01), bit by bit; this bit order identifies the MUSB core:
 *   0 EnableSuspendM   1 SuspendMode(RO)   2 Resume        3 Reset(RO)
 *   4 HSMode(RO)       5 HSEnab            6 SOFTCONN      7 ISOUpdate
 * CMSIS calls bit 6 "AMSPECIFIC".  It is the soft-connect bit: setting it
 * attaches the pull-up and is how the device becomes visible to the host.
 *
 * INTRUSB (0x0A) bits: 0 Suspend, 1 Resume, 2 Reset, 3 SOF.
 * INTRUSBE (0x0B) uses the same bit order.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- BRING-UP ORDER                                                 */
/*---------------------------------------------------------------------------*/
/*
 *  #  what                                    register / call
 * --  --------------------------------------  --------------------------
 *  1  power the controller domain             PWRCTRL periph USB
 *  2  power the PHY domain                    PWRCTRL periph USBPHY
 *     -- both domains are needed.
 *  3  write the USB SRAM trim                 USB->SRAMCTRL
 *     -- an undocumented value the vendor writes unconditionally:
 *        WABL=1 WABLM=1 RAWL=1 RAWLM=2 EMAW=0 EMAS=0 EMA=3 RET1N=1.
 *        It tunes the FIFO RAM.
 *  4  hold the PHY in reset                   MCUCTRL->USBRSTCTRL: clear
 *                                             USBRSTENABLE, USBPORRSTRELEASE,
 *                                             USBUTMIRSTRELEASE
 *     -- clearing these bits holds the PHY in reset; the vendor's function
 *        for it is named enable_phy_reset_override.
 *  5  switch the external rails on            VDDUSB33 and VDDUSB0P9 pads
 *                                             high (table 0)
 *  6  wait                                    50 ms
 *  7  disconnect battery-charger detection    USB->BCDETCRTL1 = USBSWRESET=1,
 *                                             every other field 0
 *     -- the BC circuit sits on D+/D-.  Left connected, enumeration fails
 *        with no error anywhere.
 *  8  release the PHY from reset              MCUCTRL->USBRSTCTRL: set the
 *                                             three bits from step 4
 *  9  select + enable the PHY reference clock see table 5
 * 10  set the speed                           POWER.HSEnab per table 5
 * 11  enable the bus interrupts               INTRUSBE: Reset, Resume,
 *                                             Suspend
 * 12  attach                                  POWER.SOFTCONN = 1
 *                                             (tiku_usb_attach())
 *
 * Steps 4-8 follow this order because of Apollo4 erratum ERR041, which the
 * vendor cites in its power-up path: an induced D+ output pulse can cause an
 * unintended disconnect.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 3 -- ENDPOINTS, FIFO, AND THE INDEX REGISTER HAZARD                 */
/*---------------------------------------------------------------------------*/
/*
 * Six endpoints: EP0 (control, bidirectional) plus EP1..EP5, each of which
 * has an independent IN and OUT half.  Ten ADMA channels (ADMAEP0..9) cover
 * the five IN and five OUT halves.  MSC needs one bulk IN and one bulk OUT;
 * CDC needs two bulk plus a notification endpoint.
 *
 * EP0's interrupt is reported in INTRTX bit 0 and enabled in INTRTXE bit 0 --
 * the OUT-side registers play no part for EP0.
 *
 * The INDEX register is shared mutable state.  INDEX (0x0E) selects the
 * endpoint the CSR window at 0x10..0x1F refers to, so every access to
 * TXMAXP/TXCSR/RXMAXP/RXCSR/COUNT0/FIFOADD takes two steps, and an interrupt
 * between them that changes INDEX sends the second step to another
 * endpoint.  The rule:
 *
 *   - the ISR may set INDEX freely; it is not interrupted by itself
 *   - process-context code that touches an indexed register masks the USB
 *     interrupt across the whole INDEX-then-access sequence
 *
 * A break of this rule shows as one endpoint acting on another's registers.
 *
 * FIFO allocation is manual and cumulative:
 *   - addresses and sizes are in units of 8 bytes
 *   - allocation starts at unit 8 (byte 64): the first 64 bytes belong to EP0
 *   - size code = log2(maxpacket / 8); the size table is
 *     8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes
 *   - double buffering is optional per endpoint per direction and doubles
 *     that endpoint's consumption
 *   - FIFOADD is 13 bits in 8-byte units, i.e. a 64 KB address space; the
 *     RAM actually fitted is a build option of the core
 *
 * Budget for the MSC configuration at high speed:
 *      EP0            64 B
 *      bulk IN  512  512 B   (1024 if double-buffered)
 *      bulk OUT 512  512 B   (1024 if double-buffered)
 *                  ------
 *                  1088 B single-buffered, 2112 B double-buffered
 *
 * A bus reset discards the allocation: every endpoint is re-armed, from unit
 * 8, when the host configures the device again.  Allocating without starting
 * over at unit 8 leaks FIFO space across replugs.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 4 -- INTERRUPTS                                                     */
/*---------------------------------------------------------------------------*/
/*
 * One NVIC vector for the whole controller: USB0_IRQn.  The status sources:
 *
 *   INTRUSB          bus events: Suspend, Resume, Reset, SOF   (read-to-clear)
 *   INTRTX           IN endpoint n complete, bit n             (read-to-clear)
 *   INTRRX           OUT endpoint n complete, bit n            (read-to-clear)
 *   ADMACMPINTSTAT   auto-DMA channel completion               (write-1-clear)
 *   ADMAERRINTSTAT   auto-DMA channel error                    (write-1-clear)
 *
 * The ISR reads each read-to-clear register once, into a local, and consults
 * only the local: a second read loses whatever arrived between the reads, and
 * a read anywhere but the ISR takes events from it.  The ADMA status
 * registers are polled by the process-context transfer, not the ISR.
 *
 * On a bus reset the controller does not reset its own state.  bus_reset()
 * in tiku_usb_arch.c:
 *   - returns the EP0 state machine to IDLE and drops a pending SET_ADDRESS
 *   - clears the address, the configuration, DTR and the CDC rings
 *   - re-inits EP0 with a 64-byte max packet
 *   - leaves only EP0's interrupt enabled; SET_CONFIGURATION runs
 *     cdc_endpoints_open(), which allocates the FIFO again from unit 8 and
 *     sets the endpoint interrupts
 *   - reads POWER.HSMode for the negotiated speed and sets the bulk max
 *     packet from it
 * SOF is never enabled.  A suspend event that arrives with a reset is
 * counted like any other.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 5 -- SPEED AND THE PHY REFERENCE CLOCK                              */
/*---------------------------------------------------------------------------*/
/*
 * Speed is negotiated by the chirp handshake in hardware.  Software sets
 * POWER.HSEnab before attaching, then reads POWER.HSMode after the bus reset
 * for the result.  TIMEOUT1 (chirp) and TIMEOUT2 (HS resume delay) stay at
 * their defaults.
 *
 * The PHY needs a reference clock, chosen in USB->CLKCTRL.PHYREFCLKSEL[26:24]:
 *   0 HFRC_48MHz   1 HFRC2_31MHz   2 HFRC_24MHz   3 EXTREFCLK
 *   4 EXTREFCLK_DIV2   5 XTALHS   6 XTALHS_DIV2   7 OFF
 * plus PHYREFCLKDIS[0], CTRLAPBCLKDIS[8], PHYAPBLCLKDIS[16] as gates.
 *
 * Full speed uses HFRC at 24 MHz, an internal oscillator that is always
 * available.  For high speed the vendor's clock-source selection is:
 *
 *   XTAL_HS 48 MHz  -> XTALHS_DIV2          (the green board's path)
 *   XTAL_HS 24 MHz  -> XTALHS
 *   EXTREF  48 MHz  -> EXTREFCLK_DIV2
 *   EXTREF  24 MHz  -> EXTREFCLK
 *   EXTREF  12 MHz  -> EXTREFCLK + PHY multiplier x20   <-- the Blue board
 *   otherwise       -> SYSPLL
 *
 * The Blue board has no HS crystal (AM_BSP_XTAL_HS_FREQ_HZ == 0) and declares
 * AM_BSP_EXTREF_CLK_FREQ_HZ == 12000000, so it takes the fifth branch: one
 * pin, one clock-request line and one PHY bit, with no PLL.
 *
 *   USB->CLKCTRL.PHYREFCLKSEL = EXTREFCLK (3)
 *   USBPHY->REG14.BF55        = 1     x20 rather than the default x40,
 *                                     because the reference is 12 MHz and the
 *                                     PHY is built expecting 24
 *   GP15  in,  funcsel 10 (REFCLK_EXT)      where the clock arrives
 *   GP136 out, driven high                  AM_BSP_GPIO_AP5_12M_CLKREQ
 *
 * The 12 MHz comes from the EM9305 BLE die in the package: GP136, named
 * AM_BSP_GPIO_AP5_12M_CLKREQ in the BSP, requests it, and the die's crystal
 * is the source.  The vendor's clock manager treats EXTREFCLK as available
 * only once the EM9305 has initialised (bIsSipEnabled).
 *
 * With the die in reset the PHY has no reference and cannot present its
 * pull-up, so the host sees nothing, as with an unplugged cable.
 * tiku_usb_up() therefore boots the die for high speed, and a build without
 * the EM9305 driver refuses high speed with TIKU_USB_ERR_CLOCK.
 *
 * The green board's 48 MHz crystal takes the first branch, with no radio
 * involved.
 */

/*---------------------------------------------------------------------------*/
/* INTERRUPT-DRIVEN DESIGN                                                   */
/*---------------------------------------------------------------------------*/
/*
 * The ISR owns the controller:
 *
 *   - INTRTX/INTRRX/INTRUSB are read-to-clear and need a single owner; any
 *     other reader, a register dump included, loses bus resets.
 *   - The host sets the enumeration deadlines, and a device that misses one
 *     gets no error: the host gives up on it.
 *   - The shell can block for seconds (`power emmc bench`, `fat hash` on a
 *     large file), and the device answers the host meanwhile.
 *
 * The split of work:
 *
 *   ISR      bus events; the EP0 state machine; standard requests answered
 *            from static descriptor tables; the CDC pipes and the RAM-disk
 *            MSC transport.  No allocation and no blocking, so enumeration
 *            does not depend on what the shell is doing.
 *   process  the CDC ring drain, and the eMMC-backed MSC transport
 *            (tiku_usb_msc_poll()), whose EP1 interrupts stay masked.
 *
 * The ISR is bounded: no waits, no hang check-in and no SHELL_PRINTF; it
 * counts instead.  The INDEX masking of table 3 applies to process context.
 *
 * The control transfer as the vendor's EP0 state machine describes it (this
 * driver tracks IDLE, TX and RX):
 *      IDLE -> SETUP        on CSR0.OutPktRdy, 8 bytes read from FIFO0
 *      SETUP -> DATA_TX     control-in with a data stage
 *      SETUP -> DATA_RX     control-out with a data stage
 *      SETUP -> STATUS_TX   no data stage
 *      DATA_* -> STATUS_*   when the stage completes
 *      STATUS_* -> IDLE
 * SET_ADDRESS, in the HAL's order: the zero-length status IN packet goes out
 * first, and only then is FADDR written.  An address written before the host
 * has seen the acknowledgement leaves the host on address 0 while the device
 * answers on the new one, and enumeration stalls.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Endpoints this core provides: EP0 plus EP1..EP5, IN and OUT. */
#define TIKU_USB_EP_COUNT       6u
#define TIKU_USB_EP_MAX         5u   /**< highest endpoint number           */

/** @brief EP0 max packet -- fixed at 64 for both speeds. */
#define TIKU_USB_EP0_MAXPACKET  64u

/** @brief FIFO allocation granularity and the units FIFOADD counts in. */
#define TIKU_USB_FIFO_UNIT      8u
#define TIKU_USB_FIFO_FIRST     8u   /**< first free unit; EP0 has 0..7     */

/** @brief Result codes of the USB driver. */
typedef enum {
    TIKU_USB_OK = 0,
    TIKU_USB_ERR_POWER,    /**< a domain never came up                      */
    TIKU_USB_ERR_CLOCK,    /**< PHY reference clock unavailable             */
    TIKU_USB_ERR_TIMEOUT,  /**< a bounded wait expired (not returned)       */
    TIKU_USB_ERR_ARG,      /**< bad speed, class or backing store           */
    TIKU_USB_ERR_STATE,    /**< operation illegal in the current state      */
    TIKU_USB_ERR_FIFO,     /**< FIFO RAM exhausted (not returned)           */
} tiku_usb_err_t;

/** @brief The class the device presents: one at a time, never composite. */
typedef enum {
    TIKU_USB_CLASS_CDC = 0,   /**< ACM serial -- the console               */
    TIKU_USB_CLASS_MSC,       /**< bulk-only mass storage                  */
} tiku_usb_class_t;

/** @brief Bus speed actually negotiated (read from POWER.HSMode). */
typedef enum {
    TIKU_USB_SPEED_NONE = 0,
    TIKU_USB_SPEED_FULL,
    TIKU_USB_SPEED_HIGH,
} tiku_usb_speed_t;

/*---------------------------------------------------------------------------*/
/* BRING-UP, ENUMERATION AND MASS STORAGE                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Counters kept by the ISR, which does not print.
 *
 * irq == 0 means the interrupt never fired (wiring or NVIC); reset > 0 with
 * setup == 0 means the bus is live but EP0 receives nothing.
 */
typedef struct {
    uint32_t irq;        /**< USB interrupts taken                          */
    uint32_t reset;      /**< bus resets seen                               */
    uint32_t setup;      /**< SETUP packets decoded                         */
    uint32_t stall;      /**< requests answered with a stall                */
    uint32_t setupend;   /**< host abandoned a control transfer (normal)    */
    uint32_t suspend;    /**< suspend events                                */
    uint32_t resume;     /**< resume events                                 */
    uint16_t last_req;   /**< bRequest << 8 | bmRequestType, most recent    */
    /** Last four stalled requests: bRequest << 8 | (wValue >> 8).  For
     *  GET_DESCRIPTOR (0x06) the low byte is the descriptor type: 0x0606 is
     *  DEVICE_QUALIFIER and 0x0607 is OTHER_SPEED, which this driver stalls. */
    uint16_t stalled[4];
} tiku_usb_counters_t;

/**
 * @brief Power both domains, release the PHY, clock it, arm interrupts.
 *
 * Table 2's sequence, in its order, presenting CDC.  Leaves the device
 * detached: tiku_usb_attach(1) presents the pull-up to the host.  Returns
 * TIKU_USB_OK at once if USB is already up.
 */
tiku_usb_err_t tiku_usb_up(tiku_usb_speed_t want);

/**
 * @brief Bring up presenting @p cls.  tiku_usb_up() is this with CDC.
 *
 * The class is fixed until tiku_usb_down(); switching means `power usb off`
 * and up again, a detach the host sees.
 */
tiku_usb_err_t tiku_usb_up_as(tiku_usb_speed_t want, tiku_usb_class_t cls);

/**
 * @brief Bring up choosing the MSC backing store too.
 *
 * With @p use_emmc the host is shown the card minus its top
 * TIKU_EMMC_SCRATCH_BLOCKS, so no host format or partition table can reach
 * the scratch region.
 *
 * @return TIKU_USB_OK; ERR_STATE if the card is not identified; ERR_ARG for
 *         a bad speed or class, or eMMC in a build without the eMMC driver;
 *         ERR_POWER or ERR_CLOCK from the bring-up
 */
tiku_usb_err_t tiku_usb_up_full(tiku_usb_speed_t want, tiku_usb_class_t cls,
                                int use_emmc);

/**
 * @brief Process-context pump for MSC over the eMMC.
 *
 * Serves SCSI commands until the host goes quiet, at most 256 per call,
 * waiting up to ~2 ms for each next command.  tiku_usb_up_full() registers it
 * as a shell pump; it returns at once unless MSC is up over the eMMC.
 */
void tiku_usb_msc_poll(void);

/** @brief 1 while MSC presents the eMMC; the fat and emmc shell commands then
 *         refuse to touch the card. */
int tiku_usb_msc_owns_emmc(void);

/** @brief Which class is currently presented. */
tiku_usb_class_t tiku_usb_class(void);

/** @brief Enable/disable the ADMA data path of eMMC mode (default off). */
void tiku_usb_msc_adma(int on);
/** @brief 1 if the ADMA data path is enabled. */
int  tiku_usb_msc_adma_on(void);

/** @brief ADMA transfer and error counts. */
void tiku_usb_msc_dma_stats(uint32_t *xfers, uint32_t *errs);

/** @brief MSC counters: command wrappers, reads, writes, disk size. */
void tiku_usb_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                        uint32_t *blocks);

/**
 * @brief FNV-1a over the first @p nblocks of the RAM disk (0 or more than the
 *        disk = all), to compare with a hash of the same bytes on the host.
 */
uint32_t tiku_usb_msc_hash(uint32_t nblocks);

/**
 * @brief Exercise the LBA bounds check on ranges it must accept and refuse.
 * @return 0 if all cases behaved, else a bitmask with bit n set for each
 *         case n that did not.
 */
uint32_t tiku_usb_msc_selftest(void);

/** @brief Speed requested at bring-up; tiku_usb_speed() is what the chirp
 *         handshake negotiated. */
tiku_usb_speed_t tiku_usb_want(void);

/** @brief Soft-connect (POWER.SOFTCONN): 1 attaches, 0 detaches. */
tiku_usb_err_t tiku_usb_attach(int on);

/** @brief Detach, mask the interrupt, drop the rails and both domains. */
void tiku_usb_down(void);

/** @brief 1 if the USB controller domain is powered. */
int tiku_usb_powered(void);

/** @brief 1 while soft-connected. */
int tiku_usb_attached(void);

/** @brief Speed the chirp handshake settled on (valid after a bus reset). */
tiku_usb_speed_t tiku_usb_speed(void);

/** @brief Device address the host assigned (0 until SET_ADDRESS). */
uint8_t tiku_usb_address(void);

/** @brief Configuration the host selected (0 until SET_CONFIGURATION). */
uint8_t tiku_usb_config(void);

/*---------------------------------------------------------------------------*/
/* THE CDC-ACM CONSOLE                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shell I/O backend for the CDC data pipes.
 *
 * Install with tiku_shell_io_set_backend(&tiku_shell_io_usbcdc).  The
 * backend converts LF to CRLF, echoes, and carries full VFS capabilities.
 */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;

/** @brief Queue one byte to the host (no-op unless configured and DTR set). */
void tiku_usb_cdc_putc(char c);

/** @brief 1 when the host has configured the device and set DTR. */
int tiku_usb_cdc_ready(void);

/**
 * @brief Drain the receive pipe for @p ms of idle, counting bytes only.
 *
 * Measures the transport without involving the shell parser: bytes sent by
 * the host must equal the count returned.  Returns after @p ms with no data.
 */
uint32_t tiku_usb_cdc_sink(uint32_t ms);

/** @brief TX and RX byte counts, TX bytes dropped, and RX back-offs (NAK). */
void tiku_usb_cdc_stats(uint32_t *tx, uint32_t *rx, uint32_t *drop,
                        uint32_t *nak);

/** @brief Snapshot the ISR counters. */
void tiku_usb_counters(tiku_usb_counters_t *out);

/**
 * @brief Snapshot controller registers into @p out[0..n-1].
 *
 * Slot 0 is PWRCTRL.DEVPWRSTATUS; while the controller is unpowered every
 * other slot reads 0xDEADDEAD.  INTRUSB/INTRTX/INTRRX are omitted: they are
 * read-to-clear, and a read here takes events from the ISR.
 */
void tiku_usb_regs(uint32_t *out, unsigned n);

#endif /* TIKU_USB_ARCH_H_ */
