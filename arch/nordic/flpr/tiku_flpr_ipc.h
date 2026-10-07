/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_ipc.h - shared-memory layout between the M33 app core and FLPR.
 *
 * Included by both the arm-none-eabi and riscv-none-elf builds, so it stays
 * plain C99 plus <stdint.h>.  The shared page is the last kilobyte of the
 * FLPR SRAM carve; the layout is below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Carve layout (the app linker scripts and tiku_flpr.ld match it):
 *   0x2003C000  FLPR .text/.rodata/.data/.bss   (image, loader-placed)
 *   ...         FLPR stack (grows down from the shared page)
 *   0x2003FC00  tiku_flpr_shared_t              (this header)
 *
 * SRAM is uncached for both masters.  A sender writes the payload, then a
 * barrier, then the sequence word or flag; every field is volatile.
 */


#ifndef TIKU_FLPR_IPC_H_
#define TIKU_FLPR_IPC_H_

#include <stdint.h>

/* Carve geometry used by the loader.  The MEMORY regions in the app-core
 * scripts (nrf54l15.ld, nrf54lm20a.ld) and tiku_flpr.ld repeat these values
 * and must match. */
#define TIKU_FLPR_RAM_BASE   0x2003C000u
#define TIKU_FLPR_RAM_SIZE   0x4000u                     /* 16 KB carve    */
#define TIKU_FLPR_SHARED_ADDR (TIKU_FLPR_RAM_BASE + TIKU_FLPR_RAM_SIZE \
                               - 0x400u)                 /* top 1 KB       */

/** @brief Written to .magic by tiku_flpr_main() once the payload reaches C. */
#define TIKU_FLPR_MAGIC      0x464C5052u                 /* 'FLPR'         */
/** @brief Written to .magic by the trap handler: the payload has faulted,
 *         recorded why, and waits for TIKU_FLPR_CMD_RESTART. */
#define TIKU_FLPR_MAGIC_FAULT 0x464C5021u                /* 'FLP!'         */

/**
 * @brief Bytes in each single-slot mailbox, one per direction.
 *
 * The sender fills buf and len, then bumps seq; the receiver acts on a seq
 * change.  The next message overwrites the slot, so the consumer paces the
 * sender (a2f_ack on the connection path).  The page stays within 1 KB.
 */
#define TIKU_FLPR_MSG_CAP  240u

/**
 * @brief Largest LL data-PDU payload the controller offers in LL_LENGTH_RSP.
 *
 * 80 octets carry the 69-byte SMP Public Key and a 68-byte long-read
 * response in one PDU.  It sets the RADIO MAXLEN and bounds the connection
 * RX/TX buffers.
 */
#define TIKU_FLPR_DLE_MAX_OCTETS  80u
/** @brief Air time of that payload on 1M PHY, in us: (octets + 14) * 8, the
 *         14 being preamble 1 + access address 4 + header 2 + MIC 4 + CRC 3.
 *         The formula gives the spec's 2120 us for 251 octets. */
#define TIKU_FLPR_DLE_MAX_TIME    ((TIKU_FLPR_DLE_MAX_OCTETS + 14u) * 8u)
/** @brief Buffer for a whole radio packet: S0 + LENGTH + S1 + payload (up to
 *         MAXLEN) + slack, word-aligned. */
#define TIKU_FLPR_DLE_BUF_SIZE    96u

