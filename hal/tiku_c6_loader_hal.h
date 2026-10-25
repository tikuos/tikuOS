/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_c6_loader_hal.h - resident-loader packet and owned kernel-bank format.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_LOADER_HAL_H_
#define TIKU_C6_LOADER_HAL_H_
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define TIKU_C6_LOADER_HEADER 32u
#define TIKU_C6_LOADER_FRAME 512u
#define TIKU_C6_KERNEL_MAX 49152u
#define TIKU_C6_ARENA_BYTES 65536u
/** @brief Resident operations preserve the existing loader's assigned opcodes. */
enum {
    TIKU_C6_INFO=1,TIKU_C6_READ=2,TIKU_C6_REBOOT=3,TIKU_C6_MD5=4,
    TIKU_C6_RAM_LOAD=7,TIKU_C6_RAM_RUN=8,TIKU_C6_OWNER=10,TIKU_C6_BEGIN=11,
    TIKU_C6_DATA=12,TIKU_C6_COMMIT=13,TIKU_C6_STATUS=14,TIKU_C6_KEEP=15,
    TIKU_C6_GO=16,TIKU_C6_RESET=17,TIKU_C6_FACTORY=18
};
/** @brief Manifest precedes an image inside an owned 64 KiB kernel bank. */
typedef struct {
    uint32_t magic,version,base,bytes,runtime,crc,build,header_crc;
} tiku_c6_manifest_t;
/** @brief Read an unaligned little-endian word. */
static inline uint32_t tiku_c6_get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
/** @brief Write an unaligned little-endian word. */
static inline void tiku_c6_put32(uint8_t *p,uint32_t v)
{ for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(n*8u)); }
/** @brief CRC-32 over bytes, optionally zeroing a packet's checksum field. */
static inline uint32_t tiku_c6_crc(const void *data,size_t length,uint8_t packet)
{
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    for(size_t n=0;n<length;n++){
        crc^=packet&&n>=28&&n<32?0:p[n];
        for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
    }
    return ~crc;
}
/** @brief Encode the bounded request; sequence must be nonzero and payload copied. */
static inline int tiku_c6_packet(uint8_t out[512],uint8_t op,uint32_t sequence,
    uint32_t offset,uint32_t length,const void *payload,uint16_t bytes)
{
    if(!sequence||bytes>480||(bytes&&!payload))return -1;
    memset(out,0,512);memcpy(out,"TKC6",4);out[4]=1;out[5]=op;
    tiku_c6_put32(out+8,sequence);tiku_c6_put32(out+12,offset);
    tiku_c6_put32(out+16,length);tiku_c6_put32(out+24,bytes);
    if(bytes)memcpy(out+32,payload,bytes);
    tiku_c6_put32(out+28,tiku_c6_crc(out,32u+bytes,1));return 0;
}
/** @brief Validate a complete response and its captured operation/sequence identity. */
static inline int tiku_c6_response(const void *data,uint16_t received,uint8_t op,uint32_t seq)
{
    const uint8_t *p=data;uint32_t bytes;
    if(!p||received<32||received>512||memcmp(p,"TKC6",4)||p[4]!=1||p[5]!=op||p[6]||p[7]||
        tiku_c6_get32(p+8)!=seq)return 0;
    bytes=tiku_c6_get32(p+24);
    return bytes<=480&&bytes+32u==received&&tiku_c6_get32(p+28)==tiku_c6_crc(p,received,1);
}
#endif
