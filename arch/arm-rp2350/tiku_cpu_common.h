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
 * Implemented against the 1 µs TIMER0 reading in
 * arch/arm-rp2350/tiku_cpu_common.c. Spins the CPU; do not call from
 * an ISR or in energy-sensitive paths.
 *
 * @param ms  Number of milliseconds to wait.
 */
void tiku_cpu_rp2350_delay_ms(unsigned int ms);

/**
 * @brief Blocking busy-wait for the given number of microseconds.
 *
 * Reads TIMER0's 64-bit time_us register and spins until the target
 * time has elapsed. Resolution is 1 µs; accuracy depends on the
 * system clock being stable.
 *
 * @param us  Number of microseconds to wait.
 */
void tiku_cpu_rp2350_delay_us(unsigned int us);

/**
 * @brief Read the on-chip unique device identifier.
 *
 * The RP2350's flash chip holds a unique 64-bit ID, but this port exposes a
 * synthesised value derived from a few SRAM-relative addresses so tests stay
 * deterministic across reboots without a flash-readback routine.
 *
 * @param buf  Destination buffer (caller-provided).
 * @param len  Number of bytes to copy (1..8).
 * @return Number of bytes actually written to buf.
 */
uint8_t  tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return the reset cause for the most recent boot.
 *
 * Returns 0 on a cold power-on boot; non-zero values encode the
 * watchdog or external reset reason (mirrors WD_REASON). The kernel
 * exposes this at /sys/boot/reason.
 *
 * @return 0 for cold boot; non-zero for watchdog/external reset.
 */
uint16_t tiku_cpu_rp2350_reset_reason(void);

/**
 * @brief Reboot the device into USB BOOTSEL mass-storage mode.
 *
 * Drains the UART TX FIFO, disables interrupts and asks the boot ROM to reboot
 * into BOOTSEL.  On success this does not return; on a ROM lookup miss it falls
 * through to a watchdog reset, which reboots but does NOT enter BOOTSEL.
 *
 * @note Used by the test harness when TIKU_TEST_AUTO_BOOTSEL is defined, so the
 *       Python loop runner can chain test categories without a button press.
 */
void tiku_cpu_rp2350_reboot_to_bootsel(void) __attribute__((noreturn));

#endif /* TIKU_RP2350_CPU_COMMON_H_ */
