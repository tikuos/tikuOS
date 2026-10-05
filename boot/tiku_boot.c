/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot.c - the kernel boot sequence.
 *
 * Runs the CPU, memory, peripheral and service stages in order, with the
 * per-port steps each stage needs, and records the stage reached.
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
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_flash_arch.h>
#include <arch/esp32c61/tiku_psram_arch.h>
#include <arch/esp32c61/tiku_xip_arch.h>
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
#elif defined(PLATFORM_NORDIC)
#if defined(TIKU_CONSOLE_USB)
#include "arch/nordic/tiku_usb_cdc_arch.h"
#endif
#endif


/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

/* Boot timeout in milliseconds.  No code reads it. */
#define TIKU_BOOT_TIMEOUT_MS    5000


/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                        */
/*---------------------------------------------------------------------------*/

/** Current boot stage */
static tiku_boot_stage_e current_boot_stage = TIKU_BOOT_STAGE_INIT;

/** 1 once every stage has run */
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
 * @brief Run the boot sequence: CPU, memory, peripherals, then services.
 */
int 
tiku_cpu_full_init(unsigned int cpu_freq)
{
    int result;
    
    current_boot_stage = TIKU_BOOT_STAGE_INIT;

    boot_complete = 0;

    /* At the shallowest call depth of boot, fills the stack below this frame
     * down to the arch's stack bottom, for /sys/mem/stack_free.  An arch
     * that declares no stack bottom paints nothing. */
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

    /* Applies the CPU-rate preference that tiku_mem_init() restored, before
     * the clock and peripherals start.  On Nordic the call does nothing: that
     * port picks its rate earlier, from RRAM. */
    tiku_cpu_freq_boot_apply();

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
    defined(PLATFORM_NORDIC) || defined(PLATFORM_ESP32C61)
    /* The Cortex-M reset handlers mask interrupts (cpsid i in
     * tiku_crt_early.c), so no ISR runs against half-built kernel state.
     * Everything an ISR touches exists once tiku_sched_init() has built the
     * process queue, and interrupts are unmasked here for work that runs
     * before or instead of the scheduler: TEST_ENABLE, TIKU_TURBO_BENCH, the
     * power autorun, embedded BASIC.  With interrupts masked the tick does
     * not advance, and WFI wakes on a pending IRQ without running its ISR.
     * tiku_sched_loop() unmasks again; the call is idempotent.  On MSP430
     * GIE stays clear until tiku_sched_loop() sets it. */
    tiku_cpu_irq_enable();
#endif

    boot_complete = 1;
    MAIN_PRINTF("Boot: complete\n");

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Stage the boot sequence has reached.
 */
tiku_boot_stage_e 
tiku_boot_get_stage(void)
{
    return current_boot_stage;
}

/**
 * @brief Whether tiku_cpu_full_init() has run every stage.
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
 * @brief Arch CPU boot, then the CPU clock at @p cpu_freq.
 * @param cpu_freq Target CPU frequency in MHz
 * @return TIKU_BOOT_SUCCESS
 */
static int
tiku_boot_init_cpu(unsigned int cpu_freq)
{
    tiku_cpu_boot_init();
    tiku_cpu_freq_init(cpu_freq);

    /* MSP430FR parts hold every GPIO locked after reset until LOCKLPM5 is
     * cleared.  tiku_cpu_boot_init() has cleared it already, so this second
     * clear has no effect. */
#if defined(PLATFORM_MSP430)
    PM5CTL0 &= ~LOCKLPM5;
#endif

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Durable-memory backends, tiku_mem_init(), then the tier allocator.
 * @return TIKU_BOOT_SUCCESS
 */
static int
tiku_boot_init_memory(void)
{
#if defined(PLATFORM_STM32N6)
    /* The XSPI NOR starts first: tiku_mem_init() restores the durable mirror
     * from it.  When it fails, boot continues from SRAM and the durable
     * region keeps its reset contents. */
    (void)tiku_xspi_init();
#elif defined(PLATFORM_ESP32C61)
    /* The flash starts first: tiku_mem_init() restores the durable mirror
     * from it. */
    (void)tiku_flash_init();
#endif

    /* Initialize memory subsystem (arch-specific setup + module state) */
    tiku_mem_init();

#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
    defined(PLATFORM_ESP32C61)
    /* Every port with a carved NVM region wires the tier allocator here,
     * before any consumer allocates: tiku_tier_init() runs the layout
     * service, which fixes the NVM tier's extent for this boot.  The call is
     * idempotent, so a later one (BASIC, free) does nothing.  MSP430 wires
     * its tier on the first tiku_tier_init() call. */
    (void)tiku_tier_init();
#endif

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief Console, per-port boot checks, then the system clock.
 * @return TIKU_BOOT_SUCCESS
 */
static int
tiku_boot_init_peripherals(void)
{
    /* The UART starts first, so a UART console carries the output of the
     * rest of boot.  The GPIO unlock in tiku_boot_init_cpu() has run. */
    tiku_uart_init();

#if defined(PLATFORM_STM32N6) && defined(TIKU_N6_SRAM_PROBE)
    /* The probe prints as it walks the SRAM banks, so it runs after the
     * console starts, and before anything owns the banks it writes. */
    tiku_stm32n6_sram_probe();
#endif

#if defined(PLATFORM_ESP32C61)
    /* After the console starts and before any call into the XIP image or
     * any PSRAM access.  Boot halts with a message when the kernel has code
     * in the XIP image and xip.bin is from another build, or when a build
     * that places buffers in PSRAM finds no PSRAM to hold them.  Those
     * buffers are zeroed here. */
    tiku_esp32c61_xip_require();
    tiku_esp32c61_psram_data_boot();
#endif

#if defined(TIKU_CONSOLE_USB)
    /* Native USB CDC-ACM console (TIKU_CONSOLE=usb or both).  It is polled,
     * from the scheduler's idle hook and from its own putc and getc. */
    tiku_usb_cdc_init();
    tiku_sched_set_idle_hook(tiku_usb_cdc_poll);
#endif

    /* The system clock starts before tiku_sched_init() starts the timers. */
    tiku_clock_init();

    return TIKU_BOOT_SUCCESS;
}

/**
 * @brief The scheduler, which starts processes and both timer services.
 * @return TIKU_BOOT_SUCCESS
 */
static int
tiku_boot_init_services(void)
{
    /* Starts the process subsystem, the htimer and the software timers. */
    tiku_sched_init();

    return TIKU_BOOT_SUCCESS;
}
