/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_coproc.c - native mailbox semantics behind endpoint control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_coproc.h"
#include <interfaces/coproc/tiku_coproc_driver.h>
#include <interfaces/coproc/tiku_coproc_ipc.h>
#if TIKU_IPC_ENABLE
/** @brief Preserve native mailbox replacement, reply truncation and markers. */
static tiku_ipc_err_t control(void *context, uint16_t operation, tiku_ipc_control_t *r)
{
    (void)context;
    switch (operation) {
    case 0: r->value = (uint32_t)tiku_coproc_driver_flags(); break;
    case 1: r->value = (uint32_t)tiku_coproc_driver_state(); break;
    case 2: r->value = (uint32_t)tiku_coproc_driver_start(); break;
    case 3: r->value = (uint32_t)tiku_coproc_driver_stop(); break;
    case 4: r->value = (uint32_t)tiku_coproc_driver_alive(); break;
    case 5: r->value = (uint32_t)tiku_coproc_driver_heartbeat(); break;
    case 6: r->value = (uint32_t)tiku_coproc_driver_image_size(); break;
    case 7: r->value = (uint32_t)tiku_coproc_driver_send(r->input,r->input_length); break;
    case 8: r->value = (uint32_t)tiku_coproc_driver_poll(); break;
    case 9: r->value = (uint32_t)tiku_coproc_driver_reply_seq(); break;
    case 10: r->value = (uint32_t)tiku_coproc_driver_reply(r->output,r->output_capacity); break;
    default: return TIKU_IPC_UNSUPPORTED;
    }
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_coproc_init(uint16_t endpoint)
{
    tiku_ipc_err_t result = tiku_ipc_control_bind(endpoint,control,NULL);
    if (result == TIKU_IPC_OK) tiku_coproc_ipc_endpoint = endpoint;
    return result;
}
#endif
