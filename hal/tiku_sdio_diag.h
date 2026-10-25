/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_diag.h - the C6 native diagnostic mailbox envelope.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SDIO_DIAG_H_
#define TIKU_SDIO_DIAG_H_
#include <stdint.h>
/** @brief Ready marker of the native shared-register diagnostic peer. */
#define TIKU_SDIO_NATIVE_MAGIC 0x364b4954u
#include <stddef.h>
#define TIKU_SDIO_DIAG_MAGIC 0x364b4954UL
#define TIKU_SDIO_DIAG_REQUEST 12u
#define TIKU_SDIO_DIAG_RESPONSE 24u
#define TIKU_SDIO_DIAG_READY 20u
/** @brief Card shared-register byte address; UINT16_MAX outside its 64-byte window. */
static inline uint16_t tiku_sdio_diag_address(unsigned index)
{
    return index < 64 ? (uint16_t)(0x6c+index+(index>23?4:0)+(index>31?12:0)) : UINT16_MAX;
}
/** @brief CRC-16/CCITT-FALSE over sequence, length, used payload and session. */
/* data spans limit+8 bytes and its payload length does not exceed limit. */
static inline uint16_t tiku_sdio_diag_crc(const uint8_t *data, unsigned limit)
{
    uint16_t value = 0xffff;
    for (unsigned i = 0; i < data[1]+6u; i++) {
        uint8_t byte = i < 2 ? data[i] : i < data[1]+2u
            ? data[i+2] : data[4+limit+i-data[1]-2u];
        value ^= (uint16_t)byte << 8;
        for (unsigned bit = 0; bit < 8; bit++)
            value = (uint16_t)((value << 1) ^ ((value & 0x8000u) ? 0x1021u : 0));
    }
    return value;
}
/** @brief Seal a native envelope, preserving its 12/24-byte payload layout. */
static inline int tiku_sdio_diag_seal(uint8_t *data, size_t bytes, unsigned limit)
{
    uint16_t value;
    if (!data || (limit != 12 && limit != 24) || bytes != limit+8u ||
        !data[0] || data[1] > limit) return -1;
    value = tiku_sdio_diag_crc(data,limit);
    data[2] = (uint8_t)value; data[3] = (uint8_t)(value >> 8);
    return 0;
}
/** @brief Validate framing before a mode owner checks session and sequence. */
static inline int tiku_sdio_diag_valid(const uint8_t *data, size_t bytes, unsigned limit)
{
    uint16_t value;
    if (!data || (limit != 12 && limit != 24) || bytes != limit+8u ||
        !data[0] || data[1] > limit) return 0;
    value = tiku_sdio_diag_crc(data,limit);
    return data[2] == (uint8_t)value && data[3] == (uint8_t)(value >> 8);
}
/** @brief Decode the little-endian session after validating the envelope. */
static inline uint32_t tiku_sdio_diag_session(const uint8_t *data, unsigned limit)
{
    const uint8_t *p = data+4+limit;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/** @brief Decode a committed nonce payload after the native envelope is validated. */
static inline uint64_t tiku_sdio_diag_epoch(const uint8_t *data,size_t length)
{
    uint64_t result=0;
    if(!data||length!=12||data[0]!='I'||data[1]!='P'||data[2]!='C'||data[3]!='E')return 0;
    for(unsigned n=0;n<8;n++)result|=(uint64_t)data[4+n]<<(8u*n);
    return result;
}
#endif
