/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RP2350 common helpers (delays, unique-id, reset cause)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_rp2350_regs.h"
#include "tiku_uart_arch.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Delays via TIMER0's 1 us tick                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the lower 32 bits of TIMER0's free-running 1 us counter.
 *
 * The full 64-bit read would need a paired TIMELR/TIMEHR latching
 * sequence. For short spin-based delays the 32-bit lower word is
 * sufficient — it wraps every ~71 minutes.
 *
 * @return Current TIMER0 lower word value (microseconds since last reset)
 */
static inline uint32_t rp2350_us_now(void) {
    return _RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

/**
 * @brief Busy-wait for at least @p us microseconds.
 *
 * Spins on TIMER0's 1 us free-running counter. Handles timer wrap
 * via unsigned subtraction. Returns immediately for a zero argument.
 *
 * @param us  Delay duration in microseconds
 */
void tiku_cpu_rp2350_delay_us(unsigned int us) {
    if (us == 0U) {
        return;
    }
    uint32_t start = rp2350_us_now();
    while ((rp2350_us_now() - start) < (uint32_t)us) {
        /* spin */
    }
}

/**
 * @brief Busy-wait for at least @p ms milliseconds.
 *
 * Delegates to tiku_cpu_rp2350_delay_us() in 1000 ms chunks to keep
 * each call within the 32-bit microsecond counter's ~71-minute range.
 *
 * @param ms  Delay duration in milliseconds
 */
void tiku_cpu_rp2350_delay_ms(unsigned int ms) {
    /* Decompose to keep within the 32-bit microsecond window per
     * call (max ~71 minutes; 1000 * 65535 = ~65 s comfortably fits). */
    while (ms > 0U) {
        unsigned int chunk = (ms > 1000U) ? 1000U : ms;
        tiku_cpu_rp2350_delay_us(chunk * 1000U);
        ms -= chunk;
    }
}

/*---------------------------------------------------------------------------*/
/* Unique ID                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Reading the actual flash chip's unique ID requires temporarily
 * disabling XIP and issuing a 0x4B command to the QSPI controller —
 * out of scope for the first port. We synthesise a stable 8-byte ID
 * from the addresses of three linker symbols: this is unique per
 * build (the linker layout is deterministic across reboots of the
 * same image). Programs that need a true silicon ID should add a
 * proper flash-readback driver later.
 */
extern char __sram_start;
extern char __flash_start;
extern char __vectors_start;

/**
 * @brief Fill @p buf with a stable 8-byte pseudo-unique device identifier.
 *
 * Reading the flash chip's true 8-byte UID requires disabling XIP and
 * issuing a 0x4B QSPI command — not implemented in this port. Instead,
 * a stable identifier is synthesised by XOR-mixing a build-time magic
 * constant with low bits of three linker-symbol addresses. The result is
 * deterministic across reboots of the same image but differs between
 * builds, which is sufficient for most TikuOS use-cases. Programs
 * requiring a true silicon ID should add a flash-readback driver later.
 *
 * @param buf  Output buffer; must be non-NULL and at least @p len bytes
 * @param len  Number of ID bytes to write (clamped to 8)
 * @return Number of bytes written (0 if buf is NULL or len is 0)
 */
uint8_t tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len) {
    if (buf == NULL || len == 0U) {
        return 0U;
    }
    static const uint8_t magic[8] = {
        'r', 'p', '2', '3', '5', '0', 'O', 'S'
    };
    uint8_t n = (len > 8U) ? 8U : len;
    uint8_t i;
    /* XOR magic with low bits of the linker-symbol addresses so two
     * different builds produce different IDs. */
    uintptr_t a = (uintptr_t)&__sram_start;
    uintptr_t b = (uintptr_t)&__flash_start;
    uintptr_t c = (uintptr_t)&__vectors_start;
    for (i = 0; i < n; i++) {
        uint8_t mix = (uint8_t)((a >> (i * 4)) ^ (b >> i) ^ (c >> (i * 2)));
        buf[i] = magic[i] ^ mix;
    }
    return n;
}

/*---------------------------------------------------------------------------*/
/* Reset reason                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the reset reason as a 16-bit MSP430-compatible code.
 *
 * Reads WD_REASON: bit 0 = watchdog timeout, bit 1 = watchdog force.
 * Only the low byte is used; 0 means cold boot. The value is compatible
 * with the MSP430 SYSRSTIV encoding that the rest of the kernel uses.
 *
 * @return 16-bit reset reason code; 0 on cold boot
 */
uint16_t tiku_cpu_rp2350_reset_reason(void) {
    /* WD_REASON: bit 0 = TIMEOUT, bit 1 = FORCE. Higher bits report
     * other reset sources on a future revision. We map the low byte
     * directly so callers see a 16-bit value compatible with MSP430
     * SYSRSTIV. 0 means cold boot. */
    return (uint16_t)(_RP2350_REG(RP2350_WD_REASON) & 0xFFU);
}

