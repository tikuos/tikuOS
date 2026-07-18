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

    /* Beacon-offload telemetry (F4). */
    volatile uint32_t beacon_bursts;

    /* Connection controller (L6).  RX-probe (F-L6.1 step 0) proves the
     * FLPR can drive RADIO *RX* (the beacon is TX-only): listen on the adv
     * channel and report what address-matched / CRC-passed, plus the head
     * of the first CRC-valid packet.  rx_done flips 0->1 when finished. */
    volatile uint32_t rx_addr_evts;     /* ADDRESS matches in the window   */
    volatile uint32_t rx_crcok_evts;    /* CRC-valid packets               */
    volatile uint32_t rx_done;          /* probe finished                  */
    volatile uint8_t  rx_first[16];     /* head of 1st CRC-valid packet    */
    volatile uint32_t rx_first_len;

    /* Connection state (F-L6.1 step 1).  The FLPR advertises connectably,
     * captures the CONNECT_IND, and (step 1b) holds the link.  conn_state:
     * 0 idle/advertising, 1 connected, 2 gave up (no central).  The rest
     * are the parsed CONNECT_IND LLData the M33 reads back to verify. */
    volatile uint32_t conn_state;
    volatile uint32_t conn_aa;
    volatile uint32_t conn_crcinit;
    volatile uint32_t conn_events;      /* connection events run (step 1b) */
    volatile uint16_t conn_interval;    /* 1.25 ms units                   */
    volatile uint16_t conn_timeout;     /* 10 ms units                     */
    volatile uint16_t conn_winoffset;
    volatile uint8_t  conn_hop;
    volatile uint8_t  conn_winsize;
    volatile uint8_t  conn_chm[5];
    /* Peer identity captured from the CONNECT_IND (Phase E / SMP): the SMP
     * f5/f6 key derivation binds the LTK to both device addresses.  InitA is
     * the central (initiator, address A); AdvA is us (peripheral, responder,
     * address B) -- echoed so the host needn't remember what it advertised.
     * conn_addr_types: bit0 = InitA type, bit1 = AdvA type (1 = random). */
    volatile uint8_t  conn_inita[6];    /* initiator (central) address = A  */
    volatile uint8_t  conn_adva[6];     /* advertiser (our) address     = B  */
    volatile uint8_t  conn_addr_types;  /* bit0 InitA, bit1 AdvA (1=random)  */
    volatile uint32_t conn_sub;         /* vestigial (host tracks CCCD, PhaseB)*/
    volatile uint32_t conn_gap;         /* anchored-RX: converged idle iters */
    volatile uint32_t conn_rxon;        /* anchored-RX: last RX-on iters (s)  */
    volatile uint32_t conn_cm;          /* Phase A: CHANNEL_MAP_UPDATEs applied*/
    volatile uint32_t conn_cu;          /* Phase A: CONNECTION_UPDATEs applied */
    volatile uint32_t a2f_ack;          /* Phase B: last a2f L2CAP fragment    */
                                        /* the controller consumed for TX (==  */
                                        /* a2f_seq means the slot is free)     */
    volatile uint32_t f2a_llid;         /* Phase C: RX fragment boundary       */
                                        /* (2 = start of L2CAP PDU, 1 = cont)  */
    volatile uint32_t a2f_llid;         /* Phase C: TX fragment boundary       */
    /* L2CAP transport (Phase B/C): while a connection is held the mailbox
     * carries L2CAP FRAGMENTS ([{len}{CID}payload...] split across data PDUs),
     * NOT NUS bytes.  RX: each received L2CAP data PDU -> f2a with f2a_llid
     * (2 start / 1 continuation), doorbelled, for the M33 host to RECOMBINE
     * and run ATT/GATT.  TX: the host's response/notification, fragmented, ->
     * a2f with a2f_llid, flow-controlled via a2f_ack; the controller wraps
     * each in a data PDU with that LLID.  The FLPR never parses ATT. */
} tiku_flpr_shared_t;

