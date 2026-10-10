/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_uart_arch.h - C5 UART0, 8N1, crystal clock and interrupt-fed receive.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_UART_ARCH_H_
#define TIKU_ESP32C5_UART_ARCH_H_
#include <stdint.h>

#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE 1024u
#endif

/** @brief UART counters since the most recent successful initialization. */
typedef struct {
    uint32_t interrupts;
    uint32_t tx_dropped;
    uint16_t overruns;  /**< FIFO overflow events plus full-ring byte drops. */
    uint16_t rx_errors; /**< Latched framing/parity error events. */
} tiku_c5_uart_stats_t;

/**
 * @brief Initialize UART0 at 300..3000000 baud; return 0 or -1 on failure.
 * @note Loopback disconnects the UART TX pad. External mode requires a UART
 * console build, whose board reserves GPIO11/12. Reinitialization discards RX.
 */
int tiku_c5_uart_start(uint32_t baud, int loopback);
/** @brief Disable owned interrupts and restore changed pin routing; discard
 * queued data. */
void tiku_c5_uart_stop(void);
/** @brief Return nonzero after successful initialization. */
int tiku_c5_uart_ready(void);
/** @brief Initialize the board's UART console, or leave it unavailable on
 * error. */
void tiku_uart_init(void);
/** @brief Send a byte with a bounded FIFO wait; count bytes rejected or timed
 * out. */
void tiku_uart_putc(char byte);
/** @brief Return nonzero if a byte is buffered; polling also drains the
 * hardware FIFO. */
uint8_t tiku_uart_rx_ready(void);
/** @brief Read a buffered byte, or -1 if uninitialized or empty. */
int tiku_uart_getc(void);
/** @brief Return the saturating count of FIFO overflow events and ring byte
 * drops. */
uint16_t tiku_uart_overrun_count(void);
/** @brief Clear the overrun counter with interrupts masked. */
void tiku_uart_overrun_reset(void);
/** @brief Snapshot counters without consuming pending hardware receive events.
 */
void tiku_c5_uart_stats(tiku_c5_uart_stats_t *out);
#endif
