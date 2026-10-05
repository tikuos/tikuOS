/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_policy.h - the idle mode the scheduler enters, chosen by name.
 *
 * One setter behind the sleep command and /sys/power/policy.  A mode is taken
 * only when the port has its own entry for it and an armed source wakes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_POWER_POLICY_H_
#define TIKU_POWER_POLICY_H_

#include <stdint.h>
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* RESULT CODES                                                              */
/*---------------------------------------------------------------------------*/

enum {
    TIKU_POWER_OK           =  0, /**< mode installed                        */
    TIKU_POWER_INVALID      = -1, /**< not an idle mode                      */
    TIKU_POWER_UNSUPPORTED  = -2, /**< the port has no entry of its own      */
    TIKU_POWER_NO_WAKE      = -3, /**< no armed source wakes the mode        */
    TIKU_POWER_ACK_REQUIRED = -4  /**< the mode loses console input          */
};

/** Value of tiku_power_policy_get() for an idle hook set elsewhere. */
#define TIKU_POWER_CUSTOM  (-1)

/*---------------------------------------------------------------------------*/
/* QUERIES                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Does the port have an entry of its own for @p mode?
 *
 * Off is always offered, light wherever the port has an idle entry.  A
 * deeper mode whose entry is the same as a shallower one's is not, so a port
 * where every mode is WFI offers off and light only.
 *
 * @return Non-zero when tiku_power_policy_set() may take @p mode
 */
int tiku_power_policy_supported(tiku_cpu_idle_mode_t mode);

/**
 * @brief Does @p mode lose console input sent while the core sleeps?
 *
 * @return Non-zero when setting @p mode needs allow_console_loss
 */
int tiku_power_policy_loses_console(tiku_cpu_idle_mode_t mode);

/**
 * @brief The mode the scheduler enters when idle.
 *
 * Read from the scheduler's hook, so a hook installed by tiku_sched_init() or
 * tiku_sched_set_idle_hook() is reported too.
 *
 * @return A tiku_cpu_idle_mode_t value, or TIKU_POWER_CUSTOM
 */
int tiku_power_policy_get(void);

/**
 * @brief Policy name of a mode: "off", "light", "deep" or "deepest".
 *
 * @param mode  A tiku_cpu_idle_mode_t value or TIKU_POWER_CUSTOM
 * @return The name, or "custom" for anything else
 */
const char *tiku_power_policy_token(int mode);

/**
 * @brief Mode from its policy name.
 *
 * @param token  "off", "light", "deep" or "deepest"
 * @return A tiku_cpu_idle_mode_t value, or -1 for anything else
 */
int tiku_power_policy_parse(const char *token);

/**
 * @brief The current mode's platform name, e.g. "LPM3" or "WFI".
 *
 * @return The HAL's short name, or "custom" for a hook set elsewhere
 */
const char *tiku_power_policy_mode_name(void);

/*---------------------------------------------------------------------------*/
/* SETTER                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Make @p mode the scheduler's idle mode until the next boot.
 *
 * Refused when the port has no entry of its own for the mode, when the mode
 * loses console input and @p allow_console_loss is 0, or when no armed
 * source wakes it.  Nothing is written to NVM.
 *
 * @return TIKU_POWER_OK or one of the negative result codes
 */
int tiku_power_policy_set(tiku_cpu_idle_mode_t mode,
                          uint8_t allow_console_loss);

#endif /* TIKU_POWER_POLICY_H_ */
