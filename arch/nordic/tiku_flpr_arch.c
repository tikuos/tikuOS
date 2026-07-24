/*
 * Tiku Operating System v0.06
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

#include <arch/nordic/tiku_device_select.h>   /* NRF_VPR00_NS / NRF_MPC00  */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC enable (IRQ 76)      */
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND    */
#include <kernel/timers/tiku_clock.h>          /* pulse wall-clock measure  */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during the long probe */
#include <arch/nordic/flpr/tiku_flpr_ipc.h>
#include <arch/nordic/tiku_crypto_arch.h>      /* AES-ECB for the session key */
#include <arch/nordic/tiku_trng_arch.h>        /* SKDs/IVs entropy            */
#include <string.h>

#define TIKU_NORDIC_IRQ_VPR00  76             /* MDK IRQn enum value       */

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

    /* Doorbell arming must WAIT for the firmware: VPR00's whole VEVIF
     * register half (tasks/events/INTEN, offsets < 0x800) is dead -- reads
     * zero, ignores writes, from bus and probe alike -- until the RISC-V
     * side enables its RT-peripheral interface (keyed VPRNORDICCTRL CSR,
     * the firmware's first act).  Arm once the magic proves it ran.  Only
     * channels 16..22 carry INTEN bits toward this core; the firmware
     * rings channel 16 via its VEVIF EVENTS CSR. */
    {
        uint32_t spin;
        for (spin = 0u; spin < 2000000u; spin++) {
            if (TIKU_FLPR_SHARED->magic == TIKU_FLPR_MAGIC) {
                break;
            }
        }
    }
    NRF_VPR00_NS->EVENTS_TRIGGERED[16] = 0u;
    NRF_VPR00_NS->INTENSET = (1u << 16);
    tiku_nordic_nvic_enable(TIKU_NORDIC_IRQ_VPR00);
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

/*---------------------------------------------------------------------------*/
/* Mailbox IPC (doorbelled flpr->app, polled app->flpr)                       */
/*---------------------------------------------------------------------------*/

/* Last flpr->app message, captured IN the doorbell ISR: a nonzero
 * reply_seq is therefore proof the whole interrupt path ran (VPR event ->
 * INTPEND -> NVIC 76 -> vector -> copy), not just that shared memory
 * changed. */
static uint8_t  flpr_reply[TIKU_FLPR_MSG_CAP];
static volatile uint32_t flpr_reply_len;
static volatile uint32_t flpr_reply_seq;

void tiku_nordic_flpr_isr(void)
{
    uint32_t len = TIKU_FLPR_SHARED->f2a_len;

    NRF_VPR00_NS->EVENTS_TRIGGERED[16] = 0u;
    if (len > TIKU_FLPR_MSG_CAP) {
        len = TIKU_FLPR_MSG_CAP;
    }
    memcpy(flpr_reply, (const void *)TIKU_FLPR_SHARED->f2a_buf, len);
    flpr_reply_len = len;
    flpr_reply_seq++;
}

int tiku_flpr_arch_send(const void *data, uint32_t len)
{
    if (len > TIKU_FLPR_MSG_CAP || !tiku_flpr_arch_running()) {
        return -1;
    }
    memcpy((void *)TIKU_FLPR_SHARED->a2f_buf, data, len);
    TIKU_FLPR_SHARED->a2f_len = len;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->a2f_seq = TIKU_FLPR_SHARED->a2f_seq + 1u;
    return 0;
}

/* Polled pull: the doorbell (VPR00 VEVIF half, registers < 0x800) is
 * inert on this silicon from BOTH cores by every documented mechanism
 * tried -- CSR EVENTS trigger, MMIO event write on a real channel
 * (16..22), INTENSET from the M33 (S and NS alias, before/after CPURUN,
 * after firmware-alive) and INTENSET from the VPR itself, with and
 * without the keyed RT-periph enable.  Reads return zero, writes drop,
 * probe included; INITPC/CPURUN (>= 0x800) work throughout.  Until that
 * riddle cracks (tracked in kintsugi/flpr_plan.md), consumers pull: this
 * checks the mailbox seq and captures exactly like the ISR would.  The
 * ISR stays wired -- if the doorbell ever fires it simply wins the race. */
static uint32_t flpr_pulled_seq;

