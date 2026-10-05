/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - ESP32-C61 in-package PSRAM bring-up.
 *
 * The device is identified, reset and put in QPI mode with ROM SPI user
 * commands on SPI1; SPI0 then serves cache misses to it with quad reads and
 * writes, and the MMU maps it as PSRAM pages after the flash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hal/tiku_printf_hal.h>

#include "tiku_psram_arch.h"
#include "tiku_esp32c61_regs.h"
#include <kernel/memory/tiku_mem.h>

#define PSRAM_CS1           (1U << 1)       /* the ROM's chip-select mask */
#define PSRAM_CS1_GPIO      14U
#define PSRAM_WP_GPIO       17U

/* AP Memory quad PSRAM commands. */
#define CMD_READ            0x03U
#define CMD_WRITE           0x02U
#define CMD_READ_QUAD       0xEBU
#define CMD_WRITE_QUAD      0x38U
#define CMD_ENTER_QPI       0x35U
#define CMD_EXIT_QPI        0xF5U
#define CMD_RESET_EN        0x66U
#define CMD_RESET           0x99U
#define CMD_READ_ID         0x9FU
#define READ_QUAD_DUMMY     6U
#define ID_KGD              0x5DU           /* known-good-die byte */
#define PROBE_WORD          0x5A6B7C8DUL

static uint32_t psram_id;
static uint32_t psram_bytes;
static uint8_t  psram_attached;

/** @brief Clock register value for MSPI core / @p div. */
static uint32_t mspi_clock(uint32_t div) {
    if (div <= 1U) {
        return 1UL << 31;                   /* the core clock itself */
    }
    return (div - 1U) | (((div - 1U) / 2U) << 8) | ((div - 1U) << 16);
}

/** @brief One SPI1 user command to the PSRAM, in SPI or QPI mode. */
static void psram_cmd(int qpi, uint32_t cmd, uint32_t addr, uint32_t addr_bits,
                      uint32_t *tx, uint32_t tx_bits,
                      uint32_t *rx, uint32_t rx_bits) {
    esp32c61_rom_spi_cmd_t c = {
        .cmd = (uint16_t)cmd, .cmd_bits = 8U,
        .addr = &addr, .addr_bits = addr_bits,
        .tx = tx, .tx_bits = tx_bits,
        .rx = rx, .rx_bits = rx_bits,
        .dummy_bits = 0U,
    };

    ESP32C61_ROM_SPI_SET_MODE(1, qpi ? ESP32C61_ROM_SPI_MODE_QIO
                                     : ESP32C61_ROM_SPI_MODE_SLOW);
    if (qpi) {
        TIKU_REG32(ESP32C61_SPI1_CTRL) |= ESP32C61_SPI1_FCMD_QUAD;
    } else {
        TIKU_REG32(ESP32C61_SPI1_CTRL) &= ~ESP32C61_SPI1_FCMD_QUAD;
    }
    ESP32C61_ROM_SPI_CMD_CONFIG(1, &c);
    ESP32C61_ROM_SPI_CMD_START(1, (uint8_t *)rx, (uint16_t)(rx_bits / 8U),
                               PSRAM_CS1, 0);
}

/** @brief Probe, identify, reset and switch the device to QPI.
 *  @return TIKU_ESP32C61_PSRAM_OK, or an error */
static tiku_esp32c61_psram_err_t psram_device_up(void) {
    uint32_t probe = PROBE_WORD, back = 0UL, id = 0UL;
    uint32_t density;

    /* The device may still be in QPI mode across a reset that did not power
     * it down. */
    psram_cmd(1, CMD_EXIT_QPI, 0UL, 0U, NULL, 0U, NULL, 0U);
    psram_cmd(0, CMD_WRITE, 0UL, 24U, &probe, 32U, NULL, 0U);
    psram_cmd(0, CMD_READ, 0UL, 24U, NULL, 0U, &back, 32U);
    if (back != PROBE_WORD) {
        return TIKU_ESP32C61_PSRAM_ABSENT;
    }
    /* The first ID read after power-up can come back wrong on 16 Mbit parts,
     * so a bad KGD byte is read once more. */
    psram_cmd(0, CMD_READ_ID, 0UL, 24U, NULL, 0U, &id, 24U);
    if (((id >> 8) & 0xFFUL) != ID_KGD) {
        psram_cmd(0, CMD_READ_ID, 0UL, 24U, NULL, 0U, &id, 24U);
    }
    psram_id = id & 0xFFFFFFUL;
    if (((id >> 8) & 0xFFUL) != ID_KGD) {
        return TIKU_ESP32C61_PSRAM_BAD_ID;
    }
    density = (id >> 21) & 0x7UL;           /* EID[47:45]: 2 MB << n */
    if (density > 2UL) {
        return TIKU_ESP32C61_PSRAM_BAD_ID;
    }
    psram_bytes = (2UL * 1024UL * 1024UL) << density;

    psram_cmd(0, CMD_RESET_EN, 0UL, 0U, NULL, 0U, NULL, 0U);
    psram_cmd(0, CMD_RESET, 0UL, 0U, NULL, 0U, NULL, 0U);
    psram_cmd(0, CMD_ENTER_QPI, 0UL, 0U, NULL, 0U, NULL, 0U);
    return TIKU_ESP32C61_PSRAM_OK;
}

