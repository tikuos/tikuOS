/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console_arch.c - native C6 USB Serial/JTAG FIFO with bounded output.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include "tiku_console_arch.h"
static unsigned drops;
/** @brief Offer one byte to the hardware FIFO without waiting for an external host. */
static void putc_byte(char ch)
{
    if(C6_REG(0x6000f004u)&2u){C6_REG(0x6000f000u)=(uint8_t)ch;C6_REG(0x6000f004u)=1;}
    else if(drops!=UINT32_MAX)drops++;
}
/** @brief Return the hardware FIFO's receive-available bit. */
static uint8_t ready(void){return (C6_REG(0x6000f004u)&4u)!=0;}
/** @brief Read one available byte, or -1 when the FIFO is empty. */
static int getc_byte(void){return ready()?(int)(C6_REG(0x6000f000u)&255u):-1;}
static const tiku_console_wire_t io={.putc=putc_byte,.rx_ready=ready,.getc=getc_byte,.escape_nul=1};
const tiku_console_wire_t *tiku_c6_console_io(void){return &io;}
unsigned tiku_c6_console_drops(void){return drops;}

void tiku_uart_putc(char ch){putc_byte(ch);}
uint8_t tiku_uart_rx_ready(void){return ready();}
int tiku_uart_getc(void){return getc_byte();}
