/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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

void tiku_mem_arch_init(void)
{
    /* Nothing to set up on MSP430. */
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_secure_wipe                                                 */
/*---------------------------------------------------------------------------*/

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

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
