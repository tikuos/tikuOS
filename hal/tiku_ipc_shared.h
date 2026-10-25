/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_shared.h - DMA-free shared slots with separate writer cache lines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_SHARED_H_
#define TIKU_IPC_SHARED_H_
#include <stdint.h>
#include <stddef.h>

#define TIKU_IPC_SHARED_LINE 64u
#define TIKU_IPC_SHARED_SLOTS 2u
#define TIKU_IPC_SHARED_PAYLOAD 512u
#define TIKU_IPC_SHARED_MAGIC 0x31435049UL
#define TIKU_IPC_SHARED_RESTART 0x31435052UL
#define TIKU_IPC_SHARED_INPUT 0u
#define TIKU_IPC_SHARED_OUTPUT 1u
#define TIKU_IPC_SHARED_ECHO 2u

typedef struct {
    volatile uint32_t magic, epoch, control_epoch, control_sequence;
    volatile uint32_t control_length, control_crc;
    volatile uint8_t control_data[64];
    volatile uint32_t reserved[10];
} tiku_ipc_shared_host_t;

typedef struct {
    volatile uint32_t magic, epoch, heartbeat, bytes, crc, errors;
    volatile uint32_t control_ack, control_length, control_crc, control_result;
    volatile uint32_t service_stamp, service_max_gap, service_calls, service_valid;
    volatile uint8_t control_data[64];
    volatile uint32_t reserved[2];
} tiku_ipc_shared_peer_t;

typedef struct {
    volatile uint32_t epoch, sequence, length, kind, crc;
    volatile uint32_t reserved[11];
} tiku_ipc_shared_request_t;

typedef struct {
    volatile uint32_t epoch, sequence, length, result, crc;
    volatile uint32_t reserved[11];
} tiku_ipc_shared_reply_t;

typedef struct {
    tiku_ipc_shared_request_t request;
    tiku_ipc_shared_reply_t reply;
    volatile uint8_t payload[TIKU_IPC_SHARED_PAYLOAD];
} tiku_ipc_shared_slot_t;

/** @brief Align the mapped base to LINE; every field has one designated writer. */
typedef struct {
    tiku_ipc_shared_host_t host;
    tiku_ipc_shared_peer_t peer;
    tiku_ipc_shared_slot_t slots[TIKU_IPC_SHARED_SLOTS];
} tiku_ipc_shared_t;

_Static_assert(sizeof(tiku_ipc_shared_host_t) == 2 * TIKU_IPC_SHARED_LINE,
               "IPC host control occupies whole writer lines");
_Static_assert(sizeof(tiku_ipc_shared_peer_t) == 2 * TIKU_IPC_SHARED_LINE,
               "IPC peer control occupies whole writer lines");
_Static_assert(sizeof(tiku_ipc_shared_request_t) == TIKU_IPC_SHARED_LINE,
               "IPC request occupies one writer line");
_Static_assert(sizeof(tiku_ipc_shared_reply_t) == TIKU_IPC_SHARED_LINE,
               "IPC reply occupies one writer line");
_Static_assert(offsetof(tiku_ipc_shared_slot_t, payload) % TIKU_IPC_SHARED_LINE == 0,
               "IPC payload has exclusive whole-line ownership");
_Static_assert(sizeof(tiku_ipc_shared_t) == 1536,
               "IPC ring fits the LP page's spare interval");

/** @brief CRC-32/ISO-HDLC update; start with UINT32_MAX and complement to finish. */
static inline uint32_t tiku_ipc_shared_crc(uint32_t crc,
    const volatile uint8_t *p, uint32_t length)
{
    unsigned bit;
    while (length--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320UL : 0);
    }
    return crc;
}

/** @brief Announce boot without accepting descriptors from the previous epoch. */
static inline void tiku_ipc_shared_peer_boot(tiku_ipc_shared_t *ring,
                                             void (*barrier)(void))
{
    ring->peer.epoch = ring->host.epoch;
    barrier();
    ring->peer.magic = TIKU_IPC_SHARED_RESTART;
    ring->peer.service_stamp = ring->peer.service_max_gap = ring->peer.service_calls = 0;
    ring->peer.service_valid = 0;
    barrier();
}

