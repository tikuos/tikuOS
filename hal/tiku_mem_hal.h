/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_hal.h - Platform-routing header for memory management
 *
 * Routes to the correct architecture-specific memory header based on
 * the selected platform. Provides portable fallback defaults when no
 * platform is selected (e.g. host-mode testing).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MEM_HAL_H_
#define TIKU_MEM_HAL_H_

#include <stddef.h>
#include <stdint.h>

/* Reconstruction's short metadata transitions rely on kernel-only mutation.
 * Cortex-M exposes exception context independently of the worker scheduler.
 * Host tests may supply a predicate without embedding target assembly. */
#ifndef TIKU_MEM_ARCH_IN_EXCEPTION
#if defined(__arm__) || defined(__thumb__)
static inline int tiku_mem_arch_in_exception(void)
{
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r" (ipsr));
    return ipsr != 0u;
}
#define TIKU_MEM_ARCH_IN_EXCEPTION() tiku_mem_arch_in_exception()
#elif defined(PLATFORM_ESP32C61)
/* CLIC: mintstatus holds the level of the interrupt being served in its top
 * byte, and zero outside every handler. */
static inline int tiku_mem_arch_in_exception(void)
{
    uint32_t st;

    __asm__ volatile ("csrr %0, 0xFB1" : "=r" (st));
    return (st >> 24) != 0u;
}
#define TIKU_MEM_ARCH_IN_EXCEPTION() tiku_mem_arch_in_exception()
#else
#define TIKU_MEM_ARCH_IN_EXCEPTION() 0
#endif
#endif

/*---------------------------------------------------------------------------*/
/* PLATFORM ROUTING                                                          */
/*---------------------------------------------------------------------------*/

#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_mem_arch.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_mem_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_mem_arch.h"
#elif defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_mem_arch.h"
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_mem_arch.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_mem_arch.h"
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_mem_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* FALLBACK DEFAULTS (host / unknown platform)                               */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_MEM_ARCH_ALIGNMENT
/** Default alignment for 32-bit platforms (ARM Cortex-M, RISC-V, host) */
#define TIKU_MEM_ARCH_ALIGNMENT  4U
#endif

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
/** Default 32-bit size type for platforms with > 64 KB address space */
typedef uint32_t tiku_mem_arch_size_t;
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize platform-specific memory hardware
 */
void tiku_mem_arch_init(void);

/**
 * @brief Securely wipe a memory region
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Read from non-volatile memory into SRAM
 *
 * @param dst   SRAM destination buffer
 * @param src   NVM source address
 * @param len   Number of bytes to read
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Write from SRAM into non-volatile memory
 *
 * @param dst   NVM destination address
 * @param src   SRAM source buffer
 * @param len   Number of bytes to write
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

#if !defined(PLATFORM_MSP430)
/** @brief Zero for completed/no work, negative if completion was not established. */
int tiku_mem_arch_nvm_flush_status(void);
/** @brief Unchecked compatibility wrapper for the checked flush. */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief The durable image as last persisted, read-only.
 *
 * A mirror port returns its mirror's image when that checks out; an in-place
 * port returns its persist partition.  NULL, with *len 0, when there is none.
 */
const uint8_t *tiku_mem_arch_durable(size_t *len);

/**
 * @brief Where durable variables live now: .uninit on a mirror port, the
 *        persist partition's .persistent on an in-place one.
 */
uint8_t *tiku_mem_arch_durable_live(size_t *len);
#endif

#endif /* TIKU_MEM_HAL_H_ */
