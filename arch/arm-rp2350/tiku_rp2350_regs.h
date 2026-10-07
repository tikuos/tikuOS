/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rp2350_regs.h - hand-written RP2350 register definitions.
 *
 * Addresses, masks and helpers for the peripherals the kernel drives
 * bare-metal; neither the Pico SDK nor CMSIS is pulled in.  Sourced from the
 * RP2350 datasheet.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_REGS_H_
#define TIKU_RP2350_REGS_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/*
 * Atomic register access aliases and accessor macros.
 *
 * Each APB and AHB peripheral block exposes four aliases at a 0x1000-byte
 * stride: normal RW at +0x0000, XOR at +0x1000, atomic SET at +0x2000 and
 * atomic CLEAR at +0x3000.  A write to SET or CLR changes only the given
 * bits, with no read-modify-write for an interrupt to split.  SIO and the
 * Cortex-M33 PPB have no aliases: SIO has its own _SET/_CLR/_XOR registers.
 * _RP2350_REG_SET / _CLR / _XOR use the aliases; _RP2350_REG is plain RW.
 */
#define RP2350_REG_ALIAS_RW       0x0000U
#define RP2350_REG_ALIAS_XOR      0x1000U
#define RP2350_REG_ALIAS_SET      0x2000U
#define RP2350_REG_ALIAS_CLR      0x3000U

#define _RP2350_REG(addr)         (*(volatile uint32_t *)(addr))
#define _RP2350_REG_SET(addr, m)  (*(volatile uint32_t *)((addr) + RP2350_REG_ALIAS_SET) = (m))
#define _RP2350_REG_CLR(addr, m)  (*(volatile uint32_t *)((addr) + RP2350_REG_ALIAS_CLR) = (m))
#define _RP2350_REG_XOR(addr, m)  (*(volatile uint32_t *)((addr) + RP2350_REG_ALIAS_XOR) = (m))

/*---------------------------------------------------------------------------*/
/* PERIPHERAL BASE ADDRESSES                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Base addresses of the peripheral blocks this port uses (RP2350 datasheet
 * §2, memory map).  The Cortex-M33 private peripheral bus (PPB) is at
 * 0xE0000000 and has no atomic aliases.
 */
#define RP2350_RESETS_BASE          0x40020000UL
#define RP2350_PSM_BASE             0x40018000UL
#define RP2350_CLOCKS_BASE          0x40010000UL
#define RP2350_XOSC_BASE            0x40048000UL
#define RP2350_PLL_SYS_BASE         0x40050000UL
#define RP2350_PLL_USB_BASE         0x40058000UL
#define RP2350_IO_BANK0_BASE        0x40028000UL
#define RP2350_PADS_BANK0_BASE      0x40038000UL
#define RP2350_SIO_BASE             0xD0000000UL
#define RP2350_UART0_BASE           0x40070000UL
#define RP2350_UART1_BASE           0x40078000UL
#define RP2350_TIMER0_BASE          0x400B0000UL
#define RP2350_TIMER1_BASE          0x400B8000UL
#define RP2350_WATCHDOG_BASE        0x400D8000UL
#define RP2350_ADC_BASE             0x400A0000UL
#define RP2350_I2C0_BASE            0x40090000UL
#define RP2350_I2C1_BASE            0x40098000UL
#define RP2350_SPI0_BASE            0x40080000UL
#define RP2350_SPI1_BASE            0x40088000UL
#define RP2350_TICKS_BASE           0x40108000UL
#define RP2350_PWM_BASE             0x400A8000UL
#define RP2350_DMA_BASE             0x50000000UL
#define RP2350_PIO0_BASE            0x50200000UL
#define RP2350_PIO1_BASE            0x50300000UL
#define RP2350_PIO2_BASE            0x50400000UL
#define RP2350_TRNG_BASE            0x400F0000UL

/* Cortex-M33 SCS / NVIC / SysTick / SCB live at the standard
 * private peripheral bus addresses. */
#define RP2350_PPB_BASE             0xE0000000UL
#define RP2350_SYST_BASE            (RP2350_PPB_BASE + 0xE010UL)
#define RP2350_NVIC_BASE            (RP2350_PPB_BASE + 0xE100UL)
#define RP2350_SCB_BASE             (RP2350_PPB_BASE + 0xED00UL)
#define RP2350_MPU_BASE             (RP2350_PPB_BASE + 0xED90UL)
#define RP2350_SCB_BASE             (RP2350_PPB_BASE + 0xED00UL)

/*---------------------------------------------------------------------------*/
/* RESETS BLOCK                                                              */
/*---------------------------------------------------------------------------*/

/*
 * RESETS block — peripheral reset control (datasheet §6).
 *
 * Setting a bit in RESETS_RESET holds the peripheral in reset; clearing
 * it releases reset.  RESETS_RESET_DONE is read-back: a bit goes high
 * once the peripheral reports it is out of reset.
 */
#define RP2350_RESETS_RESET         (RP2350_RESETS_BASE + 0x00U)
#define RP2350_RESETS_WDSEL         (RP2350_RESETS_BASE + 0x04U)
#define RP2350_RESETS_RESET_DONE    (RP2350_RESETS_BASE + 0x08U)

/*
 * RESETS_RESET bit positions, as in pico-sdk's
 * src/rp2350/hardware_regs/include/hardware/regs/resets.h (bits 0..28).
 */
#define RP2350_RESETS_ADC           (1U <<  0)
#define RP2350_RESETS_BUSCTRL       (1U <<  1)
#define RP2350_RESETS_DMA           (1U <<  2)
#define RP2350_RESETS_HSTX          (1U <<  3)
#define RP2350_RESETS_I2C0          (1U <<  4)
#define RP2350_RESETS_I2C1          (1U <<  5)
#define RP2350_RESETS_IO_BANK0      (1U <<  6)
#define RP2350_RESETS_IO_QSPI       (1U <<  7)
#define RP2350_RESETS_JTAG          (1U <<  8)
#define RP2350_RESETS_PADS_BANK0    (1U <<  9)
#define RP2350_RESETS_PADS_QSPI     (1U << 10)
#define RP2350_RESETS_PIO0          (1U << 11)
#define RP2350_RESETS_PIO1          (1U << 12)
#define RP2350_RESETS_PIO2          (1U << 13)
#define RP2350_RESETS_PLL_SYS       (1U << 14)
#define RP2350_RESETS_PLL_USB       (1U << 15)
#define RP2350_RESETS_PWM           (1U << 16)
#define RP2350_RESETS_SHA256        (1U << 17)
#define RP2350_RESETS_SPI0          (1U << 18)
#define RP2350_RESETS_SPI1          (1U << 19)
#define RP2350_RESETS_SYSCFG        (1U << 20)
#define RP2350_RESETS_SYSINFO       (1U << 21)
#define RP2350_RESETS_TBMAN         (1U << 22)
#define RP2350_RESETS_TIMER0        (1U << 23)
#define RP2350_RESETS_TIMER1        (1U << 24)
#define RP2350_RESETS_TRNG          (1U << 25)
#define RP2350_RESETS_UART0         (1U << 26)
#define RP2350_RESETS_UART1         (1U << 27)
#define RP2350_RESETS_USBCTRL       (1U << 28)

/*---------------------------------------------------------------------------*/
/* CLOCKS BLOCK                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Clocks block — clock source selection and dividers (datasheet §5).
 *
 * Each clock domain has a CTRL (source mux + enable), DIV (integer /
 * fractional divider), and SELECTED (readback of the active source).
 * CLK_SYS drives the CPU and most peripherals, I2C included; CLK_PERI drives
 * UART and SPI, and this port runs it from clk_sys undivided.
 * Field macros (RP2350_CLK_*_AUXSRC_*, RP2350_CLK_*_SRC_*) select the
 * clock source written into the CTRL.AUXSRC or CTRL.SRC bit-fields.
 */
