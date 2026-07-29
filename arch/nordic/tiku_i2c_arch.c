/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - nRF54L I2C master (TWIM, EasyDMA).
 *
 * Blocking polled master on the new EasyDMA model: pointers and counts describe
 * the transaction and SHORTS wire the STOP condition, so EVENTS_STOPPED catches
 * both success and a NACK.  EasyDMA reaches only RAM, so caller buffers pass through.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_i2c_arch.h>
#include <arch/nordic/tiku_device_select.h>  /* board pins + MDK TWIM/GPIO regs */

/*---------------------------------------------------------------------------*/
/* Instance + pin selection (board-overridable)                              */
/*---------------------------------------------------------------------------*/

/*
 * TWIM instance.  The nRF54L15 SERIAL20..23 instances each expose UARTE /
 * SPIM / TWIM at a single shared base, so an instance can only be one of
 * those at a time.  The board default console is UARTE20 (SERIAL20), which
 * rules out TWIM20; TWIM22 (SERIAL22, peripheral power domain) is free and
 * shares that domain with the working console, so the same PSEL routing to
 * P1 applies.  Secure alias (_S) because TikuOS runs All-Secure.
 */
#ifndef TIKU_BOARD_I2C_TWIM
#define TIKU_BOARD_I2C_TWIM         NRF_TWIM22_S
#endif

/*
 * SDA / SCL as (physical port, pin): P1.11 / P1.12.  These are free on the
 * nRF54L15-DK (P1 uses .04/.05 for the console, .08/.09/.13 for buttons,
 * .10/.14 for LEDs), adjacent, and on the same port the PERI-domain console
 * already drives.  GUESS pending the DK schematic -- override from the board
 * header (TIKU_BOARD_I2C0_{SDA,SCL}_{PORT,PIN}) once the header routing is
 * confirmed.  Port numbering is physical: 0=P0, 1=P1, 2=P2.
 */
#ifndef TIKU_BOARD_I2C0_SDA_PORT
#define TIKU_BOARD_I2C0_SDA_PORT    1u
#endif
#ifndef TIKU_BOARD_I2C0_SDA_PIN
#define TIKU_BOARD_I2C0_SDA_PIN     11u
#endif
#ifndef TIKU_BOARD_I2C0_SCL_PORT
#define TIKU_BOARD_I2C0_SCL_PORT    1u
#endif
#ifndef TIKU_BOARD_I2C0_SCL_PIN
#define TIKU_BOARD_I2C0_SCL_PIN     12u
#endif

/** @brief Shorthand for the selected TWIM register block. */
#define TIKU_TWIM                   TIKU_BOARD_I2C_TWIM

/*---------------------------------------------------------------------------*/
/* Register-level constants                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Encode a (port, pin) into a TWIM PSEL value.
 *
 * bits[4:0] = pin, bits[7:5] = port, bit31 = CONNECT (0 = connected).  Same
 * encoding the console UARTE driver uses and proves on this silicon.
 */
#define TIKU_PSEL(port, pin) \
    (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/** @brief PSEL reset/disconnect value (CONNECT = 1). */
#define TIKU_PSEL_DISCONNECTED      0xFFFFFFFFUL

/** @brief ENABLE register values (ENABLE field: 0x6 = on, 0x0 = off). */
#define TIKU_TWIM_ENABLE_ON \
    ((uint32_t)TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos)
#define TIKU_TWIM_ENABLE_OFF \
    ((uint32_t)TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos)

/** @brief Transaction shortcuts that generate the closing bus conditions. */
#define TIKU_TWIM_SH_TX_STOP        TWIM_SHORTS_LASTTX_STOP_Msk
#define TIKU_TWIM_SH_RX_STOP        TWIM_SHORTS_LASTRX_STOP_Msk
#define TIKU_TWIM_SH_TX_STARTRX     TWIM_SHORTS_LASTTX_DMA_RX_START_Msk

/** @brief All defined ERRORSRC bits, for clearing the W1C register. */
#define TIKU_TWIM_ERR_ALL           (TWIM_ERRORSRC_OVERRUN_Msk | \
                                     TWIM_ERRORSRC_ANACK_Msk   | \
                                     TWIM_ERRORSRC_DNACK_Msk)

/** @brief NACK bits (address or data) that map to TIKU_I2C_ERR_NACK. */
#define TIKU_TWIM_ERR_NACK_BITS     (TWIM_ERRORSRC_ANACK_Msk | \
                                     TWIM_ERRORSRC_DNACK_Msk)

