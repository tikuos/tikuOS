/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - nRF54L busy delays (SysTick) + system reset
 *
 * Delays use SysTick as a polled one-shot down-counter clocked from the
 * processor clock.  SysTick is core-internal and runs whether or not a
 * debugger is attached -- unlike the DWT cycle counter, whose CYCCNT can be
 * frozen without an active trace clock (an early nRF54L bring-up used DWT and
 * hung the delay loop when run standalone, printing nothing over UART).  The
 * kernel tick will live on the GRTC (low-power, always-on), so SysTick stays
 * free for busy-delays in the full build too.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_cpu_common.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/mdk/nrf54l15.h>
#include <stddef.h>

/* The nRF54L15-DK boots with the PLL at 128 MHz (OSCILLATORS.PLL.CURRENTFREQ
 * reads CK128M on hardware -- the CK64M register reset value is overridden by
 * the boot configuration).  SysTick is clocked from this processor clock, so
 * the delay math must use 128 MHz or delays run 2x fast. */
#define TIKU_NORDIC_CPU_HZ   128000000UL
#define TIKU_SYSTICK_MAX     0x00FFFFFFUL   /* SysTick reload is 24-bit */

void tiku_nordic_dwt_init(void)
{
    /* Retained for API compatibility (boot bring-up calls it); SysTick-based
     * delays need no pre-initialisation, so this is intentionally a no-op. */
}

/** @brief Busy-wait for @p cycles core cycles using SysTick (polled). */
static void tiku_nordic_delay_cycles(uint32_t cycles)
{
    while (cycles != 0U) {
        uint32_t chunk = (cycles > TIKU_SYSTICK_MAX) ? TIKU_SYSTICK_MAX : cycles;

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
    tiku_nordic_delay_cycles(us * (TIKU_NORDIC_CPU_HZ / 1000000UL));
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
/* Unique ID (real FICR device ID)                                           */
/*---------------------------------------------------------------------------*/

uint8_t tiku_cpu_nordic_unique_id(uint8_t *buf, uint8_t len)
{
    uint32_t id[2];
    uint8_t  n, i;

    if (buf == NULL || len == 0u) {
        return 0u;
    }
    /* Genuine per-die 64-bit identifier from FICR (no synthesis needed). */
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
    uint32_t r = TIKU_NORDIC_RESETREAS;

    /* RESETREAS is write-1-to-clear: latch the set bits away so the NEXT boot
     * sees a clean cause (otherwise reasons accumulate across resets). */
    TIKU_NORDIC_RESETREAS = r;

    /* Map to the MSP430 SYSRSTIV-style codes the kernel already speaks
     * (matching the ambiq reset-reason decode). */
    if (r & (TIKU_RESETREAS_DOG0 | TIKU_RESETREAS_DOG1)) {
        return 0x16u;   /* watchdog timeout */
    }
    if (r & TIKU_RESETREAS_SREQ) {
        return 0x06u;   /* software reset (SCB SYSRESETREQ / reboot) */
    }
    if (r & TIKU_RESETREAS_RESETPIN) {
        return 0x14u;   /* external reset pin */
    }
    if (r & TIKU_RESETREAS_OFF) {
        return 0x02u;   /* wake from System OFF (treat as power-domain event) */
    }
    return 0x00u;       /* cold / power-on reset */
}
