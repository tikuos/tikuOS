/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_test_pattern.h - deterministic byte, raster and sensor IPC fixtures.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_TEST_PATTERN_H_
#define TIKU_IPC_TEST_PATTERN_H_
#include <stdint.h>
/** @brief One fixture byte: generated data, 640-wide grayscale, or 16-byte records. */
static inline uint8_t tiku_ipc_test_byte(unsigned profile,uint32_t offset)
{
    uint32_t x=offset%640u,y=offset/640u,record=offset/16u,field=offset%16u,value;
    if(profile==1)return (uint8_t)((x*3u+y*5u)^((x>>5)*17u)^((y>>4)*29u));
    if(profile!=2)return (uint8_t)((offset*17u)^(offset>>8));
    if(field<4)value=record*1000u+42u;
    else if(field<8)value=record;
    else if(field<10)value=record*31u;
    else if(field<12)value=record*17u+100u;
    else if(field<14)value=(record*7u)^0x5533u;
    else value=record%8u;
    return (uint8_t)(value>>(8u*(field<8?field%4u:field%2u)));
}
#endif
