/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_arch.h - Apollo510 2.5D GPU (Think Silicon / Nema-class) driver.
 *
 * A from-scratch, register-level driver for the GPU block at 0x40090000 --
 * NO vendor blob, no NemaGFX library. The complete register map lives in the
 * vendored CMSIS header (GPU_Type, apollo510.h); this driver programs it
 * directly. apollo510/apollo510b only (Cortex-M55); compiled in behind
 * TIKU_DRV_GPU_ENABLE (the whole TU is omitted otherwise -- no in-file guard).
 *
 * The GPU is a non-coherent AHB bus master: every surface it reads (command
 * lists, source textures) MUST be cleaned from the M55 D-cache before a kick,
 * and every surface it writes (framebuffers) invalidated after completion, and
 * every GPU-visible buffer MUST live in SSRAM (.ssram) -- NEVER in DTCM/ITCM,
 * which are CPU-private and invisible to the GPU bus master.
 *
 * This header currently exposes the P0 (bring-up) surface: power, clocks,
 * identity, reset, and IRQ plumbing. Drawing / compute / async submission land
 * in later phases (see kintsugi GPU plan).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_GPU_ARCH_H_
#define TIKU_AMBIQ_GPU_ARCH_H_

#include <stdint.h>

/** Driver result codes. */
typedef enum {
    TIKU_GPU_OK          =  0,
    TIKU_GPU_ERR_POWER   = -1,   /**< GFX power domain never reached its target */
    TIKU_GPU_ERR_TIMEOUT = -2,   /**< GPU never went idle within the spin bound  */
    TIKU_GPU_ERR_ID      = -3,   /**< IDREG implausible (bad offset / dead core)  */
} tiku_gpu_err_t;

/** GFXPERFREQ performance-mode knob (PWRCTRL->GFXPERFREQ). */
typedef enum {
    TIKU_GPU_PERF_LP_96MHZ   = 0,   /**< low-power, 96 MHz (bring-up default)   */
    TIKU_GPU_PERF_HP1_192MHZ = 1,   /**< high-performance 1, 192 MHz            */
    TIKU_GPU_PERF_HP2_125MHZ = 2,   /**< high-performance 2, 125 MHz            */
} tiku_gpu_perf_t;

/**
 * @brief Bring-up forensics snapshot.
 *
 * Reset values of the opaque/unverified registers, captured right after a
 * clean power-on + SYSCLEAR. Printed by the bench so the actual silicon
 * defaults are on the record before any phase guesses at their meaning.
 */
typedef struct {
    uint32_t id;        /**< IDREG   (fixed GPU ID; nonzero when alive)         */
    uint32_t status;    /**< STATUS  (per-stage busy bits; "CHECK address!")    */
    uint32_t busctrl;   /**< BUSCTRL reset value (opaque)                       */
    uint32_t loadctrl;  /**< LOADCTRL reset value (opaque)                      */
    uint32_t cgctrl;    /**< CGCTRL reset value (clock-gate disables)           */
    uint32_t active;    /**< ACTIVE  (GPUACTIVE / GPUQACTIVE)                   */
} tiku_gpu_bringup_t;

/**
 * @brief Power on the GFX domain, reset the GPU, enable its NVIC line.
 *
 * Owns the full sequence (the boot path leaves GFX off): DEVPWREN.PWRENGFX +
 * wait DEVPWRSTATUS.PWRSTGFX, GFXPERFREQ select, SYSCLEAR, wait-idle, NVIC
 * enable. All waits are spin-bounded and fail closed.
 *
 * @param perf  Performance mode (LP 96 MHz for bring-up).
 * @return TIKU_GPU_OK, or ERR_POWER / ERR_ID / ERR_TIMEOUT.
 */
tiku_gpu_err_t tiku_gpu_init(tiku_gpu_perf_t perf);

/** @brief Disable the NVIC line and power the GFX domain back off. */
void tiku_gpu_deinit(void);

/** @brief 1 if DEVPWRSTATUS.PWRSTGFX is set (domain powered). */
int tiku_gpu_powered(void);

/** @brief IDREG (fixed GPU ID). */
uint32_t tiku_gpu_id(void);

