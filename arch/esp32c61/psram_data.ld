/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * psram_data.ld - ESP32-C61 big buffers in PSRAM (TIKU_ESP32C61_PSRAM_DATA=1).
 *
 * The .bss of TLS and the crypto kit, and BASIC's browse, persist, file and
 * HTTPS-roots buffers.  tiku_esp32c61_psram_data_boot() brings the PSRAM up
 * and zeroes them at boot, before anything uses them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* No interrupt handler touches these buffers and nothing DMAs them.  Thread
 * stacks stay in SRAM, which a trap may push onto while a flash write has
 * the cache off. */

.psram.bss (NOLOAD) :
{
    . = ALIGN(16);
    __tiku_psram_data_start = .;
    *tikukits/net/tls/*.o(.bss .bss.* COMMON)
    *tikukits/crypto/*.o(.bss .bss.* COMMON)
    *kernel/shell/basic/*.o(.bss.basic_browse_buf .bss.basic_persist_scratch
                            .bss.basic_file_scratch .bss.basic_https_roots)
    . = ALIGN(16);
    __tiku_psram_data_end = .;
} > PSRAM
