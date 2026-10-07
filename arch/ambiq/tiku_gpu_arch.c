/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_arch.c - Apollo510 2.5D GPU (Nema-class) driver.
 *
 * Register-level, over the CMSIS GPU_Type, with no AmbiqSuite or NemaGFX
 * code.  A draw emits no fragments without a fragment shader in instruction
 * memory, so init loads a constant-colour instruction for fills.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpu_arch.h"
#include "apollo510.h"           /* CMSIS: GPU / PWRCTRL / NVIC */
#include <hal/tiku_cpu.h>        /* tiku_cpu_dcache_clean / _invalidate       */

/* Defined in tiku_cpu_common.c; used for the rail and mode settle delays. */
extern void tiku_cpu_ambiq_delay_us(unsigned int us);

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Iteration cap for every busy-wait: tens of milliseconds at most.  A power
 * domain settles in microseconds and SYSCLEAR completes at once, so a wait
 * that reaches the cap means a dead or misaddressed block, and the call
 * returns an error; tiku_gpu_deinit() stops waiting and reports nothing.
 */
#define GPU_SPIN_MAX   1000000u

/*
 * STATUS busy mask -- OR of every documented per-stage busy field:
 *   SYSBSY(31) MEMBSY(30) CLBSY(29) CLPBSY(28) RASTBSY(27:24)
 *   DEPTHFIFOBSY(19:16) RENDERBSY(15:12) TEXTMAPBSY(11:8) PIPEBSY(7:4)
 *   COREBSY(3:0)
 * "Idle" == none of these set.  Every draw is a raster operation, and the
 * RASTBSY nibble (27:24) stays set while the raster drains.
 */
#define GPU_STATUS_BUSY_MASK  0xFF0FFFFFu

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/** ISR run count, written by tiku_ambiq_gpu_isr(). */
static volatile uint32_t s_irq_count;

/** STATUS read after the last drawing call. */
static uint32_t s_last_status;

/** Set by the ISR when a command list completes, with that list's id. */
static volatile uint32_t s_cl_done;
static volatile int32_t  s_cl_last_id;

/** Monotonic command-list submission id. */
static int32_t s_cl_seq;

/*---------------------------------------------------------------------------*/
/* REGISTER FIELD ENCODINGS                                                  */
/*---------------------------------------------------------------------------*/

/* TEXnSTRIDE: IMGSTRD[15:0] | IMGMODE[23:16] | IMGFMT[31:24]. */
#define GPU_IMGFMT_RGBA8888   1u     /* IMGFMT enum: RGBA8888                 */
#define GPU_IMGMODE_POINT     0u     /* IMGMODE enum: nearest-neighbour       */

/* DRAWCMD primitive codes are bits, not a sequence (from the raster
 * disassembly): LINE=bit0, RECT=bit1, TRI=bit2, QUAD=bit0|bit2. */
#define GPU_DRAWCMD_LINE      1u
#define GPU_DRAWCMD_RECT      2u     /* rectangle, STARTXY to ENDXY      */
#define GPU_DRAWCMD_TRI       4u     /* triangle, three 16.16 vertices   */

/* DRAWCMD bit 27: colour the primitive from the RGBA interpolator registers
 * across its area; without it the colour is the flat DRAWCOLOR. */
#define GPU_DRAWFLAG_GRADIENT 0x08000000u

/* RASTCTRL (a.k.a. the matrix-multiplier control): a solid fill uses no
 * coordinate transform, so the MatMul is bypassed. Bit names from the vendor's
 * ThinkSi programming layer (nema_programHW.h, MIT grant):
 * MMUL_BYPASS = bit28, MMUL_NONPERSP = bit31. */
#define GPU_RASTCTRL_MMUL_BYPASS   ((1u << 28) | (1u << 31))

/*
 * Registers that sit in RESERVED gaps of the CMSIS GPU_Type but are named in
 * the vendor's (MIT-granted) ThinkSi nema_regs.h. Written by raw offset.
 */
#define GPU_REG_ROPBLEND_MODE  0x1D0u  /* NEMA_ROPBLENDER_BLEND_MODE          */
#define GPU_REG_ROPBLEND_CONST 0x1D8u  /* NEMA_ROPBLENDER_CONST_COLOR         */
#define GPU_REG_RAST_BYPASS    0x388u  /* NEMA_RAST_BYPASS                    */

