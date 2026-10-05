/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fs.h - /data commands: rm, touch, mkdir, rmdir, recv, send
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FS_H_
#define TIKU_SHELL_CMD_FS_H_

#include <stdint.h>

/**
 * @brief "rm" command handler — delete a file from a dynamic directory.
 *
 * Only files in a dynamic directory (the /data file store) can be removed;
 * a static VFS node returns an error.
 */
void tiku_shell_cmd_rm(uint8_t argc, const char *argv[]);

/**
 * @brief "touch" command handler — create an empty file if it does not exist.
 *
 * A no-op on an existing file (the store has no modification time to bump),
 * so it never truncates one.
 */
void tiku_shell_cmd_touch(uint8_t argc, const char *argv[]);

/**
 * @brief "mkdir" command handler — create an empty folder (path-as-name).
 *
 * The store is flat, so a folder is a name ending in '/'.  This writes an
 * empty "<path>/" marker so an empty folder persists and shows in `ls`;
 * folders also appear implicitly once a file is written under the path.
 */
void tiku_shell_cmd_mkdir(uint8_t argc, const char *argv[]);

/**
 * @brief "rmdir" command handler — remove an empty folder's marker.
 *
 * Clears the "<path>/" marker that mkdir wrote.  A folder still holding files
 * stays until those files are removed; this only deletes the empty marker.
 */
void tiku_shell_cmd_rmdir(uint8_t argc, const char *argv[]);

/**
 * @brief "recv" command handler — receive a file from the host.
 *
 * Prints "recv: ready N" ("chunk C" added for a /data file), then reads
 * exactly N raw bytes and writes them to the path, binary-safe.  A /data file
 * streams up to the store's limit with a '.' per chunk; others take one buffer.
 */
void tiku_shell_cmd_recv(uint8_t argc, const char *argv[]);

/**
 * @brief "send" command handler — send a file to the host.
 *
 * `send <path>` prints "send: N", then streams N raw bytes of the file out
 * (binary-safe).  A /data file goes out whole; any other node must render in
 * one transfer buffer (TIKU_TFS_SLOT_DATA bytes) and is refused if it does not.
 */
void tiku_shell_cmd_send(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FS_H_ */
