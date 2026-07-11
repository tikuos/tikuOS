/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_main.c - FLPR coprocessor firmware (F1: liveness heartbeat).
 *
 * Runs on the nRF54L15's VPR RISC-V core out of the SRAM carve.  F1 scope
 * is deliberately tiny: stamp the magic (proves the crt reached C), then
 * bump the heartbeat forever (proves steady-state life, visible to the
 * app core through /sys/flpr/heartbeat).  The pacing loop keeps the bump
 * rate in the ~kHz class so the counter is obviously moving yet reads
 * stay meaningful across a slow console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/flpr/tiku_flpr_ipc.h>
#include <arch/nordic/mdk/nrf54l15.h>   /* plain-C register structs: the
                                         * same MDK headers the M33 uses
                                         * compile unchanged for RISC-V.
                                         * This core is a NON-SECURE bus
                                         * master, so all access is via the
                                         * _NS aliases -- and only works on
                                         * peripherals the app core flipped
                                         * to NonSecure first (SPU). */

/* Outward doorbell: EVENTS_TRIGGERED[n] cannot be set from the bus (MMIO
 * writes are ignored -- measured); the VPR raises them through its VEVIF
 * CSR (VPRCSR_NORDIC_EVENTS = 0x7E2, bit n = channel n).  Only channels
 * 16..22 have INTEN bits toward the app core, so the app doorbell is
 * channel 16. */
#define FLPR_DOORBELL_CH   16u

static inline void flpr_doorbell_to_app(void)
{
    /* VEVIF EVENTS CSR (0x7E2), the documented outward-doorbell mechanism.
     * NOTE: bus MMIO on the VPR00 VEVIF register space is NOT an option
     * from this core -- it bus-faults the VPR (no trap handler => dead
     * before main; measured).  The CSR write is internal and safe either
     * way; whether the app core observes it is the open doorbell riddle
     * (see tiku_flpr_arch.c). */
    __asm__ volatile ("csrs 0x7E2, %0" :: "r"(1u << FLPR_DOORBELL_CH));
}

/* Pulse engine (F3): 50%-duty waveform on the VIO pin.
 *
 * VIO CSRs give the VPR single-cycle pin access without touching the AHB
 * GPIO block (which, as a non-secure master, it could not reach anyway):
 * DIR=0xBC1, OUT=0xBC0.
 *
 * Pacing is a calibrated down-count, not mcycle: the VPR's cycle counter
 * proved unreliable as a timebase (waits stalled even with mcountinhibit
 * cleared -- first toggle landed, second wait hung).  A busy loop is
 * fully deterministic here since this core services no interrupts.
 * FLPR_PACE_DIV converts half-period CYCLES to loop iterations and is
 * calibrated against the M33's wall clock (see /sys/flpr/pulse ms=). */
#define FLPR_PACE_DIV  10u

static void flpr_pulse(const tiku_flpr_pulse_t *p)
{
    uint32_t mask = 1u << TIKU_FLPR_VIO_BIT;
    uint32_t i;
    volatile uint32_t w;

    __asm__ volatile ("csrs 0xBC1, %0" :: "r"(mask));   /* DIR: output    */
    for (i = 0u; i < p->edges; i++) {
        if (i & 1u) {
            __asm__ volatile ("csrc 0xBC0, %0" :: "r"(mask));
        } else {
            __asm__ volatile ("csrs 0xBC0, %0" :: "r"(mask));
        }
        for (w = 0u; w < p->half_cycles / FLPR_PACE_DIV; w++) {
        }
    }
    __asm__ volatile ("csrc 0xBC0, %0" :: "r"(mask));   /* park low       */
}

/* Beacon offload (F4): one 3-channel BLE advertising burst per interval,
 * paced by the calibrated down-count, with the app core fully asleep.
 *
 * Division of labour: the M33 configured every link-config register
 * (MODE/PCNF/CRC/access-address/TXPOWER/SHORTS, erratum-49 S1 layout)
 * while the RADIO was still secure, holds CONSTLAT for the session
 * (erratum 20), and flipped RADIO+UARTE21 to NonSecure so this core can
 * reach them.  Per burst this firmware only replays the proven per-burst
 * sequence: the burst-spanning UARTE21 clock kick, then per channel
 * FREQUENCY/DATAWHITE/PACKETPTR + TXEN + poll DISABLED.  The PDU lives in
 * this core's own .bss (the NS carve), which the radio's now-non-secure
 * EasyDMA can read. */
static const uint8_t beacon_freq[3] = { 2u, 26u, 80u };
static const uint8_t beacon_widx[3] = { 37u, 38u, 39u };
static uint8_t beacon_pdu[48] __attribute__((aligned(4)));
static uint8_t beacon_kick[16];
static uint32_t beacon_pace_iters;      /* interval in pace iterations     */
static uint32_t beacon_on;

static void flpr_hfclk_kick(void)
{
    NRF_UARTE_Type *u = NRF_UARTE21_NS;

    if (u->ENABLE == 0u) {
        u->BAUDRATE = 0x01D60000u;             /* 115200: ~1.4 ms / 16 B   */
        u->ENABLE   = 8u;
    }
    u->EVENTS_DMA.TX.END  = 0u;
    u->DMA.TX.PTR         = (uint32_t)beacon_kick;
    u->DMA.TX.MAXCNT      = sizeof(beacon_kick);
    u->TASKS_DMA.TX.START = 1u;                /* concurrent, spans burst  */
}