#define GPU_ROPBLEND_SRC       1u      /* NEMA_BL_SRC: replace destination    */

/*
 * The fragment "pico-shader" for constant-color output, and where it lives.
 *
 * The Nema pipeline emits no fragments unless DRAWCODEPTR points at a shader
 * in instruction memory -- even a solid fill.  Init loads one 64-bit "output
 * the draw color" instruction at IMEM slot 31 and zeroes slots 24..30, as the
 * stock programming layer does; every draw but the LUT pass points DRAWCODEPTR
 * at slot 31 for both foreground and background.  The 8 bytes of microcode (and
 * the pointer word) are the ThinkSi port layer's init-time constants; no
 * library is linked.
 */
#define GPU_SHADER_SLOT_FIRST  24u          /* zeroed slots 24..30            */
#define GPU_SHADER_SLOT_FILL   31u          /* constant-color instruction     */
#define GPU_SHADER_FILL_LO     0x08000002u  /* instruction bits 31:0          */
#define GPU_SHADER_FILL_HI     0x80000009u  /* instruction bits 63:32         */

/* DRAWCODEPTR word for a ROP-blended fill: FRGND[15:0] and BKGND[31:16] both
 * carry slot 31 plus the stock flag bits (0x141F001F verbatim from the
 * programming layer's no-texture path). */
#define GPU_CODEPTR_FILL       0x141F001Fu

/* DRAWCODEPTR for a textured blit: background slot 31 as for a fill, and the
 * foreground field with the texture-emit bit (0x8000), so the slot-31 shader
 * samples TEX1 and outputs the texel.  A blit therefore loads no shader.  The
 * value is the ThinkSi blend path's, and the vendor demo's DRAWCODEPTR reads
 * 0x141F801F during a blit. */
#define GPU_CODEPTR_BLIT       0x141F801Fu

/* MatMul (RASTCTRL 0x118): apply the loaded 3x3 (non-bypass). A blit derives
 * source UVs by transforming the destination coordinates, so unlike the fill
 * the multiplier must stay active. */
#define GPU_RASTCTRL_MMUL_APPLY   0u

/*
 * Compute shader entry point (DRAWCODEPTR), from the disassembly of the
 * ThinkSi library's LUT composer (nema_set_blend).  Its fragment program is
 * loaded to IMEM at draw time.
 */
#define GPU_CODEPTR_LUT        0x141D8000u  /* palette lookup, slot 0 */

/*---------------------------------------------------------------------------*/
/* INTERRUPT HANDLER                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Strong override of the weak crt_early alias.  Handles the completion
 * interrupt raised by a command list's tail (INTERRUPTCTRL=1 after the HOLD'd
 * draw retires): record which CL finished (CLID, 0x148), acknowledge by
 * writing INTERRUPTCTRL=0, clear the NVIC pending bit, and signal the waiter.
 * Also runs for tiku_gpu_irq_selftest_pend(), which asserts no GPU line; the
 * CLID read and the ack write are harmless then.
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
/* POWER, CLOCK AND RESET                                                    */
/*---------------------------------------------------------------------------*/

int
tiku_gpu_powered(void)
{
    return PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX ? 1 : 0;
}

void
tiku_gpu_reset(void)
{
    /* Any write resets the GPU; the value is ignored. */
    GPU->SYSCLEAR = 1u;
    __DSB();
    __ISB();
}

/** @brief Write a GPU register that has no CMSIS GPU_Type member. */
static inline void
gpu_reg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(GPU_BASE + offset) = value;
}

/** @brief Load one 64-bit pico-instruction into shader instruction memory. */
static void
gpu_imem_load(uint32_t slot, uint32_t lo, uint32_t hi)
{
    GPU->IMEMLDIADDR   = slot;
    GPU->IMEMLDIDATAHL = lo;      /* bits 31:0                                */
    GPU->IMEMLDIDATAHH = hi;      /* bits 63:32; commits the load             */
}

/**
 * @brief Bring the drawing pipeline to its post-init state after a reset.
 *
 * Follows the stock library's init_nema_regs and blender init, cut to this
 * silicon's configuration (CONFIG 0xF4030105: ROP blender, no depth buffer).
 * RAST_BYPASS = 1 drives the rasterizer through the direct DRAW* registers.
 */
