/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mailbox_vfs.c - the board's mailbox: part, bus, /mailbox, notices.
 *
 * The side that switches the bus (BOARD_CTL) borrows it from the other
 * processor; the other side holds it and raises its busy line around each
 * operation, backing off while a borrow is asked for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mailbox_vfs.h"

#if (TIKU_MAILBOX_ENABLE + 0)

#include <interfaces/fram/tiku_fram.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include "tiku.h"
#if (TIKU_BOARD_HAS_BOARD_CTL + 0)
#include <interfaces/board/tiku_board_ctl.h>
#elif defined(TIKU_BOARD_SHARE_REQ_PORT)
#include <interfaces/gpio/tiku_gpio.h>
#include <kernel/cpu/tiku_common.h>        /* tiku_common_delay_us */
#endif
#if (TIKU_PEER_ENABLE + 0)
#include <services/peer/tiku_peer_ctl.h>
#endif
#include <limits.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* THE PART                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief The board's SPI FRAM. */
static const tiku_fram_t mb_part = {
    TIKU_BOARD_FRAM_SPI_SIZE, TIKU_FRAM_BUS_SPI, TIKU_BOARD_FRAM_SPI_ADDR_BYTES,
    TIKU_BOARD_FRAM_SPI_CS_PORT, TIKU_BOARD_FRAM_SPI_CS_PIN, 0u
};

/** @brief Bytes of one FRAM transfer: its length is 16 bits. */
#define MB_PIECE  0x4000UL

/** @brief Read the part in pieces; 0 or -1. */
static int
mb_read(void *ctx, uint32_t a, uint8_t *b, uint32_t n)
{
    (void)ctx;
    while (n != 0UL) {
        uint32_t k = (n < MB_PIECE) ? n : MB_PIECE;

        if (tiku_fram_read(&mb_part, a, b, (uint16_t)k) != TIKU_FRAM_OK) {
            return -1;
        }
        a += k;
        b += k;
        n -= k;
    }
    return 0;
}

