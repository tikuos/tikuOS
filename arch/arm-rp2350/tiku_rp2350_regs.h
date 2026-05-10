/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rp2350_regs.h - Hand-written RP2350 register definitions
 *
 * Just enough peripheral register addresses, bitfield masks, and
 * helper macros to bring the kernel up bare-metal — we do not pull
 * in the Pico SDK or ARM CMSIS headers (matches the TikuOS philosophy
 * of "no abstractions; hit registers directly").
 *
 * Source: RP2350 datasheet (Raspberry Pi, 2024) §2 (memory map),
 * §3 (cores), §5 (clocks), §6 (resets), §9 (GPIO/IO_BANK0), §12
 * (UART/PL011), §10 (timer), §13 (watchdog).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_REGS_H_
#define TIKU_RP2350_REGS_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/* Atomic register access aliases. Each peripheral block on the
 * RP2350 has four aliases at +0x0000 / +0x1000 / +0x2000 / +0x3000:
 *   normal_rw     = *(reg)
 *   xor_atomic    = *(reg+0x1000)  -> XOR-write to reg
 *   set_atomic    = *(reg+0x2000)  -> set bits in reg
 *   clear_atomic  = *(reg+0x3000)  -> clear bits in reg
 * We use these in driver code to get RMW-free bit set/clear without
 * disabling interrupts. */
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
#define RP2350_TICKS_BASE           0x40108000UL

/* Cortex-M33 SCS / NVIC / SysTick / SCB live at the standard
 * private peripheral bus addresses. */
#define RP2350_PPB_BASE             0xE0000000UL
#define RP2350_SYST_BASE            (RP2350_PPB_BASE + 0xE010UL)
#define RP2350_NVIC_BASE            (RP2350_PPB_BASE + 0xE100UL)
#define RP2350_SCB_BASE             (RP2350_PPB_BASE + 0xED00UL)

/*---------------------------------------------------------------------------*/
/* RESETS BLOCK                                                              */
/*---------------------------------------------------------------------------*/

#define RP2350_RESETS_RESET         (RP2350_RESETS_BASE + 0x00U)
#define RP2350_RESETS_WDSEL         (RP2350_RESETS_BASE + 0x04U)
#define RP2350_RESETS_RESET_DONE    (RP2350_RESETS_BASE + 0x08U)

/*
 * RESETS_RESET bit positions, copied verbatim from
 * pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/resets.h
 * (LSB indices 0..28).
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

#define RP2350_CLK_GPOUT0_CTRL      (RP2350_CLOCKS_BASE + 0x00U)
#define RP2350_CLK_REF_CTRL         (RP2350_CLOCKS_BASE + 0x30U)
#define RP2350_CLK_REF_DIV          (RP2350_CLOCKS_BASE + 0x34U)
#define RP2350_CLK_REF_SELECTED     (RP2350_CLOCKS_BASE + 0x38U)
#define RP2350_CLK_SYS_CTRL         (RP2350_CLOCKS_BASE + 0x3CU)
#define RP2350_CLK_SYS_DIV          (RP2350_CLOCKS_BASE + 0x40U)
#define RP2350_CLK_SYS_SELECTED     (RP2350_CLOCKS_BASE + 0x44U)
#define RP2350_CLK_PERI_CTRL        (RP2350_CLOCKS_BASE + 0x48U)
#define RP2350_CLK_PERI_DIV         (RP2350_CLOCKS_BASE + 0x4CU)
#define RP2350_CLK_PERI_SELECTED    (RP2350_CLOCKS_BASE + 0x50U)

/* CLK_SYS_CTRL fields */
#define RP2350_CLK_SYS_AUXSRC_PLL_SYS   (0U << 5)
#define RP2350_CLK_SYS_AUXSRC_PLL_USB   (1U << 5)
#define RP2350_CLK_SYS_AUXSRC_ROSC      (2U << 5)
#define RP2350_CLK_SYS_AUXSRC_XOSC      (3U << 5)
#define RP2350_CLK_SYS_SRC_REF          (0U)
#define RP2350_CLK_SYS_SRC_AUX          (1U)

/* CLK_REF_CTRL fields */
#define RP2350_CLK_REF_SRC_ROSC         (0U)
#define RP2350_CLK_REF_SRC_AUX          (1U)
#define RP2350_CLK_REF_SRC_XOSC         (2U)
#define RP2350_CLK_REF_AUXSRC_PLL_USB   (0U << 5)
#define RP2350_CLK_REF_AUXSRC_ROSC      (1U << 5)

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
/* IO_BANK0 (per-pin function select + interrupt config)                     */
/*---------------------------------------------------------------------------*/

