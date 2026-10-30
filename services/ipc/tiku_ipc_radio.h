/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_radio.h - boot hooks for the radio IPC service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_RADIO_H_
#define TIKU_IPC_RADIO_H_
#include "tiku_ipc_wire.h"
#include "tiku_ipc_radio_frames.h"
/** @brief Bind initialized control/data wires and start their owning service process. */
void tiku_radio_service_start(tiku_ipc_wire_t *control, tiku_ipc_wire_t *data);
/** @brief Credential and Ethernet sinks execute bounded copies in transport context. */
extern const tiku_ipc_wire_sink_t tiku_radio_control_sink, tiku_radio_data_sink;
/** @brief Permit local mutation when unclaimed or called by the radio service owner. */
int tiku_radio_local_allowed(void);
#endif
