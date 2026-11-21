/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_async.c - incremental TMT1 framing and queued file transfers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_async.h"
#include <stddef.h>
#include <string.h>
#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Protocol phases and queued-operation continuations. */
enum {
    MT_CHECK=1,MT_CRC,MT_DUPLICATE,MT_EXECUTE,MT_SUBMIT,MT_WAIT,MT_CACHE,MT_REPLY_HEADER,MT_REPLY_CRC,
    MT_HELLO_RELEASE,MT_HELLO_CLOSE,MT_HELLO_DONE,MT_BEGIN_STAT,MT_BEGIN_DONE,
    MT_RESUME_DONE,MT_RESUME_RELEASE,MT_DATA_COPY,MT_APPEND_DONE,MT_FLUSH_DONE,
    MT_OPEN_STAT,MT_OPEN_DONE,MT_READ_DONE,MT_READ_COPY,MT_RELEASE_DONE,MT_CLOSE_DONE,
    MT_LIST_DONE,MT_NEXT_DONE
};
/** @brief One borrowed packet and one retained media request. */
struct tiku_media_transfer_async {
    tiku_media_transfer_t *session;
    const uint8_t *input;
    tiku_media_request_t request;
    tiku_mfs_object_t object;
    uint64_t generation,argument,value,list_epoch;
    size_t length,at;
    uint32_t checksum,sequence,output_size,copy_size,copy_at;
    unsigned phase,after,operation,flags,exclusive,queued;
    int result,completion;
    char path[64],lower[64];
};
/** @brief Decode an unaligned little-endian field. */
static uint32_t mt_u32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
/** @brief Decode an unaligned double word. */
static uint64_t mt_u64(const uint8_t *p){return mt_u32(p)|((uint64_t)mt_u32(p+4)<<32);}
/** @brief Encode a little-endian field. */
static void mt_p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
/** @brief Encode a little-endian double word. */
static void mt_p64(uint8_t *p,uint64_t n){mt_p32(p,(uint32_t)n);mt_p32(p+4,(uint32_t)(n>>32));}
/** @brief Advance a reflected CRC32C accumulator. */
static uint32_t mt_crc(uint32_t c,const uint8_t *p,size_t n)
{ while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0x82f63b78u&(0u-(c&1u)));}return c; }
size_t tiku_media_transfer_async_size(void){return sizeof(tiku_media_transfer_async_t);}
size_t tiku_media_transfer_async_align(void){return _Alignof(tiku_media_transfer_async_t);}
tiku_media_transfer_async_t *tiku_media_transfer_async_init(void *memory,size_t size,tiku_media_transfer_t *s,
                                                          tiku_media_store_t *store,uint32_t client,const uint8_t *key)
{
    if(!memory||size<sizeof(tiku_media_transfer_async_t)||(uintptr_t)memory%_Alignof(tiku_media_transfer_async_t)||
       !s||!store||!store->fs||!store->fs->mounted||!client||store->writer==client)return NULL;
    for(unsigned i=0;i<TIKU_MFS_READERS;++i)if(store->readers[i].token&&store->readers[i].client==client)return NULL;
    tiku_media_transfer_async_t *a=memory;memset(a,0,sizeof *a);memset(s,0,offsetof(tiku_media_transfer_t,stage));
    s->store=store;s->client=client;if(key)memcpy(s->key,key,32);
    a->session=s;a->generation=store->fs->generation;return a;
}
int tiku_media_transfer_async_submit(tiku_media_transfer_async_t *a,const void *packet,size_t size)
{
    if(!a||!packet||size<32||size>TIKU_MEDIA_TRANSFER_PACKET)return TIKU_MFS_ARG;
    if(a->phase)return TIKU_MFS_BUSY;
    a->input=packet;a->length=size;a->phase=MT_CHECK;a->completion=0;a->queued=0;
    a->at=a->output_size=a->copy_at=a->copy_size=0;a->result=0;a->value=0;return 0;
}
/** @brief Retire an invalid frame without changing cached retry state. */
static int mt_drop(tiku_media_transfer_async_t *a)
{ a->input=NULL;a->phase=0;a->completion=0;return 0; }
/** @brief Begin cached response construction after an operation result. */
static int mt_reply(tiku_media_transfer_async_t *a,int result)
{
    a->result=result;if(result){a->output_size=0;a->value=0;}
    a->at=0;a->phase=MT_CACHE;return 1;
}
/** @brief Prepare a retained request with the authenticated client's data permissions. */
static void mt_queue(tiku_media_transfer_async_t *a,tiku_media_op_t operation,unsigned after)
{
    a->request=(tiku_media_request_t){.op=operation,.client=a->session->client,
        .permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE,.info=&a->object};
    a->after=after;a->phase=MT_SUBMIT;
}
/** @brief Decode a bounded wire name without embedded terminators. */
static int mt_name(char out[64],const uint8_t *p,size_t size,int empty)
{
    if(size>=64||(!size&&!empty)||memchr(p,0,size))return TIKU_MFS_ARG;
    memcpy(out,p,size);out[size]=0;return 0;
}
/** @brief Serialize one checked object's public wire metadata. */
static void mt_metadata(tiku_media_transfer_async_t *a)
{
    uint8_t *out=a->session->reply+32;const tiku_mfs_object_t *o=&a->object;size_t n=strlen(o->name);
    mt_p64(out,o->id);mt_p64(out+8,o->length);memcpy(out+16,o->digest,32);out[48]=o->complete;
    memcpy(out+49,o->name,n);a->output_size=(uint32_t)(49+n);
}
/** @brief Queue staged bytes or the next durable publication phase. */
static void mt_flush(tiku_media_transfer_async_t *a)
{
    tiku_media_transfer_t *s=a->session;
    if(s->staged){mt_queue(a,TIKU_MEDIA_APPEND,MT_APPEND_DONE);a->request.buffer=s->stage;a->request.length=s->staged;}
    else mt_queue(a,a->operation==TIKU_MT_FINISH?TIKU_MEDIA_FINALIZE:TIKU_MEDIA_SYNC,MT_FLUSH_DONE);
}
/** @brief Authenticate and dispatch one checked packet without invoking storage callbacks. */
static int mt_execute(tiku_media_transfer_async_t *a)
{
    tiku_media_transfer_t *s=a->session;const uint8_t *in=a->input+32;size_t n=a->length-32;int rc;
    if(a->flags&~(a->operation==TIKU_MT_BEGIN?1u:0u))return mt_reply(a,TIKU_MFS_ARG);
    if(a->operation==TIKU_MT_HELLO){
        unsigned key=0,diff=0;for(unsigned i=0;i<32;++i)key|=s->key[i];
        if(key){if(n!=32)return mt_reply(a,TIKU_MFS_ARG);for(unsigned i=0;i<32;++i)diff|=s->key[i]^in[i];}
        else if(n)return mt_reply(a,TIKU_MFS_ARG);
        if(diff)return mt_reply(a,TIKU_MFS_ARG);
        a->phase=MT_HELLO_RELEASE;return 1;
    }
    if(!s->authenticated)return mt_reply(a,TIKU_MFS_ARG);
    switch(a->operation){
    case TIKU_MT_BEGIN:
        if(s->upload||s->store->writer==s->client||n<33)return mt_reply(a,TIKU_MFS_ARG);
        rc=mt_name(a->path,in+32,n-32,0);if(rc)return mt_reply(a,rc);
        mt_queue(a,TIKU_MEDIA_ENUMERATE,MT_BEGIN_STAT);a->request.name=a->path;return 1;
    case TIKU_MT_RESUME:
        if(s->upload||s->store->writer==s->client||n<41)return mt_reply(a,TIKU_MFS_ARG);
        rc=mt_name(a->path,in+40,n-40,0);if(rc)return mt_reply(a,rc);
        mt_queue(a,TIKU_MEDIA_RESUME,MT_RESUME_DONE);a->request.id=a->argument;return 1;
    case TIKU_MT_DATA:
        if(!s->upload||s->fault||!n||a->argument!=s->accepted||n>TIKU_MFS_CHUNK-s->staged||
           a->argument>s->expected||n>s->expected-a->argument)return mt_reply(a,TIKU_MFS_STALE);
        a->copy_size=(uint32_t)n;a->copy_at=0;a->phase=MT_DATA_COPY;return 1;
    case TIKU_MT_FINISH:
        if(!s->upload||s->fault||n||a->argument!=s->accepted||a->argument!=s->expected)return mt_reply(a,TIKU_MFS_STALE);
        mt_flush(a);return 1;
    case TIKU_MT_RELEASE:
        if(n)return mt_reply(a,TIKU_MFS_ARG);
        if(s->store->writer!=s->client){s->upload=s->staged=0;return mt_reply(a,0);}
        mt_queue(a,TIKU_MEDIA_RELEASE,MT_RELEASE_DONE);return 1;
    case TIKU_MT_OPEN:
        if(s->reader)return mt_reply(a,TIKU_MFS_BUSY);
        rc=mt_name(a->path,in,n,0);if(rc)return mt_reply(a,rc);
        mt_queue(a,TIKU_MEDIA_ENUMERATE,MT_OPEN_STAT);a->request.name=a->path;return 1;
    case TIKU_MT_READ:{
        if(!s->reader||n!=4||mt_u32(in)>TIKU_MEDIA_TRANSFER_CHUNK)return mt_reply(a,TIKU_MFS_ARG);
        uint32_t take=mt_u32(in);if(a->argument>=s->read_length)take=0;
        else if(take>s->read_length-a->argument)take=(uint32_t)(s->read_length-a->argument);
        a->copy_size=take;a->copy_at=0;
        if(!take){a->value=a->argument;return mt_reply(a,0);}
        if(!s->upload&&s->read_size&&a->argument>=s->read_offset&&a->argument-s->read_offset<=s->read_size&&
           take<=s->read_size-(a->argument-s->read_offset)){a->phase=MT_READ_COPY;return 1;}
        mt_queue(a,TIKU_MEDIA_READ_AT,MT_READ_DONE);a->request.id=s->reader;a->request.offset=a->argument;
        a->request.buffer=s->upload?s->reply+32:s->stage;a->request.length=s->upload?take:TIKU_MFS_CHUNK;
        if(a->request.length>s->read_length-a->argument)a->request.length=(uint32_t)(s->read_length-a->argument);
        if(!s->upload)s->read_size=0;
        return 1;
    }
    case TIKU_MT_CLOSE:
        if(n)return mt_reply(a,TIKU_MFS_ARG);
        if(!s->reader){s->read_size=0;return mt_reply(a,0);}
        mt_queue(a,TIKU_MEDIA_CLOSE,MT_CLOSE_DONE);a->request.id=s->reader;return 1;
    case TIKU_MT_LIST:
        rc=mt_name(a->path,in,n,1);if(rc)return mt_reply(a,rc);
        mt_queue(a,TIKU_MEDIA_ENUMERATE,MT_LIST_DONE);a->request.name=a->path;return 1;
    case TIKU_MT_NEXT:
        if(!s->listing||n)return mt_reply(a,TIKU_MFS_ARG);
        mt_queue(a,TIKU_MEDIA_ENUMERATE,MT_NEXT_DONE);a->request.name=a->lower;
        a->request.names_epoch=a->list_epoch;a->request.exclusive=(uint8_t)a->exclusive;return 1;
    default:return mt_reply(a,TIKU_MFS_ARG);
    }
}
/** @brief Advance one checked operation after admission or media completion. */
static int mt_operation(tiku_media_transfer_async_t *a)
{
    tiku_media_transfer_t *s=a->session;int rc=a->request.result;const uint8_t *in=a->input+32;
    switch(a->phase){
    case MT_EXECUTE:return mt_execute(a);
    case MT_HELLO_RELEASE:
        if(s->store->writer==s->client){mt_queue(a,TIKU_MEDIA_RELEASE,MT_HELLO_CLOSE);return 1;}
        a->request.result=0;a->phase=MT_HELLO_CLOSE;return 1;
    case MT_HELLO_CLOSE:
        if(rc)return mt_reply(a,rc);
        s->upload=s->staged=0;
        for(unsigned i=0;i<TIKU_MFS_READERS;++i)if(s->store->readers[i].token&&s->store->readers[i].client==s->client){
            mt_queue(a,TIKU_MEDIA_CLOSE,MT_HELLO_CLOSE);a->request.id=s->store->readers[i].token;return 1;
        }
        a->phase=MT_HELLO_DONE;return 1;
    case MT_HELLO_DONE:
        s->reader=s->accepted=s->committed=s->expected=0;s->read_size=0;s->listing=s->fault=0;s->authenticated=1;
        a->value=TIKU_MEDIA_TRANSFER_CHUNK;mt_p32(s->reply+32,TIKU_MFS_CHUNK);memcpy(s->reply+36,s->store->uuid,16);
        a->output_size=20;return mt_reply(a,0);
    case MT_BEGIN_STAT:{
        if(rc&&rc!=TIKU_MFS_NOENT)return mt_reply(a,rc);
        uint64_t id=!rc&&!strcmp(a->object.name,a->path)?a->object.id:0,epoch=a->request.names_epoch;
        if(id&&(!(a->flags&1u)||!a->object.complete))return mt_reply(a,TIKU_MFS_BUSY);
        mt_queue(a,TIKU_MEDIA_BEGIN,MT_BEGIN_DONE);a->request.id=id;a->request.names_epoch=epoch;
        a->request.name=a->path;a->request.policy=TIKU_MFS_ATOMIC;a->request.offset=a->argument;
        memcpy(a->request.digest,in,32);return 1;
    }
    case MT_BEGIN_DONE:
        if(rc)return mt_reply(a,rc);
        s->upload=a->request.id;s->expected=a->argument;s->accepted=s->committed=0;s->staged=s->read_size=0;s->fault=0;
        a->value=s->upload;return mt_reply(a,0);
    case MT_RESUME_DONE:
        if(rc)return mt_reply(a,rc);
        if(a->object.policy!=TIKU_MFS_ATOMIC||strcmp(a->path,a->object.name)||a->object.expected!=mt_u64(in)||
           memcmp(a->object.expected_digest,in+8,32)){
            mt_queue(a,TIKU_MEDIA_RELEASE,MT_RESUME_RELEASE);return 1;
        }
        s->upload=a->request.id;s->expected=a->object.expected;s->accepted=s->committed=a->object.length;
        s->staged=s->read_size=0;s->fault=0;a->value=s->accepted;return mt_reply(a,0);
    case MT_RESUME_RELEASE:return mt_reply(a,rc?rc:TIKU_MFS_STALE);
    case MT_DATA_COPY:{
        unsigned n=a->copy_size-a->copy_at;if(n>512)n=512;
        memcpy(s->stage+s->staged+a->copy_at,in+a->copy_at,n);a->copy_at+=n;
        if(a->copy_at<a->copy_size)return 1;
        s->staged+=a->copy_size;s->accepted+=a->copy_size;
        if(s->staged==TIKU_MFS_CHUNK){mt_flush(a);return 1;}
        a->value=s->accepted;mt_p64(s->reply+32,s->committed);a->output_size=8;return mt_reply(a,0);
    }
    case MT_APPEND_DONE:
        if(rc){s->fault=1;return mt_reply(a,rc);}
        mt_queue(a,a->operation==TIKU_MT_FINISH?TIKU_MEDIA_FINALIZE:TIKU_MEDIA_SYNC,MT_FLUSH_DONE);return 1;
    case MT_FLUSH_DONE:
        if(rc){s->fault=1;return mt_reply(a,rc);}
        s->committed=s->accepted;s->staged=0;a->value=s->accepted;
        if(a->operation==TIKU_MT_FINISH)s->upload=0;
        else {mt_p64(s->reply+32,s->committed);a->output_size=8;}
        return mt_reply(a,0);
    case MT_OPEN_STAT:{
        if(rc)return mt_reply(a,rc);
        if(strcmp(a->object.name,a->path))return mt_reply(a,TIKU_MFS_NOENT);
        if(!a->object.complete)return mt_reply(a,TIKU_MFS_BUSY);
        uint64_t rank=a->request.rank,epoch=a->request.names_epoch;
        mt_queue(a,TIKU_MEDIA_OPEN_VERSION,MT_OPEN_DONE);a->request.name=a->path;
        a->request.rank=rank;a->request.names_epoch=epoch;return 1;
    }
    case MT_OPEN_DONE:
        if(rc)return mt_reply(a,rc);
        s->reader=a->request.id;s->read_length=a->object.length;s->read_size=0;mt_metadata(a);return mt_reply(a,0);
    case MT_READ_DONE:
        if(rc<0)return mt_reply(a,rc);
        if((unsigned)rc<a->copy_size)return mt_reply(a,TIKU_MFS_IO);
        if(s->upload){a->output_size=a->copy_size;a->value=a->argument;return mt_reply(a,0);}
        s->read_offset=a->argument;s->read_size=(uint32_t)rc;a->phase=MT_READ_COPY;return 1;
    case MT_READ_COPY:{
        unsigned n=a->copy_size-a->copy_at;if(n>512)n=512;
        memcpy(s->reply+32+a->copy_at,s->stage+(size_t)(a->argument-s->read_offset)+a->copy_at,n);a->copy_at+=n;
        if(a->copy_at<a->copy_size)return 1;
        a->output_size=a->copy_size;a->value=a->argument;return mt_reply(a,0);
    }
    case MT_RELEASE_DONE:if(!rc)s->upload=s->staged=0;return mt_reply(a,rc);
    case MT_CLOSE_DONE:if(!rc){s->reader=0;s->read_size=0;}return mt_reply(a,rc);
    case MT_LIST_DONE:
        if(rc&&rc!=TIKU_MFS_NOENT)return mt_reply(a,rc);
        memcpy(s->prefix,a->path,64);memcpy(a->lower,a->path,64);a->exclusive=0;
        a->list_epoch=a->request.names_epoch;s->listing=1;return mt_reply(a,0);
    case MT_NEXT_DONE:
        if(rc)return mt_reply(a,rc);
        if(strncmp(a->object.name,s->prefix,strlen(s->prefix)))return mt_reply(a,TIKU_MFS_NOENT);
        memcpy(a->lower,a->object.name,64);a->exclusive=1;mt_metadata(a);return mt_reply(a,0);
    default:return mt_reply(a,TIKU_MFS_ARG);
    }
}
int tiku_media_transfer_async_step(tiku_media_transfer_async_t *a)
{
    if(!a)return TIKU_MFS_ARG;
    if(!a->phase)return 0;
    tiku_media_transfer_t *s=a->session;const uint8_t *p=a->input;
    switch(a->phase){
    case MT_CHECK:
        if(!s->store->fs->mounted||s->store->fs->generation!=a->generation){s->authenticated=0;return mt_drop(a);}
        if(memcmp(p,"TMT1",4)||p[6]||p[7]||mt_u32(p+12)||mt_u32(p+24)!=a->length-32||!mt_u32(p+8))return mt_drop(a);
        a->operation=p[4];a->flags=p[5];a->sequence=mt_u32(p+8);a->argument=mt_u64(p+16);
        a->checksum=mt_crc(UINT32_MAX,p,28);a->at=32;a->phase=MT_CRC;return 1;
    case MT_CRC:{
        size_t n=a->length-a->at;if(n>64)n=64;
        a->checksum=mt_crc(a->checksum,p+a->at,n);a->at+=n;if(a->at<a->length)return 1;
        if(~a->checksum!=mt_u32(p+28))return mt_drop(a);
        if(a->sequence==s->last_sequence&&s->last_size==a->length){a->at=0;a->phase=MT_DUPLICATE;return 1;}
        if(a->operation!=TIKU_MT_HELLO&&(!s->last_size||s->last_sequence==UINT32_MAX||a->sequence!=s->last_sequence+1))return mt_drop(a);
        a->phase=MT_EXECUTE;return 1;
    }
    case MT_DUPLICATE:{
        size_t n=a->length-a->at;if(n>64)n=64;
        if(memcmp(p+a->at,s->last+a->at,n)){
            if(a->operation!=TIKU_MT_HELLO)return mt_drop(a);
            a->phase=MT_EXECUTE;return 1;
        }
        a->at+=n;if(a->at<a->length)return 1;
        a->completion=(int)s->reply_size;a->phase=0;a->input=NULL;return 0;
    }
    case MT_SUBMIT:{
        if(!s->store->fs->mounted||s->store->fs->generation!=a->generation)return mt_reply(a,TIKU_MFS_STALE);
        int rc=tiku_media_store_submit(s->store,&a->request);
        if(rc==TIKU_MFS_BUSY)return 1;
        if(rc){if(a->operation==TIKU_MT_DATA||a->operation==TIKU_MT_FINISH)s->fault=1;return mt_reply(a,rc);}
        a->queued=1;a->phase=MT_WAIT;return 1;
    }
    case MT_WAIT:
        if(!a->request.done)return 1;
        a->queued=0;a->phase=a->after;return 1;
    case MT_CACHE:{
        size_t n=a->length-a->at;if(n>512)n=512;
        memcpy(s->last+a->at,p+a->at,n);a->at+=n;if(a->at<a->length)return 1;
        a->phase=MT_REPLY_HEADER;return 1;
    }
    case MT_REPLY_HEADER:
        memcpy(s->reply,"TMT1",4);s->reply[4]=(uint8_t)a->operation;s->reply[5]=0x80;s->reply[6]=s->reply[7]=0;
        mt_p32(s->reply+8,a->sequence);mt_p32(s->reply+12,(uint32_t)a->result);mt_p64(s->reply+16,a->value);
        mt_p32(s->reply+24,a->output_size);a->checksum=mt_crc(UINT32_MAX,s->reply,28);a->at=0;a->phase=MT_REPLY_CRC;return 1;
    case MT_REPLY_CRC:{
        size_t n=a->output_size-a->at;if(n>64)n=64;
        a->checksum=mt_crc(a->checksum,s->reply+32+a->at,n);a->at+=n;if(a->at<a->output_size)return 1;
        mt_p32(s->reply+28,~a->checksum);s->last_size=(uint32_t)a->length;s->last_sequence=a->sequence;
        s->reply_size=32+a->output_size;a->completion=(int)s->reply_size;a->phase=0;a->input=NULL;return 0;
    }
    default:
        if(!s->store->fs->mounted||s->store->fs->generation!=a->generation)return mt_reply(a,TIKU_MFS_STALE);
        return mt_operation(a);
    }
}
int tiku_media_transfer_async_result(const tiku_media_transfer_async_t *a,const uint8_t **reply)
{
    if(!a||!reply)return TIKU_MFS_ARG;
    if(a->phase)return TIKU_MFS_BUSY;
    *reply=a->session->reply;return a->completion;
}
tiku_media_request_t *tiku_media_transfer_async_request(tiku_media_transfer_async_t *a)
{ return a&&a->queued&&!a->request.done?&a->request:NULL; }
int tiku_media_transfer_async_cancel(tiku_media_transfer_async_t *a)
{
    if(!a)return TIKU_MFS_ARG;
    if(a->queued&&!a->request.done)return TIKU_MFS_BUSY;
    a->phase=a->queued=0;a->input=NULL;a->completion=0;
    a->session->authenticated=0;a->session->fault=1;a->session->last_size=a->session->reply_size=0;return 0;
}
#endif
