/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510_evb.h - Ambiq Apollo 510 EVB board definitions
 *
 * Pin assignments from the AmbiqSuite apollo510_evb BSP:
 *   - User LEDs: LED0 = pad 165, LED1 = pad 89, LED2 = pad 92
 *     (open-drain / active-low in the BSP).
 *   - Console UART (COM): TX = pad 30, RX = pad 55.
 *   - SWO (default console transport): pad 28.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO510_EVB_H_
#define TIKU_BOARD_APOLLO510_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string exposed via /sys/device. */
#define TIKU_BOARD_NAME             "Apollo510 EVB"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief LED definitions for the Apollo510 EVB.
 *
 * All three EVB user LEDs are mapped: LED0 = pad 165, LED1 = pad 89,
 * LED2 = pad 92 (the TIKU_BOARD_LED1/LED2/LED3 macros are 1-indexed and
 * surface as /dev/led0, /dev/led1, /dev/led2). All EVB LEDs are
 * active-low, so ON drives the pad low and OFF drives it high.
 */
#define TIKU_BOARD_LED_COUNT        3

/** @brief GPIO pad number for LED 1 (active-low, pad 165). */
#define TIKU_BOARD_LED1_PIN         165U
/** @brief Configure LED 1 pad as a push-pull output. */
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
/** @brief Drive LED 1 on (output low — active-low LED). */
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
/** @brief Drive LED 1 off (output high). */
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
/** @brief Toggle LED 1 output state. */
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)

/** @brief LED 2 (-> /dev/led1): active-low, pad 89. */
#define TIKU_BOARD_LED2_PIN         89U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)

/** @brief LED 3 (-> /dev/led2): active-low, pad 92. */
#define TIKU_BOARD_LED3_PIN         92U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)

/*---------------------------------------------------------------------------*/
/* Console UART pins (TX=30, RX=55)                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pin assignments.
 *
 * The default console transport is SWO/ITM (pad 28). The COM-UART
 * pins (TX=30, RX=55) are recorded here for use when a wire-UART
 * backend is selected. Pin mux is performed by am_hal at init time,
 * so the board-level init macro is a no-op.
 */
#define TIKU_BOARD_UART_TX_PIN      30U     /**< UART TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      55U     /**< UART RX pad number. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 4U      /**< FUNCSEL for pads 30/55 -> UART0. */
/** @brief Board-level UART pin mux init (no-op; handled by am_hal). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons (none wired as plain GPIO yet)                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Button stubs — no buttons are wired as plain GPIO on this EVB.
 *
 * Both BTN macros are no-ops / always-not-pressed placeholders. Wire
 * real pads and update these macros when button input is needed.
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
/** @brief Read button 1 state (always 0 — no button wired). */
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
/** @brief Read button 2 state (always 0 — no button wired). */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (placeholder — tiku_bitbang demos)                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter port and pin defaults.
 *
 * The tiku_gpio (port,pin) API encodes an Apollo510 pad as (port-1)*8 + pin,
 * with port >= 1 and pin in 0..7 (see ambiq_pad_of in tiku_gpio_arch.c). The
 * pair below therefore selects pad 13 -- a plain GPIO, clear of the SWO (pad
 * 28) and console UART (pads 30/55) lines, so the bit-bang self-test has a
 * valid, harmless pin to toggle. (A bare pad number like 0/13 is NOT a valid
 * encoding here: port 0 and pin 13 are both rejected.) Override in the build
 * system to point at a real backscatter pad.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       2U   /**< Port 2 -> pad base 8. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        5U   /**< pin 5 -> pad 13. */
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Peripheral bus availability flags and pin assignments.
 *
 * ADC and 1-Wire drivers are stubs at this milestone and are marked
 * unavailable. I2C and SPI pin macros are placeholders (TODO: assign
 * real IOM pads) — they are defined so the interface layer compiles
 * without errors on this target.
 */
/** @brief ADC not available at this milestone (stub driver). */
#define TIKU_BOARD_ADC_AVAILABLE    0
/** @brief I2C bus rate gate (symbolic — 100 kHz capable). */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief 1-Wire not available at this milestone (stub driver). */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire GPIO pad (placeholder). */
#define TIKU_BOARD_OW_PIN           13U

/** @brief I2C0 SDA pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

/*---------------------------------------------------------------------------*/
/* eMMC (U11) on SDIO0                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The data/CLK/CMD pads are identical on both Apollo510 EVBs and agree with
 * both the schematic and the BSP.  RSTn is NOT -- see below, and see TABLE 0
 * in arch/ambiq/tiku_emmc_arch.h for how that disagreement was resolved.
 *
 * The driver walks D0..CLK and D4..CMD as CONTIGUOUS RANGES (84..88, 156..160),
 * so a board that re-pins this bus must keep each run contiguous or teach the
 * driver otherwise; the _Static_assert in tiku_emmc_arch.c enforces it.
 *
 * FUNCSEL is per-pad silicon, not a free choice, and it is NOT uniform across
 * this bus: GP84..GP88 reach SDIF0 on FNCSEL 2, GP156..GP160 on FNCSEL 0.
 * It travels with the pads because a board that moves the bus needs the
 * funcsel matching ITS pads.
 */
