/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_touch_gt911.c - GT911 reports on the public touch interface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_touch.h"
#include <drivers/input/gt911/tiku_drv_input_gt911.h>

int tiku_touch_probe(void) { return tiku_drv_input_gt911_probe(); }
int tiku_touch_poll(tiku_touch_report_t *out) { return tiku_drv_input_gt911_poll(out); }
