/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - SPI master for nRF54L (SPIM + EasyDMA, blocking poll).
 *
 * The SPIM has no byte FIFO: every transfer moves through EasyDMA, which can
 * only reach RAM.  Transmit data is staged through a RAM bounce buffer, so a
 * .rodata source works; a non-RAM receive buffer gets TIKU_SPI_ERR_PARAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_spi_arch.h>      /* prototypes, config, codes  */
#include <arch/nordic/tiku_device_select.h> /* MDK, RAM and board macros, */
                                            /* GPIO helpers               */
#include <string.h>                         /* memcpy for TX staging      */

/*---------------------------------------------------------------------------*/
/* INSTANCE AND PIN SELECTION                                                */
/*---------------------------------------------------------------------------*/

/*
 * SPIM instance.  The console occupies UARTE20 (SERIAL20), so SPIM20 is
 * taken; SPIM21 (SERIAL21) is the natural free peripheral-domain SPIM:
 *   - 16 MHz base clock, PRESCALER.DIVISOR 2..126 (8 MHz .. ~127 kHz),
 *   - no high-speed silicon workarounds (SPIM00 needs the 54L-57 errata
 *     write and has a CPU-frequency-dependent base clock; SPIM30 sits in
 *     the low-power domain with only a handful of P0 pins),
 *   - errata 54L-55/69 do not apply to the nRF54L15, only to the LM20,
 *     LS05, LV10 and LC10 variants (per the MDK errata header); this driver
 *     applies their transfer workaround on the LM20.
 * The SERIAL21 interrupt is unused; completion is polled on EVENTS_END.
 * The secure alias (_S, 0x500C7000) matches the rest of this port.
 */
#ifndef TIKU_BOARD_SPI0_SPIM
#define TIKU_BOARD_SPI0_SPIM        NRF_SPIM21_S
#endif

/*
 * SCK / MOSI(SDO) / MISO(SDI) pins as (port, pin), reachable by the
 * peripheral-domain SERIAL21.  SCK and MOSI sit on P1.02/P1.03, the NFC1/NFC2
 * pads: on the nRF54L15-DK every other P1 pin is taken (P1.00/01 = LFXO
 * crystal, P1.04..07 = console UART with its RTS/CTS, P1.08/09/13 = buttons,
 * P1.10/14 = LEDs, P1.11/12 = default I2C, P1.15 = MISO).  The pads reset to
 * NFC mode (NFCT.PADCONFIG); tiku_spi_arch_init() switches them to GPIO.
 * They are defaults, not checked against the DK's header routing; a board
 * header overrides them.
 * There is no chip select here: the device driver drives CS on a free GPIO
 * around each transaction.
 */
#ifndef TIKU_BOARD_SPI0_SCK_PORT
#define TIKU_BOARD_SPI0_SCK_PORT    1u
#endif
#ifndef TIKU_BOARD_SPI0_SCK_PIN
#define TIKU_BOARD_SPI0_SCK_PIN     2u
#endif
#ifndef TIKU_BOARD_SPI0_MOSI_PORT
#define TIKU_BOARD_SPI0_MOSI_PORT   1u
#endif
#ifndef TIKU_BOARD_SPI0_MOSI_PIN
#define TIKU_BOARD_SPI0_MOSI_PIN    3u
#endif
#ifndef TIKU_BOARD_SPI0_MISO_PORT
#define TIKU_BOARD_SPI0_MISO_PORT   1u
#endif
#ifndef TIKU_BOARD_SPI0_MISO_PIN
#define TIKU_BOARD_SPI0_MISO_PIN    15u
#endif

/** @brief Selected SPIM instance. */
#define TIKU_SPIM                   TIKU_BOARD_SPI0_SPIM

/** @brief NFCT register block whose PADCONFIG selects NFC or GPIO pads. */
#ifndef TIKU_SPI_NFCT
#define TIKU_SPI_NFCT               NRF_NFCT_S
#endif

/* The NFC1/NFC2 pads, P1.02 and P1.03 on the nRF54L15 and nRF54LM20. */
#define TIKU_SPI_NFC_PORT           1u
#define TIKU_SPI_NFC1_PIN           2u
#define TIKU_SPI_NFC2_PIN           3u

/** @brief Non-zero when (port, pin) is an NFC pad. */
#define TIKU_SPI_PIN_IS_NFC(port, pin) \
    ((port) == TIKU_SPI_NFC_PORT && \
     ((pin) == TIKU_SPI_NFC1_PIN || (pin) == TIKU_SPI_NFC2_PIN))

