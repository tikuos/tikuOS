/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_opus.c - Fixed-point Opus and Ogg pages with encoder-delay trimming.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_opus.h"
#include <opus.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
extern char *global_stack,*scratch_ptr;
static tiku_opus_t *owner;
static jmp_buf fault;
/** @brief Return an upstream hardening failure to the active encoder call. */
_Noreturn void celt_fatal(const char *message,const char *file,int line)
{ (void)message;(void)file;(void)line;longjmp(fault,1); }
/** @brief Store an unsigned little-endian field. */
static void put(uint8_t *p,uint64_t value,unsigned n)
{ for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(value>>(8*i)); }
/** @brief Emit an Ogg page with a CRC covering its header, lacing and payload. */
static int page(tiku_opus_t *s,unsigned flags,uint64_t granule,unsigned count,unsigned bytes)
{
    uint8_t *p=s->page;memset(p,0,27);memcpy(p,"OggS",4);p[5]=(uint8_t)flags;
    put(p+6,granule,8);put(p+14,s->serial,4);put(p+18,s->sequence++,4);p[26]=(uint8_t)count;
    uint32_t crc=0,n=27+count+bytes;
    for(uint32_t i=0;i<n;i++){
        crc^=(uint32_t)p[i]<<24;
        for(unsigned j=0;j<8;j++)crc=(crc<<1)^((crc&0x80000000u)?0x04c11db7u:0);
    }
    put(p+22,crc,4);return s->sink(s->sink_arg,p,n);
}
size_t tiku_opus_state_bytes(void) { return (size_t)opus_encoder_get_size(1); }
int tiku_opus_open(tiku_opus_t *s,void *state,size_t state_bytes,void *scratch,
                   size_t scratch_bytes,uint32_t serial,tiku_opus_sink_t sink,void *arg)
{
    if(!s||!state||!scratch||!sink||owner||state_bytes<tiku_opus_state_bytes()||
       scratch_bytes<TIKU_OPUS_SCRATCH||((uintptr_t)state&7)||((uintptr_t)scratch&7))return OPUS_BAD_ARG;
    memset(s,0,sizeof *s);s->encoder=state;s->serial=serial;s->sink=sink;s->sink_arg=arg;
    owner=s;global_stack=scratch_ptr=scratch;
    if(setjmp(fault)){tiku_opus_close(s);return OPUS_INTERNAL_ERROR;}
    opus_int32 delay=0;
    int rc=opus_encoder_init(state,16000,1,OPUS_APPLICATION_AUDIO);
    if(!rc)rc=opus_encoder_ctl(state,OPUS_SET_BITRATE(24000));
    if(!rc)rc=opus_encoder_ctl(state,OPUS_SET_VBR(0));
    if(!rc)rc=opus_encoder_ctl(state,OPUS_SET_COMPLEXITY(0));
    if(!rc)rc=opus_encoder_ctl(state,OPUS_GET_LOOKAHEAD(&delay));
    if(rc){tiku_opus_close(s);return rc;}
    s->preskip=(uint16_t)(delay*3);
    uint8_t *p=s->page+28;memset(p,0,19);memcpy(p,"OpusHead",8);p[8]=1;p[9]=1;
    put(p+10,s->preskip,2);put(p+12,16000,4);s->page[27]=19;
    rc=page(s,2,0,1,19);
    if(!rc){p=s->page+28;memset(p,0,22);memcpy(p,"OpusTags",8);put(p+8,6,4);memcpy(p+12,"TikuOS",6);
        s->page[27]=22;rc=page(s,0,0,1,22);}
    if(rc)tiku_opus_close(s);
    return rc;
}
int tiku_opus_frame(tiku_opus_t *s,const int16_t *pcm,int final,uint32_t total)
{
    if(owner!=s||!pcm)return OPUS_BAD_ARG;
    if(setjmp(fault))return OPUS_INTERNAL_ERROR;
    unsigned slot=s->packets;
    int n=opus_encode(s->encoder,pcm,320,s->page+77+60*slot,60);
    if(n!=60)return n<0?n:OPUS_INTERNAL_ERROR;
    s->page[27+slot]=60;s->packets++;s->frames++;
    if(s->packets==50||final){
        uint64_t granule=final?(uint64_t)total*3+s->preskip:(uint64_t)s->frames*960;
        if(final&&(granule>(uint64_t)s->frames*960||granule<((uint64_t)s->frames-1)*960))return OPUS_BAD_ARG;
        memmove(s->page+27+s->packets,s->page+77,s->packets*60);
        int rc=page(s,final?4:0,granule,s->packets,s->packets*60);s->packets=0;return rc;
    }
    return 0;
}
void tiku_opus_close(tiku_opus_t *s)
{ if(owner==s){owner=NULL;global_stack=scratch_ptr=NULL;s->encoder=NULL;} }
