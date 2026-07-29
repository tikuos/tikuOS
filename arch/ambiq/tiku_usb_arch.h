/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_arch.h - Apollo510 USB 2.0 device controller.
 *
 * A Mentor/Inventra MUSB-class device core -- identified from the POWER bit order,
 * the INDEX-selected per-endpoint CSR window and read-to-clear status -- wrapped by
 * a PHY, two power domains and an auto-DMA engine.  USB is the only bulk path in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USB_ARCH_H_
#define TIKU_USB_ARCH_H_

#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>   /* tiku_shell_io_t: the console backend */
/* The rail pads below come from the BOARD header.  Including the selector here
 * rather than trusting every includer to have pulled tiku.h first is the whole
 * point: tiku_shell_cmd_power.c includes this header directly, and without
 * this line its translation unit saw no board macros and tripped the #error
 * below -- the include-order trap CLAUDE.md warns about, reproduced live. */
#include "tiku_device_select.h" 

/*---------------------------------------------------------------------------*/
/* TABLE 0 -- BOARD: WHICH SOCKET, WHICH RAILS, AND WHICH BOARD              */
/*---------------------------------------------------------------------------*/
/*
 * *** THE EVB HAS TWO USB-C SOCKETS AND ONLY ONE OF THEM IS OURS. ***
 *
 *   J18  "AP5 USB-C Connector"  -- the Apollo5 DEVICE port.  THIS ONE.
 *                                  nets USB0AP50P/N -> USB_AP5_P/N
 *   J16  "USB-C Connector"      -- the on-board J-Link.  Plugging the host
 *                                  here enumerates the DEBUGGER, not us, and
 *                                  the resulting "nothing happened" looks
 *                                  exactly like a driver that never attached.
 *
 * D+/D- are dedicated PHY pins, not GPIO: there is no pad configuration to
 * get wrong here, which is a pleasant change.  What there IS to get wrong:
 *
 * TWO EXTERNAL SUPPLY RAILS MUST BE SWITCHED ON BY GPIO, and the two boards
 * we own disagree about which GPIOs -- for the THIRD time in this port (the
 * NOR reset pin and the eMMC reset pin were the first two).  Verified against
 * both the BSP and the Blue board's schematic, which agree here:
 *
 *              | Apollo510B EVB (Blue)      | Apollo510 EVB (green)
 *   -----------+----------------------------+-----------------------
 *   VDDUSB33   | GP47                       | GP91
 *              | net VDDUSB33_AP5_ON_GP47   |
 *   VDDUSB0P9  | GP48                       | GP90
 *              | net VDDUSB0P9_AP5_ON_GP48  |
 *
 * Both are driven HIGH to enable, and the vendor waits 50 ms after switching
 * them before touching the PHY.  Forget them and the PHY is unpowered: the
 * controller's registers read back perfectly and the bus stays dead, which is
 * the worst diagnostic shape available.
 */

/* The rail switches are BOARD pads and now come from the board header -- the
 * two EVBs use different ones (Blue 47/48, green 91/90), so hard-coding them
 * here silently made the driver Blue-only. */
