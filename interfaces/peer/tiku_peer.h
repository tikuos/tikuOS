/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer.h - a peer: another processor this TikuOS instance works with.
 *
 * A board lists its peers in tiku_peer_table.  Each has a lifecycle, a
 * carrier and services, which are the kernel IPC endpoints named
 * <peer>/<service>, in consecutive rows of the endpoint table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PEER_H_
#define TIKU_PEER_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* STATES                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Where a peer is in its life, as this side sees it. */
typedef enum {
    TIKU_PEER_OFF = 0,   /**< unpowered, or never started */
    TIKU_PEER_STARTING,  /**< started; its first service has not answered */
    TIKU_PEER_READY,     /**< its services are usable */
    TIKU_PEER_LOST       /**< started and not answering: see the reason */
} tiku_peer_state_t;

/** @brief Why a peer is TIKU_PEER_LOST. */
typedef enum {
    TIKU_PEER_WHY_NONE = 0,
    TIKU_PEER_WHY_TIMEOUT,  /**< no answer within the driver's start time */
    TIKU_PEER_WHY_FAULT,    /**< the peer reported a fault */
    TIKU_PEER_WHY_HANG,     /**< its heartbeat stopped */
    TIKU_PEER_WHY_CARRIER   /**< the carrier refused or closed */
} tiku_peer_why_t;

/** @defgroup TIKU_PEER_FLAGS Peer flags
 * @{ */
/** This side switches the peer's power or reset; without it, it is the worker. */
#define TIKU_PEER_F_SUPERVISOR  (1u << 0)
/** A start cannot be undone until the board resets. */
#define TIKU_PEER_F_ONESHOT     (1u << 1)
/** @} */

/** @defgroup TIKU_PEER_STATUS Return codes
 * @{ */
#define TIKU_PEER_OK            0   /**< Done */
#define TIKU_PEER_ERR_NAME    (-1)  /**< No such peer */
#define TIKU_PEER_ERR_ROLE    (-2)  /**< This side has no such control of it */
#define TIKU_PEER_ERR_STATE   (-3)  /**< Not possible in its state or flags */
#define TIKU_PEER_ERR_DRIVER  (-4)  /**< Its driver refused or failed */
/** @} */

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief What a driver reports about its peer. */
typedef struct {
    uint8_t  state;     /**< tiku_peer_state_t */
    uint8_t  why;       /**< tiku_peer_why_t, meaningful in TIKU_PEER_LOST */
    uint8_t  mode;      /**< In READY, the driver's mode; 0 is normal service */
    uint8_t  powered;   /**< 1 while the peer has power, as far as known */
    uint16_t restarts;  /**< Times it came back as a new instance since boot */
} tiku_peer_status_t;

/** @brief One service: its name on the carrier, equal on both sides, and
 *         its channel there. */
typedef struct {
    const char *name;
    uint8_t     channel;
} tiku_peer_service_t;

/**
 * @brief A peer driver.  Every call takes the peer's context, is bounded and
 *        dispatches no process.
 *
 * prepare runs before kernel IPC starts and bind after it, with the table
 * row of the first service.  start, stop and reset act on the whole peer and
 * are called on the supervisor side only; a NULL one it does not have.
 */
typedef struct {
    int  (*prepare)(void *context);
    int  (*bind)(void *context, uint16_t first_endpoint);
    int  (*start)(void *context);
    int  (*stop)(void *context);
    int  (*reset)(void *context);
    void (*status)(void *context, tiku_peer_status_t *out);
} tiku_peer_ops_t;

/** @brief One peer of this board. */
typedef struct {
    const char                *name;      /**< This side's name for it */
    const char                *carrier;   /**< "uart", "console", "sdio", ... */
    const tiku_peer_ops_t     *ops;
    void                      *context;
    const tiku_peer_service_t *services;
    uint8_t                    service_count;
    uint8_t                    flags;     /**< TIKU_PEER_F_* */
} tiku_peer_t;

/** @brief The board's peers; boot assembly defines both. */
extern const tiku_peer_t *const tiku_peer_table[];
extern const uint8_t tiku_peer_count;

/*---------------------------------------------------------------------------*/
/* FUNCTIONS                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Index of the peer named @p name, or TIKU_PEER_ERR_NAME. */
int tiku_peer_find(const char *name);

/** @brief The peer at @p index, or NULL past the table. */
const tiku_peer_t *tiku_peer_get(uint8_t index);

/** @brief Position of the service named @p service in the list of the peer
 *         at @p index, or TIKU_PEER_ERR_NAME for no such peer or service. */
int tiku_peer_service(uint8_t index, const char *service);

/**
 * @brief Start a peer: power or launch it and open its carrier.
 * @return TIKU_PEER_OK, or ERR_NAME, ERR_ROLE (no start on this side) or
 *         ERR_DRIVER
 */
int tiku_peer_start(uint8_t index);

/**
 * @brief Stop a peer: close its services and take its power.
 * @return TIKU_PEER_OK, or ERR_NAME, ERR_ROLE, ERR_STATE for a ONESHOT peer,
 *         or ERR_DRIVER
 */
int tiku_peer_stop(uint8_t index);

/** @brief Reset a peer as a whole; its services renegotiate.
 *  @return TIKU_PEER_OK, or ERR_NAME, ERR_ROLE or ERR_DRIVER */
int tiku_peer_reset(uint8_t index);

/** @brief Read a peer's state.  @return TIKU_PEER_OK or ERR_NAME */
int tiku_peer_status(uint8_t index, tiku_peer_status_t *out);

/** @brief Run every driver's prepare, before kernel IPC starts.
 *  @return TIKU_PEER_OK, or ERR_DRIVER at the first that fails */
int tiku_peer_prepare_all(void);

/**
 * @brief Bind every peer's services, after kernel IPC has started.
 *
 * Finds the table row named <peer>/<first service> and hands it to the
 * driver's bind.
 *
 * @return TIKU_PEER_OK, ERR_NAME when a peer's row is missing, or ERR_DRIVER
 */
int tiku_peer_bind_all(void);

#endif /* TIKU_PEER_H_ */
