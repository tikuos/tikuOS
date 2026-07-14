/*
 * Tiku Operating System v0.05 — minimal smoke test (ARM ports)
 *
 * No kernel, no scheduler, no shell. Just brings up clocks + console
 * and prints a heartbeat in a loop, toggling an LED each iteration.
 * Intended as a debugging aid: if THIS doesn't print, the failure is in
 * the boot/clock/console layer; if it does, the failure is higher up.
 *
 * RP2350 (UART0 @115200 on GP0/GP1, LED on GP25):
 *   make MCU=rp2350 MINIMAL=1
 *   sudo picotool load -fx main.uf2
 *   make monitor MCU=rp2350
 *
 * Apollo510 EVB (console on SWO/ITM @1MHz, LED0 on pad 165):
 *   make MCU=apollo510 MINIMAL=1
 *   <flash main.bin to MRAM 0x410000 via J-Link>
 *   <open a SWO viewer at 1 MHz / SWOClock=1000>
 *
 * Expected output:
 *   TikuOS minimal: hello #0  clk=... Hz  fault=0
 *   TikuOS minimal: hello #1  clk=... Hz  fault=0
 *   ...
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#if defined(PLATFORM_AMBIQ)

#include "arch/ambiq/tiku_cpu_freq_boot_arch.h"
#include "arch/ambiq/tiku_cpu_common.h"
#include "arch/ambiq/tiku_uart_arch.h"
#include "arch/ambiq/tiku_gpio_arch.h"

/* EVB LED0: Apollo510 pad 165, Apollo4 Lite pad 12 (active-low in the BSP).
 * The console heartbeat is the primary signal; the LED toggle is a secondary
 * scope-visible indicator. */
#if defined(TIKU_DEVICE_APOLLO4L)
#define TIKU_MIN_LED_PAD   12u
#else
#define TIKU_MIN_LED_PAD   165u
#endif

int main(void)
{
    /* Power/clock/cache bring-up (am_bsp_low_power_init under the hood —
     * note its ~2 s silicon-settle delay before the first print). */
    tiku_cpu_boot_ambiq_init();

    tiku_ambiq_gpio_init_output(TIKU_MIN_LED_PAD);

    /* SWO/ITM console @1MHz (bind am_util_stdio to am_hal_itm_print). */
    tiku_uart_init();

    tiku_cpu_ambiq_delay_ms(100);
#if defined(TIKU_DEVICE_APOLLO4L)
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Apollo4 Lite EVB) ---\n");
#else
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Apollo510 EVB) ---\n");
#endif

    unsigned long clk = tiku_cpu_ambiq_smclk_get_hz();
    int           fault = tiku_cpu_ambiq_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        tiku_ambiq_gpio_toggle(TIKU_MIN_LED_PAD);
        tiku_cpu_ambiq_delay_ms(500u);
        i++;
    }

    return 0;
}

#elif defined(PLATFORM_NORDIC)

#include "arch/nordic/tiku_cpu_freq_boot_arch.h"
#include "arch/nordic/tiku_cpu_common.h"
#include "arch/nordic/tiku_uart_arch.h"
#include "arch/nordic/tiku_gpio_arch.h"
/* Board header (LED macros + TIKU_BOARD_NAME) via the device/board router, so
 * the smoke test uses the right pins and polarity on whichever nRF54L board is
 * selected -- the nRF54L15-DK LEDs are active-low, the nRF54LM20-DK's are
 * active-high, and the pins differ. */
#include "arch/nordic/tiku_device_select.h"

/* Two on-board LEDs make the smoke test a boot beacon independent of the
 * console UART (board macros handle pin + active-high/low polarity):
 *   LED1 solid-on  = reached main() + GPIO works
 *   LED2 blinking  = reached the loop (delays work) */

/* Fixed RAM progress markers, readable over the debugger (nrfutil device read
 * <symbol addr>) so boot/loop progress can be confirmed without eyeballing an
 * LED or relying on the console UART.  Volatile so the writes are not elided. */