#if !defined(TIKU_BOARD_USB_PAD_VDDUSB33)
#error "This board declares no USB rail pads (TIKU_BOARD_USB_PAD_VDDUSB*). \
The build system should not have compiled the USB driver for it -- see \
BOARD_CAPS/USB_RAILS in the Makefile."
#endif
#define TIKU_USB_PAD_VDDUSB33   TIKU_BOARD_USB_PAD_VDDUSB33
#define TIKU_USB_PAD_VDDUSB0P9  TIKU_BOARD_USB_PAD_VDDUSB0P9
#define TIKU_USB_RAIL_SETTLE_MS 50u

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- THE REGISTER MAP, AND WHY CMSIS MUST NOT BE USED FOR IT        */
/*---------------------------------------------------------------------------*/
/*
 * *** DO NOT USE THE CMSIS BITFIELD ACCESSORS ON CFG0/CFG1/CFG2. ***
 *
 * This is the single most dangerous thing found in U0, and it is invisible
 * unless you look for it.  The hardware's registers are 8- and 16-bit MUSB
 * registers.  CMSIS has packed four of them into each 32-bit word CFG0..CFG3
 * and named the result after nothing in particular.  Three of those packed
 * fields are READ-TO-CLEAR interrupt status.
 *
 * Therefore:
 *   - `USB->CFG0_b.HSEnab = 1;` is a 32-bit READ-MODIFY-WRITE.  The read
 *     clears INTRTX, silently discarding every pending IN-endpoint interrupt.
 *   - the same applies to CFG1 (INTRRX) and CFG2 (INTRUSB): a bus reset or a
 *     completed transfer can vanish because unrelated code touched a
 *     neighbouring field.
 *
 * The bug this produces is a device that enumerates nine times out of ten.
 * The vendor HAL avoids it by casting to pointers-to-volatile-uint8_t and
 * pointers-to-volatile-uint16_t at byte offsets, and so must we.  ACCESS
 * THESE AT THEIR TRUE WIDTHS.
 *
 * The true map, byte offsets from USB_BASE.  PROVENANCE, because this port
 * has paid repeatedly for values that were derived rather than read: the
 * widths and read-to-clear behaviour come from the HAL's own accessor macros;
 * the offsets were then CHECKED with offsetof() against the CMSIS struct
 * (CFG0/1/2/3 = 0x00/04/08/0C, IDX0/1/2 = 0x10/14/18, FIFOADD = 0x1C,
 * FIFO0..5 = 0x20 + 4n), and the POWER bit order was checked field by field
 * against USB_CFG0_*_Pos.  Nothing below is inferred from context:
 *
 *   off  w   name       notes
 *   ---  --  ---------  ------------------------------------------------
 *   0x00  8  FADDR      FuncAddr[6:0], Update[7]
 *   0x01  8  POWER      see below -- the MUSB signature register
 *   0x02 16  INTRTX     EPn IN complete, bit n.  *** READ-TO-CLEAR ***
 *   0x04 16  INTRRX     EPn OUT complete, bit n. *** READ-TO-CLEAR ***
 *   0x06 16  INTRTXE    IN interrupt enables
 *   0x08 16  INTRRXE    OUT interrupt enables
 *   0x0A  8  INTRUSB    bus events.             *** READ-TO-CLEAR ***
 *   0x0B  8  INTRUSBE   bus event enables
 *   0x0C 16  FRAME      frame number
 *   0x0E  8  INDEX      *** selects which endpoint 0x10..0x1F refer to ***
 *   0x0F  8  TESTMODE   ForceHS / ForceFS / test packet
 *   0x10 16  TXMAXP     |
 *   0x12 16  CSR0/TXCSR | indexed by INDEX  (CMSIS calls the pair IDX0)
 *   0x14 16  RXMAXP     |
 *   0x16 16  RXCSR      | indexed by INDEX  (CMSIS: IDX1)
 *   0x18 16  COUNT0/RXCOUNT + INFIFOSZ[23:16] + OUTFIFOSZ[31:24]  (IDX2)
 *   0x1C     FIFOADD    INFIFOADD[12:0], OUTFIFOADD[28:16], units of 8 B
 *   0x20+4n  FIFO0..5   per-endpoint data ports
 *
 * POWER (0x01), bit by bit -- this is what identifies the core:
 *   0 EnableSuspendM   1 SuspendMode(RO)   2 Resume        3 Reset(RO)
 *   4 HSMode(RO)       5 HSEnab            6 SOFTCONN      7 ISOUpdate
 * CMSIS calls bit 6 "AMSPECIFIC".  It is the soft-connect bit: setting it
 * attaches the pull-up and is how the device becomes visible to the host.
 *
 * INTRUSB (0x0A) bits: 0 Suspend, 1 Resume, 2 Reset, 3 SOF.
 * INTRUSBE (0x0B) uses the same bit order.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- BRING-UP ORDER (and it is an ORDER, not a set)                 */