#define RP2350_CLK_GPOUT0_CTRL      (RP2350_CLOCKS_BASE + 0x00U)
#define RP2350_CLK_GPOUT0_DIV       (RP2350_CLOCKS_BASE + 0x04U)
#define RP2350_CLK_GPOUT0_SELECTED  (RP2350_CLOCKS_BASE + 0x08U)
#define RP2350_CLK_REF_CTRL         (RP2350_CLOCKS_BASE + 0x30U)
#define RP2350_CLK_REF_DIV          (RP2350_CLOCKS_BASE + 0x34U)
#define RP2350_CLK_REF_SELECTED     (RP2350_CLOCKS_BASE + 0x38U)
#define RP2350_CLK_SYS_CTRL         (RP2350_CLOCKS_BASE + 0x3CU)
#define RP2350_CLK_SYS_DIV          (RP2350_CLOCKS_BASE + 0x40U)
#define RP2350_CLK_SYS_SELECTED     (RP2350_CLOCKS_BASE + 0x44U)
#define RP2350_CLK_PERI_CTRL        (RP2350_CLOCKS_BASE + 0x48U)
#define RP2350_CLK_PERI_DIV         (RP2350_CLOCKS_BASE + 0x4CU)
#define RP2350_CLK_PERI_SELECTED    (RP2350_CLOCKS_BASE + 0x50U)
#define RP2350_CLK_HSTX_CTRL        (RP2350_CLOCKS_BASE + 0x54U)
#define RP2350_CLK_HSTX_DIV         (RP2350_CLOCKS_BASE + 0x58U)
#define RP2350_CLK_HSTX_SELECTED    (RP2350_CLOCKS_BASE + 0x5CU)
#define RP2350_CLK_USB_CTRL         (RP2350_CLOCKS_BASE + 0x60U)
#define RP2350_CLK_USB_DIV          (RP2350_CLOCKS_BASE + 0x64U)
#define RP2350_CLK_USB_SELECTED     (RP2350_CLOCKS_BASE + 0x68U)
#define RP2350_CLK_ADC_CTRL         (RP2350_CLOCKS_BASE + 0x6CU)
#define RP2350_CLK_ADC_DIV          (RP2350_CLOCKS_BASE + 0x70U)
#define RP2350_CLK_ADC_SELECTED     (RP2350_CLOCKS_BASE + 0x74U)

/* CLK_ADC_CTRL fields (ENABLE and AUXSRC at the CLK_PERI_CTRL positions) */
#define RP2350_CLK_ADC_ENABLE           (1U << 11)
#define RP2350_CLK_ADC_AUXSRC_PLL_USB   (0U << 5)
#define RP2350_CLK_ADC_AUXSRC_PLL_SYS   (1U << 5)
#define RP2350_CLK_ADC_AUXSRC_ROSC      (2U << 5)
#define RP2350_CLK_ADC_AUXSRC_XOSC      (3U << 5)

/* CLK_SYS_CTRL fields */
#define RP2350_CLK_SYS_AUXSRC_PLL_SYS   (0U << 5)
#define RP2350_CLK_SYS_AUXSRC_PLL_USB   (1U << 5)
#define RP2350_CLK_SYS_AUXSRC_ROSC      (2U << 5)
#define RP2350_CLK_SYS_AUXSRC_XOSC      (3U << 5)
#define RP2350_CLK_SYS_SRC_REF          (0U)
#define RP2350_CLK_SYS_SRC_AUX          (1U)

/* CLK_GPOUT0 AUXSRC values: the clock routed out of the GPOUT pin */
#define RP2350_CLK_GPOUT_AUXSRC_PLL_SYS  (0U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_GPIN0    (1U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_GPIN1    (2U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_PLL_USB  (3U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_ROSC     (5U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_XOSC     (6U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_SYS  (8U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_USB  (9U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_ADC  (10U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_REF  (11U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_PERI (12U << 5)
#define RP2350_CLK_GPOUT_AUXSRC_CLK_HSTX (13U << 5)
#define RP2350_CLK_GPOUT_DC50            (1U << 12)  /* duty-cycle correction */
#define RP2350_CLK_GPOUT_KILL            (1U << 10)
#define RP2350_CLK_GPOUT_ENABLE          (1U << 11)

/* CLK_REF_CTRL fields */
#define RP2350_CLK_REF_SRC_ROSC         (0U)
#define RP2350_CLK_REF_SRC_AUX          (1U)
#define RP2350_CLK_REF_SRC_XOSC         (2U)
#define RP2350_CLK_REF_AUXSRC_PLL_USB   (0U << 5)
#define RP2350_CLK_REF_AUXSRC_GPIN0     (1U << 5)
#define RP2350_CLK_REF_AUXSRC_GPIN1     (2U << 5)

/* CLK_PERI_CTRL fields */
#define RP2350_CLK_PERI_ENABLE          (1U << 11)
#define RP2350_CLK_PERI_AUXSRC_CLK_SYS  (0U << 5)
#define RP2350_CLK_PERI_AUXSRC_PLL_SYS  (1U << 5)
#define RP2350_CLK_PERI_AUXSRC_PLL_USB  (2U << 5)
#define RP2350_CLK_PERI_AUXSRC_ROSC     (3U << 5)
#define RP2350_CLK_PERI_AUXSRC_XOSC     (4U << 5)

/*---------------------------------------------------------------------------*/
/* XOSC                                                                      */
/*---------------------------------------------------------------------------*/

/*
 * Crystal oscillator (XOSC), the 12 MHz reference (datasheet §5.3).
 *
 * Enable by writing 0xFAB000 to CTRL (with the 1-15 MHz range code
 * 0xAA0 in the low field); disable with 0xD1E000.  Poll
 * XOSC_STATUS.STABLE (bit 31) before switching CLK_SYS to use it.
 */
#define RP2350_XOSC_CTRL            (RP2350_XOSC_BASE + 0x00U)
#define RP2350_XOSC_STATUS          (RP2350_XOSC_BASE + 0x04U)
#define RP2350_XOSC_DORMANT         (RP2350_XOSC_BASE + 0x08U)
#define RP2350_XOSC_STARTUP         (RP2350_XOSC_BASE + 0x0CU)

/* XOSC_CTRL: enable + freq range. The 0xFAB code is the "enable"
 * magic; 0xD1E disables; 0xAA0 marks the 1-15 MHz range. */
#define RP2350_XOSC_CTRL_ENABLE     (0xFAB000U)
#define RP2350_XOSC_CTRL_DISABLE    (0xD1E000U)
#define RP2350_XOSC_CTRL_FREQ_1_15  (0xAA0U)
#define RP2350_XOSC_STATUS_STABLE   (1U << 31)

/*---------------------------------------------------------------------------*/
/* PLL                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * PLL_SYS and PLL_USB register offsets (datasheet §5.4).
 *
 * Offsets are relative to the respective PLL base address
 * (RP2350_PLL_SYS_BASE or RP2350_PLL_USB_BASE).  The lock sequence:
 * write CS.REFDIV and FBDIV_INT, clear PD and VCOPD in PWR, poll CS.LOCK,
 * write the PRIM post-dividers, then clear POSTDIVPD.
 */
#define RP2350_PLL_CS               (0x00U)
#define RP2350_PLL_PWR              (0x04U)
#define RP2350_PLL_FBDIV_INT        (0x08U)
#define RP2350_PLL_PRIM             (0x0CU)

#define RP2350_PLL_CS_LOCK          (1U << 31)
#define RP2350_PLL_CS_REFDIV_MASK   (0x3FU)
#define RP2350_PLL_PWR_PD           (1U <<  0)  /* main pwr down */
#define RP2350_PLL_PWR_DSMPD        (1U <<  2)  /* DSM pwr down */
#define RP2350_PLL_PWR_POSTDIVPD    (1U <<  3)  /* post divider pwr down */
#define RP2350_PLL_PWR_VCOPD        (1U <<  5)  /* VCO pwr down */
#define RP2350_PLL_PRIM_POSTDIV1_S  16
#define RP2350_PLL_PRIM_POSTDIV2_S  12

/*---------------------------------------------------------------------------*/
/* IO_BANK0                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * IO_BANK0 — GPIO function select and interrupt config (datasheet §9).
 *
 * Each of the 48 GPIO pins has an 8-byte block (STATUS + CTRL).
 * GPIOn_CTRL.FUNCSEL (low 5 bits) routes the pin to a peripheral;
 * RP2350_IO_FUNC_* constants give the valid values.  The interrupt
 * arrays (INTR / PROC0_INTE / PROC0_INTF / PROC0_INTS) each cover
 * 8 pins per 32-bit word with 4 bits per pin (level-low / level-high /
 * edge-low / edge-high), so each array is six words for GPIO 0-47.
 */
