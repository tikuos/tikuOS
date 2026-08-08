/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_ra8p1.h - R7KA8P1KF silicon constants.
 *
 * Sizes and bases are the manual's; the SRAM extent is the datasheet's
 * 1664 KB, which is the span this port places data in.
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

/** @brief Code MRAM: 1 MB at 0x0200_0000; holds the image, the NVM region
 *         and the durable carve. */
#define TIKU_RA8P1_MRAM_BASE        0x02000000UL
#define TIKU_RA8P1_MRAM_SIZE        (1024UL * 1024UL)

/**
 * @brief User SRAM base and extent.
 *
 * The datasheet's 1664 KB from the base, ending at 0x221A_0000.  The part
 * still responds above that; nothing is placed there.
 */
#define TIKU_RA8P1_SRAM_BASE        0x22000000UL
#define TIKU_RA8P1_SRAM_SIZE        (1664UL * 1024UL)

/**
 * @brief CM85 TCM window: recorded, not used.
 *
 * Responds at the base.  Nothing is placed here: the TCM control registers
 * are not driven by this port, so the usable extent is not established.
 */
#define TIKU_RA8P1_TCM_BASE         0x20000000UL

/*---------------------------------------------------------------------------*/
/* Clocks                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Peripheral clock A after reset, in Hz.
 *
 * SCKDIVCR reads 0 out of reset, so ICLK and PCLKA are both MOCO.  This is
 * MOCO's NOMINAL rate (spec 7.2/8.0/8.8), so anything derived from it before
 * the PLL is configured carries MOCO's +-10% spread.
 */
#define TIKU_RA8P1_MOCO_HZ          8000000UL
#define TIKU_RA8P1_PCLKA_BOOT_HZ    TIKU_RA8P1_MOCO_HZ
#define TIKU_RA8P1_ICLK_BOOT_HZ     TIKU_RA8P1_MOCO_HZ

/*---------------------------------------------------------------------------*/
/* Kernel-facing device description                                          */
/*---------------------------------------------------------------------------*/

/*
 * I/O ports.  The manual names them PORT0..PORT9 then PORTA..PORTD, and the
 * VFS gpio tree numbers its nodes 1..9 -- so /dev/gpio/6 is PORT6, the one
 * carrying LED1.  PORTA..PORTD have no node under that numbering; the LED
 * interface reaches PA07 through the board macros instead.
 */
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       1
#define TIKU_DEVICE_HAS_PORT6       1
#define TIKU_DEVICE_HAS_PORT7       1
#define TIKU_DEVICE_HAS_PORT8       1
#define TIKU_DEVICE_HAS_PORT9       1
#define TIKU_DEVICE_HAS_PORTJ       0   /* MSP430 port J has no RA analogue */

/* Both crystals are fitted on the EK (kit UM Table 8). */
#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         24000000UL
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_RA8P1   1
#define TIKU_DEVICE_MAX_STABLE_MHZ  1000

/* SRAM as the port uses it: 1664 KB from the base. */
#define TIKU_DEVICE_RAM_START       TIKU_RA8P1_SRAM_BASE
#define TIKU_DEVICE_RAM_SIZE        TIKU_RA8P1_SRAM_SIZE
/* What the M85 may actually use.  The first 16 KB of the bank is the CPU1
 * payload area (TIKU_CPU1_AREA_SIZE), which the M85 linker starts above, so
 * counting the whole bank overstates this core's SRAM by that much. */
#define TIKU_DEVICE_RAM_USABLE      (TIKU_RA8P1_SRAM_SIZE - (16UL * 1024UL))

/*
 * The code MRAM: the image runs from it, `.persistent` lives in a carve at
 * its top, and the in-use figure is derived from _etext.
 */
#define TIKU_DEVICE_FRAM_SIZE       TIKU_RA8P1_MRAM_SIZE
#define TIKU_DEVICE_FRAM_START      TIKU_RA8P1_MRAM_BASE
#define TIKU_DEVICE_FRAM_END        (TIKU_RA8P1_MRAM_BASE + \
                                     TIKU_RA8P1_MRAM_SIZE - 1UL)
#define TIKU_DEVICE_NVM_LABEL       "MRAM"

/** @brief Config region for the init table, inside the durable carve. */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE  576U

#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* Interrupts                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief External interrupt count, for the vector table and the NVIC loops.
 *
 * The ICU maps peripheral events onto any of these slots; 96 covers the bank
 * with room to spare and keeps the vector table one page.
 */
#define TIKU_RA8P1_NUM_EXT_IRQS     96

#endif /* TIKU_DEVICE_RA8P1_H_ */