static void
gpu_processor_init(void)
{
    uint32_t slot;

    GPU->CMDLISTSTATUS = 0u;               /* reset the CL processor          */
    GPU->SYSCLEAR      = 0u;               /* status clear, as stock init */
    (void)tiku_gpu_wait_idle();

    gpu_reg_write(GPU_REG_RAST_BYPASS, 1u);
    GPU->INTERRUPTCTRL = 0u;

    /* Fill shader at IMEM slot 31, slots 24..30 zeroed. */
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

int
tiku_gpu_perf_needs_vddf(tiku_gpu_perf_t perf)
{
    /* HP3 only.  The SDK compares against HIGH_PERFORMANCE, which aliases
     * HFRC2_HP3; HP1 and HP2 stay on VDDC. */
    return (perf == TIKU_GPU_PERF_HP3_250MHZ) ? 1 : 0;
}

unsigned long
tiku_gpu_perf_hz(tiku_gpu_perf_t perf)
{
    switch (perf) {
    case TIKU_GPU_PERF_HP1_192MHZ: return 192000000UL;   /* HFRC  */
    case TIKU_GPU_PERF_HP2_125MHZ: return 125000000UL;   /* HFRC2 */
    case TIKU_GPU_PERF_HP3_250MHZ: return 250000000UL;   /* HFRC2 */
    default:                       return  96000000UL;   /* HFRC  */
    }
}

uint32_t tiku_gpu_perf_get(void)
{
    return PWRCTRL->GFXPERFREQ_b.GFXPERFREQ;
}

int tiku_gpu_rail_is_vddf(void)
{
    return (PWRCTRL->GFXPWRSWSEL_b.GFXVDDSEL ==
            PWRCTRL_GFXPWRSWSEL_GFXVDDSEL_VDDF) ? 1 : 0;
}

tiku_gpu_err_t
tiku_gpu_init(tiku_gpu_perf_t perf)
{
    uint32_t spins = 0u;
    uint32_t pm;

    /* HP3 runs on VDDF, which is regulated only while the SIMO buck runs;
     * without the buck the mode is refused, as the SDK refuses it. */
    if (tiku_gpu_perf_needs_vddf(perf) &&
        PWRCTRL->VRSTATUS_b.SIMOBUCKST != PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        return TIKU_GPU_ERR_POWER;
    }

    /* 1. Mode and rail first, with the domain off.  Datasheet 4.3.2: the GFX
     *    mode may change only while the domain is off, and an HP transition
     *    sets GFXVDDSEL before GFXPERFREQ.  A domain an earlier init left up
     *    is powered down first.
     *
     *    No SPOT buck Ton state is set for the GPU: this port has only the
     *    CPU-only Ton states, so with the GPU powered the buck runs on
     *    CPU-only on-times (the SDK's states 2..5 cover the GPU modes). */
    if (PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX != 0u) {
        tiku_gpu_deinit();
    }

    pm = __get_PRIMASK();
    __disable_irq();

    PWRCTRL->GFXPWRSWSEL_b.GFXVDDSEL = tiku_gpu_perf_needs_vddf(perf)
        ? PWRCTRL_GFXPWRSWSEL_GFXVDDSEL_VDDF
        : PWRCTRL_GFXPWRSWSEL_GFXVDDSEL_VDDC;
    __DSB();
    tiku_cpu_ambiq_delay_us(2u);        /* SDK: VOLTADJ_WAIT = 1 us */

    PWRCTRL->GFXPERFREQ_b.GFXPERFREQ = (uint32_t)perf & 0x3u;
    __DSB();
    tiku_cpu_ambiq_delay_us(8u);        /* SDK: PWRADJ_WAIT = 6 us  */

    __set_PRIMASK(pm);

    /* 2. Power the GFX domain: set DEVPWREN, then wait for DEVPWRSTATUS. */
    PWRCTRL->DEVPWREN_b.PWRENGFX = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTGFX == 0u) {
        if (++spins > GPU_SPIN_MAX) {
            return TIKU_GPU_ERR_POWER;
        }
    }

    /* 3. Reset the core, then wait for idle. */
    tiku_gpu_reset();
    if (tiku_gpu_wait_idle() != TIKU_GPU_OK) {
        /* STATUS did not clear: a dead core or a wrong register offset.
         * The domain stays powered so the caller can read the registers. */
        return TIKU_GPU_ERR_TIMEOUT;
    }

    /* 4. IDREG reads a fixed nonzero value on a live, clocked core; 0 or
     *    all ones means no power or a wrong offset. */
    {
        uint32_t id = GPU->IDREG;
        if (id == 0u || id == 0xFFFFFFFFu) {
            return TIKU_GPU_ERR_ID;
        }
    }

    /* 5. Bring the drawing pipeline to a known-good state (CL processor
     *    reset, RAST_BYPASS, fill shader parked at IMEM slot 31, ROP=SRC). */
    gpu_processor_init();

    /* 6. Clear any pending GPU IRQ and enable the NVIC line; the ISR
     *    acknowledges each completion through INTERRUPTCTRL. */
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
/* INTROSPECTION                                                             */
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
/* IRQ SELF-TEST                                                             */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_irq_count(void)
{
    return s_irq_count;
}

/*---------------------------------------------------------------------------*/
/* DRAWING                                                                   */
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

    /* Before the draw: clean writes back any dirty line, so nothing the CPU
     * wrote can be evicted over the GPU's output mid-render, then invalidate
     * drops the lines.  The GPU is a non-coherent bus master. */
    tiku_cpu_dcache_clean(dst, span);
    tiku_cpu_dcache_invalidate(dst, span);

    /* Bind the destination surface: TEX0 doubles as "drawing surface 0". */
    GPU->TEX0BASE   = (uint32_t)base;
    GPU->TEX0STRIDE = ((uint32_t)GPU_IMGFMT_RGBA8888 << 24) |
                      ((uint32_t)GPU_IMGMODE_POINT   << 16) |
                      ((uint32_t)stride_bytes & 0xFFFFu);
    GPU->TEX0RES    = (uint32_t)w | ((uint32_t)h << 16);

    /* A 4095 x 4095 clip rectangle does not clip the fill however CLIPMAX
     * packs its coordinates.  A solid fill bypasses the matrix. */
    GPU->CLIPMIN  = 0u;
    GPU->CLIPMAX  = 0x0FFF0FFFu;
    GPU->RASTCTRL = GPU_RASTCTRL_MMUL_BYPASS;

    /* The fragment pipeline: both shader pointers at the constant-color
     * instruction (slot 31, loaded at init), ROP blender in SRC mode, and the
     * fill color in DRAWCOLOR, as the stock fill path programs them.  Without
     * DRAWCODEPTR the raster runs and writes nothing. */
    GPU->DRAWCODEPTR = GPU_CODEPTR_FILL;
    gpu_reg_write(GPU_REG_ROPBLEND_MODE, GPU_ROPBLEND_SRC);
    GPU->DRAWCOLOR   = color;

    /* Rectangle corners, packed-integer form (x | y<<16): STARTXY inclusive
     * origin, ENDXY exclusive corner -- the only two coordinate registers the
     * stock RECT path writes. */
    GPU->DRAWPT0  = 0u;                                     /* (0,0)        */
    GPU->DRAWPT1  = (uint32_t)w | ((uint32_t)h << 16);      /* (w,h)        */
    __DSB();
    GPU->DRAWCMD   = GPU_DRAWCMD_RECT;                      /* START = RECT */

    err = tiku_gpu_wait_idle();
    s_last_status = GPU->STATUS;

    /* The GPU wrote memory behind the cache -- drop stale copies so the CPU
     * reads the fresh pixels. */
    tiku_cpu_dcache_invalidate(dst, span);

    return err;
}

