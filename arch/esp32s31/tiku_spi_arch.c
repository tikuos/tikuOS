/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - Bounded SPI2 transfers on the Korvo card pins.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <kernel/threads/tiku_thread.h>
#include "tiku_spi_arch.h"
#include "tiku_runtime_arch.h"
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SPI 0x2038f000u
#define CLOCK 0x2058706cu
#define INPUT 0x205833ccu
#define MUX(p) (0x20582000u + 4u * (p))
#define CONFIG(p) (0x205830f4u + 4u * (p))
#define ROUTE(p) (0x20583af4u + 4u * (p))
#define MASK ((1u << 20) | (1u << 24) | (1u << 25))
static const unsigned pins[] = {24,25,20};
static uint32_t saved[3][3], saved_enable, saved_input, saved_clock;
static unsigned opened, failed;
static const char owner[] = "spi2";
#ifdef TIKU_S31_SD_TEST
static unsigned timeout_fault;
void tiku_s31_spi_test_loopback(unsigned enable)
{ if (opened) REG(INPUT) = (1u << 9) | (enable ? 25u : 20u); }
void tiku_s31_spi_test_timeout(void) { timeout_fault = 1; }
#endif
/** @brief Restrict controller calls to kernel foreground. */
static int context(void) { return !tiku_s31_in_trap && tiku_thread_in_kernel(); }
/** @brief Bound configuration and transfer waits to five milliseconds. */
static int wait_clear(uint32_t bits)
{
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 100000; n++) {
#ifdef TIKU_S31_SD_TEST
        if (!(timeout_fault && bits == (1u << 24)))
#endif
            if (!(REG(SPI) & bits)) return 0;
        if (tiku_s31_time() - start >= 80000u) break;
    }
#ifdef TIKU_S31_SD_TEST
    timeout_fault = 0;
#endif
    return TIKU_SPI_ERR_TIMEOUT;
}
void tiku_spi_arch_close(void)
{
    if (!context() || !opened) return;
    REG(CLOCK) |= 4u;
    REG(0x2058303cu) = MASK;
    for (unsigned i = 0; i < 3; i++) {
        REG(ROUTE(pins[i])) = saved[i][1];
        REG(CONFIG(pins[i])) = saved[i][2];
        REG(MUX(pins[i])) = saved[i][0];
        (void)tiku_gpio_release(pins[i] / 8 + 1, pins[i] % 8, owner);
    }
    REG(INPUT) = saved_input;
    REG(0x20583038u) = saved_enable & MASK;
    REG(CLOCK) = saved_clock;
    opened = failed = 0;
}
int tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    if (!context() || opened || !config || config->mode || config->bit_order ||
        config->prescaler != 100 || (REG(0x205883f4u) & 1u)) return TIKU_SPI_ERR_PARAM;
    for (unsigned p = 0; p < 62; p++) {
        unsigned signal = REG(ROUTE(p)) & 511u;
        if (signal == 53 || signal == 55) return TIKU_SPI_ERR_PARAM;
    }
    unsigned claimed = 0;
    for (; claimed < 3; claimed++) {
        if (tiku_gpio_claim(pins[claimed] / 8 + 1, pins[claimed] % 8, owner)) {
            while (claimed) { unsigned p = pins[--claimed];
                (void)tiku_gpio_release(p / 8 + 1, p % 8, owner); }
            return TIKU_SPI_ERR_PARAM;
        }
    }
    saved_clock = REG(CLOCK); saved_input = REG(INPUT);
    saved_enable = REG(0x20583034u);
    for (unsigned i = 0; i < 3; i++) {
        saved[i][0] = REG(MUX(pins[i])); saved[i][1] = REG(ROUTE(pins[i]));
        saved[i][2] = REG(CONFIG(pins[i]));
    }
    opened = 1; failed = 0;
    REG(CLOCK) = 0x800047u; REG(CLOCK) = 0x800043u;
    REG(SPI + 0xe8) = 7; REG(SPI + 0xe0) = 0; REG(SPI + 8) = 0;
    REG(SPI + 0x0c) = 49u | (24u << 6) | (49u << 12) | (1u << 18);
    REG(SPI + 0x10) = 1u | (1u << 27) | (1u << 28);
    REG(SPI + 0x14) = REG(SPI + 0x18) = 0;
    REG(SPI + 0x20) = 63; REG(SPI + 0x30) = REG(SPI + 0x34) = 0;
    REG(0x2058303cu) = MASK;
    for (unsigned i = 0; i < 3; i++) {
        unsigned p = pins[i];
        REG(MUX(p)) = (saved[i][0] & ~((7u << 12) | (3u << 7))) | (1u << 12) | (1u << 9) |
                      (p == 24 ? 0 : (1u << 8));
        REG(CONFIG(p)) = saved[i][2] & ~4u;
    }
    REG(ROUTE(24)) = 53; REG(ROUTE(25)) = 55; REG(ROUTE(20)) = 256;
    REG(INPUT) = (1u << 9) | 20u;
    REG(0x20583038u) = (1u << 24) | (1u << 25);
    REG(SPI) = 1u << 23;
    if (wait_clear(1u << 23)) { tiku_spi_arch_close(); return TIKU_SPI_ERR_TIMEOUT; }
    return 0;
}
/** @brief Exchange bounded FIFO batches; failure resets SPI and requires close. */
static int run(const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    if (!context() || !opened || failed || !length) return TIKU_SPI_ERR_PARAM;
    for (unsigned done = 0; done < length;) {
        unsigned n = length - done;
        if (n > 64) n = 64;
        for (unsigned i = 0; i < (n + 3) / 4; i++) {
            uint32_t word = UINT32_MAX;
            if (tx) for (unsigned j = 0; j < 4 && i * 4 + j < n; j++) {
                word &= ~(255u << (j * 8)); word |= (uint32_t)tx[done + i * 4 + j] << (j * 8);
            }
            REG(SPI + 0x98 + i * 4) = word;
        }
        REG(SPI + 0x1c) = n * 8 - 1;
        REG(SPI) = 1u << 23;
        if (wait_clear(1u << 23)) goto fault;
        REG(SPI) = 1u << 24;
        if (wait_clear(1u << 24)) goto fault;
        if (rx) for (unsigned i = 0; i < n; i++)
            rx[done + i] = (uint8_t)(REG(SPI + 0x98 + (i / 4) * 4) >> ((i % 4) * 8));
        done += n;
    }
    return 0;
fault:
    REG(CLOCK) |= 4u; failed = 1;
    return TIKU_SPI_ERR_TIMEOUT;
}
uint8_t tiku_spi_arch_transfer(uint8_t value)
{ uint8_t result = 255; return run(&value, &result, 1) ? 255 : result; }
int tiku_spi_arch_write(const uint8_t *data, uint16_t length)
{ return data ? run(data, NULL, length) : TIKU_SPI_ERR_PARAM; }
int tiku_spi_arch_read(uint8_t *data, uint16_t length)
{ return data ? run(NULL, data, length) : TIKU_SPI_ERR_PARAM; }
int tiku_spi_arch_write_read(const uint8_t *tx, uint8_t *rx, uint16_t length)
{ return tx && rx ? run(tx, rx, length) : TIKU_SPI_ERR_PARAM; }
