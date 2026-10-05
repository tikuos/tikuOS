/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bt.h - driver-agnostic Bluetooth Low Energy API.
 *
 * The stack in tikukits/net/bluetooth/ implements HCI, L2CAP, ATT, GATT, GAP
 * and SMP over an abstract transport, so a driver plugs in its own without the
 * stack changing.  Public application API only; internals live beside it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BT_H_
#define TIKU_BT_H_

#include <stdint.h>
#include "kernel/drivers/tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring the protocol stack online over the registered transport.
 *
 * Resets the stack state, registers the built-in demo service, starts the BT
 * runner and runs HCI_Reset, the event masks, Read_Local_Version and
 * Read_BD_ADDR; a failed identity query is not fatal.
 *
 * @return TIKU_DRV_OK, or an error when the stack's scratch arena cannot be
 *         set up.
 * @note The transport driver calls it from its own init, after its chip
 *       bring-up and tiku_bt_register_transport().
 */
int tiku_bt_init(void);

/**
 * @brief Send one HCI command or ACL packet through the transport.
 *
 * The buffer must start with an HCI packet-type byte.
 *
 * @param packet  HCI packet bytes including the 1-byte type prefix
 * @param len     Total length including the type byte
 * @return TIKU_DRV_ERR_NOT_PRESENT without a transport, else the
 *         transport's send() result.
 * @note The caller serialises calls: the runner does, and ad-hoc callers must
 *       not race it.
 */
int tiku_bt_send(const uint8_t *packet, uint16_t len);

/**
 * @brief Try to receive one HCI packet.
 *
 * Non-blocking: forwards to the transport's recv(), which copies a pending
 * packet (including the 1-byte type prefix) into @p out.
 *
 * @param out      Destination buffer
 * @param out_max  Capacity of @p out in bytes
 * @return Length of the packet copied, 0 when nothing is pending or no
 *         transport is registered, or a negative error from the transport.
 */
int tiku_bt_recv(uint8_t *out, uint16_t out_max);

/**
 * @brief Return 1 once a transport is registered and ready and
 *        tiku_bt_init() has run, so HCI traffic can flow; 0 otherwise.
 */
int tiku_bt_is_ready(void);

/**
 * @brief Information returned by HCI Read_Local_Version_Information.
 *
 * Cached by tiku_bt_init() and read with tiku_bt_local_version().  The fields
 * follow the Core Spec command of the same name: HCI and LMP versions, the
 * vendor revisions, and the SIG manufacturer id.
 */
typedef struct {
    uint8_t  hci_version;
    uint16_t hci_revision;
    uint8_t  lmp_version;
    uint16_t manufacturer;
    uint16_t lmp_subversion;
} tiku_bt_version_t;

/**
 * @brief Read back the controller's BD_ADDR (= Bluetooth MAC).
 *
 * Cached during bring-up via HCI_Read_BD_ADDR. The 6 bytes are written
 * in big-endian order (MSB at index 0), the usual print order, e.g.
 * 28:CD:C1:00:11:22.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_NOT_PRESENT until bring-up
 *         has cached a BD_ADDR.
 */
int tiku_bt_addr(uint8_t out[6]);

/**
 * @brief Read back the controller's HCI/LMP version info cached at bring-up.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_NOT_PRESENT until bring-up
 *         has cached a BD_ADDR.
 */
int tiku_bt_local_version(tiku_bt_version_t *out);

/**
 * @brief Return the controller firmware version the transport reports
 *        (the CYW43 BTFW header string, the ESP32-C61 library build id).
 *
 * @return NUL-terminated string owned by the transport, or "" when the
 *         transport reports none.
 */
const char *tiku_bt_fw_version(void);

/*---------------------------------------------------------------------------*/
/* GAP ADVERTISING                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Longest local name tiku_bt_advertise_start() sends: 31 bytes of AD
 *        data less the 3-byte Flags record and the name record's 2 bytes.
 */
#define TIKU_BT_ADV_NAME_MAX     26U

/**
 * @brief Start advertising (ADV_IND, 100-150 ms) with the given local name.
 *
 * Disables advertising, then sets the parameters, the data (Flags plus
 * Complete Local Name) and the enable, each awaiting a successful Command
 * Complete; a failure stops the sequence with advertising off.
 *
 * @param name  UTF-8 local name; NULL or empty is rejected, and a name longer
 *              than TIKU_BT_ADV_NAME_MAX bytes is cut to that length.
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for a bad name or a
 *         refused command, TIKU_DRV_ERR_NOT_PRESENT if BT isn't up, or a
 *         transport rc.
 */
int tiku_bt_advertise_start(const char *name);

/**
 * @brief Stop advertising.
 *
 * Issues LE_Set_Advertising_Enable(0).  Safe to call when advertising is
 * already off: a non-zero status from the controller is ignored.
 *
 * @return TIKU_DRV_OK, TIKU_DRV_ERR_NOT_PRESENT if BT isn't up, or a
 *         transport rc.
 */
