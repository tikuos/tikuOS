/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - nRF54L busy delays, reset, device ID, reset reason.
 *
 * Delays poll SysTick as a one-shot down-counter at the live PLL rate;
 * SysTick is core-internal and counts with or without a debugger attached.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include <stddef.h>

/* SysTick counts processor clocks, and the core runs at 64 or 128 MHz, so
 * the delay math reads OSCILLATORS.PLL.CURRENTFREQ through
 * tiku_nordic_cpu_hz_now(). */
#define TIKU_SYSTICK_MAX     0x00FFFFFFUL   /* SysTick reload is 24-bit */
#define TIKU_PLL_CK128M      0x1UL          /* CURRENTFREQ: 128 MHz     */

unsigned long tiku_nordic_cpu_hz_now(void)
{
    return ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) == TIKU_PLL_CK128M)
               ? 128000000UL
               : 64000000UL;
}

void tiku_nordic_dwt_init(void)
{
    /* No-op: SysTick delays need no setup; boot bring-up still calls it. */
}

/** @brief Busy-wait for @p cycles core cycles using SysTick (polled). */
static void tiku_nordic_delay_cycles(uint64_t cycles)
{
    while (cycles != 0U) {
        uint32_t chunk = (cycles > TIKU_SYSTICK_MAX) ? TIKU_SYSTICK_MAX
                                                     : (uint32_t)cycles;

        TIKU_SYSTICK->CTRL = 0U;                 /* stop + clear COUNTFLAG   */
        TIKU_SYSTICK->LOAD = chunk;              /* counts chunk+1 cycles    */
        TIKU_SYSTICK->VAL  = 0U;                 /* clear current + flag     */
        TIKU_SYSTICK->CTRL = TIKU_SYSTICK_CTRL_ENABLE |
                             TIKU_SYSTICK_CTRL_CLKSOURCE;   /* proc clock    */

        while ((TIKU_SYSTICK->CTRL & TIKU_SYSTICK_CTRL_COUNTFLAG) == 0U) {
            /* wait until the counter underflows past zero */
        }
        TIKU_SYSTICK->CTRL = 0U;
        cycles -= chunk;
    }
}

void tiku_cpu_nordic_delay_us(uint32_t us)
{
    /* 64-bit: at 128 MHz a 32-bit cycle count wraps past about 33 s. */
    tiku_nordic_delay_cycles((uint64_t)us *
                             (tiku_nordic_cpu_hz_now() / 1000000UL));
}

void tiku_cpu_nordic_delay_ms(uint32_t ms)
{
    while (ms-- != 0U) {
        tiku_cpu_nordic_delay_us(1000U);
    }
}

/*---------------------------------------------------------------------------*/
/* System reset                                                              */
/*---------------------------------------------------------------------------*/

void tiku_cpu_nordic_reset(void)
{
    tiku_nordic_system_reset();   /* SCB AIRCR SYSRESETREQ; does not return */
}

/*---------------------------------------------------------------------------*/
/* Unique ID (FICR device ID)                                                */
/*---------------------------------------------------------------------------*/

uint8_t tiku_cpu_nordic_unique_id(uint8_t *buf, uint8_t len)
{
    uint32_t id[2];
    uint8_t  n, i;

    if (buf == NULL || len == 0u) {
        return 0u;
    }
    /* The per-die 64-bit device ID from FICR. */
    id[0] = NRF_FICR_NS->INFO.DEVICEID[0];
    id[1] = NRF_FICR_NS->INFO.DEVICEID[1];

    n = (len > 8u) ? 8u : len;
    for (i = 0u; i < n; i++) {
        buf[i] = (uint8_t)(id[i >> 2] >> ((i & 3u) * 8u));
    }
    return n;
}

/*---------------------------------------------------------------------------*/
/* Reset reason (RESETREAS -> MSP430-compatible code)                        */
/*---------------------------------------------------------------------------*/

#define TIKU_NORDIC_RESETREAS   (*(volatile uint32_t *)0x5010E600UL)
#define TIKU_RESETREAS_RESETPIN (1UL << 0)
#define TIKU_RESETREAS_DOG0     (1UL << 1)
#define TIKU_RESETREAS_DOG1     (1UL << 2)
#define TIKU_RESETREAS_SREQ     (1UL << 6)
#define TIKU_RESETREAS_OFF      (1UL << 8)

uint16_t tiku_cpu_nordic_reset_reason(void)
{
    static uint16_t captured;
    static uint8_t captured_valid;
    uint32_t r = TIKU_NORDIC_RESETREAS;

    if (captured_valid) {
        return captured;
    }

    /* RESETREAS is write-1-to-clear and accumulates across resets; clearing
     * the bits read here leaves the next boot only its own cause. */
    TIKU_NORDIC_RESETREAS = r;

    /* Map to the MSP430 SYSRSTIV-style codes the kernel uses. */
    if (r & (TIKU_RESETREAS_DOG0 | TIKU_RESETREAS_DOG1)) {
        captured = 0x16u;   /* watchdog timeout */
    } else if (r & TIKU_RESETREAS_SREQ) {
        captured = 0x06u;   /* software reset (SCB SYSRESETREQ / reboot) */
    } else if (r & TIKU_RESETREAS_RESETPIN) {
        captured = 0x14u;   /* external reset pin */
    } else if (r & TIKU_RESETREAS_OFF) {
        captured = 0x02u;   /* wake from System OFF */
    } else {
        captured = 0x00u;   /* cold / power-on reset */
    }
    captured_valid = 1u;
    return captured;
}
