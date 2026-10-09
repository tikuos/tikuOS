/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp32c5_rom.ld - ESP32-C5 ECO 2 bindings from ESP-IDF 4d59230.
 * SPDX-License-Identifier: Apache-2.0
 */
tiku_c5_rom_reset_reason = 0x40000018;
tiku_c5_rom_cpu_frequency = 0x40000040;
tiku_c5_rom_cpu_frequency_set = 0x40000044;
tiku_c5_rom_reset = 0x40000094;
tiku_c5_rom_flash_erase = 0x40000154;
tiku_c5_rom_flash_write = 0x4000015c;
tiku_c5_rom_flash_read = 0x40000160;
tiku_c5_rom_flash_unlock = 0x40000164;
tiku_c5_rom_flash_config = 0x40000170;
tiku_c5_rom_flash_command = 0x40000174;
tiku_c5_rom_flash_attach = 0x400001f0;
tiku_c5_rom_cache_boot = 0x40000644;
tiku_c5_rom_cache_invalidate = 0x40000650;
tiku_c5_rom_cache_writeback_invalidate = 0x4000065c;
tiku_c5_rom_cache_enable = 0x400006b0;
tiku_c5_rom_cache_suspend = 0x400006b4;
tiku_c5_rom_cache_resume = 0x400006b8;
tiku_c5_rom_mmu_init = 0x400006d0;
tiku_c5_rom_mmu_set = 0x400006d4;
