/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_transfer_server.h - Explicit USB-console and TCP transfer listeners.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_SERVER_H_
#define TIKU_MEDIA_TRANSFER_SERVER_H_
#include <stdint.h>
/** @brief Start console channel 0xF7 when port is zero, or one keyed TCP listener. */
int tiku_media_transfer_server_start(uint16_t port,const uint8_t *key);
/** @brief Stop transports and release this server's file handles and SRAM arena. */
int tiku_media_transfer_server_stop(void);
#endif