/** @brief How SPI0 reads and writes the device on a cache miss. */
static void psram_spi0_phases(void) {
    uint32_t sctrl = TIKU_REG32(ESP32C61_SPI0_CACHE_SCTRL);

    sctrl &= ~ESP32C61_SCTRL_MODE_MSK;
    sctrl |= ESP32C61_SCTRL_SRAM_QIO | ESP32C61_SCTRL_RD_DUMMY |
             ESP32C61_SCTRL_USR_RCMD | ESP32C61_SCTRL_USR_WCMD |
             ((READ_QUAD_DUMMY - 1UL) << ESP32C61_SCTRL_RDUMMY_POS) |
             ((24UL - 1UL) << ESP32C61_SCTRL_ADDR_POS);
    TIKU_REG32(ESP32C61_SPI0_CACHE_SCTRL) = sctrl;
    TIKU_REG32(ESP32C61_SPI0_SRAM_DRD_CMD) =
        (7UL << ESP32C61_SPI0_CMD_BITS_POS) | CMD_READ_QUAD;
    TIKU_REG32(ESP32C61_SPI0_SRAM_DWR_CMD) =
        (7UL << ESP32C61_SPI0_CMD_BITS_POS) | CMD_WRITE_QUAD;
    /* A burst may not cross the device's page: 512 bytes on a 2 MB part. */
    TIKU_REG32(ESP32C61_SPI0_SMEM_ECC_CTRL) =
        (TIKU_REG32(ESP32C61_SPI0_SMEM_ECC_CTRL) & ~(3UL << ESP32C61_SMEM_PAGE_POS)) |
        ((psram_bytes == 2UL * 1024UL * 1024UL ? 1UL : 2UL) << ESP32C61_SMEM_PAGE_POS);
    TIKU_REG32(ESP32C61_SPI0_SMEM_AC) |= ESP32C61_SMEM_SPLIT_TRANS;
    TIKU_REG32(ESP32C61_SPI0_SRAM_CLK) = mspi_clock(2U);   /* 40 MHz */
}

tiku_esp32c61_psram_err_t tiku_esp32c61_psram_init(void) {
    uint32_t ctrl, clock, misc, mux;
    tiku_esp32c61_psram_err_t rc;
    volatile uint32_t *w = (volatile uint32_t *)TIKU_ESP32C61_PSRAM_BASE;

    if (psram_bytes != 0UL) {
        return TIKU_ESP32C61_PSRAM_OK;
    }

    /* Chip select 1 on its MSPI pad function; WP and HD as data lines. */
    mux = TIKU_REG32(ESP32C61_IO_MUX_GPIO(PSRAM_CS1_GPIO));
    TIKU_REG32(ESP32C61_IO_MUX_GPIO(PSRAM_CS1_GPIO)) =
        (mux & ~ESP32C61_IO_MUX_MCU_SEL_MSK) | ESP32C61_IO_MUX_FUN_IE;
    ESP32C61_ROM_SPI_QIO_PINS(PSRAM_WP_GPIO, 0UL);
    TIKU_REG32(ESP32C61_SPI0_SMEM_AC) |=
        ESP32C61_SMEM_CS_SETUP | ESP32C61_SMEM_CS_HOLD;

    /* SPI1 also serves the flash: its CTRL, CLOCK and MISC are restored
     * after the bring-up, whatever its result. */
    ctrl = TIKU_REG32(ESP32C61_SPI1_CTRL);
    clock = TIKU_REG32(ESP32C61_SPI1_CLOCK);
    misc = TIKU_REG32(ESP32C61_SPI1_MISC);
    TIKU_REG32(ESP32C61_SPI1_CLOCK) = mspi_clock(4U);      /* 20 MHz */
    rc = psram_device_up();
    TIKU_REG32(ESP32C61_SPI1_CTRL) = ctrl;
    TIKU_REG32(ESP32C61_SPI1_CLOCK) = clock;
    TIKU_REG32(ESP32C61_SPI1_MISC) = misc;
    if (rc != TIKU_ESP32C61_PSRAM_OK) {
        psram_bytes = 0UL;
        return rc;
    }

    psram_spi0_phases();
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMAADDR13,
                       (TIKU_ESP32C61_PSRAM_BASE | (psram_bytes / 2UL - 1UL)) >> 2);
    ESP32C61_CSR_WRITE(ESP32C61_CSR_PMACFG13, ESP32C61_PMA_NAPOT_RW);
    if (ESP32C61_ROM_MMU_SET(0UL, ESP32C61_MMU_ACCESS_PSRAM,
                             TIKU_ESP32C61_PSRAM_BASE, 0UL, 64UL,
                             psram_bytes / 0x10000UL, 0UL) != 0) {
        psram_bytes = 0UL;
        return TIKU_ESP32C61_PSRAM_MAP;
    }
    (void)ESP32C61_ROM_CACHE_INVAL(TIKU_ESP32C61_PSRAM_BASE, psram_bytes);

    /* A pattern at each end is written back and invalidated, then read
     * again from the device. */
    w[0] = 0xA5A55A5AUL;
    w[psram_bytes / 4UL - 1UL] = 0x3C3CC3C3UL;
    (void)ESP32C61_ROM_CACHE_WB_INVAL(TIKU_ESP32C61_PSRAM_BASE, psram_bytes);
    if (w[0] != 0xA5A55A5AUL || w[psram_bytes / 4UL - 1UL] != 0x3C3CC3C3UL) {
        psram_bytes = 0UL;
        return TIKU_ESP32C61_PSRAM_VERIFY;
    }
    return TIKU_ESP32C61_PSRAM_OK;
}

