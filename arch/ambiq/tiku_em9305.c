/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_em9305.c - EM9305 BLE controller SPI-HCI transport (bare-metal)
 *
 * Speaks the EM9305's framed SPI protocol over tiku_spi (IOM6) + GPIOs:
 *   - reset:  pulse EN low->high, wait RDY low then high, read the radio's
 *             {04 FF 01 01} "active state entered" boot event;
 *   - frame:  assert CS (GPIO), wait RDY high, exchange a 1-byte header
 *             (0x42 write / 0x81 read) + read two status bytes -- STS1 == 0xC0
 *             means the controller is ready and STS2 is the free/available
 *             byte count -- then move the payload full-duplex and release CS.
 * Every EM9305 exchange is full-duplex (the SPI master keeps FULLDUP on).
 *
 * This is the M0/M1 bring-up layer (raw HCI in/out + a self-test); the minimal
 * HCI host + GATT server land later in tikukits/ble. Built only for the BLE
 * config (TIKU_DRV_BLE_EM9305_ENABLE, apollo510b).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_em9305.h"

#if defined(TIKU_DRV_BLE_EM9305_ENABLE)

#include "tiku.h"                            /* board pin macros              */
#include "apollo510.h"                       /* GPIO PADKEY (CLK32K funcsel)   */
#include <interfaces/bus/tiku_spi_bus.h>
#include <arch/ambiq/tiku_gpio_arch.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Protocol + board glue                                                     */
/*---------------------------------------------------------------------------*/

#define EM_HDR_TX       0x42u   /**< host-to-controller frame header */
#define EM_HDR_RX       0x81u   /**< controller-to-host frame header */
#define EM_STS_READY    0xC0u   /**< STS1 value meaning "ready"       */
#define EM_STS_CHK_MAX  10u     /**< status re-reads before giving up */

/* The tiku_gpio (port,pin) API encodes an Apollo pad as (port-1)*8 + pin. */
#define PAD_PORT(p)     ((uint8_t)((p) / 8u + 1u))
#define PAD_PIN(p)      ((uint8_t)((p) % 8u))

#define EM_RDY_PORT     PAD_PORT(TIKU_BOARD_EM9305_RDY_PIN)
#define EM_RDY_PIN      PAD_PIN(TIKU_BOARD_EM9305_RDY_PIN)

/** PADKEY unlock value for a GPIO PINCFG write. */
#define GPIO_PADKEY_UNLOCK 0x73u

static uint8_t s_pins_done;
static uint8_t s_last_sts1;   /* remembered for the probe snapshot */
static uint8_t s_last_sts2;

/* Reset-path diagnostics captured for the probe snapshot / `ble` command. */
static uint8_t s_dbg_spi_rc;
static uint8_t s_dbg_rdy0;
static uint8_t s_dbg_saw_low;
static uint8_t s_dbg_saw_high;
static uint8_t s_dbg_rdy_final;

/*---------------------------------------------------------------------------*/
/* Low-level pin + timing helpers                                            */
/*---------------------------------------------------------------------------*/

static inline void cs_assert(void)   { tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CS_PIN, 0); }
static inline void cs_release(void)  { tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CS_PIN, 1); }
static inline int  rdy_high(void)    { return tiku_gpio_arch_read(EM_RDY_PORT, EM_RDY_PIN) == 1; }

/** Rough busy delay. ~96 MHz core; a volatile decrement is ~20 iters/us. Only
 *  used for short, non-critical spacing (reset pulse, inter-retry gaps). */
static void busy_us(uint32_t us) {
    volatile uint32_t n = us * 20u;
    while (n) { n--; }
}

/** Route a GPIO pad to a peripheral function (for the 32 kHz clock export). */
static void em_pad_funcsel(uint32_t pad, uint32_t funcsel) {
    GPIO->PADKEY = GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = funcsel;
    GPIO->PADKEY = 0u;
}

/** Poll RDY until high or ~timeout_ms elapses. Returns 1 on high, 0 on timeout. */
static int wait_rdy_high(uint32_t timeout_ms) {
    uint32_t spins = timeout_ms * 10u;      /* 100 us per spin */
    while (spins--) {
        if (rdy_high()) {
            return 1;
        }
        busy_us(100);
    }
    return rdy_high();
}

/*---------------------------------------------------------------------------*/
/* Pin bring-up                                                              */
/*---------------------------------------------------------------------------*/

