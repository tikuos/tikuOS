/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_service.h - provisioned S31 transfer owner and command API.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_SERVICE_H_
#define TIKU_MEDIA_TRANSFER_SERVICE_H_
#include "tiku_media_transfer_tcp.h"
#ifndef TIKU_MEDIA_TRANSFER_SERVICE_ENABLE
/** @brief Enable the independently scheduled provisioned TCP transfer owner. */
#define TIKU_MEDIA_TRANSFER_SERVICE_ENABLE 0
#endif
/** @brief Copied service state and the latest consumed command outcome. */
typedef struct {
    tiku_mt_tcp_snapshot_t transfer;
    uint64_t last_token;
    int last_result;
    unsigned available;
} tiku_mt_service_snapshot_t;
/** @brief Provision a stopped keyed endpoint against the mounted volume.
 * @note The console owner calls this before GUI Enable. First provisioning reserves
 * PSRAM and initializes fixed queue metadata; it does not attach a listener or
 * clear file staging. Call Stop and Forget before destructive allocator/network reset.
 */
int tiku_mt_service_configure(uint16_t,const uint8_t key[32],uint64_t *generation);
/** @brief Submit a typed command without waiting for transport or storage. */
int tiku_mt_service_submit(tiku_mt_control_command_t,uint64_t generation,uint64_t *token);
/** @brief Consume a completed command and retain its copied outcome for observation. */
int tiku_mt_service_take(uint64_t token,int *result);
/** @brief Read copied state; no provisioning reports UNCONFIGURED and available. */
int tiku_mt_service_observe(tiku_mt_service_snapshot_t *);
/** @brief Erase provisioning and release backing after Stop and consumed completions.
 * @note BUSY retains backing while a shared media request owned by this scheduler
 * retires. Foreign requests are completed, not canceled, before release.
 */
int tiku_mt_service_forget(void);
/** @brief Advance one service or media-owner phase; the dedicated process schedules this call. */
void tiku_mt_service_step(void);
/** @brief Provision and admit Enable for console callers; PENDING requires readiness polling. */
int tiku_mt_service_start(uint16_t,const uint8_t key[32]);
/** @brief Admit Stop for console callers; PENDING retains cleanup and provisioning. */
int tiku_mt_service_stop(void);
#endif
