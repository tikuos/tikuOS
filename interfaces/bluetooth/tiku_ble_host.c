/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.c - M33-side L2CAP (frag/recomb) + a TABLE-DRIVEN ATT/GATT
 * server.  Phase D: the ATT ops walk a declarative attribute table instead
 * of a hardcoded NUS if/else, so arbitrary services + characteristics are
 * served (here NUS + a Device Information service + a scratch service), with
 * long reads (Read Blob) and long writes (Prepare/Execute Write) past the MTU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_host.h>
#include <interfaces/bluetooth/tiku_ble_smp_pair.h>

#define HOST_FRAG_MAX 27u                        /* L2CAP bytes per data PDU  */
/* Largest L2CAP PDU we handle: max of an ATT payload (MTU) and the SMP
 * Pairing Public Key (65 B), each plus the 4-byte L2CAP header. */
#define HOST_L2_MAX   (((TIKU_BLE_HOST_MTU + 4u) > (TIKU_BLE_SMP_PDU_MAX + 4u)) \
                       ? (TIKU_BLE_HOST_MTU + 4u) : (TIKU_BLE_SMP_PDU_MAX + 4u))
#define HOST_H_TX     0x0014u                    /* NUS TX (notify source)    */
#define HOST_H_RX     0x0012u                    /* NUS RX (write target)     */
#define HOST_H_CCCD   0x0015u                    /* NUS TX CCCD               */
#define HOST_MODEL_LEN 72u                       /* > MTU-1 -> needs Read Blob*/
#define HOST_SCRATCH_CAP 80u                     /* long writable value       */

/* --- GATT attribute table ------------------------------------------------ */
#define GATT_READ   0x01u
#define GATT_WRITE  0x02u

typedef struct {
    uint16_t        handle;
    uint16_t        type16;      /* attr type as 16-bit UUID (0 => 128-bit)   */
    const uint8_t  *type128;     /* attr type as 128-bit UUID (NULL => 16-bit)*/
    uint8_t         perm;        /* GATT_READ | GATT_WRITE                    */
    uint8_t        *val;         /* value bytes (NULL for notify-only)        */
    uint16_t        len;         /* current value length                     */
    uint16_t        cap;         /* buffer capacity (writes clamp to this)   */
} host_attr_t;

