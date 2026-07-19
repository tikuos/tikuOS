/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_bond.c - durable LTK bond store (BLE bonding).
 *
 * The bond table is a single durable persist cell (kernel/memory): the
 * whole array is one gated value, so a store rewrites all slots behind the
 * cell API's MPU/gate discipline.  An SRAM mirror answers lookups on the
 * hot path (a reconnect) without unlocking NVM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_bond.h>
#include <kernel/memory/tiku_mem.h>
#include <string.h>

/* Gate key for the bond table.  Bump if tiku_ble_bond_t's layout changes
 * (forces a clean re-prime, discarding stale-layout bonds). */
#define TIKU_BLE_BOND_MAGIC  0xB0DDC001UL

/* Durable table + its SRAM mirror.  The default (NULL/0) primes every slot
 * to zero -> valid = 0 -> no bonds on a virgin device. */
static TIKU_DURABLE tiku_ble_bond_t bond_persist[TIKU_BLE_BOND_MAX];
TIKU_PERSIST_CELL(bond_cell, bond_persist, TIKU_BLE_BOND_MAGIC, NULL, 0);

static tiku_ble_bond_t bond_mirror[TIKU_BLE_BOND_MAX];
static uint8_t         bond_inited;

static int bond_addr_eq(const tiku_ble_bond_t *b, const uint8_t addr[6],
                        uint8_t addr_type)
{
    return b->valid &&
           (b->addr_type & 1u) == (addr_type & 1u) &&
           memcmp(b->addr, addr, 6) == 0;
}

void tiku_ble_bond_init(void)
{
    if (bond_inited) {
        return;
    }
    (void)tiku_persist_cell_init(&bond_cell);       /* validate / prime      */
    memcpy(bond_mirror, bond_persist, sizeof(bond_mirror));
    bond_inited = 1u;
}

int tiku_ble_bond_store(const uint8_t addr[6], uint8_t addr_type,
                        const uint8_t ltk[16])
{
    uint8_t i, slot = TIKU_BLE_BOND_MAX;

    tiku_ble_bond_init();
    /* Reuse the peer's existing slot (re-pairing overwrites), else the
     * first free one. */
    for (i = 0u; i < TIKU_BLE_BOND_MAX; i++) {
        if (bond_addr_eq(&bond_mirror[i], addr, addr_type)) {
            slot = i;
            break;
        }
        if (slot == TIKU_BLE_BOND_MAX && !bond_mirror[i].valid) {
            slot = i;
        }
    }
    if (slot == TIKU_BLE_BOND_MAX) {
        return -1;                                  /* table full            */
    }
    bond_mirror[slot].valid = 1u;
    bond_mirror[slot].addr_type = (uint8_t)(addr_type & 1u);
    memcpy(bond_mirror[slot].addr, addr, 6);
    memcpy(bond_mirror[slot].ltk, ltk, 16);
    /* One durable write of the whole table (gate already valid post-init). */
    tiku_persist_cell_write(&bond_cell, bond_mirror, sizeof(bond_mirror));
    return 0;
}

int tiku_ble_bond_find(const uint8_t addr[6], uint8_t addr_type,
                       uint8_t ltk[16])
{
    uint8_t i;

    tiku_ble_bond_init();
    for (i = 0u; i < TIKU_BLE_BOND_MAX; i++) {
        if (bond_addr_eq(&bond_mirror[i], addr, addr_type)) {
            if (ltk != (uint8_t *)0) {
                memcpy(ltk, bond_mirror[i].ltk, 16);
            }
            return 1;
        }
    }
    return 0;
}

uint8_t tiku_ble_bond_count(void)
{
    uint8_t i, n = 0u;

    tiku_ble_bond_init();
    for (i = 0u; i < TIKU_BLE_BOND_MAX; i++) {
        if (bond_mirror[i].valid) {
            n++;
        }
    }
    return n;
}

void tiku_ble_bond_clear(void)
{
    tiku_ble_bond_init();
    memset(bond_mirror, 0, sizeof(bond_mirror));
    tiku_persist_cell_write(&bond_cell, bond_mirror, sizeof(bond_mirror));
}