/** @brief The shared page at TIKU_FLPR_SHARED_ADDR. */
typedef struct {
    volatile uint32_t magic;        /* TIKU_FLPR_MAGIC once main() runs   */
    volatile uint32_t heartbeat;    /* increments while un-parked          */
    volatile uint32_t cmd;          /* app -> flpr command word            */
    volatile uint32_t rsp;          /* flpr -> app response word           */

    /* app -> flpr mailbox */
    volatile uint32_t a2f_seq;
    volatile uint32_t a2f_len;
    volatile uint8_t  a2f_buf[TIKU_FLPR_MSG_CAP];

    /* flpr -> app mailbox (doorbell: VPR EVENTS_TRIGGERED[16] -> IRQ 76) */
    volatile uint32_t f2a_seq;
    volatile uint32_t f2a_len;
    volatile uint8_t  f2a_buf[TIKU_FLPR_MSG_CAP];

    /* Beacon offload: bursts sent since TIKU_FLPR_CMD_BEACON. */
    volatile uint32_t beacon_bursts;

    /* Fault record, written by the trap handler before the fault park.
     * fault_count survives restarts within one launch; the cause and epc
     * describe the most recent trap only. */
    volatile uint32_t fault_count;
    volatile uint32_t fault_cause;      /* mcause                          */
    volatile uint32_t fault_epc;        /* mepc                            */

    /* RX probe results (TIKU_FLPR_CMD_RXPROBE): ADDRESS and CRCOK counts on
     * advertising channel 37, the head of the first CRC-valid packet, and
     * rx_done set to 1 when the probe ends. */
    volatile uint32_t rx_addr_evts;     /* ADDRESS matches in the window   */
    volatile uint32_t rx_crcok_evts;    /* CRC-valid packets               */
    volatile uint32_t rx_done;          /* probe finished                  */
    volatile uint8_t  rx_first[16];     /* head of 1st CRC-valid packet    */
    volatile uint32_t rx_first_len;

    /* Connection state.  The FLPR advertises connectably, captures the
     * CONNECT_IND and holds the link.  conn_state: 0 advertising,
     * 1 connected, 2 gave up or stopped, 3 link ended.  The fields after it
     * are the parsed CONNECT_IND LLData. */
    volatile uint32_t conn_state;
    volatile uint32_t conn_aa;
    volatile uint32_t conn_crcinit;
    volatile uint32_t conn_events;      /* connection events serviced      */
    volatile uint16_t conn_interval;    /* 1.25 ms units                   */
    volatile uint16_t conn_timeout;     /* 10 ms units                     */
    volatile uint16_t conn_winoffset;
    volatile uint8_t  conn_hop;
    volatile uint8_t  conn_winsize;
    volatile uint8_t  conn_chm[5];
    /* Peer identity from the CONNECT_IND, for SMP f5/f6, which bind the LTK
     * to both device addresses.  InitA is the central (initiator, address
     * A); AdvA is local (peripheral, responder, address B).
     * conn_addr_types: bit0 = InitA type, bit1 = AdvA type (1 = random). */
    volatile uint8_t  conn_inita[6];    /* initiator (central) address = A  */
    volatile uint8_t  conn_adva[6];     /* advertiser (local) address   = B  */
    volatile uint8_t  conn_addr_types;  /* bit0 InitA, bit1 AdvA (1=random)  */

    /* LL encryption startup.  The FLPR has no AES, so the M33 derives the
     * session key (SK = e(LTK, SKD) on CRACEN).  On LL_ENC_REQ the FLPR
     * publishes the central's SKDm and IVm and bumps enc_req_seq; the M33
     * fills enc_skds, enc_ivs, enc_sk and enc_iv and bumps enc_rsp_seq; the
     * FLPR then sends LL_ENC_RSP(SKDs, IVs).  The FLPR goes no further: it
     * sends no LL_START_ENC_REQ, never reads enc_sk or enc_iv, and never
     * sets enc_on to 1. */
    volatile uint32_t enc_req_seq;      /* FLPR: LL_ENC_REQ seen (params set) */
    volatile uint32_t enc_rsp_seq;      /* M33: SKDs/IVs/sk/iv ready          */
    volatile uint8_t  enc_skdm[8];      /* FLPR->M33: central's SKD (LSO)     */
    volatile uint8_t  enc_ivm[4];       /* FLPR->M33: central's IV (LSO)      */
    volatile uint8_t  enc_skds[8];      /* M33->FLPR: local SKD (MSO)         */
    volatile uint8_t  enc_ivs[4];       /* M33->FLPR: local IV (MSO)          */
    volatile uint8_t  enc_sk[16];       /* M33: session key                   */
    volatile uint8_t  enc_iv[8];        /* M33: IV = IVm||IVs                 */
    volatile uint32_t enc_on;           /* FLPR: 1 once encryption is active  */

    /* After answering LL_LENGTH_REQ the FLPR publishes the effective max LL
     * payload, min(peer MaxRxOctets, TIKU_FLPR_DLE_MAX_OCTETS) and at least
     * 27; the M33 host sets its L2CAP fragment size from it.  It reads 0
     * until an LL_LENGTH_REQ arrives (27-octet payloads). */
    volatile uint32_t dle_max;          /* FLPR->M33: negotiated max octets   */

    /* PHY update.  The FLPR applies LL_PHY_UPDATE_IND at its Instant
     * (RADIO MODE and PCNF0) and publishes the new PHY and the conn_events
     * count at the switch. */
    volatile uint32_t conn_phy;        /* current PHY: 0 1M, 1 2M, 2 Coded S8 */
    volatile uint32_t conn_phy_evt;    /* conn_events value when PHY applied  */
    /* PHY-switch telemetry: conn_phy_mode is RADIO->MODE read back after the
     * switch (4 = 2M, 5 = Coded); conn_phy_addr and conn_phy_crcok count the
     * ADDRESS and CRCOK events caught while the PHY is not 1M. */
    volatile uint32_t conn_phy_mode;   /* FLPR: MODE readback at the switch   */
    volatile uint32_t conn_phy_addr;   /* FLPR: post-switch ADDRESS events    */
    volatile uint32_t conn_phy_crcok;  /* FLPR: post-switch CRCOK events      */
    volatile uint32_t conn_sub;        /* unused, 0; the host tracks the CCCD */
    volatile uint32_t conn_gap;        /* anchored RX: idle iterations        */
    volatile uint32_t conn_rxon;       /* anchored RX: last RX-wait iters     */
    volatile uint32_t conn_cm;         /* LL_CHANNEL_MAP_UPDATE_INDs applied  */
    volatile uint32_t conn_cu;         /* LL_CONNECTION_UPDATE_INDs applied   */
    volatile uint32_t a2f_ack;         /* last a2f L2CAP fragment the         */
                                       /* controller consumed for TX (==      */
                                       /* a2f_seq means the slot is free)     */
    volatile uint32_t f2a_llid;        /* RX fragment boundary                */
                                       /* (2 = start of L2CAP PDU, 1 = cont)  */
    volatile uint32_t a2f_llid;        /* TX fragment boundary                */
    /* L2CAP transport: while a connection is held the mailbox carries L2CAP
     * fragments ([{len}{CID}payload...] split across data PDUs), not NUS
     * bytes.  RX: each received L2CAP data PDU goes to f2a with f2a_llid
     * (2 start, 1 continuation), doorbelled, for the M33 host to recombine
     * and run ATT/GATT.  TX: the host's fragments arrive on a2f with
     * a2f_llid, paced by a2f_ack; the controller wraps each in a data PDU
     * with that LLID.  The FLPR never parses ATT. */

    /* Compute-only load (TIKU_FLPR_CMD_SPIN): a register-only loop that
     * drives no pin and leaves the radio alone, so its current is the VPR
     * core's.  spin_passes counts the work done. */
    volatile uint32_t spin_iters;      /* M33->FLPR: outer passes requested   */
    volatile uint32_t spin_passes;     /* FLPR->M33: outer passes retired     */

    /* Advertising telemetry.  A central initiating a connection answers an
     * ADV_IND with a CONNECT_IND; a host discovering devices sends a
     * SCAN_REQ and expects a SCAN_RSP at T_IFS.  The counts separate the
     * two. */
    volatile uint32_t adv_tx;          /* ADV_IND PDUs transmitted            */
    volatile uint32_t adv_scanreq;     /* SCAN_REQs addressed to this AdvA    */
    volatile uint32_t adv_scanrsp;     /* SCAN_RSPs transmitted in reply      */
    volatile uint32_t adv_rxother;     /* other CRC-good PDUs in the window   */
    volatile uint32_t adv_tifs;        /* last reply's ADDRESS, TIMER10 ticks */
                                       /* since the request's end             */
} tiku_flpr_shared_t;

