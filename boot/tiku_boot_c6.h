/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_c6.h - boot-owned FireBeetle SDIO endpoint and mode controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOOT_C6_H_
#define TIKU_BOOT_C6_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#include <services/ipc/tiku_ipc_sdio.h>
/** @brief Bind c6/bulk to the single native/loader owner after kernel IPC init. */
int tiku_boot_c6_init(uint16_t endpoint);
/** @brief Boot-lifetime controller for explicit native and loader operations. */
tiku_ipc_sdio_t *tiku_boot_c6_owner(void);
/** @brief Copy the validated unsolicited loader INFO packet; zero if unavailable. */
uint16_t tiku_boot_c6_loader_info(void *out,uint16_t capacity);
/** @brief Copy the last factory packet while its exclusive physical mode is ready. */
uint16_t tiku_boot_c6_factory_info(void *out,uint16_t capacity);
/** @brief Byte length of the last completed factory FIFO read. */
uint16_t tiku_boot_c6_factory_length(void);
/** @brief Attach to the recovery app started by factory OTA without another peer reset. */
tiku_ipc_err_t tiku_boot_c6_adopt_loader(void);
/** @brief Hold factory liveness checks through its bounded delayed peer restart. */
void tiku_boot_c6_factory_wait_restart(void);
/** @brief Arm a qualification reset after factory transmit DMA has completed. */
void tiku_boot_c6_factory_cut(void);
/** @brief Count factory transfers interrupted by qualification after transmit completion. */
uint32_t tiku_boot_c6_factory_cuts(void);
/** @brief Diagnostic corruption of every Nth INPUT packet; zero disables it. */
void tiku_boot_c6_fault_every(uint32_t interval);
/** @brief Number of accepted packets corrupted by the current diagnostic profile. */
uint32_t tiku_boot_c6_faults(void);
/** @brief Copy the last reset's request/ready markers, credits and offered-byte count. */
void tiku_boot_c6_recovery(uint32_t out[4]);
/** @brief Copy the native epoch-reply trace retained at the last reset. */
void tiku_boot_c6_trace(uint32_t out[5]);
/** @brief Enable packet pumping, or pause it from loader-ready for diagnostic-only native use. */
tiku_ipc_err_t tiku_boot_c6_packet_gate(uint8_t enabled);
/** @brief Framed native bulk backend, with mode changes invalidating its epoch. */
extern const tiku_ipc_backend_ops_t tiku_boot_c6_ops;
#endif
