/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_nrf54lm20a.h - Nordic nRF54LM20A silicon-level constants.
 *
 * The larger-memory sibling of the nRF54L15, with the same clock, RRAMC and
 * GRTC: 512 KB SRAM in two contiguous banks, about 2 MB of RRAM, and a fourth
 * GPIO port.  All-Secure, so peripherals use the _S aliases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_NRF54LM20A_H_
#define TIKU_DEVICE_NRF54LM20A_H_

#include <stdint.h>
#include <arch/nordic/mdk/nrf54lm20a.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "nRF54LM20A"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief USB 2.0 high-speed device: a Synopsys DWC2 core behind the Nordic
 *        wrapper, with its own VBUS regulator.
 */
#define TIKU_DEVICE_HAS_USBHS       1

/**
 * @brief Virtual GPIO port availability flags.
 *
 * Virtual ports 1..4 (/dev/gpio/{1..4}) are P0 (LP domain, P0.00..P0.09), P1
 * (PERI, P1.00..P1.31), P2 (MCU, P2.00..P2.10) and P3 (PERI, P3.00..P3.12);
 * tiku_gpio_arch.c maps each to its register block.
 */
#define TIKU_DEVICE_HAS_PORT1       1   /* P0 */
#define TIKU_DEVICE_HAS_PORT2       1   /* P1 */
#define TIKU_DEVICE_HAS_PORT3       1   /* P2 */
#define TIKU_DEVICE_HAS_PORT4       1   /* P3 */
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       0

/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Crystal oscillator availability and frequency.
 *
 * The DK fits a 32 MHz HFXO (the high-frequency source) and a 32.768 kHz
 * LFXO (LFCLK, GRTC).
 */
#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         32000000UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock system type flags: no unlock key, Nordic clock system.
 *
 * The Makefile compiles the Nordic clock driver, tiku_cpu_freq_boot_arch.c,
 * for every Nordic part; it sets the core to 64 or 128 MHz once at boot.
 */
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_NORDIC  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Maximum stable CPU frequency in MHz (datasheet). */
#define TIKU_DEVICE_MAX_STABLE_MHZ  128

/**
 * @brief Core frequency of a default build, in Hz.
 *
 * The boot sets the PLL to TIKU_NORDIC_CPU_MHZ (128 unless overridden) or to
 * a saved rate.  Delays and tiku_cpu_mclk_hz() read the running rate from
 * the PLL.
 */
#define TIKU_DEVICE_BOOT_CPU_HZ     128000000UL

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Size of the lower SRAM bank (RAM, 256 KB at 0x20000000).
 *
 * The part has 512 KB in two banks, RAM and RAM2 (at 0x20040000).  The image,
 * .uninit and the stack live in the lower bank, as in Nordic's nrf_common.ld.
 */
#define TIKU_DEVICE_RAM_SIZE        (256UL * 1024UL)

/**
 * @brief App-usable SRAM: the lower bank less its top 16 KB, the FLPR (VPR
 *        RISC-V) coprocessor carve at 0x2003C000..0x2003FFFF.
 *
 * Every build reserves the carve, and memory reports use this figure.  It
 * must equal LENGTH(SRAM) in nrf54lm20a.ld.
 */
#define TIKU_DEVICE_RAM_USABLE      (240UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief RAM2, the upper SRAM bank: the .ram2 statics, then the SRAM tier.
 *
 * A linker region of its own (SRAM2) and a second SRAM entry in the region
 * table.  The top of the bank is not fully backed on the DK's nRF54LM20B (CPU
 * writes from 0x2007FF00 up bus-fault), so the top 1 KB is left out.
 */
#define TIKU_DEVICE_RAM2_START      0x20040000UL
#define TIKU_DEVICE_RAM2_SIZE       0x0003FC00UL   /* = LENGTH(SRAM2), 255 KB */

/**
 * @brief On-chip RRAM range, under the FRAM_* names the kernel's memory
 *        reports and the NVM region table use.
 *
 * 0x1FD000 bytes (2036 KB) at 0x0 hold code and the persistent region; the
 * top 12 KB of the 2 MB array is reserved (MDK NRF_MEMORY_FLASH_SIZE) and
 * bus-faults if addressed.  RRAM is written in place behind RRAMC WEN.
 */
#define TIKU_DEVICE_FRAM_SIZE       0x001FD000UL
#define TIKU_DEVICE_FRAM_START      0x00000000UL
#define TIKU_DEVICE_FRAM_END        0x001FCFFFUL
#define TIKU_DEVICE_NVM_LABEL       "RRAM"   /**< NVM technology (UI label). */

/**
 * @brief Init-table backing region size in bytes.
 *
 * Holds the init table: a 4-byte header and TIKU_INIT_MAX_ENTRIES (8)
 * entries; tiku_init.c asserts the fit at build time.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U

/** @brief Application slot size and count in RRAM; no code reads them. */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory Protection Unit availability flag.
 *
 * The Cortex-M33 has the ARMv8-M MPU.  The RRAMC WEN gate is this port's
 * NVM write barrier; tiku_mpu_arch.c programs the MPU for the stack guard
 * and execute-never SRAM.
 */
#define TIKU_DEVICE_HAS_MPU         1

/*---------------------------------------------------------------------------*/
/* PERIPHERAL DEFAULTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default UART baud rate for the DK console (VCOM).
 *
 * Override via -DTIKU_BOARD_UART_BAUD=... on the make line.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_NRF54LM20A_H_ */