int tiku_bt_advertise_stop(void);

/** Return 1 if advertising is currently enabled, 0 otherwise. */
int tiku_bt_is_advertising(void);

/*---------------------------------------------------------------------------*/
/* GAP SCANNING                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Scan-cache entries; a device first heard once it is full is lost. */
#define TIKU_BT_SCAN_MAX         16U

/** Max bytes of local name kept per scan entry (not NUL-terminated). */
#define TIKU_BT_SCAN_NAME_MAX    24U

/**
 * @brief One entry in the scan-results cache.
 *
 * Populated from LE Advertising Report subevents, keeping enough to display the
 * device and dropping the rest of the AD payload to bound SRAM.  @p addr is
 * MSB-first display order and @p name is not NUL-terminated -- use @p name_len.
 */
typedef struct {
    uint8_t  addr[6];
    uint8_t  addr_type;
    uint8_t  evt_type;
    int8_t   rssi_dbm;
    uint8_t  name_len;
    char     name[TIKU_BT_SCAN_NAME_MAX];
} tiku_bt_scan_entry_t;

/**
 * @brief Start an LE scan; clears any cached results from a prior scan.
 *
 * Issues LE_Set_Scan_Parameters then LE_Set_Scan_Enable(1, filter=1).
 *
 * @param active        1 = active scan (sends SCAN_REQ to scannable
 *                      advertisers and collects SCAN_RSP), 0 = passive
 * @param interval_ms   Scan repeat interval in ms (rounded to chip
 *                      0.625 ms units), clamped to 3..10240 ms.
 * @param window_ms     Scan-on window per interval in ms, clamped to
 *                      3..interval_ms.
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_NOT_PRESENT if BT isn't up,
 *         TIKU_DRV_ERR_INVALID if the controller refuses, or a transport rc.
 */
int tiku_bt_scan_start(uint8_t active, uint16_t interval_ms,
                             uint16_t window_ms);

/** Stop the LE scan (LE_Set_Scan_Enable(0)). Safe when already off. */
int tiku_bt_scan_stop(void);

/** Return 1 if a scan is currently running, 0 otherwise. */
int tiku_bt_is_scanning(void);

/** Drop all cached scan results without stopping the scan. */
void tiku_bt_scan_clear(void);

/** Number of entries currently in the scan-results cache. */
uint8_t tiku_bt_scan_count(void);

/**
 * @brief Copy the cached scan results into @p out.
 *
 * Entries are stable across the lifetime of the scan (each BD_ADDR
 * appears at most once; later sightings update RSSI / name in place).
 *
 * @param out  Destination array, sized for @p max entries
 * @param max  Capacity of @p out (TIKU_BT_SCAN_MAX is the
 *             practical maximum; passing more is fine, extra slots
 *             are left untouched)
 * @return Number of entries written (<= count())
 */
uint8_t tiku_bt_scan_results(tiku_bt_scan_entry_t *out,
                                   uint8_t max);

/**
 * @brief Drain pending HCI events and ACL packets from the transport.
 *
 * Handles at most 8 packets per call and returns at once while nothing is on
 * air (no scan, advertising or link).  The runner calls it every tick while
 * one of those is active, so events do not pile up in the controller.
 */
void tiku_bt_poll(void);

/*---------------------------------------------------------------------------*/
/* CONNECTION MANAGEMENT                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Simultaneous LE connections; the host-side state is sized for one. */
#define TIKU_BT_CONN_MAX         1U

/**
 * @brief One active LE connection.
 *
 * Populated from LE Connection Complete and torn down on Disconnection
 * Complete.  @p handle is the chip-assigned id used in every subsequent ACL
 * packet; @p peer_addr is MSB-first display order, matching the scan entry.
 */
typedef struct {
    uint16_t handle;
    uint8_t  peer_addr[6];
    uint8_t  peer_addr_type;
    uint8_t  role;
    uint16_t conn_interval_units;
    uint16_t conn_latency;
    uint16_t supv_timeout_units;
} tiku_bt_connection_t;

/** Number of currently active LE connections (0..TIKU_BT_CONN_MAX). */
uint8_t tiku_bt_connection_count(void);

/**
 * @brief Copy active connections into @p out.
 *
 * @param out  Destination array sized for @p max entries
 * @param max  Capacity of @p out
 * @return Number of entries written (<= connection_count())
 */
uint8_t tiku_bt_connections(tiku_bt_connection_t *out,
                                  uint8_t max);