void tiku_flpr_arch_poll(void)
{
    uint32_t seq = TIKU_FLPR_SHARED->f2a_seq;

    if (seq != flpr_pulled_seq) {
        flpr_pulled_seq = seq;
        tiku_nordic_flpr_isr();
    }
}

uint32_t tiku_flpr_arch_reply_seq(void)
{
    return flpr_reply_seq;
}

uint32_t tiku_flpr_arch_reply(void *out, uint32_t cap)
{
    uint32_t len = flpr_reply_len;

    if (len > cap) {
        len = cap;
    }
    memcpy(out, flpr_reply, len);
    return len;
}

/*---------------------------------------------------------------------------*/
/* Pulse engine (F3): command the waveform, verify it on the same pad        */
/*---------------------------------------------------------------------------*/

int tiku_flpr_arch_pulse(uint32_t period_us, uint32_t edges,
                         uint32_t *measured, uint32_t *ms)
{
    volatile tiku_flpr_pulse_t *req =
        (volatile tiku_flpr_pulse_t *)TIKU_FLPR_SHARED->a2f_buf;
    uint32_t mask = 1u << TIKU_FLPR_VIO_BIT;
    uint32_t count = 0u, prev, cur, spin;
    tiku_clock_time_t t0 = tiku_clock_time();

    if (!tiku_flpr_arch_running() ||
        period_us < 10u || period_us > 100000u ||
        edges == 0u || edges > 100000u ||
        (period_us * edges) > 6000000u) {      /* <= ~3 s of waveform      */
        return -1;
    }

    /* Hand P2.07 (LED3) to the VPR's fast I/O and keep OUR input buffer
     * connected: the pin's pad state stays readable through P2.IN, which
     * is what makes the soft peripheral independently verifiable without
     * any external instrument. */
    NRF_P2_S->PIN_CNF[TIKU_FLPR_VIO_BIT] =
        (1u << 28) |                           /* CTRLSEL = VPR            */
        (0u << 1);                             /* INPUT = Connect          */

    req->half_cycles = period_us * 64u;        /* 128 cycles/us, half duty */
    req->edges = edges;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->rsp = 0u;
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_PULSE;

    /* Sample the pad while the firmware runs the pattern.  The M33 poll
     * loop runs at multi-MHz sample rates, far above any period this API
     * accepts, so no transition is missed.  Exit on the firmware's DONE
     * plus the loop's own generous spin bound. */
    prev = NRF_P2_S->IN & mask;
    for (spin = 0u; spin < 20000000u; spin++) {
        cur = NRF_P2_S->IN & mask;
        if (cur != prev) {
            count++;
            prev = cur;
        }
        if (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_PULSE_DONE) {
            break;
        }
    }
    TIKU_FLPR_SHARED->rsp = 0u;
    if (measured != (uint32_t *)0) {
        *measured = count;
    }
    if (ms != (uint32_t *)0) {
        *ms = (uint32_t)((tiku_clock_time_t)(tiku_clock_time() - t0))
              * 1000u / (uint32_t)TIKU_CLOCK_SECOND;
    }
    return (spin < 20000000u) ? 0 : -2;       /* -2: firmware never DONE  */
}

/*---------------------------------------------------------------------------*/
/* Beacon offload (F4)                                                       */
/*---------------------------------------------------------------------------*/

/* RADIO (SPU10 slot 10) and UARTE21 (SPU20 slot 7) are both
 * SECUREMAPPING=UserSelectable.  For the offload window they are flipped
 * NonSecure -- SECATTR (bit 4) and the separate DMA attribute (bit 5) --
 * so the non-secure FLPR master can drive them and the radio's EasyDMA
 * can read the PDU from the NS carve.  The M33 configures all radio
 * link-config registers BEFORE the flip and does not touch either
 * peripheral while flipped (the facade blocks scans during offload);
 * stop() flips them back to Secure. */