/* Per-pin block: 8 bytes (STATUS + CTRL) at offset 0x000..0x100 for
 * pins 0..31. CTRL offset for pin n = 0x004 + n*8. */
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
 * (4 bits per pin: LEVEL_LOW / LEVEL_HIGH / EDGE_LOW / EDGE_HIGH). */
#define RP2350_IO_BANK0_INTR(i)         (RP2350_IO_BANK0_BASE + 0x230U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTE(i)   (RP2350_IO_BANK0_BASE + 0x270U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTF(i)   (RP2350_IO_BANK0_BASE + 0x2B0U + ((i) * 4U))
#define RP2350_IO_BANK0_PROC0_INTS(i)   (RP2350_IO_BANK0_BASE + 0x2F0U + ((i) * 4U))

#define RP2350_IO_INT_LEVEL_LOW     0x1U
#define RP2350_IO_INT_LEVEL_HIGH    0x2U
#define RP2350_IO_INT_EDGE_LOW      0x4U
#define RP2350_IO_INT_EDGE_HIGH     0x8U

/*---------------------------------------------------------------------------*/
/* PADS_BANK0 (drive strength, pull, schmitt)                                */
/*---------------------------------------------------------------------------*/

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
/* SIO (Single-cycle I/O for fast GPIO)                                      */
/*---------------------------------------------------------------------------*/

#define RP2350_SIO_GPIO_IN          (RP2350_SIO_BASE + 0x004U)
#define RP2350_SIO_GPIO_OUT         (RP2350_SIO_BASE + 0x010U)
#define RP2350_SIO_GPIO_OUT_SET     (RP2350_SIO_BASE + 0x018U)
#define RP2350_SIO_GPIO_OUT_CLR     (RP2350_SIO_BASE + 0x020U)
#define RP2350_SIO_GPIO_OUT_XOR     (RP2350_SIO_BASE + 0x028U)
#define RP2350_SIO_GPIO_OE          (RP2350_SIO_BASE + 0x030U)
#define RP2350_SIO_GPIO_OE_SET      (RP2350_SIO_BASE + 0x038U)
#define RP2350_SIO_GPIO_OE_CLR      (RP2350_SIO_BASE + 0x040U)

/* High GPIO bank (pins 32..47): we don't expose these via VFS but
 * declare the addresses for completeness. */
#define RP2350_SIO_GPIO_HI_IN       (RP2350_SIO_BASE + 0x008U)
#define RP2350_SIO_GPIO_HI_OUT      (RP2350_SIO_BASE + 0x014U)

/*---------------------------------------------------------------------------*/
/* UART (PrimeCell PL011)                                                    */
/*---------------------------------------------------------------------------*/

/* The RP2350 UART blocks are PL011 — same register layout as on
 * the original Pi and many ARM SoCs. */
#define RP2350_UART_DR              (0x000U)  /* data */
#define RP2350_UART_RSR             (0x004U)  /* receive status / error clear */
#define RP2350_UART_FR              (0x018U)  /* flag */
#define RP2350_UART_IBRD            (0x024U)  /* integer baud-rate */
#define RP2350_UART_FBRD            (0x028U)  /* fractional baud-rate */
#define RP2350_UART_LCR_H           (0x02CU)  /* line control */
#define RP2350_UART_CR              (0x030U)  /* control */
#define RP2350_UART_IFLS            (0x034U)  /* IFIFO level select */
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
/* TIMER (TIMER0)                                                            */
/*---------------------------------------------------------------------------*/

/* 64-bit microsecond timer with four alarms. Reads must be of the
 * paired TIMERAWL/TIMERAWH or via TIMELR/TIMEHR (latched). */
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
/* RP2350 inserts two new registers (LOCKED, SOURCE) at 0x34 and 0x38
 * relative to the RP2040 layout. INTR/INTE/INTF/INTS are pushed
 * forward by 8 bytes. The previous offsets here matched the RP2040
 * map only and silently mis-addressed every interrupt-related write
 * (e.g. "INTE" landed on the real INTR, "INTS" landed on INTF) —
 * symptom: ALARM0 fires and INTR latches, but the IRQ never reaches
 * the NVIC because real INTE was never enabled. Datasheet §12.7.2. */
#define RP2350_TIMER0_LOCKED        (RP2350_TIMER0_BASE + 0x34U)
#define RP2350_TIMER0_SOURCE        (RP2350_TIMER0_BASE + 0x38U)
#define RP2350_TIMER0_INTR          (RP2350_TIMER0_BASE + 0x3CU)
#define RP2350_TIMER0_INTE          (RP2350_TIMER0_BASE + 0x40U)
#define RP2350_TIMER0_INTF          (RP2350_TIMER0_BASE + 0x44U)
#define RP2350_TIMER0_INTS          (RP2350_TIMER0_BASE + 0x48U)

