/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - Apollo 510 common CPU helpers (delays, IDs)
 *
 * Hybrid bring-up: delays use AmbiqSuite's am_util_delay_* (tagged
 * @ambiq-sdk). The unique-id / reset-reason paths are stubbed for now
 * and will be backed by MCUCTRL/OTP and RSTGEN reads in a later pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "am_mcu_apollo.h"   /* @ambiq-sdk */
#include "am_util.h"         /* @ambiq-sdk: am_util_delay_* */
#include "tiku_cpu_common.h"

void tiku_cpu_ambiq_delay_ms(unsigned int ms) {
    am_util_delay_ms((uint32_t)ms);   /* @ambiq-sdk */
}

void tiku_cpu_ambiq_delay_us(unsigned int us) {
    am_util_delay_us((uint32_t)us);   /* @ambiq-sdk */
}

uint8_t tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len) {
    /* TODO(de-sdk): read the device unique ID from MCUCTRL/OTP. Bring-up
     * stub returns a deterministic zero fill so callers behave. */
    uint8_t i;
    if (buf == 0 || len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        buf[i] = 0U;
    }
    return len;
}

uint16_t tiku_cpu_ambiq_reset_reason(void) {
    /* TODO(de-sdk): decode RSTGEN->STAT into the tikuOS reset-reason bits. */
    return 0U;
}