/*---------------------------------------------------------------------------*/
/* BLIT AND BLEND                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief IEEE-754 bit pattern of @p f.
 *
 * The MatMul registers take IEEE-754 words and convert them to their internal
 * 24-bit format on write: 1.0f (0x3F800000) in MM00 reads back as 0x000F8000.
 */
static inline uint32_t
gpu_f2u(float f)
{
    union { float f; uint32_t u; } x;
    x.f = f;
    return x.u;
}

/**
 * @brief Load the full 3x3 screen->texture matrix and leave the MatMul active.
 *
 * All nine coefficients are written: init programs no identity default.
 * Off-diagonals are zero; only MM00/MM02/MM11/MM12 vary.
 */
static void
gpu_load_matrix(float m00, float m02, float m11, float m12)
{
    GPU->MM00 = gpu_f2u(m00);   GPU->MM01 = gpu_f2u(0.0f);  GPU->MM02 = gpu_f2u(m02);
    GPU->MM10 = gpu_f2u(0.0f);  GPU->MM11 = gpu_f2u(m11);   GPU->MM12 = gpu_f2u(m12);
    GPU->MM20 = gpu_f2u(0.0f);  GPU->MM21 = gpu_f2u(0.0f);  GPU->MM22 = gpu_f2u(1.0f);
    GPU->RASTCTRL = GPU_RASTCTRL_MMUL_APPLY;   /* apply the 3x3 (non-bypass) */
}

