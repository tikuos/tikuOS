/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_arch.c - Apollo510 2.5D GPU (Think Silicon / Nema-class) driver.
 *
 * From-scratch, register-level. No AmbiqSuite, no NemaGFX. The register map is
 * the vendored CMSIS GPU_Type (apollo510.h); this file drives it directly.
 *
 * PHASE P0: bring-up -- power the GFX domain, select the clock, reset via
 * SYSCLEAR, read IDREG, capture reset-value forensics, and prove the IRQ 28
 * plumbing (vector slot + strong ISR + counter) via a CPU-side NVIC pend that
 * needs no GPU cooperation.
 *
 * PHASE P1 (this milestone): synchronous solid fill. The pipeline is
 * programmable -- a draw emits no fragments without a fragment "pico-shader"
 * in instruction memory and DRAWCODEPTR pointing at it. Init parks the stock
 * 8-byte constant-color instruction at IMEM slot 31 (recovered from the
 * vendored, MIT-granted ThinkSi port layer -- see temp/gpuplan.md); fills
 * point DRAWCODEPTR there with the ROP blender in SRC mode. Command lists,
 * blits, compute and async completion arrive in later phases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpu_arch.h"
#include "apollo510.h"           /* CMSIS: GPU / PWRCTRL / NVIC (no AmbiqSuite) */
#include <hal/tiku_cpu.h>        /* tiku_cpu_dcache_clean / _invalidate       */

/*---------------------------------------------------------------------------*/
/* Constants                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Bounded spin cap for every busy-wait. Power domains settle in microseconds
 * and a SYSCLEAR is near-instant, so ~1e6 iterations (tens of ms at worst) is
 * enormous headroom -- large enough never to false-trip, small enough that a
 * dead/misaddressed block fails closed instead of wedging the bench runner.
 */
#define GPU_SPIN_MAX   1000000u

/*
 * STATUS busy mask -- OR of every documented per-stage busy field:
 *   SYSBSY(31) MEMBSY(30) CLBSY(29) CLPBSY(28) RASTBSY(27:24)
 *   DEPTHFIFOBSY(19:16) RENDERBSY(15:12) TEXTMAPBSY(11:8) PIPEBSY(7:4) COREBSY(3:0)
 * "Idle" == none of these set. The RASTBSY nibble (27:24) MUST be included --
 * every draw is a raster op, so omitting it lets wait_idle return mid-drain.
 */
#define GPU_STATUS_BUSY_MASK  0xFF0FFFFFu

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/** ISR fire counter (volatile: written by tiku_ambiq_gpu_isr, read by tests). */
static volatile uint32_t s_irq_count;

/** STATUS captured after the last drawing op (bring-up diagnostics). */
static uint32_t s_last_status;

/** Async command-list completion: flag set by the ISR, id of the finished CL. */
static volatile uint32_t s_cl_done;
static volatile int32_t  s_cl_last_id;

/** Monotonic command-list submission id. */
static int32_t s_cl_seq;

/*---------------------------------------------------------------------------*/
/* Register field encodings (from the vendored GPU_Type field defs)          */
/*---------------------------------------------------------------------------*/

/* TEXnSTRIDE: IMGSTRD[15:0] | IMGMODE[23:16] | IMGFMT[31:24]. */
#define GPU_IMGFMT_RGBA8888   1u     /* IMGFMT enum: RGBA8888                 */
#define GPU_IMGMODE_POINT     0u     /* IMGMODE enum: nearest-neighbour       */

/* DRAWCMD primitive codes. NOT a sequential enum -- a bitfield recovered from
 * the raster disassembly: LINE=bit0, RECT=bit1, TRI=bit2, QUAD=bit0|bit2. */
#define GPU_DRAWCMD_LINE      1u
#define GPU_DRAWCMD_RECT      2u     /* "fill rectangle from STARTXY to ENDXY" */
#define GPU_DRAWCMD_TRI       4u     /* triangle from the three 16.16 vertices */

/* DRAWCMD flag: interpolate the rasterizer color across the primitive from the
 * RGBA interpolator registers instead of the flat DRAW_COLOR (draw_flags b27). */
#define GPU_DRAWFLAG_GRADIENT 0x08000000u

/* RASTCTRL (a.k.a. the matrix-multiplier control): a solid fill uses no
 * coordinate transform, so the MatMul is bypassed. Bit names from the vendored
 * ThinkSi programming layer (nema_programHW.h, MIT grant):
 * MMUL_BYPASS = bit28, MMUL_NONPERSP = bit31. */
#define GPU_RASTCTRL_MMUL_BYPASS   ((1u << 28) | (1u << 31))

/*
 * Registers that sit in RESERVED gaps of the CMSIS GPU_Type but are named in
 * the vendored (MIT-granted) ThinkSi nema_regs.h. Written by raw offset.
 */
#define GPU_REG_ROPBLEND_MODE  0x1D0u  /* NEMA_ROPBLENDER_BLEND_MODE          */
#define GPU_REG_RAST_BYPASS    0x388u  /* NEMA_RAST_BYPASS                    */

#define GPU_ROPBLEND_SRC       1u      /* NEMA_BL_SRC: replace destination    */

/*
 * The fragment "pico-shader" for constant-color output, and where it lives.
 *
 * The Nema pipeline emits no fragments unless DRAWCODEPTR points at a shader
 * in instruction memory -- even a solid fill. The stock programming layer
 * parks one 64-bit "output the draw color" instruction at IMEM slot 31 during
 * init and leaves slots 24..30 zeroed; every ROP-blended draw then points
 * DRAWCODEPTR at slot 31 for both foreground and background. These are the
 * 8 bytes of microcode data (plus the derived pointer word) that init loads;
 * they ship as plain init-time constants in the ThinkSi port layer, recovered
 * from the vendored port under temp/.../ThinkSi (see temp/gpuplan.md), not
 * linked from any library.
 */