/*---------------------------------------------------------------------------*/
/*
 *  #  what                                    register / call
 * --  --------------------------------------  --------------------------
 *  1  power the controller domain             PWRCTRL periph USB
 *  2  power the PHY domain                    PWRCTRL periph USBPHY
 *     -- TWO domains.  The SDIO0 lesson generalises: one enable is never
 *        the whole story on this part.
 *  3  write the USB SRAM trim                 USB->SRAMCTRL
 *     -- an undocumented magic value the vendor writes unconditionally:
 *        WABL=1 WABLM=1 RAWL=1 RAWLM=2 EMAW=0 EMAS=0 EMA=3 RET1N=1.
 *        Transcribed, not derived; the FIFO RAM is what it tunes.  ("Three
 *        registers we never wrote" is already a title in this project.)
 *  4  HOLD the PHY in reset                   MCUCTRL->USBRSTCTRL: CLEAR
 *                                             USBRSTENABLE, USBPORRSTRELEASE,
 *                                             USBUTMIRSTRELEASE
 *     -- note the vendor's naming is inverted from its effect: the function
 *        called "enable_phy_reset_override" CLEARS these bits and thereby
 *        HOLDS the PHY in reset.  Our names will say what they do.
 *  5  switch the external rails on            GP47 high, GP48 high
 *  6  wait                                    50 ms
 *  7  disconnect battery-charger detection    USB->BCDETCRTL1 = USBSWRESET=1,
 *                                             every other field 0
 *     -- the BC circuit sits on D+/D-.  Left connected, enumeration fails
 *        with no error anywhere.
 *  8  RELEASE the PHY from reset              MCUCTRL->USBRSTCTRL: SET the
 *                                             three bits from step 4
 *  9  select + enable the PHY reference clock see table 5
 * 10  set the speed                           POWER.HSEnab per table 5
 * 11  enable the Reset bus interrupt          INTRUSBE bit 2
 * 12  ATTACH                                  POWER.SOFTCONN = 1
 *
 * Steps 4-8 exist in that order because of Apollo4 errata ERR041 (an induced
 * D+ output pulse can cause an unintended disconnect); the vendor cites it in
 * the power-up path.  Do not "simplify" the sequence.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 3 -- ENDPOINTS, FIFO, AND THE INDEX REGISTER HAZARD                 */
/*---------------------------------------------------------------------------*/
/*
 * SIX endpoints: EP0 (control, bidirectional) plus EP1..EP5, each of which
 * has an independent IN and OUT half.  Ten ADMA channels (ADMAEP0..9) cover
 * the five IN and five OUT halves.  MSC needs one bulk IN and one bulk OUT;
 * CDC needs two bulk plus a notification endpoint.  Room to spare either way.
 *
 * EP0's interrupt is reported in INTRTX bit 0 and enabled in INTRTXE bit 0 --
 * the OUT-side registers play no part for EP0.
 *
 * *** THE INDEX REGISTER IS SHARED MUTABLE STATE. ***
 * INDEX (0x0E) selects which endpoint the CSR window at 0x10..0x1F refers to.
 * Every access to TXMAXP/TXCSR/RXMAXP/RXCSR/COUNT0/FIFOADD is therefore a
 * two-step non-atomic operation, and an interrupt landing between the two
 * steps corrupts BOTH.  Since this driver is interrupt-driven (see below),
 * the rule is absolute:
 *
 *   - the ISR may set INDEX freely; it is not interrupted by itself
 *   - process-context code that touches an indexed register MUST mask the
 *     USB interrupt across the whole INDEX-then-access sequence
 *
 * This is the same hazard class as the VFS watch table, and it is the first
 * thing to suspect if endpoints start behaving as though they were a
 * different endpoint.
 *
 * FIFO ALLOCATION is manual and cumulative:
 *   - addresses and sizes are in units of 8 BYTES
 *   - allocation starts at unit 8 (byte 64): the first 64 bytes belong to EP0
 *   - size code = log2(maxpacket / 8); the size table is
 *     8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes
 *   - double buffering is optional per endpoint per direction and DOUBLES
 *     that endpoint's consumption
 *   - FIFOADD is 13 bits in 8-byte units, i.e. a 64 KB address space; the
 *     RAM actually fitted is a build option of the core and is MEASURED in
 *     U1 (allocate until it stops working), not assumed
 *
 * Budget for the MSC configuration at high speed:
 *      EP0            64 B
 *      bulk IN  512  512 B   (1024 if double-buffered)
 *      bulk OUT 512  512 B   (1024 if double-buffered)
 *                  ------
 *                  1088 B single-buffered, 2112 B double-buffered
 *
 * A bus reset RESETS THE ALLOCATION -- every endpoint must be re-armed and
 * the running pointer returned to unit 8.  Forgetting this leaks FIFO space
 * across replugs, which is precisely why "works once, fails on replug" is a
 * gate in the plan rather than an afterthought.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 4 -- INTERRUPTS                                                     */
