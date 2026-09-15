/* Production adapter for the opt-in configuration engine. Included after the
 * existing name/clock handlers so recovery shares their platform policy.
 * SPDX-License-Identifier: Apache-2.0 */
#include <kernel/vfs/tiku_vfs_config.h>

#if defined(PLATFORM_MSP430) || defined(PLATFORM_NORDIC)
/* Never put these two banks in the same whole-region flash mirror. The
 * alignment also keeps the banks in separate Nordic RRAM write lines. */
static TIKU_DURABLE uint8_t cfg_banks[2][TIKU_CFG_BANK_BYTES]
    __attribute__((aligned(16)));
static tiku_cfg_t cfg_service;
static uint8_t cfg_applying;

static int cfg_io_read(void *ctx, unsigned bank, size_t off, void *out, size_t n)
{
    (void)ctx;
    if (bank > 1 || off > TIKU_CFG_BANK_BYTES || n > TIKU_CFG_BANK_BYTES - off)
        return -1;
    tiku_mem_arch_nvm_read(out, cfg_banks[bank] + off, (tiku_mem_arch_size_t)n);
    return 0;
}
static int cfg_io_write(void *ctx, unsigned bank, size_t off,
                         const void *in, size_t n)
{
    uint16_t saved;
    (void)ctx;
    if (bank > 1 || off > TIKU_CFG_BANK_BYTES || n > TIKU_CFG_BANK_BYTES - off)
        return -1;
    saved = tiku_mpu_unlock_nvm();
    tiku_mem_arch_nvm_write(cfg_banks[bank] + off, in, (tiku_mem_arch_size_t)n);
    tiku_mpu_lock_nvm(saved);
    return memcmp(cfg_banks[bank] + off, in, n) ? -1 : 0;
}
static int cfg_name_normalize(const char *p, size_t n, char out[TIKU_CFG_VALUE])
{
    size_t i;
    if (!n || n > DEVICE_NAME_MAX) return -1;
    for (i = 0; i < n; i++) if ((uint8_t)p[i] < 32 || p[i] == 127) return -1;
    memcpy(out, p, n); return (int)n;
}
static int cfg_clock_normalize(const char *p, size_t n, char out[TIKU_CFG_VALUE])
{
    uint32_t hz = 0;
    size_t i;
    unsigned j;
    if (!n || n > 10 || strcmp(tiku_cpu_freq_change_mode(), "reboot")) return -1;
    for (i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9' ||
            hz > (1000000000UL - (unsigned)(p[i] - '0')) / 10UL) return -1;
        hz = hz * 10 + (unsigned)(p[i] - '0');
    }
    if (!tiku_cpu_freq_available(1)) return -1;
    for (j = 0; j < 32; j++) {
        unsigned long v = tiku_cpu_freq_available(j);
        if (!v) break;
        if (v == hz) return snprintf(out, TIKU_CFG_VALUE, "%lu", (unsigned long)hz);
    }
    return -1;
}
static int cfg_name_reconcile(const char *p, size_t n)
{
    int rc = 0;
    cfg_applying = 1;
    if (device_name_persist[n] != 0 || memcmp(device_name_persist, p, n))
        rc = device_name_write(p, n);
    cfg_applying = 0;
    return rc == 0 && device_name_persist[n] == 0 &&
           !memcmp(device_name_persist, p, n) ? TIKU_CFG_APPLIED : TIKU_CFG_BLOCKED;
}
static int cfg_clock_reconcile(const char *p, size_t n)
{
    char actual[32], desired[32];
    int rc = 0;
    cpu_target_read(desired, sizeof desired);
    cfg_applying = 1;
    if (strlen(desired) != n + 1 || memcmp(desired, p, n))
        rc = cpu_target_write(p, n);
    cfg_applying = 0;
    cpu_target_read(desired, sizeof desired);
    if (rc || strlen(desired) != n + 1 || memcmp(desired, p, n))
        return TIKU_CFG_BLOCKED;
    cpu_freq_read(actual, sizeof actual);
    return strlen(actual) == n + 1 && !memcmp(actual, p, n)
        ? TIKU_CFG_APPLIED : TIKU_CFG_RESTART;
}
static const tiku_cfg_resource_t cfg_resources[] = {
    {1, 1, TIKU_VFS_CAP_FS, cfg_name_normalize, cfg_name_reconcile},
    {2, 1, TIKU_VFS_CAP_SYS, cfg_clock_normalize, cfg_clock_reconcile}
};
static const tiku_cfg_io_t cfg_io = {NULL, cfg_io_read, cfg_io_write};