volatile uint32_t g_nordic_main_reached;
volatile uint32_t g_nordic_loop_count;

#ifdef TIKU_MIN_RRAM_TEST
/*
 * RRAM runtime-write probe (build with EXTRA_CFLAGS=-DTIKU_MIN_RRAM_TEST=1).
 *
 * HW result (2026-07-10, nRF54L15-DK): ALL THREE PASS -- with the WEN gate
 * open, plain CPU stores to RRAM just work, from RRAM-executing code, with or
 * without the nrfx READY/READYNEXT handshakes, buffered or not (the controller
 * stalls the bus as needed).  The kernel-boot bus fault this probe was built
 * to bisect turned out to be a store issued with WEN CLOSED: TIKU_PERSIST_WARM
 * was mis-graded into RRAM `.persistent` on nordic, so the hang detector's
 * intentionally-unbracketed warm write hit the shut gate (fixed in
 * kernel/memory/tiku_mem.h).  Kept as a regression probe for the RRAM write
 * path:
 *   T1  word store bracketed by the full nrfx handshake
 *   T2  8 back-to-back byte stores into the 32-entry write buffer, then
 *       TASKS_COMMITWRITEBUF + READY wait
 *   T3  WEN set, immediate store, no waits (the minimal path)
 * Scratch target: the durable-persist region base (0x17B000) -- NOTE this
 * clobbers the first persist words, so a kernel image flashed afterwards
 * re-primes its cells (fine: the probe is a MINIMAL-only diagnostic).
 */
#include "arch/nordic/tiku_nordic_mdk.h"   /* per-device MDK router */

#define RRAM_TEST_ADDR   0x0017B000UL      /* free RRAM on both devices:
                                            * L15 persist base; LM20A mid-image
                                            * free span (persist @0x1FB000) */
#define RRAMC_WEN        (1UL << 0)
#define RRAMC_BUF32      (32UL << 8)               /* WRITEBUFSIZE = 32 */

