/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic_vfs.c - /dev/mic, the samples as a character device, and
 * /sys/audio/mic, its switch and counters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mic_vfs.h"
#include "tiku_mic.h"
#include <stdio.h>
#include <string.h>

/** @brief /dev/mic at offset 0: the oldest waiting samples, little-endian
 *  PCM16, as many as @p max holds; none while capture runs and the ring is
 *  empty.  A positive offset ends the read. */
static int mic_bytes(uint32_t offset, char *buf, size_t max, uint32_t *total)
{
    static int16_t chunk[TIKU_MIC_BLOCK];
    tiku_mic_status_t st;
    size_t done = 0;

    if (total != NULL) *total = 0;
    if (offset != 0) return 0;
    tiku_mic_status(&st);
    if (st.state == TIKU_MIC_IDLE) return TIKU_VFS_ENOTSUP;
    if (st.state == TIKU_MIC_FAILED && st.buffered == 0) return TIKU_VFS_EIO;
    while (max - done >= sizeof(int16_t)) {
        size_t want = (max - done) / sizeof(int16_t);
        size_t n = tiku_mic_read(chunk, want < TIKU_MIC_BLOCK ? want : TIKU_MIC_BLOCK);

        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            buf[done++] = (char)(chunk[i] & 0xff);
            buf[done++] = (char)((uint16_t)chunk[i] >> 8);
        }
    }
    if (total != NULL) *total = (uint32_t)done;
    return (int)done;
}

/** @brief /dev/mic as text, the shell's read: up to 32 of the oldest
 *  waiting samples in decimal, each followed by a space, moved out as a
 *  byte read moves them. */
static int mic_text(char *buf, size_t max)
{
    int16_t chunk[32];
    tiku_mic_status_t st;
    size_t want = max / 7u, n, done = 0;

    tiku_mic_status(&st);
    if (st.state == TIKU_MIC_IDLE) return TIKU_VFS_ENOTSUP;
    if (st.state == TIKU_MIC_FAILED && st.buffered == 0) return TIKU_VFS_EIO;
    n = tiku_mic_read(chunk, want < 32u ? want : 32u);
    for (size_t i = 0; i < n; i++)
        done += (size_t)snprintf(buf + done, max - done, "%d ", (int)chunk[i]);
    if (done < max) buf[done] = '\0';
    return (int)done;
}

static const tiku_vfs_desc_t stream_desc = {
    .fresh = TIKU_VFS_FRESH_LIVE, .ecost = TIKU_VFS_E_PERIPH,
    .flags = TIKU_VFS_DF_CHAR | TIKU_VFS_DF_READ_CONSUMES, .read_at = mic_bytes
};

static const tiku_vfs_node_t dev_node = {
    .name = "mic", .type = TIKU_VFS_FILE, .read = mic_text, .desc = &stream_desc
};

static tiku_mic_status_t now(void)
{
    tiku_mic_status_t st;

    tiku_mic_status(&st);
    return st;
}

static int enable_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", now().state == TIKU_MIC_RUNNING ? 1u : 0u);
}

/** @brief "1" starts capture and "0" stops it; a start while running and a
 *  stop while idle change nothing. */
static int enable_write(const char *buf, size_t len)
{
    int rc;

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        len--;
    if (len != 1 || (buf[0] != '0' && buf[0] != '1')) return TIKU_VFS_EINVAL;
    if (buf[0] == '0') {
        tiku_mic_stop();
        return TIKU_VFS_OK;
    }
    if (now().state == TIKU_MIC_RUNNING) return TIKU_VFS_OK;
    rc = tiku_mic_start();
    return rc == TIKU_MIC_OK ? TIKU_VFS_OK :
           rc == TIKU_MIC_ERR_MEMORY ? TIKU_VFS_E2BIG :
           rc == TIKU_MIC_ERR_DEVICE ? TIKU_VFS_EIO : TIKU_VFS_EBUSY;
}

static int state_read(char *buf, size_t max)
{
    static const char *const names[] = {"idle", "running", "failed"};
    unsigned s = now().state;

    return snprintf(buf, max, "%s\n", s < 3u ? names[s] : "failed");
}

