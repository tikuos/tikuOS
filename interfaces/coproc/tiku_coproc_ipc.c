/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_ipc.c - legacy coprocessor calls through one IPC endpoint.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_coproc.h"
#include "tiku_coproc_ipc.h"
#if TIKU_IPC_ENABLE
uint16_t tiku_coproc_ipc_endpoint = UINT16_MAX;

/** @brief Native controls are synchronous and retain no caller data. */
static uint32_t call(uint16_t op, tiku_ipc_control_t *request, uint32_t fallback)
{
    if (tiku_ipc_control(tiku_coproc_ipc_endpoint,op,request) != TIKU_IPC_OK) return fallback;
    return request->value;
}

uint32_t tiku_coproc_flags(void)
{
    tiku_ipc_control_t request = {0};
    return (uint32_t)call(0,&request,0);
}

tiku_coproc_state_t tiku_coproc_state(void)
{
    tiku_ipc_control_t request = {0};
    return (tiku_coproc_state_t)call(1,&request,TIKU_COPROC_ABSENT);
}

int tiku_coproc_start(void)
{
    tiku_ipc_control_t request = {0};
    return (int)call(2,&request,(uint32_t)(int32_t)TIKU_COPROC_ERR_STATE);
}

int tiku_coproc_stop(void)
{
    tiku_ipc_control_t request = {0};
    return (int)call(3,&request,(uint32_t)(int32_t)TIKU_COPROC_ERR_STATE);
}

int tiku_coproc_alive(void)
{
    tiku_ipc_control_t request = {0};
    return (int)call(4,&request,(uint32_t)(int32_t)TIKU_COPROC_ERR_STATE);
}

uint32_t tiku_coproc_heartbeat(void)
{
    tiku_ipc_control_t request = {0};
    return (uint32_t)call(5,&request,0);
}

uint32_t tiku_coproc_image_size(void)
{
    tiku_ipc_control_t request = {0};
    return (uint32_t)call(6,&request,0);
}

int tiku_coproc_send(const void *data, uint32_t length)
{
    tiku_ipc_control_t request = {.input=data,.input_length=length};
    if (!length || length > TIKU_COPROC_MSG_CAP) return TIKU_COPROC_ERR_LEN;
    return (int)call(7,&request,(uint32_t)(int32_t)TIKU_COPROC_ERR_STATE);
}

int tiku_coproc_poll(void)
{
    tiku_ipc_control_t request = {0};
    return (int)call(8,&request,(uint32_t)(int32_t)TIKU_COPROC_ERR_STATE);
}

uint32_t tiku_coproc_reply_seq(void)
{
    tiku_ipc_control_t request = {0};
    return (uint32_t)call(9,&request,0);
}

uint32_t tiku_coproc_reply(void *out, uint32_t capacity)
{
    tiku_ipc_control_t request = {.output=out,.output_capacity=capacity};
    return (uint32_t)call(10,&request,0);
}
#endif
