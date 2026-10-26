/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_owner_arch.c - one SDMMC owner for native and resident C6 protocols.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sdio_owner_arch.h"
#include "tiku_sdmmc_arch.h"
#include <hal/tiku_sdio_diag.h>
#include <hal/tiku_c6_loader_hal.h>
#include <hal/tiku_ipc_epoch_hal.h>
#include <string.h>

/** @brief Read up to four card bytes without dispatching processes. */
static tiku_ipc_err_t read_bytes(uint32_t address,uint8_t *p,unsigned length)
{
    for(unsigned n=0;n<length;n++){
        tiku_ipc_err_t r=tiku_p4_sdmmc_byte(0,1,address+n,p+n);if(r){return r;}
    }
    return TIKU_IPC_OK;
}
/** @brief Accept a cumulative counter after two matching masked CMD52 samples. */
static tiku_ipc_err_t read_counter(uint32_t address,uint32_t mask,uint32_t *value)
{
    uint8_t first[4],second[4];
    tiku_ipc_err_t r=read_bytes(address,first,4);if(r)return r;
    r=read_bytes(address,second,4);if(r)return r;
    uint32_t a=tiku_c6_get32(first)&mask,b=tiku_c6_get32(second)&mask;
    if(a!=b)return TIKU_IPC_AGAIN;
    *value=b;return TIKU_IPC_OK;
}
/** @brief Read the cumulative peer FIFO byte count. */
static tiku_ipc_err_t pending(tiku_p4_sdio_owner_t *c,uint32_t *count)
{
    uint32_t offered;tiku_ipc_err_t r=read_counter(0x60,0xfffffu,&offered);
    if(!r)*count=(offered-c->consumed)&0xfffffu;
    return r;
}
/** @brief Advance cold enumeration or attach to an already selected function. */
static tiku_ipc_err_t enumerate(tiku_p4_sdio_owner_t *c)
{
    uint32_t reply=0;uint8_t v=0;tiku_ipc_err_t r;
    switch(c->enumeration){
    case 0:
        r=tiku_p4_sdmmc_byte(0,0,0,&v);
        if(!r&&v==0x32){c->enumeration=5;return TIKU_IPC_AGAIN;}
        c->enumeration=1;return TIKU_IPC_AGAIN;
    case 1:r=tiku_p4_sdmmc_command(0,0,&reply);break;
    case 2:
        r=tiku_p4_sdmmc_command(5,0x00ff8000u,&reply);
        if(r)return TIKU_IPC_AGAIN;
        if(!(reply&(1u<<31)))return TIKU_IPC_AGAIN;
        break;
    case 3:
        r=tiku_p4_sdmmc_command(3,0,&reply);c->rca=reply&0xffff0000u;
        if(!r&&(!c->rca||(reply&0xe000u)))r=TIKU_IPC_IO;
        break;
    case 4:r=tiku_p4_sdmmc_command(7,c->rca,&reply);break;
    case 5:v=2;r=tiku_p4_sdmmc_byte(1,0,2,&v);break;
    case 6:
        r=tiku_p4_sdmmc_byte(0,0,3,&v);
        if(!r&&!(v&2))return TIKU_IPC_AGAIN;
        break;
    case 7:v=2;r=tiku_p4_sdmmc_byte(1,0,7,&v);break;
    case 8:
        r=tiku_p4_sdmmc_width(4);if(!r)r=tiku_p4_sdmmc_clock(10000000);
        if(!r)c->selected=1;
        return r;
    default:return TIKU_IPC_INVALID;
    }
    if(r){return r;}
    c->enumeration++;return TIKU_IPC_AGAIN;
}
/** @brief Claim the hardware and keep any failed-open state owned until close. */
static tiku_ipc_err_t claim(void *context)
{
    tiku_p4_sdio_owner_t *c=context;
    tiku_ipc_err_t r=tiku_p4_sdmmc_open();
    if(r){if(r!=TIKU_IPC_BUSY)(void)tiku_p4_sdmmc_close();return r;}
    c->selected=c->enumeration=c->phase=c->resetting=c->booting=c->go=c->identity=0;
    c->factory=c->adopt=c->cut=0;c->factory_cuts=0;
    c->consumed=c->sent=0;return TIKU_IPC_OK;
}
/** @brief Release pads and clocks after the owner has confirmed quiescence. */
static uint8_t release(void *context)
{(void)context;return tiku_p4_sdmmc_close()==TIKU_IPC_OK;}
/** @brief Liveness uses the native marker or loader function readiness. */
static tiku_ipc_err_t ready(void *context,tiku_sdio_mode_t mode)
{
    tiku_p4_sdio_owner_t *c=context;
    uint8_t b[4];tiku_ipc_err_t r;
    if(c->booting){
        if(TIKU_CLOCK_LT(tiku_clock_time(),c->reset_until))return TIKU_IPC_AGAIN;
        c->booting=0;
    }
    if(!c->selected)return enumerate(c);
    if(mode==TIKU_SDIO_NATIVE){
        r=read_bytes(tiku_sdio_diag_address(20),b,4);if(r)return TIKU_IPC_AGAIN;
        return tiku_c6_get32(b)==TIKU_SDIO_NATIVE_MAGIC?TIKU_IPC_OK:TIKU_IPC_AGAIN;
    }
    r=tiku_p4_sdmmc_byte(0,0,3,b);
    return !r&&(b[0]&2)?TIKU_IPC_OK:TIKU_IPC_AGAIN;
}
/** @brief Select recovery or native entry and reset, retaining resident GO adoption. */
static tiku_ipc_err_t reset_to(void *context,tiku_sdio_mode_t mode)
{
    tiku_p4_sdio_owner_t *c=context;
    c->factory=mode==TIKU_SDIO_FACTORY;
    c->cut=0;
    if(mode==TIKU_SDIO_LOADER&&c->adopt){
        c->adopt=0;c->selected=0;c->enumeration=1;
        c->phase=c->identity=c->go=0;c->consumed=c->sent=0;
        c->booting=1;c->reset_until=tiku_clock_time()+2u*TIKU_CLOCK_SECOND;
        return tiku_p4_sdmmc_clock(400000);
    }
    if(mode==TIKU_SDIO_NATIVE){
#if TIKU_C6_COLD_BOOT_ENABLE
        tiku_p4_sdmmc_peer_recovery(0);
        if(!c->go){
            if(!c->resetting){
                tiku_p4_sdmmc_peer_enable(0);c->resetting=1;
                c->reset_until=tiku_clock_time()+2;return TIKU_IPC_AGAIN;
            }
            if(TIKU_CLOCK_LT(tiku_clock_time(),c->reset_until))return TIKU_IPC_AGAIN;
            tiku_p4_sdmmc_peer_enable(1);c->resetting=0;
            c->go=1;
        }
#endif
        if(!c->go)return TIKU_IPC_UNSUPPORTED;
        /* Native resets the target peripheral; CMD0 clears the inherited SDIO bus state. */
        c->go=0;c->selected=0;c->enumeration=1;
        c->phase=c->identity=0;c->consumed=c->sent=0;
        c->booting=1;c->reset_until=tiku_clock_time()+2u*TIKU_CLOCK_SECOND;
        return tiku_p4_sdmmc_clock(400000);
    }
#if TIKU_C6_COLD_BOOT_ENABLE
    tiku_p4_sdmmc_peer_recovery(1);
#endif
    if(!c->resetting){
        tiku_p4_sdmmc_status_t hardware;
        tiku_p4_sdmmc_status(&hardware);
        if(hardware.active&&!tiku_p4_sdmmc_abort())return TIKU_IPC_AGAIN;
        const uint32_t address[4]={0x6c,0x80,0x44,0x60};
        uint8_t readable=c->selected&&tiku_p4_sdmmc_clock(400000)==TIKU_IPC_OK;
        for(unsigned n=0;n<4;n++){
            uint8_t value[4];
            c->recovery[n]=readable&&!read_bytes(address[n],value,4)?
                tiku_c6_get32(value):UINT32_MAX;
        }
        uint8_t reply[32];uint8_t traced=readable;
        for(unsigned n=0;n<32&&traced;n++)
            traced=!read_bytes(tiku_sdio_diag_address(32+n),reply+n,1);
        traced=traced&&tiku_sdio_diag_valid(reply,32,24)&&reply[1]==4&&!memcmp(reply+4,"IPCE",4);
        for(unsigned n=0;n<5;n++)c->trace[n]=traced?tiku_c6_get32(reply+8+4*n):UINT32_MAX;
        tiku_p4_sdmmc_peer_enable(0);c->resetting=1;
        c->reset_until=tiku_clock_time()+2;return TIKU_IPC_AGAIN;
    }
    if(TIKU_CLOCK_LT(tiku_clock_time(),c->reset_until))return TIKU_IPC_AGAIN;
    tiku_p4_sdmmc_peer_enable(1);c->resetting=0;
    c->selected=c->enumeration=c->phase=c->go=c->identity=0;c->consumed=c->sent=0;
    c->booting=1;c->reset_until=tiku_clock_time()+TIKU_CLOCK_SECOND;
    return tiku_p4_sdmmc_clock(400000);
}
static tiku_ipc_err_t start(void *,tiku_sdio_mode_t,tiku_sdio_class_t,
    const void *,uint16_t,void *,uint16_t,uint64_t);