/**
 * @brief Common blit path: draw (dx,dy)..(dx+dw,dy+dh) of @p dst from @p src.
 *
 * The matrix maps each destination pixel back to a source texel: a pure
 * translate for a 1:1 blit, with scale factors when dw/dh differ.
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

    /* Scissor to the destination surface: a stray coordinate cannot write
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
/* RASTER PRIMITIVES AND GRADIENT                                            */
/*---------------------------------------------------------------------------*/

/** @brief Integer pixel coordinate -> 16.16 fixed point (vertex registers). */
static inline uint32_t
gpu_i2fx16(int32_t v)
{
    return (uint32_t)(v << 16);
}

/** @brief Gradient slope (b-a)/n in signed 16.16; 0 when n <= 0. */
static inline int32_t
gpu_grad_slope(int32_t a, int32_t b, int32_t n)
{
    if (n <= 0) { return 0; }
    return ((b - a) << 16) / n;
}

/**
 * @brief Flat-draw setup: bind dst as TEX0, clip to it, select the
 *        constant-color shader with the MatMul bypassed.
 *
 * The caller sets color, coordinates and interpolators, writes DRAWCMD last,
 * and does the cache maintenance and the idle wait.
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
    gpu_reg_write(GPU_REG_ROPBLEND_MODE, GPU_ROPBLEND_SRC);
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

    gpu_dst_setup(dst);   /* clips an oversized rect to the surface */
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
    /* DRAWCOLOR holds color_a.  With the gradient bit set the rasterizer
     * uses the interpolated color instead. */
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

/** @brief Integer floor(sqrt(@p v)), for the corner-span half-widths. */
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

/**
 * @brief Emit one axis-aligned RECT draw (integer coords) and wait for idle.
 *
 * The surface, shader, color and clip must already be programmed.
 */
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

/**
 * @brief Fill a rounded rectangle; with radius w/2 = h/2 it is a circle.
 *
 * A full-width center band, then for each row of the corner bands one span
 * whose half-width comes from the circle equation (no anti-aliasing).  Each
 * span is a RECT draw clipped to the surface.
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
/* ASYNC COMMAND LISTS                                                       */
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
 * until that (drawing) write retires before advancing.  Set on DRAWCMD so the
 * completion tail runs only after the draw has finished. */
#define GPU_CL_HOLD         0xFF000000u

/* Idle wakes with no completion signal that tiku_gpu_wait tolerates; the
 * next one ends the wait, which covers a missed IRQ.  Counted per wait. */
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

/** @brief Append one (register, value) pair; dropped if the buffer is full. */
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

    if (cl->n_words > cl->cap_words ||
        cl->cap_words - cl->n_words < 24u) {
        return TIKU_GPU_ERR_TIMEOUT;
    }

    /* The fill's register program as CL pairs, with the surface's own format
     * and clip; DRAWCMD carries HOLD so the completion tail waits for it. */
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
    return TIKU_GPU_OK;
}

