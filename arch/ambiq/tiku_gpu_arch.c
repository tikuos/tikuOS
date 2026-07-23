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
 * "Idle" == none of these set.
 */
#define GPU_STATUS_BUSY_MASK  0xF00FFFFFu

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/** ISR fire counter (volatile: written by tiku_ambiq_gpu_isr, read by tests). */
static volatile uint32_t s_irq_count;

/** STATUS captured after the last drawing op (bring-up diagnostics). */
static uint32_t s_last_status;

/*---------------------------------------------------------------------------*/
/* Register field encodings (from the vendored GPU_Type field defs)          */
/*---------------------------------------------------------------------------*/

/* TEXnSTRIDE: IMGSTRD[15:0] | IMGMODE[23:16] | IMGFMT[31:24]. */
#define GPU_IMGFMT_RGBA8888   1u     /* IMGFMT enum: RGBA8888                 */
#define GPU_IMGMODE_POINT     0u     /* IMGMODE enum: nearest-neighbour       */

/* DRAWCMD.START enum. */
#define GPU_DRAWCMD_RECT      2u     /* "fill rectangle from STARTXY to ENDXY" */

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

/*---------------------------------------------------------------------------*/
/* Interrupt handler                                                         */
/*---------------------------------------------------------------------------*/

/*
 * Strong override of the weak crt_early alias. For P0 this is a pure plumbing
 * proof: bump the counter. There is no asserted GPU line during the CPU-side
 * NVIC self-test, so no ack is needed (NVIC pending is one-shot). Real
 * completion handling -- ack via INTERRUPTCTRL and tiku_process_post to wake a
 * waiting process -- lands in the async-submission phase (P4).
 */
void
tiku_ambiq_gpu_isr(void)
{
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

void
tiku_gpu_irq_selftest_pend(void)
{
    NVIC_SetPendingIRQ(GPU_IRQn);
    __DSB();
    __ISB();   /* let the taken exception retire before the caller reads back */
}
