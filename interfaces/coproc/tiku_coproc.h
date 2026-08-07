/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc.h - portable control of a second compute engine.
 *
 * Launch a payload, tell whether it is executing, and exchange one bounded
 * message at a time.  What the payload does with that message is the
 * backend's business, and nothing here exposes the memory the cores share.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_COPROC_H_
#define TIKU_COPROC_H_

#include <stdint.h>

/*
 * Presence and capacity arrive as -D globals from the Makefile beside the
 * backend, never from a board or device header -- those are include-order
 * dependent.  Each backend asserts the published cap against its own mailbox
 * constant, which is what keeps the two from drifting apart.
 */
#ifndef TIKU_HAS_COPROC
#define TIKU_HAS_COPROC         0
#endif
#ifndef TIKU_COPROC_MSG_CAP
#define TIKU_COPROC_MSG_CAP     0u
#endif

/** @brief Outcomes; a caller that only tests != OK still behaves. */
#define TIKU_COPROC_OK           0
#define TIKU_COPROC_ERR_STATE   -1  /**< nothing running to talk to        */
#define TIKU_COPROC_ERR_IMAGE   -2  /**< payload absent or too big         */
#define TIKU_COPROC_ERR_LEN     -3  /**< message empty or over the cap     */
#define TIKU_COPROC_ERR_TIMEOUT -4  /**< the payload never acknowledged    */

/** @brief What this core can see of the engine.  Cheap; never blocks. */
typedef enum {
    TIKU_COPROC_ABSENT = 0,     /**< no second engine in this build       */
    TIKU_COPROC_STOPPED,        /**< never launched, or payload parked    */
    TIKU_COPROC_STARTED,        /**< released, magic not published yet    */
    TIKU_COPROC_RUNNING,        /**< magic published and not parked       */
    TIKU_COPROC_FAULTED         /**< payload reported a fault; start()
                                     decides whether it restarts           */
} tiku_coproc_state_t;

/**
 * @brief The launch cannot be undone; the engine stays powered until reset.
 *
 * Every backend so far: one core cannot be returned to power gating, the
 * other resumes at its current PC rather than its entry point.  A caller
 * that needs the pre-launch machine back must reset the board.
 */
#define TIKU_COPROC_F_ONESHOT   (1u << 0)

/** @brief The payload is a separate image the launch copies in. */
#define TIKU_COPROC_F_OWN_IMAGE (1u << 1)

/** @brief Backend properties; 0 when no coprocessor is present. */
uint32_t tiku_coproc_flags(void);

/** @brief Current state, without disturbing the payload. */
tiku_coproc_state_t tiku_coproc_state(void);

/**
 * @brief Launch the payload, or resume one already launched.
 *
 * @note Not symmetric with stop().  Under TIKU_COPROC_F_ONESHOT the first
 *       call is a one-way door and may leave other subsystems constrained
 *       for the rest of the power-on; those subsystems, not this one,
 *       report the refusal.
 * @return TIKU_COPROC_OK, ERR_IMAGE, or ERR_STATE
 */
int tiku_coproc_start(void);

/**
 * @brief Ask the payload to park, and wait for it to get there.
 *
 * @note Cooperative on every backend: no register stalls a core that is
 *       already fetching, so a wedged payload cannot be stopped from here.
 * @return TIKU_COPROC_OK once parked, ERR_TIMEOUT if it never acknowledged
 */
int tiku_coproc_stop(void);

/**
 * @brief Is the payload executing now, as opposed to having once run?
 *
 * @note BLOCKS for a short bounded interval: a counter that is merely
 *       non-zero proves the payload started, and only a moving one proves
 *       it has not since wedged.  Not for a hot loop, not for an ISR.
 * @return Non-zero when the magic is published and the heartbeat advances
 */
int tiku_coproc_alive(void);

/**
 * @brief The payload's forever-counter.
 *
 * @note May perform cache maintenance; not ISR-safe.  Meaningful only as a
 *       difference between two reads -- the rate is the payload's business,
 *       and a backend may freeze it during a long sub-command.
 * @return A value that advances while the payload runs
 */
uint32_t tiku_coproc_heartbeat(void);

/**
 * @brief Bytes of separately linked payload the launch copies in.
 *
 * @return Image size, or 0 when the payload lives in this image
 */
uint32_t tiku_coproc_image_size(void);

/**
 * @brief Hand one message to the payload, replacing any unread predecessor.
 *
 * Single slot per direction: flow control is the protocol's job, and the
 * echo-shaped protocols in this tree are lock-step by construction.
 *
 * @param data Bytes to send
 * @param len  How many, 1..TIKU_COPROC_MSG_CAP
 * @return TIKU_COPROC_OK, ERR_LEN, or ERR_STATE when nothing is running
 */
int tiku_coproc_send(const void *data, uint32_t len);

/**
 * @brief Make this core's view of the coprocessor current.
 *
 * @note Must precede reply_seq() and reply() for their answers to be fresh.
 *       What it does varies -- refetch memory the other core wrote, drain a
 *       queue, stand in for a doorbell that does not fire -- and none of
 *       that is the caller's business.
 * @return 1 when a reply arrived since the previous call, else 0
 */
int tiku_coproc_poll(void);

/**
 * @brief A value that changes exactly when a new reply lands.
 *
 * @note OPAQUE.  Backends variously count replies or echo the sequence they
 *       answered, so compare it against a value saved earlier -- never
 *       against a sequence believed sent, and never assume it starts at zero
 *       or advances by one.
 * @return The backend's reply marker
 */
uint32_t tiku_coproc_reply_seq(void);

/**
 * @brief Collect the reply to the most recent send.
 *
 * @param out Destination, or NULL to ask only for the length
 * @param cap Bytes available at @p out; a longer reply is truncated
 * @return Bytes the payload replied, or 0 when it has not answered yet
 */
uint32_t tiku_coproc_reply(void *out, uint32_t cap);

#endif /* TIKU_COPROC_H_ */