static void pins_init(void) {
    /* CS: output, released (high). */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_CS_PIN);
    cs_release();
    /* EN: output, deasserted (low) for now -- reset() pulses it. */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_EN_PIN);
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_EN_PIN, 0);
    /* CLKREQ: output low (BSP default; asserted only for deep-sleep coexistence). */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_CLKREQ_PIN);
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CLKREQ_PIN, 0);
    /* RDY: input. */
    tiku_gpio_arch_set_input(EM_RDY_PORT, EM_RDY_PIN);
    /* 32 kHz sleep-clock export to the radio. */
    em_pad_funcsel(TIKU_BOARD_EM9305_CLK32K_PIN, TIKU_BOARD_EM9305_CLK32K_FUNCSEL);
    s_pins_done = 1u;
}

/*---------------------------------------------------------------------------*/
/* Frame handshake                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Open a frame: wait RDY, assert CS, exchange the header + status.
 *
 * On success returns OK with CS left ASSERTED (the caller moves the payload and
 * then calls cs_release()) and @p sts2 = the controller's free/available byte
 * count. On any failure CS is released before returning.
 *
 * Chip-select is asserted BEFORE waiting on RDY: a host-initiated write only
 * gets an RDY assertion once the controller sees CS low (CS is the prompt). The
 * read path already has RDY high when it arrives, so this order works for both.
 */
static int frame_begin(uint8_t header, uint8_t *sts2) {
    uint8_t tx[2];
    uint8_t rx[2];
    uint32_t i;

    tx[0] = header;
    tx[1] = 0x00u;

    cs_assert();
    if (!wait_rdy_high(1300u)) {          /* ~worst-case cold start */
        cs_release();
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    for (i = 0u; i < EM_STS_CHK_MAX; i++) {
        cs_assert();
        if (tiku_spi_write_read(tx, rx, 2u) != 0) {
            cs_release();
            return TIKU_EM9305_ERR_TIMEOUT;
        }
        s_last_sts1 = rx[0];
        s_last_sts2 = rx[1];
        if (rx[0] == EM_STS_READY && rx[1] != 0u) {
            if (sts2) { *sts2 = rx[1]; }
            return TIKU_EM9305_OK;         /* CS stays asserted */
        }
        cs_release();
        busy_us(50);
    }
    return TIKU_EM9305_ERR_NOTREADY;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_em9305_reset(void) {
    tiku_spi_config_t cfg;
    uint32_t g;

    cfg.mode      = TIKU_SPI_MODE_0;
    cfg.bit_order = TIKU_SPI_MSB_FIRST;
    cfg.prescaler = 1u;                    /* non-zero (bus validates); the Ambiq
                                            * IOM ignores it and sets 16 MHz */

    if (!s_pins_done) {
        pins_init();
    }
    s_dbg_spi_rc = (uint8_t)(tiku_spi_init(&cfg) != 0);
    if (s_dbg_spi_rc) {
        return TIKU_EM9305_ERR_RESET;
    }

    s_dbg_rdy0 = (uint8_t)rdy_high();

    /* Pulse the radio out of reset: EN low then high. */
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_EN_PIN, 0);
    busy_us(200);
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_EN_PIN, 1);

    /* RDY drops while the radio boots, then rises when it is ready. */
    s_dbg_saw_low = 0u;
    s_dbg_saw_high = 0u;
    for (g = 0u; g < 300000u && rdy_high(); g++) { busy_us(1); }
    if (!rdy_high()) { s_dbg_saw_low = 1u; }
    for (g = 0u; g < 300000u && !rdy_high(); g++) { busy_us(1); }
    if (rdy_high()) { s_dbg_saw_high = 1u; }
    s_dbg_rdy_final = (uint8_t)rdy_high();

    if (!s_dbg_saw_high) {
        return TIKU_EM9305_ERR_RESET;      /* never signalled ready */
    }
    return TIKU_EM9305_OK;
}

int tiku_em9305_send(const uint8_t *data, uint16_t len) {
    uint16_t sent = 0u;

    if (data == NULL || len == 0u) {
        return TIKU_EM9305_ERR_PARAM;
    }
    while (sent < len) {
        uint8_t  sts2 = 0u;
        uint16_t chunk;
        int      rc = frame_begin(EM_HDR_TX, &sts2);
        if (rc != TIKU_EM9305_OK) {
            return rc;                     /* frame_begin already released CS */
        }
        chunk = (uint16_t)((len - sent) < sts2 ? (len - sent) : sts2);
        rc = tiku_spi_write(data + sent, chunk);
        cs_release();
        if (rc != 0) {
            return TIKU_EM9305_ERR_TIMEOUT;
        }
        sent = (uint16_t)(sent + chunk);
    }
    return TIKU_EM9305_OK;
}

