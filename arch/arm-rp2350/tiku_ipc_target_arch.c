/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_target_arch.c - bounded SPI1/I2C1 FIFO IRQ packet targets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_target_arch.h"
#include "tiku_rp2350_regs.h"
#include <hal/tiku_cpu.h>
#ifndef TIKU_RP_IPC_READ
#define TIKU_RP_IPC_READ(a) _RP2350_REG(a)
#define TIKU_RP_IPC_WRITE(a,v) (_RP2350_REG(a)=(v))
#endif
#define READ(a) TIKU_RP_IPC_READ(a)
#define WRITE(a,v) TIKU_RP_IPC_WRITE(a,v)
#define SPI RP2350_SPI1_BASE
#define I2C RP2350_I2C1_BASE
#define SPI_IRQ 32u
#define I2C_IRQ 37u
#define I2C_EVENTS ((1u<<1)|(1u<<2)|(1u<<5)|(1u<<6)|(1u<<9))
static tiku_rp2350_ipc_target_t *spi_owner,*i2c_owner;
/** @brief Mask or clear the selected NVIC line, including IRQs above 31. */
static void interrupt_write(unsigned irq,uint32_t offset)
{WRITE(RP2350_NVIC_BASE+offset+4u*(irq/32u),1u<<(irq%32u));}
/** @brief Stop interrupt access before the common layer forgets borrowed staging. */
static uint8_t quiesce(void *context)
{
    tiku_rp2350_ipc_target_t *c=context;
    if(!c||!c->opened)return 0;
    tiku_atomic_enter();
    interrupt_write(c->spi?SPI_IRQ:I2C_IRQ,0x80);
    if(c->spi){WRITE(SPI+0x14,0);WRITE(SPI+4,4);}
    else{WRITE(I2C+0x30,0);WRITE(I2C+0x6c,0);}
    interrupt_write(c->spi?SPI_IRQ:I2C_IRQ,0x180);
    tiku_atomic_exit();return 1;
}
/** @brief Enable one previously disabled peripheral with a bounded reset-done wait. */
static int reset_peripheral(uint32_t bit)
{
    WRITE(RP2350_RESETS_RESET+RP2350_REG_ALIAS_SET,bit);
    WRITE(RP2350_RESETS_RESET+RP2350_REG_ALIAS_CLR,bit);
    for(unsigned n=0;n<4096;n++)if(READ(RP2350_RESETS_RESET_DONE)&bit)return 0;
    return -1;
}
/** @brief Configure the exclusively owned peripheral without changing its pad lease. */
static void configure(tiku_rp2350_ipc_target_t *c)
{
    if(c->spi){
        WRITE(SPI+4,4);WRITE(SPI,7);WRITE(SPI+0x10,2);
        WRITE(SPI+0x14,0);WRITE(SPI+0x20,3);WRITE(SPI+0x24,0);
    }else{
        WRITE(I2C+0x6c,0);WRITE(I2C,1u<<1);
        WRITE(I2C+0x38,0);WRITE(I2C+0x3c,0);WRITE(I2C+0x7c,3);
        (void)READ(I2C+0x40);WRITE(I2C+0x30,0);
    }
}
/** @brief Drain FIFO input and refill output while preserving the staged packet. */
static void spi_step(tiku_rp2350_ipc_target_t *c)
{
    uint32_t events=READ(SPI+0x18);
    if(events&1u)c->target.fault=1;
    for(unsigned n=0;n<8&&(READ(SPI+0x0c)&4u);n++)
        (void)tiku_ipc_target_write(&c->target,(uint8_t)READ(SPI+8));
    for(unsigned n=0;n<8&&c->target.sent<c->target.capacity&&(READ(SPI+0x0c)&2u);n++)
        WRITE(SPI+8,tiku_ipc_target_read(&c->target));
    WRITE(SPI+0x20,3);
    if(c->target.sent==c->target.capacity)WRITE(SPI+0x14,7);
    if((READ(RP2350_SIO_GPIO_IN)&(1u<<9))&&c->target.received&&!(READ(SPI+0x0c)&16u)){
        tiku_ipc_target_end(&c->target);c->stopped=1;
    }
}
void tiku_rp2350_spi1_isr(void)
{
    tiku_rp2350_ipc_target_t *c=spi_owner;
    if(c&&c->opened&&c->target.armed)spi_step(c);
    else{WRITE(SPI+0x14,0);WRITE(SPI+0x20,3);}
}
/** @brief Service slave writes, clock-stretched reads and a complete packet STOP. */
static void i2c_step(tiku_rp2350_ipc_target_t *c)
{
    uint32_t events=READ(I2C+0x34);
    if(events&((1u<<1)|(1u<<6)))c->target.fault=1;
    for(unsigned n=0;n<16&&READ(I2C+0x78);n++)
        (void)tiku_ipc_target_write(&c->target,(uint8_t)READ(I2C+0x10));
    if(events&(1u<<5)){
        WRITE(I2C+0x10,tiku_ipc_target_read(&c->target));
        (void)READ(I2C+0x50);
    }
    if(events&(1u<<6))(void)READ(I2C+0x54);
    if(events&(1u<<9)){
        (void)READ(I2C+0x60);tiku_ipc_target_end(&c->target);c->stopped=1;
    }
}
void tiku_rp2350_i2c1_isr(void)
{
    tiku_rp2350_ipc_target_t *c=i2c_owner;
    if(c&&c->opened&&c->target.armed)i2c_step(c);
    else{WRITE(I2C+0x30,0);(void)READ(I2C+0x40);}
}
/** @brief Validate CPU staging and arm the target before permitting bus interrupts. */
static tiku_ipc_err_t start(void *context,const void *tx,void *rx,uint16_t length)
{
    tiku_rp2350_ipc_target_t *c=context;tiku_ipc_err_t result;
    if(!c||!c->opened||length!=TIKU_IPC_PACKET_SLOT||
        tiku_ipc_domain_check(c->io.domain,0,c->io.peer,tx,length,TIKU_IPC_INPUT)||
        tiku_ipc_domain_check(c->io.domain,0,c->io.peer,rx,length,TIKU_IPC_OUTPUT))return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    if(c->target.armed){tiku_atomic_exit();return TIKU_IPC_AGAIN;}
    if(c->spi&&(!(READ(RP2350_SIO_GPIO_IN)&(1u<<9))||(READ(SPI+0x0c)&16u))){
        tiku_atomic_exit();return TIKU_IPC_AGAIN;
    }
    if(c->spi&&reset_peripheral(RP2350_RESETS_SPI1)){
        tiku_atomic_exit();return TIKU_IPC_IO;
    }
    configure(c);c->stopped=0;
    result=tiku_ipc_target_start(&c->target,tx,rx,length);
    if(!result){
        if(c->spi){
            for(unsigned n=0;n<8;n++)WRITE(SPI+8,tiku_ipc_target_read(&c->target));
            WRITE(SPI+4,6);WRITE(SPI+0x14,15);
        }else{WRITE(I2C+0x6c,1);WRITE(I2C+0x30,I2C_EVENTS);}
        interrupt_write(c->spi?SPI_IRQ:I2C_IRQ,0x180);
        interrupt_write(c->spi?SPI_IRQ:I2C_IRQ,0);
    }
    tiku_atomic_exit();return result;
}
/** @brief Require STOP/CS release and idle hardware before completed-stage reuse. */
static tiku_ipc_err_t poll(void *context)
{
    tiku_rp2350_ipc_target_t *c=context;tiku_ipc_err_t result;
    if(!c||!c->opened)return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    if(c->spi)spi_step(c);else i2c_step(c);
    if(c->target.fault)result=TIKU_IPC_IO;
    else if(!c->stopped||(c->spi?(READ(SPI+0x0c)&16u):(READ(I2C+0x70)&1u)))result=TIKU_IPC_AGAIN;
    else{
        (void)quiesce(c);result=tiku_ipc_target_poll(&c->target);
    }
    tiku_atomic_exit();return result;
}
/** @brief Clear staging associations after the port has masked CPU access. */
static uint8_t abort_transfer(void *context)
{
    tiku_rp2350_ipc_target_t *c=context;
    return c?tiku_ipc_target_abort(&c->target):0;
}
/** @brief Distinguish an armed receiver from observed controller activity. */
static uint8_t listening(void *context)
{
    tiku_rp2350_ipc_target_t *c=context;
    if(!c||!c->target.armed||c->target.received||c->stopped)return 0;
    return c->spi?!!(READ(RP2350_SIO_GPIO_IN)&(1u<<9)):
        !c->target.sent&&!(READ(I2C+0x70)&1u);
}
/** @brief Check exclusive hardware ownership and lease the fixed target pads. */
static int open_target(tiku_rp2350_ipc_target_t *c,const tiku_ipc_access_domain_t *domain,
    uint32_t peer,tiku_clock_time_t burst,uint8_t spi,uint8_t address)
{
    unsigned irq=spi?SPI_IRQ:I2C_IRQ,first=spi?8:6,pins=spi?4:2;
    uint32_t bit=spi?RP2350_RESETS_SPI1:RP2350_RESETS_I2C1;
    if(!c||c->opened||!domain||domain->engine||domain->peer!=peer||!burst||burst>TIKU_CLOCK_MAX_INTERVAL||
        (spi?spi_owner!=NULL:i2c_owner!=NULL)||(!spi&&(address<8||address>0x77)))return -1;
    if((READ(RP2350_NVIC_ISER0+4*(irq/32u))&(1u<<(irq%32u)))||
        (spi?(READ(SPI+4)&2u):(READ(I2C+0x6c)&1u)))return -1;
    for(unsigned n=0;n<pins;n++){
        uint32_t route=READ(RP2350_IO_BANK0_GPIO_CTRL(first+n));
        if((route&31u)!=31u&&(route&31u)!=5u)return -1;
    }
    c->reset=READ(RP2350_RESETS_RESET)&bit;
    if(reset_peripheral(bit))return -1;
    c->spi=spi;c->opened=1;c->stopped=0;
    c->target=(tiku_ipc_target_t){.spi=spi,.quiesce=quiesce,.port=c};
    c->io=(tiku_ipc_packet_io_t){start,poll,abort_transfer,c,domain,0,peer,burst,listening};
    for(unsigned n=0;n<pins;n++){
        c->pads[n]=READ(RP2350_PADS_BANK0_GPIO(first+n));
        c->routes[n]=READ(RP2350_IO_BANK0_GPIO_CTRL(first+n));
        WRITE(RP2350_PADS_BANK0_GPIO(first+n),RP2350_PADS_IE|RP2350_PADS_DRIVE_4MA);
        WRITE(RP2350_IO_BANK0_GPIO_CTRL(first+n),spi?1u:3u);
    }
    configure(c);
    if(spi)spi_owner=c;else{WRITE(I2C+8,address);i2c_owner=c;}
    return 0;
}
int tiku_rp2350_ipc_spi_target_open(tiku_rp2350_ipc_target_t *c,
    const tiku_ipc_access_domain_t *d,uint32_t peer,tiku_clock_time_t burst)
{return open_target(c,d,peer,burst,1,0);}
int tiku_rp2350_ipc_i2c_target_open(tiku_rp2350_ipc_target_t *c,
    const tiku_ipc_access_domain_t *d,uint32_t peer,tiku_clock_time_t burst,uint8_t address)
{return open_target(c,d,peer,burst,0,address);}
void tiku_rp2350_ipc_target_close(tiku_rp2350_ipc_target_t *c)
{
    unsigned first,pins;uint32_t bit;
    if(!c||!c->opened)return;
    tiku_atomic_enter();(void)tiku_ipc_target_abort(&c->target);
    first=c->spi?8:6;pins=c->spi?4:2;bit=c->spi?RP2350_RESETS_SPI1:RP2350_RESETS_I2C1;
    for(unsigned n=0;n<pins;n++){
        WRITE(RP2350_PADS_BANK0_GPIO(first+n),c->pads[n]);
        WRITE(RP2350_IO_BANK0_GPIO_CTRL(first+n),c->routes[n]);
    }
    if(c->reset)WRITE(RP2350_RESETS_RESET+RP2350_REG_ALIAS_SET,bit);
    if(c->spi)spi_owner=NULL;else i2c_owner=NULL;
    c->opened=0;tiku_atomic_exit();
}
