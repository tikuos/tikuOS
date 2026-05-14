/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bt_transport.h — driver-agnostic Bluetooth transport interface
 *
 * The TikuOS BLE stack lives in tikukits/net/bluetooth/ and is
 * portable across HCI-capable chips (CYW43439, nRF52 with HCI UART,
 * ESP32 BT, TI CC256x, external dongles). Each driver implements
 * the small @ref tiku_bt_transport_t vtable below and registers
 * itself via @ref tiku_bt_register_transport(); the generic stack
 * then drives BLE end-to-end without knowing the chip's transport
 * details (BTSDIO vs HCI-UART vs HCI-SPI vs USB-HCI).
 *
 * The expected HCI packet framing is the standard Bluetooth Core
 * Spec form:
 *
 *   byte 0   packet type (0x01 = HCI cmd, 0x02 = ACL data,
 *                          0x04 = HCI event)
 *   byte 1+  type-specific bytes (HCI cmd opcode+len+params,
 *                                  ACL handle+len+L2CAP, etc.)
 *
 * The transport hides whatever wrapping the chip needs around that
 * (BTSDIO 4-byte header on CYW43, raw UART on Nordic, etc.).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BT_TRANSPORT_H_
#define TIKU_BT_TRANSPORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver-supplied transport vtable
 *
 * Each function is called by the generic stack as it pumps HCI
 * traffic. Implementations must be synchronous (no PT_YIELD) since
 * they may be called from shell context, the BT runner, and ISR
 * follow-ups. Reentrancy: only the BT runner and shell touch the
 * transport at most one-at-a-time (single-process BT use today).
 */
typedef struct {
    /**
     * Send one HCI packet (type byte at offset 0).
     * @return 0 on success, non-zero on transport error.
     */
    int (*send)(const uint8_t *pkt, uint16_t len);

    /**
     * Try to receive one HCI packet from the chip (non-blocking).
     * @param out      Destination buffer, gets the type byte first
     * @param out_max  Capacity of @p out in bytes
     * @return  >0 = bytes written, 0 = no packet pending, <0 = error
     */
    int (*recv)(uint8_t *out, uint16_t out_max);

    /**
     * Return 1 once the chip's BT subsystem is up + the host-side
     * transport state is ready to send/recv. 0 during bring-up.
     */
    int (*is_ready)(void);
} tiku_bt_transport_t;

/**
 * @brief Register the active BT transport.
 *
 * Drivers call this once during their init, typically right after
 * the chip-side bring-up (BTFW upload + ring-buffer handshake on
 * CYW43, or pin/baud config on a UART-HCI driver). The generic
 * stack stashes the pointer and uses it for every subsequent send /
 * recv operation. Only one transport may be registered at a time;
 * a second call replaces the first.
 *
 * The vtable storage must outlive every BT operation -- usually
 * declared `static const` in the driver's translation unit.
 *
 * @return 0 on success, non-zero on bad args.
 */
int tiku_bt_register_transport(const tiku_bt_transport_t *t);

/**
 * @brief Return the currently registered transport (NULL if none).
 *
 * Used internally by the generic BLE stack; rarely useful to callers.
 */
const tiku_bt_transport_t *tiku_bt_get_transport(void);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_BT_TRANSPORT_H_ */