tiku_gpu_err_t
tiku_gpu_submit(tiku_gpu_cl_t *cl)
{
    if (cl->n_words > cl->cap_words ||
        cl->cap_words - cl->n_words < 4u) {
        return TIKU_GPU_ERR_TIMEOUT;
    }

    /* Destination cache maintenance, as the synchronous path does it. */
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

    /* Sleep until the completion IRQ sets the flag.  __WFI wakes on the GPU
     * IRQ or the always-on STIMER tick; the idle count bounds the wait if the
     * IRQ is missed. */
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
/* COMPUTE                                                                   */
/*---------------------------------------------------------------------------*/

tiku_gpu_err_t
tiku_gpu_convert(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* A format-converting copy is a 1:1 blit: the source is read in its
     * format, the destination written in its own -- the texture unit and the
     * output stage do the conversion in hardware. */
    return tiku_gpu_blit(dst, src, 0, 0, TIKU_GPU_BLEND_SRC);
}

tiku_gpu_err_t
tiku_gpu_resample(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* Bilinear scale src -> dst (up- or down-sample) in one pass.  A
     * downsample puts a scale factor above 1 in the matrix; see the note on
     * scale factors below. */
    tiku_gpu_surface_t s = *src;
    s.sampling = TIKU_GPU_SAMPLE_BILINEAR;
    return tiku_gpu_blit_rect(dst, &s, 0, 0, dst->w, dst->h, TIKU_GPU_BLEND_SRC);
}

tiku_gpu_err_t
tiku_gpu_copy_rect(const tiku_gpu_surface_t *dst, int16_t dx, int16_t dy,
                   const tiku_gpu_surface_t *src, int16_t sx, int16_t sy,
                   uint16_t w, uint16_t h)
{
    /* Positioned strided 2D copy: dest rect (dx,dy,w,h) samples the source
     * starting at (sx,sy) -- translate matrix tex = screen - (dx-sx, dy-sy).
     * The general-purpose "2D memcpy" between arbitrary surface windows. */
    return gpu_blit_core(dst, src, dx, dy, w, h,
                         1.0f, -(float)(dx - sx), 1.0f, -(float)(dy - sy),
                         TIKU_GPU_BLEND_SRC);
}

tiku_gpu_err_t
tiku_gpu_multiply(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* Element-wise product: out = src * dst / 255 per channel. The ROP
     * blender with source factor DESTCOLOR (and dst factor ZERO) computes it
     * in fixed-function hardware -- masking, windowing, per-pixel gains. */
    return tiku_gpu_blit(dst, src, 0, 0, TIKU_GPU_BLEND_MULTIPLY);
}

tiku_gpu_err_t
tiku_gpu_scale_const(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src,
                     uint32_t factor)
{
    /* Exact per-channel constant scale: out = src * factor / 255 via the ROP
     * CONSTCOLOR source factor and the const-color register -- the exact-scale
     * building block that tiku_gpu_scale_bias reuses under an added bias. */
    gpu_reg_write(GPU_REG_ROPBLEND_CONST, factor);
    return tiku_gpu_blit(dst, src, 0, 0, (tiku_gpu_blend_t)0x000A);
}

/*
 * Scale factors: a MatMul scale factor above 1 (a downsample) is unreliable.
 * The IEEE->internal conversion drops precision (2.0f converts to 0x00100000
 * where 1.0f is 0x000F8000), and SX of 2 or 4 samples inconsistent source
 * coordinates.  Factors below 1 (upsampling) sample correctly.
 * tiku_gpu_reduce_mean() therefore folds with 1:1 translate blits.
 */

/* Shadow palette: the hardware palette lookup swaps the index's nibbles -- it
 * addresses the 256 entries as a 16x16 grid with x = high nibble, y = low
 * nibble, so entry = ((v & 0xF) << 4) | (v >> 4); all four RGBA lanes pass
 * through unchanged.  The swap is its own inverse, so shadow[i] =
 * palette[swap(i)] gives dst = palette[swap(swap(v))] = palette[v] for all
 * 256 values.  In SSRAM: the GPU reads it. */
static uint32_t s_lut_shadow[256] __attribute__((section(".ssram"), aligned(32)));

tiku_gpu_err_t
tiku_gpu_lut_apply(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *index,
                   const uint32_t *palette)
{
    uint32_t dspan = (uint32_t)dst->stride * (uint32_t)dst->h;
    uint32_t ispan = (uint32_t)index->stride * (uint32_t)index->h;
    tiku_gpu_err_t err;
    uint32_t e;

    /* Invert the nibble-swap addressing: shadow[e] = palette[swap(e)]. */
    for (e = 0u; e < 256u; e++) {
        s_lut_shadow[e] = palette[((e & 0xFu) << 4) | (e >> 4)];
    }

    /* GPU reads the index and the shadow palette; writes dst. */
    tiku_cpu_dcache_clean(index->base, ispan);
    tiku_cpu_dcache_clean(s_lut_shadow, sizeof s_lut_shadow);
    tiku_cpu_dcache_clean(dst->base, dspan);
    tiku_cpu_dcache_invalidate(dst->base, dspan);

    GPU->TEX0BASE   = (uint32_t)(uintptr_t)dst->base;
    GPU->TEX0STRIDE = ((uint32_t)dst->format << 24) | ((uint32_t)dst->stride & 0xFFFFu);
    GPU->TEX0RES    = (uint32_t)dst->w | ((uint32_t)dst->h << 16);
    /* index -> TEX1 (L8, point sampling). */
    GPU->TEX1BASE   = (uint32_t)(uintptr_t)index->base;
    GPU->TEX1STRIDE = ((uint32_t)TIKU_GPU_FMT_L8 << 24) | ((uint32_t)index->stride & 0xFFFFu);
    GPU->TEX1RES    = (uint32_t)index->w | ((uint32_t)index->h << 16);
    /* shadow palette -> TEX2 (256x1 RGBA8888; vendor-captured packing). */
    GPU->TEX2BASE   = (uint32_t)(uintptr_t)s_lut_shadow;
    GPU->TEX2STRIDE = ((uint32_t)TIKU_GPU_FMT_RGBA8888 << 24) | 0x00040000u;
    GPU->TEX2RES    = 256u | (1u << 16);

    /* 3-instruction palette-lookup shader at IMEM slots 0..2 (sample index,
     * fetch palette[index], emit); the words are those of the vendor
     * library's command list. */
    gpu_imem_load(0u, 0x080C108Bu, 0x00002000u);
    gpu_imem_load(1u, 0x0000110Bu, 0x00000000u);
    gpu_imem_load(2u, 0x080C0002u, 0x8A0761C7u);

    /* The LUT shader's TEX1 sample coordinate is 12.4 fixed point -- the
     * matrix output is divided by 16 -- so MM00=16.0 yields texel = x
     * exactly, and MM11=1.0 samples row y 1:1.  The matrix stays active with
     * MM02/MM12 at 0: a translate moves the palette coordinate (raw/4), not
     * the texel. */
    gpu_load_matrix(16.0f, 0.0f, 1.0f, 0.0f);
    gpu_reg_write(GPU_REG_ROPBLEND_MODE, GPU_ROPBLEND_SRC);
    GPU->DRAWCODEPTR = GPU_CODEPTR_LUT;

    GPU->CLIPMIN = 0u;
    GPU->CLIPMAX = ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu);

    /* Priming: the first LUT-mode draw after a GPU init missamples -- the
     * 12.4 coordinate mode latches one program-and-draw cycle late, so that
     * draw samples texel 16x+8 (the raw matrix output), and it latches only
     * when a fragment reaches the latch point.  So every call draws the full
     * rectangle, programs the IMEM, matrix and DRAWCODEPTR again, and draws
     * again over the first draw's pixels. */
    GPU->DRAWPT0 = 0u;
    GPU->DRAWPT1 = ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_RECT;
    err = tiku_gpu_wait_idle();
    if (err != TIKU_GPU_OK) { goto restore; }

    gpu_imem_load(0u, 0x080C108Bu, 0x00002000u);
    gpu_imem_load(1u, 0x0000110Bu, 0x00000000u);
    gpu_imem_load(2u, 0x080C0002u, 0x8A0761C7u);
    gpu_load_matrix(16.0f, 0.0f, 1.0f, 0.0f);
    GPU->DRAWCODEPTR = GPU_CODEPTR_LUT;
    GPU->DRAWPT0 = 0u;
    GPU->DRAWPT1 = ((uint32_t)dst->h << 16) | ((uint32_t)dst->w & 0xFFFFu);
    __DSB();
    GPU->DRAWCMD = GPU_DRAWCMD_RECT;

    err = tiku_gpu_wait_idle();
restore:
    s_last_status = GPU->STATUS;
    tiku_cpu_dcache_invalidate(dst->base, dspan);
    {
        tiku_gpu_perf_t perf = (tiku_gpu_perf_t)
            PWRCTRL->GFXPERFREQ_b.GFXPERFREQ;
        uint32_t irq_count = s_irq_count;
        tiku_gpu_err_t reset_err;
        /* LUT leaves pipeline state that requires a domain power cycle. */
        tiku_gpu_deinit();
        reset_err = tiku_gpu_init(perf);
        s_irq_count = irq_count;
        if (err == TIKU_GPU_OK) {
            err = reset_err;
        }
    }
    return err;
}