/** @brief Write the part in pieces; 0 or -1. */
static int
mb_write(void *ctx, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)ctx;
    while (n != 0UL) {
        uint32_t k = (n < MB_PIECE) ? n : MB_PIECE;

        if (tiku_fram_write(&mb_part, a, b, (uint16_t)k) != TIKU_FRAM_OK) {
            return -1;
        }
        a += k;
        b += k;
        n -= k;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* THE BUS                                                                   */
/*---------------------------------------------------------------------------*/

#if (TIKU_BOARD_HAS_BOARD_CTL + 0)

/** @brief Borrow the bus from the other processor. */
static int
mb_take(void *ctx)
{
    (void)ctx;
    return (tiku_board_ctl_borrow() == TIKU_BOARD_CTL_OK) ? 0 : -1;
}

/** @brief Give it back. */
static void
mb_give(void *ctx)
{
    (void)ctx;
    tiku_board_ctl_borrow_end();
}

#elif defined(TIKU_BOARD_SHARE_REQ_PORT)

/** @brief Microseconds a raised busy line settles before the request is
 *         read: longer than the trip through both pins. */
#define MB_SETTLE_US  2u
/** @brief Microseconds between reads of the request while backed off. */
#define MB_POLL_US    10u
/** @brief Microseconds this side waits for a borrow to end. */
#define MB_WAIT_US    1000000UL

/** @brief Raise busy; while the other side asks, drop it and wait. */
static int
mb_take(void *ctx)
{
    uint32_t waited = 0UL;

    (void)ctx;
    for (;;) {
        (void)tiku_gpio_set(TIKU_BOARD_SHARE_BUSY_PORT,
                            TIKU_BOARD_SHARE_BUSY_PIN);
        tiku_common_delay_us(MB_SETTLE_US);
        if (tiku_gpio_read(TIKU_BOARD_SHARE_REQ_PORT,
                           TIKU_BOARD_SHARE_REQ_PIN) == 0) {
            return 0;
        }
        (void)tiku_gpio_clear(TIKU_BOARD_SHARE_BUSY_PORT,
                              TIKU_BOARD_SHARE_BUSY_PIN);
        while (tiku_gpio_read(TIKU_BOARD_SHARE_REQ_PORT,
                              TIKU_BOARD_SHARE_REQ_PIN) != 0) {
            if (waited >= MB_WAIT_US) {
                return -1;
            }
            tiku_common_delay_us(MB_POLL_US);
            waited += MB_POLL_US;
        }
    }
}

/** @brief Drop busy. */
static void
mb_give(void *ctx)
{
    (void)ctx;
    (void)tiku_gpio_clear(TIKU_BOARD_SHARE_BUSY_PORT,
                          TIKU_BOARD_SHARE_BUSY_PIN);
}

#else
#define mb_take 0
#define mb_give 0
#endif

/** @brief This processor's side: 1 where it switches the bus, else 2. */
#if (TIKU_BOARD_HAS_BOARD_CTL + 0)
#define MB_SIDE  1u
#else
#define MB_SIDE  2u
#endif

const tiku_mailbox_medium_t tiku_mailbox_board = {
    mb_read, mb_write, mb_take, mb_give, 0, TIKU_BOARD_FRAM_SPI_SIZE, MB_SIDE
};

/*---------------------------------------------------------------------------*/
/* /mailbox                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Tell the peer a file changed. */
static void
mb_changed(void)
{
#if (TIKU_PEER_ENABLE + 0)
    tiku_peer_ctl_notice(TIKU_PEER_CTL_TOPIC_MAILBOX);
#endif
}

/** @brief Read a file from its start; its whole length. */
static int
mbv_read(const char *name, char *buf, size_t max)
{
    uint32_t total = 0UL;
    int n = tiku_mailbox_read_at(&tiku_mailbox_board, name, 0UL,
                                 (uint8_t *)buf, (uint32_t)max, &total);

    if (n < 0) {
        return n;
    }
    return (total > (uint32_t)INT_MAX) ? INT_MAX : (int)total;
}

/** @brief Read a file from @p offset. */
static int
mbv_read_at(const char *name, uint32_t offset, char *buf, size_t max,
            uint32_t *total)
{
    return tiku_mailbox_read_at(&tiku_mailbox_board, name, offset,
                                (uint8_t *)buf, (uint32_t)max, total);
}

/** @brief Create or replace a file, then tell the peer. */
static int
mbv_write(const char *name, const char *buf, size_t len)
{
    int rc = tiku_mailbox_write(&tiku_mailbox_board, name,
                                (const uint8_t *)buf, (uint32_t)len);

    if (rc == 0) {
        mb_changed();
    }
    return rc;
}

/** @brief Delete a file, then tell the peer. */
static int
mbv_unlink(const char *name)
{
    int rc = tiku_mailbox_delete(&tiku_mailbox_board, name);

    if (rc == 0) {
        mb_changed();
    }
    return rc;
}

/** @brief The VFS list callback, its context, and the folder listed. */
typedef struct {
    tiku_vfs_dyn_list_cb cb;
    void *ctx;
    uint8_t kept;               /* 1: list keep/, 0: the top */
} mbv_list_t;

/** @brief Pass on one file of the folder listed, by its name there. */
static void
mbv_one(const char *name, uint32_t len, void *ctx)
{
    mbv_list_t *l = (mbv_list_t *)ctx;
    size_t k = strlen(TIKU_MAILBOX_KEEP);
    uint8_t kept = (strncmp(name, TIKU_MAILBOX_KEEP, k) == 0) ? 1u : 0u;

    (void)len;
    if (kept == l->kept) {
        l->cb(kept ? name + k : name, l->ctx);
    }
}

/** @brief List the top, with keep/ last, or keep/. */
static int
mbv_list(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx)
{
    mbv_list_t l;

    int rc;

    l.cb = cb;
    l.ctx = ctx;
    if (prefix[0] == '\0') {
        l.kept = 0u;
    } else if (strcmp(prefix, TIKU_MAILBOX_KEEP) == 0) {
        l.kept = 1u;
    } else {
        return TIKU_VFS_ENOENT;
    }
    rc = tiku_mailbox_list(&tiku_mailbox_board, mbv_one, &l);
    if (rc == 0 && l.kept == 0u) {
        cb(TIKU_MAILBOX_KEEP, ctx);
    }
    return rc;
}

/** @brief 1 for keep, 0 for a file that exists; a check marks nothing. */
static int
mbv_is_dir(const char *name)
{
    int rc;

    if (strcmp(name, "keep") == 0) {
        return 1;
    }
    rc = tiku_mailbox_stat(&tiku_mailbox_board, name, (uint32_t *)0);
    return (rc < 0) ? rc : 0;
}

static const tiku_vfs_dynops_t mb_ops = {
    .read = mbv_read, .write = mbv_write, .unlink = mbv_unlink,
    .read_at = mbv_read_at, .list_checked = mbv_list, .is_dir = mbv_is_dir
};

static const tiku_vfs_node_t mb_root = {
    .name = "mailbox", .type = TIKU_VFS_DIR, .dyn = &mb_ops
};

const tiku_vfs_node_t *
tiku_mailbox_vfs_get(void)
{
    return &mb_root;
}

/*---------------------------------------------------------------------------*/
/* START                                                                     */
/*---------------------------------------------------------------------------*/

#if (TIKU_PEER_ENABLE + 0)
/** @brief A notice from the peer: a watch on /mailbox hears of the change. */
static void
mb_on_notice(uint8_t index, uint8_t topic)
{
    static const tiku_vfs_node_t *node;

    (void)index;
    if (topic != TIKU_PEER_CTL_TOPIC_MAILBOX) {
        return;
    }
    if (node == (const tiku_vfs_node_t *)0) {
        node = tiku_vfs_resolve("/mailbox");
    }
    if (node != (const tiku_vfs_node_t *)0) {
        tiku_vfs_notify(node);
    }
}
#endif

void
tiku_mailbox_start(void)
{
#if (TIKU_BOARD_HAS_BOARD_CTL + 0)
    (void)tiku_board_ctl_rail(1u);
    (void)tiku_board_ctl_bus_default(TIKU_BOARD_BUS_COPROC);
#elif defined(TIKU_BOARD_SHARE_REQ_PORT)
    (void)tiku_gpio_claim(TIKU_BOARD_SHARE_REQ_PORT, TIKU_BOARD_SHARE_REQ_PIN,
                          "mailbox");
    (void)tiku_gpio_claim(TIKU_BOARD_SHARE_BUSY_PORT,
                          TIKU_BOARD_SHARE_BUSY_PIN, "mailbox");
    (void)tiku_gpio_dir_in(TIKU_BOARD_SHARE_REQ_PORT,
                           TIKU_BOARD_SHARE_REQ_PIN);
    (void)tiku_gpio_clear(TIKU_BOARD_SHARE_BUSY_PORT,
                          TIKU_BOARD_SHARE_BUSY_PIN);
#endif
#if (TIKU_PEER_ENABLE + 0)
    tiku_peer_ctl_on_notice(mb_on_notice);
#endif
}

#endif /* TIKU_MAILBOX_ENABLE */