/* psram_data.ld's bounds, defined only when buffers live in PSRAM. */
extern char __tiku_psram_data_start[] __attribute__((weak));
extern char __tiku_psram_data_end[] __attribute__((weak));

/** @brief @p p as an integer the compiler cannot fold: two absent weak
 *         symbols are equal, yet it may assume distinct symbols differ. */
static uintptr_t opaque(const void *p) {
    uintptr_t a = (uintptr_t)p;

    __asm__ volatile ("" : "+r"(a));
    return a;
}

void tiku_esp32c61_psram_data_boot(void) {
    uintptr_t start = opaque(__tiku_psram_data_start);
    uintptr_t end = opaque(__tiku_psram_data_end);
    tiku_esp32c61_psram_err_t rc;

    if (start == end) {
        return;
    }
    rc = tiku_esp32c61_psram_init();
    if (rc != TIKU_ESP32C61_PSRAM_OK ||
        end > TIKU_ESP32C61_PSRAM_BASE + psram_bytes) {
        TIKU_PRINTF("psram: %d -- this build keeps buffers there, and cannot "
                    "run without it\n", (int)rc);
        for (;;) {
        }
    }
    memset((void *)start, 0, end - start);
}

tiku_esp32c61_psram_err_t tiku_esp32c61_psram_attach(void) {
    tiku_esp32c61_psram_err_t rc = tiku_esp32c61_psram_init();
    uintptr_t base = TIKU_ESP32C61_PSRAM_BASE;
    uintptr_t data_end = opaque(__tiku_psram_data_end);
    uint32_t bytes = psram_bytes;

    if (rc != TIKU_ESP32C61_PSRAM_OK || psram_attached) {
        return rc;
    }
#if defined(TIKU_BASIC_MODULE_ENABLE) && TIKU_BASIC_MODULE_ENABLE
    base += TIKU_ESP32C61_MODULE_WINDOW_BYTES;
    bytes -= TIKU_ESP32C61_MODULE_WINDOW_BYTES;
#endif
    if (data_end > base) {
        bytes -= (uint32_t)(data_end - base);
        base = data_end;
    }
    if (tiku_tier_attach_psram((void *)base, (tiku_mem_arch_size_t)bytes) !=
        TIKU_MEM_OK) {
        return TIKU_ESP32C61_PSRAM_MAP;
    }
    psram_attached = 1U;
    return TIKU_ESP32C61_PSRAM_OK;
}

uint32_t tiku_esp32c61_psram_id(void) {
    return psram_id;
}

uint32_t tiku_esp32c61_psram_size(void) {
    return psram_bytes;
}

/** @brief Whether [a, a + n) lies wholly in the mapped PSRAM. */
static int psram_holds(uintptr_t a, unsigned long n) {
    return n != 0UL && a >= TIKU_ESP32C61_PSRAM_BASE && n <= psram_bytes &&
           a - TIKU_ESP32C61_PSRAM_BASE <= psram_bytes - n;
}

void tiku_esp32c61_psram_clean(const void *addr, unsigned long len) {
    if (psram_holds((uintptr_t)addr, len)) {
        (void)ESP32C61_ROM_CACHE_WB((uint32_t)(uintptr_t)addr, (uint32_t)len);
    }
}

void tiku_esp32c61_psram_invalidate(const void *addr, unsigned long len) {
    if (psram_holds((uintptr_t)addr, len)) {
        (void)ESP32C61_ROM_CACHE_INVAL((uint32_t)(uintptr_t)addr,
                                       (uint32_t)len);
    }
}