static tiku_ipc_err_t poll(void *,uint64_t *);
/** @brief Commit a checked native nonce envelope or drain the loader's initial INFO. */
static tiku_ipc_err_t negotiate(void *context,tiku_sdio_mode_t mode,uint32_t session)
{
    tiku_p4_sdio_owner_t *c=context;
    tiku_ipc_err_t r;uint8_t value;uint32_t count;
    if(mode==TIKU_SDIO_NATIVE){
        if(c->phase==20){
            uint32_t credits;
            r=read_counter(0x44,0x0fff0000u,&credits);if(r){return r;}
            r=pending(c,&count);if(r){return r;}
            if(!((credits>>16)&0xfffu)||count!=320)return TIKU_IPC_AGAIN;
            c->phase=0;
            if(c->identity)return TIKU_IPC_OK;
            memset(c->scratch,0,512);c->scratch[0]=2;c->scratch[1]=4;
            memcpy(c->scratch+4,"info",4);
            uint32_t sid=session^0xa5a5a5a5u;if(!sid)sid=1;
            tiku_c6_put32(c->scratch+16,sid);
            if(tiku_sdio_diag_seal(c->scratch,20,12))return TIKU_IPC_INVALID;
            c->identity=1;
            r=start(c,mode,TIKU_SDIO_CONTROL,c->scratch,20,c->scratch+64,32,UINT64_MAX);
            return r?r:TIKU_IPC_AGAIN;
        }
        if(!c->phase){
            uint32_t sid=session^0xa5a5a5a5u;
            if(!sid)sid=1;
            r=tiku_ipc_epoch_fresh(&c->epoch);if(r){return r;}
            memset(c->scratch,0,512);c->scratch[0]=1;c->scratch[1]=12;
            memcpy(c->scratch+4,"IPCE",4);
            for(unsigned n=0;n<8;n++)c->scratch[8+n]=(uint8_t)(c->epoch>>(8u*n));
            tiku_c6_put32(c->scratch+16,sid);
            if(tiku_sdio_diag_seal(c->scratch,20,12))return TIKU_IPC_INVALID;
            r=start(c,mode,TIKU_SDIO_CONTROL,c->scratch,20,c->scratch+64,32,UINT64_MAX);
            return r?r:TIKU_IPC_AGAIN;
        }
        uint64_t token;
        r=poll(c,&token);if(r){return r;}
        if(c->identity){
            if(c->scratch[65]!=sizeof "TikuOS C6 resident IPC"-1||memcmp(c->scratch+68,"TikuOS C6 resident IPC",sizeof "TikuOS C6 resident IPC"-1))return TIKU_IPC_IO;
        }else if(c->scratch[65]!=4||memcmp(c->scratch+68,"IPCE",4))return TIKU_IPC_IO;
        c->phase=20;return TIKU_IPC_AGAIN;
    }
    if(!c->phase){value=3;r=tiku_p4_sdmmc_byte(1,0,4,&value);if(r){return r;}
        value=1;r=tiku_p4_sdmmc_byte(1,1,0x8c,&value);if(r){return r;}c->phase=1;}
    if(c->phase==1){
        r=pending(c,&count);if(r){return r;}if(!count)return TIKU_IPC_AGAIN;
        if(count>512)return TIKU_IPC_IO;
        c->available=count;memset(c->scratch,0,512);
        r=tiku_p4_sdmmc_start(0,1,0x1f800u-count,1,c->scratch,(uint16_t)count,512);
        if(r){return r;}c->phase=2;return TIKU_IPC_AGAIN;
    }
    r=tiku_p4_sdmmc_poll();if(r){return r;}
    c->consumed+=c->available;c->phase=0;
    if(mode==TIKU_SDIO_FACTORY)
        return c->available>=12&&c->available<=480&&
               c->scratch[0]!= 'T'?TIKU_IPC_OK:TIKU_IPC_IO;
    return tiku_c6_response(c->scratch,(uint16_t)c->available,TIKU_C6_INFO,0)&&
        !tiku_c6_get32(c->scratch+20)?TIKU_IPC_OK:TIKU_IPC_IO;
}
/** @brief Retain only owner-managed stages; defer register and DMA work to poll. */
static tiku_ipc_err_t start(void *context,tiku_sdio_mode_t mode,tiku_sdio_class_t kind,
    const void *tx,uint16_t tx_length,void *rx,uint16_t rx_length,uint64_t token)
{
    tiku_p4_sdio_owner_t *c=context;
    if(c->phase)return TIKU_IPC_BUSY;
    if((kind==TIKU_SDIO_CONTROL&&(mode!=TIKU_SDIO_NATIVE||tx_length!=20||rx_length!=32))||
       (kind==TIKU_SDIO_BULK&&(mode!=TIKU_SDIO_NATIVE||tx_length!=320||rx_length!=320))||
       (kind==TIKU_SDIO_UPDATE&&((mode!=TIKU_SDIO_LOADER&&mode!=TIKU_SDIO_FACTORY)||
        (mode==TIKU_SDIO_LOADER&&tx_length<32)||!tx_length||tx_length>512||rx_length!=512)))
        return TIKU_IPC_INVALID;
    c->tx=tx;c->rx=rx;c->tx_length=tx_length;c->rx_length=rx_length;
    c->token=token;c->kind=(uint8_t)kind;c->offset=0;c->phase=10;
    memset(rx,0,rx_length);return TIKU_IPC_OK;
}
/** @brief Complete native register traffic or one bounded write/read FIFO exchange. */
static tiku_ipc_err_t poll(void *context,uint64_t *token)
{
    tiku_p4_sdio_owner_t *c=context;
    tiku_ipc_err_t r;uint8_t value;uint32_t count;
    *token=c->token;
    if(c->phase<10)return TIKU_IPC_AGAIN;
    if(c->kind==TIKU_SDIO_CONTROL){
        if(c->phase==10){
            value=0;r=tiku_p4_sdmmc_byte(1,1,tiku_sdio_diag_address(0),&value);
            if(r){return r;}c->offset=1;c->phase=11;
        }
        if(c->phase==11){
            unsigned end=c->offset+4;if(end>20)end=20;
            for(;c->offset<end;c->offset++){
                value=c->tx[c->offset];r=tiku_p4_sdmmc_byte(1,1,tiku_sdio_diag_address(c->offset),&value);
                if(r){return r;}
            }
            if(c->offset<20)return TIKU_IPC_AGAIN;
            value=c->tx[0];r=tiku_p4_sdmmc_byte(1,1,tiku_sdio_diag_address(0),&value);
            if(r){return r;}c->phase=12;return TIKU_IPC_AGAIN;
        }
        if(c->phase==12){
            r=tiku_p4_sdmmc_byte(0,1,tiku_sdio_diag_address(0),&value);if(r){return r;}
            if(value)return TIKU_IPC_AGAIN;
            r=tiku_p4_sdmmc_byte(0,1,tiku_sdio_diag_address(32),&value);if(r){return r;}
            if(value!=c->tx[0])return TIKU_IPC_AGAIN;
            c->offset=0;c->phase=13;
        }
        unsigned end=c->offset+4;if(end>32)end=32;
        for(;c->offset<end;c->offset++){
            r=tiku_p4_sdmmc_byte(0,1,tiku_sdio_diag_address(32+c->offset),c->rx+c->offset);
            if(r){return r;}
        }
        if(c->offset<32)return TIKU_IPC_AGAIN;
        if(!tiku_sdio_diag_valid(c->rx,32,24)||c->rx[0]!=c->tx[0]||
           tiku_sdio_diag_session(c->rx,24)!=tiku_sdio_diag_session(c->tx,12)){
            c->phase=12;return TIKU_IPC_AGAIN;
        }
        c->phase=0;return TIKU_IPC_OK;
    }
    if(c->phase==10){
        if(c->factory&&c->tx_length==1&&c->tx[0]==0){c->phase=12;return TIKU_IPC_AGAIN;}
        uint32_t credits;r=read_counter(0x44,0x0fff0000u,&credits);if(r){return r;}
        if(!(((credits>>16)-c->sent)&0xfffu))return TIKU_IPC_AGAIN;
        r=tiku_p4_sdmmc_start(1,1,0x1f800u-c->tx_length,1,(void *)c->tx,c->tx_length,512);
        if(r){return r;}c->phase=11;return TIKU_IPC_AGAIN;
    }
    if(c->phase==11){
        r=tiku_p4_sdmmc_poll();if(r){return r;}c->sent=(c->sent+1)&0xfffu;c->phase=12;
        if(c->factory&&c->cut){c->cut=0;c->factory_cuts++;return TIKU_IPC_TIMEOUT;}
    }
    if(c->phase==12){
        r=pending(c,&count);if(r){return r;}if(!count)return TIKU_IPC_AGAIN;
        if(count>c->rx_length)return TIKU_IPC_IO;
        if(c->kind==TIKU_SDIO_BULK&&count!=320)return TIKU_IPC_IO;
        c->available=count;
        r=tiku_p4_sdmmc_start(0,1,0x1f800u-count,1,c->rx,(uint16_t)count,512);
        if(r){return r;}c->phase=13;return TIKU_IPC_AGAIN;
    }
    r=tiku_p4_sdmmc_poll();if(r){return r;}
    c->consumed+=c->available;
    if(c->kind==TIKU_SDIO_UPDATE&&!c->factory){
        if(!tiku_c6_response(c->rx,(uint16_t)c->available,c->tx[5],tiku_c6_get32(c->tx+8)))return TIKU_IPC_IO;
        if(c->tx[5]==TIKU_C6_GO&&!tiku_c6_get32(c->rx+20))c->go=1;
    }
    c->phase=0;return TIKU_IPC_OK;
}
/** @brief Fence descriptor access before the owner returns or quarantines stages. */
static uint8_t abort_transfer(void *context,uint64_t token)
{
    tiku_p4_sdio_owner_t *c=context;
    if(token!=c->token)return 0;
    if(!tiku_p4_sdmmc_abort())return 0;
    c->phase=0;c->tx=NULL;c->rx=NULL;return 1;
}
const tiku_sdio_owner_ops_t tiku_p4_sdio_owner_ops={claim,release,start,poll,
    abort_transfer,ready,reset_to,negotiate};
