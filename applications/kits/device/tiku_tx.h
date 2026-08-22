/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tx.h - transport seam: bytes to and from one device.
 *
 * Serial and TCP today; the WebSocket/wasm build implements the same three
 * calls.  Nothing above this header knows which one it is talking to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TX_H_
#define TIKU_TX_H_

#include <stddef.h>

/** @brief Transport handle.  Opaque; created by one of the openers below. */
typedef struct tiku_tx tiku_tx_t;

/**
 * @brief Open a serial device.
 *
 * @param path  Device node, e.g. "/dev/ttyACM0" or a /dev/serial/by-id link.
 * @param baud  Line rate (9600 on MSP430, 115200 elsewhere).
 * @return Handle, or NULL (errno set).
 */
tiku_tx_t *tiku_tx_open_serial(const char *path, int baud);

/**
 * @brief Open a TCP connection (the telnet-shell channel).
 *
 * @param host  Dotted-quad or hostname.
 * @param port  TCP port.
 * @return Handle, or NULL (errno set).
 */
tiku_tx_t *tiku_tx_open_tcp(const char *host, int port);

/**
 * @brief Read whatever has arrived, waiting at most @p timeout_ms.
 *
 * @return Bytes read (0 on timeout), or -1 on a broken link.
 */
int tiku_tx_read(tiku_tx_t *tx, void *buf, size_t max,
                      int timeout_ms);

/** @brief Write @p len bytes; -1 on a broken link. */
int tiku_tx_write(tiku_tx_t *tx, const void *buf, size_t len);

/** @brief Reopen the same endpoint after a replug.  0 on success. */
int tiku_tx_reopen(tiku_tx_t *tx);

/** @brief Human-readable endpoint name, for window titles and logs. */
const char *tiku_tx_name(const tiku_tx_t *tx);

void tiku_tx_close(tiku_tx_t *tx);

/**
 * @brief List TikuOS-looking serial devices under /dev/serial/by-id.
 *
 * Fills @p out with up to @p max NUL-terminated paths.
 * @return Count found, or -1 if the directory is unreadable.
 */
int tiku_tx_scan_serial(char out[][256], int max);

#endif /* TIKU_TX_H_ */
