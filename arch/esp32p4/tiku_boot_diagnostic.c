/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_boot_diagnostic.c - Storage-free P4 SRAM, CLIC and USB checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include "tiku_fault_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_boot_arch.h"
#include "tiku_clock_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_alarm_arch.h"
#include "tiku_watchdog_arch.h"
#include <services/console/tiku_usb_serial_jtag.h>
#include <TikuBench/tests/esp32p4/test_flash.h>

extern int tiku_p4_irq_probe(uint32_t duration_cycles);
static volatile uint32_t initialized = 0x54494b55u;
static volatile uint32_t zeroed[1024];
static uint8_t output[1536];
static size_t queued, sent;
static uint32_t memory_ok, cpu_mhz;
static volatile unsigned alarm_calls, alarm_failed;

/** @brief Queue a bounded string without accessing the USB peripheral. */
static void append(const char *text)
{
    while (*text && queued < sizeof(output)) {
        output[queued++] = (uint8_t)*text++;
    }
}

/** @brief Append eight hexadecimal digits with a prefix. */
static void hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    append("0x");
    for (shift = 28; shift >= 0 && queued < sizeof(output); shift -= 4) {
        output[queued++] = (uint8_t)digits[(value >> shift) & 15u];
    }
}

/** @brief Offer queued bytes without waiting for an absent or stalled host. */
static void pump(void)
{
    sent += tiku_usb_serial_jtag_write(output + sent, queued - sent);
    if (sent == queued) {
        sent = queued = 0;
        tiku_usb_serial_jtag_poll();
    }
}

/** @brief Read the wrapping CPU cycle counter. */
static uint32_t cycles(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}

/** @brief Measure CPU cycles against 10 ms of the crystal-derived timer. */
static uint32_t measure_cpu(void)
{
    uint64_t start, now;
    uint32_t before, tries;
    if (tiku_p4_systimer_read(&start) != 0) {
        return 0;
    }
    before = cycles();
    for (tries = 0; tries < 1000000u; tries++) {
        if (tiku_p4_systimer_read(&now) != 0) {
            return 0;
        }
        if (now - start >= 160000u) {
            return (cycles() - before + 5000u) / 10000u;
        }
    }
    return 0;
}

/** @brief Queue identity, SRAM results and monotonic interrupt counters. */
static void identity(uint32_t count)
{
    append(memory_ok ? "PASS: data, bss, SRAM read/write\r\n"
                     : "FAIL: data, bss, SRAM read/write\r\n");
    append("TikuOS ESP32-P4 RAM diagnostic; ROM ECO=");
    hex(TIKU_P4_REG_READ(TIKU_P4_ROM_ECO));
    append(" cpu_MHz=");
    hex(cpu_mhz);
    append(" heartbeat=");
    hex(count);
    append("\r\nticks=");
    hex(tiku_p4_systimer_ticks());
    append(" interrupts=");
    hex(tiku_p4_systimer_interrupts());
    append(" timer_errors=");
    hex(tiku_p4_systimer_errors());
    append(" unclaimed=");
    hex(tiku_p4_irq_unclaimed());
    append("\r\n");
}

/** @brief Verify nested masks and timer catch-up after 40 ms without IRQs. */
static void critical_check(void)
{
    uint32_t outer = TIKU_P4_IRQ_SAVE();
    uint32_t before = tiku_p4_systimer_ticks();
    uint32_t inner = TIKU_P4_IRQ_SAVE();
    uint32_t start = cycles(), state;
    int ok;
    TIKU_P4_IRQ_RESTORE(inner);
    __asm__ volatile("csrr %0, mstatus" : "=r"(state));
    while ((uint32_t)(cycles() - start) < cpu_mhz * 40000u) {
    }
    ok = !(state & 8u) && tiku_p4_systimer_ticks() == before;
    TIKU_P4_IRQ_RESTORE(outer);
    start = cycles();
    while (tiku_p4_systimer_ticks() == before &&
           (uint32_t)(cycles() - start) < cpu_mhz * 20000u) {
    }
    ok = ok && (uint32_t)(tiku_p4_systimer_ticks() - before) >= 5u;
    append(ok ? "PASS: nested IRQ mask and delayed ticks\r\n"
              : "FAIL: nested IRQ mask and delayed ticks\r\n");
}

