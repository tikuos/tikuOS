/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mic.c - "mic" command: start and stop the FireBeetle 2's
 * PDM microphone, show its level and counters, print samples.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include <stdint.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_mic.h"
#include "tiku_shell_cmd_util.h"
#include <arch/esp32p4/tiku_pdm_arch.h>

#if TIKU_MEDIA_FS_ENABLE && TIKU_P4_SDCARD_ENABLE
#include <services/storage/tiku_media_record.h>
#endif

#define DEFAULT_RATE  16000u
#define LEVEL_SAMPLES 512u            /* one of the driver's buffers */
#define CHUNK         64u             /* samples a read, on the stack */
#define DUMP_MAX      4096u
#define READ_WAIT_MS  500u

static void mic_start(uint8_t argc, const char *argv[])
{
    uint32_t rate = DEFAULT_RATE, slot = TIKU_P4_PDM_SLOT_LEFT, decimation;
    unsigned i;
    int rc;

    for (i = 2u; i < argc; i++) {
        if (tiku_cmd_streq(argv[i], "left")) {
            slot = TIKU_P4_PDM_SLOT_LEFT;
        } else if (tiku_cmd_streq(argv[i], "right")) {
            slot = TIKU_P4_PDM_SLOT_RIGHT;
        } else if (tiku_cmd_streq(argv[i], "both")) {
            slot = TIKU_P4_PDM_SLOT_BOTH;
        } else {
            rate = tiku_cmd_parse_u32(argv[i]);
        }
    }
    rc = tiku_p4_pdm_start(rate, slot);
    if (rc != TIKU_P4_PDM_OK) {
        SHELL_PRINTF("mic: start failed (%d): %s\n", rc,
                     rc == TIKU_P4_PDM_ERR_STATE ? "already running"
                     : rc == TIKU_P4_PDM_ERR_PARAM ? "rate 8000-39000"
                                                   : "pads or DMA held");
        return;
    }
    SHELL_PRINTF("mic: %lu Hz, PDM clock %lu Hz (decimation %lu), slot %s\n",
                 (unsigned long)rate,
                 (unsigned long)tiku_p4_pdm_clock_hz(rate, &decimation),
                 (unsigned long)decimation,
                 slot == TIKU_P4_PDM_SLOT_LEFT    ? "left"
                 : slot == TIKU_P4_PDM_SLOT_RIGHT ? "right"
                                                  : "both");
}

/** @brief Read @p want samples a chunk at a time; @return the count, or
 *         the first read's error. */
static int32_t take(int16_t *chunk, uint32_t want,
                    void (*each)(const int16_t *, uint32_t, uint32_t))
{
    uint32_t done = 0u;
    int32_t n;

    while (done < want) {
        n = tiku_p4_pdm_read(chunk, want - done < CHUNK ? want - done : CHUNK,
                             READ_WAIT_MS);
        if (n <= 0) {
            return done ? (int32_t)done : n;
        }
        if (each != NULL) {
            each(chunk, (uint32_t)n, done);
        }
        done += (uint32_t)n;
    }
    return (int32_t)done;
}

static void mic_level(void)
{
    int16_t chunk[CHUNK];
    uint32_t peak, rms;
    int32_t n = take(chunk, LEVEL_SAMPLES, NULL);

    if (n <= 0) {
        SHELL_PRINTF("mic: no samples (%ld)\n", (long)n);
        return;
    }
    tiku_p4_pdm_level(&peak, &rms);
    SHELL_PRINTF("mic: %ld samples, peak %lu (%lu%% of full scale), rms %lu\n",
                 (long)n, (unsigned long)peak,
                 (unsigned long)(peak * 100u / 32767u), (unsigned long)rms);
}

static void mic_stats(void)
{
    tiku_p4_pdm_stats_t s;

    tiku_p4_pdm_stats(&s);
    SHELL_PRINTF("mic: %s, %lu Hz; samples %lu, buffers %lu, overruns %lu, "
                 "faults %lu\n",
                 tiku_p4_pdm_running() ? "running" : "stopped",
                 (unsigned long)tiku_p4_pdm_rate(), (unsigned long)s.samples,
                 (unsigned long)s.buffers, (unsigned long)s.overruns,
                 (unsigned long)s.faults);
}

/** @brief @p want samples as signed decimals, 16 a line, "mic: end" after. */
/** @brief Print a chunk of samples, 16 to a line, numbered from @p done. */
static void print_chunk(const int16_t *s, uint32_t n, uint32_t done)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        SHELL_PRINTF("%d%s", s[i], (done + i + 1u) % 16u == 0u ? "\n" : " ");
    }
}

static void mic_dump(uint32_t want)
{
    int16_t chunk[CHUNK];
    int32_t n;

    if (want == 0u || want > DUMP_MAX) {
        SHELL_PRINTF("mic: 1 to %u samples\n", DUMP_MAX);
        return;
    }
    SHELL_PRINTF("mic: %lu samples at %lu Hz\n", (unsigned long)want,
                 (unsigned long)tiku_p4_pdm_rate());
    n = take(chunk, want, print_chunk);
    if (n > 0 && (uint32_t)n % 16u != 0u) {
        SHELL_PRINTF("\n");
    }
    if (n < (int32_t)want) {
        SHELL_PRINTF("mic: stopped after %ld (%ld)\n", (long)(n > 0 ? n : 0),
                     (long)(n > 0 ? 0 : n));
    }
    SHELL_PRINTF("mic: end\n");
}

void tiku_shell_cmd_mic(uint8_t argc, const char *argv[])
{
#if TIKU_MEDIA_FS_ENABLE && TIKU_P4_SDCARD_ENABLE
    if ((argc == 4u || argc == 5u) && tiku_cmd_streq(argv[1], "record")) {
        tiku_media_record_result_t result;
        uint32_t seconds=tiku_cmd_parse_u32(argv[3]);
        uint32_t rate=argc==5u?tiku_cmd_parse_u32(argv[4]):DEFAULT_RATE;
        SHELL_PRINTF("mic: recording %lu seconds at %lu Hz to %s\n",
                     (unsigned long)seconds,(unsigned long)rate,argv[2]);
        int rc=tiku_media_record_wave(argv[2],seconds,rate,&result);
        SHELL_PRINTF("mic: result=%d samples=%lu bytes=%lu elapsed_ms=%lu peak=%lu clipped=%lu overruns=%lu faults=%lu\n",
                     rc,(unsigned long)result.samples,(unsigned long)result.bytes,
                     (unsigned long)result.elapsed_ms,(unsigned long)result.peak,
                     (unsigned long)result.clipped,(unsigned long)result.overruns,(unsigned long)result.faults);
        return;
    }
#endif
    if (argc >= 2u && tiku_cmd_streq(argv[1], "start")) {
        mic_start(argc, argv);
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "stop")) {
        tiku_p4_pdm_stop();
        SHELL_PRINTF("mic: stopped\n");
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "level")) {
        mic_level();
    } else if (argc == 2u && tiku_cmd_streq(argv[1], "stats")) {
        mic_stats();
    } else if (argc == 3u && tiku_cmd_streq(argv[1], "dump")) {
        mic_dump(tiku_cmd_parse_u32(argv[2]));
    } else {
        SHELL_PRINTF("Usage: mic start [rate] [left|right|both]\n"
                     "       mic stop | level | stats | dump <samples>\n");
#if TIKU_MEDIA_FS_ENABLE && TIKU_P4_SDCARD_ENABLE
        SHELL_PRINTF("       mic record NAME SECONDS [rate] (mounted media, PSRAM)\n");
#endif
    }
}