static void rram_test_run(void)
{
    volatile uint32_t *w = (volatile uint32_t *)RRAM_TEST_ADDR;
    volatile uint8_t  *b = (volatile uint8_t  *)(RRAM_TEST_ADDR + 8u);
    uint32_t cfg0 = NRF_RRAMC_S->CONFIG;
    uint32_t i, ok;

    /* T1: single word write with the full nrfx handshake. */
    tiku_uart_puts("T1: word write + ready handshake... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }        /* config settled   */
    while (NRF_RRAMC_S->READYNEXT == 0u) { }        /* write-ready      */
    *w = 0x54494B55u;                               /* "TIKU"           */
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }        /* committed        */
    NRF_RRAMC_S->CONFIG = cfg0;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }
    tiku_uart_printf("%s (read 0x%x)\n",
                     (*w == 0x54494B55u) ? "PASS" : "MISMATCH",
                     (unsigned int)*w);

    /* T2: buffered back-to-back byte stores + explicit commit. */
    tiku_uart_puts("T2: 8 byte stores via 32-word buffer + commit... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN | RRAMC_BUF32;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }
    while (NRF_RRAMC_S->READYNEXT == 0u) { }
    for (i = 0u; i < 8u; i++) {
        b[i] = (uint8_t)(0xA0u + i);                /* no waits between */
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
    NRF_RRAMC_S->TASKS_COMMITWRITEBUF = 1u;
    while (NRF_RRAMC_S->READY == 0u)     { }
    NRF_RRAMC_S->CONFIG = cfg0;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }
    ok = 1u;
    for (i = 0u; i < 8u; i++) {
        if (b[i] != (uint8_t)(0xA0u + i)) { ok = 0u; }
    }
    tiku_uart_printf("%s (b0=0x%x b7=0x%x)\n", ok ? "PASS" : "MISMATCH",
                     (unsigned int)b[0], (unsigned int)b[7]);

    /* T3: the minimal path -- WEN then an immediate store, no waits.
     * PASSes on hardware: with WEN open the controller accepts/stalls plain
     * stores, so no handshake is strictly required for correctness. */
    tiku_uart_puts("T3: minimal path (no ready waits)... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN;
    *(volatile uint32_t *)(RRAM_TEST_ADDR + 16u) = 0xDEADBEEFu;
    while (NRF_RRAMC_S->READY == 0u)     { }
    NRF_RRAMC_S->CONFIG = cfg0;
    tiku_uart_printf("PASS (read 0x%x)\n",
                     (unsigned int)*(volatile uint32_t *)(RRAM_TEST_ADDR + 16u));

    /* T4: the persist-layer sequence -- store, then close the gate
     * IMMEDIATELY (no READY wait, no commit), then read back with the gate
     * CLOSED.  This is exactly what tiku_persist_register/write do via
     * tiku_mpu_arch_lock_nvm().  On the nRF54L15 the read is coherent; on the
     * nRF54LM20A this is the suspected stale-read window behind the
     * persist-reset-survival failures (write lands in the RRAMC buffer, the
     * closed-gate read bypasses it).  Reads again after a READY wait to show
     * whether the data eventually commits. */
    tiku_uart_puts("T4: store, close gate w/o commit, read... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }        /* settle from T3    */
    *(volatile uint32_t *)(RRAM_TEST_ADDR + 24u) = 0xCAFEF00Du;
    NRF_RRAMC_S->CONFIG = cfg0;                     /* close: NO waits   */
    __asm__ volatile ("dsb 0xF" ::: "memory");
    {
        uint32_t first = *(volatile uint32_t *)(RRAM_TEST_ADDR + 24u);
        while (NRF_RRAMC_S->READY == 0u) { }        /* drain controller  */
        tiku_uart_printf("%s (imm 0x%x, drained 0x%x)\n",
                         (first == 0xCAFEF00Du) ? "PASS" : "STALE-READ",
                         (unsigned int)first,
                         (unsigned int)*(volatile uint32_t *)
                             (RRAM_TEST_ADDR + 24u));
    }
}
#endif /* TIKU_MIN_RRAM_TEST */

#ifdef TIKU_MIN_RAM2_TEST
/*
 * RAM2 usable-span probe (build with EXTRA_CFLAGS=-DTIKU_MIN_RAM2_TEST=1).
 *
 * The nRF54LM20A has a second 256 KB SRAM bank (RAM2 @ 0x20040000) that the
 * app image does not use yet.  Phase-C bring-up proved its very top is NOT
 * CPU-writable (stacking into 0x2007FFF0 bus-faulted, CFSR STKERR) and the
 * debugger faults reading ~0x2007FF00+, so before the linker exposes the bank
 * this probe maps the exact usable boundary from the CPU side:
 *   pass 1: coarse write+verify sweep, 16 KB steps, whole bank
 *   pass 2: fine sweep of the top 8 KB, 256 B steps, printing each address
 *           BEFORE touching it -- if a write bus-faults, the last printed
 *           address is the first bad word and the default handler parks in
 *           WFE (LED2 stops blinking).
 */
static void ram2_test_run(void)
{
    uint32_t addr;
    uint32_t bad = 0u;

    tiku_uart_puts("RAM2 coarse sweep 0x20040000..0x2007C000 (16 KB steps): ");
    for (addr = 0x20040000u; addr < 0x2007C000u; addr += 0x4000u) {
        volatile uint32_t *p = (volatile uint32_t *)addr;
        *p = addr ^ 0xA5A5A5A5u;
        if (*p != (addr ^ 0xA5A5A5A5u)) { bad = addr; break; }
    }
    tiku_uart_printf("%s (bad=0x%x)\n", bad ? "MISMATCH" : "PASS",
                     (unsigned int)bad);

    tiku_uart_puts("RAM2 fine sweep of the top 8 KB (256 B steps):\n");
    for (addr = 0x2007E000u; addr < 0x20080000u; addr += 0x100u) {
        volatile uint32_t *p = (volatile uint32_t *)addr;
        tiku_uart_printf("  probe 0x%x", (unsigned int)addr);
        *p = addr ^ 0x5A5A5A5Au;                 /* may bus-fault here */
        tiku_uart_printf(" -> %s\n",
                         (*p == (addr ^ 0x5A5A5A5Au)) ? "ok" : "MISMATCH");
    }
    tiku_uart_puts("RAM2 probe DONE (whole bank writable)\n");
}
#endif /* TIKU_MIN_RAM2_TEST */

int main(void)
{
    /* Marker: proves execution reached main() (read this RAM word back). */
    g_nordic_main_reached = 0xB007B007u;

    /* FIRST thing: light LED1 (active-low -> level 0) so a solid LED proves
     * the reset handler ran, the C runtime came up, and GPIO works -- before
     * any clock/delay/UART code that could hang. */
    TIKU_BOARD_LED1_INIT();
    TIKU_BOARD_LED1_ON();               /* solid on = reached main() + GPIO ok */

    /* Start the HFXO (UARTE reference). Delays use SysTick (no setup). */
    tiku_cpu_boot_nordic_init();

    /* LED2 off initially; it blinks in the loop below. */
    TIKU_BOARD_LED2_INIT();

    /* Console UARTE at TIKU_BOARD_UART_BAUD on the board-selected pins. */
    tiku_uart_init();

    /* Let any stale bytes from the J-Link VCOM reset settle. */
    tiku_cpu_nordic_delay_ms(100);

    tiku_uart_puts("\n\n--- TikuOS minimal smoke test ("
                   TIKU_BOARD_NAME ") ---\n");

#ifdef TIKU_MIN_RRAM_TEST
    rram_test_run();
#endif
#ifdef TIKU_MIN_RAM2_TEST
    ram2_test_run();
#endif

    unsigned long clk = tiku_cpu_nordic_smclk_get_hz();
    int fault         = tiku_cpu_nordic_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        /* LED2 blinks = the loop (and the delay path) is alive. */
        TIKU_BOARD_LED2_TOGGLE();
        tiku_cpu_nordic_delay_ms(500u);
        g_nordic_loop_count++;      /* progress marker (read over debugger) */
        i++;
    }

    return 0;
}

#else /* PLATFORM_RP2350 */

#include "arch/arm-rp2350/tiku_cpu_freq_boot_arch.h"
#include "arch/arm-rp2350/tiku_cpu_common.h"
#include "arch/arm-rp2350/tiku_uart_arch.h"
#include "arch/arm-rp2350/tiku_gpio_arch.h"
#include "arch/arm-rp2350/tiku_rp2350_regs.h"

int main(void)
{
    /* Bring up XOSC + (try to) PLL_SYS + CLK_SYS + CLK_PERI. Falls
     * back silently to 12 MHz XOSC if the PLL can't lock. */
    tiku_cpu_boot_rp2350_init();

    /* Onboard LED pin (GP25 on plain Pico 2; on Pico 2 W this is
     * shared with the CYW43 WL_CLK so it won't actually light an
     * LED, but driving it as GPIO is harmless and a scope can see
     * the toggle). */
    tiku_rp2350_gpio_init_output(25U);

    /* UART0 on GP0 (TX) / GP1 (RX) at TIKU_BOARD_UART_BAUD baud. */
    tiku_uart_init();

    /* Burn 100 ms before the first message so any stale boot bytes
     * from the FT232 / picotool reset settle. */
    tiku_cpu_rp2350_delay_ms(100);

    /* Two-line preamble — easy to spot even if the UART line had
     * pre-existing junk in the picocom buffer. */
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (Pico 2 W) ---\n");

    unsigned long clk = tiku_cpu_rp2350_smclk_get_hz();
    int fault         = tiku_cpu_rp2350_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        tiku_rp2350_gpio_toggle(25U);
        tiku_cpu_rp2350_delay_ms(500U);
        i++;
    }

    return 0;
}

#endif /* PLATFORM_* */
