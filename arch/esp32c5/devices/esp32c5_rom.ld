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
tiku_c5_rom_flash_clock = 0x40000188;
tiku_c5_rom_flash_read_mode = 0x4000018c;
tiku_c5_rom_flash_attach = 0x400001f0;
tiku_c5_rom_cache_boot = 0x40000644;
tiku_c5_rom_cache_invalidate = 0x40000650;
tiku_c5_rom_cache_enable = 0x400006b0;
tiku_c5_rom_cache_suspend = 0x400006b4;
tiku_c5_rom_cache_resume = 0x400006b8;
tiku_c5_rom_mmu_init = 0x400006d0;
tiku_c5_rom_mmu_set = 0x400006d4;

/* The compiler's runtime routines, from the ROM: 64-bit integer shifts and
 * division, bit counts, and single- and double-precision arithmetic.  They
 * take no SRAM and run with the cache suspended. */
__addsf3 = 0x400008bc;
__divsf3 = 0x400008c0;
__eqsf2 = 0x400008c4;
__fixsfsi = 0x400008c8;
__floatsisf = 0x400008cc;
__floatunsisf = 0x400008d0;
__gesf2 = 0x400008d4;
__gtsf2 = 0x400008d8;
__lesf2 = 0x400008dc;
__ltsf2 = 0x400008e0;
__mulsf3 = 0x400008e4;
__nesf2 = 0x400008ec;
__subsf3 = 0x400008f4;
__truncdfsf2 = 0x400008f8;
__adddf3 = 0x40000908;
__ashldi3 = 0x40000914;
__ashrdi3 = 0x40000918;
__bswapsi2 = 0x40000920;
__clzsi2 = 0x40000934;
__ctzsi2 = 0x40000940;
__divdf3 = 0x40000948;
__divdi3 = 0x4000094c;
__eqdf2 = 0x40000958;
__extendsfdf2 = 0x4000095c;
__ffssi2 = 0x40000964;
__fixdfsi = 0x4000096c;
__fixunssfsi = 0x4000097c;
__floatsidf = 0x40000988;
__floatundisf = 0x40000990;
__floatunsidf = 0x40000994;
__gedf2 = 0x4000099c;
__gtdf2 = 0x400009a0;
__ledf2 = 0x400009a4;
__lshrdi3 = 0x400009a8;
__ltdf2 = 0x400009ac;
__moddi3 = 0x400009b0;
__muldf3 = 0x400009bc;
__nedf2 = 0x400009d4;
__popcountdi2 = 0x400009ec;
__popcountsi2 = 0x400009f0;
__subdf3 = 0x400009f8;
__udivdi3 = 0x40000a08;
__umoddi3 = 0x40000a18;
__unorddf2 = 0x40000a20;
