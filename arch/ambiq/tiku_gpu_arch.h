/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_arch.h - Apollo510 2.5D GPU (Nema-class) driver.
 *
 * The GPU is a non-coherent bus master: each call cleans the D-cache over the
 * surfaces the GPU reads and invalidates the ones it writes.  Every buffer the
 * GPU touches must be in SSRAM; the GPU cannot reach DTCM or ITCM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_GPU_ARCH_H_
#define TIKU_AMBIQ_GPU_ARCH_H_

#include <stdint.h>

/** Driver result codes. */
typedef enum {
    TIKU_GPU_OK          =  0,
    TIKU_GPU_ERR_POWER   = -1,   /**< GFX power-up failed or HP3 lacks buck */
    TIKU_GPU_ERR_TIMEOUT = -2,   /**< not idle in time, or CL buffer full   */
    TIKU_GPU_ERR_ID      = -3,   /**< IDREG read 0 or all ones              */
    TIKU_GPU_ERR_PARAM   = -4,   /**< invalid argument (non-power-of-two)   */
} tiku_gpu_err_t;

/**
 * @brief GFX performance modes, written to PWRCTRL->GFXPERFREQ.
 *
 * LP and HP1 run from HFRC, HP2 and HP3 from HFRC2.  Only HP3 moves the GFX
 * domain onto the VDDF rail; it is the mode the SDK's HIGH_PERFORMANCE names.
 */
typedef enum {
    TIKU_GPU_PERF_LP_96MHZ   = 0,   /**< HFRC   96 MHz, VDDC                */
    TIKU_GPU_PERF_HP1_192MHZ = 1,   /**< HFRC  192 MHz, VDDC                */
    TIKU_GPU_PERF_HP2_125MHZ = 2,   /**< HFRC2 125 MHz, VDDC                */
    TIKU_GPU_PERF_HP3_250MHZ = 3,   /**< HFRC2 250 MHz, VDDF; needs SIMOBUCK */
} tiku_gpu_perf_t;

/**
 * @brief 1 if @p perf needs the VDDF rail, and so the SIMO buck; else 0.
 *
 * Only HP3 does, as in the SDK's am_hal_pwrctrl_gpu_mode_select(); HP1 and
 * HP2 stay on VDDC.
 */
int tiku_gpu_perf_needs_vddf(tiku_gpu_perf_t perf);

/** @brief Nominal GFX clock in Hz for @p perf; 96 MHz for an unknown value. */
unsigned long tiku_gpu_perf_hz(tiku_gpu_perf_t perf);

/** @brief Current PWRCTRL GFXPERFREQ field. */
uint32_t tiku_gpu_perf_get(void);
/** @brief 1 if PWRCTRL GFXPWRSWSEL puts the GFX domain on VDDF, else 0. */
int      tiku_gpu_rail_is_vddf(void);

/**
 * @brief GPU identity and control registers, as tiku_gpu_bringup_info()
 *        reads them.
 *
 * Read right after tiku_gpu_init(), they are the post-reset values.
 */
typedef struct {
    uint32_t id;        /**< IDREG   (fixed GPU ID; nonzero when alive)  */
    uint32_t status;    /**< STATUS  (per-stage busy bits)               */
    uint32_t busctrl;   /**< BUSCTRL                                     */
    uint32_t loadctrl;  /**< LOADCTRL                                    */
    uint32_t cgctrl;    /**< CGCTRL  (clock-gate disables)               */
    uint32_t active;    /**< ACTIVE  (GPUACTIVE / GPUQACTIVE)            */
} tiku_gpu_bringup_t;

/**
 * @brief Power the GFX domain up in mode @p perf, reset the GPU, enable IRQ 28.
 *
 * Powers the domain down if it is up, selects the rail and the mode, powers
 * it up, resets the core, checks IDREG, loads the fill shader and enables the
 * NVIC line.  Every wait is bounded.
 *
 * @note Datasheet 4.3.2: the GFX mode may change only with the domain off,
 *       and GFXPWRSWSEL.GFXVDDSEL is set before GFXPERFREQ.
 * @param perf  Performance mode.  HP3 needs the SIMO buck active, or the call
 *              returns TIKU_GPU_ERR_POWER without touching the domain.
 * @return TIKU_GPU_OK, TIKU_GPU_ERR_POWER, or TIKU_GPU_ERR_TIMEOUT /
 *         TIKU_GPU_ERR_ID with the domain left powered.
 */
