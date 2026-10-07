/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_em9305.h - EM9305 BLE controller SPI-HCI transport.
 *
 * Resets the Apollo510B's EM9305 radio and moves raw HCI packets over its
 * framed SPI, for the BLE host stack and a self-test probe and beacon; the
 * USB PHY shares the die for its 12 MHz clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EM9305_H_
#define TIKU_EM9305_H_

#include <stdint.h>

/** @brief Result codes for the EM9305 transport. */
#define TIKU_EM9305_OK             0    /**< success                       */
#define TIKU_EM9305_ERR_RESET    (-1)   /**< radio did not leave reset     */
#define TIKU_EM9305_ERR_NOTREADY (-2)   /**< STS never ready, or bad reply */
#define TIKU_EM9305_ERR_TIMEOUT  (-3)   /**< no RDY or no reply in time    */
#define TIKU_EM9305_ERR_PARAM    (-4)   /**< bad argument                  */

/**
 * @brief Diagnostic snapshot filled by tiku_em9305_probe().
 *
 * Shows whether SPI reaches the radio (sts1 == 0xC0), the radio booted (its
 * active-state event arrived) and HCI answers (Reset -> Command Complete).
 */
typedef struct {
    int      reset_rc;        /**< tiku_em9305_reset() result                 */
    uint8_t  spi_rc;          /**< 1 if tiku_spi_init() failed, else 0        */
    uint8_t  rdy_initial;     /**< RDY level before the EN pulse              */
    uint8_t  saw_low;         /**< RDY observed low during reset              */
    uint8_t  saw_high;        /**< RDY observed high (ready) during reset     */
    uint8_t  rdy_final;       /**< RDY level at the end of reset              */
    uint8_t  sts1;            /**< boot-event read's STS1 (0xC0 = ready)      */
    uint8_t  sts2;            /**< boot-event read's STS2 (byte count)        */
    uint8_t  active_evt;      /**< 1 if the 04 FF boot event arrived          */
    uint8_t  cc_seen;         /**< 1 if an HCI Command Complete came back     */
    uint8_t  hci_status;      /**< HCI Reset status byte (0 = success)        */
    int8_t   send_rc;         /**< tiku_em9305_send(HCI Reset) result         */
    int8_t   recv_rc;         /**< tiku_em9305_recv(reply) result             */
    uint8_t  evt[16];         /**< raw bytes of the reply read                */
    uint16_t evt_len;         /**< number of valid bytes in evt[]             */
} tiku_em9305_probe_t;

/** @brief Users of the die: the BLE host stack and the USB high-speed PHY. */
#define TIKU_EM9305_USER_BLE   0x01u
#define TIKU_EM9305_USER_USB   0x02u

/**
 * @brief Take the die for @p user (a TIKU_EM9305_USER_* bit): the first user
 *        resets it, a later one finds it running.
 * @return TIKU_EM9305_OK, or the tiku_em9305_reset() error
 */
int tiku_em9305_acquire(uint8_t user);

/**
 * @brief Give the die back for @p user; once no user holds it, EN goes low
 *        and the die stays in reset, its clock output stopped.
 */
void tiku_em9305_release(uint8_t user);

/**
 * @brief Set up the radio's pads (first call only) and IOM6 SPI, pulse EN,
 *        and wait for RDY to fall and rise again.
 * @return TIKU_EM9305_OK, or TIKU_EM9305_ERR_RESET if SPI init fails or RDY
 *         never rises.
 */
int tiku_em9305_reset(void);

/**
 * @brief Send one raw HCI packet (type byte included) to the controller.
 *
 * Splits the packet across frames by the space each frame's STS2 reports.
 *
 * @param data  HCI packet bytes (e.g. {0x01, 0x03, 0x0C, 0x00} = HCI Reset).
 * @param len   Packet length in bytes.
 * @return TIKU_EM9305_OK, TIKU_EM9305_ERR_PARAM for no data, or the frame or
 *         SPI error.
 */
int tiku_em9305_send(const uint8_t *data, uint16_t len);

/**
 * @brief Wait (up to @p timeout_ms) for the controller to raise RDY, then read
 *        one frame, at most @p cap bytes, into @p buf.
 *
 * A frame may hold part of an HCI packet or several packets.
 *
 * @param buf         Destination buffer.
 * @param cap         Capacity of @p buf.
 * @param out_len     Receives the number of bytes read.
 * @param timeout_ms  How long to wait for RDY.
 * @return TIKU_EM9305_OK or a negative error code.
 */
int tiku_em9305_recv(uint8_t *buf, uint16_t cap, uint16_t *out_len,
                     uint32_t timeout_ms);

/**
 * @brief Self-test: reset the radio, read its boot event, send HCI Reset and
 *        read the reply.
 * @param out  Diagnostic snapshot (may be NULL).
 * @return TIKU_EM9305_OK if HCI Reset returned Command Complete, the reset
 *         error if the reset failed, else TIKU_EM9305_ERR_TIMEOUT.
 */
int tiku_em9305_probe(tiku_em9305_probe_t *out);

/**
 * @brief Send one HCI command and read back its Command Complete.
 *
 * Builds {0x01, opcode_lo, opcode_hi, plen, params...}, sends it and reads one
 * frame, which must hold this opcode's Command Complete.
 *
 * @param opcode  16-bit HCI opcode (e.g. 0x0C03 = Reset).
 * @param params  Command parameter bytes (may be NULL if @p plen == 0).
 * @param plen    Number of parameter bytes.
 * @param status  Receives the Command Complete status byte (0 = success),
 *                or 0xFF when the reply is something else.
 * @return TIKU_EM9305_OK if the matching Command Complete came back;
 *         TIKU_EM9305_ERR_PARAM if @p plen > 32, TIKU_EM9305_ERR_TIMEOUT if
 *         the send or read fails, TIKU_EM9305_ERR_NOTREADY otherwise.
 */
int tiku_em9305_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t plen,
                        uint8_t *status);

/** @brief Per-step status snapshot from tiku_em9305_beacon(). */
typedef struct {
    int     init_rc;      /**< tiku_em9305_reset() result                 */
    uint8_t st_reset;     /**< HCI Reset status                            */
    uint8_t st_params;    /**< LE Set Advertising Parameters status        */
    uint8_t st_data;      /**< LE Set Advertising Data status              */
    uint8_t st_enable;    /**< LE Set Advertising Enable status            */
    uint8_t ok;           /**< 1 if every step succeeded (advertising on)  */
} tiku_em9305_beacon_t;

/**
 * @brief Reset the radio and start LE advertising as a named beacon.
 *
 * Resets, then Set Advertising Parameters (non-connectable, 100 ms) + Data
 * (Flags + Complete Local Name = @p name) + Enable.  The controller then
 * advertises on its own until tiku_em9305_beacon_stop().
 *
 * @param name  Advertised complete local name, at most 26 bytes used
 *              (NULL -> "tiku").
 * @param out   Per-step status (may be NULL).
 * @return TIKU_EM9305_OK if advertising was enabled with all statuses 0;
 *         the reset error, TIKU_EM9305_ERR_TIMEOUT if a command got no
 *         Command Complete, or TIKU_EM9305_ERR_NOTREADY for a non-zero status.
 */
int tiku_em9305_beacon(const char *name, tiku_em9305_beacon_t *out);

/**
 * @brief Stop LE advertising (LE Set Advertising Enable = 0).
 * @return The tiku_em9305_hci_cmd() result; the command's status is not
 *         checked.
 */
int tiku_em9305_beacon_stop(void);

#endif /* TIKU_EM9305_H_ */
