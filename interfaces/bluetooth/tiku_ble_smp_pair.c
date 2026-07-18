/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_smp_pair.c - LE Secure Connections "Just Works" pairing engine.
 * See tiku_ble_smp_pair.h for the message flow.  Endianness contract: SMP
 * fields (public keys, nonces, DHKey checks) travel little-endian; f4/f5/f6
 * take that wire order directly.  Our P-256 kit is big-endian (SEC1), so the
 * only conversions are at the key boundary: X/Y coords reverse when they enter
 * or leave the wire, and the ECDH shared-X reverses into the DHKey.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_smp_pair.h>
#include <interfaces/bluetooth/tiku_ble_smp.h>
#include <arch/nordic/tiku_trng_arch.h>
#include <tikukits/crypto/p256/tiku_kits_crypto_p256.h>
#include <string.h>

/* SMP opcodes (Core Spec Vol 3, Part H, 3.3). */
#define SMP_PAIRING_REQUEST     0x01u
#define SMP_PAIRING_RESPONSE    0x02u
#define SMP_PAIRING_CONFIRM     0x03u
#define SMP_PAIRING_RANDOM      0x04u
#define SMP_PAIRING_FAILED      0x05u
#define SMP_PAIRING_PUBLIC_KEY  0x0Cu
#define SMP_PAIRING_DHKEY_CHECK 0x0Du

/* Our pairing parameters: NoInputNoOutput -> Just Works, SC bit set. */
#define SMP_IO_CAP     0x03u                 /* NoInputNoOutput               */
#define SMP_OOB        0x00u                 /* no OOB                        */
#define SMP_AUTHREQ    0x08u                 /* SC=1, no MITM/bonding/keypress*/
#define SMP_MAX_KEY    0x10u                 /* 16-byte key                   */

#define SMP_FAIL_CONFIRM   0x04u             /* Confirm Value Failed          */
#define SMP_FAIL_DHKEY     0x0Bu             /* DHKey Check Failed            */
#define SMP_FAIL_UNSPEC    0x08u             /* Unspecified Reason            */

#define OUTQ_DEPTH  2u                       /* responder queues PKb + Cb     */

typedef struct {
    tiku_ble_smp_role_t  role;
    tiku_ble_smp_state_t state;

    uint8_t  a[6], b[6];                     /* initiator A, responder B      */
    uint8_t  at, bt;                         /* address types (1 = random)    */

    uint8_t  priv[32];                       /* our ECDH private (big-endian) */
    uint8_t  pk_local[64];                   /* our public key   (wire, X||Y) */
    uint8_t  pk_peer[64];                    /* peer public key  (wire)       */
    uint8_t  na[16], nb[16];                 /* initiator, responder nonces   */
    uint8_t  dhkey[32];                      /* wire (little-endian) DHKey    */
    uint8_t  mackey[16], ltk[16];
    uint8_t  iocap_a[3], iocap_b[3];         /* [io_cap, oob, authreq] each   */
    uint8_t  confirm_peer[16];               /* Cb received (initiator verify)*/
    uint8_t  have_dhkey;                     /* ECDH done                     */
    uint8_t  last_rx_op;                      /* last processed opcode (dedup) */

    uint8_t  outq[OUTQ_DEPTH][TIKU_BLE_SMP_PDU_MAX];
    uint8_t  outq_len[OUTQ_DEPTH];
    uint8_t  outq_head, outq_count;
} smp_ctx_t;

static smp_ctx_t sc;

/* --- little helpers ----------------------------------------------------- */

static void rev(uint8_t *dst, const uint8_t *src, uint16_t n)
{
    uint16_t i;
    for (i = 0u; i < n; i++) {
        dst[i] = src[n - 1u - i];
    }
}

static void outq_push(const uint8_t *pdu, uint8_t len)
{
    uint8_t slot;
    if (sc.outq_count >= OUTQ_DEPTH || len > TIKU_BLE_SMP_PDU_MAX) {
        return;
    }
    slot = (uint8_t)((sc.outq_head + sc.outq_count) % OUTQ_DEPTH);
    memcpy(sc.outq[slot], pdu, len);
    sc.outq_len[slot] = len;
    sc.outq_count++;
}

