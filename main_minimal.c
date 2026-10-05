/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * main_minimal.c - bare smoke-test image, built by `make MINIMAL=1`.
 *
 * No kernel, scheduler or shell: brings up clocks and the console and prints
 * a heartbeat.  RA8P1 and ESP32-C61 run hardware probes first, and the RA8P1
 * image then serves USB mass storage in place of the heartbeat.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#if defined(PLATFORM_AMBIQ)

#include "arch/ambiq/tiku_cpu_freq_boot_arch.h"
#include "arch/ambiq/tiku_cpu_common.h"
#include "arch/ambiq/tiku_uart_arch.h"
#include "arch/ambiq/tiku_gpio_arch.h"

/* EVB LED0, active-low: Apollo510 pad 165, Apollo4 Lite pad 12.  The loop
 * toggles it once per heartbeat line. */
#if defined(TIKU_DEVICE_APOLLO4L)
#define TIKU_MIN_LED_PAD   12u
#else
#define TIKU_MIN_LED_PAD   165u
#endif

int main(void)
{
    /* Caches and prefetch on; power and clocks stay as the secure bootloader
     * set them. */
    tiku_cpu_boot_ambiq_init();

    tiku_ambiq_gpio_init_output(TIKU_MIN_LED_PAD);

    /* Console on the EVB COM UART: UART0 on Apollo510, UART2 on Apollo4
     * Lite. */
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
/* The board header, through the device/board router, supplies the LED macros
 * and TIKU_BOARD_NAME: the nRF54L15-DK LEDs are active-low, the nRF54LM20-DK's
 * active-high, on different pins. */
#include "arch/nordic/tiku_device_select.h"

/* LED signals, independent of the console UART:
 *   LED1 solid-on  = main() was reached and GPIO works
 *   LED2 blinking  = the loop and its delays run */

/* Progress markers in RAM, readable over the debugger (nrfutil device read at
 * the symbol's address) without the LEDs or the console.  main() stores
 * 0xB007B007 in the first; the second counts heartbeat lines. */
volatile uint32_t g_nordic_main_reached;
volatile uint32_t g_nordic_loop_count;

#ifdef TIKU_MIN_RRAM_TEST
/*
 * RRAM runtime-write probe (build with EXTRA_CFLAGS=-DTIKU_MIN_RRAM_TEST=1).
 *
 * Plain CPU stores to RRAM, from code running in RRAM, with the RRAMC WEN
 * gate open; a store with WEN closed faults the bus.  Each test prints its
 * result and the value read back:
 *   T1  word store bracketed by the full nrfx READY/READYNEXT handshake
 *   T2  8 back-to-back byte stores into the 32-entry write buffer, then
 *       TASKS_COMMITWRITEBUF and a READY wait
 *   T3  WEN set, immediate store, no waits
 *   T4  store, close WEN with no wait or commit, read back with WEN closed
 * The scratch target, 0x17B000, is inside the persist partition on the
 * nRF54L15 and inside the NVM region on the nRF54LM20, so the probe
 * overwrites durable words a kernel image keeps there.
 */
#include "arch/nordic/tiku_nordic_mdk.h"   /* per-device MDK router */

#define RRAM_TEST_ADDR   0x0017B000UL      /* L15: persist partition;
                                            * LM20: NVM region */
#define RRAMC_WEN        (1UL << 0)
#define RRAMC_BUF32      (32UL << 8)               /* WRITEBUFSIZE = 32 */

/** @brief Run RRAM write tests T1 to T4 and print each result. */
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

    /* T3: WEN, then an immediate store with no ready waits.  With WEN open
     * the controller stalls the bus until it accepts a plain store. */
    tiku_uart_puts("T3: minimal path (no ready waits)... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN;
    *(volatile uint32_t *)(RRAM_TEST_ADDR + 16u) = 0xDEADBEEFu;
    while (NRF_RRAMC_S->READY == 0u)     { }
    NRF_RRAMC_S->CONFIG = cfg0;
    tiku_uart_printf("PASS (read 0x%x)\n",
                     (unsigned int)*(volatile uint32_t *)(RRAM_TEST_ADDR + 16u));

    /* T4: store, then close WEN with no READY wait and no commit, as
     * tiku_mpu_arch_lock_nvm() does after a write, and read back with the
     * gate closed.  STALE-READ means that read missed the store; the second
     * value is read again after a READY wait. */
    tiku_uart_puts("T4: store, close gate w/o commit, read... ");
    NRF_RRAMC_S->CONFIG = cfg0 | RRAMC_WEN;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    while (NRF_RRAMC_S->READY == 0u)     { }        /* settle from T3    */
    *(volatile uint32_t *)(RRAM_TEST_ADDR + 24u) = 0xCAFEF00Du;
    NRF_RRAMC_S->CONFIG = cfg0;                     /* close: no waits   */
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
 * The nRF54LM20A's second SRAM bank, RAM2 at 0x20040000, is 256 KB in the
 * MDK, but its top is not backed: a store there bus-faults, and
 * nrf54lm20a.ld stops SRAM2 1 KB short of it.  The probe finds the boundary
 * from the CPU side:
 *   pass 1: write and verify every 16 KB, 0x20040000 to 0x2007C000
 *   pass 2: the top 8 KB in 256 B steps, printing each address before the
 *           store, so after a bus fault the last address printed is the
 *           first bad one; the default handler parks in WFE and LED2 stops.
 */
/** @brief Sweep RAM2 coarse then fine, printing each result. */
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
    /* For the debugger: 0xB007B007 here shows main() ran. */
    g_nordic_main_reached = 0xB007B007u;

    /* LED1 lights before any clock, delay or UART code, so a lit LED1 shows
     * the reset handler and the C runtime ran and GPIO works. */
    TIKU_BOARD_LED1_INIT();
    TIKU_BOARD_LED1_ON();               /* on = main() reached, GPIO works */

    /* Sets the PLL rate and starts the HFXO, the UARTE's reference.  The
     * delays program SysTick on each call. */
    tiku_cpu_boot_nordic_init();

    /* LED2 starts off and blinks in the loop below. */
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

/* NUCLEO-N657X0-Q LED3, on PG0.  LED1 is PG8 and LED2 is PG10 on the same
 * port. */
#define TIKU_MIN_LED_PORT   STM32N6_GPIO_PORT_G
#define TIKU_MIN_LED_PIN    0U

int main(void)
{
    /* Turns HSI on and waits for it; the boot ROM's clock tree, which the
     * console runs on, is otherwise unchanged. */
    tiku_cpu_boot_stm32n6_init();

    tiku_stm32n6_gpio_init_output(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);

    /* USART1 on PE5/PE6 -- the ST-LINK virtual COM port. */
    tiku_uart_init();

    tiku_cpu_stm32n6_delay_ms(100);
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (NUCLEO-N657X0-Q) ---\n");
    /* The build's fallback spin rate.  The delays measure the rate against
     * LPTIM1 on first use and use this constant only if that fails. */
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
#include "kernel/fs/tiku_bigblob.h"
#include "arch/ra8p1/tiku_store_arch.h"
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
    /* Empty on this port: the image starts on the reset clock tree, MOCO at
     * 8 MHz, and tiku_cpu_freq_ra8p1_init() below raises it. */
    tiku_cpu_boot_ra8p1_init();

    tiku_ra8p1_gpio_init_output(TIKU_MIN_LED_PORT, TIKU_MIN_LED_PIN);

    /* SCI8 on PD02/PD03 -- the J-Link OB virtual COM port. */
    tiku_uart_init();

    tiku_cpu_ra8p1_delay_ms(100);
    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (EK-RA8P1 v1) ---\n");

    tiku_ra8p1_clock_t c;
    tiku_cpu_ra8p1_clock_probe(&c);
    /* Nominal rates: what SCKSCR and SCKDIVCR imply for the datasheet rate of
     * the selected source.  MOCO is specified to +-10%; the cac line below
     * measures it. */
    tiku_uart_printf("clk: cksel=%u src=%u Hz(nom) iclk=%u Hz pclka=%u Hz\n",
                     (unsigned int)c.cksel, (unsigned int)c.src_hz,
                     (unsigned int)c.iclk_hz, (unsigned int)c.pclka_hz);
    tiku_uart_printf("cpuid=%x  tick=%u Hz reload=%u\n",
                     (unsigned int)TIKU_REG32(RA8P1_SCB_CPUID),
                     (unsigned int)TIKU_CLOCK_ARCH_SECOND,
                     (unsigned int)TIKU_CLOCK_ARCH_INTERVAL);
    /* The SAU reads as zero outside the secure state, so a plausible region
     * count here shows the image runs secure. */
    tiku_uart_printf("tz: sau_type=%x sau_ctrl=%x (secure-only port)\n",
                     (unsigned int)TIKU_REG32(RA8P1_SAU_TYPE),
                     (unsigned int)TIKU_REG32(RA8P1_SAU_CTRL));

    /* Starts the SysTick tick and unmasks interrupts; the critical-section
     * and delay tests below time themselves on the tick. */
    tiku_clock_arch_init();
    __asm__ volatile ("cpsie i" ::: "memory");

    /* A critical section masks the NVIC, and SysTick is a core exception
     * outside that mask, so the tick advances inside the window.  The window
     * waits for 4 ticks; a tick moved to an NVIC-routed timer prints FAIL. */
    {
        tiku_clock_arch_time_t a, b;
        /* Bounded by an iteration budget as well as the tick, so a silenced
         * tick ends the wait and prints FAIL. */
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

    /* Times a 1000 ms busy-wait delay on the tick.  Code paced without the
     * tick, such as a watchdog kick loop, relies on the delay's length. */
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

    /* CAC counts MOCO against the board's main crystal, which gives MOCO's
     * real rate. */
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
     * SDRAM bring-up probe.  Its reads test the controller and the part only
     * with the D-cache off: this image sets up no MPU, and the default memory
     * map makes the SDRAM at 0x68000000 write-back cacheable.
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
            /* Walking ones on one word: a stuck or shorted data line fails
             * here. */
            bad = 0u;
            for (i = 0; i < 32u; i++) {
                sd[0] = (1UL << i);
                if (sd[0] != (1UL << i)) { bad++; }
            }
            tiku_uart_printf("sdram: walking-1 dq errors = %u\n",
                             (unsigned int)bad);

            /*
             * Walking address: a unique marker at each power-of-two word
             * offset, all read back after the writes.  A dropped or swapped
             * address line shows as one offset aliasing onto another.
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

            /* Retention: 256 words read back at once and again after
             * 150 ms, longer than the 64 ms refresh period.  A pass then a
             * fail means the array works and refresh is not running. */
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
             * Address-as-data over the whole part: a wrong column shift, bus
             * width or bank mapping makes two addresses share a cell, which
             * a spot check misses and this pass counts.
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

            /* Bandwidth: 4 MB written and read, timed on GPT0 at PCLKD. */
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

    /* Octo-SPI flash: the JEDEC ID over 1-1-1 SPI.  The EK-RA8P1's part reads
     * C2 86 3A: Macronix, 1.8 V octaflash family, 512 Mb. */
    {
        uint8_t id[3] = { 0, 0, 0 };
        int rc = tiku_ra8p1_xflash_read_id(id);

        tiku_uart_printf("xflash: rc=%d id = %x %x %x\n", rc,
                         (unsigned int)id[0], (unsigned int)id[1],
                         (unsigned int)id[2]);

        /* SFDP offset 0 holds the JESD216 signature "SFDP".  This is the
         * first transaction here with an address and dummy cycles, and only
         * a correct one returns those four bytes. */
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
             * Memory-mapped read of bytes 0 to 7, compared with the same
             * bytes read by a manual command; a wrong dummy count makes the
             * two differ.
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
                 * Write path on the last sector, away from the board's
                 * factory content at offset 0: erase to FF, program a
                 * pattern, read it back both ways.
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
                     * OPI: opi_enter() checks the mode against the device's
                     * factory SFDP, then a 4 KB mapped read is timed in OPI
                     * and in single-bit SPI.
                     *
                     * Bytes programmed in SPI read back pair-swapped in DOPI,
                     * as the device documents, so the check below compares
                     * against the swapped pattern.
                     */
                    {
                        volatile uint32_t *cyc =
                            (volatile uint32_t *)0xE0001004UL;
                        uint8_t o[8];
                        uint32_t t0, slow, fast;
                        unsigned n, okp = 0;
                        volatile uint32_t sink = 0;

                        /* DWT counts only once the trace block is powered:
                         * DEMCR.TRCENA, then CYCCNTENA.  With TRCENA clear
                         * the counter stays at zero. */
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

                            /* A 256 KB copy from mapped flash into SDRAM,
                             * as a boot-time model restore does it, timed in
                             * cycles. */
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
                            /* Bulk write: erase a sector, write 4 KB of a
                             * position-dependent pattern, which a shifted or
                             * duplicated chunk fails, and read it back
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

                            /* The NVM backend path a filesystem uses: 300
                             * bytes at uneven offsets, so the head, the
                             * aligned middle and the tail paths all run. */
                            {
                                struct tiku_nvm_backend *be =
                                    tiku_ra8p1_xflash_backend();
                                static uint8_t ub[300];
                                unsigned long q2;
                                unsigned off2;

                                for (q2 = 0; q2 < 300UL; q2++) {
                                    ub[q2] = (uint8_t)(q2 * 7UL + 3UL);
                                }
                                /* Offsets +5, +4 and +0 from a 64-aligned
                                 * base: odd, even and aligned starts. */
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
     * USB-HS bring-up.  On a host attach the hardware performs the bus reset
     * and the chirp handshake, and DVSTCTR0.RHST reports the negotiated
     * speed.  EP0 is serviced by the USB-HS interrupt.
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
        uint32_t last_su = 0xFFFFFFFFu;

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

            /*
             * Watches enumeration: polls the mass-storage pipes and, every
             * 250 ms, prints the device state when it has changed.
             *
             * Bounded at 20 s of DWT cycles at 240 MHz, time for a host to
             * enumerate.
             */
            {
                volatile uint32_t *cyc = (volatile uint32_t *)0xE0001004UL;
                const uint32_t limit = 240000000u / 4u;   /* 250 ms   */
                uint32_t t0 = *cyc, mark = *cyc;

                TIKU_REG32(0xE000EDFCUL) |= (1UL << 24);
                TIKU_REG32(0xE0001000UL) |= 1UL;

                while ((*cyc - t0) < (20u * 240000000u)) {
                    unsigned dv, sp, ln;
                    uint32_t nsu, nst;
                    uint16_t lr;

                    tiku_ra8p1_usbhs_msc_poll();

                    if ((*cyc - mark) < limit) {
                        continue;
                    }
                    mark = *cyc;

                    dv = (unsigned)tiku_ra8p1_usbhs_devstate();
                    sp = (unsigned)tiku_ra8p1_usbhs_speed();
                    tiku_ra8p1_usbhs_regs(r, 8u);
                    ln = (unsigned)(r[1] & 3u);
                    tiku_ra8p1_usbhs_ep0_stats(&nsu, &nst, &lr);
                    if (dv == last_dv && sp == last_sp && ln == last_ln &&
                        nsu == last_su) {
                        continue;
                    }
                    last_su = nsu;
                    tiku_uart_printf("usbhs: state=%s speed=%s lnst=%s"
                                     " addr=%u cfg=%u setup=%u stall=%u"
                                     " last=%x\n",
                                     dvname[dv < 5u ? dv : 4u],
                                     spname[sp < 3u ? sp : 0u], lnname[ln],
                                     (unsigned int)
                                         tiku_ra8p1_usbhs_address(),
                                     (unsigned int)
                                         tiku_ra8p1_usbhs_configured(),
                                     (unsigned int)nsu, (unsigned int)nst,
                                     (unsigned int)lr);
                    last_dv = dv;
                    last_sp = sp;
                    last_ln = ln;
                }
                (void)t;
            }
            {
                uint16_t tv[9];
                unsigned k, nt = tiku_ra8p1_usbhs_ep0_trace(0u, tv);

                for (k = 0; k < nt && k < 16u; k++) {
                    (void)tiku_ra8p1_usbhs_ep0_trace(k, tv);
                    tiku_uart_printf("usbhs: [%u] req=%x val=%x len=%u"
                                     " ctsq=%u spins=%u pre=%x post=%x\n",
                                     k, tv[0], tv[1], tv[2], tv[3],
                                     tv[6], tv[7], tv[8]);
                }
            }
            tiku_ra8p1_usbhs_regs(r, 8u);
            tiku_uart_printf("usbhs: final syscfg=%x syssts=%x dvstctr=%x"
                             " intsts0=%x frame=%u\n",
                             r[0], r[1], r[3], r[5], r[7]);
            /*
             * Summary lines: a bus reset seen (the device state left
             * Powered), high speed negotiated, and a running frame counter,
             * which advances only on SOF packets from the host.
             */
            {
                int reset_seen = (tiku_ra8p1_usbhs_devstate() !=
                                  TIKU_RA8P1_USBHS_DEV_POWERED);
                int hs = (tiku_ra8p1_usbhs_speed() ==
                          TIKU_RA8P1_USBHS_SPEED_HIGH);
                uint32_t nsu, nst;
                uint16_t lr;

                tiku_ra8p1_usbhs_ep0_stats(&nsu, &nst, &lr);
                tiku_uart_printf("usbhs: bus: reset=%s speed=%s"
                                 " sof=%s -> %s\n",
                                 reset_seen ? "yes" : "NO",
                                 hs ? "HIGH" : "not high",
                                 (r[7] != 0u) ? "running" : "NONE",
                                 (reset_seen && hs && r[7] != 0u)
                                   ? "PASS" : "incomplete");
                tiku_uart_printf("usbhs: enum: addr=%u cfg=%u setups=%u"
                                 " stalls=%u -> %s\n",
                                 (unsigned int)tiku_ra8p1_usbhs_address(),
                                 (unsigned int)tiku_ra8p1_usbhs_configured(),
                                 (unsigned int)nsu, (unsigned int)nst,
                                 (tiku_ra8p1_usbhs_address() != 0u &&
                                  tiku_ra8p1_usbhs_configured() != 0u)
                                   ? "ENUMERATED" : "incomplete");
                {
                    uint32_t c, rd, wr, bad;

                    tiku_ra8p1_usbhs_msc_stats(&c, &rd, &wr, &bad);
                    tiku_uart_printf("usbhs: msc: cbw=%u read=%u write=%u"
                                     " bad=%u hash(1MB)=%x -> %s\n",
                                     (unsigned int)c, (unsigned int)rd,
                                     (unsigned int)wr, (unsigned int)bad,
                                     (unsigned int)
                                         tiku_ra8p1_usbhs_msc_hash(2048u),
                                     (c > 0u && bad == 0u)
                                       ? "SERVING" : "incomplete");
                }
            }
        }
    }

    /*
     * Restores the model the last import left in flash into the staging
     * window before the disk is served, so a host reads back what it wrote
     * in the previous session.
     */
    {
        char mname[TIKU_STORE_NAME_MAX + 1u];
        uint32_t rms = 0, rlen = 0;

        if (tiku_ra8p1_store_restore(&rms, &rlen, mname)) {
            tiku_uart_printf("store: restored \"%s\" %u bytes in %u ms"
                             " (%u MB/s)\n", mname, (unsigned int)rlen,
                             (unsigned int)rms,
                             (unsigned int)(rms ? (rlen / 1048576UL)
                                                  * 1000UL / rms : 0));
        } else {
            tiku_uart_printf("store: no model in flash yet\n");
        }
    }

    /*
     * Nothing below writes the staging window or the flash slot: both hold
     * the restored model, and a test pattern written there would replace it
     * at every boot.  The store is exercised through the import path.
     */

    /*
     * Serves the disk from here on and does not return: a mass-storage device
     * that stops answering looks crashed to the host, which resets it.  Prints
     * the USB and MSC counters every 5 s and imports a model when the host
     * writes the commit record.
     */
    {
        volatile uint32_t *cyc = (volatile uint32_t *)0xE0001004UL;
        uint32_t mark = *cyc;
        uint32_t t0 = *cyc;
        uint32_t last_wr = 0xFFFFFFFFu;
        unsigned last_import_busy = 0u;
        static const char *const sname_done[7] = {
            "idle", "DONE", "bad magic", "bad length",
            "flash write failed", "verify failed", "busy"
        };

        tiku_uart_printf("usbhs: serving; EP0 is on the interrupt, MSC on"
                         " this loop\n");
        for (;;) {
            uint32_t c, rd, wr, bad, pk, st, iq, dv;

            /*
             * For the first 30 s each pass busy-waits 10 ms, the load a shell
             * or scheduler puts on this loop.  Enumeration has host-set
             * deadlines a polled EP0 would miss, so a device that enumerates
             * meanwhile shows EP0 runs on the interrupt.  The wait then stops,
             * since it also slows the MSC pump, which runs in this loop.
             */
            if ((*cyc - t0) < (30u * 240000000u)) {
                tiku_cpu_ra8p1_delay_us(10000u);
            }
            tiku_ra8p1_usbhs_msc_poll();
            /* One erase sector per turn, interleaved with serving the disk. */
            if (tiku_ra8p1_store_step(NULL) == 0 &&
                last_import_busy) {
                last_import_busy = 0u;
                tiku_uart_printf("store: import -> %s\n",
                                 sname_done[(unsigned)tiku_ra8p1_store_last()
                                            < 7u
                                   ? (unsigned)tiku_ra8p1_store_last() : 0u]);
            } else if (tiku_ra8p1_store_busy()) {
                last_import_busy = 1u;
            }

            if ((*cyc - mark) < (5u * 240000000u)) {
                continue;
            }
            mark = *cyc;
            tiku_ra8p1_usbhs_msc_stats(&c, &rd, &wr, &bad);
            tiku_ra8p1_usbhs_msc_out_stats(&pk, &st);
            tiku_ra8p1_usbhs_irq_stats(&iq, &dv);

            tiku_uart_printf("usbhs: irq=%u dvst=%u | addr=%u cfg=%u | cbw=%u"
                             " rd=%u wr=%u bad=%u outstall=%u\n",
                             (unsigned int)iq, (unsigned int)dv,
                             (unsigned int)tiku_ra8p1_usbhs_address(),
                             (unsigned int)tiku_ra8p1_usbhs_configured(),
                             (unsigned int)c, (unsigned int)rd,
                             (unsigned int)wr, (unsigned int)bad,
                             (unsigned int)st);
            if (wr != last_wr) {
                uint32_t wl = 0, wb = 0;

                (void)tiku_ra8p1_usbhs_msc_last_write(&wl, &wb);
                if (wl == tiku_ra8p1_store_commit_lba()) {
                    static const char *const sname[6] = {
                        "idle", "DONE", "bad magic", "bad length",
                        "flash write failed", "verify failed"
                    };
                    tiku_store_state_t r;

                    tiku_uart_printf("store: commit record seen, importing"
                                     " (the disk stays up)\n");
                    r = tiku_ra8p1_store_begin(wl, wb);
                    if (r != TIKU_STORE_BUSY) {
                        tiku_uart_printf("store: import -> %s\n",
                                         sname[(unsigned)r < 6u
                                               ? (unsigned)r : 0u]);
                    }
                }
            }
            last_wr = wr;
        }
    }

    /* Not reached: the serve loop above does not exit. */
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

#elif defined(PLATFORM_ESP32C61)

#include "arch/esp32c61/tiku_cpu_freq_boot_arch.h"
#include "arch/esp32c61/tiku_cpu_common.h"
#include "arch/esp32c61/tiku_uart_arch.h"
#include "arch/esp32c61/tiku_gpio_arch.h"
#include "arch/esp32c61/tiku_esp32c61_regs.h"
#include "arch/esp32c61/tiku_device_select.h"
#include "arch/esp32c61/tiku_irq_arch.h"
#include "arch/esp32c61/tiku_timer_arch.h"
#include "arch/esp32c61/tiku_flash_arch.h"
#include "hal/tiku_crit_hal.h"
#include "kernel/timers/tiku_crit.h"
#include "kernel/timers/tiku_htimer.h"

extern volatile uint32_t tiku_htimer_arch_isr_count;

/* RGB channel level, out of 255; it keeps the LED dim. */
#define TIKU_MIN_RGB_LEVEL  16U

/** @brief Print the ISA from misa: one letter per extension bit set. */
static void min_print_isa(uint32_t misa)
{
    tiku_uart_puts("isa=rv32");
    for (unsigned b = 0U; b < 26U; b++) {
        if (misa & (1UL << b)) {
            tiku_uart_putc((char)('a' + b));
        }
    }
    tiku_uart_printf("  misa=0x%lx\n", (unsigned long)misa);
}

/** @brief Busy-wait on SYSTIMER alone, so no tick is needed to end it. */
static void min_spin_us(unsigned long us)
{
    uint64_t t0 = tiku_cpu_esp32c61_systimer();

    while (tiku_cpu_esp32c61_systimer() - t0 <
           (uint64_t)us * (ESP32C61_SYSTIMER_HZ / 1000000UL)) {
    }
}

/**
 * @brief Test the tick, critical sections and the htimer against SYSTIMER.
 *
 * Every wait is bounded by SYSTIMER, never by the tick, so with a silent tick
 * each test still ends, printing FAIL.
 */
static void min_tick_tests(void)
{
    tiku_clock_arch_time_t a, b, c;
    uint64_t t0, t1;

    tiku_uart_printf("clic: config=0x%lx info=0x%lx (ctlbits=%u ids=%u) "
                     "mintthresh=0x%lx\n",
                     (unsigned long)TIKU_REG32(ESP32C61_CLIC_CONFIG),
                     (unsigned long)TIKU_REG32(ESP32C61_CLIC_INFO),
                     (unsigned)((TIKU_REG32(ESP32C61_CLIC_INFO) >> 21) & 0xFU),
                     (unsigned)(TIKU_REG32(ESP32C61_CLIC_INFO) & 0x1FFFU),
                     (unsigned long)ESP32C61_CSR_READ(ESP32C61_CSR_MINTTHRESH));

    tiku_clock_arch_init();
    tiku_htimer_arch_init();
    ESP32C61_CSR_SET(mstatus, ESP32C61_MSTATUS_MIE);

    /* Rate: 2 s of SYSTIMER time is 256 ticks; 255 to 257 passes. */
    a = tiku_clock_arch_time();
    min_spin_us(2000000UL);
    b = tiku_clock_arch_time();
    tiku_uart_printf("tick: %u ticks in 2 s of SYSTIMER (expect %u) %s\n",
                     (unsigned)(b - a), 2U * TIKU_CLOCK_ARCH_SECOND,
                     ((b - a) >= 255U && (b - a) <= 257U) ? "PASS" : "FAIL");

    /* A 40 ms masked window holds the count still, and the first tick after
     * it adds every tick the window hid. */
    a = tiku_clock_arch_time();
    tiku_crit_arch_mask_irqs(0U);
    min_spin_us(40000UL);
    b = tiku_clock_arch_time();
    tiku_crit_arch_unmask_irqs();
    min_spin_us(1000UL);
    c = tiku_clock_arch_time();
    tiku_uart_printf("crit: %u ticks inside a 40 ms masked window (expect 0), "
                     "%u after it (expect 5) %s\n",
                     (unsigned)(b - a), (unsigned)(c - a),
                     (b == a && (c - a) >= 5U && (c - a) <= 6U) ? "PASS"
                                                                : "FAIL");

    a = tiku_clock_arch_time();
    tiku_crit_arch_mask_irqs(TIKU_CRIT_PRESERVE_TICK);
    min_spin_us(40000UL);
    b = tiku_clock_arch_time();
    tiku_crit_arch_unmask_irqs();
    tiku_uart_printf("crit: %u ticks inside a window preserving the tick "
                     "(expect 5) %s\n", (unsigned)(b - a),
                     ((b - a) >= 5U && (b - a) <= 6U) ? "PASS" : "FAIL");

    /* The alarm: 2000 us ahead, then one already in the past. */
    for (unsigned k = 0U; k < 2U; k++) {
        uint32_t n0 = tiku_htimer_arch_isr_count;
        tiku_htimer_clock_t when = (tiku_htimer_clock_t)
            (tiku_htimer_arch_now() + (k == 0U ? 2000U : (uint16_t)-500));
        unsigned long spins = 0UL;

        t0 = tiku_cpu_esp32c61_systimer();
        tiku_htimer_arch_schedule(when);
        do {
            t1 = tiku_cpu_esp32c61_systimer();
        } while (tiku_htimer_arch_isr_count == n0 &&
                 t1 - t0 < 10UL * (ESP32C61_SYSTIMER_HZ / 1000UL) &&
                 ++spins != 0UL);
        tiku_uart_printf("htimer: %s alarm fired after %lu us (expect %s) %s\n",
                         k == 0U ? "+2000 us" : "past", (unsigned long)
                         ((t1 - t0) / (ESP32C61_SYSTIMER_HZ / 1000000UL)),
                         k == 0U ? "~2000" : "~0",
                         tiku_htimer_arch_isr_count != n0 ? "PASS" : "FAIL");
    }

    /* The kernel's wait sleeps in wfi until the tick ends it. */
    t0 = tiku_cpu_esp32c61_systimer();
    tiku_clock_arch_wait(TIKU_CLOCK_ARCH_SECOND);
    t1 = tiku_cpu_esp32c61_systimer();
    tiku_uart_printf("wait: %u ticks took %lu us (expect ~1000000)\n",
                     TIKU_CLOCK_ARCH_SECOND, (unsigned long)
                     ((t1 - t0) / (ESP32C61_SYSTIMER_HZ / 1000000UL)));

    {
        unsigned short f0 = tiku_clock_arch_fine();
        min_spin_us(100UL);
        unsigned short f1 = tiku_clock_arch_fine();
        tiku_uart_printf("fine: max=%d  %u -> %u after 100 us  spurious=%lu\n",
                         tiku_clock_arch_fine_max(), (unsigned)f0,
                         (unsigned)f1,
                         (unsigned long)tiku_esp32c61_irq_spurious());
    }
}

/** @brief Microseconds on SYSTIMER since @p t0. */
static unsigned long min_us_since(uint64_t t0)
{
    return (unsigned long)((tiku_cpu_esp32c61_systimer() - t0) /
                           (ESP32C61_SYSTIMER_HZ / 1000000UL));
}

/**
 * @brief Test flash erase and program on the scratch sector only.
 *
 * Reads go through the mapped window, so every check after a write also
 * tests the cache invalidate: a stale line shows the old bytes.
 */
static void min_flash_tests(void)
{
    static uint8_t pat[300];
    const uint8_t *w;
    uint32_t base = TIKU_FLASH_SCRATCH_ADDR;
    unsigned bad = 0U;
    uint64_t t0;
    int rc;

    t0 = tiku_cpu_esp32c61_systimer();
    rc = tiku_flash_init();
    tiku_uart_printf("flash: init rc=%d in %lu us  jedec=0x%lx\n", rc,
                     min_us_since(t0), (unsigned long)tiku_flash_jedec_id());
    if (rc != TIKU_FLASH_OK) {
        return;
    }
    w = tiku_flash_map(0UL);
    tiku_uart_printf("flash: offset 0 reads %02x %02x %02x %02x "
                     "(a factory image opens with e9)\n",
                     w[0], w[1], w[2], w[3]);

    t0 = tiku_cpu_esp32c61_systimer();
    rc = tiku_flash_erase_sector(base);
    w = tiku_flash_map(base);
    for (unsigned i = 0U; i < TIKU_FLASH_SECTOR_SIZE; i++) {
        bad += (w[i] != 0xFFU);
    }
    tiku_uart_printf("flash: erase rc=%d in %lu us, %u bytes not 0xff %s\n",
                     rc, min_us_since(t0), bad, (rc == 0 && bad == 0U)
                     ? "PASS" : "FAIL");

    /* An odd start and an odd length, from an odd source: every alignment
     * path the program call has. */
    for (unsigned i = 0U; i < sizeof pat; i++) {
        pat[i] = (uint8_t)(i * 7U + 3U);
    }
    t0 = tiku_cpu_esp32c61_systimer();
    rc = tiku_flash_program(base + 5U, &pat[1], 251U);
    bad = 0U;
    for (unsigned i = 0U; i < 251U; i++) {
        bad += (w[5U + i] != pat[1U + i]);
    }
    bad += (w[4] != 0xFFU) + (w[256] != 0xFFU);
    tiku_uart_printf("flash: program 251 B at +5 rc=%d in %lu us, %u wrong %s\n",
                     rc, min_us_since(t0), bad, (rc == 0 && bad == 0U)
                     ? "PASS" : "FAIL");

    /* Bits only clear: programming zeros over the pattern lands, ones do not
     * come back. */
    pat[0] = 0x00U;
    rc = tiku_flash_program(base + 5U, &pat[0], 1U);
    tiku_uart_printf("flash: clear-only program reads %02x (expect 00) %s\n",
                     w[5], (rc == 0 && w[5] == 0x00U) ? "PASS" : "FAIL");
    (void)tiku_flash_erase_sector(base);
}

int main(void)
{
    static const uint8_t rgb[4][3] = {
        {TIKU_MIN_RGB_LEVEL, 0U, 0U}, {0U, TIKU_MIN_RGB_LEVEL, 0U},
        {0U, 0U, TIKU_MIN_RGB_LEVEL}, {0U, 0U, 0U}};
    uint8_t mac[6];

    /* Watchdogs before anything that waits: a flash boot leaves them armed
     * and nothing in this image feeds them. */
    tiku_cpu_boot_esp32c61_init();
    tiku_uart_init();
    tiku_esp32c61_gpio_init_output(TIKU_BOARD_RGB_LED_GPIO);

    tiku_uart_puts("\n\n--- TikuOS minimal smoke test (ESP32-C61-DevKitC) ---\n");
    min_print_isa(ESP32C61_CSR_READ(misa));
    tiku_uart_printf("mvendorid=0x%lx marchid=0x%lx mimpid=0x%lx\n",
                     (unsigned long)ESP32C61_CSR_READ(mvendorid),
                     (unsigned long)ESP32C61_CSR_READ(marchid),
                     (unsigned long)ESP32C61_CSR_READ(mimpid));
    tiku_uart_printf("reset=0x%x (rom code %u)  watchdogs armed at entry=0x%x\n",
                     (unsigned int)tiku_cpu_esp32c61_reset_reason(),
                     (unsigned int)tiku_cpu_esp32c61_reset_code(),
                     (unsigned int)tiku_cpu_esp32c61_wdt_found());
    (void)tiku_cpu_esp32c61_unique_id(mac, sizeof mac);
    tiku_uart_printf("mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Steps through every rate the tree makes, from 160 MHz down to 10 MHz
     * and back to 160, measuring each and a 10 ms delay against SYSTIMER. */
    static const unsigned int ladder[] = {160U, 80U, 40U, 20U, 10U, 160U};
    for (unsigned k = 0U; k < sizeof ladder / sizeof ladder[0]; k++) {
        tiku_esp32c61_clock_t tree;
        uint64_t t0, t1;

        (void)tiku_cpu_freq_esp32c61_set(ladder[k]);
        tiku_cpu_esp32c61_clock_probe(&tree);
        t0 = tiku_cpu_esp32c61_systimer();
        tiku_cpu_esp32c61_delay_us(10000U);
        t1 = tiku_cpu_esp32c61_systimer();
        tiku_uart_printf("freq %u MHz: measured %lu Hz (fault=%d)  root=%u "
                         "cpu/%u ahb/%u apb/%u  delay_us(10000)=%lu us\n",
                         ladder[k], tiku_cpu_esp32c61_clock_get_hz(),
                         tiku_cpu_esp32c61_clock_has_fault(),
                         (unsigned)tree.root, (unsigned)tree.cpu_div,
                         (unsigned)tree.ahb_div, (unsigned)tree.apb_div,
                         (unsigned long)((t1 - t0) /
                                         (ESP32C61_SYSTIMER_HZ / 1000000UL)));
    }

    min_tick_tests();
    min_flash_tests();

    unsigned long clk = tiku_cpu_esp32c61_clock_get_hz();
    int           fault = tiku_cpu_esp32c61_clock_has_fault();

    /* Paced by the tick alone, asleep in wfi between ticks.  Each line also
     * prints SYSTIMER, an independent clock, to check the tick's rate. */
    uint32_t i = 0;
    tiku_clock_arch_time_t next = tiku_clock_arch_time();
    while (1) {
        uint64_t t = tiku_cpu_esp32c61_systimer();

        tiku_uart_printf(
            "TikuOS minimal: hello #%u  clk=%u Hz  fault=%d  uptime=%lu s  "
            "ticks=%lu  systimer=%lu ms\n",
            (unsigned int)i,
            (unsigned int)clk,
            fault,
            tiku_clock_arch_seconds(),
            (unsigned long)tiku_clock_arch_time(),
            (unsigned long)(t / (ESP32C61_SYSTIMER_HZ / 1000UL)));

        tiku_esp32c61_rgb_set(TIKU_BOARD_RGB_LED_GPIO, rgb[i & 3U][0],
                              rgb[i & 3U][1], rgb[i & 3U][2]);
        next += TIKU_CLOCK_ARCH_SECOND;
        while ((long)(tiku_clock_arch_time() - next) < 0) {
            __asm__ volatile ("wfi");
        }
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
    /* XOSC, PLL_SYS, CLK_SYS and CLK_PERI.  A step that times out falls
     * back to the 12 MHz XOSC, and the heartbeat then prints fault=1. */
    tiku_cpu_boot_rp2350_init();

    /* GP25: the user LED on a Pico 2.  On a Pico 2 W it is the CYW43's
     * WL_CS and lights nothing; the toggle still shows on a scope. */
    tiku_rp2350_gpio_init_output(25U);

    /* UART0 on GP0 (TX) / GP1 (RX) at TIKU_BOARD_UART_BAUD baud. */
    tiku_uart_init();

    /* 100 ms before the first message, for stale bytes from the FT232 or
     * picotool reset to clear. */
    tiku_cpu_rp2350_delay_ms(100);

    /* Two blank lines set the banner apart from earlier bytes on the
     * line. */
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
