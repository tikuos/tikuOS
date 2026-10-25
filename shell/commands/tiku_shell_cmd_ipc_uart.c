/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ipc_uart.c - independent UART frames over hardware loopback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_cmd_ipc_uart.h"
#include <shell/tiku_shell.h>
#include <arch/esp32p4/tiku_uart_arch.h>
#include <arch/esp32p4/tiku_uart_port_arch.h>
#include <services/link/tiku_link_uart.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/cpu/tiku_hang.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>
#if (TIKU_HAS_IPC_UART + 0)
/** @brief Receiver counters for a finite loopback profile. */
typedef struct {unsigned received,bad;} observation_t;
/** @brief Compare a complete decoded message before the UART buffer is reused. */
static void received(void *context,uint8_t *data,size_t length)
{
    observation_t *c=context;
    if(length!=256||data[0]!=(uint8_t)c->received){c->bad++;return;}
    for(unsigned n=1;n<256;n++)if(data[n]!=(uint8_t)n){c->bad++;return;}
    c->received++;
}
void tiku_shell_ipc_uart(void)
{
    tiku_link_uart_t uart={0};observation_t results={0};uint8_t data[256];
    unsigned sent=0;tiku_link_t *link;tiku_p4_uart_stats_t stats;
    tiku_clock_time_t began=tiku_clock_time();
    if(!(tiku_vfs_caller_cap_get()&TIKU_VFS_CAP_SYS)||tiku_p4_uart_start(115200,1)){
        SHELL_PRINTF("IPC UART FAIL ownership or start\n");return;
    }
    link=tiku_link_uart_open(&uart,tiku_p4_uart_port_io(),0xf4);
    if(!link){tiku_uart_init();SHELL_PRINTF("IPC UART FAIL link\n");return;}
    tiku_link_on_recv(link,received,&results);
    for(unsigned n=0;n<256;n++)data[n]=(uint8_t)n;
    while(results.received<16&&!results.bad&&(tiku_clock_time_t)(tiku_clock_time()-began)<640){
        tiku_link_pump(link);
        if(sent==results.received&&sent<16){
            data[0]=(uint8_t)sent;if(!tiku_link_send(link,data,256))sent++;
        }
        (void)tiku_sched_run_once_except(TIKU_THIS());tiku_watchdog_kick();tiku_hang_checkin();
    }
    tiku_p4_uart_stats(&stats);tiku_link_close(link);tiku_uart_init();
    SHELL_PRINTF("IPC UART baud=115200 messages=%u bytes=%u ticks=%lu bad=%u overrun=%u rx_errors=%u dropped=%lu pass=%u\n",
        results.received,results.received*256,(unsigned long)(tiku_clock_time()-began),results.bad,
        stats.overruns,stats.rx_errors,(unsigned long)stats.tx_dropped,
        results.received==16&&!results.bad&&!stats.overruns&&!stats.rx_errors&&!stats.tx_dropped);
}
#endif