/*---------------------------------------------------------------------------*/
/* REGISTER FIELD CONSTANTS (MDK nrf54l15_types.h)                           */
/*---------------------------------------------------------------------------*/

/** @brief PSEL word: bits[4:0]=pin, bits[7:5]=port, bit31=0 -> connected. */
#define TIKU_SPI_PSEL(port, pin) \
    (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/** @brief PSEL word that disconnects the pin (CONNECT bit set). */
#define TIKU_SPI_PSEL_DISCONNECTED  0xFFFFFFFFUL

/* ENABLE.ENABLE values; the SPIM's is 0x7, the UARTE's 0x8. */
#define TIKU_SPIM_ENABLE_ENABLED    0x7UL        /**< SPIM enabled          */
#define TIKU_SPIM_ENABLE_DISABLED   0x0UL        /**< SPIM disabled         */

/* CONFIG bit fields: ORDER (bit0), CPHA (bit1), CPOL (bit2). */
#define TIKU_SPIM_CONFIG_LSBFIRST   (1UL << 0)   /**< ORDER = LsbFirst      */
#define TIKU_SPIM_CONFIG_CPHA       (1UL << 1)   /**< CPHA  = Trailing      */
#define TIKU_SPIM_CONFIG_CPOL       (1UL << 2)   /**< CPOL  = ActiveLow     */

/* PRESCALER.DIVISOR range; SCK = 16 MHz / DIVISOR, which must be even. */
#define TIKU_SPIM_DIV_MIN           2u           /**< 8 MHz                 */
#define TIKU_SPIM_DIV_MAX           126u         /**< ~127 kHz              */
#define TIKU_SPIM_DIV_DEFAULT       16u          /**< 16 MHz / 16 = 1 MHz   */

/** @brief Over-read char clocked out while receiving (read filler). */
#define TIKU_SPIM_ORC_IDLE          0xFFUL

/**
 * @brief Poll bound for one DMA burst.
 *
 * A burst moves at most TIKU_SPIM_CHUNK (64) bytes, about 4 ms at the
 * slowest SCK (~127 kHz); 2 M polls take about 60 ms at 128 MHz.
 */
#define TIKU_SPIM_SPIN_LIMIT        2000000UL

/** @brief Poll bound for EVENTS_STOPPED after TASKS_STOP on a timed-out
 *         burst, before the SPIM is disabled. */
#define TIKU_SPIM_STOP_LIMIT        100000UL

/**
 * @brief Transmit bounce-chunk size (bytes).
 *
 * Transmit data is staged in chunks of this size, so an RRAM source is
 * DMA-able.  The size bounds the static RAM cost and each burst's time
 * against the poll bound above.
 */
#define TIKU_SPIM_CHUNK             64u

/*---------------------------------------------------------------------------*/
/* EASYDMA STAGING (RAM, WORD-ALIGNED)                                       */
/*---------------------------------------------------------------------------*/

/** @brief Transmit bounce buffer; also a valid RAM pointer for 0-length TX. */
static uint8_t spim_txbuf[TIKU_SPIM_CHUNK] __attribute__((aligned(4)));

/** @brief One-byte receive landing for tiku_spi_arch_transfer(). */
static uint8_t spim_rx1 __attribute__((aligned(4)));

/** @brief Non-zero once tiku_spi_arch_init() has succeeded. */
static uint8_t spim_initialised;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Test whether an address is EasyDMA-reachable RAM.
 *
 * The SPIM DMAs only to and from on-chip SRAM: a receive buffer elsewhere
 * (RRAM, flash) raises a bus error.  Only the TIKU_DEVICE_RAM window counts.
 *
 * @param p  Address to test.
 * @return 1 if @p p lies within on-chip RAM, 0 otherwise.
 */
static int spim_addr_is_ram(const void *p)
{
    uint32_t a = (uint32_t)p;

    return (a >= TIKU_DEVICE_RAM_START &&
            a <  (TIKU_DEVICE_RAM_START + TIKU_DEVICE_RAM_SIZE));
}

/**
 * @brief Switch the NFC pads to GPIO when an SPI pin is one of them.
 *
 * NFCT.PADCONFIG resets to 1 (NFC antenna pins) on every reset; writing 0
 * makes both pads plain GPIO.  Nothing in the port uses the NFCT.
 */
static void spim_nfc_pads_to_gpio(void)
{
    if (TIKU_SPI_PIN_IS_NFC(TIKU_BOARD_SPI0_SCK_PORT,
                            TIKU_BOARD_SPI0_SCK_PIN) ||
        TIKU_SPI_PIN_IS_NFC(TIKU_BOARD_SPI0_MOSI_PORT,
                            TIKU_BOARD_SPI0_MOSI_PIN) ||
        TIKU_SPI_PIN_IS_NFC(TIKU_BOARD_SPI0_MISO_PORT,
                            TIKU_BOARD_SPI0_MISO_PIN)) {
        TIKU_SPI_NFCT->PADCONFIG = 0UL;
    }
}

/** @brief Apply the LM20 SPIM 55/69 transfer workaround. */
static void spim_errata(int active)
{
#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
    *(volatile uint32_t *)((uint8_t *)TIKU_SPIM + 0xc80u) =
        active ? 0x82u : 0u;
#else
    (void)active;
#endif
}

/**
 * @brief Run one blocking SPIM DMA burst and wait for completion.
 *
 * Points EasyDMA at the given RAM buffers, triggers TASKS_START and spins on
 * EVENTS_END with a bounded poll.  Both pointers must reference on-chip RAM.
 *
 * @note On the LM20 the errata 55/69 register is set before TASKS_START and
 *       cleared after EVENTS_END and after a timeout.
 * @note A timed-out burst is stopped with TASKS_STOP, EVENTS_STOPPED is
 *       polled with a bounded wait so EasyDMA is not cut mid-transfer, and
 *       the SPIM is then disabled; transfers are refused until
 *       tiku_spi_arch_init() runs again.
 * @note When @p txlen < @p rxlen the over-read character (ORC, 0xFF at init) is
 *       clocked out for the remaining receive bytes; when @p rxlen is 0 the
 *       received bytes are not written to RAM.
 * @param txp    Transmit RAM address (valid even when @p txlen == 0).
 * @param txlen  Transmit byte count (<= 0xFFFF).
 * @param rxp    Receive RAM address (valid even when @p rxlen == 0).
 * @param rxlen  Receive byte count (<= 0xFFFF).
 * @return 0 on completion, -1 on poll timeout.
 */
static int spim_run(uint32_t txp, uint32_t txlen,
                    uint32_t rxp, uint32_t rxlen)
{
    uint32_t spin;

    /* Clear the completion and stopped events and read one back so the
     * clears have landed before the next transfer is armed (write-buffer
     * flush). */
    TIKU_SPIM->EVENTS_END = 0UL;
    TIKU_SPIM->EVENTS_STOPPED = 0UL;
    (void)TIKU_SPIM->EVENTS_END;

    TIKU_SPIM->DMA.TX.PTR    = txp;
    TIKU_SPIM->DMA.TX.MAXCNT = txlen;
    TIKU_SPIM->DMA.RX.PTR    = rxp;
    TIKU_SPIM->DMA.RX.MAXCNT = rxlen;

    spim_errata(1);
    TIKU_SPIM->TASKS_START = 1UL;

    for (spin = 0UL; spin < TIKU_SPIM_SPIN_LIMIT; spin++) {
        if (TIKU_SPIM->EVENTS_END != 0UL) {
            TIKU_SPIM->EVENTS_END = 0UL;
            (void)TIKU_SPIM->EVENTS_END;
            spim_errata(0);
            return 0;
        }
    }
    TIKU_SPIM->TASKS_STOP = 1UL;
    for (spin = 0UL; spin < TIKU_SPIM_STOP_LIMIT; spin++) {
        if (TIKU_SPIM->EVENTS_STOPPED != 0UL) {
            break;
        }
    }
    TIKU_SPIM->ENABLE = TIKU_SPIM_ENABLE_DISABLED;
    spim_initialised = 0u;
    spim_errata(0);
    return -1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the SPIM master with the given configuration.
 *
 * Switches an NFC pad among the SPI pins to GPIO, parks the pads at idle
 * levels, routes them via PSEL, programs CONFIG (order, CPOL/CPHA),
 * PRESCALER (SCK = 16 MHz / DIVISOR) and the ORC, then enables the SPIM.
 *
 * @note TIKU_SPI_LSB_FIRST is supported, through CONFIG.ORDER.
 * @param config  Bus parameters (mode, bit order, prescaler/divisor).
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM on NULL config or an
 *         out-of-range mode.
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    uint32_t cfg;
    uint32_t div;

    if (config == (const tiku_spi_config_t *)0) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->mode > TIKU_SPI_MODE_3) {
        return TIKU_SPI_ERR_PARAM;
    }

    spim_nfc_pads_to_gpio();

    /* Park the pads before handing them to the SPIM so the bus idles in a
     * defined state.  SCK idle level follows CPOL (modes 2/3 idle high). */
    tiku_nordic_gpio_init_output(TIKU_BOARD_SPI0_SCK_PORT,
                                 TIKU_BOARD_SPI0_SCK_PIN,
                                 (config->mode & 0x2u) ? 1u : 0u);
    tiku_nordic_gpio_init_output(TIKU_BOARD_SPI0_MOSI_PORT,
                                 TIKU_BOARD_SPI0_MOSI_PIN, 0u);
    tiku_nordic_gpio_init_input_pullup(TIKU_BOARD_SPI0_MISO_PORT,
                                       TIKU_BOARD_SPI0_MISO_PIN);

    /* Program while disabled. */
    TIKU_SPIM->ENABLE = TIKU_SPIM_ENABLE_DISABLED;

    TIKU_SPIM->PSEL.SCK  = TIKU_SPI_PSEL(TIKU_BOARD_SPI0_SCK_PORT,
                                         TIKU_BOARD_SPI0_SCK_PIN);
    TIKU_SPIM->PSEL.MOSI = TIKU_SPI_PSEL(TIKU_BOARD_SPI0_MOSI_PORT,
                                         TIKU_BOARD_SPI0_MOSI_PIN);
    TIKU_SPIM->PSEL.MISO = TIKU_SPI_PSEL(TIKU_BOARD_SPI0_MISO_PORT,
                                         TIKU_BOARD_SPI0_MISO_PIN);

    /* CONFIG: bit order + CPOL/CPHA.  For TikuOS mode m = (CPOL<<1)|CPHA,
     * and the Nordic CPHA/CPOL bits map 1:1 (CPHA bit1, CPOL bit2). */
    cfg = 0UL;
    if (config->bit_order == TIKU_SPI_LSB_FIRST) {
        cfg |= TIKU_SPIM_CONFIG_LSBFIRST;
    }
    if (config->mode & 0x1u) {
        cfg |= TIKU_SPIM_CONFIG_CPHA;
    }
    if (config->mode & 0x2u) {
        cfg |= TIKU_SPIM_CONFIG_CPOL;
    }
    TIKU_SPIM->CONFIG = cfg;

    /* PRESCALER.DIVISOR: interpret the config prescaler as the 16 MHz-base
     * divisor.  Clamp into [2,126]; an unset (0/1) value defaults to ~1 MHz.
     * The divisor must be even, so round down. */
    div = (uint32_t)config->prescaler;
    if (div < TIKU_SPIM_DIV_MIN) {
        div = TIKU_SPIM_DIV_DEFAULT;
    }
    if (div > TIKU_SPIM_DIV_MAX) {
        div = TIKU_SPIM_DIV_MAX;
    }
    div &= ~1UL;
    TIKU_SPIM->PRESCALER = div;

    /* Byte clocked out while receiving (read filler / over-read char). */
    TIKU_SPIM->ORC = TIKU_SPIM_ORC_IDLE;

    TIKU_SPIM->ENABLE = TIKU_SPIM_ENABLE_ENABLED;

    spim_initialised = 1u;
    return TIKU_SPI_OK;
}

