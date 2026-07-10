/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - SPI master for nRF54L (SPIM + EasyDMA, blocking poll)
 *
 * Master-only, 8-bit frames, synchronous polled I/O on the nRF54L SPIM
 * peripheral.  The nRF54L SPIM has no byte FIFO: every transfer moves
 * through EasyDMA against a RAM buffer, using the "new" register layout
 * (DMA.TX/RX.PTR/MAXCNT) rather than the older nRF52 TXD/RXD block.  A
 * single transaction is:
 *
 *   1. clear EVENTS_END
 *   2. DMA.TX.PTR/MAXCNT  <- transmit buffer / length
 *      DMA.RX.PTR/MAXCNT  <- receive buffer  / length
 *   3. trigger TASKS_START
 *   4. spin on EVENTS_END
 *
 * (TASKS_START + EVENTS_END are retained on the nRF54L SPIM even though
 * the DMA buffer registers moved; the TASKS_DMA/EVENTS_DMA sub-blocks
 * here only drive the RX pattern-matcher, which this driver does not
 * use -- verified against the MDK NRF_SPIM_Type struct.)
 *
 * EasyDMA can only reach RAM (0x2000_0000..).  The interface hands the
 * transmit pointer straight through from the caller, and callers legally
 * pass RRAM/flash-resident constants (e.g. an e-paper image or LUT living
 * in .rodata).  Those cannot be DMA sources, so the transmit path is
 * staged through a static RAM bounce buffer in bounded chunks -- this
 * makes tiku_spi_arch_write()/write_read() accept any source address,
 * matching the CPU-copy behaviour of the PL022 (RP2350) backend.  Receive
 * data lands directly in the caller's buffer (a receive destination is by
 * definition writable RAM); a defensive RAM-reachability check rejects a
 * non-RAM destination with TIKU_SPI_ERR_PARAM rather than faulting.
 *
 * Instance & pins (see the block above the config defines): SPIM21 on
 * P1.11/P1.12/P1.15.  The nRF54L15-DK board header carries no SPI pin
 * assignment yet, so these are a documented, board-overridable default
 * to be confirmed against the DK schematic -- not a hardware-verified
 * mapping.
 *
 * Received bytes are the real MISO levels: with no slave driving the bus
 * an idle (pulled-up) MISO reads 0xFF, which is the honest hardware
 * result, not fabricated data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_spi_arch.h>        /* prototypes + tiku_spi_config_t + codes */
#include <arch/nordic/tiku_device_select.h>   /* MDK types, NRF_SPIMxx_S, RAM macros,   */
                                              /*   board macros + gpio helpers          */
#include <string.h>                           /* memcpy: transmit bounce staging        */

/*---------------------------------------------------------------------------*/
/* Instance and pin selection                                                */
/*---------------------------------------------------------------------------*/

/*
 * SPIM instance.  The console occupies UARTE20 (SERIAL20), so SPIM20 is
 * taken; SPIM21 (SERIAL21) is the natural free peripheral-domain SPIM:
 *   - 16 MHz base clock, PRESCALER.DIVISOR 2..126 (8 MHz .. ~127 kHz),
 *   - no high-speed silicon workarounds (SPIM00 needs the 54L-57 errata
 *     write and has a CPU-frequency-dependent base clock; SPIM30 sits in
 *     the low-power domain with only a handful of P0 pins),
 *   - errata 54L-55/69 do NOT apply to the nRF54L15 (only to the LM20 /
 *     LS05 / LV10 / LC10 variants -- confirmed in the MDK erratas), so no
 *     magic-register workaround is required here.
 * The SERIAL21 interrupt is unused; completion is polled on EVENTS_END.
 * The secure alias (_S, 0x500C7000) matches the rest of this port.
 */
#ifndef TIKU_BOARD_SPI0_SPIM
#define TIKU_BOARD_SPI0_SPIM        NRF_SPIM21_S
#endif