tiku_gpu_err_t
tiku_gpu_scale_bias(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src,
                    uint32_t scale, uint32_t bias)
{
    tiku_gpu_err_t err;

    /* Per-channel affine: dst = scale*src/255 + bias, saturating to [0,255],
     * in two fixed-function ROP passes:
     *
     *   1. fill dst with the constant bias (replace mode);
     *   2. blend src with source factor CONSTCOLOR, the const-color register
     *      holding scale (as in tiku_gpu_scale_const), and a destination
     *      factor of one, which adds the bias with saturation (as
     *      TIKU_GPU_BLEND_ADD does).
     *
     * gpu_blit_core's read-modify blend reads the filled bias as the
     * background, so the affine takes one blended pass after the fill. */
    err = tiku_gpu_fill(dst->base, src->w, src->h, dst->stride, bias);
    if (err != TIKU_GPU_OK) {
        return err;
    }
    gpu_reg_write(GPU_REG_ROPBLEND_CONST, scale);
    /* 0x010A: source factor CONSTCOLOR, destination factor one */
    return tiku_gpu_blit(dst, src, 0, 0, (tiku_gpu_blend_t)0x010Au);
}

tiku_gpu_err_t
tiku_gpu_avg(const tiku_gpu_surface_t *dst, const tiku_gpu_surface_t *src)
{
    /* Pairwise 50/50 average: dst = (dst + src)/2 per channel.  The ROP
     * applies the CONSTCOLOR factor (const-color 0x80 = x0.502) to both the
     * source and the destination, so out = 0.502*Src + 0.502*Dst -- a
     * two-surface cross-fade and the fold behind tiku_gpu_reduce_mean.
     * The 0.502 factor can put the result 1 above the exact mean. */
    gpu_reg_write(GPU_REG_ROPBLEND_CONST, 0x80808080u);
    return tiku_gpu_blit(dst, src, 0, 0, TIKU_GPU_BLEND_AVG);
}

