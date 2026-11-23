/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_arch.h - S31 CPU rate: the 40 MHz crystal or the 320 MHz CPLL,
 * switched at run time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_CLOCK_ARCH_H_
#define TIKU_S31_CLOCK_ARCH_H_

/** @brief Whether @p mhz is a rate the port runs now: 40 from the crystal,
 *         or 80, 160 or 320 from a locked CPLL, and in either case at
 *         least three quarters of the PSRAM bus rate. */
int tiku_s31_clock_offers(unsigned mhz);

/**
 * @brief Switch the CPU to @p mhz with interrupts masked, after the console
 *        has sent what it holds, and update the ROM delay calibration.
 *
 * Kernel thread or boot only.  A switch that does not read back restores
 * the previous rate and sets the fault flag.
 *
 * @return 0, or -1 for a rate not offered or a switch that did not take
 */
int tiku_s31_clock_set(unsigned mhz);

/** @brief The CPU rate in force, in MHz. */
unsigned tiku_s31_clock_mhz(void);

/** @brief The APB clock in force, in Hz: 40 MHz, or 53.33 MHz at 320. */
unsigned long tiku_s31_clock_apb_hz(void);

/** @brief Non-zero once a switch failed to read back. */
int tiku_s31_clock_fault(void);

/** @brief The CPU rate counted on the chip: mcycle over 10 ms of the 16 MHz
 *  system timer (tiku_cpu_services.c). */
unsigned long tiku_s31_clock_measure_hz(void);

#endif /* TIKU_S31_CLOCK_ARCH_H_ */
