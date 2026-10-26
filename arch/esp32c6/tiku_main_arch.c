/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_main_arch.c - resident native boot, diagnostic console and IPC consumer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include "tiku_sdio_arch.h"
#include "tiku_console_arch.h"
#include <kernel/ipc/tiku_ipc_backend.h>
#include <services/ipc/tiku_ipc_wire.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/memory/tiku_mem.h>
#include <hal/tiku_ipc_shared.h>
#include <hal/tiku_sdio_diag.h>
#include <hal/tiku_ipc_test_pattern.h>
#include <kernel/ipc/tiku_ipc_stream.h>
#include <string.h>
#if TIKU_C6_STORAGE
#include "tiku_flash_arch.h"
#include "tiku_nvm_arch.h"
#include <kernel/memory/tiku_layout.h>
extern unsigned char __uninit_start[],__uninit_end[];
static uint8_t storage_status;
#endif
#if TIKU_THREADS_ENABLE
#include <kernel/threads/tiku_thread.h>
#include <hal/tiku_cpu.h>
#endif
#if TIKU_C6_THREAD_TEST
#include <TikuBench/tests/esp32c6/test_threads.h>
#endif
#if TIKU_C6_STORAGE_TEST
#include <TikuBench/tests/esp32c6/test_storage.h>
#endif