static void fail(uint8_t reason)
{
    uint8_t p[2];
    /* Success is terminal: once DONE, a corrupted/duplicate late PDU (which
     * re-runs a handler for retransmit) must never undo an established LTK. */
    if (sc.state == TIKU_BLE_SMP_STATE_DONE) {
        return;
    }
    p[0] = SMP_PAIRING_FAILED;
    p[1] = reason;
    /* Best-effort notify the peer, then latch FAILED. */
    sc.outq_count = 0u; sc.outq_head = 0u;
    outq_push(p, 2u);
    sc.state = TIKU_BLE_SMP_STATE_FAILED;
}

/* Build peer point 0x04 || X_be || Y_be from the wire (little-endian) key. */
static void peer_point_be(uint8_t point[65], const uint8_t wire[64])
{
    point[0] = 0x04u;
    rev(&point[1], &wire[0], 32);            /* X: wire LE -> big-endian      */
    rev(&point[33], &wire[32], 32);          /* Y                             */
}

/* ECDH once the peer public key is in: DHKey (wire LE) = reverse(shared X). */
static int compute_dhkey(void)
{
    uint8_t point[65], sx[32];
    if (sc.have_dhkey) {
        return 0;
    }
    peer_point_be(point, sc.pk_peer);
    if (tiku_kits_crypto_p256_ecdh_shared(sc.priv, point, sx) !=
        TIKU_KITS_CRYPTO_P256_OK) {
        return -1;
    }
    rev(sc.dhkey, sx, 32);                   /* shared X big-endian -> wire   */
    sc.have_dhkey = 1u;
    return 0;
}

/* (MacKey, LTK) = f5(DHKey, Na, Nb, A, B) -- identical on both roles. */
static void derive_keys(void)
{
    tiku_ble_smp_f5(sc.dhkey, sc.na, sc.nb, sc.at, sc.a, sc.bt, sc.b,
                    sc.mackey, sc.ltk);
}

/* --- PDU builders ------------------------------------------------------- */

static void build_pair_cmd(uint8_t opcode)          /* Request or Response   */
{
    uint8_t p[7];
    p[0] = opcode;
    p[1] = SMP_IO_CAP; p[2] = SMP_OOB; p[3] = SMP_AUTHREQ;
    p[4] = SMP_MAX_KEY; p[5] = 0x00u; p[6] = 0x00u;   /* no key distribution  */
    outq_push(p, 7u);
}

static void build_public_key(void)
{
    uint8_t p[65];
    p[0] = SMP_PAIRING_PUBLIC_KEY;
    memcpy(&p[1], sc.pk_local, 64);
    outq_push(p, 65u);
}

static void build_confirm(const uint8_t cv[16])
{
    uint8_t p[17];
    p[0] = SMP_PAIRING_CONFIRM;
    memcpy(&p[1], cv, 16);
    outq_push(p, 17u);
}

static void build_random(const uint8_t n[16])
{
    uint8_t p[17];
    p[0] = SMP_PAIRING_RANDOM;
    memcpy(&p[1], n, 16);
    outq_push(p, 17u);
}

static void build_dhkey_check(const uint8_t e[16])
{
    uint8_t p[17];
    p[0] = SMP_PAIRING_DHKEY_CHECK;
    memcpy(&p[1], e, 16);
    outq_push(p, 17u);
}

/* --- lifecycle ---------------------------------------------------------- */

void tiku_ble_smp_pair_reset(void)
{
    memset(&sc, 0, sizeof(sc));
    sc.state = TIKU_BLE_SMP_STATE_IDLE;
}