#define GPU_SHADER_SLOT_FIRST  24u          /* zeroed slots 24..30            */
#define GPU_SHADER_SLOT_FILL   31u          /* constant-color instruction     */
#define GPU_SHADER_FILL_LO     0x08000002u  /* instruction bits 31:0          */
#define GPU_SHADER_FILL_HI     0x80000009u  /* instruction bits 63:32         */

/* DRAWCODEPTR word for a ROP-blended fill: FRGND[15:0] and BKGND[31:16] both
 * carry slot 31 plus the stock flag bits (0x141F001F verbatim from the
 * programming layer's no-texture path). */
#define GPU_CODEPTR_FILL       0x141F001Fu

/* DRAWCODEPTR for a textured blit: identical background (slot 31), but the
 * foreground field carries the texture-emit bit (0x8000) -- the SAME slot-31
 * shader then samples TEX1 and outputs the texel instead of DRAW_COLOR. So a
 * blit needs NO per-draw shader upload; it reuses the init-time IMEM. Recovered
 * from the ThinkSi blend path and confirmed by a live-silicon register capture
 * of the vendor demo (DRAWCODEPTR read back as 0x141F801F during a blit). */
#define GPU_CODEPTR_BLIT       0x141F801Fu

/* MatMul (RASTCTRL 0x118): apply the loaded 3x3 (non-bypass). A blit derives
 * source UVs by transforming the destination coordinates, so unlike the fill
 * the multiplier must stay active. */
#define GPU_RASTCTRL_MMUL_APPLY   0u

/*---------------------------------------------------------------------------*/
/* Interrupt handler                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Strong override of the weak crt_early alias. Handles the real GPU completion
 * interrupt raised by a command list's tail (INTERRUPTCTRL=1 after the HOLD'd
 * draw retires): record which CL finished (CLID, 0x148), acknowledge by
 * writing INTERRUPTCTRL=0, clear the NVIC pending bit, and signal the waiter.
 * Also serves the P0 CPU-side NVIC self-test (which asserts no GPU line -- the
 * CLID read and the ack write are harmless there).
 */
void
tiku_ambiq_gpu_isr(void)
{
    s_cl_last_id = (int32_t)(*(volatile uint32_t *)(uintptr_t)(GPU_BASE + 0x148u));
    GPU->INTERRUPTCTRL = 0u;             /* ack: clear the GPU IRQ            */
    __DSB();
    NVIC_ClearPendingIRQ(GPU_IRQn);
    s_cl_done = 1u;
    s_irq_count++;
}

/*---------------------------------------------------------------------------*/
/* Power / clock / reset                                                     */
/*---------------------------------------------------------------------------*/

int
tiku_gpu_powered(void)
{
    return PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX ? 1 : 0;
}

void
tiku_gpu_reset(void)
{
    /* Documented: "On write, resets the GPU." Value is don't-care. */
    GPU->SYSCLEAR = 1u;
    __DSB();
    __ISB();
}

/** Write a GPU register that has no CMSIS GPU_Type member (RESERVED gap). */
static inline void
gpu_reg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(GPU_BASE + offset) = value;
}

/** Load one 64-bit pico-instruction into shader instruction memory. */
static void
gpu_imem_load(uint32_t slot, uint32_t lo, uint32_t hi)
{
    GPU->IMEMLDIADDR   = slot;
    GPU->IMEMLDIDATAHL = lo;      /* bits 31:0                                */
    GPU->IMEMLDIDATAHH = hi;      /* bits 63:32 -- this write commits the load */
}

/*
 * Post-reset processor init -- the de-SDK'd equivalent of the stock library's
 * init sequence (init_nema_regs + blender init), reduced to what our silicon
 * config (LOADCTRL/CONFIG = 0xF4030105: ROP blender present, no depth buffer)
 * actually takes:
 *   - reset the command-list processor and status,
 *   - RAST_BYPASS = 1 (rasterizer programmed via the direct DRAW* registers),
 *   - no interrupts for now (P1 is synchronous spin-wait),
 *   - park the constant-color shader at IMEM slot 31 (slots 24..30 zeroed),
 *   - ROP blend mode = SRC, shader constants C0 = 0 / C1 = ~0.
 * Without the slot-31 shader + DRAWCODEPTR the pipeline rasterizes but emits
 * ZERO fragments -- this is the exact gap that made P1 fills silently no-op.
 */
static void
gpu_processor_init(void)
{
    uint32_t slot;

    GPU->CMDLISTSTATUS = 0u;               /* reset the CL processor          */
    GPU->SYSCLEAR      = 0u;               /* status clear (stock init writes 0) */
    (void)tiku_gpu_wait_idle();

    gpu_reg_write(GPU_REG_RAST_BYPASS, 1u);
    GPU->INTERRUPTCTRL = 0u;

    for (slot = GPU_SHADER_SLOT_FIRST; slot < GPU_SHADER_SLOT_FILL; slot++) {
        gpu_imem_load(slot, 0u, 0u);
    }
    gpu_imem_load(GPU_SHADER_SLOT_FILL, GPU_SHADER_FILL_LO, GPU_SHADER_FILL_HI);

    gpu_reg_write(GPU_REG_ROPBLEND_MODE, GPU_ROPBLEND_SRC);
    GPU->C0REG = 0u;
    GPU->C1REG = 0xFFFFFFFFu;
    __DSB();
}

