/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot.c - System boot and initialization implementation
 *
 * This file implements system boot sequence management and initialization
 * functions for the Tiku Operating System.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_boot.h"
#include "kernel/cpu/tiku_common.h"
#include "kernel/memory/tiku_mem.h"
#include "kernel/timers/tiku_clock.h"
#include "kernel/scheduler/tiku_sched.h"
#include "arch/msp430/tiku_uart_arch.h"


/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/* Boot sequence timeout in milliseconds */
#define TIKU_BOOT_TIMEOUT_MS    5000


/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                        */
/*---------------------------------------------------------------------------*/

/** Current boot stage */
static tiku_boot_stage_e current_boot_stage = TIKU_BOOT_STAGE_INIT;

/** Boot completion flag */
static volatile int boot_complete = 0;

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTION PROTOTYPES                                              */
/*---------------------------------------------------------------------------*/

static int tiku_boot_init_cpu(unsigned int cpu_freq);
static int tiku_boot_init_memory(void);
static int tiku_boot_init_peripherals(void);
static int tiku_boot_init_services(void);

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                         */
/*---------------------------------------------------------------------------*/

/*
 * @brief Perform complete system initialization
 */
int 
tiku_cpu_full_init(unsigned int cpu_freq)
{
    int result;
    
    /* Initialize boot system 
    * Currently only turn off watchdog
    */
   
    current_boot_stage = TIKU_BOOT_STAGE_INIT;

    boot_complete = 0;

    /* CPU initialization stage */
    current_boot_stage = TIKU_BOOT_STAGE_CPU;
    MAIN_PRINTF("Boot: CPU init\n");

    result = tiku_boot_init_cpu(cpu_freq);
    if (result != TIKU_BOOT_SUCCESS) {
        return result;
    }
    MAIN_PRINTF("Boot: CPU done\n");

    /* Memory initialization stage */
    current_boot_stage = TIKU_BOOT_STAGE_MEMORY;
    MAIN_PRINTF("Boot: Memory init\n");

    result = tiku_boot_init_memory();
    if (result != TIKU_BOOT_SUCCESS) {
        return result;
    }
    MAIN_PRINTF("Boot: Memory done\n");

    /* Peripheral initialization stage */
    current_boot_stage = TIKU_BOOT_STAGE_PERIPHERALS;
    MAIN_PRINTF("Boot: Peripherals init\n");

    result = tiku_boot_init_peripherals();
    if (result != TIKU_BOOT_SUCCESS) {
        return result;
    }
    MAIN_PRINTF("Boot: Peripherals done\n");

    /* System services initialization stage */
    current_boot_stage = TIKU_BOOT_STAGE_SERVICES;
    MAIN_PRINTF("Boot: Services init\n");

    result = tiku_boot_init_services();
    if (result != TIKU_BOOT_SUCCESS) {
        return result;
    }
    MAIN_PRINTF("Boot: Services done\n");

    /* Mark boot as complete */
    current_boot_stage = TIKU_BOOT_STAGE_COMPLETE;

    boot_complete = 1;
    MAIN_PRINTF("Boot: complete\n");

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Get current boot stage
 */
tiku_boot_stage_e 
tiku_boot_get_stage(void)
{
    return current_boot_stage;
}

/**
 * @brief Check if boot sequence is complete
 */
int 
tiku_boot_is_complete(void)
{
    return boot_complete;
}

/*---------------------------------------------------------------------------*/
/* PRIVATE FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize CPU subsystem
 * @param cpu_freq Target CPU frequency in MHz
 * @return TIKU_BOOT_SUCCESS on success, TIKU_BOOT_ERROR on failure
 */
static int
tiku_boot_init_cpu(unsigned int cpu_freq)
{
    tiku_cpu_boot_init();
    tiku_cpu_freq_init(cpu_freq);

    /* Unlock I/O pins from LPM5 state.
     * On MSP430FR devices all GPIO is locked after reset until
     * LOCKLPM5 is cleared.  Must happen before any GPIO access. */
#ifdef PLATFORM_MSP430
    PM5CTL0 &= ~LOCKLPM5;
#endif

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Initialize memory subsystem
 * @return TIKU_BOOT_SUCCESS on success, TIKU_BOOT_ERROR on failure
 */
static int
tiku_boot_init_memory(void)
{
    /* Initialize memory subsystem (arch-specific setup + module state) */
    tiku_mem_init();

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Initialize peripheral subsystem
 * @return TIKU_BOOT_SUCCESS on success, TIKU_BOOT_ERROR on failure
 */
static int
tiku_boot_init_peripherals(void)
{
    /* UART must be initialized before clock so printf is available
     * as early as possible (GPIO is already unlocked by init_cpu). */
    tiku_uart_init();

    /* System clock must be up before timers or scheduler */
    tiku_clock_init();

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Initialize system services
 * @return TIKU_BOOT_SUCCESS on success, TIKU_BOOT_ERROR on failure
 */
static int
tiku_boot_init_services(void)
{
    /* Scheduler init brings up processes, htimer, and software timers */
    tiku_sched_init();

    return TIKU_BOOT_SUCCESS;
}
