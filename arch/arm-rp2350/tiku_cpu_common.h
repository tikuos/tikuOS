/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - RP2350 common-utility prototypes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_COMMON_H_
#define TIKU_RP2350_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Blocking busy-wait for the given number of milliseconds.
 *
 * Spins on the 1 us TIMER0 count in arch/arm-rp2350/tiku_cpu_common.c.
 *
 * @note Holds the CPU for the whole wait; do not call it from an ISR or an
 *       energy-sensitive path.
 * @param ms  Number of milliseconds to wait.
 */
void tiku_cpu_rp2350_delay_ms(unsigned int ms);

/**
 * @brief Blocking busy-wait for the given number of microseconds.
 *
 * Spins on the low 32 bits of TIMER0's 1 us counter (TIMERAWL) until at
 * least @p us microseconds have passed.  TIMER0 ticks from clk_ref (the XOSC)
 * through the TICKS block, so a CPU frequency change does not alter it.
 *
 * @param us  Number of microseconds to wait.
 */
void tiku_cpu_rp2350_delay_us(unsigned int us);

/**
 * @brief Read the on-chip unique device identifier.
 *
 * Reads the RP2350 per-die chip identifier through the boot ROM.
 * The eight bytes are stable across firmware updates.
 *
 * @param buf  Destination buffer (caller-provided).
 * @param len  Number of bytes to copy; more than 8 is clamped to 8.
 * @return Bytes copied; 0 for invalid arguments or an unavailable ROM ID.
 */
uint8_t  tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return the reset cause for the most recent boot.
 *
 * WD_REASON mapped onto the SYSRSTIV-style codes /sys/boot/reason decodes.
 *
 * @return 0x0016 after a watchdog timeout, 0x0006 after a forced watchdog
 *         reset, 0 after any other reset
 */
uint16_t tiku_cpu_rp2350_reset_reason(void);

/**
 * @brief Reboot the device into USB BOOTSEL mass-storage mode.
 *
 * Drains the UART TX FIFO, disables interrupts and asks the boot ROM to reboot
 * into BOOTSEL.  Never returns: when no ROM call takes effect it resets
 * through the watchdog, which restarts TikuOS without entering BOOTSEL.
 */
void tiku_cpu_rp2350_reboot_to_bootsel(void) __attribute__((noreturn));

#endif /* TIKU_RP2350_CPU_COMMON_H_ */