/* Per-pin block for pin n: STATUS at n*8, CTRL at n*8 + 4. */
#define RP2350_IO_BANK0_GPIO_CTRL(n) (RP2350_IO_BANK0_BASE + 0x004U + ((n) * 8U))

/* Function-select values written into GPIOn_CTRL.FUNCSEL (low 5 bits) */
#define RP2350_IO_FUNC_HSTX         0
#define RP2350_IO_FUNC_SPI          1
#define RP2350_IO_FUNC_UART         2
#define RP2350_IO_FUNC_I2C          3
#define RP2350_IO_FUNC_PWM          4
#define RP2350_IO_FUNC_SIO          5  /* GPIO via SIO */
#define RP2350_IO_FUNC_PIO0         6
#define RP2350_IO_FUNC_PIO1         7
#define RP2350_IO_FUNC_PIO2         8
#define RP2350_IO_FUNC_GPCK         9  /* clock gpin */
#define RP2350_IO_FUNC_USB          10
#define RP2350_IO_FUNC_UART_AUX     11
#define RP2350_IO_FUNC_NULL         31

/* PROC0 IRQ enable / status arrays. Each register covers 8 pins
 * (4 bits per pin: LEVEL_LOW / LEVEL_HIGH / EDGE_LOW / EDGE_HIGH).  Each
 * array is six words and the next follows it (pico-sdk rp2350
 * regs/io_bank0.h: IO_BANK0_PROC0_INTE0_OFFSET = 0x248). */
#define RP2350_IO_BANK0_INTR(i)         (RP2350_IO_BANK0_BASE + 0x230U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTE(i)   (RP2350_IO_BANK0_BASE + 0x248U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTF(i)   (RP2350_IO_BANK0_BASE + 0x260U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTS(i)   (RP2350_IO_BANK0_BASE + 0x278U + ((i) * 4U))

#define RP2350_IO_INT_LEVEL_LOW     0x1U
#define RP2350_IO_INT_LEVEL_HIGH    0x2U
#define RP2350_IO_INT_EDGE_LOW      0x4U
#define RP2350_IO_INT_EDGE_HIGH     0x8U

/*---------------------------------------------------------------------------*/
/* PADS_BANK0                                                                */
/*---------------------------------------------------------------------------*/

/*
 * PADS_BANK0 — pad electrical properties (datasheet §9.3).
 *
 * Each GPIO pad has one 32-bit register controlling output-disable (OD),
 * input-enable (IE), drive strength (2/4/8/12 mA), pull-up (PUE),
 * pull-down (PDE), Schmitt trigger (SCHMITT), and slew rate (SLEWFAST).
 * The ISO bit must be cleared after RESETS to enable I/O on the pad.
 */
#define RP2350_PADS_BANK0_VOLTAGE   (RP2350_PADS_BANK0_BASE + 0x00U)
#define RP2350_PADS_BANK0_GPIO(n)   (RP2350_PADS_BANK0_BASE + 0x04U + ((n) * 4U))

#define RP2350_PADS_OD              (1U << 7)  /* output disable */
#define RP2350_PADS_IE              (1U << 6)  /* input enable */
#define RP2350_PADS_DRIVE_2MA       (0U << 4)
#define RP2350_PADS_DRIVE_4MA       (1U << 4)
#define RP2350_PADS_DRIVE_8MA       (2U << 4)
#define RP2350_PADS_DRIVE_12MA      (3U << 4)
#define RP2350_PADS_PUE             (1U << 3)  /* pull-up enable */
#define RP2350_PADS_PDE             (1U << 2)  /* pull-down enable */
#define RP2350_PADS_SCHMITT         (1U << 1)
#define RP2350_PADS_SLEWFAST        (1U << 0)

/* Pads-bank0 ISO bit (must be cleared after RESETS to enable I/O) */
#define RP2350_PADS_ISO             (1U << 8)

/*---------------------------------------------------------------------------*/
/* SIO                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * SIO — single-cycle GPIO data registers (datasheet §3.1.5).
 *
 * SIO is the fast GPIO path: reads and writes complete in one CPU cycle.
 * The *_SET / *_CLR / *_XOR registers change single bits with no
 * read-modify-write.  SIO has no +0x2000/+0x3000 RP2350_REG_ALIAS_* aliases,
 * so _RP2350_REG_SET/_CLR must not be used on it.  GPIO 0-31 are in
 * GPIO_IN/OUT/OE; GPIO 32-47 are in the GPIO_HI_* registers.
 */
#define RP2350_SIO_GPIO_IN          (RP2350_SIO_BASE + 0x004U)
#define RP2350_SIO_GPIO_OUT         (RP2350_SIO_BASE + 0x010U)
#define RP2350_SIO_GPIO_OUT_SET     (RP2350_SIO_BASE + 0x018U)
#define RP2350_SIO_GPIO_OUT_CLR     (RP2350_SIO_BASE + 0x020U)
#define RP2350_SIO_GPIO_OUT_XOR     (RP2350_SIO_BASE + 0x028U)
#define RP2350_SIO_GPIO_OE          (RP2350_SIO_BASE + 0x030U)
#define RP2350_SIO_GPIO_OE_SET      (RP2350_SIO_BASE + 0x038U)
#define RP2350_SIO_GPIO_OE_CLR      (RP2350_SIO_BASE + 0x040U)

/* High GPIO bank (pins 32..47); no driver in this port uses it. */
#define RP2350_SIO_GPIO_HI_IN       (RP2350_SIO_BASE + 0x008U)
#define RP2350_SIO_GPIO_HI_OUT      (RP2350_SIO_BASE + 0x014U)

/*---------------------------------------------------------------------------*/
/* UART                                                                      */
/*---------------------------------------------------------------------------*/

/*
 * UART0/UART1 — PrimeCell PL011 registers (datasheet §12.2).
 *
 * Offsets are relative to RP2350_UART0_BASE or RP2350_UART1_BASE; both
 * blocks follow the ARM PrimeCell UART (PL011) TRM.  DR holds the 8-bit
 * data byte with four RX error flags in bits 8-11.  FR reflects TX/RX FIFO
 * fill state and the BUSY bit.  Baud is set via the IBRD/FBRD pair:
 * baud_divisor = clk_peri / (16 * baud).
 */
#define RP2350_UART_DR              (0x000U)  /* data */
#define RP2350_UART_RSR             (0x004U)  /* receive status / error clear */
#define RP2350_UART_FR              (0x018U)  /* flag */
#define RP2350_UART_IBRD            (0x024U)  /* integer baud-rate */
#define RP2350_UART_FBRD            (0x028U)  /* fractional baud-rate */
#define RP2350_UART_LCR_H           (0x02CU)  /* line control */
#define RP2350_UART_CR              (0x030U)  /* control */
#define RP2350_UART_IFLS            (0x034U)  /* interrupt FIFO level */
#define RP2350_UART_IMSC            (0x038U)  /* interrupt mask */
#define RP2350_UART_RIS             (0x03CU)  /* raw interrupt status */
#define RP2350_UART_MIS             (0x040U)  /* masked interrupt status */
#define RP2350_UART_ICR             (0x044U)  /* interrupt clear */
#define RP2350_UART_DMACR           (0x048U)

/* DR upper byte holds RX error flags (FE/PE/BE/OE) */
#define RP2350_UART_DR_OE           (1U << 11)
#define RP2350_UART_DR_BE           (1U << 10)
#define RP2350_UART_DR_PE           (1U <<  9)
#define RP2350_UART_DR_FE           (1U <<  8)

/* FR (flag) */
#define RP2350_UART_FR_TXFE         (1U <<  7)  /* TX FIFO empty */
#define RP2350_UART_FR_RXFF         (1U <<  6)
#define RP2350_UART_FR_TXFF         (1U <<  5)
#define RP2350_UART_FR_RXFE         (1U <<  4)
#define RP2350_UART_FR_BUSY         (1U <<  3)

/* LCR_H */
#define RP2350_UART_LCR_BRK         (1U <<  0)
#define RP2350_UART_LCR_PEN         (1U <<  1)
#define RP2350_UART_LCR_EPS         (1U <<  2)
#define RP2350_UART_LCR_STP2        (1U <<  3)
#define RP2350_UART_LCR_FEN         (1U <<  4)
#define RP2350_UART_LCR_WLEN_8      (3U <<  5)
#define RP2350_UART_LCR_SPS         (1U <<  7)

