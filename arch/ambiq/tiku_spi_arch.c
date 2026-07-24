/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - Apollo510 SPI master (IOM, bare-metal)
 *
 * A minimal blocking full-duplex SPI master on one Apollo510 I/O Master (IOM),
 * talking straight to the CMSIS register map -- no AmbiqSuite am_hal_iom. Built
 * for the EM9305 BLE radio on the Apollo510 Blue EVB (IOM6, SPI mode 0, 16 MHz),
 * so it is compiled only when TIKU_SPI_IOM_ENABLE is defined (pulled in by the
 * BLE build). Without that flag every entry point is the historic stub, so the
 * base Apollo510 / Apollo4 builds are byte-identical.
 *
 * Bring-up (functional core of am_hal_iom_configure + _enable, minus the DMA /
 * command-queue machinery we do not use):
 *   - power:  PWRCTRL.DEVPWREN.PWRENIOM6 + wait DEVPWRSTATUS;
 *   - pins:   route SCK/MOSI/MISO to the IOM via GPIO PINCFG funcsel;
 *   - config: MSPICFG (SPI mode + full-duplex), CLKCFG (48 MHz HFRC / 3 = 16 MHz
 *             via the DIV3 prescaler), enable the MSPI submodule, wait idle.
 * A transfer is polled: build the CMD (WRITE + TSIZE + full-duplex), then pump
 * the 64-byte TX/RX FIFOs a word at a time until the byte counts drain. Chip
 * select is a plain GPIO the caller drives (the IOM nCE is unused here).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"

#if defined(TIKU_SPI_IOM_ENABLE)

#include "tiku.h"            /* board pin macros via the device-select router */
#include "apollo510.h"       /* CMSIS register map (IOM / PWRCTRL / GPIO)      */
#include <stddef.h>
#include <string.h>

#if TIKU_BOARD_SPI_IOM_MODULE != 6
#error "tiku_spi_arch (IOM) currently wires the power/instance to IOM6 only"
#endif

/** The IOM instance this driver drives (IOM6 on the Blue EVB). */
#define SPI_IOM                 IOM6

/** PADKEY unlock value required before writing any GPIO PINCFG register. */
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

/**
 * @brief 16 MHz SPI clock: HFRC 48 MHz (FSEL=2) through the /3 prescaler.
 *
 * Matches what am_hal_iom's clock search picks for 16 MHz (the div3 solution is
 * preferred over div1+TOTPER). IOCLKEN turns the generated clock on.
 */
#define SPI_CLKCFG_16MHZ    ( (2u << IOM0_CLKCFG_FSEL_Pos)   |  \
                              (1u << IOM0_CLKCFG_DIV3_Pos)   |  \
                              (1u << IOM0_CLKCFG_IOCLKEN_Pos) )

/** Max bytes per single IOM command; word-aligned scratch below sizes to it.
 *  HCI-over-SPI frames to the EM9305 are small; larger callers are chunked. */
#define SPI_CHUNK_BYTES     256u

/** Loose upper bound on FIFO poll spins before a transfer is declared stuck. */
#define SPI_POLL_LIMIT      2000000u

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

static tiku_spi_config_t s_cfg;
static uint8_t           s_inited;

/* Word-aligned marshalling scratch: the IOM FIFOs move 32-bit words, so byte
 * buffers of any alignment/length are copied through these. */
static uint32_t s_txw[SPI_CHUNK_BYTES / 4];
static uint32_t s_rxw[SPI_CHUNK_BYTES / 4];

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/** Route a GPIO pad to a peripheral function (PADKEY-guarded PINCFG write). */
static void spi_pad_funcsel(uint32_t pad, uint32_t funcsel) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = funcsel;
    GPIO->PADKEY = 0u;
}

/** Spin until the IOM is idle (IDLEST set, CMDACT clear), bounded. */
static void spi_wait_idle(void) {
    uint32_t guard = 0;
    while (((SPI_IOM->STATUS &
             (IOM0_STATUS_IDLEST_Msk | IOM0_STATUS_CMDACT_Msk)) !=
            IOM0_STATUS_IDLEST_Msk) && (++guard < SPI_POLL_LIMIT)) {
        /* wait */
    }
}

/*---------------------------------------------------------------------------*/
/* Core full-duplex transfer (word-marshalled, polled)                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief One IOM full-duplex command of up to SPI_CHUNK_BYTES bytes.
 *
 * Sends @p len bytes from @p tx (or 0xFF filler when @p tx is NULL) while
 * capturing @p len bytes into @p rx (discarded when @p rx is NULL). Chip select
 * is the caller's responsibility. Returns 0 on success, -1 on a stuck FIFO.
 */