/**
 * @brief Tear down an LE link.
 *
 * Sends HCI_Disconnect with "remote user terminated" and returns without
 * waiting; the Disconnection Complete arrives later and is what clears the
 * connection-table entry, not this call.
 *
 * @param handle  Connection handle returned via tiku_bt_connections.
 *                Pass 0xFFFF to disconnect the first active link
 *                (convenience for single-connection demos).
 * @return TIKU_DRV_OK once the command is sent, TIKU_DRV_ERR_NOT_PRESENT if
 *         BT isn't up or no link is active, TIKU_DRV_ERR_INVALID for an
 *         unknown handle, or a transport rc.
 */
int tiku_bt_disconnect(uint16_t handle);

/*---------------------------------------------------------------------------*/
/* GATT SERVER AND NOTIFICATIONS                                             */
/*---------------------------------------------------------------------------*/

/* Characteristic property bits (Core Spec Vol 3 Part G 3.3.1.1). */
#define TIKU_BT_PROP_READ        0x02U  /**< value can be read            */
#define TIKU_BT_PROP_WRITE_NORSP 0x04U  /**< write without response       */
#define TIKU_BT_PROP_WRITE       0x08U  /**< write with response          */
#define TIKU_BT_PROP_NOTIFY      0x10U  /**< notifications, gets a CCCD   */
#define TIKU_BT_PROP_INDICATE    0x20U  /**< indications, gets a CCCD     */

/* Registry sizing; both are static, so growing them costs only SRAM. */
#define TIKU_BT_SVC_MAX          4U   /**< services, the demo one included */
#define TIKU_BT_CHAR_MAX         8U   /**< services + characteristics in all */

/**
 * @brief Read callback for a characteristic value
 *
 * Invoked when a client issues ATT Read Request for the char's
 * value handle. Implementer fills @p out with the current value and
 * sets *out_len. Returning a value larger than out_max gets clamped.
 *
 * @param user      The @p user pointer from the char definition
 * @param out       Destination for the value bytes
 * @param out_max   Capacity of @p out (ATT_MTU_DEFAULT - 1)
 * @param out_len   Set to the number of bytes written
 * @return 0 on success; non-zero surfaces as ATT Error Response.
 */
typedef int (*tiku_bt_char_read_t)(void *user, uint8_t *out,
                                         uint16_t out_max,
                                         uint16_t *out_len);

/**
 * @brief Write callback for a characteristic value
 *
 * Invoked when a client issues ATT Write Request for the char's
 * value handle (or any write without response).
 *
 * @param user  The @p user pointer from the char definition
 * @param data  Incoming bytes (lifetime: until callback returns)
 * @param len   Length of @p data in bytes
 * @return 0 on success; non-zero surfaces as ATT Error Response.
 */
typedef int (*tiku_bt_char_write_t)(void *user, const uint8_t *data,
                                          uint16_t len);

/**
 * @brief One characteristic in a GATT service.
 *
 * A characteristic must supply either a static value or an on_read callback,
 * and the callback wins when both are set.  NOTIFY and INDICATE properties
 * auto-allocate a CCCD descriptor after the value handle.
 */
typedef struct {
    uint16_t                    uuid;
    uint8_t                     properties;
    const uint8_t              *static_value;
    uint16_t                    static_value_len;
    tiku_bt_char_read_t   on_read;
    tiku_bt_char_write_t  on_write;
    void                       *user;
} tiku_bt_char_t;

/**
 * @brief One GATT service (Primary Service Declaration + N chars)
 *
 * The @p chars array lifetime must extend at least as long as the
 * service stays registered; typically defined `static const` at
 * file scope by the registering module.
 */
typedef struct {
    uint16_t                    uuid;
    const tiku_bt_char_t *chars;
    uint8_t                     char_count;
} tiku_bt_service_t;

/**
 * @brief Register a GATT service.
 *
 * Called once per service, typically from a process init.  Its attributes are
 * appended to the table and later registrations get higher handles; the next
 * ATT request sees them, and no Service Changed indication is sent.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID if @p svc is NULL,
 *         has more than TIKU_BT_CHAR_MAX characteristics, or the registry
 *         is full (cap = TIKU_BT_SVC_MAX).
 */
int tiku_bt_register_service(const tiku_bt_service_t *svc);

/*---------------------------------------------------------------------------*/
/* GATT CLIENT                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initiate an LE central-role connection to a peer.
 *
 * Sends LE Create Connection (30-50 ms interval, 5 s supervision timeout).
 * The Connection Complete event fires asynchronously, after which
 * tiku_bt_connections() shows the new entry with role 0.
 *
 * @param peer_addr       BD_ADDR in MSB-first order (display order)
 * @param peer_addr_type  0 = public, 1 = random
 * @return TIKU_DRV_OK once the command is sent, TIKU_DRV_ERR_NOT_PRESENT if
 *         BT isn't up, or a transport rc.
 */
int tiku_bt_connect_to(const uint8_t peer_addr[6],
                             uint8_t peer_addr_type);