/** @brief Raw STATUS register (per-stage busy bits). */
uint32_t tiku_gpu_status(void);

/** @brief SYSCLEAR reset pulse. */
void tiku_gpu_reset(void);

/** @brief Spin (bounded) until STATUS busy bits clear. */
tiku_gpu_err_t tiku_gpu_wait_idle(void);

/** @brief Fill a forensics snapshot (must be powered). */
void tiku_gpu_bringup_info(tiku_gpu_bringup_t *out);

/**
 * @brief Times the GPU ISR has fired since init (P0 plumbing counter).
 */
uint32_t tiku_gpu_irq_count(void);

/**
 * @brief CPU-side IRQ plumbing self-test: NVIC-pend GPU_IRQn.
 *
 * Proves the vector slot, the strong-symbol override, and the ISR path with
 * ZERO GPU cooperation and no storm risk (a software-pended NVIC line is
 * one-shot -- there is no asserted hardware line to re-trigger). tiku_gpu_init
 * must have run (NVIC line enabled).
 */
void tiku_gpu_irq_selftest_pend(void);

/*---------------------------------------------------------------------------*/
/* P1: drawing                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill an entire RGBA8888 surface with a solid color (RECT raster +
 *        the constant-color pico-shader loaded at init).
 *
 * @p dst MUST live in SSRAM (GPU-visible; never DTCM). The driver handles the
 * D-cache discipline (clean+invalidate before the kick so no dirty CPU line is
 * evicted onto the GPU's output; invalidate after so the CPU reads fresh
 * pixels). Waits (bounded) for the raster to go idle.
 *
 * @param dst           Destination surface base (SSRAM, 32-byte aligned).
 * @param w             Width in pixels.
 * @param h             Height in pixels.
 * @param stride_bytes  Bytes per row (>= w*4).
 * @param color         Fill color word (channel order TBD -- P1 characterizes it).
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if the raster never went idle.
 */
tiku_gpu_err_t tiku_gpu_fill(void *dst, uint16_t w, uint16_t h,
                             uint16_t stride_bytes, uint32_t color);

/** @brief Raw STATUS after the last op (diagnostics during bring-up). */
uint32_t tiku_gpu_last_status(void);

/*---------------------------------------------------------------------------*/
/* P2: blit + blend                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief GPU pixel formats (hardware IMGFMT codes, TEXnSTRIDE[31:24]).
 *
 * These are the Nema texture-unit format codes (not the NemaDC scanout codes,
 * which differ). RGBA8888 = 1 is the code proven by the P1 fill.
 */
typedef enum {
    TIKU_GPU_FMT_RGBA8888 = 0x01,
    TIKU_GPU_FMT_RGB565   = 0x04,
    TIKU_GPU_FMT_RGB24    = 0x3C,
} tiku_gpu_fmt_t;

/** @brief Source-texture sampling (TEXnSTRIDE[23:16] IMGMODE). */
typedef enum {
    TIKU_GPU_SAMPLE_POINT    = 0,   /**< nearest-neighbour                    */
    TIKU_GPU_SAMPLE_BILINEAR = 1,   /**< bilinear filter (for scaled blits)   */
} tiku_gpu_sampling_t;

/**
 * @brief ROP blend modes (src_factor | dst_factor<<8, from nema_blender.h).
 *
 * The fragment shader samples the source; the fixed-function ROP blender
 * (present on this silicon, CONFIG bit28) combines it with the destination
 * per these factors.
 */
typedef enum {
    TIKU_GPU_BLEND_CLEAR    = 0x0000,  /**< 0 (erase)                         */
    TIKU_GPU_BLEND_SRC      = 0x0001,  /**< opaque copy: Sa                   */
    TIKU_GPU_BLEND_SRC_OVER = 0x0501,  /**< Sa + Da*(1-Sa)                    */
    TIKU_GPU_BLEND_SIMPLE   = 0x0504,  /**< Sa*Sa + Da*(1-Sa) (Nema blit dflt)*/
    TIKU_GPU_BLEND_ADD      = 0x0101,  /**< Sa + Da (saturating)              */
} tiku_gpu_blend_t;