static int spi_xfer_chunk(const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (len == 0u) {
        return 0;
    }

    /* Marshal TX into the word scratch (0xFF idle bytes for read-only). The
     * final partial word's padding is never clocked out -- TSIZE bounds it. */
    if (tx) {
        memcpy(s_txw, tx, len);
    } else {
        memset(s_txw, 0xFF, ((len + 3u) & ~3u));
    }

    spi_wait_idle();
    SPI_IOM->INTCLR = 0xFFFFFFFFu;
    SPI_IOM->OFFSETHI = 0u;

    /* CMD: WRITE (full-duplex reads MISO simultaneously via MSPICFG.FULLDUP),
     * chip-select channel 0, no offset bytes, TSIZE = len. */
    SPI_IOM->CMD = ((uint32_t)len << IOM0_CMD_TSIZE_Pos) |
                   ((uint32_t)IOM0_CMD_CMD_WRITE << IOM0_CMD_CMD_Pos);

    {
        const uint32_t *txp = s_txw;
        uint32_t       *rxp = s_rxw;
        uint32_t txbytes = len, rxbytes = len;
        uint32_t guard = 0;

        while ((txbytes || rxbytes) && (++guard < SPI_POLL_LIMIT)) {
            uint32_t rem = SPI_IOM->FIFOPTR_b.FIFO0REM;  /* TX space (bytes)  */
            uint32_t siz = SPI_IOM->FIFOPTR_b.FIFO1SIZ;  /* RX ready (bytes)  */

            /* Nothing movable yet: spin until space/data appears or the
             * command completes with no further RX pending. */
            if (rem < 4u && siz < 4u) {
                if (SPI_IOM->INTSTAT_b.CMDCMP && (rxbytes > siz)) {
                    break;   /* no more data will arrive */
                }
                continue;
            }

            while (rem >= 4u && txbytes) {
                SPI_IOM->FIFOPUSH = *txp++;
                rem -= 4u;
                txbytes = (txbytes >= 4u) ? (txbytes - 4u) : 0u;
            }
            /* FIFO1SIZ is word-granular (a partial final word counts as 4), so
             * pop whole words; exact byte count is copied out below. */
            while (siz >= 4u && rxbytes) {
                *rxp++ = SPI_IOM->FIFOPOP;
                siz -= 4u;
                rxbytes = (rxbytes >= 4u) ? (rxbytes - 4u) : 0u;
            }
        }
    }

    spi_wait_idle();

    if (rx) {
        memcpy(rx, s_rxw, len);
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    uint32_t spol, spha;

    if (config == NULL) {
        return -1;
    }
    s_cfg = *config;

    /* 1. Power up the IOM6 peripheral domain. */
    PWRCTRL->DEVPWREN_b.PWRENIOM6 = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTIOM6 == 0u) {
        /* wait for the power domain */
    }

    /* 2. Route SCK/MOSI/MISO to the IOM. */
    spi_pad_funcsel(TIKU_BOARD_SPI_SCK_PIN,  TIKU_BOARD_SPI_SCK_FUNCSEL);
    spi_pad_funcsel(TIKU_BOARD_SPI_MOSI_PIN, TIKU_BOARD_SPI_MOSI_FUNCSEL);
    spi_pad_funcsel(TIKU_BOARD_SPI_MISO_PIN, TIKU_BOARD_SPI_MISO_FUNCSEL);

    /* 3. SPI config: CPOL -> SPOL, CPHA -> SPHA, plus always-on full-duplex
     *    (every EM9305 exchange clocks MOSI and MISO together). */
    spol = (uint32_t)((config->mode >> 1) & 1u);
    spha = (uint32_t)(config->mode & 1u);
    SPI_IOM->MSPICFG = (spol << IOM0_MSPICFG_SPOL_Pos) |
                       (spha << IOM0_MSPICFG_SPHA_Pos) |
                       (1u   << IOM0_MSPICFG_FULLDUP_Pos);

    /* 4. Clock: 16 MHz. */
    SPI_IOM->CLKCFG = SPI_CLKCFG_16MHZ;

    /* 5. Enable the MSPI submodule (the one whose TYPE reads MSPI). */
    if (SPI_IOM->SUBMODCTRL_b.SMOD0TYPE == IOM0_SUBMODCTRL_SMOD0TYPE_MSPI) {
        SPI_IOM->SUBMODCTRL = (1u << IOM0_SUBMODCTRL_SMOD0EN_Pos);
    } else {
        SPI_IOM->SUBMODCTRL = (1u << IOM0_SUBMODCTRL_SMOD1EN_Pos);
    }

    /* 6. Poll, not interrupt or DMA. */
    SPI_IOM->INTEN = 0u;
    SPI_IOM->DMACFG_b.DMAEN = 0u;

    spi_wait_idle();
    s_inited = 1u;
    return 0;
}

void tiku_spi_arch_close(void) {
    if (!s_inited) {
        return;
    }
    SPI_IOM->SUBMODCTRL = 0u;
    SPI_IOM->CLKCFG = 0u;
    PWRCTRL->DEVPWREN_b.PWRENIOM6 = 0u;
    s_inited = 0u;
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    uint8_t rx = 0xFFu;
    if (!s_inited) {
        return 0xFFu;
    }
    (void)spi_xfer_chunk(&tx_byte, &rx, 1u);
    return rx;
}

/** Shared chunking loop for the write / read / write_read entry points. */
static int spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (!s_inited) {
        return -1;
    }
    while (len) {
        uint16_t n = (len > SPI_CHUNK_BYTES) ? (uint16_t)SPI_CHUNK_BYTES : len;
        if (spi_xfer_chunk(tx, rx, n) != 0) {
            return -1;
        }
        if (tx) { tx += n; }
        if (rx) { rx += n; }
        len = (uint16_t)(len - n);
    }
    return 0;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    if (buf == NULL || len == 0u) {
        return -1;
    }
    return spi_xfer(buf, NULL, len);
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    if (buf == NULL || len == 0u) {
        return -1;
    }
    return spi_xfer(NULL, buf, len);
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    if (tx_buf == NULL || rx_buf == NULL || len == 0u) {
        return -1;
    }
    return spi_xfer(tx_buf, rx_buf, len);
}

#else  /* !TIKU_SPI_IOM_ENABLE -- historic stub (no SPI device wired) */

/**
 * @brief Initialize the SPI controller (stub — not built in)
 *
 * The real IOM backend is compiled only for the BLE build
 * (TIKU_SPI_IOM_ENABLE). Until then every entry point returns a hard failure
 * so callers can detect the missing backend.
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    (void)config;
    return -1;
}

void tiku_spi_arch_close(void) {
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFF;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    (void)tx_buf; (void)rx_buf; (void)len;
    return -1;
}

#endif /* TIKU_SPI_IOM_ENABLE */