/*---------------------------------------------------------------------------*/
/* Reboot to USB BOOTSEL                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reboot the RP2350 into USB BOOTSEL (mass-storage) mode.
 *
 * RP2350 has no portable watchdog-scratch BOOTSEL trick (the 0xB007C0D3
 * scratch[4] magic in pico-sdk's watchdog_reboot() redirects to an
 * arbitrary PC, NOT BOOTSEL). The correct path is via the boot ROM's
 * reset_usb_boot() / reboot() functions, looked up from the ROM table at
 * fixed offset 0x16 (bootrom_constants.h: BOOTROM_TABLE_LOOKUP_OFFSET).
 *
 * Lookup signature on RP2350 ARM:
 *   void *rom_table_lookup(uint32_t code, uint32_t mask);
 *
 * Function codes:
 *   'U'|('B'<<8) = 0x4255  -- reset_usb_boot(gpio_pin_mask, disable_iface)
 *   'R'|('B'<<8) = 0x4252  -- reboot(flags, delay_ms, p0, p1)
 *
 * Lookup masks (RT_FLAG_FUNC_*):
 *   0x0004  ARM_SEC    (Cortex-M33 secure mode, what TikuOS runs)
 *   0x0010  ARM_NONSEC (fallback in case the function lives only here)
 *
 * reboot() flags used:
 *   0x002  REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL
 *   0x100  REBOOT2_FLAG_NO_RETURN_ON_SUCCESS
 *
 * Strategy: drain the UART TX FIFO, disable all IRQs, disable the
 * watchdog, disable the MPU, then walk every (function, mask) pair
 * until a bootrom call succeeds. If every lookup misses, falls back
 * to a plain watchdog reset so the chip restarts (back into TikuOS;
 * the user must BOOTSEL manually).
 */
void tiku_cpu_rp2350_reboot_to_bootsel(void) {
    typedef void *(*lookup_fn_t)(uint32_t code, uint32_t mask);
    typedef void (*reset_usb_boot_fn_t)(uint32_t pin_mask,
                                        uint32_t iface_mask);
    typedef int  (*reboot_fn_t)(uint32_t flags, uint32_t delay_ms,
                                uint32_t p0, uint32_t p1);

    static const uint32_t MASKS[] = { 0x0004U, 0x0010U };
    volatile uint32_t i;
    uint16_t lookup_addr;
    lookup_fn_t lookup;
    void *func;
    uint8_t k;

    /* Drain the PL011 transmitter so [TS:END] reaches the host before
     * the chip's USB endpoint disappears or the UART RX is silenced. */
    while (_RP2350_REG(RP2350_UART_FR) & RP2350_UART_FR_BUSY) {
        /* spin */
    }

    /* Belt-and-braces settle (~1 ms at 150 MHz). */
    for (i = 0; i < 200000U; i++) {
        __asm__ volatile ("nop");
    }

    /* Mask all interrupts. */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* Disable the watchdog before calling the ROM. Some tests leave
     * the watchdog enabled with a tight timeout; the ROM's BOOTSEL
     * sequence asks for ~10 ms of work, and a mid-flight watchdog
     * timeout demotes the planned BOOTSEL into a plain CPU reset.
     * The host then sees TikuOS coming back up instead of the
     * USB MSD device, and the auto-bootsel loop times out. */
    _RP2350_REG(RP2350_WD_CTRL) = 0U;

    /* Disable the MPU. PRIVDEFENA covers unmapped memory (the ROM
     * lives at 0x00000000-ish and isn't covered by any region), but
     * during the ROM's USB-reconfig path it touches address ranges
     * we'd rather not assume anything about. Lifting all protection
     * here costs nothing: we're about to reset the chip anyway. */
    _RP2350_REG(RP2350_MPU_CTRL) = 0U;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");

    /* RP2350 ARM: bootrom lookup function pointer is at fixed ROM
     * offset 0x16 (see BOOTROM_TABLE_LOOKUP_OFFSET in pico-sdk). */
    lookup_addr = *(volatile uint16_t *)(uintptr_t)0x16U;
    lookup = (lookup_fn_t)(uintptr_t)lookup_addr;

    if (lookup != (lookup_fn_t)0) {
        /* Try reset_usb_boot() ('U','B') first under each mask. */
        for (k = 0; k < (uint8_t)(sizeof(MASKS) / sizeof(MASKS[0])); k++) {
            func = lookup(0x4255U, MASKS[k]);
            if (func != (void *)0) {
                ((reset_usb_boot_fn_t)func)(0U, 0U);
                /* must not return; if it does, fall through */
            }
        }

        /* Then try reboot() ('R','B') with BOOTSEL flag under each mask. */
        for (k = 0; k < (uint8_t)(sizeof(MASKS) / sizeof(MASKS[0])); k++) {
            func = lookup(0x4252U, MASKS[k]);
            if (func != (void *)0) {
                ((reboot_fn_t)func)(0x102U, /* BOOTSEL | NO_RETURN */
                                    10U,    /* delay_ms (matches pico-sdk) */
                                    0U, 0U);
                /* must not return on success */
            }
        }
    }

    /* Bootrom paths all failed.  Plain watchdog reset (no BOOTSEL).
     * Print a marker so the host sees we tried but missed, then
     * reset.  After the reset TikuOS will boot again; the user
     * can BOOTSEL manually. */
    tiku_uart_puts("[BOOTSEL: rom lookup failed; resetting]\n");
    while (_RP2350_REG(RP2350_UART_FR) & RP2350_UART_FR_BUSY) {
        /* drain */
    }
    _RP2350_REG(RP2350_WD_CTRL) = 0U;
    _RP2350_REG(RP2350_WD_LOAD) = 0U;
    _RP2350_REG(RP2350_WD_CTRL) = RP2350_WD_CTRL_TRIGGER
                                 | RP2350_WD_CTRL_ENABLE;

    for (;;) {
        __asm__ volatile ("wfe");
    }
}