static int cfg_vfs_error(int rc)
{
    switch (rc) {
    case 0: return 0;
    case TIKU_CFG_INVALID: return TIKU_VFS_EINVAL;
    case TIKU_CFG_DENIED: return TIKU_VFS_EPERM;
    case TIKU_CFG_CONFLICT: return TIKU_VFS_ECONFLICT;
    case TIKU_CFG_STALE: return TIKU_VFS_ESTALE;
    case TIKU_CFG_BUSY: return TIKU_VFS_EBUSY;
    case TIKU_CFG_UNINITIALIZED: return TIKU_VFS_ENOTSUP;
    case TIKU_CFG_CORRUPT: case TIKU_CFG_INCOMPATIBLE: return TIKU_VFS_ECORRUPT;
    default: return TIKU_VFS_EIO;
    }
}
static void cfg_hex(char *out, const uint8_t *in, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2*i] = digits[in[i] >> 4]; out[2*i+1] = digits[in[i] & 15];
    }
    out[2*n] = 0;
}
static int cfg_unhex(uint8_t *out, const char *p, size_t n)
{
    size_t i;
    if (n & 1u) return -1;
    for (i = 0; i < n; i++) {
        unsigned v = (unsigned char)p[i];
        if (v >= '0' && v <= '9') v -= '0';
        else if (v >= 'a' && v <= 'f') v -= 'a' - 10;
        else return -1;
        if (!(i & 1u)) out[i/2] = (uint8_t)(v << 4);
        else out[i/2] |= (uint8_t)v;
    }
    return 0;
}
static void cfg_service_init(void)
{
    if (tiku_cfg_open(&cfg_service, &cfg_io, cfg_resources, 2) == 0) {
        int rc = tiku_cfg_recover(&cfg_service);
        if (rc) cfg_service.status = rc;
    }
}
static int cfg_managed_write(uint32_t id, const char *p, size_t n, int *result)
{
    tiku_cfg_request_t q;
    tiku_cfg_receipt_t receipt;
    char old[TIKU_CFG_VALUE];
    size_t length;
    int rc;
    /* An unusable journal does not take the ordinary setting down with
     * it.  Nothing reconciles over a legacy write while the engine cannot
     * read its own banks, and no endpoint repairs them; a poisoned
     * instance and an exhausted counter keep refusing, because their
     * journal is intact and would put the old value back at the next
     * boot. */
    if (cfg_applying || cfg_service.status == TIKU_CFG_UNINITIALIZED ||
        cfg_service.status == TIKU_CFG_CORRUPT ||
        cfg_service.status == TIKU_CFG_INCOMPATIBLE) return 0;
    /* The shell's line ends in a newline; a managed value never does, so
     * the strip belongs to this caller rather than to the normalizer. */
    if (n && p[n - 1] == '\n') n--;
    memset(&q, 0, sizeof q);
    rc = tiku_cfg_get(&cfg_service, id, old, &length, &receipt);
    if (!rc) {
        memcpy(q.incarnation, tiku_cfg_incarnation(&cfg_service), TIKU_CFG_TOKEN);
        /* Local operations have no retry handle; their durable expected
         * revision still makes old remote operations conflict correctly. */
        memcpy(q.token, "local-vfs-write", 15);
        q.resource = id; q.expected_revision = receipt.revision;
        q.value = p; q.length = n;
        rc = tiku_cfg_submit(&cfg_service, &q, tiku_vfs_caller_cap_get(), &receipt);
        if (!rc && receipt.state == TIKU_CFG_BLOCKED) rc = TIKU_CFG_IO;
    }
    *result = cfg_vfs_error(rc); return 1;
}
static int cfg_status_read(char *buf, size_t max)
{
    char incarnation[33] = "-";
    const char *status = "fault";
    if (!cfg_service.status) {
        status = "ready";
        cfg_hex(incarnation, tiku_cfg_incarnation(&cfg_service), TIKU_CFG_TOKEN);
    } else if (cfg_service.status == TIKU_CFG_UNINITIALIZED) status = "unenrolled";
    return snprintf(buf, max, "v1:%s:%s\n", status, incarnation);
}
static int cfg_provision_write(const char *p, size_t n)
{
    uint8_t incarnation[TIKU_CFG_TOKEN];
    if (n && p[n-1] == '\n') n--;
    if (n != 32 || cfg_unhex(incarnation, p, n)) return TIKU_VFS_EINVAL;
    return cfg_vfs_error(tiku_cfg_provision(&cfg_service, incarnation,
                                           tiku_vfs_caller_cap_get()));
}
static int cfg_resource_read(uint32_t id, char *buf, size_t max)
{
    tiku_cfg_receipt_t receipt;
    char value[TIKU_CFG_VALUE], hex[65], incarnation[33];
    size_t n;
    int rc = tiku_cfg_get(&cfg_service, id, value, &n, &receipt);
    if (rc) return cfg_vfs_error(rc);
    cfg_hex(incarnation, tiku_cfg_incarnation(&cfg_service), TIKU_CFG_TOKEN);
    cfg_hex(hex, (const uint8_t *)value, n);
    return snprintf(buf, max, "v1:%s:%08lx:%s:%s\n", incarnation,
        (unsigned long)receipt.revision, tiku_cfg_state_name(receipt.state), hex);
}
static int cfg_resource_write(uint32_t id, const char *p, size_t n)
{
    tiku_cfg_request_t q;
    tiku_cfg_receipt_t receipt;
    uint8_t rev[4];
    uint8_t checksum[4];
    uint32_t crc;
    char value[TIKU_CFG_VALUE];
    int rc;
    if (n && p[n-1] == '\n') n--;
    if (n <= 84 || n > 84 + 2*TIKU_CFG_VALUE || p[n-9] != ':' ||
        cfg_unhex(checksum, p+n-8, 8)) return TIKU_VFS_EINVAL;
    crc = (uint32_t)checksum[0] << 24 | (uint32_t)checksum[1] << 16 |
          (uint32_t)checksum[2] << 8 | checksum[3];
    n -= 9;
    if (crc != tiku_cfg_crc32((const uint8_t *)p, n) || p[32] != ':' ||
        p[41] != ':' || p[74] != ':') return TIKU_VFS_EINVAL;
    memset(&q, 0, sizeof q);
    if (cfg_unhex(q.incarnation, p, 32) || cfg_unhex(rev, p+33, 8) ||
        cfg_unhex(q.token, p+42, 32) || cfg_unhex((uint8_t *)value, p+75, n-75))
        return TIKU_VFS_EINVAL;
    q.expected_revision = (uint32_t)rev[0] << 24 | (uint32_t)rev[1] << 16 |
                          (uint32_t)rev[2] << 8 | rev[3];
    q.resource = id; q.value = value; q.length = (n-75)/2;
    rc = tiku_cfg_submit(&cfg_service, &q, tiku_vfs_caller_cap_get(), &receipt);
    if (!rc && !receipt.duplicate)
        tiku_vfs_notify(tiku_vfs_resolve(id == 1 ? "/sys/device/name" : "/sys/cpu/freq_target"));
    return cfg_vfs_error(rc);
}
static int cfg_name_read(char *b, size_t n) { return cfg_resource_read(1, b, n); }
static int cfg_freq_read(char *b, size_t n) { return cfg_resource_read(2, b, n); }
static int cfg_name_write(const char *b, size_t n) { return cfg_resource_write(1, b, n); }
static int cfg_freq_write(const char *b, size_t n) { return cfg_resource_write(2, b, n); }
static int cfg_history_read(char *buf, size_t max)
{
    unsigned i;
    size_t at = 0;
    int rc;
    tiku_cfg_request_t q;
    tiku_cfg_receipt_t receipt;
    char value[TIKU_CFG_VALUE], hex[65], token[33];
    if (cfg_service.status) return cfg_vfs_error(cfg_service.status);
    for (i = 0; i < TIKU_CFG_HISTORY; i++) {
        size_t room = at < max ? max - at : 0;
        rc = tiku_cfg_history(&cfg_service, i, &q, value, &receipt);
        if (rc == TIKU_CFG_STALE) break;
        if (rc) return cfg_vfs_error(rc);
        cfg_hex(hex, (const uint8_t *)value, q.length); cfg_hex(token, q.token, TIKU_CFG_TOKEN);
        rc = snprintf(buf + (at < max ? at : max), room, "%lu:%08lx:%s:%s:%s\n",
            (unsigned long)q.resource, (unsigned long)receipt.revision,
            token, tiku_cfg_state_name(receipt.state), hex);
        if (rc < 0) return TIKU_VFS_EIO;
        at += (size_t)rc;
    }
    if (!at && max) buf[0] = 0;
    return (int)at;
}
static int cfg_manifest_read(char *b, size_t n)
{
    return snprintf(b, n, "v1\n1:1:/sys/device/name:/sys/config/name:configuration:reconcile\n"
        "2:1:/sys/cpu/freq_target:/sys/config/frequency:configuration:reboot\n");
}
static const tiku_vfs_node_t cfg_children[] = {
    {"status", TIKU_VFS_FILE, cfg_status_read, NULL, NULL, 0},
    {"manifest", TIKU_VFS_FILE, cfg_manifest_read, NULL, NULL, 0},
    {"provision", TIKU_VFS_FILE, cfg_status_read, cfg_provision_write, NULL, 0, NULL, NULL,
        TIKU_VFS_CAP_FS | TIKU_VFS_CAP_SYS},
    {"name", TIKU_VFS_FILE, cfg_name_read, cfg_name_write, NULL, 0, NULL, NULL, TIKU_VFS_CAP_FS},
    {"frequency", TIKU_VFS_FILE, cfg_freq_read, cfg_freq_write, NULL, 0, NULL, NULL, TIKU_VFS_CAP_SYS},
    {"history", TIKU_VFS_FILE, cfg_history_read, NULL, NULL, 0}
};
#else
static int cfg_managed_write(uint32_t id, const char *p, size_t n, int *result)
{ (void)id; (void)p; (void)n; (void)result; return 0; }
static void cfg_service_init(void) {}
static int cfg_status_read(char *b, size_t n)
{ return snprintf(b, n, "v1:unsupported:-\n"); }
static const tiku_vfs_node_t cfg_children[] = {
    {"status", TIKU_VFS_FILE, cfg_status_read, NULL, NULL, 0}
};
#endif