/* CR */
#define RP2350_UART_CR_UARTEN       (1U <<  0)
#define RP2350_UART_CR_LBE          (1U <<  7)  /* loopback enable */
#define RP2350_UART_CR_TXE          (1U <<  8)
#define RP2350_UART_CR_RXE          (1U <<  9)

/* IMSC / MIS / ICR bits */
#define RP2350_UART_INT_RXIM        (1U <<  4)
#define RP2350_UART_INT_TXIM        (1U <<  5)
#define RP2350_UART_INT_RTIM        (1U <<  6)
#define RP2350_UART_INT_OEIM        (1U << 10)

/*---------------------------------------------------------------------------*/
/* TIMER0                                                                    */
/*---------------------------------------------------------------------------*/

/*
 * TIMER0 — 64-bit free-running microsecond timer (datasheet §12.7).
 *
 * Clocked by the 1 us tick from the TICKS block.  TIMERAWL/TIMERAWH
 * give the raw 64-bit value without latching; TIMELR/TIMEHR use a
 * hardware latch (read L first to capture H atomically).  ALARM0..1
 * fire when the lower 32 bits of the timer match.  Writing ALARMn arms
 * it, and writing 1 to its ARMED bit disarms it.
 */
#define RP2350_TIMER0_TIMEHW        (RP2350_TIMER0_BASE + 0x00U)
#define RP2350_TIMER0_TIMELW        (RP2350_TIMER0_BASE + 0x04U)
#define RP2350_TIMER0_TIMEHR        (RP2350_TIMER0_BASE + 0x08U)
#define RP2350_TIMER0_TIMELR        (RP2350_TIMER0_BASE + 0x0CU)
#define RP2350_TIMER0_ALARM0        (RP2350_TIMER0_BASE + 0x10U)
#define RP2350_TIMER0_ALARM1        (RP2350_TIMER0_BASE + 0x14U)
#define RP2350_TIMER0_ARMED         (RP2350_TIMER0_BASE + 0x20U)
#define RP2350_TIMER0_TIMERAWH      (RP2350_TIMER0_BASE + 0x24U)
#define RP2350_TIMER0_TIMERAWL      (RP2350_TIMER0_BASE + 0x28U)
#define RP2350_TIMER0_DBGPAUSE      (RP2350_TIMER0_BASE + 0x2CU)
#define RP2350_TIMER0_PAUSE         (RP2350_TIMER0_BASE + 0x30U)
/* LOCKED (0x34) and SOURCE (0x38) put INTR/INTE/INTF/INTS 8 bytes above
 * their RP2040 offsets; an RP2040 offset addresses the wrong register
 * (datasheet §12.7.2). */
#define RP2350_TIMER0_LOCKED        (RP2350_TIMER0_BASE + 0x34U)
#define RP2350_TIMER0_SOURCE        (RP2350_TIMER0_BASE + 0x38U)
#define RP2350_TIMER0_INTR          (RP2350_TIMER0_BASE + 0x3CU)
#define RP2350_TIMER0_INTE          (RP2350_TIMER0_BASE + 0x40U)
#define RP2350_TIMER0_INTF          (RP2350_TIMER0_BASE + 0x44U)
#define RP2350_TIMER0_INTS          (RP2350_TIMER0_BASE + 0x48U)

/*---------------------------------------------------------------------------*/
/* ADC                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * ADC — 12-bit successive-approximation converter (datasheet §12.4).
 *
 * CS.AINSEL selects the channel (0..3 = AIN0..AIN3 on GP26..GP29;
 * channel 4 = on-chip temperature sensor).  Start a single conversion
 * with CS.START_ONCE; poll CS.READY before reading RESULT.  ADC pins
 * need OD=1 (output disable) and IE=0 in PADS_BANK0, so the pad's
 * digital driver and input buffer stay off the analogue input.
 */
#define RP2350_ADC_CS               (RP2350_ADC_BASE + 0x00U)
#define RP2350_ADC_RESULT           (RP2350_ADC_BASE + 0x04U)
#define RP2350_ADC_FCS              (RP2350_ADC_BASE + 0x08U)
#define RP2350_ADC_FIFO             (RP2350_ADC_BASE + 0x0CU)
#define RP2350_ADC_DIV              (RP2350_ADC_BASE + 0x10U)
#define RP2350_ADC_INTR             (RP2350_ADC_BASE + 0x14U)
#define RP2350_ADC_INTE             (RP2350_ADC_BASE + 0x18U)
#define RP2350_ADC_INTF             (RP2350_ADC_BASE + 0x1CU)
#define RP2350_ADC_INTS             (RP2350_ADC_BASE + 0x20U)

/* CS bits */
#define RP2350_ADC_CS_EN            (1U << 0)   /* ADC enable */
#define RP2350_ADC_CS_TS_EN         (1U << 1)   /* Temperature sensor enable */
#define RP2350_ADC_CS_START_ONCE    (1U << 2)   /* Trigger one conversion */
#define RP2350_ADC_CS_START_MANY    (1U << 3)   /* Free-running mode */
#define RP2350_ADC_CS_READY         (1U << 8)   /* 1 = idle (conversion done) */
#define RP2350_ADC_CS_ERR           (1U << 9)   /* Conversion error */
#define RP2350_ADC_CS_ERR_STICKY    (1U << 10)  /* Sticky error */
#define RP2350_ADC_CS_AINSEL_SHIFT  12          /* Channel select 0..7 */
#define RP2350_ADC_CS_AINSEL_MASK   (0xFU << RP2350_ADC_CS_AINSEL_SHIFT)

/* The temperature sensor is wired to AINSEL channel 4. ADC channels
 * 0..3 are AIN0..AIN3 (GPIO26..GPIO29 on the standard package). */
#define RP2350_ADC_CHANNEL_TEMP     4U

/* GPIO offset for ADC pin n: AIN0..AIN3 → GP26..GP29. */
#define RP2350_ADC_GPIO_BASE        26U

/*---------------------------------------------------------------------------*/
/* I2C                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * I2C0/I2C1 — DesignWare DW_apb_i2c registers (datasheet §12.3).
 *
 * Offsets are relative to RP2350_I2C0_BASE or RP2350_I2C1_BASE.
 * The driver uses master mode with 7-bit addresses.  IC_CON sets speed and
 * role; IC_TAR holds the target address; IC_DATA_CMD issues read/write
 * commands with optional STOP and RESTART bits.  The SCL high/low counts
 * (IC_SS/FS_SCL_*) are in clk_sys cycles, the clock of the I2C block.
 */
#define RP2350_I2C_IC_CON              0x00U
#define RP2350_I2C_IC_TAR              0x04U
#define RP2350_I2C_IC_DATA_CMD         0x10U
#define RP2350_I2C_IC_SS_SCL_HCNT      0x14U
#define RP2350_I2C_IC_SS_SCL_LCNT      0x18U
#define RP2350_I2C_IC_FS_SCL_HCNT      0x1CU
#define RP2350_I2C_IC_FS_SCL_LCNT      0x20U
#define RP2350_I2C_IC_INTR_STAT        0x2CU
#define RP2350_I2C_IC_RAW_INTR_STAT    0x34U
#define RP2350_I2C_IC_CLR_INTR         0x40U
#define RP2350_I2C_IC_CLR_TX_ABRT      0x54U
#define RP2350_I2C_IC_ENABLE           0x6CU
#define RP2350_I2C_IC_STATUS           0x70U
#define RP2350_I2C_IC_TXFLR            0x74U
#define RP2350_I2C_IC_RXFLR            0x78U
#define RP2350_I2C_IC_SDA_HOLD         0x7CU
#define RP2350_I2C_IC_TX_ABRT_SOURCE   0x80U
#define RP2350_I2C_IC_ENABLE_STATUS    0x9CU
#define RP2350_I2C_IC_FS_SPKLEN        0xA8U

