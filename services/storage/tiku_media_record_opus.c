/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_record_opus.c - Streaming microphone compression to native media.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_record.h"
#include "tiku_media_card.h"
#include <services/audio/tiku_opus.h>
#include <arch/esp32p4/tiku_pdm_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <string.h>
#define OPUS_CLIENT 0x4d465404u
#define CAPTURE_ERROR (-20)
#define CAPTURE_LOSS (-21)
#define CODEC_ERROR (-22)
#define BATCH 32768u
/** @brief Recorder state and compressed output occupy one PSRAM allocation. */
typedef struct {
    tiku_opus_t opus;
    tiku_media_request_t request;
    tiku_media_record_result_t *result;
    uint32_t used;
    int storage_error;
    uint8_t buffer[BATCH];
    int16_t pcm[512];
} recording_t;
/** @brief Submit an operation as the recording owner. */
static int request(recording_t *r)
{
    r->request.client=OPUS_CLIENT;r->request.permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE;
    return tiku_media_card_request(&r->request);
}
/** @brief Append and checkpoint a compressed batch while measuring storage latency. */
static int flush(recording_t *r)
{
    if(!r->used)return 0;
    uint64_t start=0,end=0;
    if(tiku_p4_systimer_read(&start))return CAPTURE_ERROR;
    tiku_watchdog_kick();r->request.op=TIKU_MEDIA_APPEND;r->request.buffer=r->buffer;r->request.length=r->used;
    int rc=request(r);
    if(!rc){r->result->bytes+=r->used;r->used=0;r->request.op=TIKU_MEDIA_SYNC;rc=request(r);}
    if(tiku_p4_systimer_read(&end)&&!rc)rc=CAPTURE_ERROR;
    uint32_t us=(uint32_t)((end-start)/16);
    if(us>r->result->write_us_max)r->result->write_us_max=us;
    r->storage_error=rc;return rc;
}
/** @brief Buffer complete Ogg pages and write batches before the buffer fills. */
static int output(void *context,const uint8_t *p,uint32_t n)
{
    recording_t *r=context;
    if(n>BATCH)return CODEC_ERROR;
    if(r->used+n>BATCH){int rc=flush(r);if(rc)return rc;}
    memcpy(r->buffer+r->used,p,n);r->used+=n;return 0;
}
/** @brief Consume exact sample counts, accumulating levels for retained samples. */
static int read_pcm(recording_t *r,uint32_t count,int retain)
{
    uint32_t at=0;
    while(at<count){
        tiku_watchdog_kick();int32_t n=tiku_p4_pdm_read(r->pcm+at,count-at,500);
        if(n<=0||(uint32_t)n>count-at)return CAPTURE_ERROR;
        if(retain)for(int32_t i=0;i<n;i++){
            int32_t v=r->pcm[at+(uint32_t)i];uint32_t magnitude=(uint32_t)(v<0?-v:v);
            if(magnitude>r->result->peak)r->result->peak=magnitude;
            if(v==32767||v==-32768)r->result->clipped++;
        }
        at+=(uint32_t)n;
    }
    if(retain)r->result->samples+=count;
    return 0;
}
int tiku_media_record_opus(const char *name,uint32_t seconds,tiku_media_record_result_t *result)
{
    if(!result)return TIKU_MFS_ARG;
    memset(result,0,sizeof *result);
    if(!name||!*name||strlen(name)>=TIKU_MFS_NAME||!seconds||seconds>UINT32_MAX/16000u)return TIKU_MFS_ARG;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32||*p==127)return TIKU_MFS_ARG;
    tiku_media_store_t *store=tiku_media_card_store();if(!store)return TIKU_MFS_NOFS;
    if(tiku_p4_pdm_running()||store->writer||store->collector)return TIKU_MFS_BUSY;
    tiku_arena_t arena={0},codec={0};tiku_mem_request_t options={0};options.allocation_class=TIKU_MEM_TRANSIENT;options.alignment=8;
    uint32_t state_bytes=(uint32_t)((tiku_opus_state_bytes()+7)&~(size_t)7);
    uint32_t codec_bytes=state_bytes+TIKU_OPUS_SCRATCH+8;
    if(tiku_tier_arena_create_opts(&arena,TIKU_MEM_PSRAM,sizeof(recording_t),94,&options))return TIKU_MFS_SPACE;
    recording_t *r=tiku_arena_alloc(&arena,sizeof *r);
    int rc=TIKU_MFS_SPACE,started=0,opened=0;
    if(!r)goto cleanup;
    memset(r,0,sizeof *r);r->result=result;
    tiku_mfs_object_t info;r->request.op=TIKU_MEDIA_STAT;r->request.name=name;r->request.info=&info;
    rc=request(r);if(!rc){rc=TIKU_MFS_BUSY;goto cleanup;}if(rc!=TIKU_MFS_NOENT)goto cleanup;
    rc=TIKU_MFS_SPACE;
    if(tiku_tier_arena_create_opts(&codec,TIKU_MEM_SRAM,codec_bytes,95,&options))goto cleanup;
    uint8_t *raw=tiku_arena_alloc(&codec,codec_bytes);if(!raw)goto cleanup;
    uint8_t *memory=(uint8_t *)(((uintptr_t)raw+7)&~(uintptr_t)7);
    uint64_t stamp=0;if(tiku_p4_systimer_read(&stamp)){rc=CAPTURE_ERROR;goto cleanup;}
    rc=tiku_opus_open(&r->opus,memory,state_bytes,memory+state_bytes,TIKU_OPUS_SCRATCH,
                      (uint32_t)(stamp^(stamp>>32)),output,r);
    if(rc){rc=CODEC_ERROR;goto cleanup;}opened=1;
    r->request.op=TIKU_MEDIA_BEGIN;r->request.info=NULL;r->request.policy=TIKU_MFS_STREAM;
    rc=request(r);if(rc)goto cleanup;result->id=r->request.id;
    rc=tiku_p4_pdm_start(16000,TIKU_P4_PDM_SLOT_LEFT);
    if(rc){rc=CAPTURE_ERROR;goto cleanup;}started=1;
    for(unsigned i=0;i<8&&!rc;i++)rc=read_pcm(r,512,0);
    uint64_t start=0,end=0,encode_ticks=0;
    if(!rc&&tiku_p4_systimer_read(&start))rc=CAPTURE_ERROR;
    uint32_t total=seconds*16000u;
    uint32_t frames=(uint32_t)(((uint64_t)total+r->opus.preskip/3+319)/320);
    for(uint32_t i=0;i<frames&&!rc;i++){
        if(result->samples<total)rc=read_pcm(r,320,1);
        else memset(r->pcm,0,320*sizeof(int16_t));
        if(rc)break;
        /* Flush outside the measured encoder interval. */
        if(r->used+4096>BATCH){rc=flush(r);if(rc)break;}
        uint64_t a=0,b=0;
        if(tiku_p4_systimer_read(&a)){rc=CAPTURE_ERROR;break;}
        rc=tiku_opus_frame(&r->opus,r->pcm,i+1==frames,total);
        if(rc){rc=r->storage_error?r->storage_error:CODEC_ERROR;break;}
        if(tiku_p4_systimer_read(&b)){rc=CAPTURE_ERROR;break;}
        encode_ticks+=b-a;uint32_t us=(uint32_t)((b-a)/16);
        if(us>result->encode_us_max)result->encode_us_max=us;
        result->frames++;
        tiku_p4_pdm_stats_t stats;tiku_p4_pdm_stats(&stats);
        if(stats.overruns||stats.faults)rc=CAPTURE_LOSS;
    }
    if(tiku_p4_systimer_read(&end)&&!rc)rc=CAPTURE_ERROR;
    tiku_p4_pdm_stats_t stats;tiku_p4_pdm_stats(&stats);tiku_p4_pdm_stop();started=0;
    result->overruns=stats.overruns;result->faults=stats.faults;
    if(end>=start)result->elapsed_ms=(uint32_t)((end-start)/16000);
    if(result->frames)result->encode_us_mean=(uint32_t)(encode_ticks/16/result->frames);
    if(!rc&&(stats.overruns||stats.faults))rc=CAPTURE_LOSS;
    if(!rc)rc=flush(r);
    if(!rc){r->request.op=TIKU_MEDIA_FINALIZE;rc=request(r);}
cleanup:
    if(started)tiku_p4_pdm_stop();
    if(rc&&result->id){r->request.op=TIKU_MEDIA_RELEASE;(void)request(r);}
    if(opened)tiku_opus_close(&r->opus);
    if(codec.buf){(void)tiku_arena_reset(&codec);(void)tiku_arena_destroy(&codec);}
    (void)tiku_arena_reset(&arena);(void)tiku_arena_destroy(&arena);return rc;
}