static int rate_read(char *buf, size_t max)
{ return snprintf(buf, max, "%u\n", TIKU_MIC_RATE); }
static int buffered_read(char *buf, size_t max)
{ return snprintf(buf, max, "%lu\n", (unsigned long)now().buffered); }
static int captured_read(char *buf, size_t max)
{ return snprintf(buf, max, "%lu\n", (unsigned long)now().captured); }
static int dropped_read(char *buf, size_t max)
{ return snprintf(buf, max, "%lu\n", (unsigned long)now().dropped); }
static int faults_read(char *buf, size_t max)
{ return snprintf(buf, max, "%lu\n", (unsigned long)now().faults); }
static int gain_read(char *buf, size_t max)
{ return snprintf(buf, max, "%u\n", (unsigned)now().gain); }

/** @brief A whole number of dB, 0..TIKU_MIC_GAIN_MAX. */
static int gain_write(const char *buf, size_t len)
{
    int db = 0, rc;
    size_t i = 0;

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        len--;
    if (len == 0 || len > 2) return TIKU_VFS_EINVAL;
    for (; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9') return TIKU_VFS_EINVAL;
        db = db * 10 + (buf[i] - '0');
    }
    rc = tiku_mic_gain(db);
    return rc == TIKU_MIC_OK ? TIKU_VFS_OK : rc == TIKU_MIC_ERR_RANGE ? TIKU_VFS_ERANGE :
           rc == TIKU_MIC_ERR_DEVICE ? TIKU_VFS_EIO : TIKU_VFS_EBUSY;
}

static int peak_read(char *buf, size_t max)
{ return snprintf(buf, max, "%u\n", (unsigned)now().peak); }

static const tiku_vfs_desc_t switch_desc = {
    .vtype = TIKU_VFS_T_BOOL, .unit = TIKU_VFS_U_BOOL, .fresh = TIKU_VFS_FRESH_CACHED
};
static const tiku_vfs_desc_t rate_desc = {
    .vtype = TIKU_VFS_T_U32, .unit = TIKU_VFS_U_HERTZ, .fresh = TIKU_VFS_FRESH_STATIC
};
static const tiku_vfs_desc_t count_desc = {
    .vtype = TIKU_VFS_T_U32, .unit = TIKU_VFS_U_COUNT, .fresh = TIKU_VFS_FRESH_LIVE
};
static const tiku_vfs_desc_t gain_desc = {
    .vtype = TIKU_VFS_T_U32, .fresh = TIKU_VFS_FRESH_CACHED, .flags = TIKU_VFS_DF_RANGE,
    .vmax = TIKU_MIC_GAIN_MAX
};
static const tiku_vfs_desc_t level_desc = {
    .vtype = TIKU_VFS_T_U32, .fresh = TIKU_VFS_FRESH_LIVE, .flags = TIKU_VFS_DF_RANGE,
    .vmax = 32767
};

static const tiku_vfs_node_t mic_children[] = {
    {.name = "enable", .type = TIKU_VFS_FILE, .read = enable_read, .write = enable_write,
     .desc = &switch_desc, .req_cap = TIKU_VFS_CAP_HW},
    {.name = "gain", .type = TIKU_VFS_FILE, .read = gain_read, .write = gain_write,
     .desc = &gain_desc, .req_cap = TIKU_VFS_CAP_HW},
    {.name = "state", .type = TIKU_VFS_FILE, .read = state_read},
    {.name = "rate", .type = TIKU_VFS_FILE, .read = rate_read, .desc = &rate_desc},
    {.name = "buffered", .type = TIKU_VFS_FILE, .read = buffered_read, .desc = &count_desc},
    {.name = "captured", .type = TIKU_VFS_FILE, .read = captured_read, .desc = &count_desc},
    {.name = "dropped", .type = TIKU_VFS_FILE, .read = dropped_read, .desc = &count_desc},
    {.name = "faults", .type = TIKU_VFS_FILE, .read = faults_read, .desc = &count_desc},
    {.name = "peak", .type = TIKU_VFS_FILE, .read = peak_read, .desc = &level_desc},
};

static const tiku_vfs_node_t audio_children[] = {
    {.name = "mic", .type = TIKU_VFS_DIR, .children = mic_children,
     .child_count = sizeof mic_children / sizeof mic_children[0]},
};

static const tiku_vfs_node_t sys_node = {
    .name = "audio", .type = TIKU_VFS_DIR, .children = audio_children,
    .child_count = sizeof audio_children / sizeof audio_children[0]
};

const tiku_vfs_node_t *tiku_mic_vfs_dev(void) { return &dev_node; }
const tiku_vfs_node_t *tiku_mic_vfs_sys(void) { return &sys_node; }
