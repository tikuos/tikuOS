/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku.h - Configuration for the native C6 process and timer profile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_RESIDENT_CONFIG_H
#define TIKU_C6_RESIDENT_CONFIG_H
#define TIKU_H_
#include <stdint.h>
#define PLATFORM_ESP32C6 1
#define TIKU_CLOCK_CONF_TIME_T unsigned long
#define TIKU_CLOCK_ARCH_SECOND 128UL
#define TIKU_THREADS_ENABLE 0
#define TIKU_MEM_ARCH_ALIGNMENT 8U
#define TIKU_MEM_NVM_ENABLE 0
#define TIKU_TIER_SRAM_DERIVED 1
#define TIKU_IPC_BUFFERS_MAX 2
#define TIKU_PROCESS_MAX 8

#define TIKU_IPC_ENABLE 1
#define TIKU_IPC_ENDPOINTS_MAX 3
#define TIKU_IPC_SUBMISSIONS_MAX 4
#define TIKU_IPC_QUEUE_DEPTH 2
#define TIKU_MEM_MAX_RESERVATIONS 8
#define TIKU_AUTOSTART_ENABLE 0
#define SCHED_PRINTF(...) ((void)0)
#define MEM_PRINTF(...) ((void)0)
#define TIKU_MEM_RECLAIM_ENABLE 0
#define TIMER_PRINTF(...) ((void)0)
#define PROCESS_PRINTF(...) ((void)0)
#define CLOCK_ARCH_PRINTF(...) ((void)0)
#define TIKU_PRINTF(...) ((void)0)
void tiku_watchdog_kick(void);
void tiku_uart_putc(char ch);
uint8_t tiku_uart_rx_ready(void);
int tiku_uart_getc(void);
#endif
