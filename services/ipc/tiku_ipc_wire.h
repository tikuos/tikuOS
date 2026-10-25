/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_wire.h - negotiated IPC over whole-message links.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_WIRE_H_
#define TIKU_IPC_WIRE_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#include <services/link/tiku_link.h>
#if TIKU_IPC_ENABLE
#define TIKU_IPC_WIRE_HEADER 40u
#define TIKU_IPC_WIRE_FRAME 256u
#define TIKU_IPC_WIRE_BLOCK 512u

/**
 * @brief Process one staged block; callbacks retain no pointer on return.
 * @note AGAIN accepts nothing and permits retry. OK attests final access.
 * produce fills exactly length bytes. Callbacks do not dispatch processes.
 */
typedef struct {
    tiku_ipc_err_t (*consume)(void *context, const void *data, uint16_t length);
    tiku_ipc_err_t (*produce)(void *context, void *data, uint16_t length);
    void *context;
} tiku_ipc_wire_sink_t;

/** @brief One pending frame copied from the link's borrowed receive buffer. */
typedef struct {
    uint8_t data[TIKU_IPC_WIRE_FRAME];
    uint16_t length;
} tiku_ipc_wire_inbox_t;

/** @brief Bounded state for one logical endpoint on a marked link. */
typedef struct {
    tiku_link_t *link;
    const char *name;
    const tiku_ipc_wire_sink_t *sink;
    uint64_t epoch, peer_epoch;
    uint32_t local_session, serial, bad, stale, duplicate, failed, delivered;
    uint16_t endpoint, frame_size, peer_frame, peer_message, peer_buffer;
    uint8_t peer_directions, ready, hello_reply, retired, initialized;
    tiku_clock_time_t hello_at;
    tiku_ipc_wire_inbox_t inbox[3];
    struct {
        tiku_ipc_transfer_t transfer;
        uint32_t id;
        uint16_t offset;
        tiku_clock_time_t retry;
        uint8_t busy;
    } client[2];
    struct {
        uint8_t data[TIKU_IPC_WIRE_BLOCK];
        uint32_t id;
        uint16_t total, offset;
        tiku_ipc_err_t result;
        uint8_t kind, done;
    } server[2];
    struct {
        uint8_t data[64];
        uint16_t length;
        uint32_t id, session;
        tiku_clock_time_t retry, deadline;
        uint8_t busy;
    } control;
    uint32_t control_seen;
    tiku_ipc_err_t control_result;
} tiku_ipc_wire_t;

/**
 * @brief Bind a logical channel to a static endpoint name and a fresh epoch.
 * @note Call after core initialization, before using the endpoint. Epoch must
 * differ from earlier boots on this channel; reset increments it without wrap.
 * Link send must copy accepted bytes and deliver receives in process context.
 */
tiku_ipc_err_t tiku_ipc_wire_init(tiku_ipc_wire_t *context, tiku_link_t *link,
    const char *name, uint16_t endpoint, uint64_t epoch, uint16_t frame_size,
    const tiku_ipc_wire_sink_t *sink);
/** @brief Service a peer with no local submissions; budget bounds frame handling. */
void tiku_ipc_wire_pump(tiku_ipc_wire_t *context, uint16_t budget);
extern const tiku_ipc_backend_ops_t tiku_ipc_wire_ops;
#endif
#endif
