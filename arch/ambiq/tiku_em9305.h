/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_em9305.h - EM9305 BLE controller: SPI-HCI transport + first-contact probe
 *
 * The Apollo510 Blue EVB carries an EM9305 BLE radio on IOM6 SPI. This is the
 * bare-metal transport that speaks the EM9305's framed SPI-HCI protocol
 * (0x42 write / 0x81 read headers, a two-byte ready/space status, RDY-line
 * handshake) over tiku_spi + a few GPIOs -- no AmbiqSuite, no Cordio. It is the
 * M0/M1 bring-up layer: reset the radio and exchange raw HCI packets. The
 * minimal HCI host + GATT will layer on top later (tikukits/ble).
 *
 * Built only when TIKU_DRV_BLE_EM9305_ENABLE is defined (apollo510b).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EM9305_H_
#define TIKU_EM9305_H_

#include <stdint.h>

/** @brief Result codes for the EM9305 transport. */
#define TIKU_EM9305_OK             0    /**< success                          */
#define TIKU_EM9305_ERR_RESET    (-1)   /**< radio did not come out of reset  */
#define TIKU_EM9305_ERR_NOTREADY (-2)   /**< STS never reported ready (0xC0)   */
#define TIKU_EM9305_ERR_TIMEOUT  (-3)   /**< no RDY / no response in time      */
#define TIKU_EM9305_ERR_PARAM    (-4)   /**< bad argument                      */

/**
 * @brief First-contact diagnostic snapshot (filled by tiku_em9305_probe()).
 *
 * Captures exactly what the M0/M1 gates need: that SPI talks to the radio
 * (sts1 == 0xC0), that the radio booted (active-state event), and that HCI is
 * alive (Reset -> Command Complete with a status byte).
 */
typedef struct {
    int      reset_rc;        /**< tiku_em9305_reset() result                 */
    uint8_t  spi_rc;          /**< tiku_spi_init() result (0 = ok)            */
    uint8_t  rdy_initial;     /**< RDY level before the EN pulse              */
    uint8_t  saw_low;         /**< RDY observed low during reset              */
    uint8_t  saw_high;        /**< RDY observed high (ready) during reset     */
    uint8_t  rdy_final;       /**< RDY level at the end of reset              */
    uint8_t  sts1;            /**< first status byte seen (0xC0 = SPI ready)   */
    uint8_t  sts2;            /**< reported buffer/space byte                  */
    uint8_t  active_evt;      /**< 1 if the {04 FF 01 01} boot event arrived   */
    uint8_t  cc_seen;         /**< 1 if an HCI Command Complete came back      */
    uint8_t  hci_status;      /**< HCI Reset status byte (0 = success)         */
    int8_t   send_rc;         /**< tiku_em9305_send(HCI Reset) result          */
    int8_t   recv_rc;         /**< tiku_em9305_recv(reply) result              */
    uint8_t  evt[16];         /**< raw bytes of the last event read            */
    uint16_t evt_len;         /**< number of valid bytes in evt[]              */
} tiku_em9305_probe_t;

/**
 * @brief Configure the radio's GPIOs + IOM6 SPI and pulse it out of reset.
 * @return TIKU_EM9305_OK, or a negative TIKU_EM9305_ERR_* code.
 */
int tiku_em9305_reset(void);

/**
 * @brief Send one raw HCI packet (type byte included) to the controller.
 * @param data  HCI packet bytes (e.g. {0x01, 0x03, 0x0C, 0x00} = HCI Reset).
 * @param len   Packet length in bytes.
 * @return TIKU_EM9305_OK or a negative error code.
 */
int tiku_em9305_send(const uint8_t *data, uint16_t len);

/**
 * @brief Wait (up to @p timeout_ms) for the controller to raise RDY, then read
 *        one framed packet into @p buf.
 * @param buf         Destination buffer.
 * @param cap         Capacity of @p buf.
 * @param out_len     Receives the number of bytes read.
 * @param timeout_ms  How long to wait for RDY.
 * @return TIKU_EM9305_OK or a negative error code.
 */
int tiku_em9305_recv(uint8_t *buf, uint16_t cap, uint16_t *out_len,
                     uint32_t timeout_ms);

/**
 * @brief Full M0/M1 first-contact self-test: reset, HCI Reset, read the reply.
 * @param out  Diagnostic snapshot (may be NULL).
 * @return TIKU_EM9305_OK if HCI Reset returned Command Complete, else negative.
 */
int tiku_em9305_probe(tiku_em9305_probe_t *out);

/**
 * @brief Send one HCI command and read back its Command Complete.
 *
 * Builds {0x01, opcode_lo, opcode_hi, plen, params...}, sends it, waits for the
 * matching HCI Command Complete event and returns its status byte.
 *
 * @param opcode  16-bit HCI opcode (e.g. 0x0C03 = Reset).
 * @param params  Command parameter bytes (may be NULL if @p plen == 0).
 * @param plen    Number of parameter bytes.
 * @param status  Receives the Command Complete status byte (0 = success).
 * @return TIKU_EM9305_OK if the matching Command Complete came back.
 */
int tiku_em9305_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t plen,
                        uint8_t *status);

/** @brief Per-step status snapshot from tiku_em9305_beacon(). */
typedef struct {
    int     init_rc;      /**< reset()+boot result                        */
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
 * (Flags + Complete Local Name = @p name) + Enable. The controller then
 * advertises autonomously until tiku_em9305_beacon_stop().
 *
 * @param name  Advertised complete local name (NULL -> "tiku").
 * @param out   Per-step status (may be NULL).
 * @return TIKU_EM9305_OK if advertising was enabled with all statuses 0.
 */
int tiku_em9305_beacon(const char *name, tiku_em9305_beacon_t *out);

/**
 * @brief Stop LE advertising (LE Set Advertising Enable = 0).
 * @return TIKU_EM9305_OK on success.
 */
int tiku_em9305_beacon_stop(void);

#endif /* TIKU_EM9305_H_ */