static tiku_ipc_wire_t wire;
static tiku_ipc_bus_t bus;
static tiku_link_t *diagnostic,*carrier;
static uint32_t bytes,checksum=UINT32_MAX,bad;
static uint64_t host_epoch;
static tiku_ipc_endpoint_t bulk;
static tiku_ipc_received_t message;
static uint8_t pending;
static uint8_t epoch_ack;
static struct tiku_timer app_timer;
static uint8_t text_input[12],text_length;
static tiku_ipc_stream_reader_t reader;
static uint8_t fixture,object_mode,slow;
static tiku_clock_time_t next_accept;
static uint64_t work_cycles;
static uint32_t sink_waits;
extern unsigned char __tier_sram_start[],__tier_sram_end[];
/** @brief Consume a staged pattern block and record a running CRC and byte count. */
static tiku_ipc_err_t consume(void *unused,const void *data,uint16_t length)
{
    const uint8_t *p=data;
    tiku_ipc_stream_reader_t proposed=reader;
    (void)unused;
    if(slow&&TIKU_CLOCK_LT(tiku_clock_time(),next_accept)){sink_waits++;return TIKU_IPC_AGAIN;}
    if(object_mode){
        const void *payload;
        if(length>=28){
            uint32_t offset=(uint32_t)p[16]|(uint32_t)p[17]<<8|(uint32_t)p[18]<<16|(uint32_t)p[19]<<24;
            if(offset>reader.offset)return TIKU_IPC_AGAIN;
        }
        tiku_ipc_err_t r=tiku_ipc_stream_decode(&proposed,p,length,&payload,&length);
        if(r){bad++;return r;}
        p=payload;
    }
    for(unsigned n=0;n<length;n++){
        uint32_t offset=bytes+n;
        if(p[n]!=tiku_ipc_test_byte(fixture,offset))return TIKU_IPC_AGAIN;
    }
    checksum=tiku_ipc_shared_crc(checksum,p,length);bytes+=length;
    reader=proposed;next_accept=tiku_clock_time()+32;
    return TIKU_IPC_OK;
}
/** @brief Generate a deterministic output block without retaining the stage. */
static tiku_ipc_err_t produce(void *unused,void *data,uint16_t length)
{
    uint8_t *p=data;(void)unused;
    for(unsigned n=0;n<length;n++)p[n]=(uint8_t)(n^0x5a);
    return TIKU_IPC_OK;
}
static const tiku_ipc_wire_sink_t sink={consume,produce,NULL};
static const tiku_ipc_endpoint_config_t endpoints[]={
    {"local0",NULL,NULL,1},{"local1",NULL,NULL,0},
    {"c6/bulk",&tiku_ipc_wire_ops,&wire,0}
};
#if TIKU_C6_FIRMWARE
static uint8_t memory_ok;
extern unsigned char __worker_reserve_start[],__worker_reserve_end[];
extern unsigned char __radio_reserve_start[],__radio_reserve_end[];
/** @brief Check the unused SRAM tier with address-dependent patterns before admission. */
static uint8_t memory_check(void)
{
    volatile uint32_t *begin=(volatile uint32_t *)(void *)__tier_sram_start;
    volatile uint32_t *end=(volatile uint32_t *)(void *)__tier_sram_end;
    for(unsigned pass=0;pass<2;pass++){
        for(volatile uint32_t *p=begin;p<end;p++){
            *p=(uint32_t)(uintptr_t)p^(pass?0xa5a5a5a5u:0x5a5a5a5au);
            if(!((uintptr_t)p&16383u))tiku_watchdog_kick();
        }
        for(volatile uint32_t *p=begin;p<end;p++)
            if(*p!=((uint32_t)(uintptr_t)p^(pass?0xa5a5a5a5u:0x5a5a5a5au)))return 0;
    }
    return 1;
}
#endif
/** @brief Convert an unsigned counter to decimal without formatted-I/O storage. */
static size_t number(uint8_t *out,uint32_t value)
{
    uint8_t reverse[10];unsigned n=0;
    do{reverse[n++]=(uint8_t)('0'+value%10u);value/=10u;}while(value);
    for(unsigned i=0;i<n;i++)out[i]=reverse[n-i-1];
    return n;
}
/** @brief Match a bounded diagnostic command. */
static int equal(const uint8_t *data,size_t length,const char *word)
{ return length==strlen(word)&&!memcmp(data,word,length); }
/** @brief Execute diagnostic reads and explicit fault/reset commands. */
static size_t command(const uint8_t *data,size_t length,uint8_t out[24])
{
    const char *text=NULL;
    uint32_t value=0;uint8_t numeric=1;
    if(equal(data,length,"info")){text="TikuOS C6 resident IPC";numeric=0;}
    else if(equal(data,length,"help")){text="info test ticks dma";numeric=0;}
    else if(equal(data,length,"ticks"))value=native_ticks;
    else if(equal(data,length,"irq"))value=native_irqs;
    else if(equal(data,length,"proc"))value=tiku_process_count();
    else if(equal(data,length,"build"))value=TIKU_C6_BUILD;
#if TIKU_C6_STORAGE
    else if(equal(data,length,"storage"))value=storage_status;
    else if(equal(data,length,"nvmsaved"))value=tiku_c6_nvm_commits();
    else if(equal(data,length,"nvmrest"))value=tiku_mem_arch_nvm_restore_status();
    else if(equal(data,length,"held"))value=tiku_layout_state()->held;
    else if(equal(data,length,"nvmtier"))value=tiku_layout_state()->tier;
#endif
#if TIKU_C6_STORAGE_TEST
    else if(tiku_c6_storage_test_command(data,length,&value)){}
#endif
#if TIKU_C6_THREAD_TEST
    else if(equal(data,length,"thrtest"))value=tiku_c6_threads_test_result();
    else if(equal(data,length,"thrsoak"))value=tiku_c6_threads_test_soak();
    else if(length==5&&!memcmp(data,"thr ",4)&&data[4]>='0'&&data[4]<='9')
        value=tiku_c6_threads_test_observe(data[4]-'0');
#endif
#if TIKU_C6_FIRMWARE
    else if(equal(data,length,"memtest"))value=memory_ok;
    else if(equal(data,length,"memsize"))value=C6_ARENA_BYTES;
    else if(equal(data,length,"tiersize"))value=(uint32_t)(__tier_sram_end-__tier_sram_start);
    else if(equal(data,length,"workers"))value=(uint32_t)(__worker_reserve_end-__worker_reserve_start);
    else if(equal(data,length,"radio"))value=(uint32_t)(__radio_reserve_end-__radio_reserve_start);
#endif
    else if(equal(data,length,"bytes"))value=bytes;
    else if(equal(data,length,"crc"))value=~checksum;
    else if(equal(data,length,"bad"))value=bad+tiku_c6_diag_link_bad();
    else if(equal(data,length,"dup"))value=tiku_c6_diag_link_duplicates();
    else if(equal(data,length,"wready"))value=wire.ready;
    else if(length==5&&!memcmp(data,"dma ",4)&&data[4]>='0'&&data[4]<='5')
        value=tiku_c6_sdio_observe(data[4]-'0');
    else if(equal(data,length,"sinkwaits"))value=sink_waits;
    else if(equal(data,length,"buskhz"))value=tiku_c6_sdio_clock_khz();
    else if(equal(data,length,"worklo"))value=(uint32_t)work_cycles;
    else if(equal(data,length,"workhi"))value=(uint32_t)(work_cycles>>32);
    else if(equal(data,length,"stackfree")||equal(data,length,"irqfree")){
        const uint32_t *p=(const uint32_t *)(equal(data,length,"stackfree")?__native_stack_bottom:__native_irq_bottom);
        const uint32_t *end=(const uint32_t *)(equal(data,length,"stackfree")?__native_stack_top:__native_irq_top);
        if(equal(data,length,"irqfree"))p+=16;
        while(p<end&&*p++==0xa5a5a5a5u)value+=4;
    }
    else if(equal(data,length,"wbad"))value=wire.bad;
    else if(equal(data,length,"wstale"))value=wire.stale;
    else if(equal(data,length,"wfail"))value=wire.failed;
    else if(equal(data,length,"wdeliv"))value=wire.delivered;
    else if(equal(data,length,"bfailed"))value=bus.failed;
    else if(equal(data,length,"queued")||equal(data,length,"epopen")||equal(data,length,"sent")){
        tiku_ipc_status_t s;
        if(tiku_ipc_status(2,&s)==TIKU_IPC_OK)
            value=equal(data,length,"queued")?s.queued:equal(data,length,"epopen")?s.open:s.sent;
    }
    else if(equal(data,length,"test"))value=native_report[31];
    else if(equal(data,length,"clear")){
        bytes=bad=0;checksum=UINT32_MAX;reader=(tiku_ipc_stream_reader_t){0};
        fixture=object_mode=slow=0;next_accept=0;sink_waits=0;
    }
    else if(equal(data,length,"image")){fixture=1;object_mode=1;}
    else if(equal(data,length,"sensor")){fixture=2;object_mode=1;}
    else if(equal(data,length,"slow"))slow=1;
    else if(equal(data,length,"fault")){native_illegal_probe();text="fault recovered";numeric=0;}
    else if(equal(data,length,"reset")){((void (*)(void))0x40000090u)();}
    else if(equal(data,length,"hang")){
        __asm__ volatile("csrci mstatus,8":::"memory");for(;;){}
    }else if(length>=5&&!memcmp(data,"echo ",5)){
        memcpy(out,data+5,length-5);return length-5;
    }else if(length>=3&&data[0]=='r'&&data[1]==' '){
        unsigned index=0;
        for(size_t n=2;n<length;n++){
            if(data[n]<'0'||data[n]>'9'||index>=64){index=64;break;}
            index=index*10u+(unsigned)(data[n]-'0');
        }
        if(index<64)value=native_report[index];else{text="invalid report index";numeric=0;}
    }else{text="unknown command";numeric=0;}
    if(numeric)return number(out,value);
    size_t n=strlen(text);if(n>TIKU_SDIO_DIAG_RESPONSE)n=TIKU_SDIO_DIAG_RESPONSE;
    memcpy(out,text,n);return n;
}
/** @brief Serve the preserved native diagnostic mailbox through its whole-message link. */
static void diagnostic_receive(void *unused,uint8_t *data,size_t length)
{
    uint8_t out[24];(void)unused;
    uint64_t epoch=tiku_sdio_diag_epoch(data,length);
    if(epoch&&!tiku_c6_sdio_commit_epoch(epoch)){
        epoch_ack=1;return;
    }
    size_t n=command(data,length,out);(void)tiku_link_send(diagnostic,out,n);
}
/** @brief Accumulate a USB text line and use the diagnostic command vocabulary. */
static int text_receive(void *unused,int ch)
{
    (void)unused;
    if(ch=='\r')return 0;
    if(ch=='\n'){
        uint8_t out[24];size_t n=command(text_input,text_length,out);
        for(size_t i=0;i<n;i++)tiku_c6_console_io()->putc((char)out[i]);
        tiku_c6_console_io()->putc('\n');text_length=0;
    }else if(text_length<sizeof text_input)text_input[text_length++]=(uint8_t)ch;
    return 0;
}
TIKU_PROCESS(ipc_process,"C6 IPC");
TIKU_PROCESS_THREAD(ipc_process,ev,data)
{
    (void)ev;(void)data;
    TIKU_PROCESS_BEGIN();
    if(tiku_ipc_open("c6/bulk",&bulk)!=TIKU_IPC_OK)TIKU_PROCESS_EXIT();
    for(;;){
        tiku_timer_set_event(&app_timer,1);
        TIKU_PROCESS_WAIT_EVENT();
        if(!pending&&tiku_ipc_receive(bulk,&message)==TIKU_IPC_OK){
            if(message.is_buffer)(void)tiku_ipc_ack(bulk,message.submission,TIKU_IPC_UNSUPPORTED);
            else pending=1;
        }
        if(pending&&tiku_ipc_send(bulk,message.data,message.length)==TIKU_IPC_OK)pending=0;
    }
    TIKU_PROCESS_END();
}

