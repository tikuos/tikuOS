/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_epoch_hal.c - connection nonces from the selected entropy source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_epoch_hal.h"
#if defined(PLATFORM_ESP32P4)
#include <arch/esp32p4/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_trng_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_trng_arch.h>
#elif defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_trng_arch.h>
#elif defined(PLATFORM_RP2350)
#include <arch/arm-rp2350/tiku_trng_arch.h>
#elif defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_trng_arch.h>
#elif defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_trng_arch.h>
#elif defined(PLATFORM_RA8P1) && (TIKU_KIT_CRYPTO_ENABLE + 0)
#include <arch/ra8p1/tiku_trng_arch.h>
#elif defined(PLATFORM_MSP430)
/* No hardware generator: the die's ID above a durable count of the nonces
 * handed out, so none repeats on this part. */
#include <kernel/memory/tiku_mem.h>
#include <arch/msp430/tiku_cpu_common.h>
static TIKU_DURABLE uint32_t epoch_count;
TIKU_PERSIST_CELL(epoch_cell, epoch_count, 0x45504F43UL /* 'EPOC' */, NULL, 0);
#define TIKU_IPC_EPOCH_COUNTED 1
#endif

tiku_ipc_err_t tiku_ipc_epoch_fresh(uint64_t *out)
{
    if (!out) return TIKU_IPC_INVALID;
    *out = 0;
#if defined(TIKU_IPC_EPOCH_COUNTED)
    {
        uint8_t id[8] = {0};
        uint32_t high = 0, next;
        (void)tiku_persist_cell_init(&epoch_cell);
        next = epoch_count + 1u;
        tiku_persist_cell_write_u32(&epoch_cell, next);
        if (epoch_count != next) return TIKU_IPC_IO;
        (void)tiku_cpu_msp430_unique_id(id, sizeof id);
        for (unsigned i = 0; i < sizeof id; i++) high = high * 31u + id[i];
        *out = ((uint64_t)high << 32) | next;
        return *out ? TIKU_IPC_OK : TIKU_IPC_IO;
    }
#elif defined(TIKU_TRNG_OK)
    {
        uint8_t bytes[8];
        if (tiku_trng_arch_read_bytes(bytes,sizeof bytes) != TIKU_TRNG_OK)
            return TIKU_IPC_IO;
        for (unsigned i = 0; i < sizeof bytes; i++) *out |= (uint64_t)bytes[i] << (i*8u);
        return *out ? TIKU_IPC_OK : TIKU_IPC_IO;
    }
#else
    return TIKU_IPC_UNSUPPORTED;
#endif
}