/* IC_CON bits */
#define RP2350_I2C_CON_MASTER          (1U << 0)
#define RP2350_I2C_CON_SPEED_SS        (1U << 1)   /* standard mode */
#define RP2350_I2C_CON_SPEED_FS        (2U << 1)   /* fast mode */
#define RP2350_I2C_CON_SLAVE_DISABLE   (1U << 6)
#define RP2350_I2C_CON_RESTART_EN      (1U << 5)
#define RP2350_I2C_CON_TX_EMPTY_CTRL   (1U << 8)

/* IC_DATA_CMD bits */
#define RP2350_I2C_DATA_CMD_READ       (1U << 8)
#define RP2350_I2C_DATA_CMD_STOP       (1U << 9)
#define RP2350_I2C_DATA_CMD_RESTART    (1U << 10)

/* IC_STATUS bits */
#define RP2350_I2C_STATUS_ACTIVITY     (1U << 0)
#define RP2350_I2C_STATUS_TFNF         (1U << 1)   /* TX FIFO not full */
#define RP2350_I2C_STATUS_TFE          (1U << 2)   /* TX FIFO empty */
#define RP2350_I2C_STATUS_RFNE         (1U << 3)   /* RX FIFO not empty */

/* IC_RAW_INTR_STAT bits used by the driver */
#define RP2350_I2C_INTR_TX_ABRT        (1U << 6)
#define RP2350_I2C_INTR_STOP_DET       (1U << 9)

/* IC_TX_ABRT_SOURCE bits for a NACK on the address or on data. */
#define RP2350_I2C_ABRT_7B_ADDR_NOACK  (1U << 0)
#define RP2350_I2C_ABRT_TXDATA_NOACK   (1U << 3)

/*---------------------------------------------------------------------------*/
/* SPI                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * SPI0/SPI1 — PrimeCell PL022 SSP registers (datasheet §12.5).
 *
 * Offsets are relative to RP2350_SPI0_BASE or RP2350_SPI1_BASE.
 * Configure 8-bit Motorola format via SSPCR0; set baud via SSPCPSR
 * (pre-scaler, must be even 2..254) and SSPCR0.SCR (rate 0..255);
 * enable with SSPCR1.SSE.  SSPDR is both TX write and RX read.
 * SSPSR reflects TX/RX FIFO fill state and the BUSY bit.
 */
#define RP2350_SPI_SSPCR0              0x00U
#define RP2350_SPI_SSPCR1              0x04U
#define RP2350_SPI_SSPDR               0x08U
#define RP2350_SPI_SSPSR               0x0CU
#define RP2350_SPI_SSPCPSR             0x10U

/* SSPCR0 fields */
#define RP2350_SPI_CR0_DSS_8BIT        0x07U       /* bits[3:0] */
#define RP2350_SPI_CR0_FRF_MOTOROLA    (0U << 4)
#define RP2350_SPI_CR0_SPO             (1U << 6)   /* CPOL */
#define RP2350_SPI_CR0_SPH             (1U << 7)   /* CPHA */
#define RP2350_SPI_CR0_SCR_SHIFT       8           /* clock rate, [15:8] */

/* SSPCR1 fields */
#define RP2350_SPI_CR1_SSE             (1U << 1)   /* enable */
#define RP2350_SPI_CR1_MS              (1U << 2)   /* 0 = master */

/* SSPSR (status) */
#define RP2350_SPI_SR_TFE              (1U << 0)   /* TX FIFO empty */
#define RP2350_SPI_SR_TNF              (1U << 1)   /* TX FIFO not full */
#define RP2350_SPI_SR_RNE              (1U << 2)   /* RX FIFO not empty */
#define RP2350_SPI_SR_RFF              (1U << 3)   /* RX FIFO full */
#define RP2350_SPI_SR_BSY              (1U << 4)   /* busy */

/*---------------------------------------------------------------------------*/
/* WATCHDOG                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * Watchdog timer registers (datasheet §13).
 *
 * CTRL.ENABLE arms the watchdog; CTRL.TRIGGER forces an immediate reset.
 * LOAD sets the down-count reload value, in 1 us ticks from the TICKS
 * block.  REASON distinguishes watchdog resets from other reset causes.
 * SCRATCH0 survives a watchdog reset.  The watchdog's tick generator is
 * in the TICKS block; RP2350_WD_TICK names its CTRL register.
 */
#define RP2350_WD_CTRL              (RP2350_WATCHDOG_BASE + 0x00U)
#define RP2350_WD_LOAD              (RP2350_WATCHDOG_BASE + 0x04U)
#define RP2350_WD_REASON            (RP2350_WATCHDOG_BASE + 0x08U)
#define RP2350_WD_SCRATCH0          (RP2350_WATCHDOG_BASE + 0x0CU)
/* watchdog tick on the TICKS block */
#define RP2350_WD_TICK              (RP2350_TICKS_BASE   + 0x30U)

#define RP2350_WD_CTRL_TIME_MASK    (0xFFFFFFU)
#define RP2350_WD_CTRL_PAUSE_DBG1   (1U << 26)
#define RP2350_WD_CTRL_PAUSE_DBG0   (1U << 25)
#define RP2350_WD_CTRL_PAUSE_JTAG   (1U << 24)
#define RP2350_WD_CTRL_ENABLE       (1U << 30)
#define RP2350_WD_CTRL_TRIGGER      (1U << 31)
#define RP2350_WD_REASON_TIMER      (1U << 0)   /* the count ran out       */
#define RP2350_WD_REASON_FORCE      (1U << 1)   /* CTRL.TRIGGER was set    */

/*---------------------------------------------------------------------------*/
/* TICKS BLOCK                                                               */
/*---------------------------------------------------------------------------*/

/*
 * TICKS block — per-subsystem 1 us tick generators (datasheet §10.6).
 *
 * The TICKS block has six tick generators: PROC0, PROC1, TIMER0, TIMER1,
 * WATCHDOG and RISCV.  Each divides clk_ref: CYCLES is the clk_ref cycles
 * per tick (12 for a 1 us tick from the 12 MHz XOSC), then CTRL.ENABLE
 * starts it.  TIMER0's tick drives the 64-bit 1 us free-running counter;
 * the WATCHDOG generator drives the watchdog down-counter.
 */
#define RP2350_TICKS_PROC0_CTRL     (RP2350_TICKS_BASE + 0x00U)
#define RP2350_TICKS_PROC0_CYCLES   (RP2350_TICKS_BASE + 0x04U)
#define RP2350_TICKS_PROC0_COUNT    (RP2350_TICKS_BASE + 0x08U)
#define RP2350_TICKS_PROC1_CTRL     (RP2350_TICKS_BASE + 0x0CU)
#define RP2350_TICKS_PROC1_CYCLES   (RP2350_TICKS_BASE + 0x10U)
#define RP2350_TICKS_TIMER0_CTRL    (RP2350_TICKS_BASE + 0x18U)
#define RP2350_TICKS_TIMER0_CYCLES  (RP2350_TICKS_BASE + 0x1CU)
#define RP2350_TICKS_TIMER1_CTRL    (RP2350_TICKS_BASE + 0x24U)
#define RP2350_TICKS_TIMER1_CYCLES  (RP2350_TICKS_BASE + 0x28U)
#define RP2350_TICKS_WATCHDOG_CTRL  (RP2350_TICKS_BASE + 0x30U)
#define RP2350_TICKS_WATCHDOG_CYCLES (RP2350_TICKS_BASE + 0x34U)

#define RP2350_TICK_ENABLE          (1U << 0)
#define RP2350_TICK_RUNNING         (1U << 1)

/*---------------------------------------------------------------------------*/
/* SYSTICK                                                                   */
/*---------------------------------------------------------------------------*/

/*
 * Cortex-M33 SysTick registers (ARMv8-M TRM §B3.3).
 *
 * CSR controls the timer (ENABLE, TICKINT to fire SysTick exception,
 * CLKSRC selects processor vs. reference clock).  RVR is the 24-bit
 * reload value; CVR is the current down-count (write-any to clear).
 * The kernel tick sets CLKSRC = 1 (processor clock) and reloads for
 * clk_sys / TIKU_CLOCK_ARCH_SECOND cycles.
 */
#define RP2350_SYST_CSR             (RP2350_SYST_BASE + 0x00U)
#define RP2350_SYST_RVR             (RP2350_SYST_BASE + 0x04U)
#define RP2350_SYST_CVR             (RP2350_SYST_BASE + 0x08U)
#define RP2350_SYST_CALIB           (RP2350_SYST_BASE + 0x0CU)