/**
 * @brief Upper bound on the completion spin.
 *
 * Not a calibrated timeout -- a backstop so a wedged bus (SDA held low, no
 * clock) cannot hang the kernel.  A live transfer breaks out the instant
 * EVENTS_STOPPED asserts, so this bound only governs how fast a BROKEN bus fails.
 *
 * @note At ~11 cycles/iteration on the 128 MHz core, 100000 iterations is ~9 ms
 *       -- ample for any short probe or read, yet quick enough that a full
 *       112-address scan of an empty bus finishes in seconds, not tens.
 */
#define TIKU_TWIM_SPIN_LIMIT        100000UL

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Non-zero once tiku_i2c_arch_init() has completed successfully. */
static uint8_t i2c_initialised;

/**
 * @brief One-byte RAM source for the probe transfer.
 *
 * EasyDMA can only fetch from RAM, so the probe's dummy write byte lives in
 * .bss.  Its value is irrelevant -- a probe only cares whether the address
 * was acknowledged.
 */
static uint8_t twim_probe_byte;

/*---------------------------------------------------------------------------*/
/* Pin configuration                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure one SDA/SCL pin as open-drain input-with-pull-up.
 *
 * Mirrors nrfx's TWIM_PIN_INIT: DIR = Input, input buffer Connected so the
 * peripheral can sample the line, PULL = Pull-up, DRIVE = S0D1 so the pin is
 * only ever actively driven low -- the required open-drain behaviour.
 *
 * @note The TWIM drives the pin through its PSEL connection; PIN_CNF supplies
 *       the electrical characteristics.  External 4.7 kohm pull-ups are
 *       recommended, the internal one being weak.
 * @param port  Physical port (0 = P0, 1 = P1, 2 = P2).
 * @param pin   Pin number within the port (0..31).
 */
static void twim_cfg_pin(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g;

    switch (port) {
    case 0u:  g = NRF_P0_S; break;
    case 1u:  g = NRF_P1_S; break;
    case 2u:  g = NRF_P2_S; break;
    default:  return;
    }

    g->PIN_CNF[pin & 0x1Fu] =
        ((uint32_t)GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)    |
        ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)  |
        ((uint32_t)GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos)   |
        ((uint32_t)GPIO_PIN_CNF_DRIVE0_S0     << GPIO_PIN_CNF_DRIVE0_Pos) |
        ((uint32_t)GPIO_PIN_CNF_DRIVE1_D1     << GPIO_PIN_CNF_DRIVE1_Pos);
}

/*---------------------------------------------------------------------------*/
/* Core transfer engine                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Run one blocking TWIM transaction (write, read, or write-then-read).
 *
 * Programs ADDRESS, the requested SHORTS and the TX/RX EasyDMA descriptors,
 * starts the transfer (STARTTX when there are bytes to send, else STARTRX),
 * then spins on EVENTS_STOPPED, forcing a STOP on a wedged bus.
 *
 * @param addr     7-bit slave address (unshifted).
 * @param shorts   SHORTS bitmask selecting the closing bus condition(s).
 * @param tx       TX buffer, or NULL when @p txlen is 0.
 * @param txlen    Bytes to transmit (0 for a pure read).
 * @param rx       RX buffer, or NULL when @p rxlen is 0.
 * @param rxlen    Bytes to receive (0 for a pure write).
 * @param err_out  Receives the raw ERRORSRC bits, cleared here (may be NULL),
 *                 so the caller can classify address and data NACKs.
 * @return TIKU_I2C_OK once the bus reached STOP, TIKU_I2C_ERR_TIMEOUT if it
 *         never did.
 */
