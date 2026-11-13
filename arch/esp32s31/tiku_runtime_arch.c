/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_runtime_arch.c - S31 crystal clocks, CLIC, workers and kernel timers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_runtime_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_uart_arch.h"
#include "tiku_watchdog_arch.h"
#include <hal/tiku_cpu.h>
#include <hal/tiku_crit_hal.h>
#include <hal/tiku_wake_hal.h>
#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_crit.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/threads/tiku_thread.h>
#include <stddef.h>
#include <reent.h>
#include <string.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define CLIC(i) (0x10801000u + (i) * 4u)
#define TIMER 0x20399000u
#define CLOCK 0x20587000u
#define TICK_IRQ 17u
#define ALARM_IRQ 18u
#define SWITCH_IRQ 3u
#define UART_IRQ 19u
#define COUNTER_MASK ((1ULL << 52) - 1u)

volatile uint8_t tiku_s31_in_trap;
static struct _reent workers[TIKU_THREADS_MAX], interrupt_reent;
static struct _reent *kernel_reent;
static uint8_t reent_ready[TIKU_THREADS_MAX];
static uint8_t rx_ring[1024];
static volatile uint16_t rx_head, rx_tail, rx_overruns;
static unsigned nesting;
static uint32_t outer_state;
static volatile uint32_t ticks, timer_irqs;
static uint64_t deadline, seconds_epoch, tick_epoch;
static unsigned long seconds_base;
static uint32_t saved_ie[2];
extern char __global_pointer$;
extern uint32_t *tiku_thread_switch(uint32_t *frame);

/** @brief Mask machine interrupts and return their previous status. */
static uint32_t irq_save(void)
{
    uint32_t state;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r" (state) :: "memory");
    return state;
}

/** @brief Restore the saved machine interrupt enable bit. */
static void irq_restore(uint32_t state)
{
    if (state & 8u) {
        __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    }
}

void tiku_uart_putc(char c)
{
    unsigned remaining = 1000000u;
    while (((REG(0x2038a01cu) >> 16) & 255u) >= 127u && --remaining) { }
    if (remaining) REG(0x2038a000u) = (uint8_t)c;
}

void tiku_s31_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') tiku_uart_putc('\r');
        tiku_uart_putc(*s++);
    }
}

