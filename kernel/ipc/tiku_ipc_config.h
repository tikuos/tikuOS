/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_config.h - bounded IPC resource configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_CONFIG_H_
#define TIKU_IPC_CONFIG_H_

#ifndef TIKU_IPC_ENABLE
#define TIKU_IPC_ENABLE 0
#endif
#ifndef TIKU_PROCESS_OBSERVERS_ENABLE
#define TIKU_PROCESS_OBSERVERS_ENABLE TIKU_IPC_ENABLE
#endif
#ifndef TIKU_PROCESS_OBSERVERS_MAX
#define TIKU_PROCESS_OBSERVERS_MAX 2
#endif
#ifndef TIKU_IPC_ENDPOINTS_MAX
#define TIKU_IPC_ENDPOINTS_MAX 8
#endif
#ifndef TIKU_IPC_QUEUE_DEPTH
#define TIKU_IPC_QUEUE_DEPTH 4
#endif
#ifndef TIKU_IPC_VFS_NODES_MAX
#define TIKU_IPC_VFS_NODES_MAX (1 + 10 * TIKU_IPC_ENDPOINTS_MAX)
#endif
#ifndef TIKU_IPC_MESSAGE_MAX
#define TIKU_IPC_MESSAGE_MAX 64
#endif
#ifndef TIKU_IPC_SUBMISSIONS_MAX
#define TIKU_IPC_SUBMISSIONS_MAX 8
#endif
#ifndef TIKU_IPC_BUFFERS_MAX
#define TIKU_IPC_BUFFERS_MAX 4
#endif
#ifndef TIKU_IPC_BUFFER_SIZE
#define TIKU_IPC_BUFFER_SIZE 512
#endif
#ifndef TIKU_IPC_PROGRESS_BUDGET
#define TIKU_IPC_PROGRESS_BUDGET 4
#endif
#ifndef TIKU_IPC_GENERATION_MAX
#define TIKU_IPC_GENERATION_MAX 0x3fffffffUL
#endif

#if TIKU_IPC_ENABLE
#if TIKU_IPC_MESSAGE_MAX < 1 || TIKU_IPC_MESSAGE_MAX > 65535 || \
    TIKU_IPC_BUFFER_SIZE < 1 || TIKU_IPC_BUFFER_SIZE > 65535 || \
    TIKU_IPC_PROGRESS_BUDGET < 1 || TIKU_IPC_PROGRESS_BUDGET > 65535 || \
    TIKU_PROCESS_OBSERVERS_MAX < 1 || TIKU_PROCESS_OBSERVERS_MAX > 255
#error Invalid IPC message, buffer, progress or observer bound
#endif
#endif

#endif