int tiku_ble_smp_pair_start(tiku_ble_smp_role_t role,
                            const uint8_t a[6], uint8_t at,
                            const uint8_t b[6], uint8_t bt)
{
    uint8_t seed[32], pub[65];

    tiku_ble_smp_pair_reset();
    sc.role = role;
    memcpy(sc.a, a, 6); memcpy(sc.b, b, 6);
    sc.at = at; sc.bt = bt;

    tiku_trng_arch_init();
    if (tiku_trng_arch_read_bytes(seed, 32) != 0 ||
        tiku_kits_crypto_p256_ecdh_keypair(seed, sc.priv, pub) !=
        TIKU_KITS_CRYPTO_P256_OK) {
        sc.state = TIKU_BLE_SMP_STATE_FAILED;
        return -1;
    }
    /* Public point 0x04||X_be||Y_be -> wire X||Y little-endian. */
    rev(&sc.pk_local[0], &pub[1], 32);
    rev(&sc.pk_local[32], &pub[33], 32);

    /* Our nonce (initiator -> Na, responder -> Nb). */
    if (tiku_trng_arch_read_bytes((role == TIKU_BLE_SMP_ROLE_INITIATOR)
                                  ? sc.na : sc.nb, 16) != 0) {
        sc.state = TIKU_BLE_SMP_STATE_FAILED;
        return -1;
    }

    sc.state = TIKU_BLE_SMP_STATE_PAIRING;
    if (role == TIKU_BLE_SMP_ROLE_INITIATOR) {
        sc.iocap_a[0] = SMP_IO_CAP; sc.iocap_a[1] = SMP_OOB;
        sc.iocap_a[2] = SMP_AUTHREQ;
        build_pair_cmd(SMP_PAIRING_REQUEST);      /* kick off the exchange    */
    } else {
        sc.iocap_b[0] = SMP_IO_CAP; sc.iocap_b[1] = SMP_OOB;
        sc.iocap_b[2] = SMP_AUTHREQ;
    }
    return 0;
}

/* --- initiator (central) receive path ----------------------------------- */

static void feed_initiator(uint8_t op, const uint8_t *pdu, uint16_t len)
{
    switch (op) {
    case SMP_PAIRING_RESPONSE:
        if (len < 4u) { return; }
        sc.iocap_b[0] = pdu[1]; sc.iocap_b[1] = pdu[2]; sc.iocap_b[2] = pdu[3];
        build_public_key();                       /* send PKa                 */
        break;
    case SMP_PAIRING_PUBLIC_KEY:
        if (len < 65u) { return; }
        memcpy(sc.pk_peer, &pdu[1], 64);          /* PKb                      */
        if (compute_dhkey() != 0) { fail(SMP_FAIL_UNSPEC); }
        break;                                    /* wait for Confirm         */
    case SMP_PAIRING_CONFIRM:
        if (len < 17u) { return; }
        memcpy(sc.confirm_peer, &pdu[1], 16);     /* Cb, verified after Nb    */
        build_random(sc.na);                      /* send Na                  */
        break;
    case SMP_PAIRING_RANDOM: {
        uint8_t cb[16];
        if (len < 17u) { return; }
        memcpy(sc.nb, &pdu[1], 16);
        /* Verify Cb == f4(PKb_x, PKa_x, Nb, 0). */
        tiku_ble_smp_f4(&sc.pk_peer[0], &sc.pk_local[0], sc.nb, 0u, cb);
        if (memcmp(cb, sc.confirm_peer, 16) != 0) {
            fail(SMP_FAIL_CONFIRM); return;
        }
        derive_keys();
        {   /* Ea = f6(MacKey, Na, Nb, 0, IOcapA, A, B). */
            uint8_t ea[16], z[16];
            memset(z, 0, 16);
            tiku_ble_smp_f6(sc.mackey, sc.na, sc.nb, z, sc.iocap_a,
                            sc.at, sc.a, sc.bt, sc.b, ea);
            build_dhkey_check(ea);
        }
        break;
    }
    case SMP_PAIRING_DHKEY_CHECK: {
        uint8_t eb[16], z[16];
        if (len < 17u) { return; }
        memset(z, 0, 16);
        /* Expect Eb = f6(MacKey, Nb, Na, 0, IOcapB, B, A). */
        tiku_ble_smp_f6(sc.mackey, sc.nb, sc.na, z, sc.iocap_b,
                        sc.bt, sc.b, sc.at, sc.a, eb);
        sc.state = (memcmp(eb, &pdu[1], 16) == 0)
                 ? TIKU_BLE_SMP_STATE_DONE : TIKU_BLE_SMP_STATE_FAILED;
        if (sc.state == TIKU_BLE_SMP_STATE_FAILED) { fail(SMP_FAIL_DHKEY); }
        break;
    }
    default:
        break;
    }
}

/* --- responder (peripheral) receive path -------------------------------- */