/**
 * @brief Disable the SPIM controller and release its pins.
 *
 * Disables the peripheral and disconnects the PSEL routing so the pads
 * revert to GPIO control.  Subsequent transfer calls return the idle/error
 * results until a fresh init.
 */
void tiku_spi_arch_close(void)
{
    TIKU_SPIM->ENABLE = TIKU_SPIM_ENABLE_DISABLED;
    TIKU_SPIM->PSEL.SCK  = TIKU_SPI_PSEL_DISCONNECTED;
    TIKU_SPIM->PSEL.MOSI = TIKU_SPI_PSEL_DISCONNECTED;
    TIKU_SPIM->PSEL.MISO = TIKU_SPI_PSEL_DISCONNECTED;
    spim_initialised = 0u;
}

/**
 * @brief Full-duplex single-byte transfer.
 *
 * Clocks out @p tx_byte from the RAM bounce buffer and returns the byte
 * shifted in on MISO during the same clocks.
 *
 * @param tx_byte  Byte to transmit.
 * @return Received byte, or 0xFF if not initialised or on a poll timeout
 *         (the level of an idle, pulled-up MISO).
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte)
{
    if (spim_initialised == 0u) {
        return 0xFFu;
    }

    spim_txbuf[0] = tx_byte;
    if (spim_run((uint32_t)spim_txbuf, 1UL,
                 (uint32_t)&spim_rx1, 1UL) < 0) {
        return 0xFFu;
    }
    return spim_rx1;
}

/**
 * @brief Write a buffer over SPI, discarding received bytes.
 *
 * Stages @p buf through the static RAM bounce buffer in chunks (so the
 * source may live in RRAM/flash or RAM) and clocks each chunk out with the
 * receive length set to 0.
 *
 * @param buf  Source bytes (may be NULL only when @p len == 0).
 * @param len  Number of bytes to transmit.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if not initialised or
 *         @p buf is NULL with a non-zero length, TIKU_SPI_ERR_TIMEOUT on a
 *         poll timeout.
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    uint32_t off;
    uint32_t n;
    uint32_t total = (uint32_t)len;

    if (spim_initialised == 0u ||
        (buf == (const uint8_t *)0 && len > 0u)) {
        return TIKU_SPI_ERR_PARAM;
    }

    for (off = 0UL; off < total; off += n) {
        n = total - off;
        if (n > TIKU_SPIM_CHUNK) {
            n = TIKU_SPIM_CHUNK;
        }
        memcpy(spim_txbuf, &buf[off], n);
        /* RX length 0: nothing written back; the RX pointer just needs to
         * be a valid RAM address. */
        if (spim_run((uint32_t)spim_txbuf, n,
                     (uint32_t)spim_txbuf, 0UL) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }
    return TIKU_SPI_OK;
}