tiku_gpu_err_t tiku_gpu_init(tiku_gpu_perf_t perf);

/**
 * @brief Disable the NVIC line and power the GFX domain off.
 *
 * A powered GFX domain draws current while idle and the driver has no idle
 * timeout, so the caller powers it off after each batch of work.  The wait
 * for the domain to drop is bounded and its outcome is not reported.
 */
void tiku_gpu_deinit(void);

/** @brief 1 if DEVPWRSTATUS.PWRSTGFX is set (domain powered). */
int tiku_gpu_powered(void);

/** @brief IDREG (fixed GPU ID). */
uint32_t tiku_gpu_id(void);

/** @brief Raw STATUS register (per-stage busy bits). */
uint32_t tiku_gpu_status(void);

/** @brief Reset the GPU core by writing SYSCLEAR. */
void tiku_gpu_reset(void);

/** @brief Spin (bounded) until STATUS busy bits clear. */
tiku_gpu_err_t tiku_gpu_wait_idle(void);

/**
 * @brief Fill @p out with the registers of tiku_gpu_bringup_t; NULL is
 *        ignored.
 *
 * @note The GFX domain must be powered.
 */
void tiku_gpu_bringup_info(tiku_gpu_bringup_t *out);

/**
 * @brief Times the GPU ISR has run since the last tiku_gpu_init().
 */
uint32_t tiku_gpu_irq_count(void);

/**
 * @brief Set GPU_IRQn pending in the NVIC, so the ISR runs once.
 *
 * Exercises the vector slot and the ISR with no GPU involvement; a
 * software-pended line fires once.  tiku_gpu_irq_count() then rises by one.
 *
 * @note tiku_gpu_init() must have enabled the NVIC line.
 */
void tiku_gpu_irq_selftest_pend(void);

/*---------------------------------------------------------------------------*/
/* DRAWING                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill a whole RGBA8888 surface with a solid color.
 *
 * One RECT draw with the constant-color shader loaded at init; waits (bounded)
 * for the GPU to go idle.  The D-cache over the surface is cleaned and
 * invalidated before the draw and invalidated after it.
 *
 * @param dst           Destination surface base (SSRAM, 32-byte aligned).
 * @param w             Width in pixels.
 * @param h             Height in pixels.
 * @param stride_bytes  Bytes per row (>= w*4).
 * @param color         Fill color word, written to DRAWCOLOR unchanged.
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if the raster never went idle.
 */
tiku_gpu_err_t tiku_gpu_fill(void *dst, uint16_t w, uint16_t h,
                             uint16_t stride_bytes, uint32_t color);

/** @brief STATUS as read after the last drawing call. */
uint32_t tiku_gpu_last_status(void);

/*---------------------------------------------------------------------------*/
/* BLIT AND BLEND                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief GPU pixel formats (hardware IMGFMT codes, TEXnSTRIDE[31:24]).
 *
 * These are the Nema texture-unit format codes; the NemaDC scanout codes
 * differ.
 */
typedef enum {
    TIKU_GPU_FMT_RGBA8888 = 0x01,
    TIKU_GPU_FMT_RGB565   = 0x04,
    TIKU_GPU_FMT_L8       = 0x09,   /**< 8-bit luminance (1 B/px)             */
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
    TIKU_GPU_BLEND_MULTIPLY = 0x0008,  /**< Sc * Dc (element-wise product)    */
    TIKU_GPU_BLEND_AVG      = 0x0A0A,  /**< Cc*Sc + Cc*Dc: 50/50 avg (Cc=0x80)*/
} tiku_gpu_blend_t;

/**
 * @brief A 2D surface in GPU-visible memory (SSRAM).
 *
 * Used as blit source and destination.  @p base must be in SSRAM, not DTCM or
 * ITCM.  @p sampling is read only when the surface is a blit source.
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
 * Binds @p dst as TEX0 and @p src as TEX1, loads a translate matrix, sets the
 * ROP blender to @p blend and draws a @p src->w x @p src->h rectangle clipped
 * to @p dst.  Cleans the D-cache over both surfaces first.  Blocking.
 *
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if the raster never went idle.
 */
