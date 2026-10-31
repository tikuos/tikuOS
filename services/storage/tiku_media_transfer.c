/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_transfer.c - Atomic uploads, snapshot downloads and duplicate suppression.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer.h"
#include <string.h>
/** @brief Decode an unaligned little-endian field. */
static uint32_t u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/** @brief Decode an unaligned 64-bit field. */
static uint64_t u64(const uint8_t *p){return u32(p)|((uint64_t)u32(p+4)<<32);}
/** @brief Encode an unaligned field. */
static void p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
/** @brief Encode an unaligned 64-bit field. */
static void p64(uint8_t *p,uint64_t n){p32(p,(uint32_t)n);p32(p+4,(uint32_t)(n>>32));}
/** @brief Extend CRC32C over bytes with an inverted accumulator. */
static uint32_t checksum(uint32_t c,const uint8_t *p,size_t n)
{while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0x82f63b78u&(0u-(c&1)));}return c;}
/** @brief Execute a request through the service without consuming another caller's queue. */
static int request(tiku_media_transfer_t *s,tiku_media_request_t *r)
{
    if(!s->store||!s->store->fs->mounted)return TIKU_MFS_NOFS;
    if(s->store->count||s->store->running)return TIKU_MFS_BUSY;
    r->client=s->client;r->permissions=7;
    int rc=tiku_media_store_submit(s->store,r);if(rc)return rc;
    rc=tiku_media_store_step(s->store);
    if(rc==TIKU_MFS_BUSY){s->store->count=0;return rc;}
    return r->result;
}
void tiku_media_transfer_init(tiku_media_transfer_t *s,tiku_media_store_t *store,uint32_t client,const uint8_t *key)
{memset(s,0,sizeof *s);s->store=store;s->client=client;if(key)memcpy(s->key,key,32);}
int tiku_media_transfer_close(tiku_media_transfer_t *s)
{
    int rc=0;tiku_media_request_t r={0};
    if(s->upload){r.op=TIKU_MEDIA_RELEASE;rc=request(s,&r);if(!rc)s->upload=s->staged=0;}
    if(s->reader){r.op=TIKU_MEDIA_CLOSE;r.id=s->reader;int e=request(s,&r);if(!e)s->reader=0;if(!rc)rc=e;}
    s->authenticated=s->listing=0;return rc;
}
/** @brief Decode a bounded name without embedded terminators. */
static int name(char out[TIKU_MFS_NAME],const uint8_t *p,size_t n,int empty)
{if(n>=TIKU_MFS_NAME||(!n&&!empty)||memchr(p,0,n))return TIKU_MFS_ARG;memcpy(out,p,n);out[n]=0;return 0;}
/** @brief Serialize object identity, length, digest, completion and name. */
static unsigned metadata(uint8_t *out,const tiku_mfs_object_t *o)
{
    size_t n=strlen(o->name);p64(out,o->id);p64(out+8,o->length);
    memcpy(out+16,o->digest,32);out[48]=o->complete;memcpy(out+49,o->name,n);return (unsigned)(49+n);
}
/** @brief Commit a complete staged chunk and report its resumable boundary. */
static int flush(tiku_media_transfer_t *s,int final)
{
    tiku_media_request_t r={0};int rc;
    if(s->staged){r.op=TIKU_MEDIA_APPEND;r.buffer=s->stage;r.length=s->staged;if((rc=request(s,&r))){s->fault=1;return rc;}}
    r.op=final?TIKU_MEDIA_FINALIZE:TIKU_MEDIA_SYNC;
    if((rc=request(s,&r))){s->fault=1;return rc;}
    s->committed=s->accepted;s->staged=0;if(final)s->upload=0;return 0;
}
/** @brief Dispatch one validated operation into the mounted media service. */
static int execute(tiku_media_transfer_t *s,unsigned op,unsigned flags,uint64_t arg,
                   const uint8_t *in,unsigned n,uint64_t *value,uint8_t *out,unsigned *size)
{
    tiku_media_request_t r={0};tiku_mfs_object_t o;char path[TIKU_MFS_NAME];int rc;
    r.info=&o;
    if(flags&~(op==TIKU_MT_BEGIN?1u:0u))return TIKU_MFS_ARG;
    if(op==TIKU_MT_HELLO){
        uint8_t diff=0;for(unsigned i=0;i<32;i++)diff|=s->key[i];
        if(diff){if(n!=32)return TIKU_MFS_ARG;diff=0;for(unsigned i=0;i<32;i++)diff|=s->key[i]^in[i];}
        else if(n)return TIKU_MFS_ARG;
        if(diff)return TIKU_MFS_ARG;
        if((rc=tiku_media_transfer_close(s)))return rc;
        s->authenticated=1;*value=TIKU_MEDIA_TRANSFER_CHUNK;
        p32(out,TIKU_MFS_CHUNK);memcpy(out+4,s->store->uuid,16);*size=20;return 0;
    }
    if(!s->authenticated)return TIKU_MFS_ARG;
    switch(op){
    case TIKU_MT_BEGIN:
        if(s->upload||n<33||(flags&~1u))return TIKU_MFS_ARG;
        if((rc=name(path,in+32,n-32,0)))return rc;
        if(!(flags&1u)){r.op=TIKU_MEDIA_STAT;r.name=path;rc=request(s,&r);
            if(!rc)return TIKU_MFS_BUSY;
            if(rc!=TIKU_MFS_NOENT)return rc;}
        r.op=TIKU_MEDIA_BEGIN;r.name=path;r.policy=TIKU_MFS_ATOMIC;r.offset=arg;memcpy(r.digest,in,32);
        if((rc=request(s,&r)))return rc;
        s->upload=r.id;s->expected=arg;s->fault=0;s->staged=0;s->accepted=s->committed=0;*value=r.id;return 0;
    case TIKU_MT_RESUME:
        if(s->upload||n<41)return TIKU_MFS_ARG;
        if((rc=name(path,in+40,n-40,0)))return rc;
        r.op=TIKU_MEDIA_UPLOADS;
        do{rc=request(s,&r);}while(!rc&&o.id!=arg);
        if(rc)return rc;
        if(strcmp(path,o.name)||o.expected!=u64(in)||memcmp(o.expected_digest,in+8,32))return TIKU_MFS_STALE;
        r.op=TIKU_MEDIA_RESUME;r.id=arg;if((rc=request(s,&r)))return rc;
        s->upload=arg;s->expected=o.expected;s->fault=0;s->staged=0;s->accepted=s->committed=o.length;*value=o.length;return 0;
    case TIKU_MT_DATA:
        if(!s->upload||s->fault||!n||arg!=s->accepted||n>TIKU_MFS_CHUNK-s->staged||arg>s->expected||n>s->expected-arg)return TIKU_MFS_STALE;
        memcpy(s->stage+s->staged,in,n);s->staged+=n;s->accepted+=n;
        if(s->staged==TIKU_MFS_CHUNK&&(rc=flush(s,0)))return rc;
        *value=s->accepted;p64(out,s->committed);*size=8;return 0;
    case TIKU_MT_FINISH:
        if(!s->upload||s->fault||n||arg!=s->accepted||arg!=s->expected)return TIKU_MFS_STALE;
        if((rc=flush(s,1)))return rc;
        *value=s->accepted;return 0;
    case TIKU_MT_RELEASE:
        if(n)return TIKU_MFS_ARG;
        r.op=TIKU_MEDIA_RELEASE;rc=s->upload?request(s,&r):0;
        if(!rc)s->upload=s->staged=0;
        return rc;
    case TIKU_MT_OPEN:
        if(s->reader)return TIKU_MFS_BUSY;
        if((rc=name(path,in,n,0)))return rc;
        r.op=TIKU_MEDIA_STAT;r.name=path;if((rc=request(s,&r)))return rc;
        if(!o.complete)return TIKU_MFS_BUSY;
        r.op=TIKU_MEDIA_OPEN;if((rc=request(s,&r)))return rc;
        s->reader=r.id;*size=metadata(out,&o);return 0;
    case TIKU_MT_READ:
        if(!s->reader||n!=4||u32(in)>TIKU_MEDIA_TRANSFER_CHUNK)return TIKU_MFS_ARG;
        r.op=TIKU_MEDIA_READ_AT;r.id=s->reader;r.offset=arg;r.length=u32(in);r.buffer=out;
        rc=request(s,&r);if(rc<0)return rc;*size=(unsigned)rc;*value=arg;return 0;
    case TIKU_MT_CLOSE:
        if(n)return TIKU_MFS_ARG;
        r.op=TIKU_MEDIA_CLOSE;r.id=s->reader;rc=s->reader?request(s,&r):0;
        if(!rc)s->reader=0;
        return rc;
    case TIKU_MT_LIST:
        if((rc=name(s->prefix,in,n,1)))return rc;
        r.op=TIKU_MEDIA_SEEK;r.name=s->prefix;
        if((rc=request(s,&r)))return rc;
        s->cursor=r.cursor;s->listing=1;return 0;
    case TIKU_MT_NEXT:
        if(!s->listing||n)return TIKU_MFS_ARG;
        r.op=TIKU_MEDIA_LIST;r.cursor=s->cursor;if((rc=request(s,&r)))return rc;
        s->cursor=r.cursor;if(strncmp(o.name,s->prefix,strlen(s->prefix)))return TIKU_MFS_NOENT;
        *size=metadata(out,&o);return 0;
    default:return TIKU_MFS_ARG;
    }
}
size_t tiku_media_transfer_handle(tiku_media_transfer_t *s,const void *data,size_t length)
{
    const uint8_t *p=data;
    if(!s||!data||length<32||length>TIKU_MEDIA_TRANSFER_PACKET||memcmp(p,"TMT1",4)||
       p[6]||p[7]||u32(p+12)||u32(p+24)!=length-32||
       ~checksum(checksum(UINT32_MAX,p,28),p+32,length-32)!=u32(p+28))return 0;
    uint32_t seq=u32(p+8);if(!seq)return 0;
    if(seq==s->last_sequence&&s->last_size){
        if(length==s->last_size&&!memcmp(p,s->last,length))return s->reply_size;
        if(p[4]!=TIKU_MT_HELLO)return 0;
    }
    if(p[4]!=TIKU_MT_HELLO&&(!s->last_size||s->last_sequence==UINT32_MAX||seq!=s->last_sequence+1))return 0;
    if(!s->store||!s->store->fs->mounted)return 0;
    uint64_t value=0;unsigned size=0;
    int rc=execute(s,p[4],p[5],u64(p+16),p+32,(unsigned)(length-32),&value,s->reply+32,&size);
    if(rc){size=0;value=0;}
    memcpy(s->last,p,length);s->last_size=(uint32_t)length;s->last_sequence=seq;
    memcpy(s->reply,"TMT1",4);s->reply[4]=p[4];s->reply[5]=0x80;s->reply[6]=s->reply[7]=0;
    p32(s->reply+8,seq);p32(s->reply+12,(uint32_t)rc);p64(s->reply+16,value);p32(s->reply+24,size);
    p32(s->reply+28,~checksum(checksum(UINT32_MAX,s->reply,28),s->reply+32,size));
    s->reply_size=32+size;return s->reply_size;
}