static void feed_responder(uint8_t op, const uint8_t *pdu, uint16_t len)
{
    switch (op) {
    case SMP_PAIRING_REQUEST:
        if (len < 4u) { return; }
        sc.iocap_a[0] = pdu[1]; sc.iocap_a[1] = pdu[2]; sc.iocap_a[2] = pdu[3];
        build_pair_cmd(SMP_PAIRING_RESPONSE);
        break;
    case SMP_PAIRING_PUBLIC_KEY: {
        uint8_t cb[16];
        if (len < 65u) { return; }
        memcpy(sc.pk_peer, &pdu[1], 64);          /* PKa                      */
        if (compute_dhkey() != 0) { fail(SMP_FAIL_UNSPEC); return; }
        build_public_key();                       /* send PKb                 */
        /* Cb = f4(PKb_x, PKa_x, Nb, 0), then send Confirm. */
        tiku_ble_smp_f4(&sc.pk_local[0], &sc.pk_peer[0], sc.nb, 0u, cb);
        build_confirm(cb);
        break;
    }
    case SMP_PAIRING_RANDOM:
        if (len < 17u) { return; }
        memcpy(sc.na, &pdu[1], 16);
        derive_keys();                            /* both nonces known now    */
        build_random(sc.nb);                      /* send Nb                  */
        break;
    case SMP_PAIRING_DHKEY_CHECK: {
        uint8_t ea[16], eb[16], z[16];
        if (len < 17u) { return; }
        memset(z, 0, 16);
        /* Verify Ea = f6(MacKey, Na, Nb, 0, IOcapA, A, B). */
        tiku_ble_smp_f6(sc.mackey, sc.na, sc.nb, z, sc.iocap_a,
                        sc.at, sc.a, sc.bt, sc.b, ea);
        if (memcmp(ea, &pdu[1], 16) != 0) { fail(SMP_FAIL_DHKEY); return; }
        /* Eb = f6(MacKey, Nb, Na, 0, IOcapB, B, A). */
        tiku_ble_smp_f6(sc.mackey, sc.nb, sc.na, z, sc.iocap_b,
                        sc.bt, sc.b, sc.at, sc.a, eb);
        build_dhkey_check(eb);
        sc.state = TIKU_BLE_SMP_STATE_DONE;
        break;
    }
    default:
        break;
    }
}

int tiku_ble_smp_pair_feed(const uint8_t *pdu, uint16_t len)
{
    uint8_t op;
    if (pdu == (const uint8_t *)0 || len < 1u) {
        return 0;
    }
    op = pdu[0];
    if (op == SMP_PAIRING_FAILED) {
        sc.state = TIKU_BLE_SMP_STATE_FAILED;
        return 1;
    }
    /* Accept PDUs while pairing, and -- for the tail's sake -- a duplicate of
     * the last opcode even after DONE, so a peer that lost our final reply can
     * re-request it (we re-run the same handler, which is deterministic).
     * A duplicate re-runs its handler; clearing the queue first keeps the
     * regenerated PDUs correctly ordered.  On the retransmit link (no full LL
     * ACK yet) this is what makes the last DHKey Check reliably delivered. */
    if (sc.state != TIKU_BLE_SMP_STATE_PAIRING &&
        !(sc.state == TIKU_BLE_SMP_STATE_DONE && op == sc.last_rx_op)) {
        return 0;
    }
    if (op == sc.last_rx_op) {
        sc.outq_head = 0u; sc.outq_count = 0u;   /* drop stale, re-emit reply */
    } else {
        sc.last_rx_op = op;
    }
    if (sc.role == TIKU_BLE_SMP_ROLE_INITIATOR) {
        feed_initiator(op, pdu, len);
    } else {
        feed_responder(op, pdu, len);
    }
    return 1;
}

uint16_t tiku_ble_smp_pair_next(uint8_t *out, uint16_t cap)
{
    uint8_t len;
    if (sc.outq_count == 0u || out == (uint8_t *)0) {
        return 0u;
    }
    len = sc.outq_len[sc.outq_head];
    if (len > cap) {
        len = (uint8_t)cap;
    }
    memcpy(out, sc.outq[sc.outq_head], len);
    sc.outq_head = (uint8_t)((sc.outq_head + 1u) % OUTQ_DEPTH);
    sc.outq_count--;
    return len;
}

tiku_ble_smp_state_t tiku_ble_smp_pair_state(void)
{
    return sc.state;
}

int tiku_ble_smp_pair_ltk(uint8_t ltk[16])
{
    if (sc.state != TIKU_BLE_SMP_STATE_DONE) {
        return -1;
    }
    memcpy(ltk, sc.ltk, 16);
    return 0;
}