tiku_gpu_err_t tiku_gpu_blit(const tiku_gpu_surface_t *dst,
                             const tiku_gpu_surface_t *src,
                             int16_t dx, int16_t dy,
                             tiku_gpu_blend_t blend);

/**
 * @brief Scaled blit: fit @p src into the @p dw x @p dh dest rect at (dx,dy).
 *
 * As tiku_gpu_blit but with a scale matrix, so the source is resampled to the
 * destination rectangle (TIKU_GPU_SAMPLE_BILINEAR on @p src for smooth
 * scaling).
 *
 * @note A source larger than the rectangle needs a scale factor above 1,
 *       which the MatMul converts with lost precision: the sampled source
 *       coordinates are then inconsistent.
 */
tiku_gpu_err_t tiku_gpu_blit_rect(const tiku_gpu_surface_t *dst,
                                  const tiku_gpu_surface_t *src,
                                  int16_t dx, int16_t dy,
                                  uint16_t dw, uint16_t dh,
                                  tiku_gpu_blend_t blend);

/*---------------------------------------------------------------------------*/
/* RASTER PRIMITIVES AND GRADIENT                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fill a solid-color triangle from three vertices.
 *
 * The vertices go to the 16.16 vertex registers and the hardware derives the
 * edges; the draw is clipped to @p dst.
 */
tiku_gpu_err_t tiku_gpu_fill_triangle(const tiku_gpu_surface_t *dst,
                                      int16_t x0, int16_t y0,
                                      int16_t x1, int16_t y1,
                                      int16_t x2, int16_t y2, uint32_t color);

/** @brief Fill a positioned rectangle (x,y,w,h) with a solid color; clipped
 *         to the destination surface. */
tiku_gpu_err_t tiku_gpu_fill_rect(const tiku_gpu_surface_t *dst,
                                  int16_t x, int16_t y,
                                  uint16_t w, uint16_t h, uint32_t color);

/** @brief Draw a single-pixel-wide line (x0,y0)->(x1,y1) in a solid color. */
tiku_gpu_err_t tiku_gpu_draw_line(const tiku_gpu_surface_t *dst,
                                  int16_t x0, int16_t y0,
                                  int16_t x1, int16_t y1, uint32_t color);

/**
 * @brief Fill a rectangle with a linear color gradient (color_a -> color_b).
 *
 * @p vertical 0 runs the gradient left to right, non-zero top to bottom.
 * Colors are 0xAABBGGRR and all four channels interpolate, so equal alpha in
 * @p color_a and @p color_b gives a constant-alpha gradient.
 */
tiku_gpu_err_t tiku_gpu_fill_gradient(const tiku_gpu_surface_t *dst,
                                      int16_t x, int16_t y, uint16_t w, uint16_t h,
                                      uint32_t color_a, uint32_t color_b,
                                      int vertical);

/**
 * @brief Fill a rounded rectangle (corner radius @p r) with a solid color.
 *
 * Exact per-pixel (a center band + circle-equation corner spans), clipped to
 * @p dst. @p r is clamped to w/2 and h/2; r == 0 degenerates to a plain rect.
 */
tiku_gpu_err_t tiku_gpu_fill_rounded_rect(const tiku_gpu_surface_t *dst,
                                          int16_t x, int16_t y,
                                          uint16_t w, uint16_t h,
                                          uint16_t r, uint32_t color);

/**
 * @brief Fill a circle of radius @p r centred at (@p cx, @p cy), solid color.
 *
 * Drawn as a 2r x 2r rounded rectangle with corner radius r, clipped to
 * @p dst.
 */
tiku_gpu_err_t tiku_gpu_fill_circle(const tiku_gpu_surface_t *dst,
                                    int16_t cx, int16_t cy,
                                    uint16_t r, uint32_t color);

/*---------------------------------------------------------------------------*/
/* ASYNC COMMAND LISTS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief A GPU command list: (register offset, value) word pairs in SSRAM.
 *
 * Built with tiku_gpu_cl_fill(), started with tiku_gpu_submit() and awaited,
 * CPU asleep, with tiku_gpu_wait().  A fill takes 24 words and the submit
 * tail 4, so N fills need 24*N + 4 words and raise one completion IRQ.
 */
