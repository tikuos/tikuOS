/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_ipc.h - shared-memory layout between the Cortex-M33 app core
 *                   and the FLPR RISC-V coprocessor.
 *
 * Included by BOTH builds (arm-none-eabi and riscv-none-elf), so it must
 * stay plain C99 + <stdint.h>.  The FLPR image lives in a carve at the top
 * of SRAM; the LAST kilobyte of the carve is this shared page.  SRAM is
 * uncached for both masters, so `volatile` plus write-payload-then-flag
 * ordering is the whole coherency story.
 *
 * Layout contract (see also the app linker script and tiku_flpr.ld):
 *   0x2003C000  FLPR .text/.rodata/.data/.bss   (image, loader-placed)
 *   ...         FLPR stack (grows down from the shared page)
 *   0x2003FC00  tiku_flpr_shared_t              (this header)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FLPR_IPC_H_
#define TIKU_FLPR_IPC_H_

#include <stdint.h>

/* Carve geometry -- single source of truth for both linker scripts and the
 * loader.  Keep in sync with the MEMORY regions in nrf54l15.ld (app) and
 * tiku_flpr.ld (coprocessor). */
#define TIKU_FLPR_RAM_BASE   0x2003C000u
#define TIKU_FLPR_RAM_SIZE   0x4000u                     /* 16 KB carve    */
#define TIKU_FLPR_SHARED_ADDR (TIKU_FLPR_RAM_BASE + TIKU_FLPR_RAM_SIZE \
                               - 0x400u)                 /* top 1 KB       */

/* The FLPR firmware bumps .magic to this value as its very first act in
 * main(), so the app can tell "started and running C code" from "started
 * but wedged in the crt".  The heartbeat then proves steady-state life. */
#define TIKU_FLPR_MAGIC      0x464C5052u                 /* 'FLPR'         */

/* Single-slot message mailboxes, one per direction.  Sender fills buf/len
 * then bumps seq (release order by construction on this uncached SRAM);
 * receiver notices the seq change.  A slot is overwritten by the next
 * message -- flow control is the consumer's job (echo-style protocols are
 * naturally lock-step).  240 B fits BLE-PDU-class payloads and keeps the
 * whole page comfortably inside 1 KB. */
#define TIKU_FLPR_MSG_CAP  240u

typedef struct {
    volatile uint32_t magic;        /* TIKU_FLPR_MAGIC once main() runs   */
    volatile uint32_t heartbeat;    /* increments while un-parked          */
    volatile uint32_t cmd;          /* app -> flpr command word            */
    volatile uint32_t rsp;          /* flpr -> app response word           */

    /* app -> flpr mailbox */
    volatile uint32_t a2f_seq;
    volatile uint32_t a2f_len;
    volatile uint8_t  a2f_buf[TIKU_FLPR_MSG_CAP];

    /* flpr -> app mailbox (doorbell: VPR EVENTS_TRIGGERED[0] -> IRQ 76) */
    volatile uint32_t f2a_seq;
    volatile uint32_t f2a_len;
    volatile uint8_t  f2a_buf[TIKU_FLPR_MSG_CAP];
} tiku_flpr_shared_t;

/* Cooperative park/resume protocol.  Hardware truths this encodes:
 * clearing CPURUN does not halt a RUNNING VPR (boot-state control only),
 * and re-setting it resumes at the CURRENT PC, not INITPC -- so a parked
 * core must never have its image swapped underneath it.  Consequently the
 * image loads ONCE per power-on; "stop" parks the firmware in a polling
 * loop and "start" resumes it.  Runtime image replacement needs the
 * resident-trampoline scheme (F5) and is out of scope here. */
#define TIKU_FLPR_CMD_PARK    1u
#define TIKU_FLPR_CMD_RESUME  2u
#define TIKU_FLPR_RSP_PARKED  1u

#define TIKU_FLPR_SHARED  ((tiku_flpr_shared_t *)TIKU_FLPR_SHARED_ADDR)

#endif /* TIKU_FLPR_IPC_H_ */
