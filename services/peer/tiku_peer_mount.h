/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_mount.h - a peer's file tree under /peer/<name>.
 *
 * A peer with a service named "vfs" serves its own tree to the other side
 * and is given the other side's under /peer/<name>.  A request and its
 * answer are one kernel IPC message each on <peer>/vfs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PEER_MOUNT_H_
#define TIKU_PEER_MOUNT_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/timers/tiku_clock.h>

#if (TIKU_PEER_ENABLE + 0)

/*---------------------------------------------------------------------------*/
/* THE MESSAGES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Bytes of a request or an answer: one copied IPC message. */
#define TIKU_PEER_MOUNT_MSG    64u
/** @brief Bytes of the header both carry. */
#define TIKU_PEER_MOUNT_HEAD   8u
/** @brief Bytes after the header: a path, file bytes or names. */
#define TIKU_PEER_MOUNT_DATA   (TIKU_PEER_MOUNT_MSG - TIKU_PEER_MOUNT_HEAD)

/** @brief Byte 0 of a request and of an answer. */
#define TIKU_PEER_MOUNT_REQUEST  0x51u
#define TIKU_PEER_MOUNT_ANSWER   0x41u

/**
 * @defgroup TIKU_PEER_MOUNT_OPS Request operations (byte 2)
 * @note A request is: kind, id, operation, path length P, a 32-bit
 *       little-endian argument, P path bytes, then data.  The path is
 *       absolute on the serving side and carries no NUL.
 * @{
 */
#define TIKU_PEER_MOUNT_LIST    1u  /**< argument: entries to pass over */
#define TIKU_PEER_MOUNT_STAT    2u  /**< is the path a directory or a file */
#define TIKU_PEER_MOUNT_READ    3u  /**< argument: byte offset */
#define TIKU_PEER_MOUNT_WRITE   4u  /**< argument: whole length; data: its start */
#define TIKU_PEER_MOUNT_MORE    5u  /**< no path; argument: offset; data: next part */
#define TIKU_PEER_MOUNT_UNLINK  6u  /**< delete the file */
/** @} */

/**
 * @defgroup TIKU_PEER_MOUNT_FLAGS Answer flags (byte 3)
 * @note An answer is: kind, the request's id, a TIKU_VFS_* status as a
 *       signed byte, flags, a 32-bit little-endian result, then data.  The
 *       result is a READ's whole length or the entries a LIST passed.
 * @{
 */
#define TIKU_PEER_MOUNT_F_MORE  0x01u  /**< LIST: entries remain; WRITE: send on */
#define TIKU_PEER_MOUNT_F_DIR   0x02u  /**< STAT: the path is a directory */
/** @} */

/*---------------------------------------------------------------------------*/
/* LIMITS                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief How many of the board's peers, from the table's start, can be
 *        mounted: 1 or 2.
 * @note Each costs the serving side's buffers, about 1 KB of RAM.
 */
#ifndef TIKU_PEER_MOUNT_PEERS
#define TIKU_PEER_MOUNT_PEERS  1u
#endif
#if TIKU_PEER_MOUNT_PEERS < 1 || TIKU_PEER_MOUNT_PEERS > 2
#error "TIKU_PEER_MOUNT_PEERS is 1 or 2"
#endif

/** @brief Bytes the serving side renders and holds of the file being read. */
#ifndef TIKU_PEER_MOUNT_READ_BUF
#define TIKU_PEER_MOUNT_READ_BUF  512u
#endif

/** @brief Longest value the other side can write to one path. */
#ifndef TIKU_PEER_MOUNT_WRITE_MAX
#define TIKU_PEER_MOUNT_WRITE_MAX  256u
#endif

/** @brief Capabilities the other side's writes and deletes run under. */
#ifndef TIKU_PEER_MOUNT_CAP
#define TIKU_PEER_MOUNT_CAP  (TIKU_VFS_CAP_HW | TIKU_VFS_CAP_FS)
#endif

/** @brief Seconds one request waits for its answer. */
#ifndef TIKU_PEER_MOUNT_WAIT_S
#define TIKU_PEER_MOUNT_WAIT_S  3u
#endif

/*---------------------------------------------------------------------------*/
/* THE SERVING SIDE                                                          */
/*---------------------------------------------------------------------------*/

/** @brief What the serving side keeps between one peer's requests. */
typedef struct {
    tiku_vfs_cap_t cap;         /**< Its writes and deletes run under this */
    uint8_t  held_valid;        /**< hold[] has bytes of held_path */
    uint8_t  write_open;        /**< a WRITE waits for its MORE parts */
    uint16_t held;              /**< Bytes in hold[] */
    uint16_t write_total;
    uint16_t write_have;
    tiku_clock_time_t held_at;  /**< Clock time hold[] was last used */
    uint32_t held_base;         /**< Offset of hold[0] in the file */
    uint32_t held_total;        /**< The file's whole length */
    char     held_path[TIKU_PEER_MOUNT_DATA + 1u];
    char     write_path[TIKU_PEER_MOUNT_DATA + 1u];
    char     hold[TIKU_PEER_MOUNT_READ_BUF];
    char     write[TIKU_PEER_MOUNT_WRITE_MAX];
} tiku_peer_mount_server_t;

/**
 * @brief Answer one request from this side's tree, /peer left out of it.
 *
 * @param s        The peer's state, zeroed before its first request apart
 *                 from cap
 * @param request  The message received
 * @param length   Its length
 * @param answer   Receives the answer, TIKU_PEER_MOUNT_MSG bytes of room
 * @return The answer's length, or 0 for a message that is not a request
 */
uint8_t tiku_peer_mount_serve(tiku_peer_mount_server_t *s,
                              const uint8_t *request, uint8_t length,
                              uint8_t *answer);

/*---------------------------------------------------------------------------*/
/* THE MOUNTING SIDE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Send a request to peer @p peer and wait for its answer.
 *
 * Runs the other processes while it waits.  One request is out at a time.
 *
 * @param request        The request; byte 1 is overwritten with its id
 * @param length         Its length, TIKU_PEER_MOUNT_HEAD to TIKU_PEER_MOUNT_MSG
 * @param answer         Receives the answer, TIKU_PEER_MOUNT_MSG bytes of room
 * @param answer_length  Receives its length, at least TIKU_PEER_MOUNT_HEAD
 * @return TIKU_VFS_OK; TIKU_VFS_ENOTSUP while the peer is not ready or has
 *         no "vfs" service; TIKU_VFS_EBUSY while another request is out;
 *         TIKU_VFS_EIO when no answer came or the peer restarted;
 *         TIKU_VFS_EINVAL for a length outside its range
 */
int tiku_peer_mount_call(uint8_t peer, uint8_t *request, uint8_t length,
                         uint8_t *answer, uint8_t *answer_length);

/**
 * @brief The directory "peer", one child for each peer that has the service.
 * @return The node, for boot assembly to mount under "/"
 */
const tiku_vfs_node_t *tiku_peer_mount_get(void);

/** @brief Start the process that serves and carries requests; once, at boot,
 *         after kernel IPC and the peers are bound. */
void tiku_peer_mount_start(void);

#endif /* TIKU_PEER_ENABLE */
#endif /* TIKU_PEER_MOUNT_H_ */
