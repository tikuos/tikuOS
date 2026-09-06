/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_ble.h - a link over the BLE serial facade: whole messages on the
 * Nordic UART Service byte pipe, each behind a 32-bit length.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_BLE_H_
#define TIKU_LINK_BLE_H_

#include "tiku_link.h"
#include <stddef.h>
#include <stdint.h>

/** @brief Bytes of length in front of every message on the pipe. */
#define TIKU_LINK_BLE_HEADER 4u

/** @brief Bytes a link holds for the pipe to take later. */
#ifndef TIKU_LINK_BLE_OUTBOX
#define TIKU_LINK_BLE_OUTBOX 2048u
#endif

/** @brief Bytes handed to the pipe per send; the facade fragments them. */
#ifndef TIKU_LINK_BLE_CHUNK
#define TIKU_LINK_BLE_CHUNK 64u
#endif

/** @brief How often the pipe is serviced while a link is open and no
 *         central holds it. */
#ifndef TIKU_LINK_BLE_POLL_TICKS
#define TIKU_LINK_BLE_POLL_TICKS (TIKU_CLOCK_SECOND / 20)
#endif

/** @brief How often it is serviced while a central holds it: a pairing
 *         exchange and the pipe's one-fragment mailbox both want an answer
 *         within a connection interval. */
#ifndef TIKU_LINK_BLE_LINK_TICKS
#define TIKU_LINK_BLE_LINK_TICKS 1u
#endif

typedef enum {
    TIKU_LINK_BLE_DOWN = 0,   /**< advertising; no central subscribed */
    TIKU_LINK_BLE_UP          /**< a central is subscribed; messages cross */
} tiku_link_ble_state_t;

typedef struct {
    uint32_t rx;        /**< messages delivered */
    uint32_t tx;        /**< messages sent */
    uint32_t drops;     /**< times a subscribed central went away */
    uint32_t oversize;  /**< messages too large for the buffer, skipped */
    uint32_t refused;   /**< sends refused whole: down, too large, no room */
    uint32_t stalled;   /**< times the pipe would take no more for now */
} tiku_link_ble_stats_t;

typedef struct {
    tiku_link_t link;
    uint8_t    *buf;        /**< the message being gathered */
    size_t      cap;
    size_t      len;        /**< bytes of it gathered */
    uint32_t    need;       /**< its length, once the head is in */
    size_t      skip;       /**< bytes of one too large still to pass */
    uint8_t     head[TIKU_LINK_BLE_HEADER];
    uint8_t     head_len;
    uint8_t     state;      /**< tiku_link_ble_state_t */
    uint8_t     out[TIKU_LINK_BLE_OUTBOX];
    size_t      out_len;    /**< bytes the pipe has not taken */
    tiku_link_ble_stats_t stats;
} tiku_link_ble_t;

/**
 * @brief Start advertising as @p name and open the link over whoever
 *        connects and subscribes.  One link at a time: the radio is one.
 *        The link confers TIKU_VFS_CAP_HW while its central has paired and
 *        runs it encrypted, nothing otherwise; read it with tiku_link_cap().
 *
 * @param buf  Where a received message is gathered, @p cap bytes.
 * @return the link, or NULL when the facade refuses or one is already open.
 */
tiku_link_t *tiku_link_ble_open(tiku_link_ble_t *l, const char *name,
                                uint8_t *buf, size_t cap);

/** @brief Where the link stands, a tiku_link_ble_state_t. */
uint8_t tiku_link_ble_state(const tiku_link_ble_t *l);

const tiku_link_ble_stats_t *tiku_link_ble_stats(const tiku_link_ble_t *l);

/** @brief Whether the process that services the pipe is running. */
uint8_t tiku_link_ble_pumping(void);

/** @brief Service the open link once, from whoever's context. */
void tiku_link_ble_service(void);

#endif /* TIKU_LINK_BLE_H_ */
