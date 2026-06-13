/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - Apollo 510 memory architecture constants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MEM_ARCH_H_
#define TIKU_AMBIQ_MEM_ARCH_H_

#include <stdint.h>

/**
 * @brief Native word alignment for the Cortex-M55.
 *
 * 4-byte alignment avoids unaligned access penalties on ARMv8.1-M and
 * satisfies the ABI requirement for data placed in DTCM or MRAM.
 */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
/**
 * @brief Platform memory size type.
 *
 * 32-bit unsigned integer matching the Cortex-M55 native word width.
 * Used throughout the memory subsystem for buffer sizes, offsets,
 * and allocation counts.
 */
typedef uint32_t tiku_mem_arch_size_t;
#endif

/**
 * @brief Initialize the Apollo510 memory subsystem.
 *
 * Sets up the NVM backing region and registers the platform memory
 * map with the region registry. Called once at early boot before
 * any arena or pool is created.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Overwrite a buffer with zeros using a volatile store loop.
 *
 * The volatile pointer prevents the compiler from optimizing out the
 * zeroing operation — a known hazard when clearing sensitive data
 * (keys, credentials) that is never read after zeroing.
 *
 * @param buf  Buffer to wipe.
 * @param len  Number of bytes to zero.
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from NVM (MRAM) into an SRAM destination.
 *
 * On Apollo510 MRAM reads are bus-accessible and do not require a
 * special HAL; this is a plain memcpy wrapper that may gain
 * cache-coherence handling in a future pass.
 *
 * @param dst  Destination SRAM buffer.
 * @param src  Source NVM (MRAM) address.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from SRAM into NVM (MRAM).
 *
 * Writes are word-aligned on MRAM; the implementation pads
 * sub-word writes internally. The MPU NVM window must already be
 * unlocked by the caller before invoking this function.
 *
 * @param dst  Destination NVM (MRAM) address.
 * @param src  Source SRAM buffer.
 * @param len  Number of bytes to write.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

/**
 * @brief Flush in-RAM NVM modifications to MRAM.
 *
 * At this milestone persistent state lives in the SRAM .uninit region
 * (warm-reset durable). A later pass mirrors it to an MRAM page via
 * am_hal_mram for full power-cycle durability; for now this is a no-op.
 */
void tiku_mem_arch_nvm_flush(void);

#endif /* TIKU_AMBIQ_MEM_ARCH_H_ */
