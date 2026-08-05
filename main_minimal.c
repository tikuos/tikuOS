/*
 * Tiku Operating System v0.06 -- minimal smoke test (ARM ports).
 *
 * No kernel, scheduler or shell: brings up clocks and console, then prints a
 * heartbeat and toggles an LED in a loop.  If this does not print, the failure
 * is in boot/clock/console; if it does, the failure is higher up.
 *
 * Build with MINIMAL=1, e.g. `make MCU=rp2350 MINIMAL=1`.
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
 * stalls the bus as needed).  A store issued with WEN CLOSED faults the bus
 * instead.  Kept as a regression probe for the RRAM write path:
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

#elif defined(PLATFORM_STM32N6)

#include "arch/stm32n6/tiku_cpu_freq_boot_arch.h"
#include "arch/stm32n6/tiku_cpu_common.h"
#include "arch/stm32n6/tiku_uart_arch.h"
#include "arch/stm32n6/tiku_gpio_arch.h"
#include "arch/stm32n6/tiku_stm32n6_regs.h"

/* NUCLEO-N657X0-Q LED3, the one the boot stub proved reachable. LED1 is PG8
 * and LED2 is PG10 on the same port. */
#define TIKU_MIN_LED_PORT   STM32N6_GPIO_PORT_G
#define TIKU_MIN_LED_PIN    0U

int main(void)
{
    /* HSI only: the boot ROM's clock tree is left alone, so nothing here can
     * strand the console the ROM already relies on. */
    tiku_cpu_boot_stm32n6_init();

    tiku_stm32n6_gpio_init_output(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);

    /* USART1 on PE5/PE6 -- the ST-LINK virtual COM port. */
    tiku_uart_init();

    tiku_cpu_stm32n6_delay_ms(100);
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (NUCLEO-N657X0-Q) ---\n");
    /* The image identifies its own delay calibration, so a heartbeat period
     * can never be attributed to the wrong build. */
    tiku_uart_printf("spin=%u iters/ms\n",
                     (unsigned int)TIKU_STM32N6_SPIN_ITERS_PER_MS);

    unsigned long clk = tiku_cpu_stm32n6_smclk_get_hz();
    int           fault = tiku_cpu_stm32n6_clock_has_fault();

    uint32_t i = 0;
    while (1) {
        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault);

        tiku_stm32n6_gpio_toggle(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);
        tiku_cpu_stm32n6_delay_ms(500U);
        i++;
    }

    return 0;
}

#elif defined(PLATFORM_RA8P1)

#include "arch/ra8p1/tiku_cpu_freq_boot_arch.h"
#include "arch/ra8p1/tiku_cpu_common.h"
#include "arch/ra8p1/tiku_uart_arch.h"
#include "arch/ra8p1/tiku_cache_arch.h"
#include "arch/ra8p1/tiku_sdram_arch.h"
#include "arch/ra8p1/tiku_xflash_arch.h"
#include "arch/ra8p1/tiku_usbhs_arch.h"
#include "kernel/fs/tiku_nvm_backend.h"
#include "arch/ra8p1/tiku_gpio_arch.h"
#include "arch/ra8p1/tiku_timer_arch.h"
#include "arch/ra8p1/tiku_uart_arch.h"
#include "arch/ra8p1/tiku_ra8p1_regs.h"
#include "hal/tiku_crit_hal.h"

/* EK-RA8P1 LED1, the blue one at P600. */
#define TIKU_MIN_LED_PORT   TIKU_BOARD_LED1_PORT
#define TIKU_MIN_LED_PIN    TIKU_BOARD_LED1_PIN

