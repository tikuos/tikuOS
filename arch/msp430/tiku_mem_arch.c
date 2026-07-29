/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - MSP430 memory architecture implementation
 *
 * Implements platform-specific memory operations for the MSP430 family.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_init                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize MSP430-specific memory hardware.
 *
 * Currently a no-op; the hook exists for future FRAM wait-state, MPU or DMA
 * defaults.
 */
void tiku_mem_arch_init(void)
{
    /* Nothing to do yet on MSP430. */
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_secure_wipe                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Securely wipe a memory region with zeros.
 *
 * Writes through a volatile pointer so the compiler cannot elide the loop --
 * without it, an optimiser sees the memory is never read again and may drop the
 * whole thing.  Costs roughly 5-8 cycles per byte.
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_nvm_read                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read from FRAM into SRAM
 *
 * FRAM on MSP430 is memory-mapped, so this is a straight memcpy.
 * The abstraction exists because other NVM technologies (Flash, EEPROM)
 * may not be memory-mapped and require special read sequences.
 *
 * @param dst   SRAM destination
 * @param src   FRAM source
 * @param len   Bytes to copy
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_nvm_write                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write from SRAM into FRAM.
 *
 * FRAM is memory-mapped, so this is a straight copy.  The caller unlocks the
 * MPU, which lets several writes share one unlocked window.
 *
 * @param dst   FRAM destination
 * @param src   SRAM source
 * @param len   Bytes to copy
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
