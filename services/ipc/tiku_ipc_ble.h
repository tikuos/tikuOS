/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_ble.h - Leased HCI packets over the c6/ble mailbox.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_BLE_H_
#define TIKU_IPC_BLE_H_
#include "tiku_ipc_wire.h"
#include <interfaces/wireless/tiku_wireless_ipc.h>
#define TIKU_BLE_PACKET_MAX 260u
#define TIKU_BLE_QUEUE 2u
#define TIKU_BLE_CAPABILITIES 0x0000000fu
/** @brief Complete HCI packets, including their H4 type byte. */
typedef struct {
    uint8_t data[TIKU_BLE_QUEUE][TIKU_BLE_PACKET_MAX];
    uint16_t length[TIKU_BLE_QUEUE];
    uint8_t head, count;
    uint32_t delivered;
} tiku_ble_packets_t;
/** @brief One managed packet loan; backing survives cancellation until completion. */
typedef struct {
    tiku_ble_packets_t packets;
    tiku_ipc_buffer_t buffer;
    tiku_ipc_submission_t submission;
    uint8_t active, cancelling;
} tiku_ble_sender_t;
/** @brief Validate one complete command/ACL (host) or event/ACL (controller). */
int tiku_ble_packet_valid(const uint8_t *, uint16_t, uint8_t host);
/** @brief Copy one packet; AGAIN leaves a full queue unchanged. */
tiku_ipc_err_t tiku_ble_packet_push(tiku_ble_packets_t *, const uint8_t *, uint16_t);
/** @brief Remove the oldest packet after its consumer finishes. */
void tiku_ble_packet_pop(tiku_ble_packets_t *);
/** @brief Complete or submit one claim-tagged packet; negative means transport failure. */
int tiku_ble_sender_step(tiku_ble_sender_t *, tiku_ipc_endpoint_t, uint32_t claim);
/** @brief Cancel a loan; continue stepping until active clears before reuse. */
void tiku_ble_sender_cancel(tiku_ble_sender_t *, tiku_ipc_endpoint_t);
/** @brief Bind the BLE wire and start its endpoint-owning process. */
void tiku_ble_service_start(tiku_ipc_wire_t *);
extern const tiku_ipc_wire_sink_t tiku_ble_service_sink;
/** @brief Whether a native host may change controller power. */
int tiku_ble_service_local_allowed(void);
#endif
