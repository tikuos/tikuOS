/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_probe.c - Primary partition tables and bounded filesystem geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_probe.h"
#include <string.h>

/** @brief Decode an unaligned little-endian 16-bit value. */
static uint16_t u16(const uint8_t *p)
{ return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
/** @brief Decode an unaligned little-endian 32-bit value. */
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)u16(p)|((uint32_t)u16(p+2)<<16); }
/** @brief Decode an unaligned little-endian 64-bit value. */
static uint64_t u64(const uint8_t *p)
{ return (uint64_t)u32(p)|((uint64_t)u32(p+4)<<32); }
/** @brief Decode an unaligned big-endian 32-bit value. */
static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
/** @brief Advance the reflected IEEE CRC32 without final inversion. */
static uint32_t crc32(uint32_t crc,const uint8_t *p,unsigned n)
{
    while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return crc;
}
/** @brief Check a nonempty sector extent without addition overflow. */
static int extent(uint64_t first,uint64_t length,uint64_t limit)
{ return length&&first<limit&&length<=limit-first; }
/** @brief Identify a boot sector and check its 512-byte-sector geometry. */
static void filesystem(const uint8_t *s,tiku_media_partition_t *p)
{
    p->fs=TIKU_MEDIA_UNKNOWN;p->geometry_valid=0;
    if(u16(s+510)!=0xaa55)return;
    if(!memcmp(s+3,"EXFAT   ",8)){
        p->fs=TIKU_MEDIA_EXFAT;
        if(s[108]<9||s[108]>12)return;
        p->sector_bytes=(uint16_t)(1u<<s[108]);p->volume_sectors=u64(s+72);
        if(s[109]>25u-s[108])return;
        p->cluster_bytes=1u<<(s[108]+s[109]);
        uint64_t fat=u32(s+80),flen=u32(s+84),heap=u32(s+88),clusters=u32(s+92);
        for(unsigned n=11;n<64;n++)if(s[n])return;
        if(s[108]!=9||u16(s+104)!=0x100||(s[110]!=1&&s[110]!=2)||
           !extent(0,p->volume_sectors,p->sectors)||u64(s+64)!=p->first||
           fat<24||!extent(fat,flen*s[110],p->volume_sectors)||heap<fat+flen*s[110]||
           !clusters||clusters>0xfffffff5u||u32(s+96)<2||u32(s+96)>clusters+1||
           flen*512<(clusters+2)*4||!extent(heap,clusters<<s[109],p->volume_sectors))return;
        p->geometry_valid=1;return;
    }
    if(!memcmp(s+3,"NTFS    ",8)){p->fs=TIKU_MEDIA_NTFS;return;}
    unsigned bps=u16(s+11),spc=s[13],reserved=u16(s+14),fats=s[16],roots=u16(s+17);
    uint64_t total=u16(s+19)?u16(s+19):u32(s+32);
    uint64_t fatlen=u16(s+22)?u16(s+22):u32(s+36);
    if(bps!=512||!spc||spc>128||(spc&(spc-1))||!reserved||!fats||fats>2||
       !fatlen||!extent(0,total,p->sectors))return;
    uint64_t rootsecs=((uint64_t)roots*32+511)/512,used=reserved+fatlen*fats+rootsecs;
    if(used>=total)return;
    uint64_t clusters=(total-used)/spc;
    if(!clusters||clusters>0x0fffffedu)return;
    tiku_media_fs_t fs=clusters<4085?TIKU_MEDIA_FAT12:clusters<65525?TIKU_MEDIA_FAT16:TIKU_MEDIA_FAT32;
    uint64_t need=fs==TIKU_MEDIA_FAT12?((clusters+2)*3+1)/2:(clusters+2)*(fs==TIKU_MEDIA_FAT16?2:4);
    if(fatlen*512<need)return;
    if(fs==TIKU_MEDIA_FAT32){
        if(roots||u16(s+22)||u16(s+42)||u32(s+44)<2||u32(s+44)>clusters+1)return;
    }else if(!roots||!u16(s+22))return;
    p->fs=fs;p->geometry_valid=1;p->sector_bytes=512;
    p->cluster_bytes=spc*512;p->volume_sectors=total;
}
/** @brief Identify an HFS-family header at byte 1024, using Apple's TN1150 fields. */
static void hfs_header(const uint8_t *s,tiku_media_partition_t *p)
{
    if(s[0]=='B'&&s[1]=='D'){p->fs=TIKU_MEDIA_HFS;return;}
    if(s[0]!='H'||(s[1]!='+'&&s[1]!='X'))return;
    p->fs=s[1]=='+'?TIKU_MEDIA_HFS_PLUS:TIKU_MEDIA_HFSX;
    uint32_t bytes=be32(s+40),blocks=be32(s+44);
    if(s[2]||s[3]!=(s[1]=='+'?4:5)||bytes<512||(bytes&(bytes-1))||
       !blocks||be32(s+48)>blocks)return;
    uint64_t sectors=(uint64_t)blocks*(bytes/512);
    if(!extent(0,sectors,p->sectors)||sectors<3)return;
    p->volume_sectors=sectors;p->cluster_bytes=bytes;p->sector_bytes=512;p->geometry_valid=1;
}
/** @brief Parse and checksum a bounded primary GPT before reading partition boot sectors. */
static tiku_media_result_t gpt(tiku_media_probe_t *out,uint64_t sectors,
    tiku_media_read_fn read,void *ctx,uint8_t s[512])
{
    out->table=TIKU_MEDIA_GPT;
    if(read(1,s,ctx))return TIKU_MEDIA_IO;
    if(memcmp(s,"EFI PART",8)||u32(s+8)!=0x10000||u32(s+12)<92||u32(s+12)>512||
       u32(s+20)||u64(s+24)!=1||u64(s+32)!=sectors-1)return TIKU_MEDIA_INVALID;
    uint32_t expected=u32(s+16);memset(s+16,0,4);
    if(~crc32(UINT32_MAX,s,u32(s+12))!=expected)return TIKU_MEDIA_INVALID;
    uint64_t first=u64(s+40),last=u64(s+48),array=u64(s+72);
    uint32_t count=u32(s+80),size=u32(s+84),array_crc=u32(s+88);
    if(!count||count>128||size!=128)return TIKU_MEDIA_UNSUPPORTED;
    unsigned blocks=(count+3)/4;
    if(first>last||last>=sectors-1||array<2||!extent(array,blocks,first))return TIKU_MEDIA_INVALID;
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<blocks;i++){
        if(read((uint32_t)(array+i),s,ctx))return TIKU_MEDIA_IO;
        unsigned entries=count-i*4;if(entries>4)entries=4;
        crc=crc32(crc,s,entries*128);
        for(unsigned j=0;j<entries;j++){
            const uint8_t *e=s+j*128;unsigned used=0;
            for(unsigned b=0;b<16;b++)used|=e[b];
            if(!used)continue;
            if(out->count==TIKU_MEDIA_PARTS)return TIKU_MEDIA_LIMIT;
            uint64_t a=u64(e+32),z=u64(e+40);
            if(a<first||a>z||z>last)return TIKU_MEDIA_INVALID;
            tiku_media_partition_t *p=&out->parts[out->count++];
            p->index=(uint16_t)(i*4+j+1);p->first=a;p->sectors=z-a+1;
        }
    }
    return ~crc==array_crc?TIKU_MEDIA_OK:TIKU_MEDIA_INVALID;
}

