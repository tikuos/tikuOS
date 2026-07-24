/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_nrf54lm20b.h - Nordic nRF54LM20B silicon-level constants
 *
 * The nRF54LM20B is the nRF54LM20A plus the 128 MHz Axon NPU (NRF_AXONS @
 * 0x50056000, IRQn 86, MCU power domain); every other block, the memory map
 * and the IRQ enum are identical (diff-proven).  Like the A it is an Arm
 * Cortex-M33 (128 MHz max, FPv5-SP FPU, TrustZone,
 * ARMv8-M MPU) wireless MCU -- the higher-memory sibling of the nRF54L15 in
 * the same nRF54L family (identical PLL/clock/RRAMC/GRTC architecture), with:
 *   - 512 KB on-chip SRAM at 0x20000000 (RAM 256 KB + RAM2 256 KB, contiguous).
 *   - ~2 MB on-chip RRAM (write-in-place non-volatile memory) at 0x0, holding
 *     code + the TikuOS persistent/config region.  RRAM needs no erase cycle:
 *     a WEN write-enable gate behind the RRAMC controller maps onto
 *     tiku_mpu_unlock_nvm()/lock_nvm() exactly like MSP430 FRAM.
 *   - Four GPIO ports (P0 / P1 / P2 / P3), modelled as virtual ports 1/2/3/4
 *     (one more than the nRF54L15, which lacks P3).
 *   - GRTC (global RTC, 1 MHz off LFCLK) as the kernel tick source.
 *   - DMA-based UARTE peripherals for the console.
 *
 * The device runs All-Secure (no TF-M / SPM); peripherals use the secure
 * (_S, 0x5xxx_xxxx) aliases.  See arch/nordic/mdk/nrf54lm20a.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_NRF54LM20B_H_
#define TIKU_DEVICE_NRF54LM20B_H_

#include <stdint.h>
#include <arch/nordic/mdk/nrf54lm20b.h>
#include <arch/nordic/tiku_nordic_core.h>

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable device name string. */
#define TIKU_DEVICE_NAME            "nRF54LM20B"

/** @brief Axon NPU present (the one block the LM20A lacks). */
#define TIKU_DEVICE_HAS_AXONS       1

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Virtual GPIO port availability flags.
 *
 * The nRF54LM20B exposes four physical GPIO ports; TikuOS maps them to
 * virtual ports 1..4 so the /dev/gpio/{1..4}/{0..N} VFS layout works:
 *   port 1 = P0 (LP domain,   P0.00..P0.09)
 *   port 2 = P1 (PERI domain,  P1.00..P1.31)
 *   port 3 = P2 (MCU domain,   P2.00..P2.10)
 *   port 4 = P3 (PERI domain,  P3.00..P3.12)
 * The port->base-pointer mapping lives in the GPIO arch layer.
 */
#define TIKU_DEVICE_HAS_PORT1       1   /* P0 */
#define TIKU_DEVICE_HAS_PORT2       1   /* P1 */
#define TIKU_DEVICE_HAS_PORT3       1   /* P2 */
#define TIKU_DEVICE_HAS_PORT4       1   /* P3 (new vs nRF54L15) */
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
 * 128 MHz (the PLL FREQ options are CK64M / CK128M, identical to the L15;
 * boot programs CK128M).
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
 * The shared nRF54L boot programs OSCILLATORS.PLL.FREQ = CK128M, so the core
 * runs at 128 MHz.  SysTick-based busy-delays use this value; the delay layer
 * additionally reads live CURRENTFREQ, so it stays correct either way.
 */
#define TIKU_DEVICE_BOOT_CPU_HZ     128000000UL

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief On-chip SRAM size and base address.
 *
 * The LM20B has 512 KB physical SRAM in two banks -- RAM (256 KB @ 0x20000000)
 * + RAM2 (256 KB @ 0x20040000).  The LOWER bank is the primary/managed bank:
 * image (.data/.bss), .uninit and the stack live there, matching Nordic's own
 * nrf_common.ld (stack at the top of the lower bank; RAM2 = separate opt-in
 * region).  RAM_SIZE reports the primary bank. */
#define TIKU_DEVICE_RAM_SIZE        (256UL * 1024UL)
#define TIKU_DEVICE_RAM_START       0x20000000UL

/**
 * @brief RAM2: the upper SRAM bank, used for large buffers (the tier arena).
 *
 * Exposed as its own linker region (SRAM2 in nrf54lm20a.ld, section .ram2,
 * zeroed by the crt) and a second SRAM entry in the region table so tier
 * sub-arenas validate.  The TOP OF THE BANK IS NOT FULLY BACKED on this
 * silicon: a CPU write to 0x2007FF00 bus-faults (measured on the LM20-DK's
 * nRF54LM20B eng sample -- a boot-time stack at 0x20080000 dies with STKERR,
 * BFAR 0x2007FFF0, and a 256 B-step write probe faults first at 0x2007FF00
 * while 0x2007FExx passes).  The MDK claims the full 0x40000; reserve the top
 * 1 KB for margin and expose 255 KB.
 */
#define TIKU_DEVICE_RAM2_START      0x20040000UL
#define TIKU_DEVICE_RAM2_SIZE       0x0003FC00UL   /* 255 KB (top 1 KB reserved) */

/**
 * @brief On-chip RRAM range (exposed under the FRAM_* vocabulary).
 *
 * ~2 MB non-volatile RRAM at 0x0 holds code and the TikuOS persistent /
 * config region.  Exposed via the FRAM_* names so the kernel memory
 * introspection and NVM region table share one vocabulary; RRAM is
 * write-in-place (no erase) behind the RRAMC WEN gate.
 *
 * Usable application RRAM is 0x1FD000 (2036 KB); the top 12 KB of the nominal
 * 2 MB is reserved (MDK NRF_MEMORY_FLASH_SIZE) and bus-faults if addressed --
 * the same reserved-tail pattern as the nRF54L15.
 */
#define TIKU_DEVICE_FRAM_SIZE       0x001FD000UL
#define TIKU_DEVICE_FRAM_START      0x00000000UL
#define TIKU_DEVICE_FRAM_END        0x001FCFFFUL
#define TIKU_DEVICE_NVM_LABEL       "RRAM"   /**< NVM technology (UI label). */

/**
 * @brief Init-table backing region size in bytes.
 *
 * Sized to hold the 4-byte header + 8 init entries; matches the nRF54L15
 * value.
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

#endif /* TIKU_DEVICE_NRF54LM20B_H_ */
