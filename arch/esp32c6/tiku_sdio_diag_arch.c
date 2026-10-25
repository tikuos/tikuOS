/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_diag_arch.c - Whole-message C6 link over SDIO shared registers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_sdio_diag.h>
#define C6_REQUEST_BYTES 20u
#define C6_RESPONSE_MAX 24u
#define C6_REQUEST_MAX 12u
#define C6_READY_POS 20u
#define C6_FLASH_MAGIC TIKU_SDIO_NATIVE_MAGIC
#define c6_shared_address tiku_sdio_diag_address
#define c6_mail_seal(p,n) tiku_sdio_diag_seal(p,(n)+8u,n)
#define c6_mail_valid(p,n) tiku_sdio_diag_valid(p,(n)+8u,n)
#include "tiku_native_arch.h"
#include "tiku_sdio_arch.h"
#include <services/link/tiku_link.h>
#include <string.h>
static uint8_t previous[C6_REQUEST_BYTES],response[32];
static uint32_t session,bad,duplicates,delivered;
static uint8_t last_seq;
#ifdef C6_FLASH_HOST
uint8_t c6_host_regs[64];
static uint8_t get(unsigned pos){return c6_host_regs[pos];}
static void put(unsigned pos,uint8_t v){c6_host_regs[pos]=v;}
#define BARRIER() __asm__ volatile("":::"memory")
#else
#define BARRIER() __asm__ volatile("fence iorw,iorw":::"memory")
static uint32_t output_shadow[16];
static uint8_t get(unsigned pos)
{
    unsigned a=c6_shared_address(pos);
    return (uint8_t)(C6_REG(0x60018000u+(a&~3u))>>((a&3u)*8));
}
static void put(unsigned pos,uint8_t v)
{
    unsigned a=c6_shared_address(pos),shift=(a&3u)*8;
    volatile uint32_t *r=(volatile uint32_t *)(uintptr_t)(0x60018000u+(a&~3u));
    uint32_t word;
    if(pos>=C6_READY_POS){
        unsigned index=pos/4;
        word=(output_shadow[index]&~(255u<<shift))|((uint32_t)v<<shift);
        output_shadow[index]=word;
    }else{word=(*r&~(255u<<shift))|((uint32_t)v<<shift);}
    *r=word;BARRIER();
}
#endif
static void publish(void)
{
    for(unsigned n=1;n<32;n++){put(32+n,response[n]);}
    BARRIER();put(32,response[0]);
}
static int send(tiku_link_t *link,const void *head,size_t hlen,const void *body,size_t blen)
{
    (void)link;
    if(hlen>C6_RESPONSE_MAX || blen>C6_RESPONSE_MAX-hlen){return -1;}
    memset(response,0,sizeof response);response[0]=last_seq;response[1]=(uint8_t)(hlen+blen);
    if(hlen){memcpy(response+4,head,hlen);}if(blen){memcpy(response+4+hlen,body,blen);}
    for(unsigned n=0;n<4;n++){response[28+n]=(uint8_t)(session>>(8*n));}
    c6_mail_seal(response,C6_RESPONSE_MAX);publish();return 0;
}
static void pump(tiku_link_t *link)
{
    uint8_t p[C6_REQUEST_BYTES];
    p[0]=get(0);if(!p[0]){return;}
    for(unsigned n=1;n<C6_REQUEST_BYTES;n++){p[n]=get(n);}
    BARRIER();if(p[0]!=get(0)){return;}
    if(!c6_mail_valid(p,C6_REQUEST_MAX)){bad++;put(0,0);return;}
    uint32_t sid=(uint32_t)p[4+C6_REQUEST_MAX]|((uint32_t)p[5+C6_REQUEST_MAX]<<8)|((uint32_t)p[6+C6_REQUEST_MAX]<<16)|((uint32_t)p[7+C6_REQUEST_MAX]<<24);
    if(sid==session && p[0]==last_seq){
        if(!memcmp(previous,p,C6_REQUEST_BYTES)){duplicates++;publish();}else{bad++;}
        put(0,0);return;
    }
    uint8_t expected=last_seq==255?1:(uint8_t)(last_seq+1);
    if(!sid || (sid!=session?p[0]!=1:p[0]!=expected)){bad++;put(0,0);return;}
    session=sid;last_seq=p[0];memcpy(previous,p,C6_REQUEST_BYTES);delivered++;
    put(0,0);tiku_link_deliver(link,p+4,p[1]);
}
static const tiku_link_ops_t ops={send,pump,NULL};
static tiku_link_t console={&ops,NULL,0,NULL,NULL};
tiku_link_t *tiku_c6_diag_link(void)
{
#ifndef C6_FLASH_HOST
    memset(output_shadow,0,sizeof output_shadow);
#endif
    for(unsigned n=0;n<64;n++){if(n<24 || n>=32){put(n,0);}}
    session=bad=duplicates=delivered=0;last_seq=0;
    for(unsigned n=0;n<4;n++){put(C6_READY_POS+n,(uint8_t)(C6_FLASH_MAGIC>>(8*n)));}
    return &console;
}
uint32_t tiku_c6_diag_link_bad(void){return bad;}
uint32_t tiku_c6_diag_link_duplicates(void){return duplicates;}
uint32_t tiku_c6_diag_link_delivered(void){return delivered;}
void tiku_c6_diag_trace(const uint32_t values[5])
{
    if(response[1]!=4||memcmp(response+4,"IPCE",4))return;
    for(unsigned n=0;n<20;n++){
        response[8+n]=(uint8_t)(values[n/4]>>(8u*(n%4)));
        put(40+n,response[8+n]);
    }
}