#define TIKU_BOARD_EMMC_PAD_D0       84U  /**< DAT0; DAT1/2 at 85/86.      */
#define TIKU_BOARD_EMMC_PAD_D3       87U  /**< DAT3 -- end of low run.     */
#define TIKU_BOARD_EMMC_PAD_CLK      88U  /**< CLK; in the low run.        */
#define TIKU_BOARD_EMMC_PAD_D4      156U  /**< DAT4; DAT5/6 at 157/158.    */
#define TIKU_BOARD_EMMC_PAD_D7      159U  /**< DAT7 -- end of high run.    */
#define TIKU_BOARD_EMMC_PAD_CMD     160U  /**< CMD; in the high run.       */
#define TIKU_BOARD_EMMC_FNCSEL_LOW    2U  /**< GP84..GP88   -> SDIF0.      */
#define TIKU_BOARD_EMMC_FNCSEL_HIGH   0U  /**< GP156..GP160 -> SDIF0.      */
/*
 * RSTn = GP12 on THIS board -- the green EVB schematic says `SDIO0_RSTn_GP12`
 * and the BSP agrees, and GP12 is free here because this board's console is
 * UART0 on pads 30/55 rather than UART1 on 12/14.
 *
 * NOT HARDWARE-VERIFIED: no green EVB has been on the bench.  It is still a
 * correction -- the driver previously hard-coded the BLUE board's GP13, so an
 * eMMC build for this board drove a pad that is not its reset net.  Verify
 * with `power emmc id` when a green board is available.
 */
#define TIKU_BOARD_EMMC_PAD_RST      12U

/*---------------------------------------------------------------------------*/
/* USB device controller -- board-side rails and HS reference                */
/*---------------------------------------------------------------------------*/
/*
 * The controller is silicon; what powers it and what clocks its PHY are not.
 * Both rails are switched from board pads -- DIFFERENT pads than the Blue EVB
 * (91/90 here, 47/48 there), which is why they had to leave the driver.
 *
 * This board carries its own 48 MHz high-speed crystal
 * (AM_BSP_XTAL_HS_FREQ_HZ == 48000000), so it needs no clock-request line and
 * no EM9305: it declares USBHS_CLK_XTAL in BOARD_CAPS and the driver takes the
 * XTALHS_DIV2 reference.  There is deliberately no CLKREQ/REFCLK pad here --
 * defining one would imply a wire this board does not have.
 */
#define TIKU_BOARD_USB_PAD_VDDUSB33   91U  /**< 3.3 V rail switch.           */
#define TIKU_BOARD_USB_PAD_VDDUSB0P9  90U  /**< 0.9 V rail switch.           */

/*---------------------------------------------------------------------------*/
/* PSRAM (U14, 64 MB APS25608N) on MSPI0                                     */
/*---------------------------------------------------------------------------*/
/*
 * ONLY the chip select is here, deliberately.  MSPI0's data, clock and DQS
 * pads (GP64..GP71, GP72, GP73) are DEDICATED to that MSPI instance in
 * silicon -- a board does not get to choose them, it only chooses which MSPI
 * instance to wire the part to.  Listing them here would imply a freedom the
 * hardware does not offer, so they stay device-side in tiku_psram_arch.c.
 *
 * The chip select IS a board choice: MSPI0 exposes several CE lines and the
 * part is strapped to one of them.  Both Apollo510 EVBs happen to use CE0 on
 * GP199; a custom board need not.
 * HARDWARE-VERIFIED on the Blue EVB (64 MB mapped, timing scan, bench).
 */
#define TIKU_BOARD_PSRAM_PAD_CE     199U  /**< MSPI0 CE0. */

/*---------------------------------------------------------------------------*/
/* Octal NOR (U12, 8 MB) on MSPI1                                            */
/*---------------------------------------------------------------------------*/
/*
 * Fitted on THIS board only -- the Blue EVB does not carry U12, declares no
 * NOR capability, and its build is refused by the Makefile.  That is why the
 * Blue board has no matching block: a pin definition for a part that is not
 * on the PCB is exactly the fiction this split exists to end.
 *
 * As with the PSRAM, D0..D7/SCK/DQS are MSPI1's dedicated pads (silicon); the
 * chip select, reset and load-switch lines are board wiring and live here.
 * Note GP54 is MSPI1_CE1 in the BSP's naming but is wired as the NOR reset on
 * this board -- the part uses CE0 (GP53).
 *
 * NOT HARDWARE-VERIFIED: no green EVB has been on the bench.  N1-N5 in
 * kintsugi/mspi-nor-plan.md are still open.
 */
#define TIKU_BOARD_NOR_PAD_D0        95U  /**< D1..D6 follow at 96..101.     */
#define TIKU_BOARD_NOR_PAD_D7       102U
#define TIKU_BOARD_NOR_PAD_CLK      103U
#define TIKU_BOARD_NOR_PAD_DQS      104U
#define TIKU_BOARD_NOR_PAD_CE        53U  /**< MSPI1 CE0.                    */
#define TIKU_BOARD_NOR_PAD_RST       54U  /**< BSP calls this CE1; wired RST.*/
#define TIKU_BOARD_NOR_PAD_LSEN     208U  /**< Load switch enable.           */

#endif /* TIKU_BOARD_APOLLO510_EVB_H_ */
