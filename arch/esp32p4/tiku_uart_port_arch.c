/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_port_arch.c - bounded FIFO writes and interrupt-buffered UART0 reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_uart_port_arch.h"
#include "tiku_uart_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
/** @brief Drain at most 32 bytes from the existing interrupt-buffered UART. */
static int read_bytes(void *unused,uint8_t *out,size_t length)
{
    size_t n=0;(void)unused;
    if(!tiku_p4_uart_ready()||(!out&&length))return -1;
    if(length>32)length=32;
    while(n<length){int ch=tiku_uart_getc();if(ch<0)break;out[n++]=(uint8_t)ch;}
    return (int)n;
}
/** @brief Insert at most 32 bytes; a full TX FIFO returns partial progress. */
static int write_bytes(void *unused,const uint8_t *data,size_t length)
{
    size_t n=0;(void)unused;
    if(!tiku_p4_uart_ready()||(!data&&length))return -1;
    if(length>32)length=32;
    while(n<length){
        uint32_t saved=TIKU_P4_IRQ_SAVE();
        if(((TIKU_P4_REG_READ(0x500ca01cu)>>16)&255u)>=128){
            TIKU_P4_IRQ_RESTORE(saved);break;
        }
        TIKU_P4_REG_WRITE(0x500ca000u,data[n++]);TIKU_P4_IRQ_RESTORE(saved);
    }
    return (int)n;
}
static const tiku_uart_port_io_t io={read_bytes,write_bytes,NULL};
const tiku_uart_port_io_t *tiku_p4_uart_port_io(void){return &io;}
