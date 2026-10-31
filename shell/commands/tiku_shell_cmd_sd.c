/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sd.c - Card capacity, partition table and filesystem headers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <shell/tiku_shell_config.h>
#if TIKU_SHELL_CMD_SD
#include "tiku_shell_cmd_sd.h"
#include <shell/tiku_shell_io.h>
#include <arch/esp32p4/tiku_sdcard_arch.h>
#include <kernel/fs/tiku_media_probe.h>
#include <boot/tiku_boot_c6.h>
#include <string.h>

/** @brief Format an unsigned 64-bit count for the shell's bounded formatter. */
static const char *decimal(uint64_t value,char buffer[21])
{
    memset(buffer,'0',20);buffer[20]=0;
    for(unsigned bit=64;bit>0;bit--){
        unsigned carry=(unsigned)((value>>(bit-1))&1u);
        for(unsigned i=20;i>0;i--){
            unsigned digit=2u*(unsigned)(buffer[i-1]-'0')+carry;
            carry=digit>=10;buffer[i-1]=(char)('0'+digit-10*carry);
        }
    }
    char *p=buffer;while(p<buffer+19&&*p=='0')p++;
    return p;
}

void tiku_shell_cmd_sd(uint8_t argc,const char *argv[])
{
    tiku_p4_sdcard_t card;
    tiku_media_probe_t media;
    char a[21],b[21],c[21];
    if(argc!=2||strcmp(argv[1],"probe")){SHELL_PRINTF("usage: sd probe\n");return;}
    tiku_ipc_err_t result=tiku_p4_sdcard_open(&card);
    /* Complete pending radio transfers through their owner before leasing slot0. */
    for(unsigned n=0;result==TIKU_IPC_BUSY&&n<128;n++){
        tiku_ipc_sdio_t *link=tiku_boot_c6_owner();
        if(!link||link->state!=TIKU_SDIO_READY)break;
        tiku_ipc_sdio_step(link);
        result=tiku_p4_sdcard_open(&card);
    }
    if(result==TIKU_IPC_BUSY){SHELL_PRINTF("sd: controller busy; retry sd probe\n");return;}
    int parsed=result?TIKU_MEDIA_IO:tiku_media_probe(&media,card.sectors,tiku_p4_sdcard_read,&card);
    tiku_ipc_err_t closed=tiku_p4_sdcard_close();
    if(result){
        SHELL_PRINTF("sd: identification result=%d cmd=%u raw=%08lx response=%08lx restore=%d\n",
            result,card.command,(unsigned long)card.raw,(unsigned long)card.response,closed);return;
    }
    SHELL_PRINTF("sd: SDHC/SDXC slot=0 clock=400000 width=1 read-only\n");
    SHELL_PRINTF("  sectors=%s sector_bytes=512 capacity_bytes=%s MiB=%s\n",
        decimal(card.sectors,a),decimal(card.sectors*512,b),decimal(card.sectors/2048,c));
    SHELL_PRINTF("  CID=%08lx%08lx%08lx%08lx CSD=%08lx%08lx%08lx%08lx\n",
        (unsigned long)card.cid[3],(unsigned long)card.cid[2],(unsigned long)card.cid[1],(unsigned long)card.cid[0],
        (unsigned long)card.csd[3],(unsigned long)card.csd[2],(unsigned long)card.csd[1],(unsigned long)card.csd[0]);
    const char *table=media.table==TIKU_MEDIA_GPT?"GPT":media.table==TIKU_MEDIA_MBR?"MBR":
        media.table==TIKU_MEDIA_WHOLE?"whole-device":"none";
    SHELL_PRINTF("  partition_table=%s result=%d partitions=%u reads=%lu restore=%d\n",
        table,parsed,media.count,(unsigned long)card.reads,closed);
    SHELL_PRINTF("  last_cmd=%u raw=%08lx response=%08lx\n",card.command,
        (unsigned long)card.raw,(unsigned long)card.response);
    SHELL_PRINTF("  status=%08lx dma=%08lx transferred=%lu elapsed_us=%lu\n",
        (unsigned long)card.status,(unsigned long)card.dma,
        (unsigned long)card.transferred,(unsigned long)card.elapsed_us);
    if(parsed){SHELL_PRINTF("  partition scan incomplete or invalid; no filesystem mounted\n");return;}
    for(unsigned i=0;i<media.count;i++){
        const tiku_media_partition_t *p=&media.parts[i];
        SHELL_PRINTF("  partition=%u type=%02x boot=%u first_lba=%s sectors=%s\n",
            p->index,p->type,p->boot,decimal(p->first,a),decimal(p->sectors,b));
        SHELL_PRINTF("    filesystem=%s header_geometry=%s sector_bytes=%u cluster_bytes=%lu volume_sectors=%s\n",
            tiku_media_fs_name(p->fs),p->geometry_valid?"valid":"unverified",p->sector_bytes,
            (unsigned long)p->cluster_bytes,decimal(p->volume_sectors,c));
    }
}
#endif