/*---------------------------------------------------------------------------*/
/*
 * ONE NVIC vector for the whole controller: USB0_IRQn.  Five status sources
 * are read inside it:
 *
 *   INTRUSB          bus events: Suspend, Resume, Reset, SOF   (read-to-clear)
 *   INTRTX           IN endpoint n complete, bit n             (read-to-clear)
 *   INTRRX           OUT endpoint n complete, bit n            (read-to-clear)
 *   ADMACMPINTSTAT   auto-DMA channel completion               (write-1-clear)
 *   ADMAERRINTSTAT   auto-DMA channel error                    (write-1-clear)
 *
 * READ-TO-CLEAR MEANS READ EXACTLY ONCE.  Each of the first three must be
 * read into a local at the top of the ISR and then only that local consulted.
 * Reading INTRTX twice loses whatever arrived between the reads; reading it
 * from anywhere but the ISR loses events entirely.  This is the same shape as
 * the eMMC's INTENABLE trap (a register whose behaviour depends on how you
 * look at it), and it is why polling is not merely slow here but wrong.
 *
 * ON BUS RESET the controller does NOT tidy up for us.  The handler must:
 *   - abort every in-flight transfer and tell the upper layer
 *   - reset the EP0 state machine to IDLE
 *   - re-init EP0 with a 64-byte max packet
 *   - DISABLE all per-endpoint interrupts (they are re-enabled by
 *     SET_CONFIGURATION)
 *   - disable SOF (enable only if some class actually wants it)
 *   - reset the FIFO allocation pointer to unit 8
 *   - re-enable the Suspend interrupt, and DISCARD any suspend event in the
 *     same batch -- reset and suspend arrive together and the suspend is
 *     spurious
 *   - read POWER.HSMode to learn what speed was negotiated
 */

/*---------------------------------------------------------------------------*/
/* TABLE 5 -- SPEED, AND THE PHY REFERENCE CLOCK THIS BOARD CANNOT PROVIDE   */
/*---------------------------------------------------------------------------*/
/*
 * Speed is negotiated by the chirp handshake in HARDWARE.  Software's part is
 * small: set POWER.HSEnab before attaching, then read POWER.HSMode after the
 * bus reset to discover what was agreed.  TIMEOUT1 (chirp) and TIMEOUT2 (HS
 * resume delay) are configurable and left at their defaults for now.
 *
 * The PHY needs a reference clock, chosen in USB->CLKCTRL.PHYREFCLKSEL[26:24]:
 *   0 HFRC_48MHz   1 HFRC2_31MHz   2 HFRC_24MHz   3 EXTREFCLK
 *   4 EXTREFCLK_DIV2   5 XTALHS   6 XTALHS_DIV2   7 OFF
 * plus PHYREFCLKDIS[0], CTRLAPBCLKDIS[8], PHYAPBLCLKDIS[16] as gates.
 *
 * FULL SPEED wants HFRC at 24 MHz.  That is an internal oscillator, always
 * available, no dependencies.  This is why U1 targets FS.
 *
 * *** HIGH SPEED: U0 GOT THIS WRONG AND U1b CORRECTED IT ON HARDWARE. ***
 *
 * U0 traced the vendor's clock-source logic through its crystal cases, found
 * that this board has no HS crystal (AM_BSP_XTAL_HS_FREQ_HZ == 0), and
 * concluded that HS therefore falls through to the SYSPLL.  That was reading
 * three branches and stopping before the fourth.  The real selection is:
 *
 *   XTAL_HS 48 MHz  -> XTALHS_DIV2          (the GREEN board's path)
 *   XTAL_HS 24 MHz  -> XTALHS
 *   EXTREF  48 MHz  -> EXTREFCLK_DIV2
 *   EXTREF  24 MHz  -> EXTREFCLK
 *   EXTREF  12 MHz  -> EXTREFCLK + PHY multiplier x20   <-- THE BLUE BOARD
 *   otherwise       -> SYSPLL
 *
 * The Blue board declares AM_BSP_EXTREF_CLK_FREQ_HZ == 12000000, so it takes
 * the fifth branch and never reaches the PLL.  No PLL bring-up, no lock wait:
 * high speed costs one pin, one clock-request line, and one PHY bit.
 *
 *   USB->CLKCTRL.PHYREFCLKSEL = EXTREFCLK (3)
 *   USBPHY->REG14.BF55        = 1     x20 rather than the default x40,
 *                                     because the reference is 12 MHz and the
 *                                     PHY is built expecting 24
 *   GP15  in,  funcsel 10 (REFCLK_EXT)      where the clock arrives
 *   GP136 out, driven high                  AM_BSP_GPIO_AP5_12M_CLKREQ
 *
 * *** AND THE 12 MHz COMES FROM THE BLE RADIO DIE. ***
 *
 * GP136 is named AM_BSP_GPIO_AP5_12M_CLKREQ in the BSP: it REQUESTS the clock
 * from the EM9305 in the package.  The vendor's clock manager only treats
 * EXTREFCLK as available once bIsSipEnabled is set, which happens only after
 * the EM9305 initialises -- and that is not bookkeeping, it is physics.  The
 * die's crystal IS the source.
 *
 * Measured, because a dependency this surprising deserves evidence rather
 * than inference: requesting HS with the die still in reset produced irq 0,
 * reset 0, and a host that saw nothing at all -- with no reference the
 * PHY cannot even present its pull-up, so it is indistinguishable from an
 * unplugged cable.  Booting the die first made the identical code enumerate
 * at high speed on the next attempt.  tiku_usb_up() therefore boots it, and
 * a build without the EM9305 driver refuses HS with ERR_CLOCK rather than
 * failing silently.
 *
 * The GREEN board needs none of this: its 48 MHz crystal takes the first
 * branch, so HS there is a two-line change with no radio involved.  A fourth
 * board difference, and the largest one yet.
 *
 * Splitting U1b from U1 paid for itself regardless: the first HS attempt
 * failed completely, and because full speed was already proven on the same
 * code the only suspect was the reference clock.  Folded together, "no
 * enumeration" would have had a dozen suspects instead of one.
 */