/**
 * @brief A 2D surface in GPU-visible memory (SSRAM).
 *
 * Used as blit source and destination. @p base MUST be in SSRAM (never
 * DTCM/ITCM). @p sampling applies only when the surface is a scaled-blit
 * source; it is ignored for destinations and 1:1 blits.
 */
typedef struct {
    void    *base;       /**< surface base (SSRAM, 32-byte aligned)           */
    uint16_t w;          /**< width in pixels                                 */
    uint16_t h;          /**< height in pixels                                */
    uint16_t stride;     /**< bytes per row                                   */
    uint8_t  format;     /**< tiku_gpu_fmt_t                                  */
    uint8_t  sampling;   /**< tiku_gpu_sampling_t (source only)               */
} tiku_gpu_surface_t;

/**
 * @brief 1:1 blit: copy @p src onto @p dst at (@p dx, @p dy), blended.
 *
 * Binds @p dst as TEX0 and @p src as TEX1, loads the texture-sampling
 * fragment shader, programs the ROP blender for @p blend, and rasters a
 * @p src->w × @p src->h rectangle. Handles D-cache discipline for both
 * surfaces (clean the source, clean+invalidate the destination). Blocking.
 *
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if the raster never went idle.
 */
tiku_gpu_err_t tiku_gpu_blit(const tiku_gpu_surface_t *dst,
                             const tiku_gpu_surface_t *src,
                             int16_t dx, int16_t dy,
                             tiku_gpu_blend_t blend);

/**
 * @brief Scaled blit: fit @p src into the @p dw × @p dh dest rect at (dx,dy).
 *
 * As tiku_gpu_blit but loads a scale matrix into the MatMul so the source is
 * resampled to the destination rectangle (use TIKU_GPU_SAMPLE_BILINEAR on
 * @p src for smooth scaling).
 */
tiku_gpu_err_t tiku_gpu_blit_rect(const tiku_gpu_surface_t *dst,
                                  const tiku_gpu_surface_t *src,
                                  int16_t dx, int16_t dy,
                                  uint16_t dw, uint16_t dh,
                                  tiku_gpu_blend_t blend);

/*---------------------------------------------------------------------------*/
/* P2.3: raster primitives + gradient                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill a solid-color triangle from three vertices (16.16 rasterized).
 *
 * Reuses the constant-color shader; the hardware derives the edges from the
 * vertices. @p dst is the destination surface; the driver handles cache.
 */
tiku_gpu_err_t tiku_gpu_fill_triangle(const tiku_gpu_surface_t *dst,
                                      int16_t x0, int16_t y0,
                                      int16_t x1, int16_t y1,
                                      int16_t x2, int16_t y2, uint32_t color);

/** @brief Draw a single-pixel-wide line (x0,y0)->(x1,y1) in a solid color. */
tiku_gpu_err_t tiku_gpu_draw_line(const tiku_gpu_surface_t *dst,
                                  int16_t x0, int16_t y0,
                                  int16_t x1, int16_t y1, uint32_t color);

/**
 * @brief Fill a rectangle with a linear color gradient (color_a -> color_b).
 *
 * @p vertical selects the gradient axis (0 = left->right, non-zero =
 * top->bottom). Uses the RGBA interpolators; the rasterizer produces the
 * per-pixel color. All four channels interpolate, so pass matching alpha in
 * @p color_a / @p color_b for a constant-alpha gradient.
 */
tiku_gpu_err_t tiku_gpu_fill_gradient(const tiku_gpu_surface_t *dst,
                                      int16_t x, int16_t y, uint16_t w, uint16_t h,
                                      uint32_t color_a, uint32_t color_b,
                                      int vertical);

/**
 * @brief GPU interrupt handler (IRQ 28).
 *
 * Strong override of the weak alias declared in tiku_crt_early.c. Present only
 * when this TU is compiled (TIKU_DRV_GPU_ENABLE); otherwise the weak alias
 * keeps slot 28 pointing at the default handler and the image is byte-stable.
 */
void tiku_ambiq_gpu_isr(void);

#endif /* TIKU_AMBIQ_GPU_ARCH_H_ */
