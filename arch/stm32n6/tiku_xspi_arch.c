/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xspi_arch.c - STM32N6 external NOR flash over XSPI2, indirect mode.
 *
 * Single-lane SPI at 50 MHz: the part answers there from power-up with no mode
 * switch, and the speed needs no OTP fuse, which on this device is permanent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_xspi_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_stm32n6_regs.h"

/* IC3 carries the XSPI kernel clock. PLL1 runs at 1200 MHz, so a divider of 24
 * gives the 50 MHz ST uses for the conservative case; their 200 MHz variant
 * pairs with an OTP fuse this port will not burn. */
#define XSPI_IC_INDEX       3U
#define XSPI_IC_DIVIDER     24U
#define XSPI_CLOCK_HZ       50000000UL

/* Standard SPI-mode opcodes with 32-bit addressing; the 3-byte forms cannot
 * reach past 16 MB of a 64 MB part. */
#define CMD_READ_ID         0x9FU
#define CMD_READ_STATUS     0x05U
#define CMD_WRITE_ENABLE    0x06U
#define CMD_READ_4B         0x13U
#define CMD_PAGE_PROGRAM_4B 0x12U
#define CMD_SECTOR_ERASE_4B 0x21U

#define STATUS_WIP          0x01U   /* write in progress */
#define STATUS_WEL          0x02U   /* write enable latch */

/* Bounded so a wedged device costs an error rather than the caller. */
#define XSPI_SPINS          2000000UL
/* A 4 KB sector erase is specified in hundreds of milliseconds, so the busy
 * wait needs far more headroom than a transfer does. */
#define XSPI_ERASE_SPINS    200000000UL

/** @brief Set once init has completed, so later calls can refuse early. */
static uint8_t xspi_ready;

/** @brief Wait for a status-register bit, returning 0 on timeout. */
static int xspi_wait(uint32_t reg, uint32_t mask, int want_set,
                     unsigned long spins) {
    while (spins-- > 0UL) {
        uint32_t v = TIKU_REG32(reg) & mask;
        if (want_set ? (v != 0UL) : (v == 0UL)) {
            return 1;
        }
    }
    return 0;
}

/** @brief Clear the latched transfer flags and park the peripheral. */
static void xspi_finish(void) {
    TIKU_REG32(STM32N6_XSPI_FCR) = STM32N6_XSPI_FCR_ALL;
}

/**
 * @brief Run one indirect transfer.
 *
 * Writing the instruction starts a command that carries no address; otherwise
 * the address register write is what launches it.
 *
 * @param cmd      Opcode
 * @param addr     Address, ignored when @p use_addr is zero
 * @param use_addr Non-zero to emit a 32-bit address phase
 * @param buf      Data buffer, or NULL for a command with no data phase
 * @param len      Data length in bytes
 * @param write    Non-zero to send @p buf, zero to fill it
 * @return TIKU_XSPI_OK, or an error
 */
static tiku_xspi_err_t xspi_xfer(uint8_t cmd, uint32_t addr, int use_addr,
                                 void *buf, uint32_t len, int write) {
    if (!xspi_wait(STM32N6_XSPI_SR, STM32N6_XSPI_SR_BUSY, 0, XSPI_SPINS)) {
        return TIKU_XSPI_ERR_TIMEOUT;
    }
    xspi_finish();

    uint32_t cr = TIKU_REG32(STM32N6_XSPI_CR);
    cr &= ~STM32N6_XSPI_CR_FMODE_MSK;
    cr |= ((write ? STM32N6_XSPI_FMODE_WRITE : STM32N6_XSPI_FMODE_READ)
           << STM32N6_XSPI_CR_FMODE_POS);
    TIKU_REG32(STM32N6_XSPI_CR) = cr;

    uint32_t ccr = STM32N6_XSPI_CCR_IMODE_1L;
    if (use_addr) {
        ccr |= STM32N6_XSPI_CCR_ADMODE_1L | STM32N6_XSPI_CCR_ADSIZE_32;
    }
    if (len > 0U) {
        ccr |= STM32N6_XSPI_CCR_DMODE_1L;
        TIKU_REG32(STM32N6_XSPI_DLR) = len - 1UL;
    }
    TIKU_REG32(STM32N6_XSPI_CCR) = ccr;
    TIKU_REG32(STM32N6_XSPI_TCR) = 0UL;             /* no dummy cycles */
    TIKU_REG32(STM32N6_XSPI_IR)  = cmd;

    if (use_addr) {
        TIKU_REG32(STM32N6_XSPI_AR) = addr;
    }

    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0U; i < len; i++) {
        if (!xspi_wait(STM32N6_XSPI_SR,
                       STM32N6_XSPI_SR_FTF | STM32N6_XSPI_SR_TCF, 1,
                       XSPI_SPINS)) {
            return TIKU_XSPI_ERR_TIMEOUT;
        }
        if (write) {
            *(volatile uint8_t *)STM32N6_XSPI_DR = p[i];
        } else {
            p[i] = *(volatile uint8_t *)STM32N6_XSPI_DR;
        }
    }

    if (!xspi_wait(STM32N6_XSPI_SR, STM32N6_XSPI_SR_TCF, 1, XSPI_SPINS)) {
        return TIKU_XSPI_ERR_TIMEOUT;
    }
    if (TIKU_REG32(STM32N6_XSPI_SR) & STM32N6_XSPI_SR_TEF) {
        xspi_finish();
        return TIKU_XSPI_ERR_TIMEOUT;
    }
    xspi_finish();
    return TIKU_XSPI_OK;
}

