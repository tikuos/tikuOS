/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bt.h - driver-agnostic Bluetooth Low Energy API.
 *
 * The stack in tikukits/net/bluetooth/ implements HCI, L2CAP, ATT, GATT, GAP and
 * SMP over an abstract transport, so a driver plugs in its own without the stack
 * changing.  Public application API only; internals live beside the stack.
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
 * @brief Bring the BT subsystem online.
 *
 * Must run after the WiFi side has loaded its firmware and raised the HT clock.
 * Powers BT up, uploads its firmware, waits for ready, zeroes the ring pointers
 * and completes the host handshake.
 *
 * @return TIKU_DRV_OK on success, otherwise an error from the bring-up
 *         step that failed.
 */
int tiku_bt_init(void);

/**
 * @brief Send one HCI command or ACL packet to the chip.
 *
 * The buffer must start with an HCI packet-type byte.  The caller serialises
 * calls -- the runner does, and ad-hoc callers must not race it.
 *
 * @param packet  HCI packet bytes including the 1-byte type prefix
 * @param len     Total length including the type byte
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID on bad args,
 *         TIKU_DRV_ERR_TIMEOUT on no buffer space in the ring.
 */
int tiku_bt_send(const uint8_t *packet, uint16_t len);

/**
 * @brief Try to receive one HCI packet from the chip.
 *
 * Non-blocking. If a packet is available in the BT2H ring, copies it
 * (including the 1-byte type prefix) into @p out and returns the
 * length. If the ring is empty, returns 0.
 *
 * @param out      Destination buffer
 * @param out_max  Capacity of @p out in bytes
 * @return Length of the packet copied (1..out_max), or 0 if no
 *         packet was available, or a negative error code.
 */
int tiku_bt_recv(uint8_t *out, uint16_t out_max);

/**
 * @brief Return true if the BT subsystem has been brought up
 *        successfully and is ready for HCI traffic.
 */
int tiku_bt_is_ready(void);

/**
 * @brief Information returned by HCI Read_Local_Version_Information.
 *
 * Cached on bring-up and valid once tiku_bt_is_ready() returns 1.  The fields
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
 * @brief Read back the chip's BD_ADDR (= Bluetooth MAC).
 *
 * Cached during bring-up via HCI_Read_BD_ADDR. The 6 bytes are written
 * in big-endian order (MSB at index 0) — that's the standard
 * print order, e.g. 28:CD:C1:00:11:22.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_NOT_PRESENT if the BT
 *         subsystem isn't up.
 */
int tiku_bt_addr(uint8_t out[6]);

/**
 * @brief Read back the chip's HCI/LMP version info cached at bring-up.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_NOT_PRESENT if the BT
 *         subsystem isn't up.
 */
int tiku_bt_local_version(tiku_bt_version_t *out);

/**
 * @brief Return the BTFW version string baked into the firmware blob
 *        header (e.g. "CYW4343A2_001.003.016.0031.0000_Generic_SDIO_..").
 *
 * The pointer is into flash (read-only) and remains valid for the
 * lifetime of the program. The string is NUL-terminated.
 */
const char *tiku_bt_fw_version(void);

/*---------------------------------------------------------------------------*/
/* GAP advertising (phase 7)                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Maximum local-name length accepted by tiku_bt_advertise_start().  A legacy
 * advertising PDU carries 31 bytes of AD records; a 3-byte Flags record plus
 * the name record's own 2 bytes of overhead leave 26.
 */
#define TIKU_BT_ADV_NAME_MAX     26U

/**
 * @brief Start advertising with the given local name.
 *
 * Sets the advertising parameters, then the data (Flags plus Complete Local
 * Name), then enables.  Each step waits for a successful Command Complete, and
 * a failure aborts the sequence and disables anything already started.
 *
 * @param name  UTF-8 local name, 1..26 chars. NULL or empty rejected.
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID for bad name,
 *         TIKU_DRV_ERR_NOT_PRESENT if BT isn't up, or a transport rc.
 */
int tiku_bt_advertise_start(const char *name);

/**
 * @brief Stop advertising.
 *
 * Issues LE_Set_Advertising_Enable(0). Safe to call when advertising
 * is already off (the chip just returns success).
 */
int tiku_bt_advertise_stop(void);

/** Return 1 if advertising is currently enabled, 0 otherwise. */
int tiku_bt_is_advertising(void);

/*---------------------------------------------------------------------------*/
/* GAP scanning (phase 8)                                                    */
/*---------------------------------------------------------------------------*/

/** Hard cap on the scan-results cache. Sized so a busy office BLE
 *  environment (~10..30 devices) fits without truncation; a value above
 *  16 would spill into FRAM and lose the bound-allocator simplicity. */
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
 *                      0.625 ms units). Valid range 3..10240 ms.
 * @param window_ms     Scan-on window per interval in ms; must be
 *                      <= interval_ms.
 * @return TIKU_DRV_OK on success.
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
 * @brief Drain pending LE Advertising Report events from the BT2H ring.
 *
 * Idempotent and cheap, with an early return when nothing is pending.  The
 * runner calls it every tick while a scan, link or advertising is active, so
 * events do not pile up in the chip's ring.
 */
void tiku_bt_poll(void);

/*---------------------------------------------------------------------------*/
/* Connection management (phase 9)                                           */
/*---------------------------------------------------------------------------*/