/**
 * @brief Read a buffer over SPI, clocking out the 0xFF over-read char.
 *
 * Receives directly into the caller's buffer in bounded chunks with the
 * transmit length set to 0; the SPIM shifts out ORC (0xFF) for each
 * received byte.  The destination must be on-chip RAM.
 *
 * @param buf  Destination buffer (may be NULL only when @p len == 0).
 * @param len  Number of bytes to receive.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if not initialised,
 *         @p buf is NULL with a non-zero length, or the destination is not
 *         DMA-reachable RAM, TIKU_SPI_ERR_TIMEOUT on a poll timeout.
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    uint32_t off;
    uint32_t n;
    uint32_t total = (uint32_t)len;

    if (spim_initialised == 0u ||
        (buf == (uint8_t *)0 && len > 0u)) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (len > 0u && !spim_addr_is_ram(buf)) {
        return TIKU_SPI_ERR_PARAM;
    }

    for (off = 0UL; off < total; off += n) {
        n = total - off;
        if (n > TIKU_SPIM_CHUNK) {
            n = TIKU_SPIM_CHUNK;
        }
        /* TX length 0: ORC (0xFF) is clocked out for each received byte. */
        if (spim_run((uint32_t)spim_txbuf, 0UL,
                     (uint32_t)&buf[off], n) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }
    return TIKU_SPI_OK;
}

