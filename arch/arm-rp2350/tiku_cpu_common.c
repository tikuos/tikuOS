/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RP2350 common helpers.
 *
 * Busy-wait delays on TIMER0, the synthesised unique ID, the reset cause from
 * WD_REASON and the reboot into USB BOOTSEL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_rp2350_regs.h"
#include "tiku_uart_arch.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* DELAYS ON TIMER0'S 1 US TICK                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the lower 32 bits of TIMER0's free-running 1 us counter.
 *
 * Reads TIMERAWL, which has no latch side effect.  The value wraps every
 * 71.6 minutes; callers compare readings by unsigned subtraction.
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
 * Calls tiku_cpu_rp2350_delay_us() in chunks of at most 1000 ms, so the
 * microsecond count of a chunk never overflows an unsigned int.
 *
 * @param ms  Delay duration in milliseconds
 */
void tiku_cpu_rp2350_delay_ms(unsigned int ms) {
    while (ms > 0U) {
        unsigned int chunk = (ms > 1000U) ? 1000U : ms;
        tiku_cpu_rp2350_delay_us(chunk * 1000U);
        ms -= chunk;
    }
}

/*---------------------------------------------------------------------------*/
/* UNIQUE ID                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Read the chip identifier words through the secure boot-ROM API. */
static int rp2350_chip_id(uint32_t words[4])
{
    typedef void *(*lookup_fn)(uint32_t, uint32_t);
    typedef int (*info_fn)(uint32_t *, uint32_t, uint32_t);
    uint32_t lookup_address;
    lookup_fn lookup;
    /* The ROM table starts below the compiler's valid C object range. */
    __asm__ volatile ("ldrh %0, [%1]" : "=r" (lookup_address)
                      : "r" ((uintptr_t)0x16u) : "memory");
    lookup = (lookup_fn)(uintptr_t)lookup_address;
    info_fn info;
    if (!lookup) {
        return -1;
    }
    info = (info_fn)lookup(0x5347u, 0x0004u);
    if (!info) {
        return -1;
    }
    return info(words, 4u, 1u);
}

/**
 * @brief Copy up to eight bytes of the silicon's chip identifier.
 * @return Bytes copied, or zero if the ROM does not supply CHIP_INFO.
 */
uint8_t tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len)
{
    uint32_t words[4];
    uint8_t i, n = len > 8u ? 8u : len;

    if (buf == NULL || n == 0u ||
        rp2350_chip_id(words) != 4 || !(words[0] & 1u)) {
        return 0u;
    }
    for (i = 0u; i < n; i++) {
        buf[i] = (uint8_t)(words[2u + i / 4u] >> ((i % 4u) * 8u));
    }
    return n;
}

/*---------------------------------------------------------------------------*/
/* RESET REASON                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the reset reason as an MSP430 SYSRSTIV-style code.
 *
 * Maps WD_REASON onto the codes /sys/boot decodes: a watchdog timeout, a
 * forced watchdog reset, or neither.
 *
 * @return 0x0016 after a timeout, 0x0006 after a forced reset, else 0
 */
uint16_t tiku_cpu_rp2350_reset_reason(void) {
    uint32_t r = _RP2350_REG(RP2350_WD_REASON);

    /* The hardware clears both bits on any reset the watchdog did not cause,
     * a debugger's warm reset included, so the register needs no clearing. */
    if (r & RP2350_WD_REASON_TIMER) {
        return 0x0016U;     /* wdt-timeout, which `reboot` also uses */
    }
    if (r & RP2350_WD_REASON_FORCE) {
        return 0x0006U;     /* sw-bor: CTRL.TRIGGER, as the BOOTSEL fallback */
    }
    return 0x0000U;         /* none: a reset the watchdog did not cause */
}

/*---------------------------------------------------------------------------*/
/* REBOOT TO USB BOOTSEL                                                     */
/*---------------------------------------------------------------------------*/

/*
 * Reboot the RP2350 into USB BOOTSEL (mass-storage) mode.
 *
 * The watchdog scratch[4] magic 0xB007C0D3 of pico-sdk's watchdog_reboot()
 * makes the boot ROM jump to a PC; it does not enter BOOTSEL.  BOOTSEL is
 * reached through the boot ROM's reset_usb_boot() or reboot(), looked up from
 * the ROM table through the pointer at ROM address 0x16.
 *
 * Lookup signature on RP2350 ARM:
 *   void *rom_table_lookup(uint32_t code, uint32_t mask);
 *
 * Function codes:
 *   'U'|('B'<<8) = 0x4255  -- reset_usb_boot(gpio_pin_mask, disable_iface)
 *   'R'|('B'<<8) = 0x4252  -- reboot(flags, delay_ms, p0, p1)
 *
 * Lookup masks (RT_FLAG_FUNC_*):
 *   0x0004  ARM_SEC    (Cortex-M33 secure mode, which TikuOS runs in)
 *   0x0010  ARM_NONSEC (tried second, for a function listed only there)
 *
 * reboot() flags used:
 *   0x002  REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL
 *   0x100  REBOOT2_FLAG_NO_RETURN_ON_SUCCESS
 *
 * Sequence: drain the UART TX FIFO, mask all IRQs, stop the watchdog and the
 * MPU, then try every (function, mask) pair until a boot ROM call takes
 * effect.  If none does, a plain watchdog reset restarts TikuOS, and BOOTSEL
 * then needs the button.
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

    /* Wait until the PL011 has sent its last byte, so output written
     * before the reboot reaches the host. */
    while (_RP2350_REG(RP2350_UART_FR) & RP2350_UART_FR_BUSY) {
        /* spin */
    }

    /* About 1 ms more at 150 MHz before the reboot starts. */
    for (i = 0; i < 200000U; i++) {
        __asm__ volatile ("nop");
    }

    /* Mask all interrupts. */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* Stop the watchdog before calling the ROM: its BOOTSEL sequence
     * takes about 10 ms, and a watchdog timeout during it resets the
     * chip into TikuOS, not into BOOTSEL. */
    _RP2350_REG(RP2350_WD_CTRL) = 0U;

    /* Disable the MPU, so no region faults an access made by the ROM's
     * BOOTSEL path; the chip resets right after. */
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
                /* does not return on success; a return falls through */
            }
        }

        /* Then try reboot() ('R','B') with BOOTSEL flag under each mask. */
        for (k = 0; k < (uint8_t)(sizeof(MASKS) / sizeof(MASKS[0])); k++) {
            func = lookup(0x4252U, MASKS[k]);
            if (func != (void *)0) {
                ((reboot_fn_t)func)(0x102U, /* BOOTSEL | NO_RETURN */
                                    10U,    /* delay_ms (matches pico-sdk) */
                                    0U, 0U);
                /* does not return on success */
            }
        }
    }

    /* No boot ROM call took effect: print a marker and reset through the
     * watchdog.  TikuOS boots again; BOOTSEL then needs the button. */
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
