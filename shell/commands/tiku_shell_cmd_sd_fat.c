/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sd_fat.c - Bounded FAT32 file reads through the P4 SD card.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <shell/tiku_shell_config.h>
#if TIKU_SHELL_CMD_FAT && (TIKU_P4_SDCARD_ENABLE + 0)
#include "tiku_shell_cmd_fat.h"
#include <shell/tiku_shell_io.h>
#include <services/storage/tiku_sd_fat.h>
#include <string.h>

#define READ_MAX TIKU_SD_FAT_READ_MAX
#define LIST_MAX TIKU_SD_FAT_LIST_MAX
#define SKIP_MAX TIKU_SD_FAT_SKIP_MAX

/** @brief Parse a nonempty decimal value with checked 32-bit arithmetic. */
static int number(const char *s,uint32_t *value)
{
    uint32_t v=0;
    if(!s||!*s)return 0;
    for(;*s;s++){
        if(*s<'0'||*s>'9'||v>(UINT32_MAX-(unsigned)(*s-'0'))/10)return 0;
        v=v*10+(unsigned)(*s-'0');
    }
    *value=v;return 1;
}
/** @brief Print control bytes as hexadecimal escapes in names and text output. */
static void escaped(const char *s,unsigned count,uint8_t text)
{
    for(unsigned i=0;i<count;i++){
        unsigned char c=(unsigned char)s[i];
        if((c>=32&&c<=126)||(text&&(c=='\n'||c=='\t')))tiku_shell_io_putc((char)c);
        else SHELL_PRINTF("\\x%02x",c);
    }
}

/** @brief Print operation failures and controller restoration diagnostics. */
static void failure(int rc,const tiku_sd_fat_info_t *info)
{
    if(info->close_status)SHELL_PRINTF("fat: controller restore failed (%d); reboot required\n",info->close_status);
    else if(rc==TIKU_VFS_EBUSY)SHELL_PRINTF("fat: controller busy; retry command\n");
    else if(info->open_status)SHELL_PRINTF("fat: card identification failed (%d)\n",info->open_status);
    else if(info->limited)SHELL_PRINTF("fat: command read budget reached; operation incomplete\n");
    else if(info->probe_status)SHELL_PRINTF("fat: partition probe failed (%d)\n",info->probe_status);
    else if(rc==TIKU_VFS_ENOTSUP)SHELL_PRINTF("fat: FAT32 volume required; detected %s; card unchanged\n",
        tiku_media_fs_name(info->detected));
    else SHELL_PRINTF("fat: %s\n",tiku_fat_strerror(info->fat_status));
}
/** @brief Print a directory page after its SD controller lease has ended. */
static void list(const char *path,uint32_t skip)
{
    tiku_fat_dirent_t entries[LIST_MAX];uint32_t count=0;uint8_t more=0;
    tiku_sd_fat_info_t info;
    int rc=tiku_sd_fat_list(path,skip,entries,LIST_MAX,&count,&more,&info);
    if(rc){failure(rc,&info);return;}
    for(unsigned i=0;i<count;i++){
        const tiku_fat_dirent_t *entry=&entries[i];
        SHELL_PRINTF("%s %lu ",entry->is_dir?"dir":"file",(unsigned long)entry->size);
        escaped(entry->name,(unsigned)strlen(entry->name),0);SHELL_PRINTF("\n");
    }
    if(more)SHELL_PRINTF("fat ls: page full; next entry %lu\n",(unsigned long)(skip+count));
    else SHELL_PRINTF("fat ls: end\n");
}
/** @brief Print a file window after successful controller restoration. */
static void read_file(const char *path,uint32_t offset,uint32_t length,uint8_t text)
{
    uint8_t data[READ_MAX];uint32_t size=0;tiku_sd_fat_info_t info;
    int got=tiku_sd_fat_read_at(path,offset,data,length,&size,&info);
    if(got<0){failure(got,&info);return;}
    SHELL_PRINTF("fat read: offset=%lu bytes=%lu size=%lu\n",(unsigned long)offset,
        (unsigned long)got,(unsigned long)size);
    if(text){escaped((const char *)data,(unsigned)got,1);SHELL_PRINTF("\n");}
    else for(unsigned i=0;i<(unsigned)got;i++)
        SHELL_PRINTF("%02x%s",data[i],(i%16==15||i+1==(unsigned)got)?"\n":" ");
    if(offset<size&&(uint32_t)got<size-offset)
        SHELL_PRINTF("fat read: next offset=%lu\n",(unsigned long)(offset+(uint32_t)got));
}

void tiku_shell_cmd_fat(uint8_t argc,const char *argv[])
{
    uint32_t offset=0,length=READ_MAX;
    uint8_t mount=argc==2&&!strcmp(argv[1],"mount");
    uint8_t listing=argc>=2&&argc<=4&&!strcmp(argv[1],"ls");
    uint8_t reading=argc>=3&&argc<=5&&(!strcmp(argv[1],"read")||!strcmp(argv[1],"cat"));
    if((!mount&&!listing&&!reading)||
       (listing&&argc==4&&(!number(argv[3],&offset)||offset>SKIP_MAX))||
       (reading&&argc>=4&&!number(argv[3],&offset))||
       (reading&&argc==5&&(!number(argv[4],&length)||!length||length>READ_MAX))){
        SHELL_PRINTF("usage: fat mount | ls [path [entry]] | read|cat <path> [offset [1..512 bytes]]\n");return;
    }
    if(listing){list(argc>=3?argv[2]:"/",offset);return;}
    if(reading){read_file(argv[2],offset,length,!strcmp(argv[1],"cat"));return;}
    tiku_sd_fat_info_t info;int rc=tiku_sd_fat_info(&info);
    if(rc){failure(rc,&info);return;}
    SHELL_PRINTF("fat: FAT32 read-only; first_lba=%lu sectors=%lu cluster_bytes=%lu\n",
        (unsigned long)info.first,(unsigned long)info.sectors,(unsigned long)info.cluster_bytes);
}
#endif
