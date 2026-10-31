/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_record.c - P4 PCM capture in PSRAM followed by checked WAV storage.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_record.h"
#include "tiku_media_card.h"
#include <arch/esp32p4/tiku_pdm_arch.h>
#include <arch/esp32p4/tiku_systimer_arch.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <string.h>
#define CLIENT 0x4d465403u
#define CAPTURE_ERROR (-20)
#define CAPTURE_LOSS (-21)
/** @brief Encode a WAV field without unaligned word stores. */
static void put32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
/** @brief Submit a storage operation with the recorder's owner identity. */
static int request(tiku_media_request_t *r)
{r->client=CLIENT;r->permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE;return tiku_media_card_request(r);}
/** @brief Fill an exact sample count without retaining driver-owned DMA buffers. */
static int capture(int16_t *out,uint32_t count,tiku_media_record_result_t *result)
{
    uint32_t done=0;
    while(done<count){
        uint32_t want=count-done;if(want>512)want=512;
        tiku_watchdog_kick();int32_t got=tiku_p4_pdm_read(out+done,want,500);
        if(got<=0||(uint32_t)got>want)return CAPTURE_ERROR;
        if(result)for(int32_t i=0;i<got;i++){
            int32_t value=out[done+(uint32_t)i];uint32_t magnitude=(uint32_t)(value<0?-value:value);
            if(magnitude>result->peak)result->peak=magnitude;
            if(value==32767||value==-32768)result->clipped++;
        }
        done+=(uint32_t)got;if(result)result->samples=done;
    }
    return 0;
}
int tiku_media_record_wave(const char *name,uint32_t seconds,uint32_t rate,
                           tiku_media_record_result_t *result)
{
    if(!result)return TIKU_MFS_ARG;
    memset(result,0,sizeof *result);
    if(!name||!*name||strlen(name)>=TIKU_MFS_NAME||!seconds||rate<TIKU_P4_PDM_RATE_MIN||
       rate>TIKU_P4_PDM_RATE_MAX||seconds>(UINT32_MAX-47u)/(2u*rate))return TIKU_MFS_ARG;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32||*p==127)return TIKU_MFS_ARG;
    tiku_media_store_t *store=tiku_media_card_store();if(!store)return TIKU_MFS_NOFS;
    if(tiku_p4_pdm_running()||store->writer||store->collector)return TIKU_MFS_BUSY;
    tiku_mfs_object_t info;tiku_media_request_t r={0};r.op=TIKU_MEDIA_STAT;r.name=name;r.info=&info;
    int rc=request(&r);if(!rc)return TIKU_MFS_BUSY;if(rc!=TIKU_MFS_NOENT)return rc;
    uint32_t samples=seconds*rate,bytes=samples*2u+44u,capacity=(bytes+3u)&~3u;
    tiku_arena_t arena={0};tiku_mem_request_t options={0};
    options.allocation_class=TIKU_MEM_TRANSIENT;
    if(tiku_tier_arena_create_opts(&arena,TIKU_MEM_PSRAM,capacity,93,&options))return TIKU_MFS_SPACE;
    uint8_t *wave=tiku_arena_alloc(&arena,capacity);
    if(!wave){(void)tiku_arena_destroy(&arena);return TIKU_MFS_SPACE;}
    rc=tiku_p4_pdm_start(rate,TIKU_P4_PDM_SLOT_LEFT);
    if(rc){rc=CAPTURE_ERROR;goto free_buffer;}
    /* Consume the filter's startup interval before retaining the recording. */
    for(unsigned i=0;i<8&&!rc;i++)rc=capture((int16_t *)(wave+44),512,NULL);
    uint64_t start=0,end=0;
    if(!rc&&tiku_p4_systimer_read(&start))rc=CAPTURE_ERROR;
    if(!rc)rc=capture((int16_t *)(wave+44),samples,result);
    if(tiku_p4_systimer_read(&end)&&!rc)rc=CAPTURE_ERROR;
    tiku_p4_pdm_stats_t stats;tiku_p4_pdm_stats(&stats);tiku_p4_pdm_stop();
    result->overruns=stats.overruns;result->faults=stats.faults;
    if(end>=start)result->elapsed_ms=(uint32_t)((end-start)/16000u);
    if(!rc&&(stats.overruns||stats.faults))rc=CAPTURE_LOSS;
    if(rc)goto free_buffer;
    memset(wave,0,44);memcpy(wave,"RIFF",4);put32(wave+4,bytes-8);
    memcpy(wave+8,"WAVEfmt ",8);put32(wave+16,16);wave[20]=1;wave[22]=1;
    put32(wave+24,rate);put32(wave+28,rate*2);wave[32]=2;wave[34]=16;
    memcpy(wave+36,"data",4);put32(wave+40,bytes-44);
    r.op=TIKU_MEDIA_BEGIN;r.policy=TIKU_MFS_STREAM;r.info=NULL;
    rc=request(&r);if(rc)goto free_buffer;
    result->id=r.id;
    for(uint32_t at=0;at<bytes;){
        uint32_t n=bytes-at;if(n>TIKU_MFS_CHUNK)n=TIKU_MFS_CHUNK;
        tiku_watchdog_kick();r.op=TIKU_MEDIA_APPEND;r.buffer=wave+at;r.length=n;
        rc=request(&r);if(rc)break;
        r.op=at+n==bytes?TIKU_MEDIA_FINALIZE:TIKU_MEDIA_SYNC;
        rc=request(&r);if(rc)break;
        at+=n;result->bytes=at;
    }
    if(rc){r.op=TIKU_MEDIA_RELEASE;(void)request(&r);}
free_buffer:
    (void)tiku_arena_reset(&arena);(void)tiku_arena_destroy(&arena);return rc;
}