/**
 * @brief TIKU_FLPR_CMD_CONN_ADV input, in a2f_buf: the connectable ADV PDU,
 *        the AdvA, and the SCAN_RSP that answers a SCAN_REQ.
 *
 * A scanner's duplicate filter drops a response that repeats the advert
 * byte for byte; rsp_len 0 mirrors the advert anyway.
 */
typedef struct {
    uint32_t adv_len;                   /* bytes in adv[] ([S0][LEN][S1]..) */
    uint8_t  addr[6];                   /* AdvA to match in the CONNECT_IND */
    uint8_t  adv[48];
    uint32_t rsp_len;                   /* bytes in rsp[]; 0 = mirror adv   */
    uint8_t  rsp[48];
    uint32_t txen_ticks;                /* TIMER10 ticks from a SCAN_REQ's  */
                                        /* end to the reply's TXEN; 0 =     */
                                        /* the controller's own figure      */
} tiku_flpr_conn_t;

/*
 * Command words (cmd) and their responses (rsp).  Clearing CPURUN does not
 * halt a running VPR, and setting it again resumes at the current PC, not
 * INITPC, so the image loads once per power-on and is never swapped under a
 * parked core: stop parks the firmware in a polling loop, start resumes it.
 */
/** @brief Park in a polling loop; the firmware answers TIKU_FLPR_RSP_PARKED. */
#define TIKU_FLPR_CMD_PARK    1u
/** @brief Leave the park. */
#define TIKU_FLPR_CMD_RESUME  2u
/**
 * @brief Emit a 50% duty waveform on the VIO pin (bit 7 = P2.07 = LED3).
 *
 * Parameters in a2f_buf as tiku_flpr_pulse_t: `edges` toggles, `half_cycles`
 * FLPR cycles apart.  The app core routes the pin to the VPR
 * (PIN_CNF.CTRLSEL); the firmware answers TIKU_FLPR_RSP_PULSE_DONE.
 */