/** @brief Read the device status register. */
static tiku_xspi_err_t xspi_status(uint8_t *out) {
    return xspi_xfer(CMD_READ_STATUS, 0U, 0, out, 1U, 0);
}

/**
 * @brief Wait until the device reports no write in progress.
 *
 * @param spins  Bound, generous enough for the slowest erase
 * @return TIKU_XSPI_OK, or a timeout
 */
static tiku_xspi_err_t xspi_wait_idle(unsigned long spins) {
    while (spins-- > 0UL) {
        uint8_t st = 0U;
        tiku_xspi_err_t rc = xspi_status(&st);
        if (rc != TIKU_XSPI_OK) {
            return rc;
        }
        if ((st & STATUS_WIP) == 0U) {
            return TIKU_XSPI_OK;
        }
    }
    return TIKU_XSPI_ERR_TIMEOUT;
}

/** @brief Arm the write-enable latch the device requires before it changes. */
static tiku_xspi_err_t xspi_write_enable(void) {
    tiku_xspi_err_t rc = xspi_xfer(CMD_WRITE_ENABLE, 0U, 0, NULL, 0U, 1);
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    uint8_t st = 0U;
    rc = xspi_status(&st);
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    return (st & STATUS_WEL) ? TIKU_XSPI_OK : TIKU_XSPI_ERR_STATE;
}

tiku_xspi_err_t tiku_xspi_init(void) {
    /* Kernel clock first: IC3 from PLL1, then the controller and its pins. */
    uint32_t ic = TIKU_REG32(STM32N6_RCC_ICCFGR(XSPI_IC_INDEX));
    ic &= ~(STM32N6_IC_INT_MSK | STM32N6_IC_SEL_MSK);
    ic |= (STM32N6_IC_SEL_PLL1 << STM32N6_IC_SEL_POS);
    ic |= ((XSPI_IC_DIVIDER - 1UL) << STM32N6_IC_INT_POS);
    TIKU_REG32(STM32N6_RCC_ICCFGR(XSPI_IC_INDEX)) = ic;
    TIKU_REG32(STM32N6_RCC_DIVENR) |= (1UL << (XSPI_IC_INDEX - 1U));

    uint32_t ccipr = TIKU_REG32(STM32N6_RCC_CCIPR6);
    ccipr &= ~STM32N6_CCIPR6_XSPI2SEL_MSK;
    ccipr |= STM32N6_CCIPR6_XSPI2SEL_IC3;
    TIKU_REG32(STM32N6_RCC_CCIPR6) = ccipr;

    TIKU_REG32(STM32N6_RCC_AHB5ENR) |= STM32N6_RCC_AHB5ENR_XSPI2 |
                                       STM32N6_RCC_AHB5ENR_XSPIM;
    TIKU_REG32(STM32N6_RCC_AHB4ENR) |= STM32N6_RCC_AHB4ENR_GPION;
    (void)TIKU_REG32(STM32N6_RCC_AHB5ENR);

    /* Declare the VDDIO3 rail valid and select its 1.8 V range, then wait for
     * the monitor: until this lands the XSPI pads are unpowered and the flash
     * cannot answer at all. */
    TIKU_REG32(STM32N6_PWR_SVMCR3) |= STM32N6_PWR_SVMCR3_VDDIO3SV |
                                      STM32N6_PWR_SVMCR3_VDDIO3VRSEL;
    (void)xspi_wait(STM32N6_PWR_SVMCR3, STM32N6_PWR_SVMCR3_VDDIO3RDY, 1,
                    XSPI_SPINS);

    /* Eleven signals, all GPION at AF9: DQS0, NCS1, IO0..IO7 and the clock. */
    static const uint8_t pins[] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 8U, 9U, 10U, 11U };
    for (unsigned i = 0U; i < sizeof(pins); i++) {
        tiku_stm32n6_gpio_init_alt(STM32N6_GPIO_PORT_N, pins[i],
                                   STM32N6_XSPI2_AF);
    }

    /* The I/O manager routes the controller to port 2, where the flash sits.
     * MUXEN and MODE both stay clear: they describe XSPI1 sharing or owning
     * port 2, and XSPI1 is untouched here. */
    TIKU_REG32(STM32N6_XSPIM_CR) = STM32N6_XSPIM_CR_CSSEL_OVR_EN;

    TIKU_REG32(STM32N6_XSPI_CR) = 0UL;              /* configure disabled */
    /* DEVSIZE is log2 of the byte count minus one: 64 MB is 2^26, so 25. */
    TIKU_REG32(STM32N6_XSPI_DCR1) =
        STM32N6_XSPI_DCR1_MTYP_MACRONIX |
        (25UL << STM32N6_XSPI_DCR1_DEVSIZE_POS) |
        (2UL  << STM32N6_XSPI_DCR1_CSHT_POS);
    TIKU_REG32(STM32N6_XSPI_DCR2) = 0UL;            /* kernel clock undivided */
    TIKU_REG32(STM32N6_XSPI_TCR)  = STM32N6_XSPI_TCR_DHQC;
    TIKU_REG32(STM32N6_XSPI_CR)   = STM32N6_XSPI_CR_EN;

    xspi_ready = 1U;

    /* An identity that does not name Macronix means the wiring or the clock is
     * wrong, and every later call would be guesswork. */
    tiku_xspi_id_t id;
    tiku_xspi_err_t rc = tiku_xspi_read_id(&id);
    if (rc != TIKU_XSPI_OK) {
        xspi_ready = 0U;
        return rc;
    }
    if (id.mfr != TIKU_XSPI_MFR_MACRONIX) {
        xspi_ready = 0U;
        return TIKU_XSPI_ERR_ID;
    }
    return TIKU_XSPI_OK;
}