int tiku_em9305_recv(uint8_t *buf, uint16_t cap, uint16_t *out_len,
                     uint32_t timeout_ms) {
    uint8_t  sts2 = 0u;
    uint16_t n;
    int      rc;

    if (buf == NULL || cap == 0u) {
        return TIKU_EM9305_ERR_PARAM;
    }
    if (!wait_rdy_high(timeout_ms)) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    rc = frame_begin(EM_HDR_RX, &sts2);
    if (rc != TIKU_EM9305_OK) {
        return rc;
    }
    n = (uint16_t)(sts2 < cap ? sts2 : cap);
    rc = tiku_spi_read(buf, n);
    cs_release();
    if (rc != 0) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    if (out_len) { *out_len = n; }
    return TIKU_EM9305_OK;
}

int tiku_em9305_probe(tiku_em9305_probe_t *out) {
    static const uint8_t hci_reset[4] = { 0x01u, 0x03u, 0x0Cu, 0x00u };
    tiku_em9305_probe_t p;
    uint8_t  ev[16];
    uint16_t evlen = 0u;

    memset(&p, 0, sizeof(p));
    s_last_sts1 = 0u;
    s_last_sts2 = 0u;

    /* --- reset + boot event (M0 first light: SPI must reach the radio) --- */
    p.reset_rc      = tiku_em9305_reset();
    p.spi_rc        = s_dbg_spi_rc;
    p.rdy_initial   = s_dbg_rdy0;
    p.saw_low       = s_dbg_saw_low;
    p.saw_high      = s_dbg_saw_high;
    p.rdy_final     = s_dbg_rdy_final;
    if (p.reset_rc != TIKU_EM9305_OK) {
        if (out) { *out = p; }
        return p.reset_rc;
    }

    if (tiku_em9305_recv(ev, sizeof(ev), &evlen, 500u) == TIKU_EM9305_OK) {
        if (evlen >= 4u && ev[0] == 0x04u && ev[1] == 0xFFu) {
            p.active_evt = 1u;
        }
    }
    p.sts1 = s_last_sts1;   /* 0xC0 here == SPI is genuinely talking to the radio */
    p.sts2 = s_last_sts2;

    /* --- HCI Reset -> Command Complete (M1 gate: HCI is alive) --- */
    p.send_rc = (int8_t)tiku_em9305_send(hci_reset, sizeof(hci_reset));
    if (p.send_rc == TIKU_EM9305_OK) {
        p.recv_rc = (int8_t)tiku_em9305_recv(ev, sizeof(ev), &evlen, 1000u);
        if (p.recv_rc == TIKU_EM9305_OK) {
            memcpy(p.evt, ev, evlen < sizeof(p.evt) ? evlen : sizeof(p.evt));
            p.evt_len = evlen;
            /* HCI Command Complete: 04 0E len 01 <op_lo> <op_hi> <status> ... */
            if (evlen >= 7u && ev[0] == 0x04u && ev[1] == 0x0Eu) {
                p.cc_seen = 1u;
                p.hci_status = ev[6];
            }
        }
    }

    if (out) { *out = p; }
    return p.cc_seen ? TIKU_EM9305_OK : TIKU_EM9305_ERR_TIMEOUT;
}

/*---------------------------------------------------------------------------*/
/* HCI command helper + LE beacon (M2)                                       */
/*---------------------------------------------------------------------------*/

#define HCI_OP_RESET             0x0C03u
#define HCI_OP_LE_SET_ADV_PARAM  0x2006u
#define HCI_OP_LE_SET_ADV_DATA   0x2008u
#define HCI_OP_LE_SET_ADV_ENABLE 0x200Au