/*
 * SCK / MOSI(SDO) / MISO(SDI) pins as (port, pin).  These are free P1 pins
 * on the nRF54L15-DK (P1.04/05 = console UART, P1.08/09/13 = buttons,
 * P1.10/14 = LEDs), reachable by the peripheral-domain SERIAL21.  They are
 * a documented default, NOT a hardware-verified Arduino-header mapping --
 * override in the board header once the DK routing is confirmed.  Chip
 * select is intentionally absent: the interface layer / device driver
 * drives CS on a free GPIO around each transaction.
 */
#ifndef TIKU_BOARD_SPI0_SCK_PORT
#define TIKU_BOARD_SPI0_SCK_PORT    1u
#endif
#ifndef TIKU_BOARD_SPI0_SCK_PIN
#define TIKU_BOARD_SPI0_SCK_PIN     11u
#endif
#ifndef TIKU_BOARD_SPI0_MOSI_PORT
#define TIKU_BOARD_SPI0_MOSI_PORT   1u
#endif
#ifndef TIKU_BOARD_SPI0_MOSI_PIN
#define TIKU_BOARD_SPI0_MOSI_PIN    12u
#endif
#ifndef TIKU_BOARD_SPI0_MISO_PORT
#define TIKU_BOARD_SPI0_MISO_PORT   1u
#endif
#ifndef TIKU_BOARD_SPI0_MISO_PIN
#define TIKU_BOARD_SPI0_MISO_PIN    15u
#endif

/** @brief Selected SPIM instance. */
#define TIKU_SPIM                   TIKU_BOARD_SPI0_SPIM

/*---------------------------------------------------------------------------*/
/* Register field constants (from the MDK nrf54l15_types.h)                  */
/*---------------------------------------------------------------------------*/

