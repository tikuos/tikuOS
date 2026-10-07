/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_arch.c - nRF54L FLPR (VPR RISC-V) coprocessor control.
 *
 * Copies the embedded FLPR image into the SRAM carve once per power-on,
 * scrubs the shared IPC page, points VPR00.INITPC at the carve and sets
 * CPURUN.  Later starts resume a parked firmware or restart a faulted one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_flpr_arch.h>

#if TIKU_FLPR_ENABLE

#include <arch/nordic/tiku_device_select.h>   /* NRF_VPR00_NS / NRF_MPC00  */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC enable (IRQ 76)      */
#include <arch/nordic/tiku_cpu_common.h>      /* tiku_nordic_cpu_hz_now()  */
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

/**
 * @brief Make the SRAM carve non-secure R+W+X with MPC00 override region 0.
 *
 * The VPR is a non-secure bus master, and this port's secure-by-default map
 * rejects its fetches: CPURUN reads Running while the core executes nothing.
 * Secure M33 accesses to the carve stay legal.
 */
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

/* Set once the image is loaded; it is not reloaded this power-on.
 * Re-setting CPURUN resumes at the current PC, not INITPC, so code swapped
 * under a parked core runs garbage. */
static uint8_t flpr_booted;

/* Long jobs handed to the firmware that stop its heartbeat: beacon mode
 * (set by tiku_flpr_arch_beacon(), cleared by _beacon_stop()) and a
 * connection advertise or hold (set by _conn_capture() and _conn_start(),
 * cleared by _conn_stop()).  Both clear on a fault restart, which runs the
 * firmware's main() again. */
static uint8_t flpr_beacon_posted;
static uint8_t flpr_conn_posted;

