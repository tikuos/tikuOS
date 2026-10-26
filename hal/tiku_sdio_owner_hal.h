/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_owner_hal.h - single-owner SDIO modes and bounded hardware steps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SDIO_OWNER_HAL_H_
#define TIKU_SDIO_OWNER_HAL_H_
#include <hal/tiku_ipc_packet_hal.h>
#if TIKU_IPC_ENABLE
/** @brief Protocol selection for native, resident-loader and factory exchanges. */
typedef enum { TIKU_SDIO_OFF, TIKU_SDIO_NATIVE, TIKU_SDIO_LOADER,
               TIKU_SDIO_FACTORY } tiku_sdio_mode_t;
typedef enum { TIKU_SDIO_CONTROL, TIKU_SDIO_BULK, TIKU_SDIO_UPDATE } tiku_sdio_class_t;
/**
 * @brief One driver owns the controller and pins across protocol modes.
 * @note start OK retains the stages until poll OK or abort confirms final access.
 * ready is a liveness observation; reset_to LOADER uses hardware/watchdog recovery,
 * without requiring native acknowledgement. Each callback is bounded and non-dispatching.
 */
typedef struct {
    tiku_ipc_err_t (*claim)(void *);
    uint8_t (*release)(void *); /**< One confirms private DMA quiescence and lease release. */
    tiku_ipc_err_t (*start)(void *, tiku_sdio_mode_t, tiku_sdio_class_t,
                          const void *, uint16_t, void *, uint16_t, uint64_t token);
    tiku_ipc_err_t (*poll)(void *, uint64_t *completed_token);
    uint8_t (*abort)(void *, uint64_t token);
    tiku_ipc_err_t (*ready)(void *, tiku_sdio_mode_t);
    tiku_ipc_err_t (*reset_to)(void *, tiku_sdio_mode_t);
    tiku_ipc_err_t (*negotiate)(void *, tiku_sdio_mode_t, uint32_t session);
} tiku_sdio_owner_ops_t;
#endif
#endif