void tiku_s31_report(const char *name, uint32_t value)
{
    tiku_s31_puts(name);
    tiku_s31_puts("=0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        tiku_uart_putc("0123456789abcdef"[(value >> shift) & 15u]);
    tiku_s31_puts("\n");
}

void tiku_s31_fatal(const char *reason)
{
    (void)irq_save();
    tiku_s31_puts("FAIL S31 ");
    tiku_s31_puts(reason);
    tiku_s31_puts("\n");
    for (;;) __asm__ volatile ("wfi");
}

uint64_t tiku_s31_time(void)
{
    uint32_t state = irq_save();
    REG(TIMER + 4u) = 1u << 30;
    for (unsigned i = 0; i < 4096u; i++) {
        if (REG(TIMER + 4u) & (1u << 29)) {
            uint32_t lo = REG(TIMER + 0x44u);
            uint32_t hi = REG(TIMER + 0x40u) & 0xfffffu;
            irq_restore(state);
            return ((uint64_t)hi << 32) | lo;
        }
    }
    tiku_s31_fatal("timer snapshot");
}

/** @brief Arm a comparator at a 52-bit absolute deadline with IRQs masked. */
static void alarm_arm(unsigned index, uint64_t at)
{
    uint32_t enable = 1u << (24u - index);
    REG(TIMER) &= ~enable;
    REG(TIMER + 0x6cu) = 1u << index;
    REG(TIMER + 0x1cu + index * 8u) = (uint32_t)(at >> 32) & 0xfffffu;
    REG(TIMER + 0x20u + index * 8u) = (uint32_t)at;
    REG(TIMER + 0x50u + index * 4u) = 1;
    REG(TIMER) |= enable;
}

void tiku_s31_runtime_init(void)
{
    (void)irq_save();
    /* The application owns watchdog policy after the bootloader handoff. */
    REG(0x20801018u) = 0x50d83aa1u;
    REG(0x20801000u) &= ~((1u << 31) | (1u << 12));
    REG(0x20801018u) = 0;
    if (REG(0x20801000u) & ((1u << 31) | (1u << 12)))
        tiku_s31_fatal("boot watchdog handoff");
    /* CPU0 owns the runtime; stop HP1 and disable LP execution. */
    REG(0x207041ecu) = (REG(0x207041ecu) & ~(255u << 16)) | (0x86u << 16);
    REG(0x20700008u) |= 1u;
    if (((REG(0x207041ecu) >> 16) & 255u) != 0x86u ||
        !(REG(0x20700008u) & 1u)) tiku_s31_fatal("secondary core control");
    kernel_reent = _impure_ptr;
    _REENT_INIT_PTR(&interrupt_reent);
    for (unsigned i = 0; i < 48; i++) REG(CLIC(i)) &= ~256u;
    REG(0x10800000u) = (REG(0x10800000u) & ~30u) | 6u;
    REG(0x10800008u) = 0;
    /* XTAL root and integer divide-by-one CPU, memory, system and APB. */
    REG(CLOCK + 4u) &= ~0x3fffu;
    REG(CLOCK + 8u) &= ~1u;
    REG(CLOCK + 12u) &= ~0x3fffu;
    REG(CLOCK + 16u) &= ~0x3fffu;
    REG(CLOCK) &= ~3u;
    REG(CLOCK + 0x14u) |= 1u;
    unsigned i;
    for (i = 0; i < 100000u && (REG(CLOCK + 0x14u) & 1u); i++) { }
    if (i == 100000u) tiku_s31_fatal("clock update");
    REG(CLOCK + 0x120u) = 0x13u;
    REG(CLOCK + 0x120u) = 0x11u;
    REG(TIMER) = 0xc0000000u;
    REG(TIMER + 0x64u) = 0;
    REG(TIMER + 0x34u) = 0;
    REG(TIMER + 0x38u) = 0;
    tiku_watchdog_arch_off();
}

/** @brief Handle a machine trap with the interrupt C-library state selected. */
static uint32_t *dispatch(uint32_t *frame)
{
    uint32_t cause = frame[S31_FRAME_CAUSE / 4];
    unsigned id = cause & 4095u;
    if (!(cause & 0x80000000u)) {
        if (id == 11u) {
            /* The ECALL probe requires the trap frame to restore these. */
            __asm__ volatile ("fmv.w.x ft0, zero\ncsrw fcsr, zero" ::: "ft0");
            frame[S31_FRAME_PC / 4] += 4;
            return frame;
        }
        tiku_s31_report("mcause", cause);
        tiku_s31_report("mepc", frame[S31_FRAME_PC / 4]);
        tiku_s31_fatal("exception");
    }
    if (id == UART_IRQ) {
        uint32_t raw = REG(0x2038a004u);
        if ((raw & 28u) && rx_overruns != UINT16_MAX) rx_overruns++;
        for (unsigned i = 0; i < 128u && (REG(0x2038a01cu) & 255u); i++) {
            uint8_t byte = REG(0x2038a000u);
            uint16_t next = (rx_head + 1u) & 1023u;
            if (next != rx_tail) {
                rx_ring[rx_head] = byte;
                rx_head = next;
            } else if (rx_overruns != UINT16_MAX) rx_overruns++;
        }
        REG(0x2038a010u) = 29u;
        return frame;
    }
    if (id == SWITCH_IRQ) {
        REG(CLIC(SWITCH_IRQ)) &= ~1u;
        return tiku_thread_switch(frame);
    }
    if (id == TICK_IRQ) {
        REG(TIMER + 0x6cu) = 1;
        timer_irqs++;
        uint64_t elapsed = (tiku_s31_time() - deadline) & COUNTER_MASK;
        if (elapsed < (1ULL << 51)) {
            uint32_t due = (uint32_t)(elapsed / 125000u) + 1u;
            ticks += due;
            deadline = (deadline + (uint64_t)due * 125000u) & COUNTER_MASK;
        }
        alarm_arm(0, deadline);
        tiku_sched_notify();
        return frame;
    }
    if (id == ALARM_IRQ) {
        REG(TIMER) &= ~(1u << 23);
        REG(TIMER + 0x6cu) = 2;
        tiku_htimer_run_next();
        return frame;
    }
    tiku_s31_report("irq", id);
    tiku_s31_fatal("unclaimed interrupt");
}

uint32_t *tiku_s31_trap(uint32_t *frame)
{
    tiku_s31_in_trap = 1;
    _impure_ptr = &interrupt_reent;
    frame = dispatch(frame);
    tiku_thread_t *thread = tiku_thread_self();
    _impure_ptr = thread == NULL ? kernel_reent : &workers[thread->slot];
    tiku_s31_in_trap = 0;
    return frame;
}

void tiku_s31_reent_init(uint8_t slot)
{
    if (slot >= TIKU_THREADS_MAX) tiku_s31_fatal("worker reentrancy slot");
    if (reent_ready[slot]) _reclaim_reent(&workers[slot]);
    _REENT_INIT_PTR(&workers[slot]);
    reent_ready[slot] = 1;
}

/** @brief Return the C-library state selected for the running context. */
struct _reent *__getreent(void) { return _impure_ptr; }

void tiku_atomic_enter(void)
{
    uint32_t state = irq_save();
    if (nesting++ == 0) outer_state = state;
}

void tiku_atomic_exit(void)
{
    if (!nesting) tiku_s31_fatal("atomic underflow");
    if (--nesting == 0) irq_restore(outer_state);
}

void tiku_cpu_irq_enable(void) { irq_restore(8); }
void tiku_cpu_irq_disable(void) { (void)irq_save(); }

/** @brief Wait for an enabled interrupt without changing clock domains. */
static void idle(void) { __asm__ volatile ("wfi" ::: "memory"); }

tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode)
{
    return mode == TIKU_CPU_IDLE_LIGHT ? idle : NULL;
}

