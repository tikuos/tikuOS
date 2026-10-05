/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_arch.h - RA8P1 code-MRAM programming.
 *
 * Writes are byte-granular and in place, with no erase: a store enters a
 * 32-byte program buffer, and tiku_ra8p1_mram_flush() commits it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_MRAM_ARCH_H_
#define TIKU_RA8P1_MRAM_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Result codes; negative values are failures. */
#define TIKU_RA8P1_MRAM_OK        0
#define TIKU_RA8P1_MRAM_ERR_BUSY -1  /**< buffer or sequencer wait timed out */
#define TIKU_RA8P1_MRAM_ERR_PROG -2  /**< PRGERRC latched during programming */
#define TIKU_RA8P1_MRAM_ERR_ECC  -3  /**< ECCERRC latched during programming */

/**
 * @brief Open or close the code-MRAM secure programming window.
 *
 * Sets or clears MRCPSEN and BPCN1.  While the window is closed, a store to
 * code MRAM raises a bus fault.
 *
 * @param on  Non-zero to permit programming, zero to prohibit it
 * @note Open the window around a write and close it after; it is not a
 *       boot-time setting.
 */
void tiku_ra8p1_mram_program_enable(int on);

/**
 * @brief Commit the code-MRAM program buffer and wait for the sequencer.
 *
 * Reads and clears the programming error flags once the sequencer is idle.
 *
 * @return TIKU_RA8P1_MRAM_OK, or a negative error code
 */
int tiku_ra8p1_mram_flush(void);

/**
 * @brief Copy @p len bytes into code MRAM and commit them.
 *
 * A NULL pointer or a zero length writes nothing and returns
 * TIKU_RA8P1_MRAM_OK.
 *
 * @param dst  Destination inside the code-MRAM window
 * @param src  Source
 * @param len  Byte count; any length, any alignment
 * @return TIKU_RA8P1_MRAM_OK, or a negative error code
 * @note The programming window must be open; see
 *       tiku_ra8p1_mram_program_enable().
 */
int tiku_ra8p1_mram_write(void *dst, const void *src, size_t len);

/**
 * @brief Select the high-speed programming mode.
 *
 * @param on  Non-zero for high-speed, zero for the normal mode
 */
void tiku_ra8p1_mram_high_speed(int on);

#endif /* TIKU_RA8P1_MRAM_ARCH_H_ */
