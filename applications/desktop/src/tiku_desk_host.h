/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_host.h - native display host behind a portable event boundary.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_HOST_H_
#define TIKU_DESK_HOST_H_

#include "tiku_desk_event.h"
#include "tiku_desk_gfx.h"

typedef struct tiku_desk_host tiku_desk_host_t;

/** @brief Open one native window.  @return NULL when no display is present. */
tiku_desk_host_t *tiku_desk_host_open(int width, int height,
                                      const char *title);

/** @brief Release the native window and every platform resource it owns. */
void tiku_desk_host_close(tiku_desk_host_t *host);

/** @brief Copy the software surface into the native window. */
int tiku_desk_host_present(tiku_desk_host_t *host,
                           const tiku_desk_surface_t *surface);

/** @brief Change the native window title. */
void tiku_desk_host_set_title(tiku_desk_host_t *host, const char *title);

/** @brief Poll one normalized event without blocking. */
int tiku_desk_host_poll(tiku_desk_host_t *host, tiku_desk_event_t *event);

/** @brief Select the horizontal-resize pointer while a divider is hot. */
void tiku_desk_host_set_resize_cursor(tiku_desk_host_t *host, int enabled);

/** @brief Read the current pointer position and logical modifier state. */
int tiku_desk_host_pointer(tiku_desk_host_t *host, int *x, int *y,
                           unsigned *modifiers);

#endif /* TIKU_DESK_HOST_H_ */
