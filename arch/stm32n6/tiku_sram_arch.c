/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sram_arch.c - STM32N6 internal SRAM banks.
 *
 * The boot ROM needs only the bank it loads the image into, so it leaves the
 * rest clock-gated and in reset; this claims them for the running system.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_sram_arch.h"
#include "tiku_stm32n6_regs.h"

#define RCC_MEMRSTR     (STM32N6_RCC_BASE + 0x20CU)
#define RCC_MEMENR      (STM32N6_RCC_BASE + 0x24CU)
#define RCC_AHB2ENR     (STM32N6_RCC_BASE + 0x254U)
#define RCC_AHB2ENR_RAMCFGEN    (1UL << 12)

/* RAMCFG governs the per-bank shutdown. Banks 3..6 back the NPU and come out
 * of reset powered down, so clearing SRAMSD is what actually makes them
 * answer; a bank left shut down reads as zero and swallows writes. */
#define RAMCFG_BASE     0x42023000UL
#define RAMCFG_CR(bank)     (RAMCFG_BASE + (0x80UL * ((bank) - 1U)))
#define RAMCFG_ERKEYR(bank) (RAMCFG_CR(bank) + 0x28U)
#define RAMCFG_CR_SRAMSD    (1UL << 20)
#define RAMCFG_ERKEY_1      0xCAUL
#define RAMCFG_ERKEY_2      0x53UL

/* AXISRAM3..6 are bits 0..3 and AXISRAM1/2 bits 7/8, an ordering that comes
 * from the bank numbering rather than the address map. AHBSRAM1/2 (bits 4/5)
 * are claimed too; they are small and sit in a different address range. */
#define SRAM_BANK_MASK  ((1UL << 0) | (1UL << 1) | (1UL << 2) | (1UL << 3) | \
                         (1UL << 4) | (1UL << 5) | (1UL << 7) | (1UL << 8))

/** @brief RCC_MEMENR as read back after the last enable. */
static uint32_t sram_enabled_mask;

/** @brief Let a bank's supply settle after its shutdown bit is lifted. */
static void sram_settle(void) {
    for (volatile unsigned i = 0U; i < 200U; i++) {
    }
}

void tiku_stm32n6_sram_init(void) {
    /* Clock first, then release the reset: a bank still in reset ignores the
     * clock, and one clocked but reset answers reads as zero. */
    TIKU_REG32(RCC_MEMENR)  |= SRAM_BANK_MASK;
    (void)TIKU_REG32(RCC_MEMENR);

    TIKU_REG32(RCC_MEMRSTR) &= ~SRAM_BANK_MASK;
    (void)TIKU_REG32(RCC_MEMRSTR);

    /* RAMCFG has to be clocked before its registers accept a write. */
    TIKU_REG32(RCC_AHB2ENR) |= RCC_AHB2ENR_RAMCFGEN;
    (void)TIKU_REG32(RCC_AHB2ENR);

    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    /* Banks 1 and 2 are already awake -- the ROM ran from bank 2 -- so only
     * the NPU-side banks need lifting out of shutdown. */
    for (unsigned bank = 3U; bank <= 6U; bank++) {
        TIKU_REG32(RAMCFG_ERKEYR(bank)) = RAMCFG_ERKEY_1;
        TIKU_REG32(RAMCFG_ERKEYR(bank)) = RAMCFG_ERKEY_2;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");

        TIKU_REG32(RAMCFG_CR(bank)) &= ~RAMCFG_CR_SRAMSD;
        sram_settle();
    }

    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    sram_enabled_mask = TIKU_REG32(RCC_MEMENR);
}

uint32_t tiku_stm32n6_sram_enabled_mask(void) {
    return sram_enabled_mask;
}

#if defined(TIKU_N6_SRAM_PROBE)

#include "tiku_uart_arch.h"

/* One write/read per 64 KB is enough to find a bank edge, and the probe prints
 * a character per page before touching it, so a bus fault that wedges the core
 * is located by counting the characters that made it out. */
#define PROBE_STEP      (64UL * 1024UL)
#define PROBE_PATTERN(a) ((uint32_t)(a) ^ 0xA5A5A5A5UL)

/* The array as measured: everything except the ROM's kept context and traces
 * and the image window itself. The top bound is where a write bus-faults, so
 * these ranges are the whole of what the part actually backs. */
static const struct {
    uint32_t    lo;
    uint32_t    hi;
    const char *tag;
} probe_ranges[] = {
    { 0x34000000UL, 0x34100000UL, "below ROM data" },
    { 0x34110000UL, 0x34180000UL, "ROM reserved  " },
    { 0x341C0000UL, 0x343C0000UL, "arena         " },
};

/** @brief Drain the UART so a print survives the access that follows it. */
static void probe_drain(void) {
    for (volatile unsigned long i = 0UL; i < 20000UL; i++) {
    }
}

/**
 * @brief Write, read back and re-check a unique word per 64 KB page.
 *
 * The second pass is what catches a bank that aliases another: a mirrored page
 * loses its own pattern when the page it shadows is written later.
 */
void tiku_stm32n6_sram_probe(void) {
    tiku_uart_printf("sram: MEMENR %08lx MEMRSTR %08lx AHB2ENR %08lx\n",
                     (unsigned long)TIKU_REG32(RCC_MEMENR),
                     (unsigned long)TIKU_REG32(RCC_MEMRSTR),
                     (unsigned long)TIKU_REG32(RCC_AHB2ENR));
    for (unsigned bank = 3U; bank <= 6U; bank++) {
        tiku_uart_printf("sram: RAMCFG%u CR %08lx\n", bank,
                         (unsigned long)TIKU_REG32(RAMCFG_CR(bank)));
    }

    for (unsigned r = 0U; r < (sizeof(probe_ranges) / sizeof(probe_ranges[0]));
         r++) {
        tiku_uart_printf("sram: %s %08lx..%08lx w ",
                         probe_ranges[r].tag,
                         (unsigned long)probe_ranges[r].lo,
                         (unsigned long)probe_ranges[r].hi);
        probe_drain();

        for (uint32_t a = probe_ranges[r].lo; a < probe_ranges[r].hi;
             a += PROBE_STEP) {
            /* The character goes out before the next access, so a page that
             * bus-faults is found by counting what got out. */
            volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)a;
            *p = PROBE_PATTERN(a);
            __asm__ volatile ("dsb" ::: "memory");
            uint32_t got = *p;
            tiku_uart_putc((got == PROBE_PATTERN(a)) ? '.'
                           : ((got == 0UL) ? 'z' : '?'));
            probe_drain();
        }
        tiku_uart_puts("\n");
    }

    for (unsigned r = 0U; r < (sizeof(probe_ranges) / sizeof(probe_ranges[0]));
         r++) {
        tiku_uart_printf("sram: %s alias-recheck  r ", probe_ranges[r].tag);
        for (uint32_t a = probe_ranges[r].lo; a < probe_ranges[r].hi;
             a += PROBE_STEP) {
            const volatile uint32_t *p = (const volatile uint32_t *)(uintptr_t)a;
            tiku_uart_putc((*p == PROBE_PATTERN(a)) ? '.' : 'A');
        }
        tiku_uart_puts("\n");
    }
}

#endif /* TIKU_N6_SRAM_PROBE */