int tiku_em9305_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t plen,
                        uint8_t *status) {
    uint8_t  buf[4u + 32u];
    uint8_t  ev[32];
    uint16_t evlen = 0u;

    if (plen > 32u) {
        return TIKU_EM9305_ERR_PARAM;
    }
    buf[0] = 0x01u;                        /* HCI command packet type */
    buf[1] = (uint8_t)(opcode & 0xFFu);
    buf[2] = (uint8_t)(opcode >> 8);
    buf[3] = plen;
    if (plen && params) {
        memcpy(buf + 4, params, plen);
    }
    if (tiku_em9305_send(buf, (uint16_t)(4u + plen)) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    if (tiku_em9305_recv(ev, sizeof(ev), &evlen, 1000u) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    /* Command Complete for this opcode: 04 0E len 01 op_lo op_hi status ... */
    if (evlen >= 7u && ev[0] == 0x04u && ev[1] == 0x0Eu &&
        ev[4] == buf[1] && ev[5] == buf[2]) {
        if (status) { *status = ev[6]; }
        return TIKU_EM9305_OK;
    }
    if (status) { *status = 0xFFu; }
    return TIKU_EM9305_ERR_NOTREADY;
}

int tiku_em9305_beacon(const char *name, tiku_em9305_beacon_t *out) {
    /* LE Set Advertising Parameters: 100 ms interval, non-connectable
     * undirected, public own address, all 3 channels, no filtering. */
    static const uint8_t adv_params[15] = {
        0xA0u, 0x00u,                       /* min interval 160*0.625ms=100ms */
        0xA0u, 0x00u,                       /* max interval 100 ms            */
        0x03u,                              /* ADV_NONCONN_IND (beacon)       */
        0x00u,                              /* own address type: public       */
        0x00u,                              /* peer address type              */
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,  /* peer address (unused)   */
        0x07u,                              /* channel map 37/38/39           */
        0x00u                               /* filter policy: allow all       */
    };
    static uint8_t adv_data[32];
    tiku_em9305_beacon_t r;
    uint8_t nlen, idx, en;

    memset(&r, 0, sizeof(r));
    if (name == NULL) {
        name = "tiku";
    }

    /* Bring the radio up (EN pulse + boot event), then drain the boot event. */
    r.init_rc = tiku_em9305_reset();
    if (r.init_rc != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return r.init_rc;
    }
    {
        uint8_t  bev[16];
        uint16_t bl = 0u;
        (void)tiku_em9305_recv(bev, sizeof(bev), &bl, 500u);   /* {04 FF 01 01} */
    }

    /* 1. HCI Reset -> known state. */
    if (tiku_em9305_hci_cmd(HCI_OP_RESET, NULL, 0u, &r.st_reset)
        != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    /* 2. Advertising parameters. */
    if (tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_PARAM, adv_params,
                            (uint8_t)sizeof(adv_params), &r.st_params)
        != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    /* 3. Advertising data: [sig-len][Flags AD][Complete Local Name AD], padded
     *    into a fixed 31-byte field (the command carries len + 31 data bytes). */
    memset(adv_data, 0, sizeof(adv_data));
    idx = 1u;
    adv_data[idx++] = 0x02u;                /* Flags AD: len */
    adv_data[idx++] = 0x01u;                /*           type = Flags */
    adv_data[idx++] = 0x06u;                /*           LE General Disc + no BR/EDR */
    nlen = (uint8_t)strlen(name);
    if (nlen > 26u) {
        nlen = 26u;                         /* keep within the 31-byte AD budget */
    }
    adv_data[idx++] = (uint8_t)(nlen + 1u); /* Name AD: len */
    adv_data[idx++] = 0x09u;                /*          type = Complete Local Name */
    memcpy(adv_data + idx, name, nlen);
    idx = (uint8_t)(idx + nlen);
    adv_data[0] = (uint8_t)(idx - 1u);      /* significant byte count */
    if (tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_DATA, adv_data,
                            (uint8_t)sizeof(adv_data), &r.st_data)
        != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    /* 4. Enable advertising -- the controller broadcasts autonomously now. */
    en = 0x01u;
    if (tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_ENABLE, &en, 1u, &r.st_enable)
        != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    r.ok = (uint8_t)(r.st_reset == 0u && r.st_params == 0u &&
                     r.st_data == 0u && r.st_enable == 0u);
    if (out) { *out = r; }
    return r.ok ? TIKU_EM9305_OK : TIKU_EM9305_ERR_NOTREADY;
}

int tiku_em9305_beacon_stop(void) {
    uint8_t en = 0x00u;
    uint8_t st = 0u;
    return tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_ENABLE, &en, 1u, &st);
}

#endif /* TIKU_DRV_BLE_EM9305_ENABLE */
