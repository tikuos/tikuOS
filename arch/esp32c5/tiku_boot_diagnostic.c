/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_boot_diagnostic.c - RAM boot, C storage and USB console diagnostic.
 * SYSTIMER interrupts run beside the polled USB console.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include "tiku_esp32c5_regs.h"
#include "tiku_boot_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_watchdog_arch.h"
#include <services/console/tiku_usb_serial_jtag.h>

extern unsigned int tiku_c5_rom_cpu_frequency(void);
extern int tiku_c5_irq_probe(uint32_t duration_cycles);

static volatile uint32_t initialized = 0x54494B55u;
static volatile uint32_t zeroed[16];
static uint8_t output[512];
static size_t output_size;
static size_t output_sent;
static int memory_ok;

/** @brief Append a bounded string to the diagnostic output buffer. */
static void append(const char *text)
{
    while (*text != '\0' && output_size < sizeof(output)) {
        output[output_size++] = (uint8_t)*text++;
    }
}

/** @brief Append an unsigned value as eight hexadecimal digits. */
static void hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    append("0x");
    for (shift = 28; shift >= 0 && output_size < sizeof(output); shift -= 4) {
        output[output_size++] = (uint8_t)digits[(value >> shift) & 15u];
    }
}

/** @brief Offer the pending suffix to the USB FIFO without waiting. */
static void pump(void)
{
    output_sent += tiku_usb_serial_jtag_write(output + output_sent,
                                             output_size - output_sent);
    if (output_sent == output_size) {
        output_size = 0;
        output_sent = 0;
    }
}

/** @brief Read the wrapping CPU-cycle counter. */
static uint32_t cycles(void)
{
    uint32_t result;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(result));
    return result;
}

/** @brief Queue a diagnostic identity line and a monotonic heartbeat count. */
static void identity(uint32_t count, uint32_t mhz)
{
    append(memory_ok ? "PASS: data, bss, SRAM read/write\r\n" :
                       "FAIL: data, bss, SRAM read/write\r\n");
    append("TikuOS ESP32-C5 RAM diagnostic; ROM ECO=");
    hex(TIKU_C5_REG_READ(TIKU_C5_ROM_ECO));
    append(" cpu_MHz=");
    hex(mhz);
    append(" heartbeat=");
    hex(count);
    append("\r\n");
    append("ticks=");
    hex(tiku_c5_systimer_ticks());
    append(" interrupts=");
    hex(tiku_c5_systimer_interrupts());
    append(" timer_errors=");
    hex(tiku_c5_systimer_errors());
    append(" unclaimed=");
    hex(tiku_c5_irq_unclaimed());
    append("\r\n");
}

/** @brief Mask interrupts for 40 ms and check nested restoration and tick recovery. */
static void critical_check(uint32_t mhz)
{
    uint32_t outer = TIKU_C5_IRQ_SAVE();
    uint32_t before = tiku_c5_systimer_ticks();
    uint32_t inner = TIKU_C5_IRQ_SAVE();
    uint32_t start = cycles(), state;
    int ok;

    TIKU_C5_IRQ_RESTORE(inner);
    __asm__ volatile ("csrr %0, mstatus" : "=r"(state));
    while ((uint32_t)(cycles() - start) < mhz * 40000u) { }
    ok = !(state & 8u) && tiku_c5_systimer_ticks() == before;
    TIKU_C5_IRQ_RESTORE(outer);
    start = cycles();
    while (tiku_c5_systimer_ticks() == before &&
           (uint32_t)(cycles() - start) < mhz * 20000u) { }
    ok = ok && (uint32_t)(tiku_c5_systimer_ticks() - before) >= 5u;
    append(ok ? "PASS: nested IRQ mask and delayed ticks\r\n" :
                "FAIL: nested IRQ mask and delayed ticks\r\n");
}

/** @brief Check register preservation while timer interrupts enter and return. */
static void frame_check(uint32_t mhz)
{
    uint32_t before = tiku_c5_systimer_interrupts();
    int result = tiku_c5_irq_probe(mhz * 40000u);

    append(result == 0 && tiku_c5_systimer_interrupts() - before >= 5u ?
           "PASS: IRQ register frame\r\n" : "FAIL: IRQ register frame\r\n");
}

