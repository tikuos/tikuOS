/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_touch.h - toolkit-independent complete contact reports.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TOUCH_H_
#define TIKU_TOUCH_H_
#include <stdint.h>
#define TIKU_TOUCH_MAX_CONTACTS 10u
/** @brief Raw contact identity and controller coordinates, before calibration. */
typedef struct { uint16_t x, y; uint8_t id; } tiku_touch_contact_t;
/**
 * @brief One poll's result; cancel requires consumers to abandon active interaction.
 * @note fresh is 1 for a report the controller made since the poll before:
 * count is the contacts on the panel, 0 once the last has lifted. fresh is 0
 * when the controller made none: count is 0 whatever touches the panel, and
 * the consumer keeps the contacts it has.
 */
typedef struct {
    uint8_t count, cancel, fresh;
    tiku_touch_contact_t contacts[TIKU_TOUCH_MAX_CONTACTS];
} tiku_touch_report_t;
/** @brief Probe the provider on an already-open bus; zero succeeds. */
int tiku_touch_probe(void);
/** @brief Read one report; zero succeeds, a negative result sets cancel.
 * @note Calls require serialized foreground execution with other I2C clients.
 * This operation blocks for the transport's bounded transaction waits.
 */
int tiku_touch_poll(tiku_touch_report_t *);
#endif
