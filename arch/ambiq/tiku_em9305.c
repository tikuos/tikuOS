/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_em9305.c - EM9305 BLE controller SPI-HCI transport (bare-metal).
 *
 * The controller's framed SPI protocol over IOM6 and a few GPIOs: an EN pulse
 * and the RDY line for reset, then per frame a header byte exchanged for two
 * status bytes before the payload.  Uses no vendor host stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_em9305.h"

#if defined(TIKU_DRV_BLE_EM9305_ENABLE)

#include "tiku.h"                            /* board pin macros              */
#include "apollo510.h"                       /* GPIO PADKEY, PINCFG */
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <arch/ambiq/tiku_gpio_arch.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PROTOCOL AND BOARD GLUE                                                   */
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
/* PIN AND TIMING HELPERS                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Assert the radio's chip select (drive it low). */
static inline void cs_assert(void)   { tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CS_PIN, 0); }
/** @brief Release the radio's chip select (drive it high). */
static inline void cs_release(void)  { tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CS_PIN, 1); }
/** @brief Non-zero while the radio drives RDY high. */
static inline int  rdy_high(void)    { return tiku_gpio_arch_read(EM_RDY_PORT, EM_RDY_PIN) == 1; }

/**
 * @brief Uncalibrated busy delay of @p us * 20 volatile decrements.
 *
 * Sized for the 96 MHz core, and shorter at higher clocks.  It spaces the
 * reset pulse and the frame retries, and times the RDY waits.
 */
static void busy_us(uint32_t us) {
    volatile uint32_t n = us * 20u;
    while (n) { n--; }
}

/** @brief Write a pad's FUNCSEL (for the 32 kHz clock export). */
static void em_pad_funcsel(uint32_t pad, uint32_t funcsel) {
    GPIO->PADKEY = GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = funcsel;
    GPIO->PADKEY = 0u;
}

/** @brief Poll RDY until high or about @p timeout_ms passes; 1 if high. */
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
/* PIN BRING-UP                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Set up the radio's CS, EN, CLKREQ, RDY and 32 kHz clock pads. */
static void pins_init(void) {
    /* CS: output, released (high). */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_CS_PIN);
    cs_release();
    /* EN: output, held low until tiku_em9305_reset() pulses it. */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_EN_PIN);
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_EN_PIN, 0);
    /* CLKREQ: output low, the BSP default. */
    tiku_ambiq_gpio_init_output(TIKU_BOARD_EM9305_CLKREQ_PIN);
    tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_CLKREQ_PIN, 0);
    /* RDY: input. */
    tiku_gpio_arch_set_input(EM_RDY_PORT, EM_RDY_PIN);
    /* 32 kHz sleep-clock export to the radio. */
    em_pad_funcsel(TIKU_BOARD_EM9305_CLK32K_PIN, TIKU_BOARD_EM9305_CLK32K_FUNCSEL);
    s_pins_done = 1u;
}