static int twim_run(uint8_t addr, uint32_t shorts,
                    const uint8_t *tx, uint16_t txlen,
                    uint8_t *rx, uint16_t rxlen,
                    uint32_t *err_out)
{
    uint32_t spin;
    uint32_t err;

    TIKU_TWIM->ADDRESS = (uint32_t)(addr & 0x7Fu);
    TIKU_TWIM->SHORTS  = shorts;

    /* Clear any stale completion / error state from a prior transaction. */
    TIKU_TWIM->EVENTS_STOPPED = 0UL;
    TIKU_TWIM->EVENTS_ERROR   = 0UL;
    TIKU_TWIM->ERRORSRC       = TIKU_TWIM_ERR_ALL;   /* W1C */
    (void)TIKU_TWIM->EVENTS_STOPPED;                 /* flush the clears */

    if (txlen > 0u) {
        TIKU_TWIM->DMA.TX.PTR    = (uint32_t)tx;
        TIKU_TWIM->DMA.TX.MAXCNT = (uint32_t)txlen;
    }
    if (rxlen > 0u) {
        TIKU_TWIM->DMA.RX.PTR    = (uint32_t)rx;
        TIKU_TWIM->DMA.RX.MAXCNT = (uint32_t)rxlen;
    }

    /* A write (or write-then-read) begins with STARTTX; the LASTTX->STARTRX
     * short then chains into the read phase.  A pure read begins with STARTRX. */
    if (txlen > 0u) {
        TIKU_TWIM->TASKS_DMA.TX.START = 1UL;
    } else {
        TIKU_TWIM->TASKS_DMA.RX.START = 1UL;
    }

    for (spin = 0UL; spin < TIKU_TWIM_SPIN_LIMIT; spin++) {
        if (TIKU_TWIM->EVENTS_STOPPED != 0UL) {
            break;
        }
    }

    if (TIKU_TWIM->EVENTS_STOPPED == 0UL) {
        /* Bus never reached STOP -- force one and report a timeout. */
        TIKU_TWIM->TASKS_STOP = 1UL;
        for (spin = 0UL; spin < TIKU_TWIM_SPIN_LIMIT; spin++) {
            if (TIKU_TWIM->EVENTS_STOPPED != 0UL) {
                break;
            }
        }
        TIKU_TWIM->EVENTS_STOPPED = 0UL;
        if (err_out != (uint32_t *)0) {
            *err_out = 0UL;
        }
        return TIKU_I2C_ERR_TIMEOUT;
    }

    TIKU_TWIM->EVENTS_STOPPED = 0UL;

    err = TIKU_TWIM->ERRORSRC;
    TIKU_TWIM->ERRORSRC = err;                        /* W1C: clear for next */
    if (err_out != (uint32_t *)0) {
        *err_out = err;
    }
    return TIKU_I2C_OK;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the nRF54L TWIM as an I2C master.
 *
 * Parks SDA/SCL as open-drain pull-up pins, routes them to the TWIM via PSEL,
 * selects 100 kHz (Standard) or 400 kHz (Fast), and enables the peripheral.
 * No separate clock gate: the TWIM shares the domain with the console UARTE.
 *
 * @param config  Bus configuration (speed).  Must be non-NULL.
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if @p config is NULL.
 */
int tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    if (config == (const tiku_i2c_config_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }

    /* Disable while (re)configuring. */
    TIKU_TWIM->ENABLE = TIKU_TWIM_ENABLE_OFF;

    /* Electrical setup of the pins before the peripheral takes them. */
    twim_cfg_pin(TIKU_BOARD_I2C0_SCL_PORT, TIKU_BOARD_I2C0_SCL_PIN);
    twim_cfg_pin(TIKU_BOARD_I2C0_SDA_PORT, TIKU_BOARD_I2C0_SDA_PIN);

    TIKU_TWIM->PSEL.SCL = TIKU_PSEL(TIKU_BOARD_I2C0_SCL_PORT,
                                    TIKU_BOARD_I2C0_SCL_PIN);
    TIKU_TWIM->PSEL.SDA = TIKU_PSEL(TIKU_BOARD_I2C0_SDA_PORT,
                                    TIKU_BOARD_I2C0_SDA_PIN);

    TIKU_TWIM->FREQUENCY = (config->speed == TIKU_I2C_SPEED_FAST)
                         ? TWIM_FREQUENCY_FREQUENCY_K400
                         : TWIM_FREQUENCY_FREQUENCY_K100;

    TIKU_TWIM->SHORTS = 0UL;
    TIKU_TWIM->ENABLE = TIKU_TWIM_ENABLE_ON;

    i2c_initialised = 1u;
    return TIKU_I2C_OK;
}

/**
 * @brief Disable the TWIM and release its pins.
 *
 * Clears ENABLE, disconnects the PSEL routing so SDA/SCL fall back to plain
 * GPIO, and marks the driver uninitialised.
 */
void tiku_i2c_arch_close(void)
{
    TIKU_TWIM->ENABLE   = TIKU_TWIM_ENABLE_OFF;
    TIKU_TWIM->PSEL.SCL = TIKU_PSEL_DISCONNECTED;
    TIKU_TWIM->PSEL.SDA = TIKU_PSEL_DISCONNECTED;
    i2c_initialised = 0u;
}

/**
 * @brief Write bytes to an I2C slave (START, addr+W, data, STOP).
 *
 * @param addr  7-bit slave address.
 * @param buf   Bytes to transmit.
 * @param len   Number of bytes; 0 returns TIKU_I2C_OK immediately.
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or
 *         @p buf is NULL with len > 0, TIKU_I2C_ERR_NACK on slave NACK,
 *         TIKU_I2C_ERR_TIMEOUT on a wedged bus.
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint32_t err;
    int rc;

    if (i2c_initialised == 0u || (buf == (const uint8_t *)0 && len > 0u)) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (len == 0u) {
        return TIKU_I2C_OK;
    }

    rc = twim_run(addr, TIKU_TWIM_SH_TX_STOP, buf, len,
                  (uint8_t *)0, 0u, &err);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    if ((err & TIKU_TWIM_ERR_NACK_BITS) != 0u) {
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}

/**
 * @brief Read bytes from an I2C slave (START, addr+R, data, NACK, STOP).
 *
 * @param addr  7-bit slave address.
 * @param buf   Destination buffer.
 * @param len   Number of bytes; 0 returns TIKU_I2C_OK immediately.
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or
 *         @p buf is NULL, TIKU_I2C_ERR_NACK on slave NACK,
 *         TIKU_I2C_ERR_TIMEOUT on a wedged bus.
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint32_t err;
    int rc;

    if (i2c_initialised == 0u || buf == (uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (len == 0u) {
        return TIKU_I2C_OK;
    }

    rc = twim_run(addr, TIKU_TWIM_SH_RX_STOP, (const uint8_t *)0, 0u,
                  buf, len, &err);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    if ((err & TIKU_TWIM_ERR_NACK_BITS) != 0u) {
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}

/**
 * @brief Probe an address for a bus scan (presence check).
 *
 * The nRF TWIM will not clock the address for a zero-length transfer, so the
 * probe is a real 1-byte write and presence is decided solely by the address
 * ACK.  A data NACK still proves the device is present.
 *
 * @param addr  7-bit slave address.
 * @return TIKU_I2C_OK if the device acknowledged its address,
 *         TIKU_I2C_ERR_NACK if not, TIKU_I2C_ERR_PARAM if uninitialised,
 *         TIKU_I2C_ERR_TIMEOUT on a wedged bus.
 */
int tiku_i2c_arch_probe(uint8_t addr)
{
    uint32_t err;
    int rc;

    if (i2c_initialised == 0u) {
        return TIKU_I2C_ERR_PARAM;
    }

    twim_probe_byte = 0u;
    rc = twim_run(addr, TIKU_TWIM_SH_TX_STOP, &twim_probe_byte, 1u,
                  (uint8_t *)0, 0u, &err);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    if ((err & TWIM_ERRORSRC_ANACK_Msk) != 0u) {
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}

/**
 * @brief Combined write-then-read with a repeated START.
 *
 * Performs START, addr+W, @p tx_buf, repeated START, addr+R, @p rx_buf, STOP
 * as a single hardware transaction (LASTTX->STARTRX + LASTRX->STOP shorts).
 * A zero-length side degrades to a pure write or pure read.
 *
 * @param addr    7-bit slave address.
 * @param tx_buf  Write-phase bytes (e.g. a register address).
 * @param tx_len  Write length; may be 0.
 * @param rx_buf  Read-phase destination.
 * @param rx_len  Read length; may be 0.
 * @return TIKU_I2C_OK on success, TIKU_I2C_ERR_PARAM if uninitialised or a
 *         non-zero length has a NULL buffer, TIKU_I2C_ERR_NACK on slave NACK,
 *         TIKU_I2C_ERR_TIMEOUT on a wedged bus.
 */
int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf, uint16_t rx_len)
{
    uint32_t err;
    int rc;

    if (i2c_initialised == 0u) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (tx_len > 0u && tx_buf == (const uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (rx_len > 0u && rx_buf == (uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (tx_len == 0u && rx_len == 0u) {
        return TIKU_I2C_OK;
    }
    /* Pure write or pure read -- delegate. */
    if (rx_len == 0u) {
        return tiku_i2c_arch_write(addr, tx_buf, tx_len);
    }
    if (tx_len == 0u) {
        return tiku_i2c_arch_read(addr, rx_buf, rx_len);
    }

    rc = twim_run(addr, TIKU_TWIM_SH_TX_STARTRX | TIKU_TWIM_SH_RX_STOP,
                  tx_buf, tx_len, rx_buf, rx_len, &err);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    if ((err & TIKU_TWIM_ERR_NACK_BITS) != 0u) {
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}
