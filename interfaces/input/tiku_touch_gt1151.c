/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_touch_gt1151.c - GT1151 reports through the public touch boundary.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_touch.h"
#include <drivers/input/gt1151/tiku_drv_input_gt1151.h>
#include <string.h>

int tiku_touch_probe(void)
{
    tiku_gt1151_identity_t identity;
    return tiku_drv_input_gt1151_identity(&identity);
}

int tiku_touch_poll(tiku_touch_report_t *out)
{
    tiku_gt1151_report_t raw;
    if (!out) return TIKU_DRV_ERR_INVALID;
    memset(out, 0, sizeof *out);
    int result = tiku_drv_input_gt1151_poll(&raw);
    if (result || raw.count > TIKU_TOUCH_MAX_CONTACTS) {
        out->cancel = 1;
        return result ? result : TIKU_DRV_ERR_INVALID;
    }
    out->count = raw.count;
    for (unsigned i = 0; i < raw.count; ++i) {
        out->contacts[i].x = raw.points[i].x;
        out->contacts[i].y = raw.points[i].y;
        out->contacts[i].id = raw.points[i].id;
    }
    return 0;
}