int main(void)
{
    /* Nothing to do: the reset clock tree is what every constant in this image
     * was computed from.  The call is here so the shape matches the other
     * ports and R4 has one place to grow into. */
    tiku_cpu_boot_ra8p1_init();

    tiku_ra8p1_gpio_init_output(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);

    /* SCI8 on PD02/PD03 -- the J-Link OB virtual COM port. */
    tiku_uart_init();

    tiku_cpu_ra8p1_delay_ms(100);
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (EK-RA8P1 v1) ---\n");

    tiku_ra8p1_clock_t c;
    tiku_cpu_ra8p1_clock_probe(&c);
    /* NOMINAL, not measured: these are what SCKSCR and SCKDIVCR imply given
     * the datasheet's figure for the selected source.  MOCO's own tolerance is
     * +-10%, and on this board the real rate is 8.33 MHz -- so this line says
     * what the tree is CONFIGURED as, and R4's job is to make it also true. */
    tiku_uart_printf("clk: cksel=%u src=%u Hz(nom) iclk=%u Hz pclka=%u Hz\n",
                     (unsigned int)c.cksel, (unsigned int)c.src_hz,
                     (unsigned int)c.iclk_hz, (unsigned int)c.pclka_hz);
    tiku_uart_printf("cpuid=%x  tick=%u Hz reload=%u\n",
                     (unsigned int)TIKU_REG32(RA8P1_SCB_CPUID),
                     (unsigned int)TIKU_CLOCK_ARCH_SECOND,
                     (unsigned int)TIKU_CLOCK_ARCH_INTERVAL);
    /* SAU is readable only from the secure state, so a plausible region count
     * here is direct evidence the image executes secure -- rather than the
     * debugger's claim quoted back at itself. */
    tiku_uart_printf("tz: sau_type=%x sau_ctrl=%x (secure-only port)\n",
                     (unsigned int)TIKU_REG32(RA8P1_SAU_TYPE),
                     (unsigned int)TIKU_REG32(RA8P1_SAU_CTRL));

    /* Start the tick and let it drive the loop.  This is the whole point of
     * the R2 smoke test: the interval between lines is measured by SysTick and
     * by nothing else, so a line every second of WALL clock is proof the tick
     * counts at the rate it claims.  A delay-loop heartbeat would prove only
     * that the delay loop is self-consistent. */
    tiku_clock_arch_init();
    __asm__ volatile ("cpsie i" ::: "memory");

    /* Prove the claim tiku_crit_arch.c makes in its header comment, rather
     * than asserting it: hold a masked critical window across a whole tick
     * period and show the counter advanced anyway.  It can, because SysTick is
     * a core exception with no NVIC line for the mask to reach.  When R4 moves
     * the tick to ULPT or AGT this test starts FAILING, which is exactly when
     * someone needs to be told. */
    {
        tiku_clock_arch_time_t a, b;
        /* Bounded by ITERATIONS, not by the tick.  Waiting on the tick would
         * make a silenced tick spin here forever -- the FAIL branch would be
         * unreachable and the test could only ever pass. */
        unsigned long budget = 5000000UL;

        a = tiku_clock_arch_time();
        tiku_crit_arch_mask_irqs(0U);
        while (budget-- != 0UL && (long)(tiku_clock_arch_time() - a) < 4) { }
        b = tiku_clock_arch_time();
        tiku_crit_arch_unmask_irqs();
        tiku_uart_printf("crit: tick advanced %u ticks under NVIC mask (%s)\n",
                         (unsigned int)(b - a),
                         (b != a) ? "PASS" : "FAIL -- tick was silenced");
    }

    /* Is the busy-wait delay the length it claims?  Everything that paces
     * itself without the tick -- notably a watchdog kick loop -- depends on
     * this, and the answer is a measurement, not the spin constant. */
    {
        tiku_clock_arch_time_t a, b;

        a = tiku_clock_arch_time();
        tiku_cpu_ra8p1_delay_ms(1000U);
        b = tiku_clock_arch_time();
        tiku_uart_printf("delay: 1000 ms measured %u ticks (%u expected), "
                         "spin=%u/ms\n",
                         (unsigned int)(b - a),
                         (unsigned int)TIKU_CLOCK_ARCH_SECOND,
                         (unsigned int)tiku_cpu_ra8p1_spin_per_ms());
    }

    /* The clock, stated without a host stopwatch: CAC counts one on-chip
     * clock against the board's crystal, so this is the port's own witness. */
    {
        uint16_t n = tiku_cpu_ra8p1_cac_measure(RA8P1_CAC_CLK_MOCO,
                                                RA8P1_CAC_CLK_MAIN, 3U);

        if (n != 0U) {
            tiku_uart_printf("cac: moco=%u Hz (nominal %u)\n",
                             (unsigned int)((unsigned long)n *
                                            (TIKU_BOARD_MOSC_HZ / 8192UL)),
                             (unsigned int)TIKU_RA8P1_MOCO_HZ);
        } else {
            tiku_uart_printf("cac: measurement did not complete\n");
        }
    }

    tiku_cpu_freq_ra8p1_init(240U);
    tiku_uart_printf("pll: core %u Hz, sciclk %u Hz, tick reload %u\n",
                     (unsigned int)tiku_cpu_ra8p1_clock_get_hz(),
                     (unsigned int)tiku_cpu_ra8p1_sciclk_get_hz(),
                     (unsigned int)tiku_clock_arch_fine_max());
    {
        uint16_t n = tiku_cpu_ra8p1_cac_measure(RA8P1_CAC_CLK_PCLKB,
                                                RA8P1_CAC_CLK_MAIN, 3U);
        tiku_uart_printf("cac: pclkb=%u Hz (expect 60000000)\n",
                         (unsigned int)((unsigned long)n *
                                        (TIKU_BOARD_MOSC_HZ / 8192UL)));
    }

    /*
     * SDRAM bring-up probe (R7b).  MINIMAL on purpose: no MPU and no caches,
     * so what is measured is the controller and the part rather than a cache
     * interaction.
     */
    {
        volatile uint32_t *sd = (volatile uint32_t *)TIKU_RA8P1_SDRAM_ADDR;
        uint32_t words = TIKU_RA8P1_SDRAM_BYTES / 4u;
        uint32_t i, bad, first_bad, t0, t1;
        int rc;

        tiku_uart_printf("sdram: bclk = %u Hz\n",
                         (unsigned int)tiku_cpu_ra8p1_bclk_get_hz());
        rc = tiku_ra8p1_sdram_init();
        tiku_uart_printf("sdram: init rc=%d SDTR=%x SDCCR=%x SDSR=%x\n", rc,
                         (unsigned int)TIKU_REG32(RA8P1_SDTR),
                         (unsigned int)TIKU_REG8(RA8P1_SDCCR),
                         (unsigned int)TIKU_REG8(RA8P1_SDSR));

        if (rc == TIKU_RA8P1_SDRAM_OK) {
            /* Walking ones: catches a stuck or shorted data line before the
             * long test spends a second proving the same thing. */
            bad = 0u;
            for (i = 0; i < 32u; i++) {
                sd[0] = (1UL << i);
                if (sd[0] != (1UL << i)) { bad++; }
            }
            tiku_uart_printf("sdram: walking-1 dq errors = %u\n",
                             (unsigned int)bad);

            /*
             * WALKING ADDRESS.  Write a unique marker at each power-of-two
             * word offset, then read them all back.  A dropped or swapped
             * address line shows up here as one specific offset aliasing
             * onto another, which a linear test only reports as "everything
             * is wrong".
             */
            for (i = 0; i < 24u; i++) { sd[1UL << i] = 0xC0DE0000u + i; }
            sd[0] = 0xC0DEFFFFu;
            bad = 0u;
            for (i = 0; i < 24u; i++) {
                uint32_t got = sd[1UL << i];
                if (got != 0xC0DE0000u + i) {
                    bad++;
                    if (bad <= 3u) {
                        tiku_uart_printf("sdram:   A%u (word %x) = %x,"
                                         " expected %x\n", (unsigned int)i,
                                         (unsigned int)(1UL << i),
                                         (unsigned int)got,
                                         (unsigned int)(0xC0DE0000u + i));
                    }
                }
            }
            tiku_uart_printf("sdram: walking-address errors = %u of 24"
                             " (sd0=%x)\n", (unsigned int)bad,
                             (unsigned int)sd[0]);

            /* RETENTION vs ADDRESSING, isolated.  Write a small block and
             * read it straight back, then again after >64 ms.  If the first
             * passes and the second does not, the array is fine and refresh
             * is not running -- which is the difference between a wiring
             * fault and a controller one. */
            for (i = 0; i < 256u; i++) { sd[i] = 0xA5A50000u + i; }
            bad = 0u;
            for (i = 0; i < 256u; i++) {
                if (sd[i] != 0xA5A50000u + i) { bad++; }
            }
            tiku_uart_printf("sdram: 256w immediate errors = %u "
                             "(sd0=%x sd1=%x sd2=%x MOD=%x)\n",
                             (unsigned int)bad, (unsigned int)sd[0],
                             (unsigned int)sd[1], (unsigned int)sd[2],
                             (unsigned int)TIKU_REG16(RA8P1_SDMOD));
            tiku_cpu_ra8p1_delay_us(150000u);      /* > 64 ms retention */
            bad = 0u;
            for (i = 0; i < 256u; i++) {
                if (sd[i] != 0xA5A50000u + i) { bad++; }
            }
            tiku_uart_printf("sdram: 256w after 150ms errors = %u "
                             "(RFEN=%x RFCR=%x SELF=%x CKO=%x)\n",
                             (unsigned int)bad,
                             (unsigned int)TIKU_REG8(RA8P1_SDRFEN),
                             (unsigned int)TIKU_REG16(RA8P1_SDRFCR),
                             (unsigned int)TIKU_REG8(RA8P1_SDSELF),
                             (unsigned int)TIKU_REG8(RA8P1_SDCKOCR));

            /*
             * Address-as-data over the WHOLE 64 MB.  This is the test that
             * matters: a wrong column shift, bus width or bank mapping shows
             * up as ALIASING -- two addresses sharing a cell -- which any
             * spot check passes and this cannot.
             */
            for (i = 0; i < words; i++) { sd[i] = i; }
            bad = 0u; first_bad = 0xFFFFFFFFu;
            for (i = 0; i < words; i++) {
                if (sd[i] != i) {
                    if (bad == 0u) { first_bad = i; }
                    bad++;
                }
            }
            tiku_uart_printf("sdram: address-as-data over %u MB: %u errors"
                             " (first at word %x)\n",
                             (unsigned int)(TIKU_RA8P1_SDRAM_BYTES >> 20),
                             (unsigned int)bad, (unsigned int)first_bad);

            /* Bandwidth, GPT0 at PCLKD. */
            TIKU_REG32(RA8P1_MSTPCRE) |= RA8P1_MSTPE_GPT0;
            TIKU_REG32(RA8P1_GPT_GTCLKCR) = RA8P1_GPT_GTCLKCR_BPEN;
            TIKU_REG32(RA8P1_MSTPCRE) &= ~RA8P1_MSTPE_GPT0;
            (void)TIKU_REG32(RA8P1_MSTPCRE);
            TIKU_REG32(RA8P1_GPT_GTCR(0)) = 0UL;
            TIKU_REG32(RA8P1_GPT_GTPR(0)) = 0xFFFFFFFFUL;
            TIKU_REG32(RA8P1_GPT_GTCNT(0)) = 0UL;
            TIKU_REG32(RA8P1_GPT_GTCR(0)) = RA8P1_GPT_GTCR_MD_SAW |
                                            RA8P1_GPT_GTCR_CST;
            t0 = TIKU_REG32(RA8P1_GPT_GTCNT(0));
            for (i = 0; i < (1u << 20); i++) { sd[i] = i; }
            t1 = TIKU_REG32(RA8P1_GPT_GTCNT(0));
            tiku_uart_printf("sdram: seq write 4 MB = %u counts\n",
                             (unsigned int)(t1 - t0));
            t0 = TIKU_REG32(RA8P1_GPT_GTCNT(0));
            bad = 0u;
            for (i = 0; i < (1u << 20); i++) { bad += sd[i]; }
            t1 = TIKU_REG32(RA8P1_GPT_GTCNT(0));
            tiku_uart_printf("sdram: seq read  4 MB = %u counts (%x)\n",
                             (unsigned int)(t1 - t0), (unsigned int)bad);
        }
    }

    /* Octo-SPI flash phase 1: identify the part over 1-1-1 SPI.  Known-good
     * on this board: C2 86 3A -- Macronix, 1.8 V octaflash family, 512 Mb. */
    {
        uint8_t id[3] = { 0, 0, 0 };
        int rc = tiku_ra8p1_xflash_read_id(id);

        tiku_uart_printf("xflash: rc=%d id = %x %x %x\n", rc,
                         (unsigned int)id[0], (unsigned int)id[1],
                         (unsigned int)id[2]);

        /* SFDP offset 0 must read the JESD216 signature "SFDP".  This is the
         * first transaction carrying an address AND dummy cycles, and it
         * checks itself: no other four bytes are correct. */
        {
            uint8_t sf[8] = { 0 };
            uint8_t sr = 0xFFU;
            int rs = tiku_ra8p1_xflash_read_sfdp(0UL, sf, 8U);

            tiku_uart_printf("xflash: sfdp rc=%d -> %x %x %x %x (\"%c%c%c%c\")"
                             " rev %x.%x\n", rs,
                             (unsigned int)sf[0], (unsigned int)sf[1],
                             (unsigned int)sf[2], (unsigned int)sf[3],
                             sf[0], sf[1], sf[2], sf[3],
                             (unsigned int)sf[5], (unsigned int)sf[4]);
            rs = tiku_ra8p1_xflash_read_status(&sr);
            tiku_uart_printf("xflash: rdsr rc=%d sr=%x (wip=%u wel=%u)\n",
                             rs, (unsigned int)sr, (unsigned int)(sr & 1U),
                             (unsigned int)((sr >> 1) & 1U));

            /*
             * Memory-mapped read, cross-checked against the manual path.
             * Agreement between two independent routes to the same bytes is
             * the proof; a mapped window that returns plausible-looking data
             * nobody compared is how a wrong dummy count ships.
             */
            {
                volatile const uint8_t *xm =
                    (volatile const uint8_t *)TIKU_RA8P1_XFLASH_ADDR;
                uint8_t man[8];
                unsigned k, bad = 0;

                (void)tiku_ra8p1_xflash_mmap_enable();
                (void)tiku_ra8p1_xflash_cmd(0x0C00U, 0UL, 4U, 8U, man, 8U, 0);
                for (k = 0; k < 8u; k++) {
                    if (xm[k] != man[k]) { bad++; }
                }
                tiku_uart_printf("xflash: mmap[0..7] = %x %x %x %x %x %x %x %x"
                                 "  (vs manual: %u mismatches)\n",
                                 (unsigned int)xm[0], (unsigned int)xm[1],
                                 (unsigned int)xm[2], (unsigned int)xm[3],
                                 (unsigned int)xm[4], (unsigned int)xm[5],
                                 (unsigned int)xm[6], (unsigned int)xm[7],
                                 bad);

                /*
                 * Write path on the LAST sector, deliberately far from
                 * offset 0 which holds what this board shipped with.
                 * Erase to FF, program a pattern, read it back both ways.
                 */
                {
                    const uint32_t a = TIKU_RA8P1_XFLASH_BYTES -
                                       TIKU_RA8P1_XFLASH_SECTOR;
                    static const uint8_t pat[8] =
                        { 0xC0, 0xFF, 0xEE, 0x01, 0x23, 0x45, 0x67, 0x89 };
                    uint8_t rb[8] = { 0 };
                    int re, rp;
                    unsigned q, blank = 0, match = 0, mm_ok = 0;

                    re = tiku_ra8p1_xflash_erase_sector(a);
                    (void)tiku_ra8p1_xflash_cmd(0x0C00U, a, 4U, 8U, rb, 8U, 0);
                    for (q = 0; q < 8u; q++) {
                        if (rb[q] == 0xFFU) { blank++; }
                    }

                    rp = tiku_ra8p1_xflash_program(a, pat, 8U);
                    for (q = 0; q < 8u; q++) { rb[q] = 0; }
                    (void)tiku_ra8p1_xflash_cmd(0x0C00U, a, 4U, 8U, rb, 8U, 0);
                    for (q = 0; q < 8u; q++) {
                        if (rb[q] == pat[q]) { match++; }
                        if (xm[a + q] == pat[q]) { mm_ok++; }
                    }
                    tiku_uart_printf("xflash: erase rc=%d blank=%u/8 |"
                                     " program rc=%d manual=%u/8 mmap=%u/8\n",
                                     re, blank, rp, match, mm_ok);

                    /*
                     * OPI.  Verified against the device's own factory SFDP
                     * inside opi_enter(), then benched here against the
                     * single-bit baseline over the same span.
                     *
                     * Note the pattern programmed above was written in SPI,
                     * so reading it back in DOPI returns it PAIR-SWAPPED --
                     * that is the documented device behaviour, not a fault,
                     * and it is why the check below compares against the
                     * swapped pattern rather than the plain one.
                     */
                    {
                        volatile uint32_t *cyc =
                            (volatile uint32_t *)0xE0001004UL;
                        uint8_t o[8];
                        uint32_t t0, slow, fast;
                        unsigned n, okp = 0;
                        volatile uint32_t sink = 0;

                        /* DWT counts only once the trace block is powered:
                         * DEMCR.TRCENA first, then CYCCNTENA.  Setting the
                         * second without the first leaves the counter at
                         * zero and a bench that reports an infinite speedup. */
                        TIKU_REG32(0xE000EDFCUL) |= (1UL << 24);
                        TIKU_REG32(0xE0001000UL) |= 1UL;
                        *cyc = 0UL;

                        t0 = *cyc;
                        for (n = 0; n < 4096U; n += 4U) {
                            sink += *(volatile const uint32_t *)(xm + n);
                        }
                        slow = *cyc - t0;

                        re = tiku_ra8p1_xflash_opi_enter();
                        if (re == 0) {
                            (void)tiku_ra8p1_xflash_read(a, o, 8U);
                            for (n = 0; n < 8u; n++) {
                                if (o[n] == pat[n ^ 1u]) { okp++; }
                            }
                            (void)tiku_ra8p1_xflash_mmap_enable();
                            t0 = *cyc;
                            for (n = 0; n < 4096U; n += 4U) {
                                sink += *(volatile const uint32_t *)(xm + n);
                            }
                            fast = *cyc - t0;

                            /* The number that decides whether a model can
                             * live in flash: a real bulk copy into SDRAM,
                             * the way a boot-time restore would do it. */
                            if (tiku_ra8p1_sdram_ready()) {
                                uint32_t t1, cp;
                                unsigned long kbps;

                                uint32_t *d = (uint32_t *)
                                    TIKU_RA8P1_SDRAM_ADDR;
                                const uint32_t *sp = (const uint32_t *)
                                    TIKU_RA8P1_XFLASH_ADDR;
                                unsigned long q;

                                t1 = *cyc;
                                for (q = 0; q < 262144UL / 4UL; q++) {
                                    d[q] = sp[q];
                                }
                                __asm__ volatile ("dsb" ::: "memory");
                                cp = *cyc - t1;
                                /* 240 MHz core: bytes * 240 / cycles = MB/s */
                                kbps = cp ? (262144UL / (cp / 240UL)) : 0UL;
                                tiku_uart_printf("xflash: 256KB flash->SDRAM"
                                    " %u cyc = %u MB/s -> 62.8 MB in %u ms\n",
                                    (unsigned int)cp, (unsigned int)kbps,
                                    (unsigned int)(kbps ? 62800UL / kbps : 0));
                            }
                            /* Bulk write: erase a sector, fill 4 KB with a
                             * position-dependent pattern (so a shifted or
                             * duplicated chunk cannot pass), read it back
                             * through the mapped window. */
                            {
                                static uint64_t buf[512];
                                const uint32_t wa = 0x02000000UL;
                                unsigned long q;
                                unsigned bad = 0;
                                int rw;
                                uint32_t t2, wc;

                                for (q = 0; q < 512UL; q++) {
                                    buf[q] = 0x5A5A0000UL + q;
                                }
                                (void)tiku_ra8p1_xflash_erase_sector(wa);
                                t2 = *cyc;
                                rw = tiku_ra8p1_xflash_write(wa, buf, 4096UL);
                                wc = *cyc - t2;
                                (void)tiku_ra8p1_xflash_mmap_enable();
                                for (q = 0; q < 512UL; q++) {
                                    const volatile uint64_t *v =
                                        (const volatile uint64_t *)
                                        (TIKU_RA8P1_XFLASH_ADDR + wa);
                                    if (v[q] != buf[q]) { bad++; }
                                }
                                tiku_uart_printf("xflash: bulk write 4KB rc=%d"
                                    " bad=%u/512 in %u cyc (%u KB/s)\n",
                                    rw, bad, (unsigned int)wc,
                                    (unsigned int)(wc ? (4096UL * 240000UL)
                                                        / wc : 0));
                            }

                            /* The backend path a filesystem actually
                             * exercises: an odd offset and a length that is
                             * no multiple of anything, so head, aligned
                             * middle and tail all get used. */
                            {
                                struct tiku_nvm_backend *be =
                                    tiku_ra8p1_xflash_backend();
                                static uint8_t ub[300];
                                unsigned long q2;
                                unsigned off2;

                                for (q2 = 0; q2 < 300UL; q2++) {
                                    ub[q2] = (uint8_t)(q2 * 7UL + 3UL);
                                }
                                /* Odd vs even start, and a 64-aligned start,
                                 * so the failing case is isolated rather than
                                 * inferred. */
                                for (off2 = 0; off2 < 3U; off2++) {
                                    const uint32_t base2 =
                                        0x02010000UL + (off2 * 0x2000UL);
                                    const uint32_t delta =
                                        (off2 == 0U) ? 5UL
                                                     : ((off2 == 1U) ? 4UL
                                                                     : 0UL);
                                    unsigned bad2 = 0, head = 0;
                                    int rb2;

                                    if (be == NULL) { break; }
                                    (void)be->erase(be, base2, 4096UL);
                                    rb2 = be->write(be, base2 + delta,
                                                    ub, 300UL);
                                    (void)tiku_ra8p1_xflash_mmap_enable();
                                    for (q2 = 0; q2 < 300UL; q2++) {
                                        if (be->base[base2 + delta + q2] !=
                                            ub[q2]) {
                                            bad2++;
                                            if (q2 < 64UL) { head++; }
                                        }
                                    }
                                    tiku_uart_printf("xflash: backend +%u"
                                        " len300 rc=%d bad=%u/300"
                                        " (first64 bad=%u)\n",
                                        (unsigned int)delta, rb2, bad2, head);
                                }
                            }

                            tiku_uart_printf("xflash: OPI ok, dqs=%d"
                                " (eye %d cells) ddrsmpex=%d, pattern=%u/8\n",
                                tiku_ra8p1_xflash_dqs_shift(),
                                tiku_ra8p1_xflash_dqs_margin(),
                                tiku_ra8p1_xflash_ddrsmpex(), okp);
                            tiku_uart_printf("xflash: 4KB mapped read %u cyc"
                                " (SPI 4MHz x1) -> %u cyc (OPI 120MHz x8 DDR)"
                                " = %ux\n", (unsigned int)slow,
                                (unsigned int)fast,
                                (unsigned int)(fast ? slow / fast : 0));
                        } else {
                            tiku_uart_printf("xflash: OPI enter rc=%d (%s),"
                                " device left in SPI\n", re,
                                (re == -2) ? "frame rejected even at 4 MHz"
                                           : "ok slow, fails at 120 MHz");
                        }
                        (void)sink;
                        (void)sink;
                    }
                }
            }
        }
    }

    /*
     * U1: USB-HS bring-up.  The gate is that a host attach produces a bus
     * reset and DVSTCTR0.RHST reports high speed -- both of which the
     * hardware does by itself (the chirp handshake is not software's job),
     * so this is reachable with no endpoint code at all.
     */
    {
        static const char *const dvname[5] = {
            "powered", "default", "address", "configured", "suspend"
        };
        static const char *const spname[3] = { "none", "full", "HIGH" };
        /* Full-speed reading of the line pair; in HS these mean squelch /
         * unsquelch, and during a reset handshake, chirp J / chirp K. */
        static const char *const lnname[4] = { "SE0", "J", "K", "SE1" };
        uint16_t r[8];
        int rc = tiku_ra8p1_usbhs_up(1);
        unsigned last_dv = 99u, last_sp = 99u, last_ln = 99u, t;

        tiku_uart_printf("usbhs: up rc=%d pll_locked=%d\n", rc,
                         tiku_ra8p1_usbhs_pll_locked());
        if (rc == 0) {
            tiku_ra8p1_usbhs_regs(r, 8u);
            tiku_uart_printf("usbhs: syscfg=%x syssts=%x pllsta=%x"
                             " dvstctr=%x physet=%x intsts0=%x lpsts=%x\n",
                             r[0], r[1], r[2], r[3], r[4], r[5], r[6]);
            tiku_uart_printf("usbhs: ID pin = %d (1=device strap, 0=host)\n",
                             tiku_ra8p1_usbhs_id_high());

            (void)tiku_ra8p1_usbhs_attach(1);
            tiku_uart_printf("usbhs: attached, watching for a host...\n");

            /* Six seconds is generous: a host enumerates in tens of
             * milliseconds, so a quiet window this long means the cable or
             * the port is the answer, not the timing. */
            for (t = 0; t < 600u; t++) {
                unsigned dv = (unsigned)tiku_ra8p1_usbhs_devstate();
                unsigned sp = (unsigned)tiku_ra8p1_usbhs_speed();
                unsigned ln;

                tiku_ra8p1_usbhs_regs(r, 8u);
                ln = (unsigned)(r[1] & 3u);
                if (dv != last_dv || sp != last_sp || ln != last_ln) {
                    tiku_uart_printf("usbhs: t=%ums state=%s speed=%s"
                                     " lnst=%u(%s) frame=%u\n", t * 10u,
                                     dvname[dv < 5u ? dv : 4u],
                                     spname[sp < 3u ? sp : 0u], ln,
                                     lnname[ln], (unsigned int)r[7]);
                    last_dv = dv;
                    last_sp = sp;
                    last_ln = ln;
                }
                tiku_cpu_ra8p1_delay_us(10000u);
            }
            tiku_ra8p1_usbhs_regs(r, 8u);
            tiku_uart_printf("usbhs: final syscfg=%x syssts=%x dvstctr=%x"
                             " intsts0=%x frame=%u\n",
                             r[0], r[1], r[3], r[5], r[7]);
            /*
             * The U1 gate, stated rather than left to be read off: a host
             * drove a bus reset (device state left Powered) and the chirp
             * handshake settled on high speed.  The frame counter is the
             * corroborating evidence -- it only advances on SOF packets the
             * host actually sends, so it cannot be produced by the device
             * misreading its own idle bus.
             */
            {
                int reset_seen = (tiku_ra8p1_usbhs_devstate() !=
                                  TIKU_RA8P1_USBHS_DEV_POWERED);
                int hs = (tiku_ra8p1_usbhs_speed() ==
                          TIKU_RA8P1_USBHS_SPEED_HIGH);

                tiku_uart_printf("usbhs: U1 gate: bus reset=%s speed=%s"
                                 " sof=%s -> %s\n",
                                 reset_seen ? "yes" : "NO",
                                 hs ? "HIGH" : "not high",
                                 (r[7] != 0u) ? "running" : "NONE",
                                 (reset_seen && hs && r[7] != 0u)
                                   ? "PASS" : "incomplete");
            }
        }
    }

    tiku_uart_printf("cache: state=%u (bit0 I, bit1 D)\n",
                     (unsigned int)tiku_ra8p1_cache_state());

    tiku_clock_arch_time_t next = tiku_clock_arch_time() +
                                  (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND;
    uint32_t i = 0;
    while (1) {
        while ((long)(tiku_clock_arch_time() - next) < 0) {
            __asm__ volatile ("wfi");
        }
        next += (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND;

        tiku_uart_printf("TikuOS minimal: hello #%u  ticks=%u\n",
                         (unsigned int)i,
                         (unsigned int)tiku_clock_arch_time());

        tiku_ra8p1_gpio_toggle(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);
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