static void flpr_beacon_burst(void)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t c, spin;

    flpr_hfclk_kick();
    for (c = 0u; c < 3u; c++) {
        r->FREQUENCY = beacon_freq[c];
        r->DATAWHITE = 0x00890000u | (0x40u | beacon_widx[c]);
        r->PACKETPTR = (uint32_t)beacon_pdu;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_READY    = 0u;
        r->TASKS_TXEN = 1u;
        for (spin = 0u; spin < 1000000u; spin++) {
            if (r->EVENTS_DISABLED != 0u) {
                break;
            }
        }
    }
}

/* Echo service: consume an app->flpr message, mirror it back, ring. */
static void flpr_echo_pump(tiku_flpr_shared_t *sh, uint32_t *last_seq)
{
    uint32_t seq = sh->a2f_seq;
    uint32_t len, i;

    if (seq == *last_seq) {
        return;
    }
    *last_seq = seq;
    len = sh->a2f_len;
    if (len > TIKU_FLPR_MSG_CAP) {
        len = TIKU_FLPR_MSG_CAP;
    }
    for (i = 0u; i < len; i++) {
        sh->f2a_buf[i] = sh->a2f_buf[i];
    }
    sh->f2a_len = len;
    sh->f2a_seq = sh->f2a_seq + 1u;
    flpr_doorbell_to_app();
}

void tiku_flpr_main(void)
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    volatile uint32_t pace;
    uint32_t last_seq = 0u;

    /* Enable the VPR's RT-peripheral interface (keyed VPRNORDICCTRL CSR,
     * 0x7C0: NORDICKEY=0x507D<<16 | ENABLERTPERIPH).  Until this runs, the
     * whole VEVIF register half of VPR00 (tasks/events/INTEN, offsets
     * <0x800) is dead from BOTH sides: bus reads return zero and writes
     * are ignored -- which looked exactly like \"INTENSET does not stick\"
     * from the app core.  Host-side control (INITPC/CPURUN, >=0x800) works
     * regardless. */
    __asm__ volatile ("csrw 0x7C0, %0" :: "r"((0x507Du << 16) | 1u));

    /* Un-inhibit the machine counters (mcountinhibit, 0x320): mcycle is
     * the pulse engine's timebase and RISC-V cores may reset with the
     * counters gated -- a constant mcycle turns the pacing wait into an
     * infinite loop. */
    __asm__ volatile ("csrw 0x320, zero");

    sh->heartbeat = 0u;
    sh->magic = TIKU_FLPR_MAGIC;

    for (;;) {
        /* Cooperative park/resume (see tiku_flpr_ipc.h): heartbeat freezes
         * while parked; RESUME returns to this loop.  The parked wait is a
         * paced busy-poll for now -- a WFI here could never wake, since no
         * interrupt source is wired to the VPR yet (the F2 doorbell will
         * turn parking into genuine sleep). */
        if (sh->cmd == TIKU_FLPR_CMD_PARK) {
            sh->rsp = TIKU_FLPR_RSP_PARKED;
            while (sh->cmd != TIKU_FLPR_CMD_RESUME) {
                for (pace = 0u; pace < 8000u; pace++) {
                }
            }
            sh->cmd = 0u;
            sh->rsp = 0u;
        }
        if (sh->cmd == TIKU_FLPR_CMD_PULSE) {
            tiku_flpr_pulse_t p;
            p.half_cycles = ((const volatile tiku_flpr_pulse_t *)
                             sh->a2f_buf)->half_cycles;
            p.edges = ((const volatile tiku_flpr_pulse_t *)
                       sh->a2f_buf)->edges;
            sh->cmd = 0u;
            flpr_pulse(&p);                    /* blocking, bounded        */
            sh->rsp = TIKU_FLPR_RSP_PULSE_DONE;
        }
        if (sh->cmd == TIKU_FLPR_CMD_BEACON) {
            const volatile tiku_flpr_beacon_t *b =
                (const volatile tiku_flpr_beacon_t *)sh->a2f_buf;
            uint32_t i, n = b->pdu_len;
            if (n > sizeof(beacon_pdu)) {
                n = sizeof(beacon_pdu);
            }
            for (i = 0u; i < n; i++) {
                beacon_pdu[i] = b->pdu[i];
            }
            /* interval_ms -> pace iterations: 128000 cycles/ms / DIV. */
            beacon_pace_iters = b->interval_ms * (128000u / FLPR_PACE_DIV);
            sh->beacon_bursts = 0u;
            beacon_on = 1u;
            sh->cmd = 0u;
        }
        if (sh->cmd == TIKU_FLPR_CMD_BEACON_STOP) {
            beacon_on = 0u;
            sh->cmd = 0u;
            sh->rsp = TIKU_FLPR_RSP_BEACON_STOPPED;
        }
        if (beacon_on) {
            volatile uint32_t w;
            flpr_beacon_burst();
            sh->beacon_bursts = sh->beacon_bursts + 1u;
            sh->heartbeat = sh->heartbeat + 1u;
            /* Interval pacing; broken into slices so STOP/PARK commands
             * are honoured within ~a millisecond. */
            for (w = 0u; w < beacon_pace_iters; w += 12800u) {
                volatile uint32_t s;
                for (s = 0u; s < 12800u; s++) {
                }
                if (sh->cmd != 0u) {
                    break;
                }
            }
            continue;
        }
        flpr_echo_pump(sh, &last_seq);
        sh->heartbeat = sh->heartbeat + 1u;
        for (pace = 0u; pace < 8000u; pace++) {
        }
    }
}
