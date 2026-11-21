/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_control.h - owner-driven listener command lifecycle.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_CONTROL_H_
#define TIKU_MEDIA_TRANSFER_CONTROL_H_
#include "tiku_media_store.h"

/** @brief Command completion codes in addition to the media error codes. */
enum { TIKU_MT_CONTROL_PENDING=1, TIKU_MT_CONTROL_CANCELLED=-11, TIKU_MT_CONTROL_OWNER=-12 };
/** @brief Listener commands consume the current configuration generation. */
typedef enum { TIKU_MT_CONTROL_ENABLE, TIKU_MT_CONTROL_STOP } tiku_mt_control_command_t;
/** @brief Observable lifecycle states; FAILED can retain resources requiring Stop. */
typedef enum { TIKU_MT_CONTROL_UNCONFIGURED, TIKU_MT_CONTROL_STOPPED,
    TIKU_MT_CONTROL_STARTING, TIKU_MT_CONTROL_READY, TIKU_MT_CONTROL_STOPPING,
    TIKU_MT_CONTROL_FAILED } tiku_mt_control_state_t;
/** @brief Backend steps; QUIESCE must cancel pending preparation and reject new work. */
typedef enum { TIKU_MT_CONTROL_PREPARE, TIKU_MT_CONTROL_ATTACH, TIKU_MT_CONTROL_QUIESCE,
    TIKU_MT_CONTROL_DETACH, TIKU_MT_CONTROL_RELEASE, TIKU_MT_CONTROL_WIPE } tiku_mt_control_phase_t;
/** @brief Service-private provisioned endpoint, key and mounted-volume identity. */
typedef struct { uint16_t port; uint8_t key[32], volume[16]; } tiku_mt_control_profile_t;
/**
 * @brief Backend owner and bounded phase callbacks; contexts outlive the controller.
 * @note Each step returns 0 complete, PENDING/BUSY to retry, or a negative error.
 * A callback must bound its work, retain phase ownership across retries and not
 * reenter this controller. QUIESCE drains a pending PREPARE/ATTACH before DETACH.
 * WIPE releases session resources while preserving the controller's profile.
 */
typedef struct {
    int (*in_owner)(void *);
    int (*step)(void *, tiku_mt_control_phase_t, const tiku_mt_control_profile_t *);
} tiku_mt_control_ops_t;
/** @brief Opaque service-owned state; keep it alive until consumers drop their tokens. */
typedef struct tiku_mt_control tiku_mt_control_t;
/** @brief Copied state contains no key or profile pointer. */
typedef struct {
    uint64_t generation, enabling, stopping;
    tiku_mt_control_state_t state;
    int error;
    unsigned cleanup_required, completion_slots_free;
} tiku_mt_control_snapshot_t;
/** @brief Size and alignment of caller-provided controller storage. */
size_t tiku_mt_control_size(void);
/** @brief Required alignment of caller-provided controller storage. */
size_t tiku_mt_control_align(void);
/** @brief Initialize unused aligned storage; does not allocate or call a backend step. */
tiku_mt_control_t *tiku_mt_control_init(void *, size_t, tiku_mt_control_ops_t, void *);
/** @brief Copy a nonzero port/key/volume when stopped and all completions are taken. */
int tiku_mt_control_configure(tiku_mt_control_t *, const tiku_mt_control_profile_t *, uint64_t *generation);
/** @brief Erase the profile and invalidate its generation when stopped with no completions. */
int tiku_mt_control_forget(tiku_mt_control_t *);
/**
 * @brief Admit a command without backend work; output token is unchanged on refusal.
 * @note Stop can cancel Enable. Repeated pending Stop returns the same token.
 * Two completion slots retain outcomes until take; a full set refuses admission.
 * Enable reserves the final sequence value for Stop. Reinitialization starts a
 * new controller lifetime and requires retiring the preceding consumer references.
 */
int tiku_mt_control_submit(tiku_mt_control_t *, tiku_mt_control_command_t, uint64_t generation, uint64_t *token);
/** @brief Advance at most one backend phase call; idle/terminal returns 0, active returns PENDING. */
int tiku_mt_control_step(tiku_mt_control_t *);
/** @brief Copy state without backend progression; wrong-owner calls leave output unchanged. */
int tiku_mt_control_observe(tiku_mt_control_t *, tiku_mt_control_snapshot_t *);
/** @brief Take a terminal result once; PENDING retains the slot, stale tokens return STALE. */
int tiku_mt_control_take(tiku_mt_control_t *, uint64_t token, int *result);
/**
 * @brief Release one client's writer or one reader without acquiring the medium.
 * @return PENDING after one release, 0 when empty, or a media error; no queue is
 * consumed when it contains another request. Call in the serialized store owner.
 */
int tiku_mt_control_release_one(tiku_media_store_t *, uint32_t client);
/** @brief Zero at most 512 session bytes per call; cursor storage must not overlap the buffer. */
int tiku_mt_control_wipe(void *, size_t, size_t *cursor);
#endif
