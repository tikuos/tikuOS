/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_arch.c - nRF54L15 FLPR (VPR RISC-V) coprocessor control.
 *
 * The FLPR firmware is built by the RISC-V sub-build (arch/nordic/flpr/),
 * objcopy'd to a flat binary and embedded into this application image as a
 * binary blob (_binary_* symbols).  Starting the coprocessor is then pure
 * software: copy the blob into the SRAM carve, point VPR00.INITPC at the
 * carve base (the crt0 `_start` is the first instruction by linker-script
 * construction) and set CPURUN.EN.  Stopping clears CPURUN; a subsequent
 * start reloads the image so the firmware always boots a fresh world.
 *
 * The blob contains .text/.rodata/.data; .bss is zeroed by the FLPR crt0
 * itself.  The shared IPC page (top 1 KB of the carve, tiku_flpr_ipc.h)
 * is scrubbed here before each start so stale magic/heartbeat values can
 * never masquerade as liveness.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_flpr_arch.h>

#if TIKU_FLPR_ENABLE

#include <arch/nordic/tiku_device_select.h>   /* NRF_VPR00_S               */
#include <arch/nordic/flpr/tiku_flpr_ipc.h>
#include <string.h>

/* Embedded FLPR image, created by `objcopy -I binary` in the Makefile. */
extern const uint8_t _binary_tiku_flpr_bin_start[];
extern const uint8_t _binary_tiku_flpr_bin_end[];

#define FLPR_IMAGE_SIZE \
    ((uint32_t)(_binary_tiku_flpr_bin_end - _binary_tiku_flpr_bin_start))

/* Room for the image: the carve minus the shared page and a 1 KB stack
 * floor (mirrors the ASSERT in tiku_flpr.ld). */
#define FLPR_IMAGE_MAX  (TIKU_FLPR_RAM_SIZE - 0x400u - 0x400u)

/* The VPR is a hard-attributed NON-SECURE bus master (its register block
 * only answers on the NS alias -- the secure alias bus-faults even for the
 * debug probe), so every FLPR instruction fetch and data access is a
 * non-secure transaction.  Against this port's secure-by-default memory
 * map those get rejected: CPURUN reads Running while the core executes
 * nothing (measured -- shared page stayed zero).  Fix: one MPC override
 * region turning the carve into the port's single deliberate non-secure
 * enclave (R+W+X, SECATTR=NonSecure).  Secure M33 accesses to it remain
 * legal, so the loader/IPC side needs nothing special. */
static void flpr_carve_nonsecure(void)
{
    NRF_MPC00_S->OVERRIDE[0].STARTADDR = TIKU_FLPR_RAM_BASE;
    NRF_MPC00_S->OVERRIDE[0].ENDADDR   = TIKU_FLPR_RAM_BASE
                                         + TIKU_FLPR_RAM_SIZE;
    NRF_MPC00_S->OVERRIDE[0].PERM =
        (1u << 0) | (1u << 1) | (1u << 2) |    /* READ | WRITE | EXECUTE   */
        (0u << 3);                             /* SECATTR = NonSecure      */
    NRF_MPC00_S->OVERRIDE[0].PERMMASK = 0xFu;  /* override all four fields */
    NRF_MPC00_S->OVERRIDE[0].CONFIG   = (1u << 9);              /* ENABLE  */
}

/* Once the VPR has ever been started this boot, the image must NOT be
 * reloaded: re-setting CPURUN resumes at the CURRENT PC (not INITPC), so
 * swapping code under a parked core executes garbage (hardware-measured:
 * it took the whole board down).  Boot-once + park/resume instead. */
static uint8_t flpr_booted;

int tiku_flpr_arch_start(void)
{
    uint32_t size = FLPR_IMAGE_SIZE;

    if (size == 0u || size > FLPR_IMAGE_MAX) {
        return -1;
    }

    if (flpr_booted) {
        /* Resume a parked firmware; already-running is a no-op. */
        if (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_PARKED) {
            TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_RESUME;
            __asm__ volatile ("dsb 0xF" ::: "memory");
        }
        return 0;
    }

    flpr_carve_nonsecure();

    /* First (and only) load this power-on: image + scrubbed shared page. */
    memcpy((void *)TIKU_FLPR_RAM_BASE, _binary_tiku_flpr_bin_start, size);
    memset((void *)TIKU_FLPR_SHARED_ADDR, 0, 0x400u);
    __asm__ volatile ("dsb 0xF" ::: "memory");

    NRF_VPR00_NS->INITPC = TIKU_FLPR_RAM_BASE;
    NRF_VPR00_NS->CPURUN = 1u;                  /* EN = Running             */
    flpr_booted = 1u;
    return 0;
}

void tiku_flpr_arch_stop(void)
{
    /* Cooperative park: ask the firmware to spin in its parked loop and
     * wait for the ack (bounded).  CPURUN is left alone -- see the
     * boot-once comment above. */
    if (TIKU_FLPR_SHARED->magic == TIKU_FLPR_MAGIC &&
        TIKU_FLPR_SHARED->rsp != TIKU_FLPR_RSP_PARKED) {
        uint32_t spin;
        TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_PARK;
        __asm__ volatile ("dsb 0xF" ::: "memory");
        for (spin = 0u; spin < 2000000u; spin++) {
            if (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_PARKED) {
                break;
            }
        }
    }
}

int tiku_flpr_arch_running(void)
{
    /* "Running" = booted and not parked.  CPURUN itself stays set for the
     * rest of the power-on (see the boot-once comment). */
    return (flpr_booted &&
            TIKU_FLPR_SHARED->rsp != TIKU_FLPR_RSP_PARKED) ? 1 : 0;
}

int tiku_flpr_arch_alive(void)
{
    return (TIKU_FLPR_SHARED->magic == TIKU_FLPR_MAGIC) ? 1 : 0;
}

uint32_t tiku_flpr_arch_heartbeat(void)
{
    return TIKU_FLPR_SHARED->heartbeat;
}

uint32_t tiku_flpr_arch_image_size(void)
{
    return FLPR_IMAGE_SIZE;
}

#endif /* TIKU_FLPR_ENABLE */
