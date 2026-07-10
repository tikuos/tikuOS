/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_nrf54l15.h - Nordic nRF54L15 silicon-level constants
 *
 * The nRF54L15 is an Arm Cortex-M33 (128 MHz max, FPv5-SP FPU, TrustZone,
 * ARMv8-M MPU) wireless MCU with:
 *   - 256 KB on-chip SRAM at 0x20000000.
 *   - 1.5 MB on-chip RRAM (write-in-place non-volatile memory) at 0x0,
 *     holding code + the TikuOS persistent/config region.  RRAM needs no
 *     erase cycle: a WEN write-enable gate behind the RRAMC controller
 *     maps onto tiku_mpu_unlock_nvm()/lock_nvm() exactly like MSP430 FRAM.
 *   - Three GPIO ports (P0 / P1 / P2), modelled as virtual ports 1/2/3.
 *   - GRTC (global RTC, 1 MHz off LFCLK) as the kernel tick source.
 *   - New DMA-based UARTE peripherals for the console.
 *
 * The device runs All-Secure (no TF-M / SPM); peripherals use the secure
 * (_S, 0x5xxx_xxxx) aliases.  See arch/nordic/mdk/nrf54l15.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_NRF54L15_H_
#define TIKU_DEVICE_NRF54L15_H_

#include <stdint.h>
#include <arch/nordic/mdk/nrf54l15.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "nRF54L15"

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Virtual GPIO port availability flags.
 *
 * The nRF54L15 exposes three physical GPIO ports; TikuOS maps them to
 * virtual ports 1..3 so the /dev/gpio/{1..3}/{0..N} VFS layout works:
 *   port 1 = P0 (LP domain, P0.00..P0.04)
 *   port 2 = P1 (P1.00..P1.15)
 *   port 3 = P2 (P2.00..P2.10)
 * The port->base-pointer mapping lives in the GPIO arch layer.
 */
#define TIKU_DEVICE_HAS_PORT1       1   /* P0 */
#define TIKU_DEVICE_HAS_PORT2       1   /* P1 */
#define TIKU_DEVICE_HAS_PORT3       1   /* P2 */
#define TIKU_DEVICE_HAS_PORT4       0
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
 * The DK provides a 32 MHz HFXO (system high-frequency source) and a
 * 32.768 kHz LFXO (feeds LFCLK / GRTC).  Both are reported present.
 */
#define TIKU_DEVICE_HAS_LFXT        1
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         32000000UL

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock system type selector flags.
 *
 * No MSP430-style unlock key.  TIKU_DEVICE_CS_TYPE_NORDIC selects the
 * nRF54L clock driver in arch/nordic/tiku_cpu_freq_*.c.  The core runs at
 * 128 MHz on this DK (OSCILLATORS.PLL.CURRENTFREQ reads CK128M).
 */
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_CS_TYPE_NORDIC  1

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Maximum stable CPU frequency in MHz (datasheet). */
#define TIKU_DEVICE_MAX_STABLE_MHZ  128

/**
 * @brief Core frequency on this DK, in Hz.
 *
 * Measured on hardware: OSCILLATORS.PLL.CURRENTFREQ reads CK128M, so the
 * core runs at 128 MHz (the CK64M register reset value is overridden by the
 * boot configuration).  SysTick-based busy-delays use this value.
 */
#define TIKU_DEVICE_BOOT_CPU_HZ     128000000UL

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief On-chip SRAM size and base address (256 KB @ 0x20000000). */
#define TIKU_DEVICE_RAM_SIZE        (256UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief On-chip RRAM range (exposed under the FRAM_* vocabulary).
 *
 * 1.5 MB non-volatile RRAM at 0x0 holds code and the TikuOS persistent /
 * config region.  Exposed via the FRAM_* names so the kernel memory
 * introspection and NVM region table share one vocabulary; RRAM is
 * write-in-place (no erase) behind the RRAMC WEN gate.
 */
#define TIKU_DEVICE_FRAM_SIZE       (1536UL * 1024UL)
#define TIKU_DEVICE_FRAM_START      0x00000000UL
#define TIKU_DEVICE_FRAM_END        0x0017FFFFUL
#define TIKU_DEVICE_NVM_LABEL       "RRAM"   /**< NVM technology (UI label). */

/**
 * @brief Init-table backing region size in bytes.
 *
 * Sized to hold the 4-byte header + 8 init entries; matches the RP2350
 * port's value with headroom (see tiku_device_rp2350.h rationale).
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      576U

/** @brief Application slot parameters within the RRAM region. */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    4096U
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   4

/*---------------------------------------------------------------------------*/
/* MPU                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Memory Protection Unit availability flag.
 *
 * The Cortex-M33 has the ARMv8-M MPU.  On this port the RRAMC WEN gate is
 * the primary NVM write barrier (like MSP430 FRAM); the MPU can additionally
 * enforce RO-by-default on the persistent region (see tiku_mpu_arch.c).
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

#endif /* TIKU_DEVICE_NRF54L15_H_ */