int tiku_flpr_arch_start(void)
{
    uint32_t size = FLPR_IMAGE_SIZE;

    if (size == 0u || size > FLPR_IMAGE_MAX) {
        return -1;
    }

    if (flpr_booted) {
        /* A faulted payload sits in its trap park; CPURUN cannot restart
         * the core, so TIKU_FLPR_CMD_RESTART makes the trap handler call
         * tiku_flpr_main() again. */
        if (TIKU_FLPR_SHARED->magic == TIKU_FLPR_MAGIC_FAULT) {
            uint32_t spin;

            flpr_beacon_posted = 0u;
            flpr_conn_posted = 0u;
            TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_RESTART;
            __asm__ volatile ("dsb 0xF" ::: "memory");
            for (spin = 0u; spin < 2000000u; spin++) {
                if (TIKU_FLPR_SHARED->magic == TIKU_FLPR_MAGIC) {
                    return 0;
                }
            }
            return -1;
        }
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

    /* The doorbell is armed after the firmware's magic appears: VPR00's
     * VEVIF half (tasks, events, INTEN; offsets below 0x800) reads zero and
     * ignores writes, from bus and probe alike, until the firmware enables
     * its RT-peripheral interface (keyed VPRNORDICCTRL CSR).  Only channels
     * 16..22 carry INTEN bits toward this core; the firmware raises channel
     * 16 through its VEVIF EVENTS CSR. */
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

uint32_t tiku_flpr_arch_magic(void)
{
    return flpr_booted ? TIKU_FLPR_SHARED->magic : 0u;
}

void tiku_flpr_arch_stop(void)
{
    /* Cooperative park: ask the firmware to spin in its parked loop and
     * wait (bounded) for the answer.  CPURUN is left alone; see
     * flpr_booted. */
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
     * rest of the power-on (see flpr_booted). */
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

int tiku_flpr_arch_busy(void)
{
    uint32_t st = TIKU_FLPR_SHARED->conn_state;

    /* Beacon mode advances the heartbeat once per burst interval, which
     * tiku_flpr_arch_beacon() bounds only by overflow; a connection
     * advertise (up to 4000 channel attempts) and the hold that follows a
     * CONNECT_IND (conn_state 1, until the link ends) do not advance it. */
    if (flpr_beacon_posted) {
        return 1;
    }
    return (flpr_conn_posted && (st == 0u || st == 1u)) ? 1 : 0;
}

uint32_t tiku_flpr_arch_image_size(void)
{
    return FLPR_IMAGE_SIZE;
}

/*---------------------------------------------------------------------------*/
/* Mailbox IPC (doorbelled flpr->app, polled app->flpr)                       */
/*---------------------------------------------------------------------------*/

/* Last flpr->app message, captured by tiku_nordic_flpr_isr(), whether the
 * doorbell interrupt or tiku_flpr_arch_poll() called it. */
static uint8_t  flpr_reply[TIKU_FLPR_MSG_CAP];
static volatile uint32_t flpr_reply_len;
static volatile uint32_t flpr_reply_seq;

/**
 * @brief VPR00 doorbell ISR (IRQ 76): clear the event and copy the f2a
 *        message into flpr_reply.  tiku_flpr_arch_poll() also calls it.
 */
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

/* Polled pull.  The doorbell interrupt does not reach this core on this
 * silicon: the VPR00 VEVIF half (registers below 0x800) reads zero and drops
 * writes from both cores, through the CSR EVENTS trigger, an MMIO event
 * write on channels 16..22, and INTENSET from either side, with or without
 * the RT-peripheral enable; INITPC/CPURUN (0x800 and up) work.  Consumers
 * therefore pull: this checks the mailbox seq and captures as the ISR does.
 * The ISR stays wired; a doorbell that does fire captures first. */
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
/* Pulse engine: command the waveform, verify it on the same pad             */
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

    /* Hand P2.07 (LED3) to the VPR's fast I/O with the input buffer
     * connected, so this core reads the pad through P2.IN and counts the
     * transitions itself. */
    NRF_P2_S->PIN_CNF[TIKU_FLPR_VIO_BIT] =
        (1u << 28) |                           /* CTRLSEL = VPR            */
        (0u << 1);                             /* INPUT = Connect          */

    /* Half-cycles come from the live core clock: the FLPR shares HCLK128M
     * with the M33 (datasheet block diagram: both sit in "MCU PD
     * (128 MHz)"), so its cycle rate is 64 or 128 MHz with the core. */
    req->half_cycles = period_us * (uint32_t)(tiku_nordic_cpu_hz_now()
                                              / 2000000UL);
    req->edges = edges;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->rsp = 0u;
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_PULSE;

    /* Sample the pad until the firmware reports DONE or the spin bound
     * runs out.  The poll loop samples at several MHz, faster than the
     * shortest period accepted (10 us). */
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
/* Compute-only load (power characterisation)                                */
/*---------------------------------------------------------------------------*/

int tiku_flpr_arch_spin_start(uint32_t iters)
{
    if (!tiku_flpr_arch_running() || iters == 0u) {
        return -1;
    }
    TIKU_FLPR_SHARED->spin_passes = 0u;
    TIKU_FLPR_SHARED->spin_iters = iters;
    TIKU_FLPR_SHARED->rsp = 0u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_SPIN;
    return 0;
}

uint32_t tiku_flpr_arch_spin_passes(void)
{
    return TIKU_FLPR_SHARED->spin_passes;
}

/* End a sustained load early.  The firmware re-reads the request count every
 * pass, so zeroing it drops the coprocessor back to its mailbox loop. */
void tiku_flpr_arch_spin_abort(void)
{
    TIKU_FLPR_SHARED->spin_iters = 0u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

int tiku_flpr_arch_spin_done(void)
{
    return (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_SPIN_DONE) ? 1 : 0;
}

/* Timed against the GRTC SYSCOUNTER (1 MHz, independent of the PLL); the
 * 128 Hz system tick is too coarse for short runs. */
int tiku_flpr_arch_spin_timed(uint32_t iters, uint32_t *passes, uint32_t *us)
{
    uint32_t t0, spin;

    if (tiku_flpr_arch_spin_start(iters) != 0) {
        return -1;
    }
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    for (spin = 0u; spin < 200000000u; spin++) {
        if (TIKU_FLPR_SHARED->rsp == TIKU_FLPR_RSP_SPIN_DONE) {
            break;
        }
    }
    if (us != (uint32_t *)0) {
        *us = (uint32_t)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0);
    }
    if (passes != (uint32_t *)0) {
        *passes = TIKU_FLPR_SHARED->spin_passes;
    }
    TIKU_FLPR_SHARED->rsp = 0u;
    return (spin < 200000000u) ? 0 : -2;
}

/*---------------------------------------------------------------------------*/
/* Beacon offload                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Make the radio peripherals non-secure for the FLPR (@p on), or
 *        secure again.
 *
 * Covers RADIO, TIMER10 and DPPIC10 (SPU10 slots 10, 5, 2), UARTE21 (SPU20
 * slot 7) and DPPI channels 3..5.
 *
 * @note The M33 programs the radio link config before the flip and leaves
 *       these peripherals alone until the flip back.
 */
static void flpr_radio_ns(int on)
{
    /* SECATTR (bit 4) and the DMA attribute (bit 5) both go non-secure, so
     * the FLPR can drive the peripherals and the radio's EasyDMA can read
     * the PDU from the non-secure carve.  TIMER10 and DPPIC10 time the
     * advertiser's scan response.  The DPPI channels the reply uses
     * (3 PHYEND, 4 ADDRESS, 5 TXEN) carry their own security attribute: a
     * channel left secure never meets a non-secure publisher. */
    uint32_t ch;
    if (on) {
        NRF_SPU10_S->PERIPH[10].PERM &= ~((1u << 4) | (1u << 5));
        NRF_SPU10_S->PERIPH[5].PERM  &= ~((1u << 4) | (1u << 5));
        NRF_SPU10_S->PERIPH[2].PERM  &= ~((1u << 4) | (1u << 5));
        NRF_SPU20_S->PERIPH[7].PERM  &= ~((1u << 4) | (1u << 5));
        for (ch = 3u; ch <= 5u; ch++) {
            NRF_SPU10_S->FEATURE.DPPIC.CH[ch] &= ~(1u << 4);
        }
    } else {
        for (ch = 3u; ch <= 5u; ch++) {
            NRF_SPU10_S->FEATURE.DPPIC.CH[ch] |= (1u << 4);
        }
        NRF_SPU10_S->PERIPH[10].PERM |= (1u << 4) | (1u << 5);
        NRF_SPU10_S->PERIPH[5].PERM  |= (1u << 4) | (1u << 5);
        NRF_SPU10_S->PERIPH[2].PERM  |= (1u << 4) | (1u << 5);
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
        interval_ms == 0u ||
        interval_ms > UINT32_MAX /
                      (uint32_t)(tiku_nordic_cpu_hz_now() / 10000UL)) {
        return -1;
    }
    flpr_radio_ns(1);
    for (i = 0u; i < len; i++) {
        b->pdu[i] = pdu[i];
    }
    b->pdu_len = len;
    b->pace_iters = interval_ms *
                    (uint32_t)(tiku_nordic_cpu_hz_now() / 10000UL);
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_BEACON;
    flpr_beacon_posted = 1u;
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
    flpr_beacon_posted = 0u;
    flpr_radio_ns(0);
}

uint32_t tiku_flpr_arch_beacon_bursts(void)
{
    if (TIKU_FLPR_SHARED->cmd == TIKU_FLPR_CMD_BEACON) {
        return 0u;  /* The new command has not reset its counter yet. */
    }
    return TIKU_FLPR_SHARED->beacon_bursts;
}

/*---------------------------------------------------------------------------*/
/* RX probe: the FLPR listens on advertising channel 37                      */
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

    /* The FLPR bounds the probe itself; block until it publishes rx_done,
     * kicking the watchdog through the listen. */
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
/* Connection controller: the FLPR advertises, captures and holds a link     */
/*---------------------------------------------------------------------------*/

uint32_t tiku_flpr_arch_adv_txen_ticks;      /* 0 = the controller's own */

/**
 * @brief Stage the SCAN_RSP and the reply timing for the next advertise.
 *
 * An absent, empty or oversized @p rsp leaves rsp_len 0, and the controller
 * mirrors the advert, which a scanner's duplicate filter can drop as a
 * repeat.
 */
static void flpr_conn_set_scanrsp(volatile tiku_flpr_conn_t *in,
                                  const uint8_t *rsp, uint32_t rsp_len)
{
    uint32_t i;

    in->txen_ticks = tiku_flpr_arch_adv_txen_ticks;

    if (rsp == (const uint8_t *)0 || rsp_len == 0u ||
        rsp_len > sizeof(in->rsp)) {
        in->rsp_len = 0u;
        return;
    }
    in->rsp_len = rsp_len;
    for (i = 0u; i < rsp_len; i++) {
        in->rsp[i] = rsp[i];
    }
}

int tiku_flpr_arch_conn_capture(const uint8_t *adv, uint32_t adv_len,
                                const uint8_t *rsp, uint32_t rsp_len,
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
    flpr_conn_set_scanrsp(in, rsp, rsp_len);
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_ADV;
    flpr_conn_posted = 1u;

    /* Block until the FLPR connects (1) or gives up (2), or the spin bound
     * runs out; the watchdog is kicked meanwhile. */
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
        tiku_flpr_arch_conn_stop();            /* stop, reclaim secure       */
        return -2;
    }
    /* Connected: the FLPR holds the link and still drives the non-secure
     * RADIO, so the radio stays non-secure here; conn_stop() flips it back
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

/* 1 while the FLPR is holding a live link. */
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

/* Peer and local address from the CONNECT_IND, for SMP f5/f6.  Copies InitA
 * (central = A) and AdvA (local = B); returns the type bitfield (bit0 InitA,
 * bit1 AdvA; 1 = random).  Valid once conn_active(). */
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

/* Last enc_req_seq a session key was derived for. */
static uint32_t flpr_enc_serviced;

/* Service an LL_ENC_REQ the FLPR forwarded: generate SKDs and IVs, derive
 * SK = e(LTK, SKDm||SKDs) and IV = IVm||IVs, publish them, and release the
 * FLPR to send LL_ENC_RSP.  Returns 1 on the call that services a request. */
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
    (void)tiku_trng_arch_read_bytes(&skd[8], 8); /* local SKDs (MSO half)     */
    (void)tiku_trng_arch_read_bytes(ivs, 4);     /* local IVs                 */
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
    sh->enc_rsp_seq = req;                        /* release LL_ENC_RSP */
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

/* Effective DLE max LL payload (0 until an LL_LENGTH_REQ is answered). */
uint32_t tiku_flpr_arch_dle_max(void)
{
    return TIKU_FLPR_SHARED->dle_max;
}

/* Current PHY (0 = 1M, 1 = 2M, 2 = Coded S8); @p at_evt gets the
 * conn_events count at the switch. */
uint32_t tiku_flpr_arch_conn_phy(uint32_t *at_evt)
{
    if (at_evt != (uint32_t *)0) {
        *at_evt = TIKU_FLPR_SHARED->conn_phy_evt;
    }
    return TIKU_FLPR_SHARED->conn_phy;
}

/* PHY-switch telemetry: RADIO->MODE read back at the switch (4 = 2M) and the
 * ADDRESS and CRCOK events caught on the new PHY.  mode != 4 after a 2M
 * switch means the write did not take; mode == 4 with addr == 0 means the
 * receiver hears nothing on 2M. */
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

/* Advertising telemetry: what the advertiser sent and what answered it. */
void tiku_flpr_arch_adv_counts(uint32_t *tx, uint32_t *scanreq,
                               uint32_t *scanrsp, uint32_t *other)
{
    if (tx != (uint32_t *)0) {
        *tx = TIKU_FLPR_SHARED->adv_tx;
    }
    if (scanreq != (uint32_t *)0) {
        *scanreq = TIKU_FLPR_SHARED->adv_scanreq;
    }
    if (scanrsp != (uint32_t *)0) {
        *scanrsp = TIKU_FLPR_SHARED->adv_scanrsp;
    }
    if (other != (uint32_t *)0) {
        *other = TIKU_FLPR_SHARED->adv_rxother;
    }
}

uint32_t tiku_flpr_arch_adv_tifs(void)
{
    return TIKU_FLPR_SHARED->adv_tifs;
}

/* LL updates the FLPR applied at their Instant this connection. */
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

/* Connection-event telemetry from the FLPR, and the CONNECT_IND's window. */
void tiku_flpr_arch_conn_timing(tiku_flpr_conn_timing_t *out)
{
    const tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;

    out->misses     = sh->conn_misses;
    out->late_opens = sh->conn_late;
    out->late_tx    = sh->conn_tx_late;
    out->first      = sh->conn_first;
    out->widen_us   = sh->conn_widen_us;
    out->interval   = sh->conn_interval;
    out->winoffset  = sh->conn_winoffset;
    out->winsize    = sh->conn_winsize;
}

/* Ask the FLPR to stop advertising or leave its hold loop, wait for it,
 * then make the RADIO secure again.  The stop is sent while conn_state is 1,
 * or 0 on a running FLPR; otherwise only the security flip runs. */
void tiku_flpr_arch_conn_stop(void)
{
    uint32_t spin, st;

    st = TIKU_FLPR_SHARED->conn_state;
    if (st == 1u || (st == 0u && tiku_flpr_arch_running())) {
        TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_STOP;
        __asm__ volatile ("dsb 0xF" ::: "memory");
        for (spin = 0u; spin < 400000000u; spin++) {
            st = TIKU_FLPR_SHARED->conn_state;
            if (st != 0u && st != 1u) {
                break;                          /* off the radio             */
            }
            if ((spin & 0xFFFFFu) == 0u) {
                tiku_watchdog_kick();
            }
        }
    }
    flpr_conn_posted = 0u;
    flpr_radio_ns(0);                          /* reclaim secure alias       */
}

/*---------------------------------------------------------------------------*/
/* L2CAP fragment pipe over the mailbox, for the BLE serial facade           */
/*---------------------------------------------------------------------------*/

/* Non-blocking advertise and hold: flip the radio non-secure, hand over the
 * ADV PDU and return.  The FLPR advertises, then holds the link on its own;
 * poll conn_active() for the link. */
/* During a connection the mailbox carries L2CAP fragments: conn_recv pops
 * one the controller forwarded, conn_send hands one of the host's back for
 * TX. */
static uint32_t flpr_nus_rx_seen;

int tiku_flpr_arch_conn_start(const uint8_t *adv, uint32_t adv_len,
                              const uint8_t *rsp, uint32_t rsp_len,
                              const uint8_t *addr)
{
    volatile tiku_flpr_conn_t *in =
        (volatile tiku_flpr_conn_t *)TIKU_FLPR_SHARED->a2f_buf;
    uint32_t i;

    if (!tiku_flpr_arch_running() || adv_len > sizeof(in->adv)) {
        return -1;
    }
    TIKU_FLPR_SHARED->conn_state = 0u;
    /* A fragment the last link left untaken is dropped: the slot starts
     * free. */
    flpr_nus_rx_seen = TIKU_FLPR_SHARED->f2a_seq;
    TIKU_FLPR_SHARED->f2a_ack = flpr_nus_rx_seen;
    flpr_radio_ns(1);
    in->adv_len = adv_len;
    for (i = 0u; i < 6u; i++) {
        in->addr[i] = addr[i];
    }
    for (i = 0u; i < adv_len; i++) {
        in->adv[i] = adv[i];
    }
    flpr_conn_set_scanrsp(in, rsp, rsp_len);
    __asm__ volatile ("dsb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->cmd = TIKU_FLPR_CMD_CONN_ADV;
    flpr_conn_posted = 1u;
    return 0;
}

/* Always 0: the controller leaves conn_sub at 0; tiku_ble_host tracks the
 * NUS CCCD. */
int tiku_flpr_arch_conn_subscribed(void)
{
    return (TIKU_FLPR_SHARED->conn_sub != 0u) ? 1 : 0;
}

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
    /* Taken: the controller may hand on the next fragment. */
    __asm__ volatile ("dmb 0xF" ::: "memory");
    TIKU_FLPR_SHARED->f2a_ack = seq;
    return (int)n;
}

/* Hand one L2CAP fragment (at most 32 bytes; longer ones are cut) to the
 * controller for TX on a2f, tagged with @p llid (2 = start of an L2CAP PDU,
 * 1 = continuation).  Returns the bytes queued, -2 while the previous
 * fragment is unconsumed (a2f_ack != a2f_seq), or -1 with no link or no
 * buffer. */
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
