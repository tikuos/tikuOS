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
 * PHASE P0 (this milestone): bring-up only -- power the GFX domain, select the
 * clock, reset via SYSCLEAR, read IDREG, capture reset-value forensics, and
 * prove the IRQ 28 plumbing (vector slot + strong ISR + counter) via a CPU-side
 * NVIC pend that needs no GPU cooperation. Drawing, command lists, compute and
 * async completion arrive in later phases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpu_arch.h"
#include "apollo510.h"           /* CMSIS: GPU / PWRCTRL / NVIC (no AmbiqSuite) */

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

    /* 5. Enable the GPU NVIC line (edge cleared on entry; real ack protocol is
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

void
tiku_gpu_irq_selftest_pend(void)
{
    NVIC_SetPendingIRQ(GPU_IRQn);
    __DSB();
    __ISB();   /* let the taken exception retire before the caller reads back */
}
