/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wireless_ap.h - access-point lifecycle and saved profile control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WIRELESS_AP_H_
#define TIKU_WIRELESS_AP_H_
#include "tiku_wireless.h"

typedef struct {
    uint8_t active;
    uint8_t channel;
    uint8_t clients;
    uint8_t secured; /**< 0 open, 1 WPA2-PSK, 2 SAE with required PMF. */
} tiku_wireless_ap_status_t;

#define TIKU_WIRELESS_AP_MAX_CLIENTS 4u
/** @brief Interface ids for simultaneous station/AP Ethernet delivery. */
enum { TIKU_WIRELESS_IF_STA, TIKU_WIRELESS_IF_AP };
typedef void (*tiku_wireless_rx_if_t)(uint8_t, const uint8_t *, uint16_t, void *);
/** @brief Select simultaneous operation while powered with both interfaces idle. */
int tiku_wireless_dual(uint8_t enable);
/** @brief Read whether simultaneous station/AP operation is selected. */
uint8_t tiku_wireless_is_dual(void);
/** @brief Read the AP interface MAC while powered. */
int tiku_wireless_ap_mac(uint8_t mac[6]);
/** @brief Transmit Ethernet on the selected interface; require its link to be active. */
int tiku_wireless_tx_if(uint8_t interface, const uint8_t *frame, uint16_t length);
/** @brief Set an interface-aware receiver; replaces the legacy receiver. */
int tiku_wireless_set_rx_if(tiku_wireless_rx_if_t cb, void *context);
/** @brief Start an AP on channel 1..11 with up to four clients; empty PSK selects open authentication.
 * @return TIKU_DRV_OK on admission, INVALID for bad arguments/state, TIMEOUT while busy. */
int tiku_wireless_ap_start(const char *ssid, const char *psk, uint8_t channel);
/** @brief Start an AP with an explicit WPA2-PSK or SAE authentication method.
 * @return INVALID when the backend does not support the requested method. */
int tiku_wireless_ap_start_auth(const char *ssid, const char *psk, uint8_t channel,
                                tiku_wireless_auth_t auth);
/** @brief Stop the AP and return the powered radio to station mode. */
int tiku_wireless_ap_stop(void);
/** @brief Read cached AP state without exposing its key. */
int tiku_wireless_ap_status(tiku_wireless_ap_status_t *out);
/** @brief Save both profiles using a 12..128-byte passphrase, fresh salt and AES-256-GCM. */
int tiku_wireless_save_encrypted(const char *passphrase);
/** @brief Authenticate and restore saved profiles while both interfaces are idle. */
int tiku_wireless_unlock_profile(const char *passphrase);
/** @brief Reject unencrypted persistence; callers must use save_encrypted. */
int tiku_wireless_save_profile(void);
#endif