/*---------------------------------------------------------------------------*/
/* THE DECISION U0 EXISTS TO MAKE: INTERRUPT-DRIVEN, AND IT IS NOT CLOSE     */
/*---------------------------------------------------------------------------*/
/*
 * Every other driver in this port polls.  This one cannot, for four reasons,
 * in increasing order of how badly they end the argument:
 *
 * 1. THE STATUS REGISTERS ARE READ-TO-CLEAR.  Any reader that is not the
 *    single owner of INTRTX/INTRRX/INTRUSB destroys events for everyone else.
 *    A poll loop that races with anything -- including a diagnostic that
 *    dumps registers -- loses bus resets.
 *
 * 2. THE HOST HAS TIMEOUTS AND WE DO NOT CONTROL THEM.  Enumeration is a
 *    conversation with deadlines set by the other end.  Missing them does not
 *    produce an error; it produces a device the host gives up on.
 *
 * 3. THE SHELL BLOCKS FOR SECONDS.  `power emmc bench` occupies the shell
 *    process for over two seconds; a 54 MB stage for 2.2 s; `fat hash` on a
 *    large file for longer.  A polled USB device would drop off the bus every
 *    time the board did anything interesting -- and "interesting" here means
 *    exactly the operations USB exists to feed.
 *
 * 4. IT IS THE WHOLE POINT.  MSC must serve the host WHILE the board is doing
 *    something else.  A polled implementation is not a slower version of that
 *    feature; it is a different, useless feature.
 *
 * SO: the ISR owns the controller.  The split of work:
 *
 *   ISR      bus events; the EP0 state machine (IDLE -> SETUP -> DATA_RX /
 *            DATA_TX -> STATUS_RX / STATUS_TX); standard requests answered
 *            from static descriptor tables; ADMA completion bookkeeping.
 *            Small, table-driven, no allocation, no blocking -- enumeration
 *            must never depend on what the shell is doing.
 *   process  class logic: MSC command dispatch, SCSI, the block backend.
 *            Bulk DATA never passes through the CPU (ADMA moves it), so only
 *            CBW/CSW-sized decisions are deferred, and the ring absorbs the
 *            latency.
 *
 * This makes tiku_usb the port's first genuinely interrupt-driven driver, so
 * two disciplines arrive with it: the INDEX-masking rule in table 3, and a
 * bounded ISR (no waits, no hang_checkin, no SHELL_PRINTF from interrupt
 * context -- diagnostics go through a ring the process drains).
 *
 * The EP0 state machine, transcribed:
 *      IDLE -> SETUP        on CSR0.OutPktRdy, 8 bytes read from FIFO0
 *      SETUP -> DATA_TX     control-in with a data stage
 *      SETUP -> DATA_RX     control-out with a data stage
 *      SETUP -> STATUS_TX   no data stage
 *      DATA_* -> STATUS_*   when the stage completes
 *      STATUS_* -> IDLE
 * SET_ADDRESS is the classic trap and the HAL confirms the order: send the
 * zero-length status IN packet FIRST, and only then write FADDR.  Writing the
 * address before the host has seen the acknowledgement means the host talks
 * to address 0 while we answer on the new one, and enumeration stalls.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS -- what U1 will implement                                */
