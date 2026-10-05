/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_serial.h - driver-agnostic "serial port over BLE" facade.
 *
 * Advertises as a connectable peripheral with a byte pipe (the Nordic UART
 * Service layout), reports when a central subscribes, and sends and receives
 * bytes.  Callers see no HCI, L2CAP or ATT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_SERIAL_H_
#define TIKU_BLE_SERIAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 1 when a BLE-serial backend is compiled in, 0 otherwise.
 *
 * The build sets TIKU_HAS_BLE for the EM9305.  Without that -D (e.g. a unit
 * build) TIKU_DRV_BLE_EM9305_ENABLE also gives 1, and the TIKU_FLPR_ENABLE
 * check selects Nordic's FLPR controller.
 */
#if (defined(TIKU_HAS_BLE) && (TIKU_HAS_BLE + 0)) ||                          \
    (defined(TIKU_DRV_BLE_EM9305_ENABLE) && (TIKU_DRV_BLE_EM9305_ENABLE + 0)) \
    || (defined(TIKU_FLPR_ENABLE) && (TIKU_FLPR_ENABLE + 0) &&                \
        defined(TIKU_HAS_BLE_ADV) && (TIKU_HAS_BLE_ADV + 0))
/* Nordic: the FLPR (VPR RISC-V coprocessor) is the on-die BLE controller; the
 * M33 exchanges L2CAP fragments with it over the shared-page mailbox and runs
 * ATT/GATT itself. */
#define TIKU_BLE_SERIAL_PRESENT 1
#else
#define TIKU_BLE_SERIAL_PRESENT 0
#endif

/**
 * @brief Is a BLE-serial radio backend present in this build?
 * @return 1 if the facade is backed by a real radio, 0 if it is a stub.
 */
int tiku_ble_serial_available(void);

/**
 * @brief Start connectable advertising as a BLE serial peripheral.
 *
 * A central (phone app, tikuble.py, the mac GUI) can then connect and
 * subscribe; after that, tiku_ble_serial_ready() turns true and send/recv
 * carry data.
 *
 * @param name  Advertised local name (NULL -> a default).
 * @return 0 on success, negative on failure (no radio, the radio already
 *         claimed, or a controller error).
 */
int tiku_ble_serial_start(const char *name);

/** @brief Stop advertising and drop any link. Idempotent. */
void tiku_ble_serial_stop(void);

/**
 * @brief 1 when a central is connected and subscribed to notifications, so
 *        send() reaches it.  Pumps the stack as a side effect, so a poll loop
 *        on this keeps the link serviced.
 *
 * The EM9305 backend also waits 5/8 s after a fresh subscribe, since
 * notifications sent while the central is still arming are dropped.  The
 * Nordic backend re-advertises a dropped link from here.
 */
int tiku_ble_serial_ready(void);

/**
 * @brief 1 when the connected central has paired (or is bonded from before)
 *        and the link runs encrypted under that key; 0 otherwise, including
 *        while it is still being agreed.
 *
 * The Nordic backend pairs and bonds the central itself, so a central seen
 * before skips pairing; the EM9305 backend does no pairing and returns 0.
 *
 * @note Base trust in what crosses the link on this, not on the peer address.
 */
int tiku_ble_serial_secured(void);

/** @brief 1 while a central holds the link, subscribed or not. */
int tiku_ble_serial_connected(void);

/** @brief Where the connection's security stands: 0 none, 1 pairing,
 *         2 key agreed or recalled, 3 encrypted under it (always 0 on the
 *         EM9305 backend). */
int tiku_ble_serial_secure_state(void);

/**
 * @brief Pump the stack once: drain events and RX, push queued TX.
 *
 * ready() calls it, and so do rx_ready() and recv() (on Nordic, only while no
 * received bytes wait), so a poll loop that reads what arrives keeps the link
 * serviced.
 */
void tiku_ble_serial_service(void);

/**
 * @brief Send @p len bytes to the connected central.
 *
 * EM9305: queues every byte and drains under controller flow control, giving
 * up on the rest after a 1 s stall.  Nordic: one notification of at most
 * MTU - 3 bytes, refused while unsubscribed or while TX is busy.
 *
 * @return Bytes accepted (EM9305: @p len; Nordic: up to MTU - 3, or 0 when
 *         unsubscribed or busy), or -1 if not connected or @p data is NULL.
 */
int tiku_ble_serial_send(const uint8_t *data, uint16_t len);

/**
 * @brief Are there received bytes waiting to be read?
 *
 * Pumps the stack first.  The Nordic backend holds one received write and
 * pumps only once it has been read, since a pump would overwrite it.
 *
 * @return 1 if at least one byte is waiting, 0 otherwise.
 */
int tiku_ble_serial_rx_ready(void);

/**
 * @brief Pop up to @p cap bytes the central has written to this device.
 * @return Number of bytes copied (0 if none waiting).
 */
int tiku_ble_serial_recv(uint8_t *buf, uint16_t cap);

/**
 * @brief Start a non-connectable beacon advertising @p name.
 *
 * On Nordic the broadcast facade (tiku_ble_adv.h) owns beacons, and this
 * returns -1.
 *
 * @return 0 on success, negative on failure.
 */
int tiku_ble_serial_beacon(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BLE_SERIAL_H_ */
