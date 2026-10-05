/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dc_arch.h - Apollo510 display path: NemaDC, DSI host and CO5300 panel.
 *
 * Drives the round 468x468 AMOLED over MIPI DSI with blocking one-shot frame
 * pushes and polled completion.  The DC reads the framebuffer as a
 * non-coherent bus master, so each present cleans the D-cache range first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_DC_ARCH_H_
#define TIKU_AMBIQ_DC_ARCH_H_

#include <stdint.h>

/**
 * @name Panel geometry, in pixels (CO5300 round AMOLED, EVB display kit)
 * @{
 */
#define TIKU_DC_PANEL_W   468u
#define TIKU_DC_PANEL_H   468u
/** @} */

/** Driver result codes. */
typedef enum {
    TIKU_DC_OK           =  0,   /**< Success                                 */
    TIKU_DC_ERR_POWER    = -1,   /**< DISP/DISPPHY power never came up        */
    TIKU_DC_ERR_ID       = -2,   /**< DC IDREG != 0x87452365                  */
    TIKU_DC_ERR_DSI      = -3,   /**< DSI PHY INITDONE timeout                */
    TIKU_DC_ERR_TIMEOUT  = -4,   /**< DBI/frame busy stuck, or a bad rect     */
} tiku_dc_err_t;

/** Scanout formats (values are the NemaDC layer-format codes). */
typedef enum {
    TIKU_DC_FMT_RGB24    = 0x0B,  /**< 3 B/px, packed                         */
    TIKU_DC_FMT_RGBA8888 = 0x0D,  /**< 4 B/px, alpha ignored on scanout       */
} tiku_dc_fmt_t;

/**
 * @brief Full display bring-up: pins, VDD18, DSI PHY, DC, panel init.
 *
 * Order: display pins -> DISPPHY power + DSI clocks -> DISP power + DC
 * identify -> DSI PHY config -> DC configure (DBIDSI, RGB888, 468x468) ->
 * panel hardware reset -> CO5300 DCS init.
 *
 * @note Blocks for the panel's reset and init delays, about 0.8 s in all.
 * @return TIKU_DC_OK, or the TIKU_DC_ERR_* of the stage that failed.
 */
tiku_dc_err_t tiku_dc_init(void);

/**
 * @brief Push one frame from an SSRAM surface to the panel (blocking).
 *
 * Cleans the D-cache over the surface, programs layer 0 at @p fb, issues DCS
 * write_memory_start, scans one frame, and polls until the transfer ends.
 *
 * @note @p fb must be in SSRAM, not DTCM or ITCM.  @p w and @p h are not
 *       checked against the panel.
 *
 * @param fb            Surface base (32-byte aligned recommended).
 * @param w             Width in pixels  (<= TIKU_DC_PANEL_W).
 * @param h             Height in pixels (<= TIKU_DC_PANEL_H).
 * @param stride_bytes  Bytes per row.
 * @param fmt           Scanout format of @p fb.
 * @return TIKU_DC_OK or TIKU_DC_ERR_TIMEOUT.
 */
tiku_dc_err_t tiku_dc_present(const void *fb, uint16_t w, uint16_t h,
                              uint16_t stride_bytes, tiku_dc_fmt_t fmt);

/**
 * @brief Push only a sub-rectangle of a surface to the panel (partial update).
 *
 * Sets the panel window to the rectangle (DCS CASET/RASET), scans the
 * rectangle out of the framebuffer, then restores the full panel window.
 *
 * @param fb         Full surface base (must be in SSRAM).
 * @param fb_stride  Bytes per row of the full surface.
 * @param x,y,w,h    Damage rectangle (must lie within the panel).
 * @param fmt        Scanout format.
 * @return TIKU_DC_OK, or TIKU_DC_ERR_TIMEOUT (incl. a rect outside the panel).
 */
tiku_dc_err_t tiku_dc_present_rect(const void *fb, uint16_t fb_stride,
                                   uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   tiku_dc_fmt_t fmt);

/** @brief Frames successfully presented since init (test/diagnostic). */
uint32_t tiku_dc_frame_count(void);

/** @brief Raw DC STATUS register (diagnostics). */
uint32_t tiku_dc_status(void);

#endif /* TIKU_AMBIQ_DC_ARCH_H_ */
