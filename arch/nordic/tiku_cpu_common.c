/*
 * Tiku Operating System v0.06
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
#include <arch/nordic/tiku_nordic_mdk.h>
#include <stddef.h>

/* SysTick is clocked from the processor clock, whose speed is NOT fixed: the
 * PLL reset default is 64 MHz, the boot bring-up requests 128 MHz, and an
 * attached debug session can change what a given boot lands on (Phase-0
 * measured CK128M only because nrfutil was attached; standalone boots came up
 * at 64 MHz and every delay ran 2x slow -- the watchdog C-unit test caught it
 * biting through kicks that were supposed to land at half its timeout).  So
 * the delay math reads OSCILLATORS.PLL.CURRENTFREQ live instead of trusting
 * a constant. */
#define TIKU_SYSTICK_MAX     0x00FFFFFFUL   /* SysTick reload is 24-bit */
#define TIKU_PLL_CK128M      0x1UL          /* CURRENTFREQ: 128 MHz     */

/** @brief Current core clock in Hz, from the live PLL state (64 or 128 MHz). */
static uint32_t tiku_nordic_cpu_hz_now(void)
{
    return ((NRF_OSCILLATORS_S->PLL.CURRENTFREQ & 0x3UL) == TIKU_PLL_CK128M)
               ? 128000000UL
               : 64000000UL;
}

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
    tiku_nordic_delay_cycles(us * (tiku_nordic_cpu_hz_now() / 1000000UL));
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
    static uint16_t captured;
    static uint8_t captured_valid;
    uint32_t r = TIKU_NORDIC_RESETREAS;

    if (captured_valid) {
        return captured;
    }

    /* RESETREAS is write-1-to-clear: latch the set bits away so the NEXT boot
     * sees a clean cause (otherwise reasons accumulate across resets). */
    TIKU_NORDIC_RESETREAS = r;

    /* Map to the MSP430 SYSRSTIV-style codes the kernel already speaks
     * (matching the ambiq reset-reason decode). */
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