/* NUS 128-bit UUIDs (base 6E400001-B5A3-...; byte[12] = 01 svc / 02 rx /03 tx)*/
static const uint8_t nus_svc_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t nus_rx_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x02u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t nus_tx_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x03u, 0x00u, 0x40u, 0x6Eu };
/* Characteristic declaration values: [props][value handle:2][char UUID]. */
static const uint8_t nus_rx_decl[19] = {
    0x0Cu, 0x12u, 0x00u,                         /* Write|WriteNoRsp, vh 0x12 */
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x02u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t nus_tx_decl[19] = {
    0x10u, 0x14u, 0x00u,                         /* Notify, vhandle 0x14      */
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x03u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t dis_svc[2]     = { 0x0Au, 0x18u };   /* 0x180A           */
static const uint8_t model_decl[5]  = { 0x02u, 0x22u, 0x00u, 0x24u, 0x2Au };
static const uint8_t scr_svc[2]     = { 0x01u, 0xFEu };   /* 0xFE01 (custom)  */
static const uint8_t scr_decl[5]    = { 0x0Au, 0x32u, 0x00u, 0x02u, 0xFEu };

static uint8_t  host_rx[TIKU_BLE_HOST_MTU];      /* NUS RX value + byte pipe  */
static uint8_t  host_cccd[2];                    /* NUS TX CCCD value         */
static uint8_t  model_val[HOST_MODEL_LEN];       /* long readable (Read Blob) */
static uint8_t  scratch_val[HOST_SCRATCH_CAP];   /* long read/write           */

/* Handle-ordered attribute table: NUS, Device Information, scratch. */
static host_attr_t gatt_db[] = {
 { 0x0010u, 0x2800u, 0, GATT_READ, (uint8_t *)nus_svc_uuid, 16u, 16u },
 { 0x0011u, 0x2803u, 0, GATT_READ, (uint8_t *)nus_rx_decl, 19u, 19u },
 { 0x0012u, 0x0000u, nus_rx_uuid, GATT_WRITE, host_rx, 0u, sizeof(host_rx) },
 { 0x0013u, 0x2803u, 0, GATT_READ, (uint8_t *)nus_tx_decl, 19u, 19u },
 { 0x0014u, 0x0000u, nus_tx_uuid, 0u, 0, 0u, 0u },        /* TX: notify-only  */
 { 0x0015u, 0x2902u, 0, GATT_READ | GATT_WRITE, host_cccd, 2u, 2u },
 { 0x0020u, 0x2800u, 0, GATT_READ, (uint8_t *)dis_svc, 2u, 2u },
 { 0x0021u, 0x2803u, 0, GATT_READ, (uint8_t *)model_decl, 5u, 5u },
 { 0x0022u, 0x2A24u, 0, GATT_READ, model_val, HOST_MODEL_LEN, HOST_MODEL_LEN },
 { 0x0030u, 0x2800u, 0, GATT_READ, (uint8_t *)scr_svc, 2u, 2u },
 { 0x0031u, 0x2803u, 0, GATT_READ, (uint8_t *)scr_decl, 5u, 5u },
 { 0x0032u, 0xFE02u, 0, GATT_READ | GATT_WRITE, scratch_val, 0u,
   sizeof(scratch_val) },
};
#define GATT_N  (sizeof(gatt_db) / sizeof(gatt_db[0]))

/* --- connection state ---------------------------------------------------- */
static uint8_t  host_sub;                         /* CCCD: notifications on   */
static uint8_t  host_rx_len;                      /* pending NUS RX bytes     */
static uint8_t  host_rc[HOST_L2_MAX];             /* RX recombination buffer  */
static uint16_t host_rc_len, host_rc_expect;
static uint8_t  host_tx[HOST_L2_MAX];             /* one pending TX L2CAP PDU */
static uint16_t host_tx_len, host_tx_off;
static uint8_t  host_sig_id;                      /* L2CAP signalling ident   */
static uint8_t  host_cpu_resp;                    /* 0 none, 1 accept, 2 reject*/
static uint16_t host_prep_h;                      /* Prepare-Write handle     */
static uint8_t  host_prep[HOST_SCRATCH_CAP];      /* accumulated long write   */
static uint16_t host_prep_len;
static uint8_t  host_frag_max = HOST_FRAG_MAX;   /* TX frag size (DLE raises) */
static uint16_t host_max_single;                 /* largest 1-PDU L2CAP RX'd  */

void tiku_ble_host_reset(void)
{
    uint16_t i;
    host_sub = 0u; host_rx_len = 0u;
    host_rc_len = 0u; host_rc_expect = 0u;
    host_tx_len = 0u; host_tx_off = 0u;
    host_sig_id = 0u; host_cpu_resp = 0u;
    host_prep_h = 0u; host_prep_len = 0u;
    host_frag_max = HOST_FRAG_MAX;               /* F1: pre-DLE default (27)  */
    host_max_single = 0u;
    host_cccd[0] = 0u; host_cccd[1] = 0u;
    /* Deterministic long values so a client can verify a long read/write. */
    for (i = 0u; i < HOST_MODEL_LEN; i++) {
        model_val[i] = (uint8_t)('0' + (i % 10u));
    }
    gatt_db[2].len = 0u;                          /* NUS RX value cleared     */
    gatt_db[11].len = 0u;                         /* scratch value cleared    */
    tiku_ble_smp_pair_reset();                    /* SMP: fresh per connection*/
}

int tiku_ble_host_subscribed(void)
{
    return (host_sub != 0u) ? 1 : 0;
}

static host_attr_t *host_find(uint16_t handle)
{
    uint16_t i;
    for (i = 0u; i < GATT_N; i++) {
        if (gatt_db[i].handle == handle) {
            return &gatt_db[i];
        }
    }
    return (host_attr_t *)0;
}

/* End handle of the service that @p idx starts: one before the next service. */
static uint16_t host_service_end(uint16_t idx)
{
    uint16_t j;
    for (j = idx + 1u; j < GATT_N; j++) {
        if (gatt_db[j].type16 == 0x2800u || gatt_db[j].type16 == 0x2801u) {
            return (uint16_t)(gatt_db[j].handle - 1u);
        }
    }
    return gatt_db[GATT_N - 1u].handle;
}

/* Queue a whole L2CAP PDU for (possibly fragmented) TX. */
static void host_store_tx(const uint8_t *pdu, uint16_t len)
{
    uint16_t i;
    if (len > (uint16_t)sizeof(host_tx)) {
        len = (uint16_t)sizeof(host_tx);
    }
    for (i = 0u; i < len; i++) {
        host_tx[i] = pdu[i];
    }
    host_tx_len = len;
    host_tx_off = 0u;
}

/* Wrap an ATT PDU (<= MTU bytes) in an L2CAP frame (CID 0x0004) and queue it. */
static void host_reply(const uint8_t *att, uint16_t alen)
{
    uint8_t  pdu[HOST_L2_MAX];
    uint16_t i;
    if (alen > (uint16_t)TIKU_BLE_HOST_MTU) {
        alen = (uint16_t)TIKU_BLE_HOST_MTU;
    }
    pdu[0] = (uint8_t)alen; pdu[1] = 0u;         /* L2CAP length              */
    pdu[2] = 0x04u; pdu[3] = 0x00u;              /* CID = ATT                 */
    for (i = 0u; i < alen; i++) {
        pdu[4u + i] = att[i];
    }
    host_store_tx(pdu, (uint16_t)(4u + alen));
}

static void host_error(uint8_t req_op, uint16_t h, uint8_t err)
{
    uint8_t e[5];
    e[0] = 0x01u; e[1] = req_op;
    e[2] = (uint8_t)h; e[3] = (uint8_t)(h >> 8);
    e[4] = err;
    host_reply(e, 5u);
}

/* Read By Group Type (0x10): service discovery (type 0x2800/0x2801). */
static void host_read_by_group(const uint8_t *att)
{
    uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    uint16_t end   = (uint16_t)(att[3] | ((uint16_t)att[4] << 8));
    uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
    uint16_t i;
    if (type != 0x2800u && type != 0x2801u) {
        host_error(0x10u, start, 0x0Au);
        return;
    }
    for (i = 0u; i < GATT_N; i++) {
        if (gatt_db[i].type16 == type && gatt_db[i].handle >= start &&
            gatt_db[i].handle <= end) {
            uint8_t  r[4 + 2 + 16];
            uint16_t eh = host_service_end(i), k;
            r[0] = 0x11u; r[1] = (uint8_t)(4u + gatt_db[i].len);
            r[2] = (uint8_t)gatt_db[i].handle;
            r[3] = (uint8_t)(gatt_db[i].handle >> 8);
            r[4] = (uint8_t)eh; r[5] = (uint8_t)(eh >> 8);
            for (k = 0u; k < gatt_db[i].len; k++) {
                r[6u + k] = gatt_db[i].val[k];
            }
            host_reply(r, (uint16_t)(6u + gatt_db[i].len));
            return;
        }
    }
    host_error(0x10u, start, 0x0Au);
}

/* Read By Type (0x08): characteristic discovery (0x2803) or read-by-UUID. */
static void host_read_by_type(const uint8_t *att)
{
    uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    uint16_t end   = (uint16_t)(att[3] | ((uint16_t)att[4] << 8));
    uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
    uint16_t i;
    for (i = 0u; i < GATT_N; i++) {
        if (gatt_db[i].type16 == type && gatt_db[i].handle >= start &&
            gatt_db[i].handle <= end) {
            uint8_t  r[2 + 2 + 22];
            uint16_t n = gatt_db[i].len, k;
            if (n > (uint16_t)(TIKU_BLE_HOST_MTU - 4u)) {
                n = (uint16_t)(TIKU_BLE_HOST_MTU - 4u);
            }
            r[0] = 0x09u; r[1] = (uint8_t)(2u + n);
            r[2] = (uint8_t)gatt_db[i].handle;
            r[3] = (uint8_t)(gatt_db[i].handle >> 8);
            for (k = 0u; k < n; k++) {
                r[4u + k] = gatt_db[i].val[k];
            }
            host_reply(r, (uint16_t)(4u + n));
            return;
        }
    }
    host_error(0x08u, start, 0x0Au);
}

/* Find Information (0x04): descriptor discovery -> [handle, UUID] pairs. */
static void host_find_info(const uint8_t *att)
{
    uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    uint16_t end   = (uint16_t)(att[3] | ((uint16_t)att[4] << 8));
    uint16_t i;
    for (i = 0u; i < GATT_N; i++) {
        if (gatt_db[i].handle >= start && gatt_db[i].handle <= end) {
            uint8_t r[2 + 2 + 16], k;
            r[0] = 0x05u;
            r[2] = (uint8_t)gatt_db[i].handle;
            r[3] = (uint8_t)(gatt_db[i].handle >> 8);
            if (gatt_db[i].type128 != (const uint8_t *)0) {
                r[1] = 0x02u;                    /* 128-bit format            */
                for (k = 0u; k < 16u; k++) {
                    r[4u + k] = gatt_db[i].type128[k];
                }
                host_reply(r, 20u);
            } else {
                r[1] = 0x01u;                    /* 16-bit format             */
                r[4] = (uint8_t)gatt_db[i].type16;
                r[5] = (uint8_t)(gatt_db[i].type16 >> 8);
                host_reply(r, 6u);
            }
            return;
        }
    }
    host_error(0x04u, start, 0x0Au);
}

/* Read (0x0A) / Read Blob (0x0C): value at @p offset, up to MTU-1 bytes. */
static void host_read(const uint8_t *att, uint16_t offset, uint8_t is_blob)
{
    uint16_t handle = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    host_attr_t *a = host_find(handle);
    uint8_t  r[1 + (TIKU_BLE_HOST_MTU - 1u)];
    uint16_t n, k;
    if (a == (host_attr_t *)0) {
        host_error(is_blob ? 0x0Cu : 0x0Au, handle, 0x0Au);
        return;
    }
    if ((a->perm & GATT_READ) == 0u || a->val == (uint8_t *)0) {
        host_error(is_blob ? 0x0Cu : 0x0Au, handle, 0x02u); /* Read Not Perm  */
        return;
    }
    if (offset > a->len) {
        host_error(0x0Cu, handle, 0x07u);        /* Invalid Offset            */
        return;
    }
    n = (uint16_t)(a->len - offset);
    if (n > (uint16_t)(TIKU_BLE_HOST_MTU - 1u)) {
        n = (uint16_t)(TIKU_BLE_HOST_MTU - 1u);
    }
    r[0] = is_blob ? 0x0Du : 0x0Bu;              /* Read / Read Blob Response */
    for (k = 0u; k < n; k++) {
        r[1u + k] = a->val[offset + k];
    }
    host_reply(r, (uint16_t)(1u + n));
}

/* Apply a value to a writable attribute + fire the NUS byte-pipe hooks. */
static void host_apply_write(host_attr_t *a, const uint8_t *v, uint16_t n)
{
    uint16_t k;
    if (n > a->cap) {
        n = a->cap;
    }
    for (k = 0u; k < n; k++) {
        a->val[k] = v[k];
    }
    a->len = n;
    if (a->handle == HOST_H_CCCD) {
        host_sub = a->val[0];                    /* subscription state        */
    } else if (a->handle == HOST_H_RX) {
        host_rx_len = (uint8_t)n;                /* NUS RX byte pipe          */
    }
}

/* Write (0x12) / Write Command (0x52). */
static void host_write(const uint8_t *att, uint16_t alen, uint8_t with_rsp)
{
    uint16_t handle = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    host_attr_t *a = host_find(handle);
    if (a == (host_attr_t *)0) {
        if (with_rsp) {
            host_error(0x12u, handle, 0x0Au);
        }
        return;
    }
    if ((a->perm & GATT_WRITE) == 0u || a->val == (uint8_t *)0) {
        if (with_rsp) {
            host_error(0x12u, handle, 0x03u);    /* Write Not Permitted       */
        }
        return;
    }
    host_apply_write(a, &att[3], (uint16_t)(alen - 3u));
    if (with_rsp) {
        uint8_t rsp = 0x13u;
        host_reply(&rsp, 1u);
    }
}

/* Prepare Write (0x16): accumulate a long-write fragment; echo it back. */
static void host_prepare_write(const uint8_t *att, uint16_t alen)
{
    uint16_t handle = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
    uint16_t offset = (uint16_t)(att[3] | ((uint16_t)att[4] << 8));
    uint16_t n = (uint16_t)(alen - 5u), k;
    if (handle != host_prep_h) {                 /* new target: restart       */
        host_prep_h = handle; host_prep_len = 0u;
    }
    for (k = 0u; k < n && (offset + k) < (uint16_t)sizeof(host_prep); k++) {
        host_prep[offset + k] = att[5u + k];
    }
    if ((uint16_t)(offset + n) > host_prep_len) {
        host_prep_len = (uint16_t)(offset + n);
    }
    {   /* echo [0x17][handle][offset][value] */
        uint8_t  r[5 + (TIKU_BLE_HOST_MTU - 5u)];
        r[0] = 0x17u;
        r[1] = att[1]; r[2] = att[2];
        r[3] = att[3]; r[4] = att[4];
        for (k = 0u; k < n; k++) {
            r[5u + k] = att[5u + k];
        }
        host_reply(r, (uint16_t)(5u + n));
    }
}

/* Execute Write (0x18): flags 1 = commit the accumulated write, 0 = cancel. */
static void host_execute_write(const uint8_t *att)
{
    uint8_t rsp = 0x19u;
    if (att[1] == 0x01u && host_prep_h != 0u) {
        host_attr_t *a = host_find(host_prep_h);
        if (a != (host_attr_t *)0 && (a->perm & GATT_WRITE) != 0u) {
            host_apply_write(a, host_prep, host_prep_len);
        }
    }
    host_prep_h = 0u; host_prep_len = 0u;
    host_reply(&rsp, 1u);
}

/* ATT dispatch on a whole, recombined L2CAP PDU (CID 0x0004). */
static void host_att(const uint8_t *l2cap, uint16_t len)
{
    const uint8_t *att;
    uint16_t alen;
    uint8_t  op;

    if (len < 5u) {
        return;
    }
    att = &l2cap[4];
    alen = (uint16_t)(len - 4u);
    op = att[0];

    if (op == 0x02u) {                           /* Exchange MTU Request      */
        uint8_t m[3];
        m[0] = 0x03u; m[1] = (uint8_t)TIKU_BLE_HOST_MTU; m[2] = 0u;
        host_reply(m, 3u);
    } else if (op == 0x10u && alen >= 7u) {      /* Read By Group Type        */
        host_read_by_group(att);
    } else if (op == 0x08u && alen >= 7u) {      /* Read By Type              */
        host_read_by_type(att);
    } else if (op == 0x04u && alen >= 5u) {      /* Find Information          */
        host_find_info(att);
    } else if (op == 0x0Au && alen >= 3u) {      /* Read Request              */
        host_read(att, 0u, 0u);
    } else if (op == 0x0Cu && alen >= 5u) {      /* Read Blob Request         */
        host_read(att, (uint16_t)(att[3] | ((uint16_t)att[4] << 8)), 1u);
    } else if (op == 0x12u && alen >= 3u) {      /* Write Request             */
        host_write(att, alen, 1u);
    } else if (op == 0x52u && alen >= 3u) {      /* Write Command             */
        host_write(att, alen, 0u);
    } else if (op == 0x16u && alen >= 5u) {      /* Prepare Write Request     */
        host_prepare_write(att, alen);
    } else if (op == 0x18u && alen >= 2u) {      /* Execute Write Request     */
        host_execute_write(att);
    } else {
        host_error(op, 0u, 0x06u);               /* Request Not Supported     */
    }
}

/* L2CAP signalling (CID 0x0005): note the central's Conn Param Update Rsp. */
static void host_sig(const uint8_t *l2cap, uint16_t len)
{
    const uint8_t *sig;
    if (len < 6u) {
        return;
    }
    sig = &l2cap[4];
    if (sig[0] == 0x13u) {
        uint16_t result = (uint16_t)(sig[4] | ((uint16_t)sig[5] << 8));
        host_cpu_resp = (result == 0x0000u) ? 1u : 2u;
    }
}

/* --- SMP (CID 0x0006): drive the LE-SC pairing responder ----------------- */

void tiku_ble_host_smp_start(const uint8_t inita[6], uint8_t at,
                             const uint8_t adva[6], uint8_t bt)
{
    /* A = initiator (central) = inita; B = responder (us) = adva. */
    (void)tiku_ble_smp_pair_start(TIKU_BLE_SMP_ROLE_RESPONDER,
                                  inita, at, adva, bt);
}

int tiku_ble_host_smp_pump(void)
{
    uint8_t  smp[TIKU_BLE_SMP_PDU_MAX];
    uint8_t  pdu[HOST_L2_MAX];
    uint16_t n, i;

    if (host_tx_len != 0u) {                     /* TX still draining         */
        return 0;
    }
    n = tiku_ble_smp_pair_next(smp, sizeof(smp));
    if (n == 0u) {
        return 0;
    }
    pdu[0] = (uint8_t)n; pdu[1] = 0u;            /* L2CAP length              */
    pdu[2] = 0x06u; pdu[3] = 0x00u;             /* CID = SMP                 */
    for (i = 0u; i < n; i++) {
        pdu[4u + i] = smp[i];
    }
    host_store_tx(pdu, (uint16_t)(4u + n));
    return 1;
}

int tiku_ble_host_smp_state(void)
{
    return (int)tiku_ble_smp_pair_state();
}

int tiku_ble_host_smp_ltk(uint8_t ltk[16])
{
    return tiku_ble_smp_pair_ltk(ltk);
}

/* Feed a recombined SMP L2CAP PDU to the engine, then stage the first reply. */
static void host_smp(const uint8_t *l2cap, uint16_t len)
{
    if (len < 5u) {
        return;
    }
    (void)tiku_ble_smp_pair_feed(&l2cap[4], (uint16_t)(len - 4u));
    (void)tiku_ble_host_smp_pump();
}

int tiku_ble_host_rx(const uint8_t *frag, uint16_t len, uint8_t llid)
{
    uint16_t i;

    if (llid == 2u) {                            /* start of a new L2CAP PDU  */
        host_rc_len = 0u;
        host_rc_expect = 0u;
        if (len >= 2u) {
            host_rc_expect = (uint16_t)(frag[0] |
                                        ((uint16_t)frag[1] << 8)) + 4u;
            if (host_rc_expect > (uint16_t)sizeof(host_rc)) {
                host_rc_expect = (uint16_t)sizeof(host_rc);
            }
        }
    }
    for (i = 0u; i < len && host_rc_len < (uint16_t)sizeof(host_rc); i++) {
        host_rc[host_rc_len++] = frag[i];
    }
    if (host_rc_expect >= 4u && host_rc_len >= host_rc_expect) {
        if (llid == 2u && host_rc_expect > host_max_single) {
            host_max_single = host_rc_expect;    /* F1: whole PDU in one LL PDU*/
        }
        if (host_rc[2] == 0x05u && host_rc[3] == 0x00u) {
            host_sig(host_rc, host_rc_expect);   /* L2CAP signalling channel  */
        } else if (host_rc[2] == 0x06u && host_rc[3] == 0x00u) {
            host_smp(host_rc, host_rc_expect);   /* SMP pairing (CID 0x0006)  */
        } else {
            host_att(host_rc, host_rc_expect);   /* ATT (CID 0x0004)          */
        }
        host_rc_len = 0u; host_rc_expect = 0u;
        return 1;
    }
    return 0;
}

uint16_t tiku_ble_host_next_tx(uint8_t *out, uint16_t out_cap, uint8_t *llid)
{
    uint16_t n, i;

    if (host_tx_off >= host_tx_len) {
        return 0u;                               /* nothing pending / done    */
    }
    n = (uint16_t)(host_tx_len - host_tx_off);
    if (n > host_frag_max) {                     /* DLE-negotiated size       */
        n = host_frag_max;
    }
    if (n > out_cap) {
        n = out_cap;
    }
    for (i = 0u; i < n; i++) {
        out[i] = host_tx[host_tx_off + i];
    }
    if (llid != (uint8_t *)0) {
        *llid = (host_tx_off == 0u) ? 2u : 1u;   /* first vs continuation     */
    }
    host_tx_off += n;
    if (host_tx_off >= host_tx_len) {
        host_tx_len = 0u; host_tx_off = 0u;      /* PDU fully sent            */
    }
    return n;
}

uint16_t tiku_ble_host_nus_recv(uint8_t *buf, uint16_t cap)
{
    uint8_t n = host_rx_len, i;
    if (n == 0u) {
        return 0u;
    }
    if (n > cap) {
        n = (uint8_t)cap;
    }
    for (i = 0u; i < n; i++) {
        buf[i] = host_rx[i];
    }
    host_rx_len = 0u;
    return n;
}

int tiku_ble_host_nus_notify(const uint8_t *data, uint16_t len)
{
    uint8_t  pdu[HOST_L2_MAX];
    uint16_t n, i;

    if (host_sub == 0u || data == (const uint8_t *)0) {
        return -1;
    }
    if (host_tx_len != 0u) {
        return -2;                               /* a PDU is still draining   */
    }
    n = len;
    if (n > (uint16_t)(TIKU_BLE_HOST_MTU - 3u)) {
        n = (uint16_t)(TIKU_BLE_HOST_MTU - 3u);
    }
    pdu[0] = (uint8_t)(3u + n); pdu[1] = 0u;
    pdu[2] = 0x04u; pdu[3] = 0x00u;
    pdu[4] = 0x1Bu;                              /* Handle Value Notification */
    pdu[5] = (uint8_t)HOST_H_TX;
    pdu[6] = (uint8_t)(HOST_H_TX >> 8);
    for (i = 0u; i < n; i++) {
        pdu[7u + i] = data[i];
    }
    host_store_tx(pdu, (uint16_t)(7u + n));
    return 0;
}

int tiku_ble_host_request_conn_param(uint16_t interval_min,
                                     uint16_t interval_max,
                                     uint16_t latency, uint16_t timeout)
{
    uint8_t pdu[16];
    if (host_tx_len != 0u) {
        return -2;
    }
    host_cpu_resp = 0u;
    pdu[0] = 12u; pdu[1] = 0u;
    pdu[2] = 0x05u; pdu[3] = 0x00u;              /* CID = signalling          */
    pdu[4] = 0x12u;                              /* Conn Param Update Request */
    pdu[5] = ++host_sig_id;
    pdu[6] = 0x08u; pdu[7] = 0x00u;
    pdu[8]  = (uint8_t)interval_min; pdu[9]  = (uint8_t)(interval_min >> 8);
    pdu[10] = (uint8_t)interval_max; pdu[11] = (uint8_t)(interval_max >> 8);
    pdu[12] = (uint8_t)latency;      pdu[13] = (uint8_t)(latency >> 8);
    pdu[14] = (uint8_t)timeout;      pdu[15] = (uint8_t)(timeout >> 8);
    host_store_tx(pdu, 16u);
    return 0;
}

int tiku_ble_host_conn_param_result(void)
{
    return (host_cpu_resp == 1u) ? 1 : ((host_cpu_resp == 2u) ? -1 : 0);
}

/* Phase F1 (DLE): set the TX fragment size (the negotiated max LL payload). */
void tiku_ble_host_set_frag_max(uint8_t n)
{
    host_frag_max = (n < HOST_FRAG_MAX) ? HOST_FRAG_MAX : n;
}

/* Largest L2CAP PDU received whole in a single LL PDU (> 31 proves DLE). */
uint16_t tiku_ble_host_max_single_frag(void)
{
    return host_max_single;
}
