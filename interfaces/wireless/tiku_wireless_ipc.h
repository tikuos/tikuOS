/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wireless_ipc.h - versioned radio control and Ethernet service contract.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIRELESS_IPC_H_
#define TIKU_WIRELESS_IPC_H_
#include <stdint.h>
#include <stddef.h>
#define TIKU_RADIO_VERSION 1u
#define TIKU_RADIO_HEADER 16u
#define TIKU_RADIO_CONTROL 64u
#define TIKU_RADIO_CREDENTIALS 114u
#define TIKU_RADIO_FRAME_MAX 1514u
#define TIKU_RADIO_QUEUE 2u
#define TIKU_RADIO_LEASE_SECONDS 10u
#define TIKU_RADIO_RENEW_SECONDS 2u
/** @brief Operations use little-endian ids and lengths; response sets bit seven. */
enum {
    TIKU_RADIO_DISCOVER = 1,
    TIKU_RADIO_CLAIM,
    TIKU_RADIO_RENEW,
    TIKU_RADIO_RELEASE,
    TIKU_RADIO_STATUS,
    TIKU_RADIO_POWER,
    TIKU_RADIO_SCAN,
    TIKU_RADIO_AP,
    TIKU_RADIO_CREDENTIAL,
    TIKU_RADIO_CONNECT,
    TIKU_RADIO_DISCONNECT,
    TIKU_RADIO_FORGET
};
/** @brief Service results are independent of native driver error numbers. */
enum {
    TIKU_RADIO_OK,
    TIKU_RADIO_INVALID,
    TIKU_RADIO_BUSY,
    TIKU_RADIO_STALE,
    TIKU_RADIO_UNSUPPORTED,
    TIKU_RADIO_FAILED
};
/** @brief Station capabilities; persistent credentials and other radios are absent. */
#define TIKU_RADIO_CAPABILITIES 0x00000003u
/** @brief Read a little-endian 32-bit word from four accessible bytes. */
static inline uint32_t tiku_radio_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/** @brief Write a little-endian 32-bit word to four accessible bytes. */
static inline void tiku_radio_put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
/** @brief Initialize a control header; payload bytes belong to the caller. */
void tiku_radio_header(uint8_t *p, uint8_t op, uint32_t request, uint32_t claim, uint8_t result,
                       uint8_t payload);
/** @brief Validate envelope, version, reserved bytes and operation direction. */
int tiku_radio_valid(const void *data, size_t length, uint8_t response);
/** @brief Fingerprint bounded requests for duplicate comparison without retained keys. */
uint32_t tiku_radio_digest(const void *data, size_t length);
/** @brief Clear secret storage through volatile writes. */
void tiku_radio_wipe(void *data, size_t length);
#endif
