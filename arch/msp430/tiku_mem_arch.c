/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - MSP430 memory architecture implementation
 *
 * Secure wipe and the byte-copy NVM read and write for MSP430 FRAM.
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
 * Does nothing on MSP430.
 */
void tiku_mem_arch_init(void)
{
    /* Nothing to set up on MSP430. */
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_secure_wipe                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Securely wipe a memory region with zeros.
 *
 * Writes through a volatile pointer, so the compiler keeps every store even
 * when the memory is never read again.
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
 * FRAM is memory-mapped, so this is a byte copy.
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
 * FRAM is memory-mapped and written in place, so this is a byte copy.  The
 * caller holds the MPU write window, so several writes can share one.
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