/** @brief PSEL word: bits[4:0]=pin, bits[7:5]=port, bit31=0 -> connected. */
#define TIKU_SPI_PSEL(port, pin) \
    (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/** @brief PSEL word that disconnects the pin (CONNECT bit set). */
#define TIKU_SPI_PSEL_DISCONNECTED  0xFFFFFFFFUL

/** @brief ENABLE.ENABLE enumerations (SPIM uses 0x7, unlike UARTE's 0x8). */
#define TIKU_SPIM_ENABLE_ENABLED    0x7UL
#define TIKU_SPIM_ENABLE_DISABLED   0x0UL

/** @brief CONFIG bit fields: ORDER (bit0), CPHA (bit1), CPOL (bit2). */
#define TIKU_SPIM_CONFIG_LSBFIRST   (1UL << 0)   /**< ORDER = LsbFirst      */
#define TIKU_SPIM_CONFIG_CPHA       (1UL << 1)   /**< CPHA  = Trailing      */
#define TIKU_SPIM_CONFIG_CPOL       (1UL << 2)   /**< CPOL  = ActiveLow     */

/** @brief PRESCALER.DIVISOR range; SCK = 16 MHz / DIVISOR, must be even. */
#define TIKU_SPIM_DIV_MIN           2u
#define TIKU_SPIM_DIV_MAX           126u
#define TIKU_SPIM_DIV_DEFAULT       16u          /**< 16 MHz / 16 = 1 MHz   */

/** @brief Over-read char clocked out while receiving (read filler). */
#define TIKU_SPIM_ORC_IDLE          0xFFUL

/**
 * @brief Poll bound for one DMA burst.
 *
 * Every hardware transaction moves at most TIKU_SPIM_CHUNK (64) bytes,
 * whose worst-case duration is ~4 ms at the slowest SCK (~127 kHz).  At
 * the 128 MHz core this ~2 M-iteration spin is ~60 ms, comfortably above
 * that yet still a hard ceiling so a wedged bus cannot hang the kernel.
 */
#define TIKU_SPIM_SPIN_LIMIT        2000000UL

/**
 * @brief Transmit bounce-chunk size (bytes).
 *
 * Bulk transmit is staged here so RRAM/flash-resident source buffers are
 * DMA-able; kept small to bound both the static RAM cost and the per-burst
 * transfer time relative to the poll ceiling above.
 */
#define TIKU_SPIM_CHUNK             64u

/*---------------------------------------------------------------------------*/
/* EasyDMA staging (must be RAM, word-aligned)                               */
/*---------------------------------------------------------------------------*/

/** @brief Transmit bounce buffer; also a valid RAM pointer for 0-length TX. */
static uint8_t spim_txbuf[TIKU_SPIM_CHUNK] __attribute__((aligned(4)));

/** @brief One-byte receive landing for tiku_spi_arch_transfer(). */
static uint8_t spim_rx1 __attribute__((aligned(4)));

/** @brief Non-zero once tiku_spi_arch_init() has succeeded. */
static uint8_t spim_initialised;

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Test whether an address is EasyDMA-reachable RAM.
 *
 * The nRF54L SPIM can only DMA to/from on-chip SRAM
 * (@ref TIKU_DEVICE_RAM_START).  A receive destination outside that
 * window (e.g. an RRAM/flash pointer) is rejected rather than allowed to
 * raise a bus error.
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
 * @brief Run one blocking SPIM DMA burst and wait for completion.
 *
 * Points EasyDMA at the given RAM buffers, triggers TASKS_START, and
 * spins on EVENTS_END with a bounded poll.  When @p txlen < @p rxlen the
 * over-read character (ORC, set to 0xFF at init) is clocked out for the
 * remaining receive bytes; when @p rxlen is 0 the received bytes are not
 * written to RAM.  Both pointers must reference on-chip RAM.
 *
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

    /* Clear the completion event and read it back so the clear has landed
     * before we arm the next transfer (write-buffer flush). */
    TIKU_SPIM->EVENTS_END = 0UL;
    (void)TIKU_SPIM->EVENTS_END;

    TIKU_SPIM->DMA.TX.PTR    = txp;
    TIKU_SPIM->DMA.TX.MAXCNT = txlen;
    TIKU_SPIM->DMA.RX.PTR    = rxp;
    TIKU_SPIM->DMA.RX.MAXCNT = rxlen;

    TIKU_SPIM->TASKS_START = 1UL;

    for (spin = 0UL; spin < TIKU_SPIM_SPIN_LIMIT; spin++) {
        if (TIKU_SPIM->EVENTS_END != 0UL) {
            TIKU_SPIM->EVENTS_END = 0UL;
            (void)TIKU_SPIM->EVENTS_END;
            return 0;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the SPIM master with the given configuration.
 *
 * Parks the SCK/MOSI/MISO pads at defined idle levels (SCK at the CPOL
 * idle, MOSI low, MISO input with pull-up), routes them via PSEL, programs
 * CONFIG (bit order + CPOL/CPHA), PRESCALER (SCK = 16 MHz / DIVISOR), and
 * the over-read character, then enables the peripheral.  Unlike the PL022
 * backend the nRF54L SPIM supports LSB-first natively (CONFIG.ORDER), so
 * TIKU_SPI_LSB_FIRST is honoured rather than rejected.
 *
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
 * Clocks out @p tx_byte (via a 1-byte RAM bounce, EasyDMA cannot reach a
 * register/stack temporary reliably as a "buffer") and returns the byte
 * shifted in on MISO during the same clocks.
 *
 * @param tx_byte  Byte to transmit.
 * @return Received byte, or 0xFF if not initialised or on a poll timeout
 *         (the same idle-high level a floating MISO reads back).
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
 * Transmits from @p tx_buf while receiving into @p rx_buf, chunk by chunk:
 * the transmit slice is staged through the RAM bounce buffer (any source
 * region) and the receive slice lands directly in the caller's buffer.
 * Chip select is held by the caller across the whole call, so the brief
 * clock gaps between chunks are transparent to the slave.
 *
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
