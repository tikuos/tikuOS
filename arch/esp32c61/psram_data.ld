/*
 * psram_data.ld - big buffers in PSRAM (ESP32-C61), with
 * TIKU_ESP32C61_PSRAM_DATA=1: TLS's records and handshake, the crypto's
 * bignum scratch, BASIC's browse and scratch buffers.  The boot brings the
 * PSRAM up and zeroes them before anything runs that uses them.
 *
 * No interrupt touches them and nothing DMAs them; thread stacks stay in
 * SRAM, which a trap may push onto while a flash write has the cache off.
 */

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