/** @brief Check live IRQ entry/return preserves the integer register frame. */
static void frame_check(void)
{
    uint32_t before = tiku_p4_systimer_interrupts();
    int result = tiku_p4_irq_probe(cpu_mhz * 50000u);
    int ok = result == 0 && tiku_p4_systimer_interrupts() - before >= 5u;
    append(ok ? "PASS: IRQ register frame\r\n"
              : "FAIL: IRQ register frame\r\n");
}

/** @brief Wait a bounded interval using the measured CPU rate. */
static void wait_us(uint32_t us)
{
    uint32_t before = cycles();
    while ((uint32_t)(cycles() - before) < cpu_mhz * us) {
    }
}

/** @brief Rearm twice from the one-shot interrupt callback. */
static void diagnostic_alarm(void)
{
    uint64_t now;
    alarm_calls++;
    if (alarm_calls < 3u && (tiku_p4_systimer_read(&now) != 0 ||
        tiku_p4_alarm_schedule((now + 16000u) & TIKU_P4_SYSTIMER_MASK) != 0)) {
        alarm_failed = 1;
    }
}

/** @brief Verify one-shot delivery, rearm and cancellation at the current rate. */
static int alarm_verify(void)
{
    uint64_t now = 0;
    int ok;
    alarm_calls = alarm_failed = 0;
    if (tiku_p4_alarm_init(diagnostic_alarm) != 0) return 0;
    ok = tiku_p4_systimer_read(&now) == 0 &&
         tiku_p4_alarm_schedule((now + 32000u) & TIKU_P4_SYSTIMER_MASK) == 0;
    wait_us(12000);
    ok = ok && alarm_calls == 3u && !alarm_failed;
    alarm_calls = 0;
    ok = tiku_p4_systimer_read(&now) == 0 && ok;
    ok = tiku_p4_alarm_schedule((now + 32000u) & TIKU_P4_SYSTIMER_MASK) == 0 && ok;
    tiku_p4_alarm_cancel();
    wait_us(12000);
    ok = ok && alarm_calls == 0;
    tiku_p4_alarm_stop();
    return ok;
}

/** @brief Feed beyond the timeout, hold beyond it, then resume and disable. */
static void watchdog_check(void)
{
    unsigned i;
    tiku_watchdog_arch_on(TIKU_WDT_SRC_SMCLK, 8192);
    for (i = 0; i < 6; i++) {
        wait_us(50000);
        tiku_watchdog_arch_kick();
    }
    tiku_watchdog_arch_pause();
    wait_us(350000);
    tiku_watchdog_arch_resume(1);
    for (i = 0; i < 6; i++) {
        wait_us(50000);
        tiku_watchdog_arch_kick();
    }
    tiku_watchdog_arch_off();
    wait_us(300000);
    append("PASS: watchdog kick, pause, resume, off\r\n");
}

/** @brief Request watchdog expiry into ROM download mode without flash boot. */
static void watchdog_reset_check(void)
{
    uint32_t saved_ctrl = TIKU_P4_REG_READ(TIKU_P4_LP_SYSTEM_CTRL);
    unsigned tries;
    TIKU_P4_REG_WRITE(TIKU_P4_LP_SYSTEM_CTRL, saved_ctrl | 4u);
    if (!(TIKU_P4_REG_READ(TIKU_P4_LP_SYSTEM_CTRL) & 4u)) {
        append("FAIL: watchdog ROM download request\r\n");
        return;
    }
    append("WATCHDOG: expiring into ROM download mode\r\n");
    for (tries = 0; queued && tries < 1000000u; tries++) pump();
    tiku_watchdog_arch_on(TIKU_WDT_SRC_SMCLK, 8192);
    wait_us(1000000);
    tiku_watchdog_arch_off();
    TIKU_P4_REG_WRITE(TIKU_P4_LP_SYSTEM_CTRL, saved_ctrl);
    append("FAIL: watchdog did not reset\r\n");
}

