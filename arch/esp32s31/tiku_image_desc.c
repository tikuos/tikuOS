/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_image_desc.c - Application metadata for the Korvo factory bootloader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_image_desc.h"

/** @brief The 256-byte application descriptor at the first segment's start. */
static const struct {
    uint32_t magic, secure_version, reserved1[2];
    char version[32], project[32], time[16], date[16], framework[32];
    uint8_t elf_hash[32];
    uint16_t min_block_revision, max_block_revision;
    uint8_t page_size_log2, flash_mode, reserved3[2];
    uint32_t reserved2[18];
} image_descriptor __attribute__((section(".app_desc"), used)) = {
    .magic = 0xabcd5432u,
    .version = "0.06-s31-sram",
#ifdef TIKU_S31_PSRAM_TEST
    .project = "tikuos_s31_psram",
#else
    .project = "tikuos_s31_shell",
#endif
    .framework = "native TikuOS",
    .max_block_revision = 99,
    .page_size_log2 = 16,
    .flash_mode = 1,
};
_Static_assert(sizeof(image_descriptor) == 256, "S31 application descriptor size");

const char * __attribute__((section(".flash.text"), noinline))
tiku_s31_image_project(void)
{
    return image_descriptor.project;
}