/** @brief Sample a wrapping peer cycle counter at a service opportunity. */
static inline void tiku_ipc_shared_peer_sample(tiku_ipc_shared_t *ring, uint32_t now)
{
    uint32_t gap = now - ring->peer.service_stamp;
    if (ring->peer.service_valid && gap > ring->peer.service_max_gap)
        ring->peer.service_max_gap = gap;
    ring->peer.service_stamp = now;
    ring->peer.service_valid = 1;
}

/**
 * @brief Service up to two slots on an uncached peer, with no DMA.
 *
 * Input chunks contribute to the byte counter and CRC. OUTPUT produces the
 * sequence's byte pattern; ECHO returns the input bytes. Publish the reply
 * sequence after payload and status.
 * @note A changed epoch is acknowledged between slots, after the preceding
 *       call has ended its payload access.
 */
static inline void tiku_ipc_shared_peer_step(tiku_ipc_shared_t *ring,
                                             void (*barrier)(void))
{
    unsigned i;
    uint32_t epoch, sequence, length, kind, crc, n;
    if (ring->host.magic != TIKU_IPC_SHARED_MAGIC || !ring->host.epoch) return;
    barrier();
    epoch = ring->host.epoch;
    if (ring->peer.magic == TIKU_IPC_SHARED_RESTART && ring->peer.epoch == epoch)
        return;
    if (ring->peer.epoch != epoch || ring->peer.magic != TIKU_IPC_SHARED_MAGIC) {
        ring->peer.bytes = ring->peer.crc = ring->peer.errors = 0;
        ring->peer.service_max_gap = ring->peer.service_calls = 0;
        ring->peer.control_ack = 0;
        for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) {
            ring->slots[i].reply.epoch = epoch;
            ring->slots[i].reply.sequence = 0;
        }
        ring->peer.magic = TIKU_IPC_SHARED_MAGIC;
        barrier();
        ring->peer.epoch = epoch;
    }
    ring->peer.service_calls++;
    sequence = ring->host.control_sequence;
    if (ring->host.control_epoch == epoch && sequence &&
        sequence != ring->peer.control_ack) {
        barrier();
        length = ring->host.control_length;
        if (length <= sizeof ring->host.control_data &&
            ~tiku_ipc_shared_crc(UINT32_MAX, ring->host.control_data, length) ==
             ring->host.control_crc) {
            for (n = 0; n < length; n++)
                ring->peer.control_data[n] = ring->host.control_data[n];
            ring->peer.control_length = length;
            ring->peer.control_crc = ring->host.control_crc;
            ring->peer.control_result = 0;
        } else {
            ring->peer.control_result = 1;
            ring->peer.control_length = 0;
            ring->peer.control_crc = 0;
            ring->peer.errors++;
        }
        barrier();
        ring->peer.control_ack = sequence;
    }
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) {
        tiku_ipc_shared_slot_t *slot = &ring->slots[i];
        sequence = slot->request.sequence;
        if (slot->request.epoch != epoch || !sequence ||
            (slot->reply.epoch == epoch && slot->reply.sequence == sequence)) continue;
        barrier();
        length = slot->request.length; kind = slot->request.kind;
        slot->reply.result = 0;
        slot->reply.length = 0;
        if (length > TIKU_IPC_SHARED_PAYLOAD || kind > TIKU_IPC_SHARED_ECHO) {
            slot->reply.result = 1;
        } else if (kind != TIKU_IPC_SHARED_OUTPUT &&
            ~tiku_ipc_shared_crc(UINT32_MAX, slot->payload, length) !=
             slot->request.crc) {
            slot->reply.result = 1;
        } else {
            if (kind == TIKU_IPC_SHARED_OUTPUT)
                for (n = 0; n < length; n++) slot->payload[n] = (uint8_t)sequence;
            crc = tiku_ipc_shared_crc(UINT32_MAX, slot->payload, length);
            slot->reply.crc = ~crc;
            slot->reply.length = length;
            if (kind == TIKU_IPC_SHARED_INPUT) {
                ring->peer.bytes += length;
                ring->peer.crc = ~tiku_ipc_shared_crc(~ring->peer.crc,
                                                     slot->payload, length);
            }
        }
        if (slot->reply.result) ring->peer.errors++;
        slot->reply.epoch = epoch;
        barrier();
        slot->reply.sequence = sequence;
    }
    ring->peer.heartbeat++;
}
#endif
