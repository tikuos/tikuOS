/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * diagnostic.c - S31 SRAM, machine trap and software interrupt probes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define UART_FIFO 0x2038a000u
#define UART_STATUS 0x2038a01cu
#define CLIC_CONFIG 0x10800000u
#define CLIC_THRESHOLD 0x10800008u
#define CLIC_CTRL(id) (0x10801000u + 4u * (id))
#define PROBE_IRQ 3u
#define TIMER_IRQ 17u
#define TIMER_BASE 0x20399000u
#define TIMER_CLOCK 0x20587120u
#define TIMER_ROUTE 0x20585084u

static volatile uint32_t memory_probe[1024];
static volatile uint32_t exceptions;
static volatile uint32_t interrupts;
static volatile uint32_t timer_interrupts;
static uint32_t timer_deadline;

/** @brief Return zero when an ECALL preserves the probe's integer registers. */
extern uint32_t tiku_s31_context_probe(void);

/** @brief Write a character through the ROM-configured UART0 FIFO. */
static void putc_uart(char c)
{
    unsigned timeout = 1000000u;
    while (((REG(UART_STATUS) >> 16) & 255u) >= 127u && --timeout) {
    }
    if (timeout) {
        REG(UART_FIFO) = (uint8_t)c;
    }
}

/** @brief Write a terminated string with CRLF line endings. */
static void puts_uart(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            putc_uart('\r');
        }
        putc_uart(*s++);
    }
}

/** @brief Write a named 32-bit hexadecimal observation. */
static void report(const char *name, uint32_t value)
{
    puts_uart(name);
    puts_uart("=0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        putc_uart("0123456789abcdef"[(value >> shift) & 15u]);
    }
    puts_uart("\n");
}

/** @brief Read the low counter word; return UINT32_MAX on snapshot timeout. */
static uint32_t timer_now(void)
{
    REG(TIMER_BASE + 4u) = 1u << 30;
    for (unsigned i = 0; i < 4096u; i++) {
        if (REG(TIMER_BASE + 4u) & (1u << 29)) {
            return REG(TIMER_BASE + 0x44u);
        }
    }
    return UINT32_MAX;
}

/** @brief Arm comparator zero at the diagnostic's absolute deadline. */
static void timer_arm(void)
{
    REG(TIMER_BASE) &= ~(1u << 24);
    REG(TIMER_BASE + 0x6cu) = 1;
    REG(TIMER_BASE + 0x1cu) = 0;
    REG(TIMER_BASE + 0x20u) = timer_deadline;
    REG(TIMER_BASE + 0x50u) = 1;
    REG(TIMER_BASE) |= 1u << 24;
}

/** @brief Handle diagnostic ECALL and software IRQ; halt on other traps. */
uint32_t tiku_s31_diagnostic_trap(uint32_t cause, uint32_t pc, uint32_t value)
{
    if ((cause & 0x80000000u) && (cause & 4095u) == TIMER_IRQ) {
        REG(TIMER_BASE + 0x6cu) = 1;
        timer_interrupts++;
        timer_deadline += 16000u;
        timer_arm();
        return pc;
    }
    if ((cause & 0x80000000u) && (cause & 4095u) == PROBE_IRQ) {
        REG(CLIC_CTRL(PROBE_IRQ)) &= ~1u;
        interrupts++;
        return pc;
    }
    if (!(cause & 0x80000000u) && (cause & 4095u) == 11u) {
        exceptions++;
        return pc + 4u;
    }
    report("FAIL mcause", cause);
    report("mepc", pc);
    report("mtval", value);
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/** @brief Exercise low SRAM and machine trap delivery without flash writes. */
void tiku_s31_diagnostic(void)
{
    uint32_t value, errors = 0, context_errors = 0;

    /* Leave the ROM loader time to finish its serial response. */
    for (volatile unsigned i = 0; i < 1000000u; i++) {
        __asm__ volatile ("nop");
    }
    puts_uart("TIKU S31 RAM diagnostic v2\n");
    __asm__ volatile ("csrr %0, mhartid" : "=r" (value));
    report("hart", value);
    __asm__ volatile ("csrr %0, misa" : "=r" (value));
    report("misa", value);
    report("rom_chip", REG(0x2f800010u));
    report("rom_eco", REG(0x2f800014u));

    for (unsigned i = 0; i < 1024; i++) {
        if (memory_probe[i] != 0) {
            errors++;
        }
        memory_probe[i] = 0xa5a50000u ^ i;
    }
    for (unsigned i = 0; i < 1024; i++) {
        if (memory_probe[i] != (0xa5a50000u ^ i)) {
            errors++;
        }
    }
    report("sram_errors", errors);
    for (unsigned i = 0; i < 100; i++) {
        context_errors += tiku_s31_context_probe();
    }
    report("ecall_count", exceptions);
    report("context_errors", context_errors);

    for (unsigned i = 0; i < 48; i++) {
        REG(CLIC_CTRL(i)) &= ~256u;
    }
    REG(CLIC_CONFIG) = (REG(CLIC_CONFIG) & ~30u) | 6u;
    REG(CLIC_THRESHOLD) = 0;
    REG(CLIC_CTRL(PROBE_IRQ)) = 0x3fc20100u;
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    for (unsigned i = 0; i < 100; i++) {
        unsigned timeout = 1000000u;
        REG(CLIC_CTRL(PROBE_IRQ)) |= 1u;
        while (interrupts <= i && --timeout) {
        }
        if (!timeout) {
            break;
        }
    }
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
    REG(CLIC_CTRL(PROBE_IRQ)) &= ~256u;
    report("software_irq_count", interrupts);

    /* Reset SYSTIMER, select XTAL/2.5 and enable its bus and source clocks. */
    REG(TIMER_CLOCK) = 0x13u;
    REG(TIMER_CLOCK) = 0x11u;
    REG(TIMER_BASE) = 0xc0000000u;
    REG(TIMER_BASE + 0x34u) = 0;
    REG(TIMER_BASE + 0x6cu) = 1;
    REG(TIMER_ROUTE) = TIMER_IRQ;
    REG(CLIC_CTRL(TIMER_IRQ)) = 0x3fc00100u;
    REG(TIMER_BASE + 0x64u) = 1;
    puts_uart("TIMER begin\n");
    value = timer_now();
    if (value == UINT32_MAX) {
        errors++;
    }
    timer_deadline = value + 16000u;
    timer_arm();
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    for (volatile unsigned timeout = 0;
         timer_interrupts < 1000u && timeout < 20000000u; timeout++) {
        __asm__ volatile ("nop");
    }
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
    REG(TIMER_BASE + 0x64u) = 0;
    REG(TIMER_BASE) &= ~(1u << 24);
    REG(CLIC_CTRL(TIMER_IRQ)) &= ~256u;
    REG(TIMER_ROUTE) = 0;
    puts_uart("TIMER end\n");
    report("timer_irq_count", timer_interrupts);
    puts_uart(!errors && !context_errors && exceptions == 100u && interrupts == 100u &&
              timer_interrupts == 1000u ?
              "PASS S31 RAM diagnostic\n" : "FAIL S31 RAM diagnostic\n");
}
