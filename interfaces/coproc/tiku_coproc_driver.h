/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_driver.h - driver symbols behind the endpoint compatibility API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COPROC_DRIVER_H_
#define TIKU_COPROC_DRIVER_H_
#include "tiku_coproc.h"
#if (TIKU_IPC_ENABLE + 0)
/** @brief Port driver implementation of the portable flags operation. */
uint32_t tiku_coproc_driver_flags(void);
/** @brief Port driver implementation of the portable state operation. */
tiku_coproc_state_t tiku_coproc_driver_state(void);
/** @brief Port driver implementation of the portable start operation. */
int tiku_coproc_driver_start(void);
/** @brief Port driver implementation of the portable stop operation. */
int tiku_coproc_driver_stop(void);
/** @brief Port driver implementation of the portable alive operation. */
int tiku_coproc_driver_alive(void);
/** @brief Port driver implementation of the portable heartbeat operation. */
uint32_t tiku_coproc_driver_heartbeat(void);
/** @brief Port driver implementation of the portable image_size operation. */
uint32_t tiku_coproc_driver_image_size(void);
/** @brief Port driver implementation of the portable send operation. */
int tiku_coproc_driver_send(const void *data, uint32_t length);
/** @brief Port driver implementation of the portable poll operation. */
int tiku_coproc_driver_poll(void);
/** @brief Port driver implementation of the portable reply_seq operation. */
uint32_t tiku_coproc_driver_reply_seq(void);
/** @brief Port driver implementation of the portable reply operation. */
uint32_t tiku_coproc_driver_reply(void *out, uint32_t capacity);
#if defined(TIKU_COPROC_DRIVER_IMPLEMENTATION)
#define tiku_coproc_flags tiku_coproc_driver_flags
#define tiku_coproc_state tiku_coproc_driver_state
#define tiku_coproc_start tiku_coproc_driver_start
#define tiku_coproc_stop tiku_coproc_driver_stop
#define tiku_coproc_alive tiku_coproc_driver_alive
#define tiku_coproc_heartbeat tiku_coproc_driver_heartbeat
#define tiku_coproc_image_size tiku_coproc_driver_image_size
#define tiku_coproc_send tiku_coproc_driver_send
#define tiku_coproc_poll tiku_coproc_driver_poll
#define tiku_coproc_reply_seq tiku_coproc_driver_reply_seq
#define tiku_coproc_reply tiku_coproc_driver_reply
#endif
#endif
#endif
