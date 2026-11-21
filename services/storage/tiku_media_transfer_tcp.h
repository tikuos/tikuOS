/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_tcp.h - caller-owned TCP transfer service and command boundary.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_TCP_H_
#define TIKU_MEDIA_TRANSFER_TCP_H_
#include "tiku_media_transfer_listener.h"
/** @brief One TCP service binding; callbacks and requests share its serialized owner. */
typedef struct tiku_mt_tcp tiku_mt_tcp_t;
/** @brief Copied command/session state contains no credentials. */
typedef struct {
    tiku_mt_control_snapshot_t control;
    tiku_mt_listener_snapshot_t session;
    uint16_t port;
} tiku_mt_tcp_snapshot_t;
/** @brief Required storage including controller, listener and staging. */
size_t tiku_mt_tcp_size(void);
/** @brief Required storage alignment. */
size_t tiku_mt_tcp_align(void);
/** @brief Bind unused aligned storage after network initialization; one binding can exist.
 * @note Initialization clears fixed metadata without I/O. Owner and cancellation
 * callbacks, store and storage outlive unbind. Cancel returns zero after retiring
 * its request, BUSY while borrowed, or an error. Media owners run independently.
 */
tiku_mt_tcp_t *tiku_mt_tcp_init(void *,size_t,tiku_media_store_t *,uint32_t client,
                               int (*owner)(void *),int (*cancel)(void *,tiku_media_request_t *),void *);
/** @brief Copy a provisioned profile while stopped with consumed command outcomes. */
int tiku_mt_tcp_configure(tiku_mt_tcp_t *,const tiku_mt_control_profile_t *,uint64_t *generation);
/** @brief Admit Enable/Stop without transport work; retain the returned completion token. */
int tiku_mt_tcp_submit(tiku_mt_tcp_t *,tiku_mt_control_command_t,uint64_t generation,uint64_t *token);
/** @brief Consume one terminal command outcome, or return PENDING without consuming it. */
int tiku_mt_tcp_take(tiku_mt_tcp_t *,uint64_t token,int *result);
/** @brief Copy current command and live-listener state without I/O. */
int tiku_mt_tcp_observe(tiku_mt_tcp_t *,tiku_mt_tcp_snapshot_t *);
/** @brief Advance one controller, listener or rejected-connection retirement phase.
 * @note Application work does not send link output. The network owner separately
 * schedules TCP deferred_poll and timers. Stop also retires passive handshakes
 * that have not produced a CONNECTED notification, leaving other listeners intact.
 */
int tiku_mt_tcp_step(tiku_mt_tcp_t *,uint32_t now_ms);
/** @brief Erase provisioning while stopped with consumed command outcomes. */
int tiku_mt_tcp_forget(tiku_mt_tcp_t *);
/** @brief Release the callback binding after Stop, Forget and completion consumption.
 * @note BUSY retains storage; zero permits freeing it. No callback or media request
 * can retain this binding after success. Network initialization must not reset live
 * connection tables while a binding owns reservations.
 */
int tiku_mt_tcp_unbind(tiku_mt_tcp_t *);
#endif
