/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_arch.h - P4 LP core lifecycle and mailbox, from the HP core.
 *
 * The payload (arch/esp32p4/lpcore/) is copied into LP SRAM at the first
 * launch; a relaunch resets the core and runs the copy again.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_LPCORE_ARCH_H_
#define TIKU_ESP32P4_LPCORE_ARCH_H_

#include <stdint.h>

/** @brief The LP core's control and fault registers and the payload's
 *         fault record, read for diagnostics. */
typedef struct {
    uint32_t reset_cause;   /* LP_AON_CLKRST RESET_CAUSE                  */
    uint32_t dbg_pc;        /* LP_SYS LP_CPU_DBG_PC                       */
    uint32_t exc_pc;        /* LP_SYS LP_CPU_EXC_PC                       */
    uint32_t exc_int;       /* PMU HP_INT_RAW LP-core-exception bit, 0/1  */
    uint32_t pwr0;          /* PMU LP_CPU_PWR0                            */
    uint32_t pwr2;          /* PMU LP_CPU_PWR2 (its wake sources)         */
    uint32_t clk_on;        /* LPPERI clock bit, 0/1                      */
    uint32_t in_reset;      /* LPPERI reset bit, 0/1                      */
    uint32_t lp_io_in;      /* LP_GPIO_IN: pad levels of LP IO 0..15      */
    uint32_t boots;         /* payload entries since the image was loaded */
    uint32_t fault_count;
    uint32_t fault_cause;   /* mcause of the latest trap */
    uint32_t fault_epc;     /* mepc                      */
    uint32_t fault_tval;    /* mtval                     */
} tiku_p4_lpcore_info_t;

/**
 * @brief Take over a payload that runs on from before this boot.
 *
 * LP SRAM and the LP core outlive an HP reset and a deep sleep.  A payload
 * running the linked image that has published (or faulted) is adopted;
 * anything else running is held in reset for tiku_p4_lpcore_start().
 *
 * @note Call once, early in every boot.
 */
void tiku_p4_lpcore_boot(void);

/**
 * @brief Launch the payload, resume a parked one, or relaunch one that
 *        faulted or was held in reset.
 * @return 0 once the payload has published its magic (or was already
 *         running); -1 when the image is absent or too large, or the
 *         payload never published
 */
int tiku_p4_lpcore_start(void);

/**
 * @brief Ask a running payload to park and wait for it; hold the core in
 *        reset when it does not answer, or when it faulted.
 * @note IPC builds return the board's I2C pads to HP when gating the core clock.
 * @return 0 once parked or stopped, -1 when a running payload ignored the
 *         park and was held in reset
 */
int tiku_p4_lpcore_stop(void);

/** @brief 1 after a launch, until a stop holds the core in reset. */
int tiku_p4_lpcore_launched(void);

/** @brief The payload's magic word, or 0 when not launched. */
uint32_t tiku_p4_lpcore_magic(void);

/** @brief 1 when launched, published and not parked. */
int tiku_p4_lpcore_running(void);

/** @brief The payload's heartbeat; it advances while it runs. */
uint32_t tiku_p4_lpcore_heartbeat(void);

/** @brief Bytes of payload image the launch copies, 0 without one. */
uint32_t tiku_p4_lpcore_image_size(void);

/**
 * @brief Hand one message to the payload, replacing any unread one.
 * @return 0, or -1 when @p len exceeds the mailbox or nothing runs
 */
int tiku_p4_lpcore_send(const void *data, uint32_t len);

/** @brief Capture a reply the payload published since the last capture. */
void tiku_p4_lpcore_poll(void);

/** @brief A count that advances once per captured reply. */
uint32_t tiku_p4_lpcore_reply_seq(void);

/**
 * @brief Copy the most recent captured reply.
 * @return Bytes copied, at most @p cap
 */
uint32_t tiku_p4_lpcore_reply(void *out, uint32_t cap);

/** @brief Read the LP core's registers and the payload's fault record. */
void tiku_p4_lpcore_info(tiku_p4_lpcore_info_t *out);

/**
 * @brief Run one payload command (TIKU_LPCORE_CMD_LED, _I2C_SCAN) with up to
 *        four arguments and wait for its answer.
 * @return The answer (TIKU_LPCORE_RSP_DONE or _BAD_ARG), or -1 when nothing
 *         runs, or when the payload did not take the command in time and
 *         was held in reset
 */
int tiku_p4_lpcore_command(uint32_t cmd, uint32_t a0, uint32_t a1,
                           uint32_t a2, uint32_t a3);

/** @brief The counters the payload published on its latest pass. */
typedef struct {
    uint32_t cycles;        /* the LP core's mcycle                       */
    uint64_t lp_ticks;      /* LP timer count, 48 bits                    */
    uint32_t led_level;     /* the LED pad the payload drives, 0/1        */
    uint32_t wake_mode;     /* the armed HP wake, TIKU_LPCORE_WAKE_*, or 0 */
    uint32_t wake_count;    /* HP wakes the payload has sent              */
} tiku_p4_lpcore_counters_t;

/** @brief Copy the published counters; zeros before the first load. */
void tiku_p4_lpcore_counters(tiku_p4_lpcore_counters_t *out);

/**
 * @brief Copy the latest I2C scan: bit (a & 7) of @p found[a >> 3] set when
 *        address a acknowledged.
 * @return The number of addresses that acknowledged, or -1 when SCL never
 *         rose
 */
int tiku_p4_lpcore_i2c_result(uint8_t found[16]);

/**
 * @brief Take the SAR from the LP agent's ADC jobs before an HP conversion.
 *
 * Asks the payload to skip its conversions and waits for one in progress to
 * end, going ahead after a bounded wait; with no payload published it
 * returns at once.  Pair with tiku_p4_lpcore_sar_give().
 */
void tiku_p4_lpcore_sar_take(void);
/** @brief Let the LP agent's ADC jobs convert again. */
void tiku_p4_lpcore_sar_give(void);

/**
 * @brief Keep @p n words (at most TIKU_LPCORE_SAR_WORDS) of HP SAR state in
 *        LP SRAM, where an HP reset or a deep sleep leaves them.
 * @return 0, or -1 when they do not fit
 */
int tiku_p4_lpcore_sar_keep(const uint32_t *words, unsigned n);
/** @brief Copy the kept words into @p words.
 *  @return 1 when tiku_p4_lpcore_sar_keep() left @p n of them, else 0 */
int tiku_p4_lpcore_sar_kept(uint32_t *words, unsigned n);
/** @brief Drop the kept words. */
void tiku_p4_lpcore_sar_forget(void);

#endif