uint32_t native_main(uint32_t *report)
{
    tiku_mem_arch_size_t count;
    const tiku_mem_region_t *table;
    tiku_c6_sdio_init();tiku_watchdog_kick();
    (void)tiku_c6_qualify(report);
#if TIKU_C6_FIRMWARE
    memory_ok=memory_check();if(!memory_ok)return 0;
#endif
#if TIKU_C6_STORAGE
    storage_status=tiku_c6_durable_guard_off()?5:tiku_c6_flash_init()?2:tiku_c6_nvm_init()?3:1;
    if(storage_status!=5){tiku_mpu_init();if(!tiku_c6_mpu_ready())storage_status=5;}
    if(storage_status==1&&tiku_c6_region_recover())storage_status=6;
    if(storage_status==1){
        tiku_persist_move_boot();if(tiku_persist_moved()<0)storage_status=4;
    }
    if(storage_status!=1)tiku_layout_restore_complete(-1);
#endif
#if TIKU_C6_STORAGE_TEST
    tiku_c6_storage_test_boot();
#endif
    table=tiku_region_arch_get_table(&count);
    if(tiku_region_init(table,count)!=TIKU_MEM_OK||
#if TIKU_C6_STORAGE
        tiku_region_claim(__native_image_start,(uint32_t)(__uninit_start-__native_image_start),0xe2)!=TIKU_MEM_OK||
        tiku_region_claim(__uninit_start,(uint32_t)(__uninit_end-__uninit_start),0xe2)!=TIKU_MEM_OK||
        (&__tier_sram_start[0]!=&__uninit_end[0]&&tiku_region_claim(__uninit_end,(uint32_t)(__tier_sram_start-__uninit_end),0xe2)!=TIKU_MEM_OK)||
#else
        tiku_region_claim(__native_image_start,(uint32_t)(__tier_sram_start-__native_image_start),0xe2)!=TIKU_MEM_OK||
#endif
        tiku_region_claim(__tier_sram_end,(uint32_t)(__native_stack_top-__tier_sram_end),0xe3)!=TIKU_MEM_OK||
        tiku_tier_init()!=TIKU_MEM_OK||tiku_ipc_init(endpoints,3)!=TIKU_IPC_OK)return 0;
#if TIKU_C6_STORAGE
    tiku_c6_region_seal();
#endif
    report[62]=TIKU_C6_BUILD;report[63]=0x52455331u;
#if TIKU_C6_FIRMWARE
    if(tiku_region_claim(__native_stack_top,4096,0xe4)!=TIKU_MEM_OK)return 0;
#endif
    diagnostic=tiku_c6_diag_link();tiku_link_on_recv(diagnostic,diagnostic_receive,NULL);
    carrier=tiku_ipc_bus_open(&bus,tiku_c6_sdio_io());if(!carrier)return 0;
    tiku_console_set_wire(tiku_c6_console_io());tiku_console_set_text_sink(text_receive,NULL);
    if(native_hardware_start())return 0;
    __asm__ volatile("csrsi mstatus,8":::"memory");
#if TIKU_C6_THREAD_TEST
    tiku_c6_threads_test_run();
#endif
    for(;;){
        uint64_t epoch=tiku_c6_sdio_epoch();
        tiku_watchdog_kick();tiku_link_pump(diagnostic);tiku_console_pump();
#if TIKU_C6_STORAGE_TEST
        tiku_c6_storage_test_pump();
#endif
        if(epoch&&epoch!=host_epoch){
            if(!host_epoch){
                if(tiku_ipc_wire_init(&wire,carrier,"c6/bulk",2,epoch,256,&sink)!=TIKU_IPC_OK)return 0;
                tiku_process_start(&ipc_process,NULL);
            }else{
                (void)tiku_ipc_wire_ops.reset(&wire,2);wire.epoch=epoch;
                tiku_process_exit(&ipc_process);pending=0;tiku_process_start(&ipc_process,NULL);
            }
            host_epoch=epoch;
        }
        uint32_t cycle=native_cycles();
        (void)tiku_sched_run_once();
        work_cycles+=(uint32_t)(native_cycles()-cycle);
#if TIKU_THREADS_ENABLE
        tiku_atomic_enter();
        if(!tiku_sched_has_pending()&&!tiku_ipc_pending()&&tiku_thread_worker_ready())
            tiku_thread_kernel_block();
        tiku_atomic_exit();
#endif
        if(epoch_ack&&bus.active&&host_epoch==tiku_c6_sdio_epoch()){
            tiku_ipc_status_t status;
            if(!tiku_ipc_status(2,&status)&&status.open&&!tiku_link_send(diagnostic,"IPCE",4))
                epoch_ack=0;
        }
        {
            uint32_t trace[5]={tiku_c6_sdio_observe(0),tiku_c6_sdio_observe(1),
                tiku_c6_sdio_observe(2),tiku_c6_sdio_observe(3),native_ticks};
            tiku_c6_diag_trace(trace);
        }
    }
}
