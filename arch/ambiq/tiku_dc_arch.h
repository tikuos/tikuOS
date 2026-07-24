/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dc_arch.h - Apollo510 display path: NemaDC + DSI host + CO5300 panel.
 *
 * From-scratch, register-level -- NO AmbiqSuite HAL, no NemaDC library. The
 * register sequences were recovered from the vendored MIT-granted ThinkSi
 * sources (nema_dc_regs.h + the open nema_dc_hal.c port layer), disassembly
 * of the vendored libam_hal.a/lib_nema_apollo510_nemagfx.a treated as
 * documentation, and a J-Link register capture of the running vendor demo
 * (golden values noted in-line in the .c). See temp/gpu-roadmap.md.
 *
 * Scope (first milestone): the Apollo510 EVB round-display kit -- 468x468
 * CO5300 AMOLED on MIPI DSI (1 lane, 16-bit DBI bridge, trim X20). Synchronous
 * one-shot frame pushes, polled completion, no TE sync yet. The DC scans any
 * SSRAM surface; pair with tiku_gpu_arch for GPU-rendered frames.
 *
 * Cache rule: the DC is a non-coherent bus master READING the framebuffer --
 * tiku_dc_present() cleans the D-cache range before the push.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_DC_ARCH_H_
#define TIKU_AMBIQ_DC_ARCH_H_

#include <stdint.h>

/** Panel geometry (CO5300 round AMOLED on the EVB display kit). */
#define TIKU_DC_PANEL_W   468u
#define TIKU_DC_PANEL_H   468u

/** Driver result codes. */
typedef enum {
    TIKU_DC_OK           =  0,
    TIKU_DC_ERR_POWER    = -1,   /**< DISP/DISPPHY power domain never came up   */
    TIKU_DC_ERR_ID       = -2,   /**< DC IDREG != 0x87452365 (dead/unclocked)   */
    TIKU_DC_ERR_DSI      = -3,   /**< DSI PHY INITDONE timeout                  */
    TIKU_DC_ERR_TIMEOUT  = -4,   /**< DBI/frame busy never cleared              */
} tiku_dc_err_t;

/** Scanout formats (values are the NemaDC layer-format codes). */
typedef enum {
    TIKU_DC_FMT_RGB24    = 0x0B,  /**< 3 B/px, packed                           */
    TIKU_DC_FMT_RGBA8888 = 0x0D,  /**< 4 B/px, alpha ignored on scanout         */
} tiku_dc_fmt_t;

/**
 * @brief Full display bring-up: pins, VDD18, DSI PHY, DC, panel init.
 *
 * Sequence (mirrors the proven vendor order): display pins -> DISPPHY power +
 * DSI clocks -> DISP power -> DC identify -> DSI para-config (1 lane, DBI16,
 * trim X20) -> panel hardware reset -> DC configure (DBIDSI, RGB888 bridge,
 * 468x468) -> CO5300 DCS init (sleep-out, display-on, window, tear-off).
 * Blocking; includes ~700 ms of mandatory panel delays.
 */
tiku_dc_err_t tiku_dc_init(void);

/**
 * @brief Push one frame from an SSRAM surface to the panel (blocking).
 *
 * Programs layer 0 at @p fb, issues DCS write_memory_start, scans exactly one
 * frame, and polls until the transfer completes. Cleans the D-cache range
 * first; @p fb MUST be in SSRAM (never DTCM/ITCM).
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
 * Transfers the @p w x @p h region at (@p x, @p y) of a surface whose full row
 * pitch is @p fb_stride bytes -- addressing just that window on the panel
 * (DCS CASET/RASET) and scanning the sub-rect out of the framebuffer. Far
 * cheaper than a full frame for small damage. Restores the full panel window
 * afterward so a later tiku_dc_present() is unaffected. @p fb MUST be in SSRAM.
 *
 * @param fb         Full surface base (SSRAM).
 * @param fb_stride  Bytes per row of the FULL surface.
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
