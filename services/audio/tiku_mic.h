/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic.h - The microphone as mono 16 kHz PCM16: a kernel process
 * drains the port's capture blocks into a PSRAM ring that readers empty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MIC_H_
#define TIKU_MIC_H_
#include <stddef.h>
#include <stdint.h>

#define TIKU_MIC_RATE 16000u
/** Samples a port delivers per poll. */
#define TIKU_MIC_BLOCK 256u
/** Ring capacity in samples: four seconds, 128 KB of PSRAM. */
#ifndef TIKU_MIC_RING_SAMPLES
#define TIKU_MIC_RING_SAMPLES (4u * TIKU_MIC_RATE)
#endif
/** Blocks dropped after each start while the codec's input settles. */
#ifndef TIKU_MIC_SETTLE_BLOCKS
#define TIKU_MIC_SETTLE_BLOCKS 32u
#endif

/** Microphone gain in dB at boot, and its largest value. */
#ifndef TIKU_MIC_GAIN_DEFAULT
#define TIKU_MIC_GAIN_DEFAULT 30
#endif
#define TIKU_MIC_GAIN_MAX 36

#define TIKU_MIC_OK          0
#define TIKU_MIC_ERR_STATE  (-1)  /**< not kernel context, or already running */
#define TIKU_MIC_ERR_MEMORY (-2)  /**< no PSRAM for the ring */
#define TIKU_MIC_ERR_DEVICE (-3)  /**< the port's capture did not open, or
                                       refused the gain */
#define TIKU_MIC_ERR_RANGE  (-4)  /**< gain outside 0..TIKU_MIC_GAIN_MAX */

typedef enum {
    TIKU_MIC_IDLE = 0,      /**< stopped; nothing is held */
    TIKU_MIC_RUNNING,       /**< capturing into the ring */
    TIKU_MIC_FAILED         /**< capture stopped on a port error; the
                                 ring keeps what it held until stop */
} tiku_mic_state_t;

/** @brief Counters since the last start, and the last block's level. */
typedef struct {
    uint8_t  state;         /**< tiku_mic_state_t */
    uint32_t captured;      /**< samples put in the ring */
    uint32_t buffered;      /**< samples waiting for a reader */
    uint32_t dropped;       /**< samples lost to a full ring */
    uint32_t faults;        /**< port errors; each stops capture */
    uint16_t peak;          /**< largest magnitude in the last block */
    uint8_t  gain;          /**< microphone gain in dB */
    uint16_t starts;        /**< starts since boot */
} tiku_mic_status_t;

/**
 * @brief Take a PSRAM ring, open the port's capture and start the capture
 *        process; kernel context.
 * @return TIKU_MIC_OK, or an error after which nothing is held
 */
int tiku_mic_start(void);

/** @brief Stop capture, close the port and free the ring; kernel context.
 *         Stopping an idle microphone does nothing. */
void tiku_mic_stop(void);

/**
 * @brief Move up to @p max of the oldest waiting samples into @p out;
 *        kernel context.
 * @return the samples moved; 0 when none wait
 */
size_t tiku_mic_read(int16_t *out, size_t max);

/**
 * @brief Set the microphone gain to @p db, 0..TIKU_MIC_GAIN_MAX; kernel
 *        context.  It applies at once while capture runs, and at each start.
 * @return TIKU_MIC_OK, TIKU_MIC_ERR_RANGE, or TIKU_MIC_ERR_DEVICE when the
 *         port refused it (the setting is kept)
 */
int tiku_mic_gain(int db);

/** @brief Copy the counters and state into @p out. */
void tiku_mic_status(tiku_mic_status_t *out);

/* The port's capture, called from the capture process in kernel context. */

/** @brief Open the microphones and start capture; 0 on success. */
int tiku_mic_arch_open(void);

/** @brief Take the next TIKU_MIC_BLOCK mono samples into @p block.
 *  @return 1 with a block, 0 when none is ready, negative on an error */
int tiku_mic_arch_poll(int16_t *block);

/** @brief Set the microphones' gain to @p db; 0 on success. */
int tiku_mic_arch_gain(int db);

/** @brief Stop capture and release what open took. */
void tiku_mic_arch_close(void);

#endif