/**
 * @brief Send ATT Read Request on a connection.
 *
 * The response (Read Response or Error Response) arrives asynchronously and
 * is only logged, via TIKU_BT_PRINTF.
 *
 * @param conn_handle  Connection handle from tiku_bt_connections
 * @param attr_handle  Attribute handle on the peer to read
 * @return TIKU_DRV_OK if the request was queued for send,
 *         TIKU_DRV_ERR_INVALID for an unknown connection handle.
 */
int tiku_bt_client_read(uint16_t conn_handle, uint16_t attr_handle);

/**
 * @brief Send ATT Write Request on a connection.
 *
 * @param conn_handle  Connection handle
 * @param attr_handle  Attribute handle on the peer to write
 * @param value        Bytes to write
 * @param len          Length of @p value (must fit in the default MTU - 3)
 * @return TIKU_DRV_OK if the request was queued for send,
 *         TIKU_DRV_ERR_INVALID for an unknown connection or a bad value.
 */
int tiku_bt_client_write(uint16_t conn_handle, uint16_t attr_handle,
                               const uint8_t *value, uint16_t len);

/**
 * @brief Send ATT Read By Group Type Request (primary service discovery).
 *
 * Walks handles 0x0001..0xFFFF asking for primary service decls.
 * The response is logged.
 */
int tiku_bt_client_discover_services(uint16_t conn_handle);

/**
 * @brief Subscribe to a characteristic via CCCD write.
 *
 * Convenience wrapper: writes 0x0001 (notifications enable) to the
 * given CCCD attribute handle. CCCD handle is typically (value
 * handle + 1) when the char's properties include NOTIFY.
 */
int tiku_bt_client_subscribe(uint16_t conn_handle,
                                   uint16_t cccd_handle);

/**
 * @brief Push a Handle Value Notification for a characteristic.
 *
 * Finds the characteristic by UUID and, when its CCCD has notifications
 * enabled, sends the notification on every connection.  A silent no-op when
 * nobody is subscribed or the characteristic has no CCCD.
 *
 * @param char_uuid  16-bit UUID of the characteristic
 * @param value      Bytes to put in the notification PDU
 * @param len        Length of @p value (must fit in the default MTU - 3)
 * @return TIKU_DRV_OK on send (or no-subscriber no-op),
 *         TIKU_DRV_ERR_INVALID if BT isn't up or for a NULL value, an
 *         unknown UUID or an oversize value.
 */
int tiku_bt_notify(uint16_t char_uuid, const uint8_t *value,
                         uint16_t len);

/*---------------------------------------------------------------------------*/
/* SMP BONDING STORE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stored bonds.  The record is a fixed 32 bytes, so more slots cost
 *        only their size and need no migration of stored records.
 */
#define TIKU_BT_BOND_MAX         1U

/**
 * @brief Magic at the head of each bond record ("BOND" read most-significant
 *        byte first), telling a populated slot from uninitialised NVM.
 */
#define TIKU_BT_BOND_MAGIC       0x424F4E44UL

/**
 * @brief One stored LE bond (LTK and peer identity).
 *
 * A 32-byte fixed-width record holding the peer identity and its LTK.  `magic`
 * marks a populated slot and `peer_addr` is MSB-first, matching scan and
 * connection.
 */
typedef struct {
    uint32_t magic;
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint8_t  _pad;
    uint8_t  ltk[16];
    uint32_t flags;
} tiku_bt_bond_record_t;

/**
 * @brief Save a bond record to the persistent store.
 *
 * Writes the record into its durable slot.  The SMP state machine calls this
 * when pairing completes, so the link can re-encrypt on reconnect without
 * pairing again.
 *
 * @param slot  Bond index in [0, TIKU_BT_BOND_MAX)
 * @param rec   Record to store; magic is set automatically
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for a bad slot or a
 *         NULL @p rec.
 */
int tiku_bt_bond_save(uint8_t slot,
                            const tiku_bt_bond_record_t *rec);

/**
 * @brief Load a bond record from the persistent store
 *
 * Returns TIKU_DRV_OK with a zeroed @p out (and magic=0) when the
 * slot is empty, so callers can distinguish "no bond" from a real
 * record by the magic field.
 *
 * @param slot  Bond index in [0, TIKU_BT_BOND_MAX)
 * @param out   Destination record (must not be NULL)
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for a bad slot or a
 *         NULL @p out.
 */
int tiku_bt_bond_load(uint8_t slot, tiku_bt_bond_record_t *out);

/**
 * @brief Clear a stored bond
 *
 * Zeros the on-NVM slot so the next tiku_bt_bond_load returns an
 * empty record. The bt shell command uses it to forget a pairing.
 *
 * @param slot  Bond index in [0, TIKU_BT_BOND_MAX)
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for a bad slot.
 */
int tiku_bt_bond_clear(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BT_H_ */
