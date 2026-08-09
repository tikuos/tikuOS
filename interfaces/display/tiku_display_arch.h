/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.h - what a screen backend must provide.
 *
 * Damage tracking and the portable entry points live in tiku_display.c; a
 * backend supplies only the primitives its hardware performs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DISPLAY_ARCH_H_
#define TIKU_DISPLAY_ARCH_H_

#include "tiku_display.h"

/**
 * @brief Bring the panel and drawing engine up for a bound framebuffer.
 *
 * @param d  Screen, with fb/w/h/stride already filled in
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_arch_init(tiku_display_t *d);

/** @brief Optional primitives this backend really performs. */
uint32_t tiku_display_arch_caps(void);

/** @brief The framebuffer layout this backend scans. */
tiku_display_fmt_t tiku_display_arch_format(void);

/**
 * @brief The panel's fixed size.
 *
 * @param w  Receives width in pixels
 * @param h  Receives height in pixels
 */
void tiku_display_arch_geometry(uint16_t *w, uint16_t *h);

/**
 * @brief Fill a rectangle already clipped to the screen.
 *
 * @param d      Screen
 * @param x      Left edge, on-screen
 * @param y      Top edge, on-screen
 * @param w      Width in pixels
 * @param h      Height in pixels
 * @param colour ARGB8888
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, uint32_t colour);

/**
 * @brief Fill a circle; refuse unless CAP_CIRCLE is advertised.
 *
 * @param d      Screen
 * @param cx     Centre x
 * @param cy     Centre y
 * @param r      Radius
 * @param colour ARGB8888
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED
 */
int tiku_display_arch_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                                  uint16_t r, uint32_t colour);

/**
 * @brief Fill a rounded rectangle; refuse unless CAP_ROUNDED is advertised.
 *
 * @param d      Screen
 * @param x      Left edge
 * @param y      Top edge
 * @param w      Width in pixels
 * @param h      Height in pixels
 * @param r      Corner radius
 * @param colour ARGB8888
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED
 */
int tiku_display_arch_fill_rounded_rect(tiku_display_t *d,
                                        int16_t x, int16_t y,
                                        uint16_t w, uint16_t h, uint16_t r,
                                        uint32_t colour);

/**
 * @brief Make one region of the framebuffer visible on the glass.
 *
 * @param d  Screen
 * @param x  Left edge of the region
 * @param y  Top edge of the region
 * @param w  Width in pixels
 * @param h  Height in pixels
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h);

#endif /* TIKU_DISPLAY_ARCH_H_ */
