/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - ESP32-C61 delays, resets and identity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_cpu_watchdog_arch.h"
#include "tiku_esp32c61_regs.h"
#include "tiku_irq_arch.h"

void tiku_cpu_esp32c61_delay_us(unsigned int us) {
    /* Cycles per 1/64 us, which keeps the fraction of a measured rate that
     * is not a whole number of MHz. */
    uint32_t per64 = (uint32_t)(tiku_cpu_esp32c61_clock_get_hz() / 15625UL);

    /* 100 ms at a time keeps chunk * per64 inside 32 bits at 160 MHz. */
    while (us > 0U) {
        unsigned int chunk = us > 100000U ? 100000U : us;
        uint32_t need = ((uint32_t)chunk * per64 + 63UL) / 64UL;
        uint32_t start = ESP32C61_CSR_READ(mcycle);

        while ((uint32_t)(ESP32C61_CSR_READ(mcycle) - start) < need) {
        }
        us -= chunk;
    }
}

void tiku_cpu_esp32c61_delay_ms(unsigned int ms) {
    while (ms-- > 0U) {
        tiku_cpu_esp32c61_delay_us(1000U);
    }
}

uint8_t tiku_cpu_esp32c61_unique_id(uint8_t *buf, uint8_t len) {
    uint32_t lo = TIKU_REG32(ESP32C61_EFUSE_MAC0);
    uint32_t hi = TIKU_REG32(ESP32C61_EFUSE_MAC1);
    uint8_t mac[6];
    uint8_t n;

    mac[0] = (uint8_t)(hi >> 8);
    mac[1] = (uint8_t)hi;
    mac[2] = (uint8_t)(lo >> 24);
    mac[3] = (uint8_t)(lo >> 16);
    mac[4] = (uint8_t)(lo >> 8);
    mac[5] = (uint8_t)lo;
    for (n = 0U; buf != 0 && n < len && n < 6U; n++) {
        buf[n] = mac[n];
    }
    return n;
}

uint32_t tiku_cpu_esp32c61_reset_code(void) {
    return ESP32C61_ROM_RESET_REASON(0);
}

uint16_t tiku_cpu_esp32c61_reset_reason(void) {
    /* The MSP430 SYSRSTIV-style codes, the only ones /sys/boot/reason
     * renders. */
    switch (tiku_cpu_esp32c61_reset_code()) {
    case ESP32C61_RESET_TG0_WDT_SYS:
    case ESP32C61_RESET_TG1_WDT_SYS:
    case ESP32C61_RESET_RTC_WDT_SYS:
    case ESP32C61_RESET_TG0_WDT_CPU:
    case ESP32C61_RESET_RTC_WDT_CPU:
    case ESP32C61_RESET_RTC_WDT_RTC:
    case ESP32C61_RESET_TG1_WDT_CPU:
    case ESP32C61_RESET_SUPER_WDT:
        return 0x0016U;     /* wdt-timeout */
    case ESP32C61_RESET_SW_SYS:
    case ESP32C61_RESET_SW_CPU:
        return 0x0006U;     /* sw-bor: a reboot, or the reset after a fault */
    case ESP32C61_RESET_USB_UART:
    case ESP32C61_RESET_USB_JTAG:
        return 0x0014U;     /* sw-por: the host reset the chip */
    case ESP32C61_RESET_DEEPSLEEP:
        return 0x0008U;     /* lpm5-wake */
    case ESP32C61_RESET_BROWNOUT:
        return 0x0002U;     /* brownout */
    default:
        /* Power-on, the EN pin and every other code read as none;
         * tiku_cpu_esp32c61_reset_code() keeps the detail. */
        return 0x0000U;     /* none */
    }
}

void tiku_cpu_esp32c61_icache_invalidate(void) {
    ESP32C61_ROM_CACHE_WB_INVAL_ALL();
    __asm__ volatile ("fence.i" ::: "memory");
}

void tiku_cpu_esp32c61_restart(int by_watchdog) {
    uint32_t us;

    (void)tiku_esp32c61_mie_off();
    /* Drain the console FIFO for at most 20 ms: a stuck UART must not hold
     * off the reset. */
    for (us = 0UL; us < 20000UL &&
         ESP32C61_UART_TXCNT(TIKU_REG32(ESP32C61_UART_STATUS(
             ESP32C61_UART0_BASE))) != 0UL; us += 10UL) {
        tiku_cpu_esp32c61_delay_us(10U);
    }
    tiku_cpu_esp32c61_delay_us(200U);   /* the last byte leaves the shifter */
    /* Back on the PLL: a reset taken while the core runs from the crystal
     * leaves the ROM silent until EN. */
    (void)tiku_cpu_freq_esp32c61_set(160U);
    /* Nothing below touches flash or PSRAM. */
    (void)ESP32C61_ROM_CACHE_DISABLE();
    if (by_watchdog) {
        /* 64/32768 s is a hold of 4 watchdog ticks, about 2 ms; a hold of
         * one tick never fires. */
        tiku_cpu_esp32c61_watchdog_on_arch(TIKU_WDT_SRC_ACLK, 64U);
    } else {
        ESP32C61_ROM_SOFTWARE_RESET();
    }
    for (;;) {
    }
}
