/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_axon_platform.c - TikuOS platform layer for the Axon NPU (nRF54LM20B).
 *
 * Implements the nrf_axon_platform_* functions the vendor driver core calls,
 * bare-metal: reservations are a power refcount, driver events run directly
 * in the caller, and the IRQ forwards to the vendor handler.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_uart_arch.h>

/* Nordic Axon public interface headers (from the temp/axon-models checkout,
 * include path supplied by the Makefile). */
#include "drivers/axon/nrf_axon_driver.h"
#include "drivers/axon/nrf_axon_platform_interface.h"

/*---------------------------------------------------------------------------*/
/* Model working buffers (declared extern in axon/nrf_axon_platform.h)      */
/*---------------------------------------------------------------------------*/

/* Activation buffers the compiled models stream through; the Makefile sets
 * their sizes.  They sit in RAM2 (.ram2), ahead of the SRAM tier, which
 * takes what they leave of the bank. */
#if (NRF_AXON_INTERLAYER_BUFFER_SIZE) > 0
__attribute__((section(".ram2"), aligned(8)))
uint32_t nrf_axon_interlayer_buffer[NRF_AXON_INTERLAYER_BUFFER_SIZE
                                    / sizeof(uint32_t)];
#endif
#if (NRF_AXON_PSUM_BUFFER_SIZE) > 0
__attribute__((section(".ram2"), aligned(8)))
uint32_t nrf_axon_psum_buffer[NRF_AXON_PSUM_BUFFER_SIZE / sizeof(uint32_t)];
#endif

/*---------------------------------------------------------------------------*/
/* Hardware enable/disable (the documented AXONS wrapper)                    */
/*---------------------------------------------------------------------------*/

#define TIKU_AXON_BASE   ((void *)0x50056000UL)

/** @brief Enable the AXONS wrapper, hold constant latency, keep RRAM in
 *         standby. */
static void tiku_axon_hw_enable(void)
{
    NRF_AXONS_S->ENABLE |= (AXONS_ENABLE_EN_Enabled << AXONS_ENABLE_EN_Pos);
    __asm__ volatile ("dsb 0xF" ::: "memory");

    /* Constant latency while the NPU is up: Axon DMA streams from RRAM. */
    NRF_POWER_S->TASKS_CONSTLAT = 1u;

    /* Enabling Axon clears bit 0x20 of RRAMC POWER.LOWPOWERCONFIG; setting
     * it again keeps RRAM in standby instead of powered off. */
#if defined(RRAMC_POWER_LOWPOWERCONFIG_MODE_Pos)
    NRF_RRAMC_S->POWER.LOWPOWERCONFIG |= 0x20u;
#endif
}

/** @brief Disable the AXONS wrapper and return to low-power mode. */
static void tiku_axon_hw_disable(void)
{
    NRF_AXONS_S->ENABLE &= ~(AXONS_ENABLE_EN_Enabled << AXONS_ENABLE_EN_Pos);
    __asm__ volatile ("dsb 0xF" ::: "memory");
    NRF_POWER_S->TASKS_LOWPWR = 1u;
}

/*---------------------------------------------------------------------------*/
/* Power refcount + reservations (single-context bare-metal)                 */
/*---------------------------------------------------------------------------*/

static uint8_t tiku_axon_power_refs;

/** @brief Take a power reference; the first one powers the engine up. */
static void tiku_axon_power_request(void)
{
    if (tiku_axon_power_refs++ == 0u) {
        tiku_axon_hw_enable();
        (void)nrf_axon_driver_power_on();
    }
}

/** @brief Drop a power reference; the last one powers the engine down. */
static void tiku_axon_power_release(void)
{
    if (tiku_axon_power_refs != 0u && --tiku_axon_power_refs == 0u) {
        (void)nrf_axon_driver_power_off();
        tiku_axon_hw_disable();
    }
}

bool nrf_axon_platform_reserve_for_driver(void)
{
    tiku_axon_power_request();
    return true;                    /* one context: never contended */
}