tiku_gpu_err_t
tiku_gpu_reduce_mean(const tiku_gpu_surface_t *surf, uint32_t *out_mean)
{
    uint32_t w = surf->w;
    uint32_t h = surf->h;
    tiku_gpu_err_t err = TIKU_GPU_OK;

    /* Balanced fold tree -> power-of-two dimensions only. */
    if (w == 0u || h == 0u ||
        (w & (w - 1u)) != 0u || (h & (h - 1u)) != 0u) {
        return TIKU_GPU_ERR_PARAM;
    }

    /* out = 0.502*Src + 0.502*Dst on all four lanes (see tiku_gpu_avg). */
    gpu_reg_write(GPU_REG_ROPBLEND_CONST, 0x80808080u);

    /* Horizontal folds: average the right half onto the left, halving the
     * width to 1.  Each fold is a 1:1 translate blit -- destination [0,w/2)
     * samples source [w/2,w), disjoint regions of the same buffer, so there
     * is no read/write hazard and no scale factor above 1.  After log2(w)
     * folds column 0 holds the equal-weight mean of all w columns. */
    while (w > 1u) {
        uint32_t hw = w >> 1;
        err = gpu_blit_core(surf, surf, 0, 0, (uint16_t)hw, (uint16_t)h,
                            1.0f, (float)hw, 1.0f, 0.0f, TIKU_GPU_BLEND_AVG);
        if (err != TIKU_GPU_OK) { return err; }
        w = hw;
    }
    /* Vertical folds: same, over the surviving column 0 -> pixel (0,0) is the
     * grand mean of the original surface. */
    while (h > 1u) {
        uint32_t hh = h >> 1;
        err = gpu_blit_core(surf, surf, 0, 0, 1u, (uint16_t)hh,
                            1.0f, 0.0f, 1.0f, (float)hh, TIKU_GPU_BLEND_AVG);
        if (err != TIKU_GPU_OK) { return err; }
        h = hh;
    }

    tiku_cpu_dcache_invalidate(surf->base, 4u);
    if (out_mean != (uint32_t *)0) {
        *out_mean = ((const volatile uint32_t *)surf->base)[0];
    }
    return err;
}

/*
 * Rotation and shear: the RECT-raster MatMul applies only the diagonal (scale)
 * and translation terms; MM01 and MM10 are ignored, so a rotation matrix
 * gives a pure scale.  A warp needs the QUAD raster with interpolated texture
 * coordinates, as the vendor's nema_blit_rotate uses; this driver has no QUAD
 * path.
 */

void
tiku_gpu_irq_selftest_pend(void)
{
    NVIC_SetPendingIRQ(GPU_IRQn);
    __DSB();
    __ISB();   /* let the taken exception retire before the caller reads back */
}
