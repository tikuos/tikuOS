/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_record.h - Buffered P4 microphone recordings in native media storage.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_RECORD_H_
#define TIKU_MEDIA_RECORD_H_
#include <stdint.h>
/** @brief Capture and storage results; ID is nonzero after the file is created. */
typedef struct {
    uint64_t id;
    uint32_t samples, bytes, elapsed_ms, peak, clipped, overruns, faults;
} tiku_media_record_result_t;
/**
 * @brief Capture left-slot mono PCM into PSRAM, then publish a WAV on mounted media.
 * @note Runs synchronously in foreground context. Name must be unused; rate is
 * 8000..39000 Hz. PSRAM must hold seconds * rate * 2 + 44 bytes, rounded to four.
 * No samples are published if capture times out or reports DMA loss. A storage
 * failure can leave an incomplete file. The microphone must be stopped on entry.
 * @return Zero on completion, a native media error, -20 for capture failure,
 * or -21 for a capture with DMA overruns/faults.
 */
int tiku_media_record_wave(const char *name,uint32_t seconds,uint32_t rate,
                           tiku_media_record_result_t *result);
#endif
