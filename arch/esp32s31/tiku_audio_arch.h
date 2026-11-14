/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_audio_arch.h - Korvo stereo PCM16 rings at 16 kHz.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_AUDIO_ARCH_H_
#define TIKU_S31_AUDIO_ARCH_H_
#include <stdint.h>
#define TIKU_S31_AUDIO_FRAMES 256u
#define TIKU_S31_AUDIO_SAMPLES (2u * TIKU_S31_AUDIO_FRAMES)
#define TIKU_S31_AUDIO_INVALID (-1)
#define TIKU_S31_AUDIO_IO (-2)
/** @brief Per-stream delivery counts, fault counts and 16 MHz start timestamp. */
typedef struct {
    uint64_t started_ticks;
    uint32_t blocks, rx_faults, tx_faults, timeouts;
    uint8_t running, failed;
} tiku_s31_audio_status_t;
/** @brief Copy current stream status in kernel foreground, including latched faults. */
int tiku_s31_audio_status(tiku_s31_audio_status_t *status);
/** @brief Claim I2S0, AHB DMA pair one and GPIO3..7; run BCLK/WS with amplifier off.
 * @note Kernel foreground, serialized calls. Configure codec after open.
 * Stereo PCM16: left then right, 16 kHz, 32-bit frame, 512 kHz BCLK.
 */
int tiku_s31_audio_open(void);
/** @brief Start duplex rings; copy seed's 512 samples into each TX block, or silence.
 * @note Eight private SRAM blocks per direction; caller retains no DMA ownership.
 */
int tiku_s31_audio_start(const int16_t *seed);
/** @brief Copy 512 captured samples and queue 512 playback samples, or silence.
 * @return 1 with a block and zero-based sequence, 0 if pending, negative on error.
 * @note capture and sequence are required. Drain ready blocks before yielding;
 * keep pauses below 100 ms and sustain one block per 16 ms.
 * Queue exhaustion stops DMA and latches failure; close before reopening.
 * TX playback is queued eight blocks ahead of the returned capture block.
 */
int tiku_s31_audio_poll(int16_t *capture, const int16_t *playback, uint32_t *sequence);
/** @brief Stop DMA and clocks, disable amplifier and restore pads; codec closes first. */
void tiku_s31_audio_close(void);
/** @brief Set amplifier enable while open; caller controls codec mute and volume. */
int tiku_s31_audio_amplifier(unsigned enable);
#ifdef TIKU_S31_AUDIO_TEST
/** @brief Route RX to the driven TX pad, or restore the codec data input. */
void tiku_s31_audio_test_loopback(unsigned enable);
#endif
#endif
