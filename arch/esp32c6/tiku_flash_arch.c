/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_arch.c - C6 mapped reads and bounded SPI1 tail writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_flash_arch.h"
#include "tiku_native_arch.h"
#include <kernel/memory/tiku_nvm_mirror.h>
#include <hal/tiku_mem_hal.h>
#include <string.h>
#if TIKU_C6_STORAGE_TEST
#include <TikuBench/tests/esp32c6/test_storage.h>
#define FLASH_TEST_STEP() tiku_c6_storage_test_flash_step()
#else
#define FLASH_TEST_STEP() ((void)0)
#endif
#if TIKU_SHELL_ENABLE
extern void tiku_c6_control_progress(void);
#define FLASH_STEP() do { FLASH_TEST_STEP(); tiku_c6_control_progress(); } while(0)
#else
#define FLASH_STEP() FLASH_TEST_STEP()
#endif

/* C6 rev 0.2 cache, mapping and boot-read ROM addresses at ESP-IDF 4d59230.
 * Program and erase use preserved SPI1 user-command registers. */
#ifndef ROM_SUSPEND
#define ROM_SUSPEND ((uint32_t (*)(void))0x40000698u)
#define ROM_RESUME ((void (*)(uint32_t))0x4000069cu)
#define ROM_INVALIDATE ((int (*)(uint32_t,uint32_t))0x40000648u)
#define ROM_READ ((int (*)(uint32_t,void *,int32_t))0x40000134u)
#define SPI_PROGRAM spi_program
#define SPI_ERASE spi_erase
#define SPI_WRITABLE spi_writable
#define SPI_UNLOCK ((int (*)(void))0x40000138u)
#define ROM_MMU_SIZE ((void (*)(uint32_t,uint32_t))0x400006a8u)
#define ROM_MMU_INIT ((void (*)(void))0x400006b4u)
#define ROM_MAP ((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))0x400006b8u)
#define ROM_CACHE_ENABLE ((void (*)(uint32_t))0x40000694u)
#endif
static uint32_t identity;
static uint8_t busy,poisoned;
static uint32_t stage[64];