int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode)
{
    return mode == TIKU_CPU_IDLE_LIGHT;
}

/** @brief Enable the local software interrupt used by the worker scheduler. */
void tiku_thread_arch_boot(void) { REG(CLIC(SWITCH_IRQ)) = 0x3fc20100u; }
/** @brief Request a worker switch when machine interrupts are enabled. */
void tiku_thread_arch_pend(void) { REG(CLIC(SWITCH_IRQ)) |= 1u; }
/** @brief Read the wrapping CPU cycle counter. */
uint32_t tiku_thread_arch_cycles(void)
{
    uint32_t value;
    __asm__ volatile ("csrr %0, mcycle" : "=r" (value));
    return value;
}

/** @brief Create an integer and FPU frame on a 16-byte-aligned worker stack. */
uint32_t *tiku_thread_arch_frame_init(uint32_t *top, void (*entry)(void *),
                                    void *arg, void (*exit_fn)(void))
{
    uintptr_t aligned = (uintptr_t)top & ~(uintptr_t)15u;
    uint32_t *f = (uint32_t *)(aligned - S31_FRAME_SIZE);
    for (unsigned i = 0; i < S31_FRAME_SIZE / 4; i++) f[i] = 0;
    f[1] = (uintptr_t)exit_fn;
    f[2] = aligned;
    f[3] = (uintptr_t)&__global_pointer$;
    f[10] = (uintptr_t)arg;
    f[S31_FRAME_PC / 4] = (uintptr_t)entry;
    f[S31_FRAME_STATUS / 4] = 0x3880u;
    f[S31_FRAME_CAUSE / 4] = 0xb8000000u;
    return f;
}

void tiku_clock_arch_init(void)
{
    ticks = timer_irqs = 0;
    tick_epoch = seconds_epoch = tiku_s31_time();
    seconds_base = 0;
    deadline = (seconds_epoch + 125000u) & COUNTER_MASK;
    REG(0x20585084u) = TICK_IRQ;
    REG(CLIC(TICK_IRQ)) = 0x5fc00100u;
    REG(TIMER + 0x64u) |= 1u;
    alarm_arm(0, deadline);
}

