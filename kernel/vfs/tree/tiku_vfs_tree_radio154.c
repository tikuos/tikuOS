/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_radio154.c - /sys/radio/154 VFS nodes.
 *
 * The 802.15.4 radio as files over tiku_154's raw frames and background
 * listen: a frame is the PSDU as sent on air, with no MAC header added.
 * Another processor reaches them through its peer mount.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_radio154.h"

#if (TIKU_HAS_154_LISTEN + 0)

#include <hal/tiku_ieee154_hal.h>            /* TIKU_154_MAX_PSDU         */
#include <interfaces/radio/tiku_154.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** Level of the frame /sys/radio/154/rx last gave, in dBm. */
static int8_t last_rssi;

/** @brief Length of a written value without one trailing "\n" or "\r\n". */
static size_t
value_length(const char *buf, size_t len)
{
    if (len > 0u && buf[len - 1u] == '\n') {
        len--;
        if (len > 0u && buf[len - 1u] == '\r') {
            len--;
        }
    }
    return len;
}

/** @brief 1 when the @p len bytes at @p buf spell @p word. */
static int
value_is(const char *buf, size_t len, const char *word)
{
    return strlen(word) == len && memcmp(buf, word, len) == 0;
}

/*---------------------------------------------------------------------------*/
/* CHANNEL AND LISTEN                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Read handler for /sys/radio/154/channel: 11..26. */
static int
r154_channel_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_154_channel());
}

/**
 * @brief Write handler for /sys/radio/154/channel: a channel 11..26, which
 *        a running listen moves to.
 * @return 0, or TIKU_VFS_EINVAL for another value
 */
static int
r154_channel_write(const char *buf, size_t len)
{
    unsigned ch = 0u;
    size_t i;

    len = value_length(buf, len);
    if (len == 0u || len > 2u) {
        return TIKU_VFS_EINVAL;
    }
    for (i = 0u; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return TIKU_VFS_EINVAL;
        }
        ch = ch * 10u + (unsigned)(buf[i] - '0');
    }
    if (ch < TIKU_154_CHAN_MIN || ch > TIKU_154_CHAN_MAX) {
        return TIKU_VFS_EINVAL;
    }
    tiku_154_tune((uint8_t)ch);
    return 0;
}

/** @brief Read handler for /sys/radio/154/listen: on or off. */
static int
r154_listen_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_154_listening() ? "on" : "off");
}

/**
 * @brief Write handler for /sys/radio/154/listen: on (or 1) starts the
 *        background listen, off (or 0) ends it.
 * @return 0, TIKU_VFS_EBUSY when the radio cannot be taken, or
 *         TIKU_VFS_EINVAL for another value
 */
static int
r154_listen_write(const char *buf, size_t len)
{
    len = value_length(buf, len);
    if (value_is(buf, len, "on") || value_is(buf, len, "1")) {
        return (tiku_154_listen(1) == 0) ? 0 : TIKU_VFS_EBUSY;
    }
    if (value_is(buf, len, "off") || value_is(buf, len, "0")) {
        return tiku_154_listen(0);
    }
    return TIKU_VFS_EINVAL;
}

/*---------------------------------------------------------------------------*/
/* FRAMES                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write handler for /sys/radio/154/tx: the bytes written go out as
 *        one frame, unchanged.
 * @return 0, TIKU_VFS_EINVAL for 0 or more than TIKU_154_MAX_PSDU bytes, or
 *         TIKU_VFS_EBUSY when the radio cannot be taken or the frame did not
 *         go out
 */
static int
r154_tx_write(const char *buf, size_t len)
{
    if (len == 0u || len > TIKU_154_MAX_PSDU) {
        return TIKU_VFS_EINVAL;
    }
    return (tiku_154_send_raw((const uint8_t *)buf, (uint8_t)len) == 0)
               ? 0 : TIKU_VFS_EBUSY;
}

/**
 * @brief Read handler for /sys/radio/154/rx: takes the oldest held frame.
 * @return its length, 0 when none is held; a frame longer than @p max is
 *         cut to it and the rest lost
 */
