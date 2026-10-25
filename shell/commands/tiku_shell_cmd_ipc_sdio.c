/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc_sdio.c - bounded hardware SDIO discovery and native diagnostics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_cmd_ipc_sdio.h"
#include <shell/tiku_shell.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/cpu/tiku_hang.h>
#include <kernel/vfs/tiku_vfs.h>
#include <hal/tiku_c6_loader_hal.h>
#include <hal/tiku_ipc_shared.h>
#include <hal/tiku_ipc_test_pattern.h>
#include <kernel/ipc/tiku_ipc_stream.h>
#include <arch/esp32p4/tiku_sdmmc_arch.h>
#include <boot/tiku_boot_c6.h>
#include <services/console/tiku_console.h>
#include <string.h>
#if (TIKU_HAS_SDIO_C6 + 0)
static uint8_t loader_tx[512] __attribute__((section(".p4.ipc"),aligned(64)));
static uint8_t loader_rx[512] __attribute__((section(".p4.ipc"),aligned(64)));
static uint8_t bridge_frame[512] __attribute__((section(".p4.ipc"),aligned(64)));
static uint16_t bridge_length;
static uint32_t bridge_errors;
/** @brief Run eligible processes while retaining the shell's current binding. */
static void service(void)
{
    (void)tiku_sched_run_once_except(TIKU_THIS());
    tiku_watchdog_kick();tiku_hang_checkin();
}
/** @brief Wait within the owner's separate boot and negotiation budgets. */
static tiku_ipc_err_t wait_mode(tiku_sdio_mode_t mode)
{
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();
    tiku_clock_time_t began=tiku_clock_time();
    while(c->state!=TIKU_SDIO_READY||c->mode!=mode){
        if(c->state==TIKU_SDIO_FAILED)return TIKU_IPC_IO;
        if((tiku_clock_time_t)(tiku_clock_time()-began)>c->profile.boot+c->profile.negotiate+c->profile.abort)
            return TIKU_IPC_TIMEOUT;
        service();
    }
    return TIKU_IPC_OK;
}
/** @brief Submit one copied job and take its result after confirmed completion. */
static tiku_ipc_err_t exchange(tiku_sdio_class_t kind,const void *tx,uint16_t length,
    void *rx,uint16_t reply_length)
{
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();
    tiku_sdio_job_t job;tiku_sdio_result_t result;
    tiku_ipc_err_t r=tiku_ipc_sdio_submit(c,kind,tx,length,reply_length,256,&job);
    if(r)return r;
    do{service();r=tiku_ipc_sdio_take(c,job,rx,reply_length,&result);}while(r==TIKU_IPC_AGAIN);
    if(r)return r;
    return result.quarantined?TIKU_IPC_BUSY:result.status;
}
/** @brief Preserve native envelope layout under the shared controller owner. */
static tiku_ipc_err_t native_exchange(const char *command,uint8_t reply[32])
{
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();
    uint8_t request[20]={0};size_t length=strlen(command);
    tiku_ipc_err_t r;
    if(length>12)return TIKU_IPC_INVALID;
    r=wait_mode(TIKU_SDIO_NATIVE);if(r)return r;
    request[0]=c->native_sequence==255?1:(uint8_t)(c->native_sequence+1u);
    request[1]=(uint8_t)length;memcpy(request+4,command,length);
    tiku_c6_put32(request+16,c->session);
    if(tiku_sdio_diag_seal(request,20,12))return TIKU_IPC_INVALID;
    return exchange(TIKU_SDIO_CONTROL,request,20,reply,32);
}
/** @brief Print one complete native reply received through the owner. */
static tiku_ipc_err_t native_command(const char *command)
{
    uint8_t reply[32];tiku_ipc_err_t r=native_exchange(command,reply);if(r)return r;
    SHELL_PRINTF("C6 reply: ");
    for(unsigned n=0;n<reply[1];n++)tiku_shell_io_putc((char)reply[4+n]);
    SHELL_PRINTF("\n");return TIKU_IPC_OK;
}
/** @brief Decode a bounded decimal counter from a validated native reply. */
static tiku_ipc_err_t native_counter(const char *command,uint32_t *out)
{
    uint8_t reply[32];uint32_t value=0;tiku_ipc_err_t r=native_exchange(command,reply);
    if(r)return r;
    if(!reply[1]||reply[1]>10)return TIKU_IPC_IO;
    for(unsigned n=0;n<reply[1];n++){
        uint8_t b=reply[4+n];
        if(b<'0'||b>'9'||value>(UINT32_MAX-(b-'0'))/10u)return TIKU_IPC_IO;
        value=value*10u+b-'0';
    }
    *out=value;return TIKU_IPC_OK;
}
/** @brief Stream 600 KiB through two loans while copied and native control progress. */
static tiku_ipc_err_t stream_test(void)
{
    tiku_ipc_endpoint_t endpoint={0};tiku_ipc_buffer_t buffers[2]={{0}};
    tiku_ipc_status_t status;tiku_ipc_received_t received;
    uint32_t produced=0,chunks=0,controls=0,max_control=0,native_max=0,failures=0;
    uint32_t crc=UINT32_MAX,peer_bytes=0,peer_crc=0;
    tiku_clock_time_t began=tiku_clock_time(),last=began,until=began+5u*TIKU_CLOCK_SECOND;
    uint8_t reply[32],sent=0,pending=0;tiku_ipc_err_t r;
    r=wait_mode(TIKU_SDIO_NATIVE);if(r)return r;
    r=native_exchange("clear",reply);if(r)return r;
    r=tiku_ipc_open("c6/bulk",&endpoint);if(r)return r;
    while(TIKU_CLOCK_LT(tiku_clock_time(),until)){
        service();
        if(!sent&&tiku_ipc_send(endpoint,"ready",5)==TIKU_IPC_OK)sent=1;
        if(tiku_ipc_receive(endpoint,&received)==TIKU_IPC_OK){
            if(received.length!=5||memcmp(received.data,"ready",5)){failures++;goto cleanup;}
            break;
        }
    }
    if(!sent||!TIKU_CLOCK_LT(tiku_clock_time(),until)){failures++;goto cleanup;}
    for(unsigned i=0;i<2;i++)if(tiku_ipc_buffer_alloc(512,&buffers[i])!=TIKU_IPC_OK){failures++;goto cleanup;}
    SHELL_PRINTF("SDIO profile bytes=614400 window=2 block=512 bus=10000000 width=4 control_bound=256ticks native_burst=64ticks\n");
    began=tiku_clock_time();
    while(produced<614400){
        unsigned complete=0;until=tiku_clock_time()+10u*TIKU_CLOCK_SECOND;
        for(unsigned i=0;i<2;i++){
            void *p;uint16_t size;tiku_ipc_submission_t ticket;
            if(tiku_ipc_buffer_map(buffers[i],&p,&size)!=TIKU_IPC_OK){failures++;goto cleanup;}
            for(unsigned n=0;n<size;n++)((uint8_t *)p)[n]=(uint8_t)(((produced+n)*17u)^((produced+n)>>8));
            crc=tiku_ipc_shared_crc(crc,p,size);
            r=tiku_ipc_submit(endpoint,buffers[i],TIKU_IPC_INPUT,8u*TIKU_CLOCK_SECOND,&ticket);
            if(r){failures++;goto cleanup;}
            produced+=size;
        }
        while(complete<2){
            tiku_ipc_completion_t completion;service();
            if(pending&&(tiku_clock_time_t)(tiku_clock_time()-last)>256){failures++;goto cleanup;}
            if(!pending&&tiku_ipc_send(endpoint,"control",7)==TIKU_IPC_OK){last=tiku_clock_time();pending=1;}
            while(tiku_ipc_receive(endpoint,&received)==TIKU_IPC_OK){
                uint32_t elapsed=(tiku_clock_time_t)(tiku_clock_time()-last);
                if(!pending||received.length!=7||memcmp(received.data,"control",7)||elapsed>256)failures++;
                if(elapsed>max_control)max_control=elapsed;
                controls++;pending=0;
            }
            while(tiku_ipc_completion(endpoint,&completion)==TIKU_IPC_OK){
                if(completion.status||completion.quarantined)failures++;
                complete++;chunks++;
            }
            if(!TIKU_CLOCK_LT(tiku_clock_time(),until)){failures++;goto cleanup;}
        }
        if(!(chunks%128u)){
            uint32_t unused;tiku_clock_time_t tick=tiku_clock_time();
            if(native_counter("ticks",&unused)!=TIKU_IPC_OK)failures++;
            uint32_t elapsed=(tiku_clock_time_t)(tiku_clock_time()-tick);
            if(elapsed>native_max)native_max=elapsed;
            if(elapsed>128)failures++;
            SHELL_PRINTF("SDIO progress chunks=%lu bytes=%lu\n",(unsigned long)chunks,(unsigned long)produced);
        }
        if(failures)break;
    }
    if(native_counter("bytes",&peer_bytes)!=TIKU_IPC_OK||native_counter("crc",&peer_crc)!=TIKU_IPC_OK||
        peer_bytes!=produced||peer_crc!=~crc||produced!=614400||!controls)failures++;
cleanup:
    for(unsigned i=0;i<2;i++)if(buffers[i].slot_plus_one)(void)tiku_ipc_buffer_release(buffers[i]);
    if(endpoint.slot_plus_one){
        (void)tiku_ipc_close(endpoint);
        for(unsigned i=0;i<20;i++)service();
        if(tiku_ipc_status((uint16_t)(endpoint.slot_plus_one-1u),&status)==TIKU_IPC_OK)
            SHELL_PRINTF("SDIO backing live=%u free=%u quarantined=%u wire_bad=%lu failed=%lu\n",status.buffers_live,
                status.buffers_free,status.buffers_quarantined,(unsigned long)status.wire_bad,(unsigned long)status.wire_failed);
    }
    SHELL_PRINTF("SDIO stream bytes=%lu peer_bytes=%lu crc=%08lx peer_crc=%08lx chunks=%lu controls=%lu max_control=%lu native_max=%lu ticks=%lu failures=%lu\n",
        (unsigned long)produced,(unsigned long)peer_bytes,(unsigned long)~crc,(unsigned long)peer_crc,
        (unsigned long)chunks,(unsigned long)controls,(unsigned long)max_control,(unsigned long)native_max,
        (unsigned long)(tiku_clock_time()-began),(unsigned long)failures);
    return failures?TIKU_IPC_IO:TIKU_IPC_OK;
}
/** @brief Send a raster or sensor object using the public two-chunk stream API. */
static tiku_ipc_err_t object_test(unsigned profile,uint8_t slow)
{
    tiku_ipc_endpoint_t endpoint={0};tiku_ipc_stream_t stream={0};
    tiku_ipc_status_t status;tiku_ipc_received_t received;
    uint8_t payload[TIKU_IPC_STREAM_CHUNK],reply[32],sent=0,pending=0;
    uint32_t produced=0,chunks=0,controls=0,failures=0,max_control=0,crc=UINT32_MAX;
    uint32_t peer_bytes=0,peer_crc=0,next_progress=65536;
    tiku_clock_time_t began=tiku_clock_time(),last=began,deadline=began+640;
    tiku_ipc_err_t r=wait_mode(TIKU_SDIO_NATIVE);if(r)return r;
    if(native_exchange("clear",reply)||native_exchange(profile==1?"image":"sensor",reply)||
        (slow&&native_exchange("slow",reply)))return TIKU_IPC_IO;
    r=tiku_ipc_open("c6/bulk",&endpoint);if(r)return r;
    while(TIKU_CLOCK_LT(tiku_clock_time(),deadline)){
        service();
        if(!sent&&tiku_ipc_send(endpoint,"ready",5)==TIKU_IPC_OK)sent=1;
        if(tiku_ipc_receive(endpoint,&received)==TIKU_IPC_OK)break;
    }
    if(!sent||!TIKU_CLOCK_LT(tiku_clock_time(),deadline)||received.length!=5||memcmp(received.data,"ready",5)){
        failures++;goto cleanup;
    }
    if(tiku_ipc_stream_open(&stream,endpoint,profile,614400)!=TIKU_IPC_OK){failures++;goto cleanup;}
    began=tiku_clock_time();deadline=began+10u*TIKU_CLOCK_SECOND;
    SHELL_PRINTF("SDIO object profile=%u slow=%u bytes=614400 window=2 payload=484 control_bound=256ticks\n",profile,slow);
    while(!tiku_ipc_stream_done(&stream)){
        tiku_ipc_completion_t completion;service();
        if(pending&&(tiku_clock_time_t)(tiku_clock_time()-last)>256){failures++;goto cleanup;}
        if(produced<614400){
            uint16_t length=(uint16_t)(614400-produced>sizeof payload?sizeof payload:614400-produced);
            for(unsigned n=0;n<length;n++)payload[n]=tiku_ipc_test_byte(profile,produced+n);
            r=tiku_ipc_stream_write(&stream,payload,length,8u*TIKU_CLOCK_SECOND);
            if(r==TIKU_IPC_OK){crc=tiku_ipc_shared_crc(crc,payload,length);produced+=length;}
            else if(r!=TIKU_IPC_AGAIN){failures++;goto cleanup;}
        }
        if(!pending&&tiku_ipc_send(endpoint,"control",7)==TIKU_IPC_OK){last=tiku_clock_time();pending=1;}
        while(tiku_ipc_receive(endpoint,&received)==TIKU_IPC_OK){
            uint32_t elapsed=(tiku_clock_time_t)(tiku_clock_time()-last);
            if(!pending||received.length!=7||memcmp(received.data,"control",7)||elapsed>256)failures++;
            if(elapsed>max_control)max_control=elapsed;
            controls++;pending=0;
        }
        while(tiku_ipc_completion(endpoint,&completion)==TIKU_IPC_OK){
            if(tiku_ipc_stream_finish(&stream,&completion)!=TIKU_IPC_OK||completion.quarantined)failures++;
            chunks++;deadline=tiku_clock_time()+10u*TIKU_CLOCK_SECOND;
        }
        if(produced>=next_progress){
            SHELL_PRINTF("SDIO object progress bytes=%lu chunks=%lu\n",(unsigned long)produced,(unsigned long)chunks);
            next_progress+=65536;
        }
        if(failures||!TIKU_CLOCK_LT(tiku_clock_time(),deadline)){failures++;goto cleanup;}
    }
    if(native_counter("bytes",&peer_bytes)||native_counter("crc",&peer_crc)||peer_bytes!=614400||
        peer_crc!=~crc||!controls)failures++;
cleanup:
    if(endpoint.slot_plus_one){
        (void)tiku_ipc_close(endpoint);for(unsigned n=0;n<20;n++)service();
        if(tiku_ipc_status((uint16_t)(endpoint.slot_plus_one-1u),&status)==TIKU_IPC_OK)
            SHELL_PRINTF("SDIO object backing live=%u free=%u quarantined=%u\n",status.buffers_live,status.buffers_free,status.buffers_quarantined);
    }
    SHELL_PRINTF("SDIO object profile=%u bytes=%lu peer_bytes=%lu crc=%08lx peer_crc=%08lx chunks=%lu controls=%lu max_control=%lu ticks=%lu failures=%lu\n",
        profile,(unsigned long)produced,(unsigned long)peer_bytes,(unsigned long)~crc,(unsigned long)peer_crc,
        (unsigned long)chunks,(unsigned long)controls,(unsigned long)max_control,
        (unsigned long)(tiku_clock_time()-began),(unsigned long)failures);
    return failures?TIKU_IPC_IO:TIKU_IPC_OK;
}
/** @brief Reset the peer while the local descriptor engine owns a bulk stage. */
static tiku_ipc_err_t interrupt_test(void)
{
    tiku_ipc_endpoint_t endpoint={0};tiku_ipc_buffer_t buffer={0};
    tiku_ipc_submission_t ticket;tiku_ipc_completion_t completion;
    tiku_ipc_received_t message;tiku_ipc_status_t status={0};tiku_p4_sdmmc_status_t hardware={0};
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();uint8_t reply[32],sent=0,found=0;
    tiku_clock_time_t began=tiku_clock_time(),until=began+640;
    tiku_ipc_err_t result=TIKU_IPC_IO;
    if(native_exchange("clear",reply)||tiku_ipc_open("c6/bulk",&endpoint))return result;
    while(TIKU_CLOCK_LT(tiku_clock_time(),until)){
        service();if(!sent&&!tiku_ipc_send(endpoint,"ready",5))sent=1;
        if(tiku_ipc_receive(endpoint,&message)==TIKU_IPC_OK){found=1;break;}
    }
    if(!found||tiku_ipc_buffer_alloc(512,&buffer))goto cleanup;
    {
        void *p;uint16_t length;
        if(tiku_ipc_buffer_map(buffer,&p,&length))goto cleanup;
        for(unsigned n=0;n<length;n++)((uint8_t *)p)[n]=tiku_ipc_test_byte(0,n);
    }
    if(tiku_ipc_submit(endpoint,buffer,TIKU_IPC_INPUT,1024,&ticket))goto cleanup;
    (void)tiku_ipc_buffer_release(buffer);buffer=(tiku_ipc_buffer_t){0};found=0;
    until=tiku_clock_time()+256;
    do{
        service();tiku_p4_sdmmc_status(&hardware);
        if(hardware.active&&c->active>0&&c->active<3){found=1;break;}
    }while(TIKU_CLOCK_LT(tiku_clock_time(),until));
    if(!found)goto cleanup;
    began=tiku_clock_time();
    if(tiku_ipc_reset((uint16_t)(endpoint.slot_plus_one-1u)))goto cleanup;
    until=tiku_clock_time()+1024;found=0;
    do{
        service();
        if(tiku_ipc_completion(endpoint,&completion)==TIKU_IPC_OK){found=1;break;}
    }while(TIKU_CLOCK_LT(tiku_clock_time(),until));
    if(!found||completion.quarantined||(completion.status!=TIKU_IPC_DOWN&&completion.status!=TIKU_IPC_CANCELLED))goto cleanup;
    if(wait_mode(TIKU_SDIO_LOADER))goto cleanup;
    result=TIKU_IPC_OK;
cleanup:
    if(buffer.slot_plus_one)(void)tiku_ipc_buffer_release(buffer);
    (void)tiku_ipc_close(endpoint);for(unsigned n=0;n<20;n++)service();
    if(tiku_ipc_status((uint16_t)(endpoint.slot_plus_one-1u),&status)||status.buffers_live||
        status.buffers_quarantined||status.buffers_free!=4)result=TIKU_IPC_IO;
    SHELL_PRINTF("SDIO reset pending_dma=%u ready_loader=%u ticks=%lu result=%d live=%u free=%u quarantined=%u\n",
        hardware.active,c->state==TIKU_SDIO_READY&&c->mode==TIKU_SDIO_LOADER,
        (unsigned long)(tiku_clock_time()-began),result,status.buffers_live,status.buffers_free,status.buffers_quarantined);
    return result;
}
/** @brief Stage one validated loader packet from the explicitly opened USB channel. */
static void bridge_receive(void *unused,uint8_t *data,size_t length)
{
    (void)unused;
    if(bridge_length||length<32||length>512||
        !tiku_c6_response(data,(uint16_t)length,data[5],tiku_c6_get32(data+8))||
        !tiku_c6_get32(data+8)||tiku_c6_get32(data+20)){
        bridge_errors++;return;
    }
    memset(loader_tx,0,sizeof loader_tx);memcpy(loader_tx,data,length);
    bridge_length=(uint16_t)length;
}
/** @brief Forward update jobs through the same owner as native control and bulk. */
static tiku_ipc_err_t loader_bridge(void)
{
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();
    tiku_clock_time_t began=tiku_clock_time();
    tiku_ipc_err_t result;uint32_t exchanges=0;uint16_t length;
    result=tiku_ipc_sdio_mode(c,TIKU_SDIO_LOADER);if(result)return result;
    result=wait_mode(TIKU_SDIO_LOADER);if(result)return result;
    bridge_length=0;bridge_errors=0;
    if(tiku_console_add_channel(0xf5,0xff,0,bridge_receive,NULL,bridge_frame,sizeof bridge_frame))
        return TIKU_IPC_BUSY;
    SHELL_PRINTF("C6 loader bridge ready channel=f5 ticks=%u\n",64u*TIKU_CLOCK_SECOND);
    length=tiku_boot_c6_loader_info(loader_rx,sizeof loader_rx);
    if(!length){tiku_console_remove_channel(0xf5,0xff);return TIKU_IPC_IO;}
    (void)tiku_console_send_frame(0xf5,loader_rx,length,NULL,0);
    while((tiku_clock_time_t)(tiku_clock_time()-began)<64u*TIKU_CLOCK_SECOND){
        tiku_console_pump();service();
        if(!bridge_length)continue;
        result=exchange(TIKU_SDIO_UPDATE,loader_tx,bridge_length,loader_rx,512);
        bridge_length=0;if(result)break;
        length=(uint16_t)(32u+tiku_c6_get32(loader_rx+24));
        if(length>512){result=TIKU_IPC_IO;break;}
        (void)tiku_console_send_frame(0xf5,loader_rx,length,NULL,0);exchanges++;
        if(!tiku_c6_get32(loader_rx+20)&&(loader_rx[5]==TIKU_C6_GO||
            loader_rx[5]==TIKU_C6_RESET||loader_rx[5]==TIKU_C6_FACTORY)){
            if(loader_rx[5]==TIKU_C6_GO)result=tiku_ipc_sdio_mode(c,TIKU_SDIO_NATIVE);
            break;
        }
    }
    tiku_console_remove_channel(0xf5,0xff);
    SHELL_PRINTF("C6 loader bridge done result=%d exchanges=%lu rejected=%lu\n",result,
        (unsigned long)exchanges,(unsigned long)bridge_errors);
    return result;
}
/** @brief Verify the chosen bank manifest before an explicit resident-loader GO. */
static tiku_ipc_err_t run_bank(unsigned bank)
{
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();tiku_ipc_err_t r;
    if(c->state!=TIKU_SDIO_READY||c->mode!=TIKU_SDIO_LOADER){
        r=tiku_ipc_sdio_mode(c,TIKU_SDIO_LOADER);if(r)return r;
        r=wait_mode(TIKU_SDIO_LOADER);if(r)return r;
    }
    tiku_c6_packet(loader_tx,TIKU_C6_STATUS,1,bank,0,NULL,0);
    r=exchange(TIKU_SDIO_UPDATE,loader_tx,32,loader_rx,512);if(r)return r;
    if(tiku_c6_get32(loader_rx+20)||tiku_c6_get32(loader_rx+24)!=40||
        tiku_c6_get32(loader_rx+64)||tiku_c6_get32(loader_rx+32)!=0x364b4c54u)return TIKU_IPC_IO;
    tiku_c6_packet(loader_tx,TIKU_C6_GO,2,bank,0,NULL,0);
    r=exchange(TIKU_SDIO_UPDATE,loader_tx,32,loader_rx,512);if(r)return r;
    if(tiku_c6_get32(loader_rx+20))return TIKU_IPC_IO;
    r=tiku_ipc_sdio_mode(c,TIKU_SDIO_NATIVE);if(r)return r;
    return wait_mode(TIKU_SDIO_NATIVE);
}
void tiku_shell_ipc_sdio(uint8_t argc,const char *argv[])
{
    tiku_ipc_err_t r=TIKU_IPC_INVALID;tiku_p4_sdmmc_status_t status;
    tiku_ipc_sdio_t *c=tiku_boot_c6_owner();
    if(argc<3||argc>4){
        SHELL_PRINTF("ipc sdio probe|reset|loader|bridge|qualify|stream|image|sensor|slow|corrupt|interrupt|pause|resume\n");
        SHELL_PRINTF("ipc sdio console <command>|run <bank>\n");return;
    }
    if(!(tiku_vfs_caller_cap_get()&TIKU_VFS_CAP_SYS)){
        SHELL_PRINTF("SDMMC SYS capability required\n");return;
    }
    if(argc==4&&!strcmp(argv[2],"console"))r=native_command(argv[3]);
    else if(argc==4&&!strcmp(argv[2],"run")&&strlen(argv[3])==1&&argv[3][0]>='0'&&argv[3][0]<='1')
        r=run_bank((unsigned)(argv[3][0]-'0'));
    else if(argc==3&&!strcmp(argv[2],"bridge"))r=loader_bridge();
    else if(argc==3&&!strcmp(argv[2],"pause"))r=tiku_boot_c6_packet_gate(0);
    else if(argc==3&&!strcmp(argv[2],"resume"))r=tiku_boot_c6_packet_gate(1);
    else if(argc==3&&!strcmp(argv[2],"stream"))r=stream_test();
    else if(argc==3&&!strcmp(argv[2],"image"))r=object_test(1,0);
    else if(argc==3&&!strcmp(argv[2],"sensor"))r=object_test(2,0);
    else if(argc==3&&!strcmp(argv[2],"slow"))r=object_test(2,1);
    else if(argc==3&&!strcmp(argv[2],"corrupt")){
        tiku_boot_c6_fault_every(100);r=object_test(1,0);
        SHELL_PRINTF("SDIO injected=%lu\n",(unsigned long)tiku_boot_c6_faults());
        tiku_boot_c6_fault_every(0);
    }
    else if(argc==3&&!strcmp(argv[2],"interrupt"))r=interrupt_test();
    else if(argc==3&&(!strcmp(argv[2],"reset")||!strcmp(argv[2],"loader"))){
        r=tiku_ipc_sdio_mode(c,TIKU_SDIO_LOADER);if(!r)r=wait_mode(TIKU_SDIO_LOADER);
        if(!r){uint16_t n=tiku_boot_c6_loader_info(loader_rx,512);
            SHELL_PRINTF("C6 loader info=");for(unsigned i=32;i<n;i++)tiku_shell_io_putc((char)loader_rx[i]);SHELL_PRINTF("\n");}
    }else if(argc==3&&!strcmp(argv[2],"qualify")){
        const char *checks[]={"info","test","ticks","irq","proc","bytes"};r=TIKU_IPC_OK;
        for(unsigned n=0;n<sizeof checks/sizeof checks[0]&&!r;n++)r=native_command(checks[n]);
    }else if(argc==3&&!strcmp(argv[2],"probe"))r=TIKU_IPC_OK;
    tiku_p4_sdmmc_status(&status);
    if(status.errors){
        uint32_t recovery[4];tiku_boot_c6_recovery(recovery);
        SHELL_PRINTF("C6 recovery mailbox=%08lx ready=%08lx credits=%08lx offered=%lu\n",
            (unsigned long)recovery[0],(unsigned long)recovery[1],
            (unsigned long)recovery[2],(unsigned long)recovery[3]);
        uint32_t trace[5];tiku_boot_c6_trace(trace);
        if(trace[0]!=UINT32_MAX)SHELL_PRINTF("C6 startup flags=%08lx raw=%08lx rx=%08lx tx=%08lx ticks=%lu\n",
            (unsigned long)trace[0],(unsigned long)trace[1],(unsigned long)trace[2],
            (unsigned long)trace[3],(unsigned long)trace[4]);
    }
    SHELL_PRINTF("SDMMC result=%d state=%u mode=%u hz=%lu width=%u commands=%lu bytes=%lu raw=%08lx dma=%08lx errors=%lu\n",
        r,(unsigned)c->state,(unsigned)c->mode,(unsigned long)status.hz,status.width,
        (unsigned long)status.commands,(unsigned long)status.bytes,(unsigned long)status.raw,
        (unsigned long)status.dma,(unsigned long)status.errors);
}
#endif
