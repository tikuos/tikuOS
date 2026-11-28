/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_arch.h - P4 CPU clock control from internal SRAM.
 *
 * The CPU runs from the 40 MHz crystal or the calibrated CPLL at full,
 * half or quarter rate. The P4X uses a 400 MHz CPLL, the v1.3 boards 360 MHz.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_CLOCK_ARCH_H_
#define TIKU_ESP32P4_CLOCK_ARCH_H_
#if defined(TIKU_BOARD_ESP32_P4X_FUNCTION_EV_BOARD)
#define TIKU_P4_CPLL_MHZ 400u
#else
#define TIKU_P4_CPLL_MHZ 360u
#endif

/**
 * @brief Select 40 MHz crystal clocks for CPU, memory, system and APB.
 * @note CPU0 SRAM startup only, with IRQs masked and other cores parked.
 * @return Zero on clock-update completion; -1 on bounded timeout.
 */
int tiku_p4_clock_xtal_init(void);
/** @brief Initialize the board CPLL; leave the CPU at 40 MHz.
 * @note Requires masked IRQs, CPU0 in SRAM, and other cores parked.
 * @return Zero on success, negative on power, calibration or clock failure.
 */
int tiku_p4_clock_pll_init(void);
/** @brief Select XTAL or a full, half or quarter CPLL rate; return -1 for unsupported/unready
 * requests, -2 on divider failure. A divider failure selects the XTAL root.
 * @note SRAM diagnostic only; no active DMA or clock-dependent peripherals.
 */
int tiku_p4_clock_set_mhz(unsigned mhz);
/** @brief Register @p follower (the PSRAM bus), called with the target
 *         clock before a step down and with the CPU clock after a switch. */
void tiku_p4_clock_follow(void (*follower)(unsigned mhz));
#endif
