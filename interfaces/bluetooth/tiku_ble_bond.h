/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_bond.h - durable LTK bond store (BLE bonding).
 *
 * After LE Secure Connections pairing derives an LTK (Phase E2), a BONDED
 * device remembers {peer address -> LTK} across reboots so a later
 * reconnection SKIPS pairing entirely and goes straight to LL encryption
 * with the stored key.  The table lives in durable NVM (a magic-gated
 * persist cell, kernel/memory) so it survives power cycles; an SRAM mirror
 * serves lookups without touching the NVM write gate.
 *
 * Transport-agnostic: the central keys the bond on the peripheral's AdvA,
 * the peripheral on the central's InitA (both from the CONNECT_IND).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BLE_BOND_H_
#define TIKU_BLE_BOND_H_

#include <stdint.h>

/** Bond slots.  Small: this is a demo/embedded peer set, not a phone. */
#define TIKU_BLE_BOND_MAX 4u

/** One stored bond: peer identity + the pairing-derived LTK. */
typedef struct {
    uint8_t valid;                      /**< 1 = slot in use                 */
    uint8_t addr_type;                  /**< bit0: 1 = random address        */
    uint8_t addr[6];                    /**< peer device address             */
    uint8_t ltk[16];                    /**< Long Term Key                   */
} tiku_ble_bond_t;

/** @brief Validate the durable bond table (call once at boot). */
void tiku_ble_bond_init(void);

/**
 * @brief Store (or refresh) a bond for @p addr.  Durable across reboot.
 * @return 0 stored, -1 table full.
 */
int tiku_ble_bond_store(const uint8_t addr[6], uint8_t addr_type,
                        const uint8_t ltk[16]);

/**
 * @brief Look up a bond by address.
 * @param ltk  out: the stored LTK when found (may be NULL to just test).
 * @return 1 if bonded (ltk filled), 0 if not.
 */
int tiku_ble_bond_find(const uint8_t addr[6], uint8_t addr_type,
                       uint8_t ltk[16]);

/** @brief Number of stored bonds (valid slots). */
uint8_t tiku_ble_bond_count(void);

/** @brief Forget all bonds (durable). */
void tiku_ble_bond_clear(void);

#endif /* TIKU_BLE_BOND_H_ */