tiku_clock_arch_time_t tiku_clock_arch_time(void) { return ticks; }
uint32_t tiku_s31_timer_interrupts(void) { return timer_irqs; }
unsigned long tiku_clock_arch_seconds(void)
{
    uint32_t state = irq_save();
    unsigned long value = seconds_base +
        (unsigned long)(((tiku_s31_time() - seconds_epoch) & COUNTER_MASK) / 16000000u);
    irq_restore(state);
    return value;
}
void tiku_clock_arch_set_seconds(unsigned long seconds)
{
    uint32_t state = irq_save();
    seconds_epoch = tiku_s31_time();
    seconds_base = seconds;
    irq_restore(state);
}
void tiku_clock_arch_delay(unsigned int us)
{
    uint64_t start = tiku_s31_time();
    while (((tiku_s31_time() - start) & COUNTER_MASK) < (uint64_t)us * 16u) { }
}
void tiku_clock_arch_wait(tiku_clock_arch_time_t count)
{
    while (count--) tiku_clock_arch_delay(7813);
}
unsigned short tiku_clock_arch_fine(void)
{
    return (unsigned short)((((tiku_s31_time() - tick_epoch) & COUNTER_MASK) % 125000u) / 2u);
}
int tiku_clock_arch_fine_max(void) { return 62500; }
unsigned char tiku_clock_arch_fault(void) { return TIKU_CLOCK_ARCH_FAULT_NONE; }

void tiku_htimer_arch_init(void)
{
    REG(0x20585088u) = ALARM_IRQ;
    REG(CLIC(ALARM_IRQ)) = 0x5fc00100u;
    REG(TIMER + 0x64u) |= 2u;
}
tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)(tiku_s31_time() / 16u);
}
void tiku_htimer_arch_schedule(tiku_htimer_clock_t at)
{
    uint32_t state = irq_save();
    uint64_t now = tiku_s31_time();
    int16_t delta = (int16_t)(at - (tiku_htimer_clock_t)(now / 16u));
    alarm_arm(1, (now + (delta > 4 ? (uint32_t)delta * 16u : 64u)) & COUNTER_MASK);
    irq_restore(state);
}

void tiku_crit_arch_mask_irqs(uint8_t preserve)
{
    uint32_t state = irq_save();
    saved_ie[0] = saved_ie[1] = 0;
    for (unsigned i = 0; i < 48; i++) {
        uint32_t value = REG(CLIC(i));
        if (value & 256u) saved_ie[i / 32] |= 1u << (i % 32);
        if ((i == TICK_IRQ && (preserve & TIKU_CRIT_PRESERVE_TICK)) ||
            (i == ALARM_IRQ && (preserve & TIKU_CRIT_PRESERVE_HTIMER))) continue;
        REG(CLIC(i)) = value & ~256u;
    }
    irq_restore(state);
}
void tiku_crit_arch_unmask_irqs(void)
{
    uint32_t state = irq_save();
    for (unsigned i = 0; i < 48; i++) {
        uint32_t value = REG(CLIC(i)) & ~256u;
        if (saved_ie[i / 32] & (1u << (i % 32))) value |= 256u;
        REG(CLIC(i)) = value;
    }
    irq_restore(state);
}

void tiku_uart_init(void)
{
    uint32_t state = irq_save();
    REG(CLOCK + 0x88u) = 0x83u;
    REG(0x2038a00cu) = 0;
    REG(0x2038a014u) = 347u | (3u << 20);
    REG(0x2038a020u) = 0x1cu;
    REG(0x2038a024u) = (REG(0x2038a024u) & ~255u) | 1u;
    REG(0x2038a098u) = 1;
    unsigned wait;
    for (wait = 0; wait < 100000u && (REG(0x2038a098u) & 1u); wait++) { }
    if (wait == 100000u) tiku_s31_fatal("UART update");
    /* UART0's IOMUX function on the board-reserved GPIO58/59 pads. */
    REG(0x205820e8u) &= ~(7u << 12);
    REG(0x205820ecu) = (REG(0x205820ecu) & ~(7u << 12)) | (1u << 9);
    REG(0x2058331cu) &= ~(1u << 9);
    rx_head = rx_tail = rx_overruns = 0;
    REG(0x20585024u) = UART_IRQ;
    REG(0x2038a010u) = UINT32_MAX;
    REG(CLIC(UART_IRQ)) = 0x5fc00100u;
    REG(0x2038a00cu) = 29u;
    irq_restore(state);
}
uint8_t tiku_uart_rx_ready(void) { return rx_head != rx_tail; }
int tiku_uart_getc(void)
{
    uint32_t state = irq_save();
    int value = -1;
    if (rx_head != rx_tail) {
        value = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1u) & 1023u;
    }
    irq_restore(state);
    return value;
}
uint16_t tiku_uart_overrun_count(void) { return rx_overruns; }
void tiku_uart_overrun_reset(void)
{
    uint32_t state = irq_save();
    rx_overruns = 0;
    irq_restore(state);
}
