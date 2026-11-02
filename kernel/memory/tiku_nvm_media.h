/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media.h - the medium under a port's NVM, as one descriptor.
 *
 * A port states four facts once, in arch/<port>/tiku_nvm_media_arch.h: the
 * program unit, what a power cut leaves in it, the programs it is rated for
 * and the erase unit.  Store geometry and wear rules derive from them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_MEDIA_H_
#define TIKU_NVM_MEDIA_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* WHAT A CUT LEAVES                                                         */
/*---------------------------------------------------------------------------*/

/* What a power cut during a program leaves in the unit being written. */
#define TIKU_NVM_TEAR_NONE  0u   /**< the unit lands whole or stays as it was   */
#define TIKU_NVM_TEAR_BYTE  1u   /**< a prefix of the bytes written has landed  */
#define TIKU_NVM_TEAR_LINE  2u   /**< every byte of the unit may read as anything */

/*---------------------------------------------------------------------------*/
/* THE PORT'S FACTS                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Each port's header defines TIKU_NVM_MEDIA_UNIT (bytes, a power of two),
 * TIKU_NVM_MEDIA_TEAR, TIKU_NVM_MEDIA_ENDURANCE (programs one unit is rated
 * for; 0 when the medium states no limit) and TIKU_NVM_MEDIA_ERASE (bytes
 * erased as one, 0 where a unit is rewritten in place).
 */
#if defined(PLATFORM_NORDIC)
#include "arch/nordic/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_ESP32C5)
#include "arch/esp32c5/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_ESP32P4)
#include "arch/esp32p4/tiku_nvm_media_arch.h"
#elif defined(PLATFORM_ESP32C6)
#include "arch/esp32c6/tiku_nvm_media_arch.h"
#elif defined(__arm__) || defined(__riscv) || defined(__MSP430__)
#error "tiku_nvm_media: this port has no arch/<port>/tiku_nvm_media_arch.h"
#else
/* A host build: a 4-byte word that tears byte by byte, with no limit. */
#define TIKU_NVM_MEDIA_HOST       1
#define TIKU_NVM_MEDIA_UNIT       4u
#define TIKU_NVM_MEDIA_TEAR       TIKU_NVM_TEAR_BYTE
#define TIKU_NVM_MEDIA_ENDURANCE  0ul
#define TIKU_NVM_MEDIA_ERASE      0u
#endif

#if (TIKU_NVM_MEDIA_UNIT < 2u) || \
    ((TIKU_NVM_MEDIA_UNIT & (TIKU_NVM_MEDIA_UNIT - 1u)) != 0u)
#error "tiku_nvm_media: the program unit is a power of two of at least 2"
#endif
#if TIKU_NVM_MEDIA_TEAR > TIKU_NVM_TEAR_LINE
#error "tiku_nvm_media: the tear is one of TIKU_NVM_TEAR_NONE, _BYTE, _LINE"
#endif
#if (TIKU_NVM_MEDIA_ERASE % TIKU_NVM_MEDIA_UNIT) != 0u
#error "tiku_nvm_media: the erase unit is a multiple of the program unit"
#endif

/*---------------------------------------------------------------------------*/
/* WHAT DERIVES FROM THEM                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Non-zero where a unit is rewritten in place, with no erase. */
#define TIKU_NVM_MEDIA_IN_PLACE  (TIKU_NVM_MEDIA_ERASE == 0u)

/** @brief Writes to one place at which the wear check warns: half the
 *         rating, or 0 where the medium states no limit. */
#define TIKU_NVM_WEAR_THRESHOLD  (TIKU_NVM_MEDIA_ENDURANCE / 2ul)

/** @brief Initialiser of a tiku_nvm_media_t holding this port's facts. */
#define TIKU_NVM_MEDIA_INIT \
    { TIKU_NVM_MEDIA_UNIT, (uint8_t)TIKU_NVM_MEDIA_TEAR, 0u, \
      TIKU_NVM_MEDIA_ENDURANCE, TIKU_NVM_MEDIA_ERASE }

/** @brief One of the TIKU_NVM_TEAR_* values. */
typedef uint8_t tiku_nvm_tear_t;

/**
 * @brief The medium under one NVM extent.
 *
 * `unit` is the program granule: a 16-byte line on RRAM and MRAM, the
 * architecture word on FRAM, a page on flash, where `erase_unit` is the
 * sector erased before a rewrite.
 */
typedef struct {
    uint16_t unit;        /**< program granule in bytes, a power of two        */
    uint8_t  tear;        /**< tiku_nvm_tear_t                                 */
    uint8_t  reserved;
    uint32_t endurance;   /**< programs one unit is rated for; 0: no limit     */
    uint32_t erase_unit;  /**< bytes erased as one; 0: rewritten in place      */
} tiku_nvm_media_t;

/** @brief This port's descriptor, the facts of TIKU_NVM_MEDIA_INIT. */
const tiku_nvm_media_t *tiku_nvm_media(void);

#endif /* TIKU_NVM_MEDIA_H_ */