static void flpr_radio_ns(int on)
{
    if (on) {
        NRF_SPU10_S->PERIPH[10].PERM &= ~((1u << 4) | (1u << 5));
        NRF_SPU20_S->PERIPH[7].PERM  &= ~((1u << 4) | (1u << 5));
    } else {
        NRF_SPU10_S->PERIPH[10].PERM |= (1u << 4) | (1u << 5);
        NRF_SPU20_S->PERIPH[7].PERM  |= (1u << 4) | (1u << 5);
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

int tiku_flpr_arch_beacon(const uint8_t *pdu, uint32_t len,
                          uint32_t interval_ms)
{
    volatile tiku_flpr_beacon_t *b =
        (volatile tiku_flpr_beacon_t *)TIKU_FLPR_SHARED->a2f_buf;
    uint32_t i;

    if (!tiku_flpr_arch_running() || len > sizeof(b->pdu) ||
        interval_ms == 0u) {
        return -1;
    }
    flpr_radio_ns(1);
    for (i = 0u; i < len; i++) {
        b->pdu[i] = pdu[i];
    }
    b->pdu_len = len;
    b->interval_ms = interval_ms;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_BEACON;
    return 0;
}

void tiku_flpr_arch_beacon_stop(void)
{
    uint32_t spin;

    TIKU_FLPR_SHARED->rsp = 0u;
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_BEACON_STOP;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (spin = 0u; spin < 2000000u; spin++) {
        if (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_BEACON_STOPPED) {
            break;
        }
    }
    TIKU_FLPR_SHARED->rsp = 0u;
    flpr_radio_ns(0);
}

uint32_t tiku_flpr_arch_beacon_bursts(void)
{
    return TIKU_FLPR_SHARED->beacon_bursts;
}

/*---------------------------------------------------------------------------*/
/* RX probe (L6 F-L6.1 step 0): does the FLPR's RADIO RX work?                */
/*---------------------------------------------------------------------------*/

int tiku_flpr_arch_rxprobe(uint32_t *addr_evts, uint32_t *crcok_evts,
                           uint8_t *first, uint32_t cap, uint32_t *flen)
{
    uint32_t spin, n;

    if (!tiku_flpr_arch_running()) {
        return -1;
    }
    TIKU_FLPR_SHARED->rx_done = 0u;
    flpr_radio_ns(1);                          /* RADIO+UARTE21 -> NonSecure */
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_RXPROBE;

    /* The probe is internally bounded (~4-5 s of listening); block until it
     * publishes rx_done, kicking the watchdog so the long listen survives. */
    for (spin = 0u; spin < 400000000u; spin++) {
        if (TIKU_FLPR_SHARED->rx_done != 0u) {
            break;
        }
        if ((spin & 0xFFFFFu) == 0u) {
            tiku_watchdog_kick();
        }
    }
    flpr_radio_ns(0);                          /* reclaim secure alias       */

    if (TIKU_FLPR_SHARED->rx_done == 0u) {
        return -2;
    }
    if (addr_evts != (uint32_t *)0) {
        *addr_evts = TIKU_FLPR_SHARED->rx_addr_evts;
    }
    if (crcok_evts != (uint32_t *)0) {
        *crcok_evts = TIKU_FLPR_SHARED->rx_crcok_evts;
    }
    n = TIKU_FLPR_SHARED->rx_first_len;
    if (n > cap) {
        n = cap;
    }
    if (first != (uint8_t *)0) {
        memcpy(first, (const void *)TIKU_FLPR_SHARED->rx_first, n);
    }
    if (flen != (uint32_t *)0) {
        *flen = n;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Connection controller (L6 F-L6.1 step 1a): FLPR advertises + captures     */
/*---------------------------------------------------------------------------*/

int tiku_flpr_arch_conn_capture(const uint8_t *adv, uint32_t adv_len,
                                const uint8_t *addr,
                                tiku_flpr_conn_info_t *out)
{
    volatile tiku_flpr_conn_t *in =
        (volatile tiku_flpr_conn_t *)TIKU_FLPR_SHARED->a2f_buf;
    uint32_t i, spin, st;

    if (!tiku_flpr_arch_running() || adv_len > sizeof(in->adv)) {
        return -1;
    }
    TIKU_FLPR_SHARED->conn_state = 0u;
    flpr_radio_ns(1);                          /* RADIO+UARTE21 -> NonSecure */
    in->adv_len = adv_len;
    for (i = 0u; i < 6u; i++) {
        in->addr[i] = addr[i];
    }
    for (i = 0u; i < adv_len; i++) {
        in->adv[i] = adv[i];
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_ADV;

    /* Block until the FLPR connects (1) or gives up (2); WDT-kicked. */
    for (spin = 0u; spin < 400000000u; spin++) {
        st = TIKU_FLPR_SHARED->conn_state;
        if (st == 1u || st == 2u) {
            break;
        }
        if ((spin & 0xFFFFFu) == 0u) {
            tiku_watchdog_kick();
        }
    }

    if (TIKU_FLPR_SHARED->conn_state != 1u) {
        flpr_radio_ns(0);                      /* gave up: reclaim secure    */
        return -2;
    }
    /* Connected: the FLPR is now HOLDING the link (step 1b) and still owns
     * the NonSecure RADIO -- do NOT flip it back here; conn_stop() does that
     * once the coprocessor has left its hold loop. */
    if (out != (tiku_flpr_conn_info_t *)0) {
        out->aa       = TIKU_FLPR_SHARED->conn_aa;
        out->crcinit  = TIKU_FLPR_SHARED->conn_crcinit;
        out->interval = TIKU_FLPR_SHARED->conn_interval;
        out->timeout  = TIKU_FLPR_SHARED->conn_timeout;
        out->hop      = TIKU_FLPR_SHARED->conn_hop;
        out->winsize  = TIKU_FLPR_SHARED->conn_winsize;
    }
    return 0;
}

/* 1 while the FLPR is holding a live link (step 1b). */
int tiku_flpr_arch_conn_active(void)
{
    return (TIKU_FLPR_SHARED->conn_state == 1u) ? 1 : 0;
}

uint32_t tiku_flpr_arch_conn_state(void)
{
    return TIKU_FLPR_SHARED->conn_state;
}

/* Connection events the FLPR has serviced (rising = link alive). */
uint32_t tiku_flpr_arch_conn_events(void)
{
    return TIKU_FLPR_SHARED->conn_events;
}

/* Phase E: peer + our address (from the CONNECT_IND) for SMP f5/f6.  Copies
 * InitA (central = A) and AdvA (us = B); returns the type bitfield (bit0
 * InitA, bit1 AdvA; 1 = random).  Valid once conn_active(). */
uint8_t tiku_flpr_arch_conn_addrs(uint8_t inita[6], uint8_t adva[6])
{
    int i;
    for (i = 0; i < 6; i++) {
        if (inita != (uint8_t *)0) {
            inita[i] = TIKU_FLPR_SHARED->conn_inita[i];
        }
        if (adva != (uint8_t *)0) {
            adva[i] = TIKU_FLPR_SHARED->conn_adva[i];
        }
    }
    return TIKU_FLPR_SHARED->conn_addr_types;
}

/* Phase E3: last enc_req_seq we derived a session key for. */
static uint32_t flpr_enc_serviced;

/* Service an LL_ENC_REQ the FLPR forwarded: generate our SKDs/IVs, derive
 * SK = e(LTK, SKDm||SKDs) + IV = IVm||IVs, publish them, and release the FLPR
 * to send LL_ENC_RSP.  Returns 1 the (first) call that services a request. */
int tiku_flpr_arch_enc_service(const uint8_t ltk[16])
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    uint8_t  skd[16], sk[16], ivs[4];
    uint32_t req = sh->enc_req_seq;
    int i;

    if (req == flpr_enc_serviced || req == 0u || ltk == (const uint8_t *)0) {
        return 0;                                /* no new request           */
    }
    for (i = 0; i < 8; i++) {                    /* SKD = SKDm || SKDs        */
        skd[i] = sh->enc_skdm[i];
    }
    tiku_trng_arch_init();
    (void)tiku_trng_arch_read_bytes(&skd[8], 8); /* our SKDs (MSO half)       */
    (void)tiku_trng_arch_read_bytes(ivs, 4);     /* our IVs                   */
    (void)tiku_crypto_arch_aes_ecb(0, ltk, 16u, skd, sk);   /* SK = e(LTK,SKD)*/
    for (i = 0; i < 8; i++) {
        sh->enc_skds[i] = skd[8 + i];
    }
    for (i = 0; i < 16; i++) {
        sh->enc_sk[i] = sk[i];
    }
    for (i = 0; i < 4; i++) {
        sh->enc_ivs[i] = ivs[i];
        sh->enc_iv[i] = sh->enc_ivm[i];
        sh->enc_iv[4 + i] = ivs[i];
    }
    sh->enc_rsp_seq = req;                        /* release LL_ENC_RSP        */
    flpr_enc_serviced = req;
    return 1;
}

/* Copy the derived session key (valid once enc_service() has returned 1). */
void tiku_flpr_arch_enc_sk(uint8_t sk[16])
{
    int i;
    for (i = 0; i < 16; i++) {
        sk[i] = TIKU_FLPR_SHARED->enc_sk[i];
    }
}

/* Copy the session IV = IVm||IVs (valid once enc_service() has returned 1). */
void tiku_flpr_arch_enc_iv(uint8_t iv[8])
{
    int i;
    for (i = 0; i < 8; i++) {
        iv[i] = TIKU_FLPR_SHARED->enc_iv[i];
    }
}

/* Phase F1: negotiated DLE max LL payload (0 until LL_LENGTH completes). */
uint32_t tiku_flpr_arch_dle_max(void)
{
    return TIKU_FLPR_SHARED->dle_max;
}

/* Phase F2: current PHY (0 = 1M, 1 = 2M); @p at_evt = conn_events count when
 * the FLPR switched, so the caller can measure survival on the new PHY. */
uint32_t tiku_flpr_arch_conn_phy(uint32_t *at_evt)
{
    if (at_evt != (uint32_t *)0) {
        *at_evt = TIKU_FLPR_SHARED->conn_phy_evt;
    }
    return TIKU_FLPR_SHARED->conn_phy;
}

/* F2 bisect telemetry (radioleft.md H1): the FLPR's RADIO->MODE readback at
 * the switch plus its post-switch ADDRESS/CRCOK counts.  mode != 4 means the
 * 2M write never latched; mode == 4 with addr == 0 means the receiver
 * genuinely hears nothing on 2M. */
void tiku_flpr_arch_conn_phy_diag(uint32_t *mode, uint32_t *addr,
                                  uint32_t *crcok)
{
    if (mode != (uint32_t *)0) {
        *mode = TIKU_FLPR_SHARED->conn_phy_mode;
    }
    if (addr != (uint32_t *)0) {
        *addr = TIKU_FLPR_SHARED->conn_phy_addr;
    }
    if (crcok != (uint32_t *)0) {
        *crcok = TIKU_FLPR_SHARED->conn_phy_crcok;
    }
}

/* Phase A telemetry: LL updates the FLPR applied this connection. */
uint32_t tiku_flpr_arch_conn_updates(uint32_t *chan_map, uint32_t *conn_upd)
{
    uint32_t cm = TIKU_FLPR_SHARED->conn_cm;
    uint32_t cu = TIKU_FLPR_SHARED->conn_cu;
    if (chan_map != (uint32_t *)0) {
        *chan_map = cm;
    }
    if (conn_upd != (uint32_t *)0) {
        *conn_upd = cu;
    }
    return cm + cu;
}

/* Anchored-RX telemetry: RADIO-off + RX-wait loop iterations, duty %. */
uint32_t tiku_flpr_arch_conn_anchor(uint32_t *gap_off_it, uint32_t *rxon_it)
{
    uint32_t gap  = TIKU_FLPR_SHARED->conn_gap;
    uint32_t rxon = TIKU_FLPR_SHARED->conn_rxon;
    uint32_t period;

    if (gap_off_it != 0) {
        *gap_off_it = gap;
    }
    if (rxon_it != 0) {
        *rxon_it = rxon;
    }
    if (gap == 0u) {
        return 100u;                             /* continuous RX             */
    }
    period = gap + rxon;
    return (period != 0u) ? (uint32_t)(((uint64_t)rxon * 100u) / period)
                          : 100u;
}

/* Ask the FLPR to leave its hold loop, wait for it, then reclaim the
 * RADIO for the secure alias.  Safe if not connected. */
void tiku_flpr_arch_conn_stop(void)
{
    uint32_t spin;

    if (TIKU_FLPR_SHARED->conn_state == 1u) {
        TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_STOP;
        __asm__ volatile ("dsb 0xF" ::: "memory");
        for (spin = 0u; spin < 400000000u; spin++) {
            if (TIKU_FLPR_SHARED->conn_state != 1u) {
                break;                          /* left the hold loop        */
            }
            if ((spin & 0xFFFFFu) == 0u) {
                tiku_watchdog_kick();
            }
        }
    }
    flpr_radio_ns(0);                          /* reclaim secure alias       */
}

/*---------------------------------------------------------------------------*/
/* NUS byte pipe over the mailbox (L6 F-L6.2) -- facade primitives (F-L6.3)  */
/*---------------------------------------------------------------------------*/

/* Non-blocking advertise+hold: flip NS, ship the ADV PDU, return.  The FLPR
 * advertises then holds autonomously; poll conn_active() for the link. */
int tiku_flpr_arch_conn_start(const uint8_t *adv, uint32_t adv_len,
                              const uint8_t *addr)
{
    volatile tiku_flpr_conn_t *in =
        (volatile tiku_flpr_conn_t *)TIKU_FLPR_SHARED->a2f_buf;
    uint32_t i;

    if (!tiku_flpr_arch_running() || adv_len > sizeof(in->adv)) {
        return -1;
    }
    TIKU_FLPR_SHARED->conn_state = 0u;
    flpr_radio_ns(1);
    in->adv_len = adv_len;
    for (i = 0u; i < 6u; i++) {
        in->addr[i] = addr[i];
    }
    for (i = 0u; i < adv_len; i++) {
        in->adv[i] = adv[i];
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_ADV;
    return 0;
}

/* 1 once the central subscribed to NUS TX notifications (CCCD written). */
int tiku_flpr_arch_conn_subscribed(void)
{
    return (TIKU_FLPR_SHARED->conn_sub != 0u) ? 1 : 0;
}

/* Phase B: the mailbox carries L2CAP frames during a connection.  conn_recv
 * pops a received frame the controller forwarded; conn_send hands the host's
 * response/notification frame back for TX. */
static uint32_t flpr_nus_rx_seen;

/* Peek: is a received L2CAP frame waiting (no consume)? */
int tiku_flpr_arch_conn_rx_ready(void)
{
    return (TIKU_FLPR_SHARED->f2a_seq != flpr_nus_rx_seen) ? 1 : 0;
}

/* Pop the L2CAP fragment the controller forwarded (f2a); 0 if none new.
 * @p llid (if non-NULL) gets the fragment boundary: 2 = start of an L2CAP
 * PDU, 1 = continuation (the host recombines). */
int tiku_flpr_arch_conn_recv(uint8_t *buf, uint32_t cap, uint8_t *llid)
{
    uint32_t seq = TIKU_FLPR_SHARED->f2a_seq;
    uint32_t n;

    if (seq == flpr_nus_rx_seen || buf == (uint8_t *)0 || cap == 0u) {
        return 0;
    }
    flpr_nus_rx_seen = seq;
    n = TIKU_FLPR_SHARED->f2a_len;
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, (const void *)TIKU_FLPR_SHARED->f2a_buf, n);
    if (llid != (uint8_t *)0) {
        *llid = (uint8_t)TIKU_FLPR_SHARED->f2a_llid;
    }
    return (int)n;
}

/* Hand one L2CAP fragment to the controller for TX (a2f mailbox), tagged with
 * @p llid (2 = start of an L2CAP PDU, 1 = continuation).  Non-blocking and
 * flow-controlled: if the previous fragment has not been consumed yet
 * (a2f_ack != a2f_seq) it returns -2 so the caller retries -- a fast host
 * cannot overwrite an unsent fragment.  Returns bytes queued, or -1 bad. */
int tiku_flpr_arch_conn_send(const uint8_t *buf, uint32_t len, uint8_t llid)
{
    volatile tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    uint32_t i;

    if (!tiku_flpr_arch_conn_active() || buf == (const uint8_t *)0) {
        return -1;
    }
    if (sh->a2f_ack != sh->a2f_seq) {
        return -2;                             /* TX slot busy: retry later  */
    }
    if (len > 32u) {
        len = 32u;                             /* one fragment / data PDU    */
    }
    for (i = 0u; i < len; i++) {
        sh->a2f_buf[i] = buf[i];
    }
    sh->a2f_len = len;
    sh->a2f_llid = (llid == 1u) ? 1u : 2u;     /* default to start           */
    __asm__ volatile ("dsb 0xF" ::: "memory");
    sh->a2f_seq = sh->a2f_seq + 1u;
    return (int)len;
}

#endif /* TIKU_FLPR_ENABLE */