/** @brief Check subtraction-safe physical bounds against the writable reservation. */
static int writable(uint32_t off,uint32_t length)
{
    return off>=TIKU_C6_FLASH_REGION_START&&off<=TIKU_C6_FLASH_SIZE&&
        length<=TIKU_C6_FLASH_SIZE-off;
}
#ifndef C6_FLASH_HOST
#include "tiku_flash_spi.inl"
#endif
/** @brief Refuse source or destination storage outside the native SRAM arena. */
static int sram(const void *p,uint32_t length)
{
    uintptr_t at=(uintptr_t)p,start=(uintptr_t)__native_image_start;
    return at>=start&&at<=start+C6_ARENA_BYTES&&length<=start+C6_ARENA_BYTES-at;
}
/** @brief Serialize the controller while preserving the caller's interrupt mask. */
static int acquire(void)
{
    uint32_t state=native_irq_save();int ok=!busy&&!TIKU_MEM_ARCH_IN_EXCEPTION()&&identity;
    if(ok)busy=1;
    native_irq_restore(state);return ok;
}
/** @brief Release controller ownership after restoring the cache. */
static void release(void)
{
    uint32_t state=native_irq_save();busy=0;native_irq_restore(state);
}
int tiku_c6_flash_init(void)
{
    uint32_t state=native_irq_save(),word=0,cache,chip=C6_REG(0x4087ffecu);
    int rc=-1;
    if(identity){native_irq_restore(state);return 0;}
    if(poisoned)goto done;
    if(TIKU_MEM_ARCH_IN_EXCEPTION()||chip<0x4086e000u||chip>0x40880000u-24u)goto done;
    if(C6_REG(chip+4)!=TIKU_C6_FLASH_SIZE||C6_REG(chip+12)!=4096||C6_REG(chip+16)!=256)goto done;
    cache=ROM_SUSPEND();rc=ROM_READ(0,&word,4);ROM_RESUME(cache);
    if(rc||word!=0x200203e9u){rc=-1;goto done;}
    ROM_MMU_SIZE(0x200,0x200);
    ROM_MMU_INIT();
    rc=ROM_MAP(0,0,TIKU_C6_FLASH_VIRTUAL,0,64,64,0);
    ROM_CACHE_ENABLE(0);
    if(rc||ROM_INVALIDATE(TIKU_C6_FLASH_VIRTUAL,TIKU_C6_FLASH_SIZE)||C6_REG(TIKU_C6_FLASH_VIRTUAL)!=word){rc=-1;goto done;}
    const uint32_t *owner=(const uint32_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+0x310000u);
    if(owner[0]!=0x364e574fu||owner[1]!=1||owner[2]!=0x310000u||owner[3]!=0x21000u||
        tiku_nvm_crc32(owner,60)!=owner[15]){rc=-1;goto done;}
    cache=ROM_SUSPEND();rc=SPI_UNLOCK();ROM_RESUME(cache);
    if(rc)goto done;
    identity=C6_REG(chip);rc=identity?0:-1;
done:
    native_irq_restore(state);return rc;
}
uint32_t tiku_c6_flash_jedec(void){return identity;}
int tiku_c6_flash_read(uint32_t off,void *data,uint32_t length)
{
    if(off>TIKU_C6_FLASH_SIZE||length>TIKU_C6_FLASH_SIZE-off||!sram(data,length)||!acquire())return -1;
    memcpy(data,(const void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+off),length);release();return 0;
}
int tiku_c6_flash_program(uint32_t off,const void *data,uint32_t length)
{
    const uint8_t *src=data;int rc=0;
    if(!writable(off,length)||!sram(data,length)||!acquire())return -1;
    const uint8_t *old=(const uint8_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+off);
    for(uint32_t n=0;n<length;n++)if((old[n]&src[n])!=src[n]){release();return -1;}
    while(length){
        uint32_t aligned=off&~3u,within=off-aligned,part=256u-(off&255u);
        if(part>length)part=length;
        if(!memcmp((const void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+off),src,part)){
            off+=part;src+=part;length-=part;continue;
        }
        uint32_t bytes=(within+part+3u)&~3u;
        memcpy(stage,(const void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+aligned),bytes);
        memcpy((uint8_t *)stage+within,src,part);
        uint32_t state=native_irq_save(),cache=ROM_SUSPEND();
        rc=SPI_WRITABLE();if(!rc)rc=SPI_PROGRAM(aligned,stage,(int32_t)bytes);
        ROM_RESUME(cache);
        if(ROM_INVALIDATE(TIKU_C6_FLASH_VIRTUAL+(aligned&~4095u),4096))rc=-1;
        native_irq_restore(state);
        if(rc||memcmp((const void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+off),src,part)){
            identity=0;poisoned=1;rc=-1;break;
        }
        off+=part;src+=part;length-=part;tiku_watchdog_kick();FLASH_STEP();
    }
    release();return rc;
}
int tiku_c6_flash_erase_sector(uint32_t off)
{
    int rc;
    if((off&4095u)||!writable(off,4096)||!acquire())return -1;
    uint32_t state=native_irq_save(),cache=ROM_SUSPEND();
    rc=SPI_WRITABLE();if(!rc)rc=SPI_ERASE(off/4096);
    ROM_RESUME(cache);
    if(ROM_INVALIDATE(TIKU_C6_FLASH_VIRTUAL+off,4096))rc=-1;
    native_irq_restore(state);
    const uint32_t *p=(const uint32_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+off);
    for(unsigned n=0;!rc&&n<1024;n++)if(p[n]!=UINT32_MAX)rc=-1;
    if(rc){identity=0;poisoned=1;}
    release();tiku_watchdog_kick();if(!rc)FLASH_STEP();return rc;
}

#if TIKU_C6_STORAGE_TEST
uint32_t tiku_c6_flash_trace(void){return spi_trace;}
#endif
