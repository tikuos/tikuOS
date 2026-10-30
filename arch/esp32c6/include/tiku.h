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
#include <stddef.h>
#define PLATFORM_ESP32C6 1
#define TIKU_CLOCK_CONF_TIME_T unsigned long
#define TIKU_CLOCK_ARCH_SECOND 128UL
#ifndef TIKU_THREADS_ENABLE
#define TIKU_THREADS_ENABLE 0
#endif
#define TIKU_THREAD_STACK_ATTR __attribute__((section(".c6.worker_stacks"),aligned(16)))
#define TIKU_MEM_ARCH_ALIGNMENT 8U
#ifndef TIKU_C6_STORAGE
#define TIKU_C6_STORAGE 0
#endif
#define TIKU_MEM_NVM_ENABLE TIKU_C6_STORAGE
#define TIKU_TIER_SRAM_DERIVED 1
#ifndef TIKU_IPC_BUFFERS_MAX
#define TIKU_IPC_BUFFERS_MAX 2
#endif
#define TIKU_PROCESS_MAX 8
#define TIKU_VERSION "0.06"
#define TIKU_TAGLINE "Simple. Persistent. Intelligent."
#define TIKU_DEVICE_NAME "ESP32-C6"
#define TIKU_DEVICE_RAM_SIZE 524288UL
#define TIKU_DEVICE_RAM_USABLE 409600UL
#define TIKU_DEVICE_FRAM_SIZE 4194304UL
#define TIKU_DEVICE_FRAM_START 0x42000000UL
#define TIKU_DEVICE_FRAM_END 0x423fffffUL
#define TIKU_DEVICE_NVM_LABEL "Flash"
#define TIKU_BOARD_ADC_AVAILABLE 1
#define TIKU_BOARD_UART_BAUD 115200UL
#define MAIN_CPU_FREQ 80
#define TIKU_BOARD_LED_COUNT 0
#define TIKU_BOARD_BTN_COUNT 0
#define TIKU_CPU_FREQ 80
#define TIKU_HTIMER_ARCH_SECOND 1000000UL
#if TIKU_SHELL_ENABLE
#ifndef TIKU_SHELL_CMD_HTIMER
#define TIKU_SHELL_CMD_HTIMER 0
#endif
#ifndef TIKU_SHELL_CMD_TRNG
#define TIKU_SHELL_CMD_TRNG 0
#endif
#ifndef TIKU_SHELL_CMD_IPC
#define TIKU_SHELL_CMD_IPC 0
#endif
#ifndef TIKU_SHELL_CMD_GPIO
#define TIKU_SHELL_CMD_GPIO 0
#endif
#ifndef TIKU_SHELL_CMD_ADC
#define TIKU_SHELL_CMD_ADC 0
#endif
#ifndef TIKU_SHELL_CMD_SLEEP
#define TIKU_SHELL_CMD_SLEEP 0
#endif
#ifndef TIKU_SHELL_CMD_WAKE
#define TIKU_SHELL_CMD_WAKE 0
#endif
#ifndef TIKU_SHELL_CMD_FREQ
#define TIKU_SHELL_CMD_FREQ 0
#endif
#ifndef TIKU_SHELL_CMD_POWER
#define TIKU_SHELL_CMD_POWER 0
#endif
#ifndef TIKU_SHELL_CMD_IRQ
#define TIKU_SHELL_CMD_IRQ 0
#endif
#ifndef TIKU_BASIC_PROGRAM_LINES
#define TIKU_BASIC_PROGRAM_LINES 100
#endif
#ifndef TIKU_BASIC_ADC_ENABLE
#define TIKU_BASIC_ADC_ENABLE 0
#endif
#ifndef TIKU_BASIC_I2C_ENABLE
#define TIKU_BASIC_I2C_ENABLE 0
#endif
#endif

#define TIKU_IPC_ENABLE 1
#if TIKU_C6_RADIO_SERVICE_ENABLE
#define TIKU_IPC_ENDPOINTS_MAX 5
#define TIKU_IPC_SUBMISSIONS_MAX 8
#else
#define TIKU_IPC_ENDPOINTS_MAX 3
#define TIKU_IPC_SUBMISSIONS_MAX 4
#endif
#define TIKU_IPC_QUEUE_DEPTH 2
#define TIKU_MEM_MAX_RESERVATIONS 8
#define TIKU_AUTOSTART_ENABLE 0
#define SCHED_PRINTF(...) ((void)0)
#define MEM_PRINTF(...) ((void)0)
#define TIKU_MEM_RECLAIM_ENABLE 0
#define TIMER_PRINTF(...) ((void)0)
#define PROCESS_PRINTF(...) ((void)0)
#define WDT_PRINTF(...) ((void)0)
#define HTIMER_PRINTF(...) ((void)0)
#define COMMON_PRINTF(...) ((void)0)
#define CLOCK_ARCH_PRINTF(...) ((void)0)
#define TIKU_PRINTF(...) ((void)0)
#include <hal/tiku_cpu.h>
#include <kernel/timers/tiku_clock.h>
void tiku_watchdog_kick(void);
#if TIKU_SHELL_ENABLE
#include <arch/esp32c6/tiku_uart_arch.h>
#else
unsigned int tiku_uart_overrun_count(void);
#endif
void tiku_uart_putc(char ch);
uint8_t tiku_uart_rx_ready(void);
int tiku_uart_getc(void);
#endif
