/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_media.c - Native SD mounting and bounded object transfers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <shell/tiku_shell_config.h>
#if TIKU_SHELL_CMD_MEDIA
#include "tiku_shell_cmd_media.h"
#include <shell/tiku_shell_io.h>
#include <services/storage/tiku_media_card.h>
#include <string.h>
#define MEDIA_CLIENT 0x4d465301u
/** @brief Decode a decimal count without accepting overflow or trailing text. */
static int number(const char *s,uint64_t *out)
{
    if(!s||!*s)return -1;
    uint64_t n=0;
    while(*s){unsigned d=(unsigned)(*s++-'0');if(d>9||(n>1844674407370955161ull||(n==1844674407370955161ull&&d>5)))return -1;n=n*10+d;}
    *out=n;return 0;
}
/** @brief Render a 64-bit count through the shell's string formatter. */
static char *decimal(uint64_t n,char out[21])
{
    memset(out,'0',20);out[20]=0;
    for(unsigned bit=64;bit;bit--){
        unsigned carry=(unsigned)((n>>(bit-1))&1u);
        for(unsigned i=20;i;i--){unsigned d=2u*(unsigned)(out[i-1]-'0')+carry;
            carry=d>=10;out[i-1]=(char)('0'+d-10*carry);}
    }
    char *p=out;while(p<out+19&&*p=='0')p++;return p;
}
/** @brief Decode one hexadecimal nibble. */
static int nibble(char c)
{ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
/** @brief Decode bounded hex bytes, returning their length or an error. */
static int hex(const char *s,uint8_t *out,unsigned capacity)
{
    unsigned n=0;
    while(*s){if(!s[1]||n==capacity)return -1;int a=nibble(s[0]),b=nibble(s[1]);
        if(a<0||b<0)return -1;
        out[n++]=(uint8_t)(16*a+b);s+=2;}
    return (int)n;
}
/** @brief Print file metadata with its exact length and digest. */
static void info(const tiku_mfs_object_t *o)
{
    char a[21],b[21];SHELL_PRINTF("id=%s bytes=%s complete=%u name=%s sha256=",
        decimal(o->id,a),decimal(o->length,b),o->complete,o->name);
    for(unsigned i=0;i<32;i++)SHELL_PRINTF("%02x",o->digest[i]);
    SHELL_PRINTF("\n");
}
/** @brief Report native error codes and completion without retaining borrowed buffers. */
static void result(int rc){SHELL_PRINTF("media: result=%d\n",rc);}
void tiku_shell_cmd_media(uint8_t argc,const char *argv[])
{
    uint64_t value=0;int rc=TIKU_MFS_ARG;uint8_t bytes[128];
    if(argc>=2&&!strcmp(argv[1],"mount")&&argc<=3){
        value=4000000;if(argc==3&&number(argv[2],&value)){result(rc);return;}
        if(value>20000000){result(rc);return;}
        result(tiku_media_card_mount(NULL,NULL,(uint32_t)value));return;
    }
    if(argc>=2&&!strcmp(argv[1],"format")&&(argc==4||argc==5)){
        uint8_t cid_bytes[16],uuid[16];uint32_t cid[4];value=4000000;
        if(hex(argv[2],cid_bytes,16)!=16||hex(argv[3],uuid,16)!=16||
           (argc==5&&number(argv[4],&value))||value>20000000){result(rc);return;}
        for(unsigned i=0;i<4;i++)cid[3-i]=((uint32_t)cid_bytes[4*i]<<24)|
            ((uint32_t)cid_bytes[4*i+1]<<16)|((uint32_t)cid_bytes[4*i+2]<<8)|cid_bytes[4*i+3];
        result(tiku_media_card_mount(cid,uuid,(uint32_t)value));return;
    }
    if(argc<2){
        SHELL_PRINTF("media mount [hz] | format CID UUID [hz] | info | ls [prefix] | uploads\n");
        SHELL_PRINTF("media begin NAME [length SHA256] | append HEX | sync | finish | release | resume ID\n");
        SHELL_PRINTF("media stat NAME | read NAME offset bytes | rm NAME | abort ID | gc\n");return;
    }
    tiku_media_store_t *store=tiku_media_card_store();if(!store){result(TIKU_MFS_NOFS);return;}
    tiku_media_request_t r={0};r.client=MEDIA_CLIENT;r.permissions=7;
    tiku_mfs_object_t object;r.info=&object;
    if(!strcmp(argv[1],"info")&&argc==2){
        tiku_mfs_t *fs=store->fs;char a[21],b[21],c[21];
        SHELL_PRINTF("media: sectors=%s files=%s uploads=%s uuid=",decimal(fs->block.sectors,a),
            decimal(fs->names.count,b),decimal(fs->names.pending,c));
        for(unsigned i=0;i<16;i++)SHELL_PRINTF("%02x",fs->uuid[i]);
        SHELL_PRINTF(" failed=%u writer=%u\n",fs->failed,fs->writing);return;
    }
    if((!strcmp(argv[1],"ls")&&argc<=3)||(!strcmp(argv[1],"uploads")&&argc==2)){
        int pending=!strcmp(argv[1],"uploads");const char *prefix=argc==3?argv[2]:"";
        if(!pending){r.op=TIKU_MEDIA_SEEK;r.name=prefix;rc=tiku_media_card_request(&r);if(rc){result(rc);return;}}
        r.op=pending?TIKU_MEDIA_UPLOADS:TIKU_MEDIA_LIST;
        while(!(rc=tiku_media_card_request(&r))){if(strncmp(prefix,object.name,strlen(prefix)))break;info(&object);}
        result(rc==TIKU_MFS_NOENT?0:rc);return;
    }
    if(!strcmp(argv[1],"begin")&&(argc==3||argc==5)){
        r.op=TIKU_MEDIA_BEGIN;r.name=argv[2];r.policy=argc==3?TIKU_MFS_STREAM:TIKU_MFS_ATOMIC;
        if(argc==5&&(number(argv[3],&r.offset)||hex(argv[4],r.digest,32)!=32)){result(rc);return;}
    }else if(!strcmp(argv[1],"append")&&argc==3){
        int n=hex(argv[2],bytes,sizeof bytes);if(n<=0){result(rc);return;}
        r.op=TIKU_MEDIA_APPEND;r.buffer=bytes;r.length=(uint32_t)n;
    }else if(!strcmp(argv[1],"sync")&&argc==2)r.op=TIKU_MEDIA_SYNC;
    else if(!strcmp(argv[1],"finish")&&argc==2)r.op=TIKU_MEDIA_FINALIZE;
    else if(!strcmp(argv[1],"release")&&argc==2)r.op=TIKU_MEDIA_RELEASE;
    else if((!strcmp(argv[1],"resume")||!strcmp(argv[1],"abort"))&&argc==3){
        if(number(argv[2],&r.id)){result(rc);return;}
        r.op=!strcmp(argv[1],"resume")?TIKU_MEDIA_RESUME:TIKU_MEDIA_DELETE;
    }else if(!strcmp(argv[1],"stat")&&argc==3){
        r.op=TIKU_MEDIA_STAT;r.name=argv[2];rc=tiku_media_card_request(&r);if(!rc)info(&object);result(rc);return;
    }else if(!strcmp(argv[1],"rm")&&argc==3){
        r.op=TIKU_MEDIA_STAT;r.name=argv[2];rc=tiku_media_card_request(&r);if(rc){result(rc);return;}
        r.op=TIKU_MEDIA_DELETE;r.id=object.id;
    }else if(!strcmp(argv[1],"read")&&argc==5){
        if(number(argv[3],&r.offset)||number(argv[4],&value)||value>sizeof bytes){result(rc);return;}
        r.op=TIKU_MEDIA_OPEN;r.name=argv[2];rc=tiku_media_card_request(&r);if(rc){result(rc);return;}
        r.op=TIKU_MEDIA_READ_AT;r.buffer=bytes;r.length=(uint32_t)value;
        int got=tiku_media_card_request(&r);r.op=TIKU_MEDIA_CLOSE;rc=tiku_media_card_request(&r);
        if(got<0)rc=got;
        if(!rc){SHELL_PRINTF("data=");for(int i=0;i<got;i++)SHELL_PRINTF("%02x",bytes[i]);SHELL_PRINTF("\n");}
        result(rc);return;
    }else if(!strcmp(argv[1],"gc")&&argc==2){
        r.op=TIKU_MEDIA_COLLECT_BEGIN;rc=tiku_media_card_request(&r);
        if(!rc){r.op=TIKU_MEDIA_COLLECT_STEP;r.length=32;do{rc=tiku_media_card_request(&r);}while(rc==1);}
        result(rc);return;
    }else{result(rc);return;}
    rc=tiku_media_card_request(&r);
    if(!rc&&r.op==TIKU_MEDIA_BEGIN){char a[21];SHELL_PRINTF("id=%s\n",decimal(r.id,a));}
    result(rc);
}
#endif