#define TIKU_FLPR_CMD_PULSE   3u
/**
 * @brief Beacon offload: one 3-channel BLE burst per interval, each with the
 *        UARTE21 HF-clock kick, until TIKU_FLPR_CMD_BEACON_STOP.
 *
 * Parameters in a2f_buf as tiku_flpr_beacon_t.  The app core programs the
 * radio link config while RADIO is secure, makes RADIO and UARTE21
 * non-secure (SPU) and holds CONSTLAT; .beacon_bursts counts the bursts.
 */
#define TIKU_FLPR_CMD_BEACON      4u
/** @brief End beacon mode; the answer is TIKU_FLPR_RSP_BEACON_STOPPED. */
#define TIKU_FLPR_CMD_BEACON_STOP 5u
/**
 * @brief Listen on advertising channel 37 and report in the rx_* fields.
 *
 * The M33 programs the link config (MODE, PCNF, advertising access address,
 * CRC) while RADIO is secure, then makes RADIO and UARTE21 non-secure.
 */
#define TIKU_FLPR_CMD_RXPROBE     6u
/**
 * @brief Advertise the tiku_flpr_conn_t in a2f_buf, capture the CONNECT_IND
 *        into the conn_* fields, then hold the link.
 *
 * Same security handoff as the beacon.
 */
#define TIKU_FLPR_CMD_CONN_ADV    7u
/** @brief Stop advertising or end the held link (conn_state 2 or 3). */
#define TIKU_FLPR_CMD_CONN_STOP   8u
/**
 * @brief Compute-only load: .spin_iters outer passes of a register-only loop.
 *
 * The count goes to .spin_passes and the firmware answers
 * TIKU_FLPR_RSP_SPIN_DONE.  The inner loop touches no pin, radio or shared
 * memory; the M33 times the load on its own clock.
 */
#define TIKU_FLPR_CMD_SPIN        9u
/**
 * @brief Leave the fault park: the trap handler calls tiku_flpr_main() again.
 *
 * Valid only while magic reads TIKU_FLPR_MAGIC_FAULT; .bss is not re-zeroed.
 */
#define TIKU_FLPR_CMD_RESTART    10u
/** @brief Response words the firmware writes to rsp. */
#define TIKU_FLPR_RSP_PARKED  1u
#define TIKU_FLPR_RSP_PULSE_DONE 2u
#define TIKU_FLPR_RSP_BEACON_STOPPED 3u
#define TIKU_FLPR_RSP_SPIN_DONE      4u

/** @brief TIKU_FLPR_CMD_PULSE parameters. */
typedef struct {
    uint32_t half_cycles;           /* FLPR cycles per half-period          */
    uint32_t edges;                 /* number of transitions to emit        */
} tiku_flpr_pulse_t;

/** @brief TIKU_FLPR_CMD_BEACON parameters. */
typedef struct {
    uint32_t pace_iters;            /* cycles divided by 10 per interval */
    uint32_t pdu_len;
    uint8_t  pdu[48];               /* [S0][LEN][S1][payload...] layout     */
} tiku_flpr_beacon_t;

/** @brief The pulse engine's VIO bit: VIO bit 7 == P2.07 == DK LED3. */
#define TIKU_FLPR_VIO_BIT     7u

/** @brief The shared page. */
#define TIKU_FLPR_SHARED  ((tiku_flpr_shared_t *)TIKU_FLPR_SHARED_ADDR)

#endif /* TIKU_FLPR_IPC_H_ */
