/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_ctl.h - rail, bus and coprocessor controls of a two-chip board.
 *
 * Implemented by a board whose always-on processor switches them (BOARD_CAPS
 * BOARD_CTL).  An order that would break one of the board's hand-over rules
 * is refused and changes nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_CTL_H_
#define TIKU_BOARD_CTL_H_

#include <stdint.h>

/** @defgroup TIKU_BOARD_BUS Owner of the switched sensor bus
 * @{ */
#define TIKU_BOARD_BUS_OFF      0   /**< Switches open: neither processor */
#define TIKU_BOARD_BUS_HOST     1   /**< The always-on processor */
#define TIKU_BOARD_BUS_COPROC   2   /**< The coprocessor */
/** @} */

/** @defgroup TIKU_BOARD_CTL_STATUS Return codes
 * @{ */
#define TIKU_BOARD_CTL_OK          0   /**< Done */
#define TIKU_BOARD_CTL_ERR_ORDER (-1)  /**< Refused: it would break a rule */
#define TIKU_BOARD_CTL_ERR_PARAM (-2)  /**< Unknown bus owner */
#define TIKU_BOARD_CTL_ERR_BUSY  (-3)  /**< The coprocessor kept the bus */
/** @} */

/** @brief Microseconds a borrow waits for the coprocessor to finish with the
 *         bus before it gives up. */
#ifndef TIKU_BOARD_CTL_BORROW_US
#define TIKU_BOARD_CTL_BORROW_US  1000000UL
#endif

/** @brief The board's controls as the pin registers show them. */
typedef struct {
    uint8_t rail;           /**< 1: sensor rail on */
    uint8_t bus;            /**< TIKU_BOARD_BUS_* */
    uint8_t coproc_power;   /**< 1: coprocessor supply on */
    uint8_t coproc_reset;   /**< 1: reset line driven low */
    uint8_t wake_out;       /**< Level driven on the wake line to the coprocessor */
    uint8_t wake_in;        /**< Level read on the wake line from the coprocessor */
} tiku_board_ctl_state_t;

/**
 * @brief Switch the sensor rail.
 *
 * Off takes the bus back from the coprocessor first; the parts on the rail
 * and the bus switches lose power with it.
 */
int tiku_board_ctl_rail(uint8_t on);

/**
 * @brief Hand the sensor bus to @p owner (TIKU_BOARD_BUS_*).
 *
 * The coprocessor gets it only with the rail and its supply on.  The
 * switches open before the select line moves.
 */
int tiku_board_ctl_bus(uint8_t owner);

/**
 * @brief Switch the coprocessor's supply.
 *
 * Off takes the bus back, drives the link outputs low, makes the link input
 * pulled down and releases the reset line before the supply drops; on
 * restores the link pins' functions from before the last off.
 */
int tiku_board_ctl_coproc_power(uint8_t on);

/**
 * @brief Pulse the coprocessor's reset line low.
 *
 * Takes the bus back first.  Refused while the coprocessor is unpowered.
 */
int tiku_board_ctl_coproc_reset(void);

/**
 * @brief Drive the wake line to the coprocessor.
 *
 * A high level is refused while the coprocessor is unpowered.
 */
int tiku_board_ctl_wake(uint8_t level);

/**
 * @brief Name the owner the bus goes back to.  TIKU_BOARD_BUS_COPROC gives it
 *        to the coprocessor at once, and after its power-on, its reset and a
 *        borrow, while the rail and its supply are on.  HOST is the default.
 */
int tiku_board_ctl_bus_default(uint8_t owner);

/**
 * @brief Take the bus for one operation.  From a powered coprocessor that
 *        holds it, the wake lines become a handshake: this side raises its
 *        line and switches once the coprocessor's line, its busy flag, is low.
 * @return TIKU_BOARD_CTL_OK, ERR_ORDER with the rail off, or ERR_BUSY when
 *         the coprocessor stayed busy for TIKU_BOARD_CTL_BORROW_US
 */
int tiku_board_ctl_borrow(void);

/** @brief End a borrow: the bus back to its default owner, then this side's
 *         line low. */
void tiku_board_ctl_borrow_end(void);

/** @brief Read the controls back from the pin registers. */
void tiku_board_ctl_state(tiku_board_ctl_state_t *s);

#endif /* TIKU_BOARD_CTL_H_ */