#define RP2350_SYST_CSR_ENABLE      (1U << 0)
#define RP2350_SYST_CSR_TICKINT     (1U << 1)
#define RP2350_SYST_CSR_CLKSRC_CPU  (1U << 2)  /* 1 = processor clock */
#define RP2350_SYST_CSR_COUNTFLAG   (1U << 16)

/*---------------------------------------------------------------------------*/
/* NVIC                                                                      */
/*---------------------------------------------------------------------------*/

/*
 * Cortex-M33 NVIC registers (ARMv8-M TRM §B3.4).
 *
 * ISER0/ICER0 enable/disable IRQs by setting a bit at position irq%32
 * in the word at base + (irq/32)*4, so IRQs 32 and up are in ISER1/ICER1.
 * ISPR/ICPR set and clear pending state.  IPR0 holds four 8-bit priority
 * bytes per word.  The rp2350_nvic_* helpers below compute the word.
 */
#define RP2350_NVIC_ISER0           (RP2350_NVIC_BASE + 0x000U)
#define RP2350_NVIC_ICER0           (RP2350_NVIC_BASE + 0x080U)
#define RP2350_NVIC_ISPR0           (RP2350_NVIC_BASE + 0x100U)
#define RP2350_NVIC_ICPR0           (RP2350_NVIC_BASE + 0x180U)
#define RP2350_NVIC_IPR0            (RP2350_NVIC_BASE + 0x300U)

/*---------------------------------------------------------------------------*/
/* SCB                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * SCB — System Control Block registers (ARMv8-M TRM §B3.2).
 *
 * AIRCR.SYSRESET (with VECTKEY = 0x05FA) triggers a warm system reset.
 * SHCSR.MEMFAULTENA enables the MemManage fault handler; without it,
 * all MemManage faults escalate to HardFault.  CFSR.MMFSR (low byte)
 * reports the fault type; MMFAR holds the faulting address when
 * MMARVALID is set.
 */
/* AIRCR: application interrupt and reset control; SHCSR: system handler
 * control and state; CFSR: configurable fault status; HFSR: HardFault
 * status; MMFAR: MemManage fault address. */
#define RP2350_SCB_AIRCR            (RP2350_SCB_BASE + 0x0CU)
#define RP2350_SCB_SHCSR            (RP2350_SCB_BASE + 0x24U)
#define RP2350_SCB_CFSR             (RP2350_SCB_BASE + 0x28U)
#define RP2350_SCB_HFSR             (RP2350_SCB_BASE + 0x2CU)
#define RP2350_SCB_MMFAR            (RP2350_SCB_BASE + 0x34U)

#define RP2350_SCB_AIRCR_VECTKEY    (0x05FAUL << 16)
#define RP2350_SCB_AIRCR_SYSRESET   (1U << 2)
#define RP2350_SCB_SHCSR_MEMFAULTENA (1U << 16)

/* CFSR.MMFSR (low byte) — write 1 to clear */
#define RP2350_SCB_MMFSR_IACCVIOL   (1U << 0)
#define RP2350_SCB_MMFSR_DACCVIOL   (1U << 1)
#define RP2350_SCB_MMFSR_MUNSTKERR  (1U << 3)
#define RP2350_SCB_MMFSR_MSTKERR    (1U << 4)
#define RP2350_SCB_MMFSR_MMARVALID  (1U << 7)

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * ARMv8-M MPU registers (ARMv8-M TRM §B3.5).
 *
 * The Cortex-M33 MPU supports up to 8 regions (TYPE.DREGION = 8 on
 * RP2350).  Each region is configured by writing RNR (select), then
 * RBAR (base + attributes) and RLAR (limit + enable).  AP bits in
 * RBAR control read/write access; XN (bit 0) marks a region
 * as Execute-Never.  MAIR0/MAIR1 hold memory type attributes (one byte
 * per AttrIndx 0..7); MAIR_NORMAL_NC (0x44) is Normal, Non-cacheable.
 */
/* TYPE: read the DREGION count */
#define RP2350_MPU_TYPE             (RP2350_MPU_BASE + 0x00U)
#define RP2350_MPU_CTRL             (RP2350_MPU_BASE + 0x04U)
#define RP2350_MPU_RNR              (RP2350_MPU_BASE + 0x08U)
#define RP2350_MPU_RBAR             (RP2350_MPU_BASE + 0x0CU)
#define RP2350_MPU_RLAR             (RP2350_MPU_BASE + 0x10U)
#define RP2350_MPU_MAIR0            (RP2350_MPU_BASE + 0x30U)
#define RP2350_MPU_MAIR1            (RP2350_MPU_BASE + 0x34U)

/* MPU_CTRL */
#define RP2350_MPU_CTRL_ENABLE      (1U << 0)
#define RP2350_MPU_CTRL_HFNMIENA    (1U << 1)
#define RP2350_MPU_CTRL_PRIVDEFENA  (1U << 2)

/* RBAR fields (ARMv8-M):
 *   bits[31:5]  BASE      (32-byte aligned)
 *   bits[ 4:3]  SH        (shareability; 00 = Non-shareable)
 *   bits[ 2:1]  AP        (00 RW priv-only, 01 RW any,
 *                          10 RO priv-only, 11 RO any)
 *   bit      0  XN        (1 = Execute Never)
 *
 * RLAR fields:
 *   bits[31:5]  LIMIT     (32-byte aligned, inclusive)
 *   bits[ 3:1]  AttrIndx  (index into MPU_MAIR{0,1})
 *   bit      0  EN        (region enable) */
#define RP2350_MPU_RBAR_AP_RW_ANY   (0x1U << 1)
#define RP2350_MPU_RBAR_AP_RO_ANY   (0x3U << 1)
#define RP2350_MPU_RBAR_XN          (1U << 0)
#define RP2350_MPU_RBAR_AP_MASK     (0x3U << 1)
#define RP2350_MPU_RLAR_EN          (1U << 0)

/* MPU_MAIR memory attribute encodings (one byte per attr index 0..3 in MAIR0,
 * 4..7 in MAIR1).  0x44 = Normal, Inner and Outer Non-cacheable. */
#define RP2350_MPU_MAIR_NORMAL_NC   0x44U

/*---------------------------------------------------------------------------*/
/* PWM                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * PWM block — 12 slices, 2 channels each (datasheet §12.7).
 *
 * Per-slice registers stride 0x14 bytes from PWM_BASE.  GPIO 0-31 drive
 * slices 0-7, slice (n/2) % 8, so GPIO 16-31 repeat the slices of GPIO 0-15;
 * GPIO 32-47 drive slices 8-11, slice 8 + (n/2) % 4.  Channel is n%2
 * (A=even, B=odd); channel A occupies the low 16 bits of CC, channel B the
 * high 16.  TOP sets the wrap value (65535 for 16-bit duty resolution);
 * DIV.INT/FRAC divides clk_sys to reach the desired wrap frequency.
 */

#define RP2350_PWM_NUM_SLICES        12U
#define RP2350_PWM_SLICE(s)          (RP2350_PWM_BASE + 0x14U * (s))
#define RP2350_PWM_SLICE_CSR(s)      (RP2350_PWM_SLICE(s) + 0x00U)
#define RP2350_PWM_SLICE_DIV(s)      (RP2350_PWM_SLICE(s) + 0x04U)
#define RP2350_PWM_SLICE_CTR(s)      (RP2350_PWM_SLICE(s) + 0x08U)
#define RP2350_PWM_SLICE_CC(s)       (RP2350_PWM_SLICE(s) + 0x0CU)
#define RP2350_PWM_SLICE_TOP(s)      (RP2350_PWM_SLICE(s) + 0x10U)

#define RP2350_PWM_EN       (RP2350_PWM_BASE + 0xF0U)   /* global enable */
#define RP2350_PWM_INTR     (RP2350_PWM_BASE + 0xF4U)   /* raw wrap IRQs */
#define RP2350_PWM_INTE     (RP2350_PWM_BASE + 0xF8U)   /* enable */
#define RP2350_PWM_INTF     (RP2350_PWM_BASE + 0xFCU)   /* force */
#define RP2350_PWM_INTS     (RP2350_PWM_BASE + 0x100U)  /* status */