/** Maximum simultaneous LE connections. The CYW43439 firmware
 *  supports several but the host-side state is sized for one to keep
 *  the demo footprint small; bump when GATT client work (Phase 12)
 *  needs multiple. */
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
 * Issues HCI_Disconnect with "remote user terminated".  The chip answers with a
 * Command Status; the Disconnection Complete arrives later and is what clears
 * the connection-table entry, not this call.
 *
 * @param handle  Connection handle returned via tiku_bt_connections.
 *                Pass 0xFFFF to disconnect the first active link
 *                (convenience for single-connection demos).
 * @return TIKU_DRV_OK on Command Status accept, otherwise an error.
 */
int tiku_bt_disconnect(uint16_t handle);

/*---------------------------------------------------------------------------*/
/* GATT server (phase 11) + notifications (phase 12)                         */
/*---------------------------------------------------------------------------*/

/** Characteristic property bits (matches Bluetooth Core Spec
 *  Vol 3 Part G 3.3.1.1). */
#define TIKU_BT_PROP_READ        0x02U
#define TIKU_BT_PROP_WRITE_NORSP 0x04U
#define TIKU_BT_PROP_WRITE       0x08U
#define TIKU_BT_PROP_NOTIFY      0x10U
#define TIKU_BT_PROP_INDICATE    0x20U

/* Phase-11 sizing knobs. Bump alongside any new service that needs
 * more headroom; both are static so growing them only costs SRAM. */
#define TIKU_BT_SVC_MAX          4U   /* GAP + GATT + 2 user */
#define TIKU_BT_CHAR_MAX         8U   /* across all services */

/**
 * @brief Read callback for a characteristic value
 *
 * Invoked when a client issues ATT Read Request for the char's
 * value handle. Implementer fills @p out with the current value and
 * sets *out_len. Returning a value larger than out_max gets clamped.
 *
 * @param user      The @p user pointer from the char definition
 * @param out       Destination for the value bytes
 * @param out_max   Capacity of @p out (currently ATT_MTU_DEFAULT - 1)
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
 * ATT request sees them, with no service-changed indication sent today.
 *
 * @return TIKU_DRV_OK on success, TIKU_DRV_ERR_INVALID if @p svc is
 *         NULL or if the registry is full (cap = TIKU_BT_SVC_MAX).
 */
int tiku_bt_register_service(const tiku_bt_service_t *svc);

/*---------------------------------------------------------------------------*/
/* GATT client (phase 13)                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initiate an LE central-role connection to a peer.
 *
 * Issues LE Create Connection with sensible defaults.  The Connection Complete
 * event fires asynchronously, after which tiku_bt_connections() shows the new
 * entry with role 0.
 *
 * @param peer_addr       BD_ADDR in MSB-first order (display order)
 * @param peer_addr_type  0 = public, 1 = random
 * @return TIKU_DRV_OK on command-status accept.
 */
int tiku_bt_connect_to(const uint8_t peer_addr[6],
                             uint8_t peer_addr_type);

/**
 * @brief Send ATT Read Request on a connection.
 *
 * The response (Read Response or Error Response) arrives
 * asynchronously and is logged via CYW43_BT_PRINTF. Phase 13.x
 * will add callback delivery; today it's print-only.
 *
 * @param conn_handle  Connection handle from tiku_bt_connections
 * @param attr_handle  Attribute handle on the peer to read
 * @return TIKU_DRV_OK if the request was queued for send.
 */
int tiku_bt_client_read(uint16_t conn_handle, uint16_t attr_handle);

/**
 * @brief Send ATT Write Request on a connection.
 *
 * @param conn_handle  Connection handle
 * @param attr_handle  Attribute handle on the peer to write
 * @param value        Bytes to write
 * @param len          Length of @p value (must fit in MTU-3)
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
 * Finds the characteristic by UUID, confirms NOTIFY is in its properties, and
 * sends the notification to every connection whose CCCD has it enabled.  A
 * silent no-op when nobody is subscribed.
 *
 * @param char_uuid  16-bit UUID of the characteristic
 * @param value      Bytes to put in the notification PDU
 * @param len        Length of @p value (must fit in MTU-3)
 * @return TIKU_DRV_OK on send (or no-subscriber no-op),
 *         TIKU_DRV_ERR_INVALID for bad UUID / oversize.
 */
int tiku_bt_notify(uint16_t char_uuid, const uint8_t *value,
                         uint16_t len);

/*---------------------------------------------------------------------------*/
/* SMP bonding store (phase 14)                                              */
/*---------------------------------------------------------------------------*/

/** Max stored bonds. Sized for one slot today; bumps to N when
 *  multi-link bonding lands in Phase 14. The schema below is fixed
 *  32-byte width so growing the slot count costs only sizeof bytes
 *  in .persistent and no on-disk migration. */
#define TIKU_BT_BOND_MAX         1U

/** Magic placed at the head of each bond record (ASCII "BOND" LE)
 *  to distinguish a populated slot from uninitialised NVM. */
#define TIKU_BT_BOND_MAGIC       0x424F4E44UL

/**
 * @brief One stored LE bond (LTK and peer identity).
 *
 * A 32-byte fixed-width record, laid out so real LTK and IRK material can drop
 * in later without shifting bytes or migrating storage.  `magic` marks a
 * populated slot and `peer_addr` is MSB-first, matching scan and connection.
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
 * @return TIKU_DRV_OK on success.
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
 * @return TIKU_DRV_OK on success.
 */
int tiku_bt_bond_load(uint8_t slot, tiku_bt_bond_record_t *out);

/**
 * @brief Clear a stored bond
 *
 * Zeros the on-NVM slot so the next tiku_bt_bond_load returns an
 * empty record. Used by Phase 14's "forget pairing" entry point.
 *
 * @param slot  Bond index in [0, TIKU_BT_BOND_MAX)
 * @return TIKU_DRV_OK on success.
 */
int tiku_bt_bond_clear(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BT_H_ */