static int
r154_rx_read(char *buf, size_t max)
{
    uint8_t cap = (max > TIKU_154_MAX_PSDU) ? (uint8_t)TIKU_154_MAX_PSDU
                                            : (uint8_t)max;
    return tiku_154_take((uint8_t *)buf, cap, &last_rssi);
}

/** @brief Read handler for /sys/radio/154/rssi: the level in dBm of the
 *         frame rx last gave, 0 before the first. */
static int
r154_rssi_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%d\n", (int)last_rssi);
}

/** @brief Read handler for /sys/radio/154/held: the frames rx can take. */
static int
r154_held_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_154_held());
}

/**
 * @brief Read handler for /sys/radio/154/ed: the energy on the channel now,
 *        in dBm.
 * @return the text's length, or TIKU_VFS_EBUSY when the radio cannot be
 *         taken or the measurement timed out
 */
static int
r154_ed_read(char *buf, size_t max)
{
    int8_t dbm = 0;

    if (tiku_154_energy(&dbm) < 0) {
        return TIKU_VFS_EBUSY;
    }
    return snprintf(buf, max, "%d\n", (int)dbm);
}

/**
 * @brief The listen's notify hook: a drain held frames, so /sys/radio/154/rx
 *        has something to take.
 */
static void
r154_rx_notify(void)
{
    static const tiku_vfs_node_t *rx_node;

    if (rx_node == (const tiku_vfs_node_t *)0) {
        rx_node = tiku_vfs_resolve("/sys/radio/154/rx");
    }
    if (rx_node != (const tiku_vfs_node_t *)0) {
        tiku_vfs_notify(rx_node);
    }
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/* A read of rx takes a frame and a read of ed runs a measurement, so
 * neither is a passive read. */
static const tiku_vfs_desc_t desc_channel = TIKU_VFS_DESC_R(
    TIKU_VFS_T_U32, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE,
    TIKU_154_CHAN_MIN, TIKU_154_CHAN_MAX);
static const tiku_vfs_desc_t desc_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_rx = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_NONE, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_FREE,
    TIKU_VFS_DF_READ_CONSUMES);
static const tiku_vfs_desc_t desc_rssi = TIKU_VFS_DESC(
    TIKU_VFS_T_I32, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_held = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_COUNT, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_ed = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_I32, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_PERIPH,
    TIKU_VFS_DF_READ_EFFECT);

/**
 * /sys/radio/154 children.  Writes drive the radio, so they need
 * TIKU_VFS_CAP_HW.  The entry count is TIKU_VFS_TREE_RADIO154_NCHILD because
 * sizeof() cannot cross translation units.
 */
const tiku_vfs_node_t tiku_vfs_tree_radio154_children[] = {
    { "channel", TIKU_VFS_FILE, r154_channel_read, r154_channel_write, NULL,
      0, &desc_channel, NULL, TIKU_VFS_CAP_HW },
    { "listen",  TIKU_VFS_FILE, r154_listen_read, r154_listen_write, NULL,
      0, &desc_text, NULL, TIKU_VFS_CAP_HW },
    { "tx",      TIKU_VFS_FILE, NULL, r154_tx_write, NULL, 0, NULL, NULL,
      TIKU_VFS_CAP_HW },
    { "rx",      TIKU_VFS_FILE, r154_rx_read, NULL, NULL, 0, &desc_rx },
    { "rssi",    TIKU_VFS_FILE, r154_rssi_read, NULL, NULL, 0, &desc_rssi },
    { "held",    TIKU_VFS_FILE, r154_held_read, NULL, NULL, 0, &desc_held },
    { "ed",      TIKU_VFS_FILE, r154_ed_read, NULL, NULL, 0, &desc_ed },
};

_Static_assert(sizeof(tiku_vfs_tree_radio154_children) /
               sizeof(tiku_vfs_tree_radio154_children[0])
               == TIKU_VFS_TREE_RADIO154_NCHILD,
               "TIKU_VFS_TREE_RADIO154_NCHILD out of sync");

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_vfs_tree_radio154_init(void)
{
    tiku_154_set_rx_notify(r154_rx_notify);
}

#endif /* TIKU_HAS_154_LISTEN */
