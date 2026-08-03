/*
 * Tiku Operating System v0.06
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
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_boot.h"
#if defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_xspi_arch.h>
#include <arch/stm32n6/tiku_sram_arch.h>
#endif
#include <kernel/cpu/tiku_stack.h>   /* stack-paint for /sys/mem/stack_free */
#include "kernel/cpu/tiku_common.h"
#include "kernel/memory/tiku_mem.h"
#include "kernel/timers/tiku_clock.h"
#include "kernel/scheduler/tiku_sched.h"
#include "hal/tiku_cpu.h"        /* tiku_cpu_irq_enable() at boot-complete */
#if defined(PLATFORM_MSP430)
#include "arch/msp430/tiku_uart_arch.h"
#elif defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_uart_arch.h"
#if defined(TIKU_CONSOLE_USB)
#include "arch/arm-rp2350/tiku_usb_cdc_arch.h"
#endif
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_uart_arch.h"
#endif


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

    /* Paint the unused stack now, at the shallowest call depth, so
     * /sys/mem/stack_free can report worst-case headroom.  Bounded by the
     * arch stack bottom (above the MPU guard + heap); a no-op on an arch
     * that has not declared its bounds. */
    tiku_stack_paint();

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

#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC)
    /* The ARM reset handlers mask IRQs (cpsid i in tiku_crt_early.c) so no
     * ISR can fire into half-initialized kernel state.  Everything an ISR
     * touches now exists -- tiku_sched_init() just built the process queue --
     * so unmask HERE, not only at the top of tiku_sched_loop(): every build
     * that runs work before (or instead of) the scheduler -- TEST_ENABLE,
     * TIKU_TURBO_BENCH, the deep-sleep power autorun, embedded BASIC -- was
     * otherwise running with a dead tick, and every WFI in it fell straight
     * through (wake on pended IRQ, ISR never executed, time never advanced).
     * The scheduler's own tiku_cpu_irq_enable() stays: it is idempotent.
     * MSP430 is untouched -- its GIE discipline predates this and works. */
    tiku_cpu_irq_enable();
#endif

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

    /* Unlock I/O pins from LPM5 state on MSP430FR devices: all GPIO
     * is locked after reset until LOCKLPM5 is cleared. The Cortex-M
     * RP2350 has no equivalent — pads are usable immediately after
     * the bank's reset is released, which the arch boot does. */
#if defined(PLATFORM_MSP430)
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
#if defined(PLATFORM_STM32N6)
    /* External NOR first: the durable mirror is restored from it inside
     * tiku_mem_init(), so the controller has to be live before that runs. A
     * failure is not fatal -- the image runs from SRAM and the durable region
     * simply keeps its reset contents. */
    (void)tiku_xspi_init();
#endif

    /* Initialize memory subsystem (arch-specific setup + module state) */
    tiku_mem_init();

#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6)
    /* Bring up the tier allocator at boot so tier-backed allocations (per-
     * process memory, etc.) work without relying on a lazy first-touch init.
     * tiku_tier_init is idempotent, so BASIC's later lazy call is a no-op.
     *
     * Every platform with a carved NVM region does this, because since v0.06
     * the NVM tier is a DECLARED extent of fixed size (TIKU_NVM_TIER_BYTES)
     * rather than whatever was left over -- so it should exist from boot, and
     * `free` should be able to report it without something having touched
     * BASIC first.  RP2350 needed it even before that, having no BASIC in its
     * build to trigger the lazy path at all.
     * (MSP430 keeps its existing lazy init until validated there.) */
    (void)tiku_tier_init();
#endif

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

#if defined(PLATFORM_STM32N6) && defined(TIKU_N6_SRAM_PROBE)
    /* After the console exists: the probe reports as it walks, and nothing
     * owns the banks it writes to yet. */
    tiku_stm32n6_sram_probe();
#endif

#if defined(TIKU_CONSOLE_USB)
    /* Native USB CDC-ACM console (TIKU_CONSOLE=usb/both). Polled: serviced
     * whenever the scheduler is idle, and also nudged from putc/getc. */
    tiku_usb_cdc_init();
    tiku_sched_set_idle_hook(tiku_usb_cdc_poll);
#endif

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