/*---------------------------------------------------------------------------*/
/* FRAME HANDSHAKE                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Open a frame: assert CS, wait for RDY, exchange the header for the
 *        two status bytes.
 *
 * On success CS stays asserted for the caller to move the payload and call
 * cs_release(), and @p sts2 holds the byte count STS2 reports.  On failure CS
 * is released before returning.
 *
 * @note CS goes low before the RDY wait: the controller raises RDY for a
 *       host-initiated write only once it sees CS low.  On the read path RDY
 *       is already high, so the same order serves both.
 * @return TIKU_EM9305_OK, TIKU_EM9305_ERR_TIMEOUT (no RDY or an SPI error),
 *         or TIKU_EM9305_ERR_NOTREADY (STS never ready)
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

    /* Poll the two status bytes with CS held asserted throughout (the SDK
     * does the same): toggling CS between retries can make the radio
     * mis-latch the frame and corrupt its buffer accounting. */
    for (i = 0u; i < EM_STS_CHK_MAX; i++) {
        if (tiku_spi_write_read(tx, rx, 2u) != 0) {
            busy_us(50);
            cs_release();
            return TIKU_EM9305_ERR_TIMEOUT;
        }
        s_last_sts1 = rx[0];
        s_last_sts2 = rx[1];
        if (rx[0] == EM_STS_READY && rx[1] != 0u) {
            if (sts2) { *sts2 = rx[1]; }
            return TIKU_EM9305_OK;         /* CS stays asserted */
        }
        busy_us(50);
    }
    busy_us(50);
    cs_release();
    return TIKU_EM9305_ERR_NOTREADY;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_em9305_reset(void) {
    tiku_spi_config_t cfg;
    uint32_t g;

    cfg.mode      = TIKU_SPI_MODE_0;
    cfg.bit_order = TIKU_SPI_MSB_FIRST;
    cfg.prescaler = 1u;                    /* non-zero for the bus check; the
                                            * IOM ignores it and runs 16 MHz */

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
        busy_us(50);                       /* the radio latches the frame
                                            * before CS rises (SDK tx_ends) */
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

    /*
     * One framed read: STS2 bounds this frame's bytes.  The radio's side is
     * a byte stream: one frame may carry part of an HCI packet (large packets
     * span frames) or several small packets back to back, such as coalesced
     * Number-Of-Completed-Packets events.  A caller that needs packet
     * boundaries reassembles across calls, as the host stack's transport
     * below does.
     */
    n = (uint16_t)(sts2 < cap ? sts2 : cap);
    rc = tiku_spi_read(buf, n);
    busy_us(50);                           /* settle before CS rises */
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

    /* Reset, then the boot event: SPI reaches the radio. */
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
    p.sts1 = s_last_sts1;   /* 0xC0: SPI reaches the radio */
    p.sts2 = s_last_sts2;

    /* HCI Reset -> Command Complete: HCI answers. */
    p.send_rc = (int8_t)tiku_em9305_send(hci_reset, sizeof(hci_reset));
    if (p.send_rc == TIKU_EM9305_OK) {
        p.recv_rc = (int8_t)tiku_em9305_recv(ev, sizeof(ev), &evlen, 1000u);
        if (p.recv_rc == TIKU_EM9305_OK) {
            memcpy(p.evt, ev, evlen < sizeof(p.evt) ? evlen : sizeof(p.evt));
            p.evt_len = evlen;
            /* Command Complete: 04 0E len 01 op_lo op_hi status ... */
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
/* HCI COMMANDS AND LE BEACON                                                */
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
        (void)tiku_em9305_recv(bev, sizeof(bev), &bl, 500u); /* 04 FF 01 01 */
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
    /* 3. Advertising data: [sig-len][Flags AD][Complete Local Name AD],
     *    padded to the fixed 31-byte field (the command is len + 31 bytes). */
    memset(adv_data, 0, sizeof(adv_data));
    idx = 1u;
    adv_data[idx++] = 0x02u;                /* Flags AD: len */
    adv_data[idx++] = 0x01u;                /* Flags AD: type */
    adv_data[idx++] = 0x06u;                /* LE general disc, no BR/EDR */
    nlen = (uint8_t)strlen(name);
    if (nlen > 26u) {
        nlen = 26u;                         /* fit the 31-byte AD field */
    }
    adv_data[idx++] = (uint8_t)(nlen + 1u); /* Name AD: len */
    adv_data[idx++] = 0x09u;                /* Name AD: Complete Local Name */
    memcpy(adv_data + idx, name, nlen);
    idx = (uint8_t)(idx + nlen);
    adv_data[0] = (uint8_t)(idx - 1u);      /* significant byte count */
    if (tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_DATA, adv_data,
                            (uint8_t)sizeof(adv_data), &r.st_data)
        != TIKU_EM9305_OK) {
        if (out) { *out = r; }
        return TIKU_EM9305_ERR_TIMEOUT;
    }
    /* 4. Enable advertising; the controller then broadcasts on its own. */
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

/*---------------------------------------------------------------------------*/
/* DIE OWNERSHIP                                                             */
/*---------------------------------------------------------------------------*/

static uint8_t s_users;         /* TIKU_EM9305_USER_* bits holding the die */

int tiku_em9305_acquire(uint8_t user) {
    if (s_users == 0u) {
        int rc = tiku_em9305_reset();
        if (rc != TIKU_EM9305_OK) {
            return rc;
        }
    }
    s_users = (uint8_t)(s_users | user);
    return TIKU_EM9305_OK;
}

void tiku_em9305_release(uint8_t user) {
    s_users = (uint8_t)(s_users & (uint8_t)~user);
    if (s_users == 0u && s_pins_done) {
        tiku_ambiq_gpio_set(TIKU_BOARD_EM9305_EN_PIN, 0);
    }
}

/*---------------------------------------------------------------------------*/
/* HCI TRANSPORT FOR THE HOST STACK                                          */
/*---------------------------------------------------------------------------*/

/* The radio's side of SPI is a byte stream: a frame holds part of a packet
 * or several, so frames gather here until a whole packet is in.  A frame is
 * read only while a whole one fits (STS2 counts at most 255 bytes), so the
 * stream holds a partial packet of up to 258 bytes and one frame more. */
#define EM_FRAME_MAX    255u
#define EM_STREAM_MAX   560u

static uint8_t  s_hci_up;                   /* the host stack owns HCI    */
static uint8_t  s_stream[EM_STREAM_MAX];
static uint16_t s_stream_len;

/**
 * @brief Append the frames the radio has pending (RDY high) to the stream.
 *
 * Non-blocking.  Runs before every send as well as every receive: a write
 * the host starts while a frame waits collides with it and is lost.
 */
static void em_slurp(void) {
    uint8_t guard;

    for (guard = 0u; guard < 16u; guard++) {
        uint16_t l = 0u;

        if ((uint16_t)(EM_STREAM_MAX - s_stream_len) < EM_FRAME_MAX ||
            tiku_em9305_recv(s_stream + s_stream_len, EM_FRAME_MAX, &l, 0u)
            != TIKU_EM9305_OK) {
            return;
        }
        s_stream_len = (uint16_t)(s_stream_len + l);
    }
}

/**
 * @brief The length of the packet at the front of the stream, 0 while part
 *        of it is still in the radio.
 *
 * A byte that cannot start an event (0x04) or an ACL packet (0x02) is
 * dropped, as is an ACL header longer than the stream holds: the stream
 * resyncs on the next packet.
 */
static uint16_t em_front_len(void) {
    for (;;) {
        uint16_t need = 0u;

        if (s_stream_len == 0u) {
            return 0u;
        }
        if (s_stream[0] == 0x04u) {
            if (s_stream_len < 3u) {
                return 0u;
            }
            need = (uint16_t)(3u + s_stream[2]);
        } else if (s_stream[0] == 0x02u) {
            if (s_stream_len < 5u) {
                return 0u;
            }
            need = (uint16_t)(5u + (s_stream[3] |
                                    ((uint16_t)s_stream[4] << 8)));
        }
        if (need != 0u && need <= (uint16_t)(EM_STREAM_MAX - EM_FRAME_MAX)) {
            return (s_stream_len >= need) ? need : 0u;
        }
        s_stream_len--;
        memmove(s_stream, s_stream + 1, s_stream_len);
    }
}

/** @brief tiku_bt_transport_t send: one whole HCI packet, framed. */
static int em_bt_send(const uint8_t *pkt, uint16_t len) {
    if (!s_hci_up) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if (pkt == NULL || len < 2u) {
        return TIKU_DRV_ERR_INVALID;
    }
    em_slurp();
    return (tiku_em9305_send(pkt, len) == TIKU_EM9305_OK) ? TIKU_DRV_OK
                                                          : TIKU_DRV_ERR_IO;
}

/**
 * @brief tiku_bt_transport_t recv: the next whole packet, or 0.  A packet
 *        larger than @p out_max stays for a caller with room for it.
 */
static int em_bt_recv(uint8_t *out, uint16_t out_max) {
    uint16_t n;

    if (out == NULL || out_max < 2u) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (!s_hci_up) {
        return 0;
    }
    em_slurp();
    n = em_front_len();
    if (n == 0u) {
        return 0;
    }
    if (n > out_max) {
        return TIKU_DRV_ERR_INVALID;
    }
    memcpy(out, s_stream, n);
    s_stream_len = (uint16_t)(s_stream_len - n);
    memmove(s_stream, s_stream + n, s_stream_len);
    return (int)n;
}

static int em_bt_ready(void) {
    return s_hci_up;
}

/** @brief The stack waits for a reply: until RDY rises or @p ms pass. */
static void em_bt_wait(uint16_t ms) {
    (void)wait_rdy_high(ms);
}

static const char *em_bt_version(void) {
    return "EM9305";
}

static const tiku_bt_transport_t em_bt_transport = {
    .send     = em_bt_send,
    .recv     = em_bt_recv,
    .is_ready = em_bt_ready,
    .wait     = em_bt_wait,
    .version  = em_bt_version,
};

int tiku_bt_controller_power(uint8_t on) {
    uint8_t addr[6];
    int rc;

    if (!on) {
        if (s_hci_up) {
            tiku_bt_shutdown();
            s_hci_up = 0u;
            s_stream_len = 0u;
            tiku_em9305_release(TIKU_EM9305_USER_BLE);
        }
        return TIKU_DRV_OK;
    }
    if (s_hci_up) {
        return TIKU_DRV_OK;
    }
    if (tiku_em9305_acquire(TIKU_EM9305_USER_BLE) != TIKU_EM9305_OK) {
        return TIKU_DRV_ERR_NOT_PRESENT;    /* RDY never rose: no radio */
    }
    s_stream_len = 0u;
    s_hci_up = 1u;
    (void)tiku_bt_register_transport(&em_bt_transport);
    rc = tiku_bt_init();
    /* A radio that answered no identity query is not up: say so. */
    if (rc == TIKU_DRV_OK && tiku_bt_addr(addr) != TIKU_DRV_OK) {
        rc = TIKU_DRV_ERR_TIMEOUT;
    }
    if (rc != TIKU_DRV_OK) {
        (void)tiku_bt_controller_power(0u);
    }
    return rc;
}

#endif /* TIKU_DRV_BLE_EM9305_ENABLE */