tiku_media_result_t tiku_media_probe(tiku_media_probe_t *out,uint64_t sectors,
    tiku_media_read_fn read,void *context)
{
    uint8_t s[512];
    if(!out||!read||!sectors||sectors>0x100000000ull)return TIKU_MEDIA_INVALID;
    memset(out,0,sizeof *out);
    if(read(0,s,context))return TIKU_MEDIA_IO;
    if(u16(s+510)!=0xaa55)return TIKU_MEDIA_OK;
    tiku_media_partition_t whole={.sectors=sectors};filesystem(s,&whole);
    if(whole.fs!=TIKU_MEDIA_UNKNOWN){
        out->table=TIKU_MEDIA_WHOLE;out->parts[0]=whole;out->count=1;return TIKU_MEDIA_OK;
    }
    out->table=TIKU_MEDIA_MBR;
    unsigned protective=0;
    for(unsigned i=0;i<4;i++){
        const uint8_t *e=s+446+16*i;
        if(!e[4]){if(u32(e+8)||u32(e+12))return TIKU_MEDIA_INVALID;continue;}
        if((e[0]!=0&&e[0]!=0x80)||!u32(e+8)||!extent(u32(e+8),u32(e+12),sectors))return TIKU_MEDIA_INVALID;
        tiku_media_partition_t *p=&out->parts[out->count++];
        p->index=(uint16_t)(i+1);p->type=e[4];p->boot=e[0]==0x80;
        p->first=u32(e+8);p->sectors=u32(e+12);protective+=e[4]==0xee;
    }
    if(protective){
        if(out->count!=1||out->parts[0].first!=1)return TIKU_MEDIA_UNSUPPORTED;
        memset(out->parts,0,sizeof out->parts);out->count=0;
        tiku_media_result_t result=gpt(out,sectors,read,context,s);if(result)return result;
    }
    for(unsigned i=0;i<out->count;i++){
        tiku_media_partition_t *p=&out->parts[i];
        if(p->type==5||p->type==15||p->type==0x85)return TIKU_MEDIA_UNSUPPORTED;
        for(unsigned j=0;j<i;j++){
            const tiku_media_partition_t *q=&out->parts[j];
            if(p->first<q->first+q->sectors&&q->first<p->first+p->sectors)return TIKU_MEDIA_INVALID;
        }
    }
    for(unsigned i=0;i<out->count;i++){
        if(read((uint32_t)out->parts[i].first,s,context))return TIKU_MEDIA_IO;
        filesystem(s,&out->parts[i]);
        tiku_media_partition_t *p=&out->parts[i];
        if(p->fs==TIKU_MEDIA_UNKNOWN&&p->sectors>=3&&(p->type==0xaf||out->table==TIKU_MEDIA_GPT)){
            if(read((uint32_t)(p->first+2),s,context))return TIKU_MEDIA_IO;
            hfs_header(s,p);
        }
    }
    return TIKU_MEDIA_OK;
}

const char *tiku_media_fs_name(tiku_media_fs_t fs)
{
    switch(fs){
    case TIKU_MEDIA_FAT12:return "FAT12";
    case TIKU_MEDIA_FAT16:return "FAT16";
    case TIKU_MEDIA_FAT32:return "FAT32";
    case TIKU_MEDIA_EXFAT:return "exFAT";
    case TIKU_MEDIA_NTFS:return "NTFS signature";
    case TIKU_MEDIA_HFS:return "HFS signature";
    case TIKU_MEDIA_HFS_PLUS:return "HFS+";
    case TIKU_MEDIA_HFSX:return "HFSX";
    default:return "unknown";
    }
}
