/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_sdio_packet.h - packet links over the SDIO mode owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_SDIO_PACKET_H_
#define TIKU_IPC_SDIO_PACKET_H_
#include <services/ipc/tiku_ipc_sdio.h>
#if TIKU_IPC_ENABLE
/** @brief Adapter retains a CPU copy-out destination; DMA uses owner staging. */
typedef struct {
    tiku_ipc_sdio_t *owner;
    tiku_sdio_job_t job;
    void *output;
    uint16_t length;
    uint8_t active, connected;
    uint32_t session;
    void (*lost)(void *);
    void *lost_context;
    tiku_ipc_packet_io_t io;
} tiku_ipc_sdio_packet_t;
/**
 * @brief Attach a packet adapter with a CPU-copy domain and physical-loss hook.
 * @note The owner declares the peripheral's separate DMA access domain. The
 * hook invalidates attached wire sessions; it does not dispatch applications.
 */
int tiku_ipc_sdio_packet_init(tiku_ipc_sdio_packet_t *, tiku_ipc_sdio_t *,
    const tiku_ipc_access_domain_t *cpu_domain, uint32_t peer,
    void (*lost)(void *), void *context);
#endif
#endif