tiku_gpu_err_t
tiku_gpu_wait_idle(void)
{
    uint32_t spins = 0u;

    while (GPU->STATUS & GPU_STATUS_BUSY_MASK) {
        if (++spins > GPU_SPIN_MAX) {
            return TIKU_GPU_ERR_TIMEOUT;
        }
    }
    return TIKU_GPU_OK;
}

tiku_gpu_err_t
tiku_gpu_init(tiku_gpu_perf_t perf)
{
    uint32_t spins = 0u;

    /* 1. Power the GFX domain (mirror of the UART-domain enable pattern:
     *    DEVPWREN bit, then busy-wait for DEVPWRSTATUS). */
    PWRCTRL->DEVPWREN_b.PWRENGFX = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX == 0u) {
        if (++spins > GPU_SPIN_MAX) {
            return TIKU_GPU_ERR_POWER;
        }
    }

    /* 2. Select the performance/clock mode (LP 96 MHz for bring-up). */
    PWRCTRL->GFXPERFREQ_b.GFXPERFREQ = (uint32_t)perf & 0x3u;
    __DSB();

    /* 3. Reset the core, then wait for idle. */
    tiku_gpu_reset();
    if (tiku_gpu_wait_idle() != TIKU_GPU_OK) {
        /* STATUS never cleared -- likely a bad STATUS/SYSCLEAR offset
         * ("CHECK address!") or a dead core. Leave powered so the caller can
         * read forensics, and report the failure. */
        return TIKU_GPU_ERR_TIMEOUT;
    }

    /* 4. Sanity: the ID register must read a fixed nonzero value once the core
     *    is alive and clocked. Zero / all-ones smells like power or offset. */
    {
        uint32_t id = GPU->IDREG;
        if (id == 0u || id == 0xFFFFFFFFu) {
            return TIKU_GPU_ERR_ID;
        }
    }

    /* 5. Bring the drawing pipeline to a known-good state (CL processor
     *    reset, RAST_BYPASS, fill shader parked at IMEM slot 31, ROP=SRC). */
    gpu_processor_init();

    /* 6. Enable the GPU NVIC line (edge cleared on entry; real ack protocol is
     *    established with the first hardware completion in a later phase). */
    s_irq_count = 0u;
    NVIC_ClearPendingIRQ(GPU_IRQn);
    NVIC_EnableIRQ(GPU_IRQn);

    return TIKU_GPU_OK;
}

