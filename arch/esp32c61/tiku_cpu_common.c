/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - ESP32-C61 delays, reset cause and identity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_esp32c61_regs.h"

void tiku_cpu_esp32c61_delay_us(unsigned int us) {
    /* Cycles per 1/64 us: a measured rate is no whole number of MHz, and
     * truncating it to one ran 20 MHz delays 5% short. */
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
    /* The MSP430 SYSRSTIV-style codes the kernel speaks, as the other ports
     * report them: /sys/boot/reason renders those and nothing else. */
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
        /* The EN pin reads as power-on too; the ROM code keeps the rest. */
        return 0x0000U;     /* none */
    }
}
