/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_ra8p1.h - R7KA8P1KF silicon constants.
 *
 * Sizes and bases are the manual's; the SRAM extent is this board's, measured
 * with the debugger rather than taken from the capacity table, because the two
 * disagree at the top (see kintsugi/ra8p1-port.md, R1 log).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_RA8P1_H_
#define TIKU_DEVICE_RA8P1_H_

/** @brief Human-readable device name, used by `info` and the boot banner. */
#define TIKU_DEVICE_NAME            "R7KA8P1KF"

/*---------------------------------------------------------------------------*/
/* Memories                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Code MRAM: 1 MB at 0x0200_0000 (holds the factory image today). */
#define TIKU_RA8P1_MRAM_BASE        0x02000000UL
#define TIKU_RA8P1_MRAM_SIZE        (1024UL * 1024UL)

/**
 * @brief User SRAM base.
 *
 * MEASURED read/write to 0x221C_0000 on this board; accesses above that abort.
 * The datasheet's 1664 KB user SRAM would end at 0x221A_0000, so the extent
 * below is the smaller, provable one -- the port does not rely on the 128 KB
 * the two figures disagree about.
 */
#define TIKU_RA8P1_SRAM_BASE        0x22000000UL
#define TIKU_RA8P1_SRAM_SIZE        (1664UL * 1024UL)

/**
 * @brief CM85 TCM window.
 *
 * Responds at the base, but repeated probing of its extent gave inconsistent
 * results in R1, so nothing is placed here until the TCM control registers are
 * driven deliberately.  Recorded, not used.
 */
#define TIKU_RA8P1_TCM_BASE         0x20000000UL

/*---------------------------------------------------------------------------*/
/* Clocks                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Peripheral clock A after reset, in Hz.
 *
 * SCKDIVCR reads 0 out of reset -- MEASURED -- so every divider is /1 and both
 * ICLK and PCLKA are the middle-speed oscillator.  The 8 MHz below is MOCO's
 * NOMINAL rate; its spec is 7.2 / 8.0 / 8.8 (datasheet Table, FMOCO), and this
 * board measures 8.330 MHz -- +4.12%, well inside spec.
 *
 * The nominal figure is nevertheless the right constant to derive from, and
 * the reason is worth stating: it is what the manual's own baud tables are
 * computed against, so a divisor derived from it agrees with the table a
 * reader will check it against.  What it costs is that every derived rate
 * inherits MOCO's tolerance -- the 128 Hz tick really runs at 133.3 Hz here,
 * and the 9600 console really runs at 10012 baud, which is inside 8N1's
 * framing tolerance but not by much.  R4's crystal-referenced PLL is what
 * turns these from nominal into true; nothing before R4 should be believed to
 * better than 10%.
 */
#define TIKU_RA8P1_MOCO_HZ          8000000UL
#define TIKU_RA8P1_PCLKA_BOOT_HZ    TIKU_RA8P1_MOCO_HZ
#define TIKU_RA8P1_ICLK_BOOT_HZ     TIKU_RA8P1_MOCO_HZ

/*---------------------------------------------------------------------------*/
/* Interrupts                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief External interrupt count, for the vector table and the NVIC loops.
 *
 * The RA event-link controller maps peripheral events onto a fixed bank of
 * NVIC lines; 96 covers the bank with room to spare and keeps the table one
 * page.  Nothing external is wired in R2 -- the tick is SysTick, which is a
 * core exception and needs no NVIC line at all.
 */
#define TIKU_RA8P1_NUM_EXT_IRQS     96

#endif /* TIKU_DEVICE_RA8P1_H_ */