/** @brief Initialize the flash map and optionally erase/program its scratch sector. */
static void flash_check(int destructive)
{
    uint8_t pattern[259], readback[259];
    unsigned i;
    int ok = tiku_flash_init() == TIKU_FLASH_OK;

    append("flash_id=");
    hex(tiku_flash_jedec_id());
    append(ok ? " PASS: flash mapping\r\n" : " FAIL: flash mapping\r\n");
    if (!ok || !destructive) { return; }
    for (i = 0; i < sizeof(pattern); i++) { pattern[i] = (uint8_t)(i * 37u); }
    ok = tiku_flash_erase_sector(TIKU_FLASH_SCRATCH_ADDR) == TIKU_FLASH_OK &&
         tiku_flash_program(TIKU_FLASH_SCRATCH_ADDR + 3u, pattern,
                             sizeof(pattern)) == TIKU_FLASH_OK &&
         tiku_flash_read(TIKU_FLASH_SCRATCH_ADDR + 3u, readback,
                          sizeof(readback)) == TIKU_FLASH_OK;
    for (i = 0; i < sizeof(pattern) && ok; i++) { ok = readback[i] == pattern[i]; }
    append(ok ? "PASS: scratch erase/program/readback\r\n" :
                "FAIL: scratch erase/program/readback\r\n");
}

/** @brief Kick for one second, hold for half a second, then permit a watchdog reset. */
static void watchdog_check(uint32_t mhz)
{
    unsigned i;
    uint32_t start;

    tiku_watchdog_arch_on(TIKU_WDT_SRC_ACLK, 8192u);
    for (i = 0; i < 10; i++) {
        start = cycles();
        while ((uint32_t)(cycles() - start) < mhz * 100000u) { }
        tiku_watchdog_arch_kick();
    }
    tiku_watchdog_arch_pause();
    start = cycles();
    while ((uint32_t)(cycles() - start) < mhz * 500000u) { }
    append("PASS: watchdog kick and pause survival\r\n");
    append("Watchdog reset armed: 250 ms\r\n");
    start = cycles();
    while (output_size && (uint32_t)(cycles() - start) < mhz * 20000u) { pump(); }
    tiku_watchdog_arch_resume(1);
}

/** @brief Validate ROM and C storage, then service USB commands and heartbeats. */
__attribute__((noreturn))
void tiku_esp32c5_diagnostic(void)
{
    uint32_t i, previous, count = 0, mhz;

    memory_ok = initialized == 0x54494B55u;

    tiku_usb_serial_jtag_init();
    if (TIKU_C5_REG_READ(TIKU_C5_ROM_CHIP_ID) != TIKU_ESP32C5_IMAGE_CHIP_ID ||
        TIKU_C5_REG_READ(TIKU_C5_ROM_ECO) < TIKU_ESP32C5_MIN_ROM_ECO) {
        append("FAIL: unsupported C5 ROM identity\r\n");
        for (;;) {
            pump();
        }
    }
    tiku_esp32c5_boot_watchdogs_disable();
    for (i = 0; i < 16; i++) {
        if (zeroed[i] != 0) {
            memory_ok = 0;
        }
        zeroed[i] = 0xA55A0000u | i;
        if (zeroed[i] != (0xA55A0000u | i)) {
            memory_ok = 0;
        }
    }
    mhz = tiku_c5_rom_cpu_frequency();
    tiku_c5_irq_init();
    if (tiku_c5_systimer_init() != 0) {
        append("FAIL: SYSTIMER initialization\r\n");
        for (;;) { pump(); }
    }
    TIKU_C5_IRQ_RESTORE(8u);
    identity(count, mhz);
    append("Commands: i identity, p ping, c IRQ mask, r registers, t trap, ? help\r\n");
    previous = cycles();
    for (;;) {
        int c;
        uint32_t now;

        pump();
        if (output_size != 0) {
            continue;
        }
        c = tiku_usb_serial_jtag_getc();
        if (c == 'p') {
            append("PONG\r\n");
        } else if (c == 'i') {
            identity(count, mhz);
        } else if (c == 't') {
            __asm__ volatile ("ebreak");
        } else if (c == 'c') {
            critical_check(mhz);
        } else if (c == 'r') {
            frame_check(mhz);
        } else if (c == 'f' || c == 'w') {
            flash_check(c == 'w');
        } else if (c == 'd') {
            watchdog_check(mhz);
        } else if (c == '?') {
            append("RAM diagnostic, not the TikuOS shell. i / p / c / r / t / ?\r\n");
        }
        now = cycles();
        if (output_size == 0 && (uint32_t)(now - previous) >=
            (mhz > 0 && mhz <= 240 ? mhz : 48) * 1000000u) {
            previous = now;
            identity(++count, mhz);
        }
    }
}

/** @brief Report a terminal exception with a bounded USB transmit attempt. */
__attribute__((noreturn))
void tiku_esp32c5_diagnostic_fault(uint32_t cause, uint32_t pc, uint32_t value)
{
    uint32_t tries;

    output_size = output_sent = 0;
    append("TRAP cause=");
    hex(cause);
    append(" pc=");
    hex(pc);
    append(" value=");
    hex(value);
    append("\r\n");
    for (tries = 0; tries < 1000000u && output_size != 0; tries++) {
        pump();
    }
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