/*---------------------------------------------------------------------------*/

/** @brief Endpoints this core provides: EP0 plus EP1..EP5, IN and OUT. */
#define TIKU_USB_EP_COUNT       6u
#define TIKU_USB_EP_MAX         5u

/** @brief EP0 max packet -- fixed at 64 for both speeds. */
#define TIKU_USB_EP0_MAXPACKET  64u

/** @brief FIFO allocation granularity and the units FIFOADD counts in. */
#define TIKU_USB_FIFO_UNIT      8u
#define TIKU_USB_FIFO_FIRST     8u   /* units; the first 64 B are EP0's      */

/** @brief Result codes -- distinct causes stay distinct. */
typedef enum {
    TIKU_USB_OK = 0,
    TIKU_USB_ERR_POWER,    /**< a domain never came up                      */
    TIKU_USB_ERR_CLOCK,    /**< PHY reference clock unavailable / no lock   */
    TIKU_USB_ERR_TIMEOUT,  /**< a bounded wait expired                      */
    TIKU_USB_ERR_ARG,      /**< bad endpoint, size, or descriptor           */
    TIKU_USB_ERR_STATE,    /**< operation illegal in the current state      */
    TIKU_USB_ERR_FIFO,     /**< FIFO RAM exhausted by the requested config  */
} tiku_usb_err_t;

/**
 * @brief Which class the device presents.  One at a time, on purpose.
 *
 * A composite CDC+MSC device is possible and is NOT what U3 builds: a host
 * that fails to bind one function on a composite device is markedly harder
 * to diagnose than one that fails to bind the only function present.
 */
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
/* API -- the U1 surface (bring-up + enumeration).  Classes land in U2/U3.   */
/*---------------------------------------------------------------------------*/

/** @brief ISR counters -- the whole diagnostic surface of an ISR that cannot
 *         print.  A host that never enumerates leaves a specific fingerprint
 *         here: irq==0 means the interrupt never fired (wiring/NVIC);
 *         reset>0 with setup==0 means the bus is live but EP0 is deaf. */
typedef struct {
    uint32_t irq;        /**< USB interrupts taken                          */
    uint32_t reset;      /**< bus resets seen                               */
    uint32_t setup;      /**< SETUP packets decoded                         */
    uint32_t stall;      /**< requests answered with a stall                */
    uint32_t setupend;   /**< host abandoned a control transfer (normal)    */
    uint32_t suspend;
    uint32_t resume;
    uint16_t last_req;   /**< bRequest << 8 | bmRequestType, most recent    */
    /** Last four stalled requests: bRequest << 8 | (wValue >> 8).  For
     *  GET_DESCRIPTOR (0x06) the low byte is the descriptor TYPE, so 0x0606
     *  is DEVICE_QUALIFIER and 0x0607 is OTHER_SPEED -- both of which a
     *  full-speed-only device is REQUIRED to stall. */
    uint16_t stalled[4];
} tiku_usb_counters_t;

/**
 * @brief Power both domains, release the PHY, clock it, arm interrupts.
 *
 * Table 2's sequence, in its order.  Leaves the device DETACHED: call
 * tiku_usb_attach(1) to present the pull-up and let the host find us, so
 * that bring-up can be inspected before the bus starts making demands.
 */