typedef struct {
    uint32_t *buf;         /**< command buffer (SSRAM, 32-byte aligned)       */
    uint32_t  cap_words;   /**< capacity in 32-bit words                      */
    uint32_t  n_words;     /**< words written so far                          */
    int32_t   id;          /**< submission id assigned at submit              */
    void     *flush_base;  /**< destination to invalidate after completion    */
    uint32_t  flush_span;  /**< bytes of @p flush_base to invalidate          */
} tiku_gpu_cl_t;

/** @brief Bind a caller-provided SSRAM buffer as an (empty) command list. */
void tiku_gpu_cl_init(tiku_gpu_cl_t *cl, void *buf, uint32_t cap_words);

/** @brief Rewind a command list to empty (keeps the buffer). */
void tiku_gpu_cl_reset(tiku_gpu_cl_t *cl);

/**
 * @brief Append a solid-fill draw of @p dst to the command list.
 *
 * Records the destination so tiku_gpu_wait() can invalidate it on completion.
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if the buffer is too small.
 * @note Only the last appended destination gets cache maintenance.
 */
tiku_gpu_err_t tiku_gpu_cl_fill(tiku_gpu_cl_t *cl,
                                const tiku_gpu_surface_t *dst, uint32_t color);

/**
 * @brief Submit a command list to the GPU and return immediately (async).
 *
 * Appends a completion tail (stamp CLID, raise IRQ 28), cleans the list and
 * destination from the D-cache, and kicks the command-list processor
 * (CMDLISTADDR + CMDLISTSIZE).
 *
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if fewer than 4 words remain.
 * @note A capacity failure leaves the list and the GPU unchanged.
 */
tiku_gpu_err_t tiku_gpu_submit(tiku_gpu_cl_t *cl);

/**
 * @brief Wait for a submitted command list to complete, CPU asleep (__WFI).
 *
 * Sleeps until the completion IRQ sets the done flag, then invalidates the
 * destination; gives up after 9 wakes that find the GPU idle, flag clear.
 * @return TIKU_GPU_OK, or TIKU_GPU_ERR_TIMEOUT if completion was not signalled
 */
tiku_gpu_err_t tiku_gpu_wait(tiku_gpu_cl_t *cl);

/** @brief Id of the most recently completed command list (set by the ISR). */
int32_t tiku_gpu_last_cl_id(void);

/*---------------------------------------------------------------------------*/
/* COMPUTE                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Format-converting 2D copy: @p src (any format) -> @p dst (any format).
 *
 * The texture unit decodes the source to RGBA and the output stage repacks to
 * the destination format, so RGBA8888 -> RGB565 or -> L8 is one blit pass with
 * no CPU per-pixel work.  Dimensions must match; same-format is a plain copy.
 */
tiku_gpu_err_t tiku_gpu_convert(const tiku_gpu_surface_t *dst,
                                const tiku_gpu_surface_t *src);

/**
 * @brief Bilinear resample @p src into @p dst at @p dst's size (scale gather).
 *
 * Up- or down-samples a 2D grid in one pass using the texture unit's
 * bilinear interpolation.
 *
 * @note Downsampling has the scale-factor hazard of tiku_gpu_blit_rect().
 */
tiku_gpu_err_t tiku_gpu_resample(const tiku_gpu_surface_t *dst,
                                 const tiku_gpu_surface_t *src);

/**
 * @brief Positioned strided 2D copy: the (@p w x @p h) window of @p src at
 *        (@p sx, @p sy) copied to @p dst at (@p dx, @p dy).
 *
 * The general-purpose 2D memcpy between arbitrary surface windows (clipped to
 * @p dst); with differing formats it converts on the way.
 */
tiku_gpu_err_t tiku_gpu_copy_rect(const tiku_gpu_surface_t *dst,
                                  int16_t dx, int16_t dy,
                                  const tiku_gpu_surface_t *src,
                                  int16_t sx, int16_t sy,
                                  uint16_t w, uint16_t h);