/** @brief Exercise both frequency directions, measuring clocks and IRQs. */
static void clock_check(void)
{
    static const unsigned rates[] = {40, 90, 180, 360, 180, 90, 40, 360};
    unsigned i;
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        uint32_t before, after;
        int ok = tiku_p4_clock_set_mhz(rates[i]) == 0;
        cpu_mhz = measure_cpu();
        ok = ok && cpu_mhz == rates[i];
        ok = alarm_verify() && ok;
        before = tiku_p4_systimer_interrupts();
        ok = tiku_p4_irq_probe(cpu_mhz * 50000u) == 0 && ok;
        after = tiku_p4_systimer_interrupts();
        ok = ok && after - before >= 5u && after - before <= 8u;
        append(ok ? "PASS: clock requested=" : "FAIL: clock requested=");
        hex(rates[i]);
        append(" measured=");
        hex(cpu_mhz);
        append(" IRQs=");
        hex(after - before);
        append("\r\n");
        if (!ok) return;
    }
}

/** @brief Run SRAM checks, then service polled USB beside a native tick ISR. */
__attribute__((noreturn)) void tiku_esp32p4_diagnostic(void)
{
    uint32_t i, pass, count = 0, previous;
    tiku_usb_serial_jtag_init();
    if (TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) != 18u ||
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) != 2u) {
        append("FAIL: unsupported P4 ROM identity\r\n");
        for (;;) {
            pump();
        }
    }
    tiku_esp32p4_boot_watchdogs_disable();
    tiku_esp32p4_boot_park_cores();
    if (tiku_p4_clock_xtal_init() != 0) {
        append("FAIL: crystal clock initialization\r\n");
        for (;;) {
            pump();
        }
    }
    memory_ok = initialized == 0x54494b55u;
    for (i = 0; i < 1024; i++) {
        memory_ok &= zeroed[i] == 0;
    }
    for (pass = 0; pass < 6; pass++) {
        for (i = 0; i < 1024; i++) {
            zeroed[i] = (0xa55a0000u ^ (i * 0x10203u)) << pass;
        }
        for (i = 0; i < 1024; i++) {
            memory_ok &= zeroed[i] ==
                         ((0xa55a0000u ^ (i * 0x10203u)) << pass);
        }
    }
    tiku_p4_irq_init();
    if (tiku_p4_systimer_init() != 0 || (cpu_mhz = measure_cpu()) == 0 ||
        cpu_mhz != 40u) {
        append("FAIL: native timer or CPU measurement\r\n");
        for (;;) {
            pump();
        }
    }
    if (tiku_p4_clock_pll_init() != 0 ||
        tiku_p4_clock_set_mhz(360) != 0 || (cpu_mhz = measure_cpu()) != 360u) {
        append("FAIL: native regulator, CPLL or 360 MHz measurement\r\n");
        for (;;) {
            pump();
        }
    }
    TIKU_P4_IRQ_RESTORE(8u);
    identity(count);
    append("Commands: i identity, p ping, c IRQ mask, r registers, f clocks,\r\n"
           "a alarms, w watchdog, W watchdog reset, t/x/v/b/n faults, d/D record,\r\n"
           "F read-only flash scan\r\n");
    previous = tiku_p4_systimer_ticks();
    for (;;) {
        int c;
        pump();
        if (queued) {
            continue;
        }
        c = tiku_usb_serial_jtag_getc();
        if (c == 'i') {
            identity(count);
        } else if (c == 'p') {
            append("PONG\r\n");
        } else if (c == 'c') {
            critical_check();
        } else if (c == 'r') {
            frame_check();
        } else if (c == 'f') {
            clock_check();
        } else if (c == 'a') {
            append(alarm_verify() ? "PASS: one-shot alarm rearm and cancel\r\n"
                                  : "FAIL: one-shot alarm rearm and cancel\r\n");
        } else if (c == 'w') {
            watchdog_check();
        } else if (c == 'W') {
            watchdog_reset_check();
        } else if (c == 'F') {
            tiku_p4_test_flash();
        } else {
            tiku_p4_fault_command(c);
        }
        if (!queued &&
            (uint32_t)(tiku_p4_systimer_ticks() - previous) >= 128u) {
            previous = tiku_p4_systimer_ticks();
            identity(++count);
        }
    }
}
