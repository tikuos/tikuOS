/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_spi.inl - C6 SPI1 user commands with preserved controller state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
static uint32_t spi_trace;
#define SPI_BASE 0x60003000u
#define SPI_CMD SPI_BASE
#define SPI_DATA (SPI_BASE+0x58u)
#ifndef C6_SPI_READ
#define C6_SPI_READ(at) C6_REG(at)
#define C6_SPI_WRITE(at,value) (C6_REG(at)=(value))
#endif

/** @brief Wait a bounded number of reads for an outstanding SPI1 command. */
static int spi_idle(void)
{
    for(unsigned n=0;n<65536;n++)if(!(C6_SPI_READ(SPI_CMD)&0xfffe0000u))return 0;
    spi_trace=0xf0000000u|C6_SPI_READ(SPI_CMD);return -1;
}
/** @brief Run one allowed single-line flash command while preserving idle registers.
 * @note Requires exclusive SPI1 ownership, cache suspension and masked interrupts.
 */
static int spi_command(unsigned command,uint32_t address,const uint8_t *src,uint8_t *out,unsigned n)
{
    static const uint32_t regs[]={4,8,0x14,0x18,0x1c,0x20,0x24,0x28};
    uint32_t saved[8],words[16];unsigned i;
    int input=command==0x05||command==0x35;
    int addressed=command==0x02||command==0x20;
    if(input){if(!out||n!=1||src||address)return -1;}
    else if(command==0x02){
        if(!src||out||!n||n>64||!writable(address,n)||n>256u-(address&255u))return -1;
    }else if(command==0x20){
        if(src||out||n||(address&4095u)||!writable(address,4096))return -1;
    }else if(command!=0x06||address||src||out||n)return -1;
    if(spi_idle())return -1;
    for(i=0;i<8;i++)saved[i]=C6_SPI_READ(SPI_BASE+regs[i]);
    for(i=0;i<16;i++)words[i]=C6_SPI_READ(SPI_DATA+4*i);
    C6_SPI_WRITE(SPI_BASE+8,saved[1]&~0x01906100u);
    C6_SPI_WRITE(SPI_BASE+0x14,0x00030103u);
    C6_SPI_WRITE(SPI_BASE+0x18,(1u<<31)|(addressed?1u<<30:0)|
        (input?1u<<28:0)|(src?1u<<27:0));
    C6_SPI_WRITE(SPI_BASE+0x1c,addressed?23u<<26:0);
    C6_SPI_WRITE(SPI_BASE+0x20,(7u<<28)|command);
    C6_SPI_WRITE(SPI_BASE+4,address);
    if(n)C6_SPI_WRITE(SPI_BASE+(input?0x28u:0x24u),n*8-1);
    for(i=0;src&&i<n;i+=4){
        uint32_t word=0;
        for(unsigned j=0;j<4&&i+j<n;j++)word|=(uint32_t)src[i+j]<<(8*j);
        C6_SPI_WRITE(SPI_DATA+i,word);
    }
    C6_SPI_WRITE(SPI_CMD,(1u<<18)|(addressed?1u<<17:0));
    if(spi_idle())return -1;
    if(input)*out=(uint8_t)C6_SPI_READ(SPI_DATA);
    for(i=0;i<8;i++)C6_SPI_WRITE(SPI_BASE+regs[i],saved[i]);
    for(i=0;i<16;i++)C6_SPI_WRITE(SPI_DATA+4*i,words[i]);
    return 0;
}
/** @brief Refuse a busy, write-enabled, suspended or globally protected chip. */
static int spi_writable(void)
{
    uint8_t low,high;
    if(spi_command(0x05,0,NULL,&low,1)||spi_command(0x35,0,NULL,&high,1))return -1;
    spi_trace=0x10000000u|low|(uint32_t)high<<8;
    return (low&0x1fu)||(high&0xc0u)?-1:0;
}
/** @brief Wait for WIP and WEL to clear within one second and a poll-count bound. */
static int spi_ready(void)
{
    uint64_t start=native_time();uint8_t status;
    for(unsigned n=0;n<200000;n++){
        if(spi_command(0x05,0,NULL,&status,1))return -1;
        spi_trace=0x30000000u|status;
        if(!(status&3u))return 0;
        if(native_time()-start>=16000000u)return -1;
        if(!(n&127u))tiku_watchdog_kick();
    }
    return -1;
}
/** @brief Enable and confirm one program or erase operation. */
static int spi_enable(void)
{
    uint8_t status;
    if(spi_writable()||spi_command(0x06,0,NULL,NULL,0)||
        spi_command(0x05,0,NULL,&status,1))return -1;
    spi_trace=0x20000000u|status;
    return (status&3u)==2u?0:-1;
}
/** @brief Program an aligned page through independent 64-byte user transactions. */
static int spi_program(uint32_t address,const void *data,int32_t length)
{
    const uint8_t *src=data;
    if(!src||length<=0||length>256||!writable(address,(uint32_t)length)||
        (uint32_t)length>256u-(address&255u))return -1;
    while(length){
        unsigned part=length>64?64:(unsigned)length;
        if(spi_enable()||spi_command(0x02,address,src,NULL,part)||spi_ready())return -1;
        src+=part;address+=part;length-=(int32_t)part;
    }
    return 0;
}
/** @brief Erase one reserved sector after confirming write enable. */
static int spi_erase(uint32_t sector)
{
    if(sector<TIKU_C6_FLASH_REGION_START/4096||sector>=TIKU_C6_FLASH_SIZE/4096||
        spi_enable()||spi_command(0x20,sector*4096,NULL,NULL,0)||spi_ready())return -1;
    return 0;
}
