/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_transfer_server.h - USB-console, high-speed bulk and TCP transfer listeners.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_SERVER_H_
#define TIKU_MEDIA_TRANSFER_SERVER_H_
#include <stdint.h>
/** @brief Start console channel 0xF7 when port is zero, or one keyed TCP listener.
 * @note With TRANSFER_SERVICE_ENABLE, TCP returns PENDING after command admission;
 * observe the typed service until readiness or a terminal error. Console transfer
 * remains synchronous and refuses an active provisioned TCP service. */
int tiku_media_transfer_server_start(uint16_t port,const uint8_t *key);
/** @brief Start the P4 high-speed USB bulk listener, with no console framing. */
int tiku_media_transfer_server_usbhs(void);
/** @brief Stop transports and release legacy handles/backing; a paced TCP Stop returns PENDING. */
int tiku_media_transfer_server_stop(void);
#ifndef TIKU_MEDIA_TRANSFER_STATUS_ENABLE
/** @brief Enable read-only transfer observations without adding a session. */
#define TIKU_MEDIA_TRANSFER_STATUS_ENABLE 0
#endif
/** @brief Copied listener/session progress; upload committed bytes exclude staged input. */
typedef struct {
    uint64_t accepted, committed, expected, read_offset, read_length;
    uint16_t port;
    uint8_t active, connected, uploading, reading, fault;
} tiku_media_transfer_status_t;
/** @brief Copy status in the serialized service owner: 0 success, ARG for NULL, STALE for lost backing.
 * @note Requires TIKU_MEDIA_TRANSFER_STATUS_ENABLE. This call performs no storage,
 * transport or allocator mutation and exposes no key or authentication material.
 */
int tiku_media_transfer_server_status(tiku_media_transfer_status_t *);
#endif