/**
 * @brief Full-duplex transfer over two equal-length buffers.
 *
 * Transmits from @p tx_buf while receiving into @p rx_buf chunk by chunk: the
 * transmit slice stages through the RAM bounce buffer, so any source region
 * works, and the receive slice lands directly in the caller's buffer.
 *
 * @note The caller holds chip select across the whole call; SCK pauses
 *       briefly between chunks.
 * @param tx_buf  Source bytes (non-NULL when @p len > 0).
 * @param rx_buf  Destination buffer (non-NULL, DMA-reachable RAM, when
 *                @p len > 0).
 * @param len     Number of bytes to exchange.
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM if not initialised,
 *         either buffer is NULL with @p len > 0, or the destination is not
 *         DMA-reachable RAM, TIKU_SPI_ERR_TIMEOUT on a poll timeout.
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len)
{
    uint32_t off;
    uint32_t n;
    uint32_t total = (uint32_t)len;

    if (spim_initialised == 0u) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (len > 0u && (tx_buf == (const uint8_t *)0 ||
                     rx_buf == (uint8_t *)0)) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (len > 0u && !spim_addr_is_ram(rx_buf)) {
        return TIKU_SPI_ERR_PARAM;
    }

    for (off = 0UL; off < total; off += n) {
        n = total - off;
        if (n > TIKU_SPIM_CHUNK) {
            n = TIKU_SPIM_CHUNK;
        }
        memcpy(spim_txbuf, &tx_buf[off], n);
        if (spim_run((uint32_t)spim_txbuf, n,
                     (uint32_t)&rx_buf[off], n) < 0) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }
    return TIKU_SPI_OK;
}