/* CSR bit fields */
#define RP2350_PWM_CSR_EN            (1U << 0)
#define RP2350_PWM_CSR_PH_CORRECT    (1U << 1)
#define RP2350_PWM_CSR_A_INV         (1U << 2)
#define RP2350_PWM_CSR_B_INV         (1U << 3)
#define RP2350_PWM_CSR_DIVMODE_FREE  (0U << 4)   /* free-running (default) */
#define RP2350_PWM_CSR_PH_ADV        (1U << 6)
#define RP2350_PWM_CSR_PH_RET        (1U << 7)

/**
 * @brief Map a GPIO pin number to the PWM slice that drives it.
 *
 * @param gpio  GPIO pin (0..47).
 * @return Slice index (0..11).
 */
static inline uint8_t rp2350_pwm_pin_to_slice(uint8_t gpio) {
    return (uint8_t)(gpio < 32U ? (gpio / 2U) % 8U
                                : 8U + (gpio / 2U) % 4U);
}

/**
 * @brief Map a GPIO pin number to its PWM channel within the slice.
 *
 * @param gpio  GPIO pin (0..47).
 * @return 0 for channel A (even pins), 1 for channel B (odd pins).
 */
static inline uint8_t rp2350_pwm_pin_to_channel(uint8_t gpio) {
    return (uint8_t)(gpio & 1U);
}

/*---------------------------------------------------------------------------*/
/* DMA                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * DMA controller — 16 channels (datasheet §12.6).
 *
 * Each channel has READ_ADDR, WRITE_ADDR, TRANS_COUNT, and CTRL_TRIG
 * at a 0x40-byte stride from DMA_BASE.  Writing CTRL_TRIG starts the
 * transfer as a side-effect; CTRL is the same field without the trigger.
 */

#define RP2350_DMA_NUM_CHANNELS      16U
#define RP2350_DMA_CHAN(c)           (RP2350_DMA_BASE + 0x40U * (c))
#define RP2350_DMA_CHAN_READ_ADDR(c)    (RP2350_DMA_CHAN(c) + 0x00U)
#define RP2350_DMA_CHAN_WRITE_ADDR(c)   (RP2350_DMA_CHAN(c) + 0x04U)
#define RP2350_DMA_CHAN_TRANS_COUNT(c)  (RP2350_DMA_CHAN(c) + 0x08U)
#define RP2350_DMA_CHAN_CTRL_TRIG(c)    (RP2350_DMA_CHAN(c) + 0x0CU)

#define RP2350_DMA_INTR    (RP2350_DMA_BASE + 0x400U)  /* raw */
#define RP2350_DMA_CHAN_ABORT (RP2350_DMA_BASE + 0x464U)
#define RP2350_DMA_INTE0   (RP2350_DMA_BASE + 0x404U)  /* enable for IRQ0 */
#define RP2350_DMA_INTF0   (RP2350_DMA_BASE + 0x408U)
#define RP2350_DMA_INTS0   (RP2350_DMA_BASE + 0x40CU)  /* status after enable */

/* CTRL_TRIG fields (datasheet §12.6.7.4).  RP2350 adds INCR_READ_REV
 * (bit 5) and INCR_WRITE_REV (bit 7) to the RP2040 layout, moving every
 * higher field by one or two bits, so RP2040 bit positions do not apply. */
#define RP2350_DMA_CTRL_EN              (1U << 0)
#define RP2350_DMA_CTRL_HIGH_PRIO       (1U << 1)
#define RP2350_DMA_CTRL_DATA_SIZE_BYTE  (0U << 2)
#define RP2350_DMA_CTRL_DATA_SIZE_HALF  (1U << 2)
#define RP2350_DMA_CTRL_DATA_SIZE_WORD  (2U << 2)
#define RP2350_DMA_CTRL_INCR_READ       (1U << 4)
#define RP2350_DMA_CTRL_INCR_READ_REV   (1U << 5)
#define RP2350_DMA_CTRL_INCR_WRITE      (1U << 6)
#define RP2350_DMA_CTRL_INCR_WRITE_REV  (1U << 7)
#define RP2350_DMA_CTRL_RING_SHIFT       8           /* RING_SIZE [11:8] */
#define RP2350_DMA_CTRL_RING_SEL_WR     (1U << 12)
#define RP2350_DMA_CTRL_CHAIN_TO_SHIFT  13           /* CHAIN_TO [16:13] */
#define RP2350_DMA_CTRL_TREQ_SEL_SHIFT  17           /* TREQ_SEL [22:17] */
#define RP2350_DMA_CTRL_TREQ_PERMANENT  0x3FU        /* unpaced (m2m) */
#define RP2350_DMA_CTRL_IRQ_QUIET       (1U << 23)
#define RP2350_DMA_CTRL_BSWAP           (1U << 24)
#define RP2350_DMA_CTRL_SNIFF_EN        (1U << 25)
#define RP2350_DMA_CTRL_WRITE_ERROR     (1U << 29)
#define RP2350_DMA_CTRL_READ_ERROR      (1U << 30)
#define RP2350_DMA_CTRL_BUSY            (1U << 26)   /* RO */
#define RP2350_DMA_CTRL_AHB_ERR         (1U << 31)   /* RO */

/*---------------------------------------------------------------------------*/
/* PIO                                                                       */
/*---------------------------------------------------------------------------*/

/*
 * PIO blocks PIO0/PIO1/PIO2 — registers and bit fields (datasheet §11).
 *
 * Three identical PIO blocks; each has four state machines (SM0..SM3)
 * sharing 32 instruction-memory slots and 4-deep TX/RX FIFOs.  All
 * offsets below are relative to the block base (RP2350_PIO0/1/2_BASE).
 */

#define RP2350_PIO_CTRL              0x000U   /* SM enable, restart */
#define RP2350_PIO_FSTAT             0x004U   /* FIFO status         */
#define RP2350_PIO_FDEBUG            0x008U   /* FIFO underflow / oflow */
#define RP2350_PIO_FLEVEL            0x00CU   /* FIFO fill levels    */
#define RP2350_PIO_TXF(sm)           (0x010U + 4U * (sm))   /* TX FIFO */
#define RP2350_PIO_RXF(sm)           (0x020U + 4U * (sm))   /* RX FIFO */
#define RP2350_PIO_IRQ               0x030U   /* PIO IRQ status (W1C) */
#define RP2350_PIO_INPUT_SYNC_BYPASS 0x038U   /* bypass input sync per GPIO */
#define RP2350_PIO_INSTR_MEM(i)      (0x048U + 4U * (i))    /* program slot */
#define RP2350_PIO_SM_CLKDIV(sm)     (0x0C8U + 0x18U * (sm))
#define RP2350_PIO_SM_EXECCTRL(sm)   (0x0CCU + 0x18U * (sm))
#define RP2350_PIO_SM_SHIFTCTRL(sm)  (0x0D0U + 0x18U * (sm))
#define RP2350_PIO_SM_ADDR(sm)       (0x0D4U + 0x18U * (sm))   /* RO */
#define RP2350_PIO_SM_INSTR(sm)      (0x0D8U + 0x18U * (sm))   /* write: exec */
#define RP2350_PIO_GPIOBASE          0x168U
#define RP2350_PIO_SM_PINCTRL(sm)    (0x0DCU + 0x18U * (sm))

/* PIO interrupt subsystem (two outputs per block, IRQ0 and IRQ1).
 *
 * RP2350 places RXF0_PUTGET0..RXF3_PUTGET3 and GPIOBASE (0x128-0x168)
 * after SM3_PINCTRL (0x124), so INTR and the IRQ0/IRQ1 registers sit 0x44
 * bytes above their RP2040 offsets (pico-sdk rp2350 regs/pio.h:
 * PIO_IRQ0_INTE_OFFSET = 0x170).  An RP2040 offset enables no interrupt. */
#define RP2350_PIO_INTR              0x16CU   /* raw IRQ source bits  */
#define RP2350_PIO_IRQ0_INTE         0x170U   /* enable               */
#define RP2350_PIO_IRQ0_INTF         0x174U   /* force                */
#define RP2350_PIO_IRQ0_INTS         0x178U   /* status (after enable) */
#define RP2350_PIO_IRQ1_INTE         0x17CU
#define RP2350_PIO_IRQ1_INTF         0x180U
#define RP2350_PIO_IRQ1_INTS         0x184U

