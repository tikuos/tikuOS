/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdm_arch.h - P4 PDM microphone capture: I2S0's PDM-to-PCM receiver
 * clocked out of GPIO12 and read on GPIO9, delivered as 16-bit mono PCM
 * through an AHB DMA ring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_PDM_ARCH_H_
#define TIKU_ESP32P4_PDM_ARCH_H_
#include <stdint.h>

#define TIKU_P4_PDM_OK            0
#define TIKU_P4_PDM_ERR_PARAM    -1   /**< NULL, zero, or a rate off the range */
#define TIKU_P4_PDM_ERR_STATE    -2   /**< started twice, or read while stopped */
#define TIKU_P4_PDM_ERR_BUSY     -3   /**< a pad is held by another driver */
#define TIKU_P4_PDM_ERR_TIMEOUT  -4   /**< no buffer within the time given */
#define TIKU_P4_PDM_ERR_NOMEM    -5   /**< no SRAM for the buffer ring */

/** @brief Number of 512-sample DMA buffers, each with a 64-byte descriptor. */
#ifndef TIKU_P4_PDM_BUFFERS
#define TIKU_P4_PDM_BUFFERS 8u
#endif
#if TIKU_P4_PDM_BUFFERS < 8 || TIKU_P4_PDM_BUFFERS > 64
#error "TIKU_P4_PDM_BUFFERS must be 8..64"
#endif

#define TIKU_P4_PDM_RATE_MIN   8000u  /**< the PDM clock stays at 1 MHz or more */
#define TIKU_P4_PDM_RATE_MAX  39000u  /**< the module clock stays under half the crystal */
#define TIKU_P4_PDM_SLOT_LEFT    2u   /**< the device whose select pin is low */
#define TIKU_P4_PDM_SLOT_RIGHT   1u   /**< the device whose select pin is high */
#define TIKU_P4_PDM_SLOT_BOTH    3u   /**< both, interleaved left then right */

/** @brief Counters since the start: samples handed out, buffers the engine
 *         filled, buffers lost because none was free, DMA faults. */
typedef struct {
    uint32_t samples;
    uint32_t buffers;
    uint32_t overruns;
    uint32_t faults;
} tiku_p4_pdm_stats_t;

/** @brief The clock the microphone receives for @p rate_hz and the
 *         decimation behind it: 64 at 15625 Hz and above, 128 below. */
uint32_t tiku_p4_pdm_clock_hz(uint32_t rate_hz, uint32_t *decimation);

/**
 * @brief Start capturing at @p rate_hz samples a second from @p slot.
 *
 * @p rate_hz within TIKU_P4_PDM_RATE_MIN..MAX; @p slot one of the
 * TIKU_P4_PDM_SLOT_* values (BOTH doubles the sample stream).
 * @return TIKU_P4_PDM_OK, or a negative TIKU_P4_PDM_ERR_*
 */
int tiku_p4_pdm_start(uint32_t rate_hz, uint32_t slot);
/** @brief Stop, release the pads and the clocks; a repeat does nothing. */
void tiku_p4_pdm_stop(void);
/**
 * @brief Up to @p n samples into @p buf, waiting up to @p timeout_ms for
 *        the first buffer.
 * @return samples copied (0 when nothing came within the time), or a
 *         negative TIKU_P4_PDM_ERR_*
 */
int32_t tiku_p4_pdm_read(int16_t *buf, uint32_t n, uint32_t timeout_ms);
/** @brief Non-zero while capturing. */
int tiku_p4_pdm_running(void);
/** @brief The rate in effect, 0 while stopped. */
uint32_t tiku_p4_pdm_rate(void);
/** @brief The counters; @p s may be NULL for a reset of them. */
void tiku_p4_pdm_stats(tiku_p4_pdm_stats_t *s);
/** @brief Peak and root-mean-square magnitude of the last buffer consumed. */
void tiku_p4_pdm_level(uint32_t *peak, uint32_t *rms);
#endif /* TIKU_ESP32P4_PDM_ARCH_H_ */