tiku_usb_err_t tiku_usb_up(tiku_usb_speed_t want);

/**
 * @brief Bring up presenting @p cls.  tiku_usb_up() is this with CDC.
 *
 * The class is fixed for the lifetime of the bring-up: switching means
 * `power usb off` and up again, which is a real detach the host will notice.
 */
tiku_usb_err_t tiku_usb_up_as(tiku_usb_speed_t want, tiku_usb_class_t cls);

/**
 * @brief Bring up choosing the MSC backing store too.
 *
 * With @p use_emmc the host is shown the card MINUS the scratch region, so
 * the scratch blocks are unreachable by construction rather than by
 * convention -- no host format or partition table can touch them.
 */
tiku_usb_err_t tiku_usb_up_full(tiku_usb_speed_t want, tiku_usb_class_t cls,
                                int use_emmc);

/**
 * @brief Process-context pump: at most one SCSI command per call.
 *
 * Install on the scheduler idle hook.  The eMMC answers in milliseconds, so
 * its data phase cannot live in the ISR; this is where it lives instead.
 */
void tiku_usb_msc_poll(void);

/** @brief 1 while the host owns the card -- board-side access must refuse. */
int tiku_usb_msc_owns_emmc(void);

/** @brief Which class is currently presented. */
tiku_usb_class_t tiku_usb_class(void);

/** @brief Enable/disable the ADMA data path (default on).  For measuring. */
void tiku_usb_msc_adma(int on);
int  tiku_usb_msc_adma_on(void);

/** @brief ADMA transfer and error counts. */
void tiku_usb_msc_dma_stats(uint32_t *xfers, uint32_t *errs);

/** @brief MSC counters: command wrappers, reads, writes, disk size. */
void tiku_usb_msc_stats(uint32_t *cbw, uint32_t *rd, uint32_t *wr,
                        uint32_t *blocks);

/**
 * @brief FNV-1a over the first @p nblocks of the RAM disk (0 = all).
 *
 * The U3 gate is not "the host mounted it" but "what the host wrote is what
 * the board holds", and only a hash on both sides can say that.
 */
uint32_t tiku_usb_msc_hash(uint32_t nblocks);

/**
 * @brief Exercise the LBA bounds check against ranges that must be refused.
 * @return 0 if all cases behaved, else a bitmask naming the ones that did not.
 */
uint32_t tiku_usb_msc_selftest(void);

/** @brief Speed REQUESTED at bring-up; tiku_usb_speed() is what was
 *         actually negotiated by the chirp handshake. */
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
/* U2 -- the CDC-ACM console                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shell I/O backend for the CDC data pipes.
 *
 * Install with tiku_shell_io_set_backend(&tiku_shell_io_usbcdc).  The shell
 * itself needs no change: it has been transport-agnostic since it was
 * written, and this is simply another channel.
 */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;

/** @brief Queue one byte to the host (no-op unless configured and DTR set). */
void tiku_usb_cdc_putc(char c);

/** @brief 1 when the host has configured us AND opened the terminal (DTR). */
int tiku_usb_cdc_ready(void);

/**
 * @brief Drain the receive pipe for @p ms of idle, counting bytes only.
 *
 * Measures the transport without involving the shell parser: bytes sent by
 * the host must equal the count returned.  Returns after @p ms with no data.
 */
uint32_t tiku_usb_cdc_sink(uint32_t ms);

/** @brief Byte counters and the NAK count -- flow control made visible. */
void tiku_usb_cdc_stats(uint32_t *tx, uint32_t *rx, uint32_t *drop,
                        uint32_t *nak);

/** @brief Snapshot the ISR counters. */
void tiku_usb_counters(tiku_usb_counters_t *out);

/**
 * @brief Snapshot host registers (power-safe: 0xDEADDEAD when down).
 *
 * Deliberately omits INTRUSB/INTRTX/INTRRX: they are read-to-clear, and a
 * diagnostic that steals interrupts from the ISR causes the bug it is
 * looking for.
 */
void tiku_usb_regs(uint32_t *out, unsigned n);

#endif /* TIKU_USB_ARCH_H_ */