void
tiku_gpu_deinit(void)
{
    NVIC_DisableIRQ(GPU_IRQn);
    NVIC_ClearPendingIRQ(GPU_IRQn);

    PWRCTRL->DEVPWREN_b.PWRENGFX = 0u;
    /* Best-effort wait for the domain to drop; bounded, never fatal. */
    {
        uint32_t spins = 0u;
        while (PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX != 0u) {
            if (++spins > GPU_SPIN_MAX) {
                break;
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Introspection                                                             */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_id(void)
{
    return GPU->IDREG;
}

uint32_t
tiku_gpu_status(void)
{
    return GPU->STATUS;
}

void
tiku_gpu_bringup_info(tiku_gpu_bringup_t *out)
{
    if (out == (tiku_gpu_bringup_t *)0) {
        return;
    }
    out->id       = GPU->IDREG;
    out->status   = GPU->STATUS;
    out->busctrl  = GPU->BUSCTRL;
    out->loadctrl = GPU->LOADCTRL;
    out->cgctrl   = GPU->CGCTRL;
    out->active   = GPU->ACTIVE;
}

/*---------------------------------------------------------------------------*/
/* IRQ plumbing self-test                                                    */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_irq_count(void)
{
    return s_irq_count;
}

/*---------------------------------------------------------------------------*/
/* P1: drawing                                                               */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_last_status(void)
{
    return s_last_status;
}

tiku_gpu_err_t
tiku_gpu_fill(void *dst, uint16_t w, uint16_t h,
              uint16_t stride_bytes, uint32_t color)
{
    uintptr_t base = (uintptr_t)dst;
    uint32_t  span = (uint32_t)stride_bytes * (uint32_t)h;
    tiku_gpu_err_t err;

    /* Destination cache hygiene BEFORE the kick: clean writes back any dirty
     * lines (so nothing the CPU touched -- canary init, a prior fill -- can be
     * evicted onto the GPU's output mid-render), then invalidate drops the
     * lines so the GPU starts from a clean slate. The GPU is a non-coherent
     * bus master; this is mandatory, not cosmetic. */
    tiku_cpu_dcache_clean(dst, span);
    tiku_cpu_dcache_invalidate(dst, span);

    /* Bind the destination surface: TEX0 doubles as "drawing surface 0". */
    GPU->TEX0BASE   = (uint32_t)base;
    GPU->TEX0STRIDE = ((uint32_t)GPU_IMGFMT_RGBA8888 << 24) |
                      ((uint32_t)GPU_IMGMODE_POINT   << 16) |
                      ((uint32_t)stride_bytes & 0xFFFFu);
    GPU->TEX0RES    = (uint32_t)w | ((uint32_t)h << 16);

    /* Generous clip rect so a 64x64 draw cannot be clipped out regardless of
     * how CLIP packs its coordinates. No matrix transform for a solid fill. */
    GPU->CLIPMIN  = 0u;
    GPU->CLIPMAX  = 0x0FFF0FFFu;
    GPU->RASTCTRL = GPU_RASTCTRL_MMUL_BYPASS;

    /* The fragment pipeline: both shader pointers at the constant-color
     * instruction (slot 31, loaded at init), ROP blender in SRC mode, and the
     * fill color in DRAWCOLOR -- exactly what the stock fill path programs.
     * Without DRAWCODEPTR the raster runs and writes nothing (proven on
     * silicon twice before this was understood). */
    GPU->DRAWCODEPTR = GPU_CODEPTR_FILL;
    gpu_reg_write(GPU_REG_ROPBLEND_MODE, GPU_ROPBLEND_SRC);
    GPU->DRAWCOLOR   = color;

    /* Rectangle corners, packed-integer form (x | y<<16): STARTXY inclusive
     * origin, ENDXY exclusive corner -- the only two coordinate registers the
     * stock RECT path writes. */
    GPU->DRAWPT0  = 0u;                                     /* (0,0)           */
    GPU->DRAWPT1  = (uint32_t)w | ((uint32_t)h << 16);      /* (w,h)           */
    __DSB();
    GPU->DRAWCMD   = GPU_DRAWCMD_RECT;                      /* START = RECT    */

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;

    /* The GPU wrote memory behind the cache -- drop stale copies so the CPU
     * (and the CRC check) read the fresh pixels. */
    tiku_cpu_dcache_invalidate(dst, span);

    return err;
}

/*---------------------------------------------------------------------------*/
/* P2: blit + blend                                                          */
/*---------------------------------------------------------------------------*/

/*
 * IEEE-754 bit pattern of a float. The MatMul registers accept this word and
 * the hardware converts to its internal 24-bit matrix format on write --
 * verified on silicon: writing 0x3F800000 (1.0f) to MM00 reads back
 * 0x000F8000; writing -38.0f reads back exactly the value the vendor demo's
 * matrix held. So the driver writes plain IEEE-754 and lets the GPU convert.
 */
static inline uint32_t
gpu_f2u(float f)
{
    union { float f; uint32_t u; } x;
    x.f = f;
    return x.u;
}

/*
 * Load the full 3x3 screen->texture matrix and leave the MatMul active.
 * NemaGFX writes only the coefficients it changes and relies on an identity
 * default that init never programs (both extraction passes flagged this), so
 * we always write all nine -- correctness cannot depend on reset values.
 * Off-diagonals are zero; only MM00/MM02/MM11/MM12 vary between translate and
 * scale.
 */
static void
gpu_load_matrix(float m00, float m02, float m11, float m12)
{
    GPU->MM00 = gpu_f2u(m00);   GPU->MM01 = gpu_f2u(0.0f);  GPU->MM02 = gpu_f2u(m02);
    GPU->MM10 = gpu_f2u(0.0f);  GPU->MM11 = gpu_f2u(m11);   GPU->MM12 = gpu_f2u(m12);
    GPU->MM20 = gpu_f2u(0.0f);  GPU->MM21 = gpu_f2u(0.0f);  GPU->MM22 = gpu_f2u(1.0f);
    GPU->RASTCTRL = GPU_RASTCTRL_MMUL_APPLY;   /* apply the 3x3 (non-bypass) */
}

/*
 * Common blit path. The destination rectangle is (dx,dy)..(dx+dw,dy+dh); the
 * matrix maps each destination pixel back to a source texel. For a 1:1 blit
 * dw/dh equal the source size and the matrix is a pure translate; for a scaled
 * blit dw/dh differ and the matrix carries the scale factors.
 */
static tiku_gpu_err_t
gpu_blit_core(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src,
              int16_t dx, int16_t dy, uint16_t dw, uint16_t dh,
              float m00, float m02, float m11, float m12,
              tiku_gpu_blend_t blend)
{
    uint32_t dst_span = (uint32_t)dst->stride * (uint32_t)dst->h;
    uint32_t src_span = (uint32_t)src->stride * (uint32_t)src->h;
    tiku_gpu_err_t err;

    /* Cache discipline for a non-coherent bus master: clean the source so the
     * GPU sees the CPU's texel writes; clean+invalidate the destination so no
     * dirty CPU line evicts onto the GPU output and the read-modify blend sees
     * the current background. */
    tiku_cpu_dcache_clean(src->base, src_span);
    tiku_cpu_dcache_clean(dst->base, dst_span);
    tiku_cpu_dcache_invalidate(dst->base, dst_span);

    /* Destination surface = TEX0 (never sampled -> no IMGMODE field). */
    GPU->TEX0BASE   = (uint32_t)(uintptr_t)dst->base;
    GPU->TEX0STRIDE = ((uint32_t)dst->format << 24) |
                      ((uint32_t)dst->stride & 0xFFFFu);
    GPU->TEX0RES    = (uint32_t)dst->w | ((uint32_t)dst->h << 16);

    /* Source texture = TEX1 (sampled -> IMGMODE = sampling mode at [23:16]). */
    GPU->TEX1BASE   = (uint32_t)(uintptr_t)src->base;
    GPU->TEX1STRIDE = ((uint32_t)src->format   << 24) |
                      ((uint32_t)src->sampling  << 16) |
                      ((uint32_t)src->stride & 0xFFFFu);
    GPU->TEX1RES    = (uint32_t)src->w | ((uint32_t)src->h << 16);

    /* screen->texture transform (also clears the MatMul bypass). */
    gpu_load_matrix(m00, m02, m11, m12);

    /* Scissor to the destination surface: a stray coordinate cannot scribble
     * beyond it (the draw rect bounds the write; this bounds the surface). */
    GPU->CLIPMIN = 0u;
    GPU->CLIPMAX = ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu);

    /* Fragment path: ROP blender = blend mode; DRAWCODEPTR = the slot-31
     * shader with the texture-emit bit. No shader upload -- the init-time
     * IMEM is reused. */
    gpu_reg_write(GPU_REG_ROPBLEND_MODE, (uint32_t)blend);
    GPU->DRAWCODEPTR = GPU_CODEPTR_BLIT;

    /* Destination rectangle: packed integer (y<<16)|x, end corner exclusive. */
    GPU->DRAWPT0 = ((uint32_t)(uint16_t)dy << 16) |
                   ((uint32_t)(uint16_t)dx & 0xFFFFu);
    GPU->DRAWPT1 = ((uint32_t)(uint16_t)(dy + (int16_t)dh) << 16) |
                   ((uint32_t)(uint16_t)(dx + (int16_t)dw) & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_RECT;

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;

    tiku_cpu_dcache_invalidate(dst->base, dst_span);
    return err;
}

tiku_gpu_err_t
tiku_gpu_blit(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src,
              int16_t dx, int16_t dy, tiku_gpu_blend_t blend)
{
    /* 1:1 translate: tex = screen - (dx,dy), so the dest top-left samples
     * src (0,0). Destination rect = source size. */
    return gpu_blit_core(dst, src, dx, dy, src->w, src->h,
                         1.0f, -(float)dx, 1.0f, -(float)dy, blend);
}

tiku_gpu_err_t
tiku_gpu_blit_rect(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src,
                   int16_t dx, int16_t dy, uint16_t dw, uint16_t dh,
                   tiku_gpu_blend_t blend)
{
    /* Scale so the whole source fits the dest rect: tex = S*(screen - d),
     * S = src_extent / dst_extent. */
    float sx = (float)src->w / (float)dw;
    float sy = (float)src->h / (float)dh;
    return gpu_blit_core(dst, src, dx, dy, dw, dh,
                         sx, -(float)dx * sx, sy, -(float)dy * sy, blend);
}

/*---------------------------------------------------------------------------*/
/* P2.3: raster primitives (triangle, line) + color gradient                 */
/*---------------------------------------------------------------------------*/

/** Integer pixel coordinate -> 16.16 fixed point (the vertex registers). */
static inline uint32_t
gpu_i2fx16(int32_t v)
{
    return (uint32_t)(v << 16);
}

/** Per-channel gradient slope (b-a)/n in signed 16.16 (channel scale 0..255). */
static inline int32_t
gpu_grad_slope(int32_t a, int32_t b, int32_t n)
{
    if (n <= 0) { return 0; }
    return ((b - a) << 16) / n;
}

/*
 * Common flat-draw setup: bind dst as TEX0, clip to it, and select the
 * constant-color shader with the MatMul bypassed (the fill path -- the
 * primitives all reuse it; the rasterizer, not a texture, produces the color).
 * The caller sets color/coordinates/interpolators, writes DRAWCMD last, and
 * owns the cache discipline and idle wait.
 */
static void
gpu_dst_setup(const tiku_gpu_surface_t *dst)
{
    GPU->TEX0BASE    = (uint32_t)(uintptr_t)dst->base;
    GPU->TEX0STRIDE  = ((uint32_t)dst->format << 24) | ((uint32_t)dst->stride & 0xFFFFu);
    GPU->TEX0RES     = (uint32_t)dst->w | ((uint32_t)dst->h << 16);
    GPU->CLIPMIN     = 0u;
    GPU->CLIPMAX     = ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu);
    GPU->RASTCTRL    = GPU_RASTCTRL_MMUL_BYPASS;
    GPU->DRAWCODEPTR = GPU_CODEPTR_FILL;
}

tiku_gpu_err_t
tiku_gpu_fill_triangle(const tiku_gpu_surface_t *dst,
                       int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2, uint32_t color)
{
    uint32_t span = (uint32_t)dst->stride * (uint32_t)dst->h;
    tiku_gpu_err_t err;

    tiku_cpu_dcache_clean(dst->base, span);
    tiku_cpu_dcache_invalidate(dst->base, span);

    gpu_dst_setup(dst);
    GPU->DRAWCOLOR = color;
    /* Three vertices in the 16.16 per-vertex registers; the HW derives the
     * edges (no edge-equation or depth programming for a flat 2D triangle). */
    GPU->DRAWPT0X = gpu_i2fx16(x0);  GPU->DRAWPT0Y = gpu_i2fx16(y0);
    GPU->DRAWPT1X = gpu_i2fx16(x1);  GPU->DRAWPT1Y = gpu_i2fx16(y1);
    GPU->DRAWPT2X = gpu_i2fx16(x2);  GPU->DRAWPT2Y = gpu_i2fx16(y2);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_TRI;

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, span);
    return err;
}

tiku_gpu_err_t
tiku_gpu_fill_rect(const tiku_gpu_surface_t *dst, int16_t x, int16_t y,
                   uint16_t w, uint16_t h, uint32_t color)
{
    uint32_t span = (uint32_t)dst->stride * (uint32_t)dst->h;
    tiku_gpu_err_t err;

    tiku_cpu_dcache_clean(dst->base, span);
    tiku_cpu_dcache_invalidate(dst->base, span);

    gpu_dst_setup(dst);   /* clips to the surface, so an oversized rect is safe */
    GPU->DRAWCOLOR = color;
    GPU->DRAWPT0 = ((uint32_t)(uint16_t)y << 16) | ((uint32_t)(uint16_t)x & 0xFFFFu);
    GPU->DRAWPT1 = ((uint32_t)(uint16_t)(y + (int16_t)h) << 16) |
                   ((uint32_t)(uint16_t)(x + (int16_t)w) & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_RECT;

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, span);
    return err;
}

tiku_gpu_err_t
tiku_gpu_draw_line(const tiku_gpu_surface_t *dst,
                   int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color)
{
    uint32_t span = (uint32_t)dst->stride * (uint32_t)dst->h;
    tiku_gpu_err_t err;

    tiku_cpu_dcache_clean(dst->base, span);
    tiku_cpu_dcache_invalidate(dst->base, span);

    gpu_dst_setup(dst);
    GPU->DRAWCOLOR = color;
    /* A line uses the integer STARTXY/ENDXY (not the 16.16 vertex regs);
     * single-pixel width. */
    GPU->DRAWPT0 = ((uint32_t)(uint16_t)y0 << 16) | ((uint32_t)(uint16_t)x0 & 0xFFFFu);
    GPU->DRAWPT1 = ((uint32_t)(uint16_t)y1 << 16) | ((uint32_t)(uint16_t)x1 & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_LINE;

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, span);
    return err;
}

tiku_gpu_err_t
tiku_gpu_fill_gradient(const tiku_gpu_surface_t *dst,
                       int16_t x, int16_t y, uint16_t w, uint16_t h,
                       uint32_t color_a, uint32_t color_b, int vertical)
{
    uint32_t span = (uint32_t)dst->stride * (uint32_t)dst->h;
    tiku_gpu_err_t err;

    /* Unpack channels: word = A<<24 | B<<16 | G<<8 | R. */
    int32_t ar = (int32_t)(color_a        & 0xFFu);
    int32_t ag = (int32_t)((color_a >> 8)  & 0xFFu);
    int32_t ab = (int32_t)((color_a >> 16) & 0xFFu);
    int32_t aa = (int32_t)((color_a >> 24) & 0xFFu);
    int32_t br = (int32_t)(color_b        & 0xFFu);
    int32_t bg = (int32_t)((color_b >> 8)  & 0xFFu);
    int32_t bb = (int32_t)((color_b >> 16) & 0xFFu);
    int32_t ba = (int32_t)((color_b >> 24) & 0xFFu);
    int32_t n  = vertical ? (int32_t)h : (int32_t)w;

    /* Slope along the gradient axis (16.16), zero along the other. */
    int32_t s_r = gpu_grad_slope(ar, br, n);
    int32_t s_g = gpu_grad_slope(ag, bg, n);
    int32_t s_b = gpu_grad_slope(ab, bb, n);
    int32_t s_a = gpu_grad_slope(aa, ba, n);
    int32_t dxr = vertical ? 0 : s_r, dyr = vertical ? s_r : 0;
    int32_t dxg = vertical ? 0 : s_g, dyg = vertical ? s_g : 0;
    int32_t dxb = vertical ? 0 : s_b, dyb = vertical ? s_b : 0;
    int32_t dxa = vertical ? 0 : s_a, dya = vertical ? s_a : 0;

    /* Interpolation is screen-absolute: color(px,py) = INIT + px*DX + py*DY.
     * Anchor so the rect's top-left (x,y) evaluates to color_a. */
#define GPU_GRAD_ANCHOR(c, dx, dy) \
    ((uint32_t)(((int32_t)(c) << 16) - (int32_t)x * (dx) - (int32_t)y * (dy)))

    tiku_cpu_dcache_clean(dst->base, span);
    tiku_cpu_dcache_invalidate(dst->base, span);

    gpu_dst_setup(dst);
    /* Flat-color fallback so a mis-set gradient shows as solid color_a (which
     * the test detects) rather than garbage; ignored when the gradient bit
     * makes the rasterizer use the interpolated color. */
    GPU->DRAWCOLOR = color_a;

    GPU->REDX = (uint32_t)dxr;  GPU->REDY = (uint32_t)dyr;
    GPU->GREENX = (uint32_t)dxg; GPU->GREENY = (uint32_t)dyg;
    GPU->BLUEX = (uint32_t)dxb;  GPU->BLUEY = (uint32_t)dyb;
    GPU->ALFX = (uint32_t)dxa;   GPU->ALFY = (uint32_t)dya;
    GPU->REDINIT = GPU_GRAD_ANCHOR(ar, dxr, dyr);
    GPU->GREINIT = GPU_GRAD_ANCHOR(ag, dxg, dyg);
    GPU->BLUINIT = GPU_GRAD_ANCHOR(ab, dxb, dyb);
    GPU->ALFINIT = GPU_GRAD_ANCHOR(aa, dxa, dya);

    GPU->DRAWPT0 = ((uint32_t)(uint16_t)y << 16) | ((uint32_t)(uint16_t)x & 0xFFFFu);
    GPU->DRAWPT1 = ((uint32_t)(uint16_t)(y + (int16_t)h) << 16) |
                   ((uint32_t)(uint16_t)(x + (int16_t)w) & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWFLAG_GRADIENT | GPU_DRAWCMD_RECT;

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, span);

#undef GPU_GRAD_ANCHOR
    return err;
}

/* Integer floor(sqrt(v)) -- corner-span half-widths without a libm dependency
 * in this low-level driver. */
static uint32_t
gpu_isqrt(uint32_t v)
{
    uint32_t r = 0u, bit = 1u << 30;
    while (bit > v) { bit >>= 2; }
    while (bit != 0u) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else              { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

/* Emit one axis-aligned RECT draw (integer coords). The surface, shader,
 * color and clip must already be programmed; waits for the raster to drain. */
static tiku_gpu_err_t
gpu_emit_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    GPU->DRAWPT0 = ((uint32_t)(uint16_t)y << 16) | ((uint32_t)(uint16_t)x & 0xFFFFu);
    GPU->DRAWPT1 = ((uint32_t)(uint16_t)(y + h) << 16) |
                   ((uint32_t)(uint16_t)(x + w) & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_RECT;
    return tiku_gpu_wait_idle();
}

/*
 * Filled rounded rectangle -- and, via corner radius = w/2 = h/2, a filled
 * circle (the recovered vendor identity: a circle IS a rounded rect). Exact
 * per-pixel: a full-width center band plus, for each row of the top/bottom
 * corner bands, one horizontal span whose half-width is the circle equation
 * (no trig, no AA). Surface bind + cache happen once; each span is a direct
 * RECT draw, clipped to the surface so shapes near an edge are safe.
 */
static tiku_gpu_err_t
gpu_rounded_rect(const tiku_gpu_surface_t *dst, int32_t x0, int32_t y0,
                 int32_t w, int32_t h, int32_t rr, uint32_t color)
{
    uint32_t span_bytes = (uint32_t)dst->stride * (uint32_t)dst->h;
    tiku_gpu_err_t err = TIKU_GPU_OK;
    int32_t dy;

    tiku_cpu_dcache_clean(dst->base, span_bytes);
    tiku_cpu_dcache_invalidate(dst->base, span_bytes);

    gpu_dst_setup(dst);
    GPU->DRAWCOLOR = color;

    /* Center band (full width). Zero-height for a pure circle -> skipped. */
    if (h > 2 * rr) {
        err = gpu_emit_rect(x0, y0 + rr, w, h - 2 * rr);
    }
    /* Corner bands: one horizontal span per row, mirrored top and bottom. */
    for (dy = 1; dy <= rr && err == TIKU_GPU_OK; dy++) {
        int32_t cx   = (int32_t)gpu_isqrt((uint32_t)(rr * rr - dy * dy));
        int32_t left = x0 + rr - cx;
        int32_t sw   = (w - 2 * rr) + 2 * cx;
        if (sw > 0) {
            err = gpu_emit_rect(left, y0 + rr - dy, sw, 1);
            if (err == TIKU_GPU_OK) {
                err = gpu_emit_rect(left, y0 + h - rr + dy - 1, sw, 1);
            }
        }
    }

    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, span_bytes);
    return err;
}

tiku_gpu_err_t
tiku_gpu_fill_rounded_rect(const tiku_gpu_surface_t *dst, int16_t x, int16_t y,
                           uint16_t w, uint16_t h, uint16_t r, uint32_t color)
{
    int32_t rr = (int32_t)r;

    if (rr > (int32_t)w / 2) { rr = (int32_t)w / 2; }
    if (rr > (int32_t)h / 2) { rr = (int32_t)h / 2; }
    if (rr <= 0) {
        return tiku_gpu_fill_rect(dst, x, y, w, h, color);
    }
    return gpu_rounded_rect(dst, x, y, (int32_t)w, (int32_t)h, rr, color);
}

tiku_gpu_err_t
tiku_gpu_fill_circle(const tiku_gpu_surface_t *dst, int16_t cx, int16_t cy,
                     uint16_t r, uint32_t color)
{
    return gpu_rounded_rect(dst, (int32_t)cx - (int32_t)r, (int32_t)cy - (int32_t)r,
                            2 * (int32_t)r, 2 * (int32_t)r, (int32_t)r, color);
}

/*---------------------------------------------------------------------------*/
/* P2.4: async command-list submission                                       */
/*---------------------------------------------------------------------------*/

/* Register offsets used when composing a command list as (offset,value)
 * pairs (the CL processor writes each value to GPU_BASE + offset). */
#define GPU_OFF_TEX0BASE    0x000u
#define GPU_OFF_TEX0STRIDE  0x004u
#define GPU_OFF_TEX0RES     0x008u
#define GPU_OFF_DRAWCMD     0x100u
#define GPU_OFF_STARTXY     0x104u
#define GPU_OFF_ENDXY       0x108u
#define GPU_OFF_CLIPMIN     0x110u
#define GPU_OFF_CLIPMAX     0x114u
#define GPU_OFF_RASTCTRL    0x118u
#define GPU_OFF_CODEPTR     0x11Cu
#define GPU_OFF_DRAWCOLOR   0x12Cu
#define GPU_OFF_CLID        0x148u
#define GPU_OFF_ROPBLEND    0x1D0u
#define GPU_OFF_INTCTRL     0x0F8u

/* HOLD flag OR'd into a command-list register offset: the CL processor blocks
 * until that (drawing) write retires before advancing. Mandatory on DRAWCMD so
 * the completion tail runs only after the draw is truly done. */
#define GPU_CL_HOLD         0xFF000000u

/* Wait bound: consecutive observations of an idle GPU without a completion
 * signal before tiku_gpu_wait gives up (missed-IRQ safety, never hit in
 * normal operation -- the completion IRQ sets the flag on the first wake). */
#define GPU_CL_IDLE_GIVEUP  8u

void
tiku_gpu_cl_init(tiku_gpu_cl_t *cl, void *buf, uint32_t cap_words)
{
    cl->buf        = (uint32_t *)buf;
    cl->cap_words  = cap_words;
    cl->n_words    = 0u;
    cl->id         = 0;
    cl->flush_base = (void *)0;
    cl->flush_span = 0u;
}

void
tiku_gpu_cl_reset(tiku_gpu_cl_t *cl)
{
    cl->n_words    = 0u;
    cl->flush_base = (void *)0;
    cl->flush_span = 0u;
}

/** Append one (register, value) pair; silently drops if the buffer is full. */
static void
cl_add(tiku_gpu_cl_t *cl, uint32_t reg, uint32_t val)
{
    if (cl->n_words + 2u <= cl->cap_words) {
        cl->buf[cl->n_words++] = reg;
        cl->buf[cl->n_words++] = val;
    }
}

tiku_gpu_err_t
tiku_gpu_cl_fill(tiku_gpu_cl_t *cl, const tiku_gpu_surface_t *dst, uint32_t color)
{
    uint32_t res = (uint32_t)dst->w | ((uint32_t)dst->h << 16);

    /* Same register program as the synchronous tiku_gpu_fill, emitted as CL
     * pairs; DRAWCMD carries HOLD so the completion tail waits for the draw. */
    cl_add(cl, GPU_OFF_TEX0BASE,   (uint32_t)(uintptr_t)dst->base);
    cl_add(cl, GPU_OFF_TEX0STRIDE, ((uint32_t)dst->format << 24) |
                                   ((uint32_t)dst->stride & 0xFFFFu));
    cl_add(cl, GPU_OFF_TEX0RES,    res);
    cl_add(cl, GPU_OFF_CLIPMIN,    0u);
    cl_add(cl, GPU_OFF_CLIPMAX,    ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu));
    cl_add(cl, GPU_OFF_RASTCTRL,   GPU_RASTCTRL_MMUL_BYPASS);
    cl_add(cl, GPU_OFF_CODEPTR,    GPU_CODEPTR_FILL);
    cl_add(cl, GPU_OFF_ROPBLEND,   GPU_ROPBLEND_SRC);
    cl_add(cl, GPU_OFF_DRAWCOLOR,  color);
    cl_add(cl, GPU_OFF_STARTXY,    0u);
    cl_add(cl, GPU_OFF_ENDXY,      res);
    cl_add(cl, GPU_OFF_DRAWCMD | GPU_CL_HOLD, GPU_DRAWCMD_RECT);

    cl->flush_base = dst->base;
    cl->flush_span = (uint32_t)dst->stride * (uint32_t)dst->h;
    return (cl->n_words <= cl->cap_words) ? TIKU_GPU_OK : TIKU_GPU_ERR_TIMEOUT;
}

tiku_gpu_err_t
tiku_gpu_submit(tiku_gpu_cl_t *cl)
{
    /* Destination hygiene (as the synchronous path does). */
    if (cl->flush_span != 0u) {
        tiku_cpu_dcache_clean(cl->flush_base, cl->flush_span);
        tiku_cpu_dcache_invalidate(cl->flush_base, cl->flush_span);
    }

    /* Completion tail: once the HOLD'd draw retires, stamp this CL's id and
     * raise IRQ 28 by writing INTERRUPTCTRL=1. */
    cl->id = ++s_cl_seq;
    cl_add(cl, GPU_OFF_CLID,    (uint32_t)cl->id);
    cl_add(cl, GPU_OFF_INTCTRL, 1u);

    /* The GPU reads the list from memory as a non-coherent master. */
    tiku_cpu_dcache_clean(cl->buf, cl->n_words * 4u);

    s_cl_done = 0u;
    __DSB();
    GPU->CMDLISTADDR = (uint32_t)(uintptr_t)cl->buf;
    __DSB();
    GPU->CMDLISTSIZE = cl->n_words;    /* kick -- length in words */
    return TIKU_GPU_OK;
}

tiku_gpu_err_t
tiku_gpu_wait(tiku_gpu_cl_t *cl)
{
    uint32_t idle_seen = 0u;

    /* Sleep until the completion IRQ sets the flag. __WFI wakes on the GPU
     * IRQ (normal case, first wake) or the always-on STIMER tick. The idle
     * fallback keeps a missed IRQ from wedging the system. */
    while (s_cl_done == 0u) {
        __WFI();
        if ((GPU->STATUS & GPU_STATUS_BUSY_MASK) == 0u) {
            if (++idle_seen > GPU_CL_IDLE_GIVEUP) {
                break;
            }
        }
    }

    s_last_status = GPU->STATUS;
    if (cl->flush_span != 0u) {
        tiku_cpu_dcache_invalidate(cl->flush_base, cl->flush_span);
    }
    return (s_cl_done != 0u) ? TIKU_GPU_OK : TIKU_GPU_ERR_TIMEOUT;
}

int32_t
tiku_gpu_last_cl_id(void)
{
    return s_cl_last_id;
}

/*---------------------------------------------------------------------------*/
/* P3: compute -- the GPU as a 2D data / gather engine                       */
/*---------------------------------------------------------------------------*/

tiku_gpu_err_t
tiku_gpu_convert(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* A format-converting copy is just a 1:1 blit: the source is read in its
     * format, the destination written in its own -- the texture unit and the
     * output stage do the conversion in hardware. */
    return tiku_gpu_blit(dst, src, 0, 0, TIKU_GPU_BLEND_SRC);
}

tiku_gpu_err_t
tiku_gpu_resample(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* Bilinear scale src -> dst (up- or down-sample). The texture unit is a
     * 2D gather engine with free bilinear interpolation: one pass resamples a
     * grid to any size. Downscaling is a cheap box-ish reduction. */
    tiku_gpu_surface_t s = *src;
    s.sampling = TIKU_GPU_SAMPLE_BILINEAR;
    return tiku_gpu_blit_rect(dst, &s, 0, 0, dst->w, dst->h, TIKU_GPU_BLEND_SRC);
}

/*
 * NOTE on affine warp (rotation / shear): the RECT-raster MatMul on this
 * silicon applies only the diagonal (scale) + translation columns -- the
 * off-diagonal terms MM01/MM10 are ignored, verified on hardware (a rotation
 * matrix produces a pure scale, no axis mixing). Arbitrary warp therefore
 * needs the quad-raster path (rotated geometry + interpolated texture coords,
 * as the vendor's nema_blit_rotate uses DRAW_CMD=QUAD). Deferred; tiku_gpu_
 * resample covers the scale gather that the rect path does support.
 */

void
tiku_gpu_irq_selftest_pend(void)
{
    NVIC_SetPendingIRQ(GPU_IRQn);
    __DSB();
    __ISB();   /* let the taken exception retire before the caller reads back */
}
