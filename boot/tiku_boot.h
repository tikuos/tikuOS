/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot.h - the kernel boot sequence.
 *
 * tiku_cpu_full_init() brings up the CPU, memory, peripherals and the
 * scheduler's services, in that order, and records the stage it is in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOOT_H_
#define TIKU_BOOT_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_BOOT_SUCCESS
 * @brief Return value of a boot stage, or of the sequence, that completed
 */
#define TIKU_BOOT_SUCCESS    0

/**
 * @def TIKU_BOOT_ERROR
 * @brief Failure code for a boot stage; no stage in tiku_boot.c returns it
 */
#define TIKU_BOOT_ERROR     -1

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @enum tiku_boot_stage_e
 * @brief Boot stages, in the order tiku_cpu_full_init() runs them
 */
typedef enum {
    TIKU_BOOT_STAGE_INIT = 0,      /**< Initial boot stage */
    TIKU_BOOT_STAGE_CPU,           /**< CPU initialization */
    TIKU_BOOT_STAGE_MEMORY,        /**< Memory initialization */
    TIKU_BOOT_STAGE_PERIPHERALS,   /**< Peripheral initialization */
    TIKU_BOOT_STAGE_SERVICES,      /**< Scheduler, processes, timers */
    TIKU_BOOT_STAGE_COMPLETE       /**< Boot complete */
} tiku_boot_stage_e;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/


/**
 * @brief Run the boot sequence: CPU, memory, peripherals, then services.
 *
 * Ambiq, RP2350, Nordic and ESP32-C61 builds return with interrupts
 * unmasked; on the other ports tiku_sched_loop() unmasks them.
 *
 * @param cpu_freq Target CPU frequency in MHz
 * @return TIKU_BOOT_SUCCESS, the only value any stage returns
 */
int tiku_cpu_full_init(unsigned int cpu_freq);

/**
 * @brief Stage the boot sequence has reached; /sys/boot reports it.
 * @return The stage, TIKU_BOOT_STAGE_COMPLETE once the sequence has run
 */
tiku_boot_stage_e tiku_boot_get_stage(void);

/**
 * @brief Whether tiku_cpu_full_init() has run every stage.
 * @return 1 once it has, 0 before
 */
int tiku_boot_is_complete(void);

#endif /* TIKU_BOOT_H_ */