tiku_xspi_err_t tiku_xspi_read_id(tiku_xspi_id_t *out) {
    if (out == NULL) {
        return TIKU_XSPI_ERR_ARG;
    }
    if (!xspi_ready) {
        return TIKU_XSPI_ERR_STATE;
    }
    uint8_t raw[3] = { 0U, 0U, 0U };
    tiku_xspi_err_t rc = xspi_xfer(CMD_READ_ID, 0U, 0, raw, sizeof(raw), 0);
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    out->mfr      = raw[0];
    out->type     = raw[1];
    out->capacity = raw[2];
    return TIKU_XSPI_OK;
}

tiku_xspi_err_t tiku_xspi_read(uint32_t addr, void *buf, uint32_t len) {
    if (buf == NULL || len == 0U ||
        addr > TIKU_XSPI_SIZE_BYTES || len > (TIKU_XSPI_SIZE_BYTES - addr)) {
        return TIKU_XSPI_ERR_ARG;
    }
    if (!xspi_ready) {
        return TIKU_XSPI_ERR_STATE;
    }
    return xspi_xfer(CMD_READ_4B, addr, 1, buf, len, 0);
}

tiku_xspi_err_t tiku_xspi_erase_sector(uint32_t addr) {
    if (addr >= TIKU_XSPI_SIZE_BYTES) {
        return TIKU_XSPI_ERR_ARG;
    }
    if (!xspi_ready) {
        return TIKU_XSPI_ERR_STATE;
    }

    tiku_xspi_err_t rc = xspi_wait_idle(XSPI_SPINS);
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    rc = xspi_write_enable();
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    rc = xspi_xfer(CMD_SECTOR_ERASE_4B,
                   addr & ~(TIKU_XSPI_SECTOR_SIZE - 1UL), 1, NULL, 0U, 1);
    if (rc != TIKU_XSPI_OK) {
        return rc;
    }
    return xspi_wait_idle(XSPI_ERASE_SPINS);
}

tiku_xspi_err_t tiku_xspi_program(uint32_t addr, const void *buf, uint32_t len) {
    if (buf == NULL || len == 0U ||
        addr > TIKU_XSPI_SIZE_BYTES || len > (TIKU_XSPI_SIZE_BYTES - addr)) {
        return TIKU_XSPI_ERR_ARG;
    }
    if (!xspi_ready) {
        return TIKU_XSPI_ERR_STATE;
    }

    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0U) {
        /* A program never crosses a page boundary: the device wraps within the
         * page instead of advancing, which would silently corrupt the start. */
        uint32_t room = TIKU_XSPI_PAGE_SIZE - (addr % TIKU_XSPI_PAGE_SIZE);
        uint32_t n    = (len < room) ? len : room;

        tiku_xspi_err_t rc = xspi_wait_idle(XSPI_SPINS);
        if (rc != TIKU_XSPI_OK) {
            return rc;
        }
        rc = xspi_write_enable();
        if (rc != TIKU_XSPI_OK) {
            return rc;
        }
        rc = xspi_xfer(CMD_PAGE_PROGRAM_4B, addr, 1, (void *)(uintptr_t)p, n, 1);
        if (rc != TIKU_XSPI_OK) {
            return rc;
        }
        rc = xspi_wait_idle(XSPI_ERASE_SPINS);
        if (rc != TIKU_XSPI_OK) {
            return rc;
        }

        addr += n;
        p    += n;
        len  -= n;
    }
    return TIKU_XSPI_OK;
}

unsigned long tiku_xspi_clock_hz(void) {
    return xspi_ready ? XSPI_CLOCK_HZ : 0UL;
}

int tiku_xspi_ready(void) {
    return xspi_ready ? 1 : 0;
}