/* CMD_CONN_ADV input (in a2f_buf): connectable ADV PDU + our AdvA. */
typedef struct {
    uint32_t adv_len;                   /* bytes in adv[] ([S0][LEN][S1]..) */
    uint8_t  addr[6];                   /* AdvA to match in the CONNECT_IND */
    uint8_t  adv[48];
} tiku_flpr_conn_t;

/* Cooperative park/resume protocol.  Hardware truths this encodes:
 * clearing CPURUN does not halt a RUNNING VPR (boot-state control only),
 * and re-setting it resumes at the CURRENT PC, not INITPC -- so a parked
 * core must never have its image swapped underneath it.  Consequently the
 * image loads ONCE per power-on; "stop" parks the firmware in a polling
 * loop and "start" resumes it.  Runtime image replacement needs the
 * resident-trampoline scheme (F5) and is out of scope here. */
#define TIKU_FLPR_CMD_PARK    1u
#define TIKU_FLPR_CMD_RESUME  2u
/* Pulse engine (F3): parameters in a2f_buf as tiku_flpr_pulse_t; the
 * firmware drives its VIO pin (bit 7 = P2.07 = LED3, routed by the app
 * core via GPIO.PIN_CNF.CTRLSEL=VPR) for `edges` transitions with
 * `half_cycles` FLPR cycles between them, then raises RSP_PULSE_DONE.
 * 50 % duty by construction (every edge is a toggle). */
#define TIKU_FLPR_CMD_PULSE   3u
/* Beacon offload (F4): parameters in a2f_buf as tiku_flpr_beacon_t.  The
 * firmware enters beacon mode -- one 3-channel BLE burst per interval,
 * including the UARTE21 HF-clock kick -- until CMD_BEACON_STOP.  The app
 * core prepares everything the firmware must not: radio link-config
 * registers (while the RADIO is still secure), the SPU flips that make
 * RADIO+UARTE21 reachable by this non-secure master, and the session
 * CONSTLAT hold.  Burst count is published in .beacon_bursts. */
#define TIKU_FLPR_CMD_BEACON      4u
#define TIKU_FLPR_CMD_BEACON_STOP 5u
/* RX probe (L6 F-L6.1 step 0): listen on the adv channel; results in the
 * rx_* shared fields.  Link config (MODE/PCNF/adv-AA/CRC) is programmed by
 * the M33 while RADIO is secure, then RADIO+UARTE21 are flipped NonSecure
 * (same handoff as the beacon). */
#define TIKU_FLPR_CMD_RXPROBE     6u
/* Connection controller (L6 F-L6.1 step 1): advertise the connectable PDU
 * in a2f_buf (tiku_flpr_conn_t), capture the CONNECT_IND, publish it in the
 * conn_* fields (step 1a); step 1b then holds the link.  Same NS handoff. */
#define TIKU_FLPR_CMD_CONN_ADV    7u
#define TIKU_FLPR_CMD_CONN_STOP   8u
#define TIKU_FLPR_RSP_PARKED  1u
#define TIKU_FLPR_RSP_PULSE_DONE 2u
#define TIKU_FLPR_RSP_BEACON_STOPPED 3u

typedef struct {
    uint32_t half_cycles;           /* FLPR cycles per half-period (128/us) */
    uint32_t edges;                 /* number of transitions to emit        */
} tiku_flpr_pulse_t;

typedef struct {
    uint32_t interval_ms;
    uint32_t pdu_len;
    uint8_t  pdu[48];               /* [S0][LEN][S1][payload...] layout     */
} tiku_flpr_beacon_t;

#define TIKU_FLPR_VIO_BIT     7u    /* VIO bit 7 == P2.07 == DK LED3       */

#define TIKU_FLPR_SHARED  ((tiku_flpr_shared_t *)TIKU_FLPR_SHARED_ADDR)

#endif /* TIKU_FLPR_IPC_H_ */