bool nrf_axon_platform_reserve_for_user(void)
{
    tiku_axon_power_request();
    return true;
}

void nrf_axon_platform_free_reservation_from_user(void)
{
    tiku_axon_power_release();
    if (nrf_axon_queue_not_empty()) {
        /* Contract (platform_interface.h): kick queued async jobs; the
         * driver carries its own pending power vote. */
        nrf_axon_start_queue_processing();
    }
}

void nrf_axon_platform_free_reservation_from_driver(void)
{
    tiku_axon_power_release();
}

/*---------------------------------------------------------------------------*/
/* Events                                                                    */
/*---------------------------------------------------------------------------*/

static volatile uint8_t tiku_axon_user_event;

/*
 * Unbounded, and does not kick the watchdog: this loop cannot tell a slow
 * inference from an engine that never raises its event.  A stuck engine is
 * left to the hang detector, which resets the chip and names the culprit in
 * /sys/boot/hang.
 */
void nrf_axon_platform_wait_for_user_event(void)
{
    while (!tiku_axon_user_event) {
        __asm__ volatile ("wfe");
    }
    tiku_axon_user_event = 0u;
}

void nrf_axon_platform_generate_user_event(void)
{
    tiku_axon_user_event = 1u;
    __asm__ volatile ("sev");
}

void nrf_axon_platform_generate_driver_event(void)
{
    /* The interface's bare-metal option: process the event directly.  The
     * call comes from the Axon ISR tail, so this runs in handler context,
     * which the driver treats as its "driver thread". */
    (void)nrf_axon_process_driver_event();
}

/*---------------------------------------------------------------------------*/
/* Interrupt plumbing                                                        */
/*---------------------------------------------------------------------------*/

uint32_t nrf_axon_platform_disable_interrupts(void)
{
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    return primask;
}

void nrf_axon_platform_restore_interrupts(uint32_t restore_value)
{
    if ((restore_value & 1u) == 0u) {
        __asm__ volatile ("cpsie i" ::: "memory");
    }
}

/** @brief Strong override of the crt's weak IRQ-86 slot. */
void tiku_nordic_axons_isr(void)
{
    (void)nrf_axon_handle_interrupt();
}

/*---------------------------------------------------------------------------*/
/* Console / time services                                                   */
/*---------------------------------------------------------------------------*/

void nrf_axon_platform_printf(const char *fmt, ...)
{
    char buf[192];
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    tiku_uart_puts(buf);
}

void nrf_axon_platform_log(char *msg)
{
    tiku_uart_puts(msg);
    tiku_uart_puts("\n");
}

uint32_t nrf_axon_platform_get_clk_hz(void)
{
    return 1000000u;                /* GRTC SYSCOUNTER: 1 MHz, always on */
}

uint32_t nrf_axon_platform_get_ticks(void)
{
    return NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
}

void delay_us(uint32_t delay)
{
    tiku_cpu_nordic_delay_us(delay);
}

void nrf_axon_platform_set_profiling_gpio(void)   { }
void nrf_axon_platform_clear_profiling_gpio(void) { }

/*---------------------------------------------------------------------------*/
/* Lifecycle (mirrors the vendor platform's init/close)                      */
/*---------------------------------------------------------------------------*/

nrf_axon_result_e nrf_axon_platform_init(void)
{
    nrf_axon_result_e result;

    tiku_axon_hw_enable();
    result = nrf_axon_driver_init(TIKU_AXON_BASE);
    if (result == NRF_AXON_RESULT_SUCCESS) {
        tiku_nordic_nvic_enable(86);            /* AXONS_IRQn */
    }
    tiku_axon_hw_disable();
    return result;
}

/**
 * @brief End an Axon session: zero the power refcount and switch the engine
 *        off.
 *
 * The refcount is set to 0, not decremented: an inference can return with it
 * higher than it started, and a count left above zero after the engine is
 * off makes the next power request skip the enable.
 */
void nrf_axon_platform_close(void)
{
    tiku_axon_power_refs = 0u;
    tiku_axon_hw_disable();
}

#endif /* TIKU_AXON_ENABLE */
