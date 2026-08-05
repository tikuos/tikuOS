/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_arch.h - RA8P1 code-MRAM programming.
 *
 * Byte-granular writes in place: an STR enters a 32-byte buffer that a flush
 * commits.  No erase cycle and no mirror, so a store IS the durable write.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_MRAM_ARCH_H_
#define TIKU_RA8P1_MRAM_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Result codes; negative values are failures. */
#define TIKU_RA8P1_MRAM_OK        0
#define TIKU_RA8P1_MRAM_ERR_BUSY -1  /**< sequencer never went idle        */
#define TIKU_RA8P1_MRAM_ERR_PROG -2  /**< PRGERRC latched during programming */
#define TIKU_RA8P1_MRAM_ERR_ECC  -3  /**< ECCERRC latched during programming */

/**
 * @brief Open or close the code-MRAM secure programming window.
 *
 * Without it a store to MRAM BUS FAULTS rather than being dropped, so this
 * is a gate to open deliberately and close again, not a boot-time setting.
 *
 * @param on  Non-zero to permit programming, zero to prohibit it
 */
void tiku_ra8p1_mram_program_enable(int on);

/**
 * @brief Commit whatever is sitting in the code-MRAM program buffer.
 *
 * @return TIKU_RA8P1_MRAM_OK, or a negative error code
 */
int tiku_ra8p1_mram_flush(void);

/**
 * @brief Copy @p len bytes into code MRAM and commit them.
 *
 * @param dst  Destination inside the code-MRAM window
 * @param src  Source
 * @param len  Byte count; any length, any alignment
 * @return TIKU_RA8P1_MRAM_OK, or a negative error code
 */
int tiku_ra8p1_mram_write(void *dst, const void *src, size_t len);

/**
 * @brief Select the high-speed programming mode.
 *
 * @param on  Non-zero for high-speed, zero for the normal mode
 */
void tiku_ra8p1_mram_high_speed(int on);

#endif /* TIKU_RA8P1_MRAM_ARCH_H_ */