/*---------------------------------------------------------------------------*/
/* WATCHDOG                                                                  */
/*---------------------------------------------------------------------------*/

#define RP2350_WD_CTRL              (RP2350_WATCHDOG_BASE + 0x00U)
#define RP2350_WD_LOAD              (RP2350_WATCHDOG_BASE + 0x04U)
#define RP2350_WD_REASON            (RP2350_WATCHDOG_BASE + 0x08U)
#define RP2350_WD_SCRATCH0          (RP2350_WATCHDOG_BASE + 0x0CU)
#define RP2350_WD_TICK              (RP2350_TICKS_BASE   + 0x18U)  /* watchdog tick on TICKS block */

#define RP2350_WD_CTRL_TIME_MASK    (0xFFFFFFU)
#define RP2350_WD_CTRL_PAUSE_DBG1   (1U << 25)
#define RP2350_WD_CTRL_PAUSE_DBG0   (1U << 26)
#define RP2350_WD_CTRL_PAUSE_JTAG   (1U << 24)
#define RP2350_WD_CTRL_ENABLE       (1U << 30)
#define RP2350_WD_CTRL_TRIGGER      (1U << 31)

/*---------------------------------------------------------------------------*/
/* TICKS BLOCK (per-block tick generators on RP2350 — datasheet §10.6)       */
/*---------------------------------------------------------------------------*/

/* The TICKS block hosts an array of 9 individually-controllable
 * tick generators. The watchdog and timer each own one. We need
 * the watchdog one to produce the 1 us tick to drive TIMER0. */
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
/* SysTick (Cortex-M)                                                        */
/*---------------------------------------------------------------------------*/

#define RP2350_SYST_CSR             (RP2350_SYST_BASE + 0x00U)
#define RP2350_SYST_RVR             (RP2350_SYST_BASE + 0x04U)
#define RP2350_SYST_CVR             (RP2350_SYST_BASE + 0x08U)
#define RP2350_SYST_CALIB           (RP2350_SYST_BASE + 0x0CU)

#define RP2350_SYST_CSR_ENABLE      (1U << 0)
#define RP2350_SYST_CSR_TICKINT     (1U << 1)
#define RP2350_SYST_CSR_CLKSRC_CPU  (1U << 2)  /* 1 = processor clock */
#define RP2350_SYST_CSR_COUNTFLAG   (1U << 16)

/*---------------------------------------------------------------------------*/
/* NVIC (basic ISER/ICER/ISPR/ICPR layout, indexed by IRQ number)           */
/*---------------------------------------------------------------------------*/

#define RP2350_NVIC_ISER0           (RP2350_NVIC_BASE + 0x000U)
#define RP2350_NVIC_ICER0           (RP2350_NVIC_BASE + 0x080U)
#define RP2350_NVIC_ISPR0           (RP2350_NVIC_BASE + 0x100U)
#define RP2350_NVIC_ICPR0           (RP2350_NVIC_BASE + 0x180U)
#define RP2350_NVIC_IPR0            (RP2350_NVIC_BASE + 0x300U)

/*
 * Common IRQ numbers we use (RP2350 datasheet §3.6.1 IRQ mapping +
 * pico-sdk hardware/regs/intctrl.h):
 *
 *    0  TIMER0_IRQ_0
 *    1  TIMER0_IRQ_1
 *   21  IO_IRQ_BANK0
 *   33  UART0_IRQ
 *   34  UART1_IRQ
 */
#define RP2350_IRQ_TIMER0_0         0
#define RP2350_IRQ_TIMER0_1         1
#define RP2350_IRQ_IO_BANK0         21
#define RP2350_IRQ_UART0            33
#define RP2350_IRQ_UART1            34

static inline void rp2350_nvic_enable(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ISER0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

static inline void rp2350_nvic_disable(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ICER0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

static inline void rp2350_nvic_clear_pending(uint32_t irq) {
    *(volatile uint32_t *)(RP2350_NVIC_ICPR0 + (irq / 32U) * 4U)
        = (1U << (irq & 31U));
}

/*---------------------------------------------------------------------------*/
/* RESETS helpers                                                            */
/*---------------------------------------------------------------------------*/

/* Bring a peripheral out of reset and spin-wait until it reports ready. */
static inline void rp2350_unreset(uint32_t mask) {
    _RP2350_REG_CLR(RP2350_RESETS_RESET, mask);
    while ((_RP2350_REG(RP2350_RESETS_RESET_DONE) & mask) != mask) {
        /* spin */
    }
}

static inline void rp2350_reset(uint32_t mask) {
    _RP2350_REG_SET(RP2350_RESETS_RESET, mask);
}

#endif /* TIKU_RP2350_REGS_H_ */