/**
 * @brief Element-wise product: dst = src * dst / 255, per channel.
 *
 * Uses the ROP DESTCOLOR source factor: per-pixel masking, windowing and gain
 * maps.  Same dimensions required.
 */
tiku_gpu_err_t tiku_gpu_multiply(const tiku_gpu_surface_t *dst,
                                 const tiku_gpu_surface_t *src);

/**
 * @brief Constant scale: dst = src * @p factor / 255, per channel.
 *
 * Uses the ROP CONSTCOLOR source factor with @p factor in the const-color
 * register.  @p factor packs 0x00BBGGRR (0x80 = x0.502).
 */
tiku_gpu_err_t tiku_gpu_scale_const(const tiku_gpu_surface_t *dst,
                                    const tiku_gpu_surface_t *src,
                                    uint32_t factor);

/**
 * @brief Indexed-color LUT: dst[x,y] = palette[ index[x,y] ] in one call.
 *
 * @p index is L8, @p palette 256 RGBA8888 entries, @p dst RGBA8888 of the
 * index's size.  The hardware reads the palette nibble-swapped, so a shadow
 * copy reorders it; the draw runs twice, as the first after init missamples.
 *
 * @note Power-cycles and reinitializes the GPU at its current performance
 *       setting after the draw. The call includes this recovery cost.
 */
tiku_gpu_err_t tiku_gpu_lut_apply(const tiku_gpu_surface_t *dst,
                                  const tiku_gpu_surface_t *index,
                                  const uint32_t *palette);

/**
 * @brief Affine: dst = @p scale * src / 255 + @p bias, per channel,
 *        saturating to [0,255].
 *
 * Two ROP passes: fill dst with @p bias, then blend the source with the
 * CONSTCOLOR source factor over a destination factor of one.  Both surfaces
 * share dimensions, and @p dst is RGBA8888 (the bias pass uses tiku_gpu_fill).
 *
 * @param scale  packed 0x00BBGGRR (0x80 = x0.502), as tiku_gpu_scale_const
 * @param bias   packed constant in the destination's channel order (like a fill
 *               color)
 */
tiku_gpu_err_t tiku_gpu_scale_bias(const tiku_gpu_surface_t *dst,
                                   const tiku_gpu_surface_t *src,
                                   uint32_t scale, uint32_t bias);

/**
 * @brief Pairwise 50/50 average: dst = (dst + src)/2, per channel (saturating).
 *
 * The ROP CONSTCOLOR factor (const-color 0x80 = x0.502) applies to both source
 * and destination: out = 0.502*src + 0.502*dst.  A two-surface cross-fade, and
 * the fold primitive behind tiku_gpu_reduce_mean.
 *
 * @note Same dimensions required.
 */
tiku_gpu_err_t tiku_gpu_avg(const tiku_gpu_surface_t *dst,
                            const tiku_gpu_surface_t *src);

/**
 * @brief Reduce a surface to the mean of all its pixels, per channel.
 *
 * Averages the right half onto the left until one column remains, then the
 * bottom half onto the top, each fold a 1:1 blit with TIKU_GPU_BLEND_AVG.
 * Pixel (0,0) ends up holding the mean.
 *
 * @param surf      power-of-two width and height; the folds overwrite it
 * @param out_mean  the surviving pixel, the equal-weight grand mean (RGBA8888);
 *                  may be NULL
 * @return TIKU_GPU_OK, TIKU_GPU_ERR_PARAM for a zero or non-power-of-two
 *         dimension, or TIKU_GPU_ERR_TIMEOUT from a fold that never went idle.
 */
tiku_gpu_err_t tiku_gpu_reduce_mean(const tiku_gpu_surface_t *surf,
                                    uint32_t *out_mean);

/**
 * @brief GPU interrupt handler (IRQ 28).
 *
 * Records the finished list's CLID, acknowledges INTERRUPTCTRL and sets the
 * flag tiku_gpu_wait() sleeps on.  Overrides the weak alias in
 * tiku_crt_early.c; without TIKU_DRV_GPU_ENABLE slot 28 keeps the default.
 */
void tiku_ambiq_gpu_isr(void);

#endif /* TIKU_AMBIQ_GPU_ARCH_H_ */
