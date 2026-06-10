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

/** Cortex-M55 32-bit native word — 4-byte alignment avoids unaligned
 *  access penalties. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

void tiku_mem_arch_init(void);
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);
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