/* CTRL register fields */
#define RP2350_PIO_CTRL_SM_ENABLE(sm)   (1U << (sm))
#define RP2350_PIO_CTRL_SM_RESTART(sm)  (1U << (4U + (sm)))
#define RP2350_PIO_CTRL_CLKDIV_RESTART(sm)  (1U << (8U + (sm)))

/* SHIFTCTRL register fields */
#define RP2350_PIO_SHIFTCTRL_AUTOPULL    (1U << 17)
#define RP2350_PIO_SHIFTCTRL_AUTOPUSH    (1U << 16)
#define RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_RIGHT  (1U << 19)  /* 1=LSB */
#define RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_LEFT   (0U)        /* 0=MSB */
#define RP2350_PIO_SHIFTCTRL_PULL_THRESH_SHIFT   25
#define RP2350_PIO_SHIFTCTRL_FJOIN_TX            (1U << 30)  /* 8-deep TX */
#define RP2350_PIO_SHIFTCTRL_FJOIN_RX            (1U << 31)

/* PINCTRL register fields */
#define RP2350_PIO_PINCTRL_OUT_BASE_SHIFT     0
#define RP2350_PIO_PINCTRL_SET_BASE_SHIFT     5
#define RP2350_PIO_PINCTRL_SIDESET_BASE_SHIFT 10
#define RP2350_PIO_PINCTRL_IN_BASE_SHIFT      15
#define RP2350_PIO_PINCTRL_OUT_COUNT_SHIFT    20
#define RP2350_PIO_PINCTRL_SET_COUNT_SHIFT    26
#define RP2350_PIO_PINCTRL_SIDESET_COUNT_SHIFT 29

/* IRQ0_INTE / INTS bit positions: PIO IRQ flags 0..7 at [15..8],
 * per-SM TXNFULL at [7..4] and RXNEMPTY at [3..0]. */
#define RP2350_PIO_INT_SM0_IRQ       (1U << 8)
#define RP2350_PIO_INT_SM1_IRQ       (1U << 9)
#define RP2350_PIO_INT_SM2_IRQ       (1U << 10)
#define RP2350_PIO_INT_SM3_IRQ       (1U << 11)

/*
 * NVIC IRQ numbers (datasheet §3.6.1), a subset of the RP2350 interrupt
 * table; the numbers follow pico-sdk's hardware/regs/intctrl.h.
 */
#define RP2350_IRQ_TIMER0_0         0
#define RP2350_IRQ_TIMER0_1         1
#define RP2350_IRQ_DMA_IRQ_0        10
#define RP2350_IRQ_DMA_IRQ_1        11
#define RP2350_IRQ_DMA_IRQ_2        12
#define RP2350_IRQ_DMA_IRQ_3        13
#define RP2350_IRQ_PWM_IRQ_WRAP_0   8
#define RP2350_IRQ_PIO0_0           15
#define RP2350_IRQ_PIO0_1           16
#define RP2350_IRQ_PIO1_0           17
#define RP2350_IRQ_PIO1_1           18
#define RP2350_IRQ_PIO2_0           19
#define RP2350_IRQ_PIO2_1           20
#define RP2350_IRQ_IO_BANK0         21
#define RP2350_IRQ_UART0            33
#define RP2350_IRQ_UART1            34
#define RP2350_IRQ_ADC_FIFO         35
#define RP2350_IRQ_I2C0             36
#define RP2350_IRQ_I2C1             37

/**
 * @brief Enable an IRQ in the Cortex-M33 NVIC.
 *
 * @param irq  IRQ number (RP2350_IRQ_*).
 */
static inline void rp2350_nvic_enable(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ISER0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

/**
 * @brief Disable an IRQ in the Cortex-M33 NVIC.
 *
 * @param irq  IRQ number (RP2350_IRQ_*).
 */
static inline void rp2350_nvic_disable(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ICER0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

/**
 * @brief Clear a pending IRQ in the Cortex-M33 NVIC.
 *
 * @param irq  IRQ number (RP2350_IRQ_*).
 */
static inline void rp2350_nvic_clear_pending(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ICPR0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

/*---------------------------------------------------------------------------*/
/* TRNG                                                                      */
/*---------------------------------------------------------------------------*/

/*
 * TRNG, a derivative of the ARM CryptoCell-312 TRNG (datasheet §12.13).
 * Register offsets match pico-sdk's hardware/regs/trng.h.
 *
 * Sequence to fill EHR_DATA0..5 (192 random bits):
 *
 *   1. Unreset TRNG  (RESETS bit 25)
 *   2. RND_SOURCE_ENABLE = 0       (stop the source)
 *   3. ICR = 0x3F                  (clear the source-interrupt bits)
 *   4. TRNG_CONFIG = 0             (fastest of the four ring-oscillator
 *                                   chains)
 *   5. SAMPLE_CNT1 = sample count  (rng_clk cycles between samples)
 *   6. RND_SOURCE_ENABLE = 1       (start)
 *   7. spin while (TRNG_VALID & 1) == 0
 *   8. read EHR_DATA0..5           (six 32-bit words, side-effect: clears
 *                                   the VALID flag)
 *   9. RND_SOURCE_ENABLE = 0       (idle)
 *   Repeat from step 5 for more bits.
 */
#define RP2350_TRNG_RNG_IMR            (RP2350_TRNG_BASE + 0x100U)
#define RP2350_TRNG_TRNG_ISR           (RP2350_TRNG_BASE + 0x104U)
#define RP2350_TRNG_TRNG_ICR           (RP2350_TRNG_BASE + 0x108U)
#define RP2350_TRNG_CONFIG             (RP2350_TRNG_BASE + 0x10CU)
#define RP2350_TRNG_VALID              (RP2350_TRNG_BASE + 0x110U)
#define RP2350_TRNG_EHR_DATA0          (RP2350_TRNG_BASE + 0x114U)
#define RP2350_TRNG_EHR_DATA1          (RP2350_TRNG_BASE + 0x118U)
#define RP2350_TRNG_EHR_DATA2          (RP2350_TRNG_BASE + 0x11CU)
#define RP2350_TRNG_EHR_DATA3          (RP2350_TRNG_BASE + 0x120U)
#define RP2350_TRNG_EHR_DATA4          (RP2350_TRNG_BASE + 0x124U)
#define RP2350_TRNG_EHR_DATA5          (RP2350_TRNG_BASE + 0x128U)
#define RP2350_TRNG_RND_SOURCE_ENABLE  (RP2350_TRNG_BASE + 0x12CU)
#define RP2350_TRNG_SAMPLE_CNT1        (RP2350_TRNG_BASE + 0x130U)
#define RP2350_TRNG_AUTOCORR_STATISTIC (RP2350_TRNG_BASE + 0x134U)
#define RP2350_TRNG_DEBUG_CONTROL      (RP2350_TRNG_BASE + 0x138U)
#define RP2350_TRNG_SW_RESET           (RP2350_TRNG_BASE + 0x140U)
#define RP2350_TRNG_BUSY               (RP2350_TRNG_BASE + 0x1B8U)
#define RP2350_TRNG_RST_BITS_COUNTER   (RP2350_TRNG_BASE + 0x1BCU)

#define RP2350_TRNG_VALID_EHR_BIT      (1U << 0)

/*---------------------------------------------------------------------------*/
/* RESETS HELPERS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Release a peripheral from reset and wait until it is ready.
 *
 * Clears the reset bits through the CLR alias, then spins on
 * RESETS_RESET_DONE until every peripheral in @p mask is out of reset.
 *
 * @param mask  Bitmask of RP2350_RESETS_* bits for the target peripheral(s).
 */
static inline void rp2350_unreset(uint32_t mask) {
    _RP2350_REG_CLR(RP2350_RESETS_RESET, mask);
    while ((_RP2350_REG(RP2350_RESETS_RESET_DONE) & mask) != mask) {
        /* spin */
    }
}

/**
 * @brief Assert reset on a peripheral (hold it in reset).
 *
 * @param mask  Bitmask of RP2350_RESETS_* bits for the target peripheral(s).
 */
static inline void rp2350_reset(uint32_t mask) {
    _RP2350_REG_SET(RP2350_RESETS_RESET, mask);
}

#endif /* TIKU_RP2350_REGS_H_ */
