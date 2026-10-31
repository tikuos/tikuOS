/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sd_vfs.c - Bounded FAT32 file windows and directory listings under /sd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sd_vfs.h"
#include "tiku_sd_fat.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static tiku_sd_fat_info_t last_info;
static int last_result=TIKU_VFS_ENOTSUP;

/** @brief Store diagnostics after the operation and any listing callbacks finish. */
static int record(int rc,const tiku_sd_fat_info_t *info)
{
    last_info=*info;last_result=rc<0?rc:0;return rc;
}
/** @brief Serve binary windows with the full unsigned 32-bit file length. */
static int file_at(const char *name,uint32_t offset,char *buf,size_t max,uint32_t *total)
{
    tiku_sd_fat_info_t info;
    if(max>TIKU_SD_FAT_READ_MAX)max=TIKU_SD_FAT_READ_MAX;
    int rc=tiku_sd_fat_read_at(name,offset,buf,(uint32_t)max,total,&info);
    return record(rc,&info);
}
/** @brief Serve a legacy full-length read when its copied prefix fits one window. */
static int file_read(const char *name,char *buf,size_t max)
{
    uint32_t total=0;int rc=file_at(name,0,buf,max,&total);
    if(rc<0)return rc;
    if(total>INT_MAX||(total>(uint32_t)rc&&max>(size_t)rc)){
        last_result=TIKU_VFS_E2BIG;return TIKU_VFS_E2BIG;
    }
    return (int)total;
}
/** @brief Resolve empty directories and files without relying on directory contents. */
static int is_dir(const char *name)
{
    tiku_sd_fat_info_t info;int rc=tiku_sd_fat_is_dir(name,&info);
    return record(rc,&info);
}
/** @brief List one page; E2BIG reports more entries or names outside VFS bounds. */
static int list_checked(const char *prefix,tiku_vfs_dyn_list_cb cb,void *ctx)
{
    tiku_fat_dirent_t entries[TIKU_SD_FAT_LIST_MAX];
    tiku_sd_fat_info_t info;uint32_t count=0;uint8_t more=0;
    int rc=tiku_sd_fat_list(*prefix?prefix:"/",0,entries,TIKU_SD_FAT_LIST_MAX,&count,&more,&info);
    if(rc)return record(rc,&info);
    rc=more?TIKU_VFS_E2BIG:0;
    for(unsigned i=0;i<count;i++){
        char name[TIKU_FAT_NAME_MAX+1];size_t length=strlen(entries[i].name);
        if(strlen(prefix)+length+4>TIKU_VFS_PATH_MAX){rc=TIKU_VFS_E2BIG;continue;}
        memcpy(name,entries[i].name,length);
        if(entries[i].is_dir)name[length++]='/';
        name[length]=0;cb(name,ctx);
    }
    return record(rc,&info);
}
/** @brief Render cached status without issuing an SD or C6 transaction. */
static int status_read(char *buf,size_t max)
{
    return snprintf(buf,max,"result=%s filesystem=%s limited=%u restore=%d read_only=1\n",
        tiku_vfs_strerror(last_result),tiku_media_fs_name(last_info.detected),
        last_info.limited,last_info.close_status);
}

static const tiku_vfs_dynops_t ops={
    .read=file_read,.read_at=file_at,.list_checked=list_checked,.is_dir=is_dir,.read_only=1
};
static const tiku_vfs_node_t files={.name="sd",.type=TIKU_VFS_DIR,.dyn=&ops};
static const tiku_vfs_node_t observation={.name="sd",.type=TIKU_VFS_FILE,.read=status_read};

const tiku_vfs_node_t *tiku_sd_vfs_files(void){return &files;}
const tiku_vfs_node_t *tiku_sd_vfs_status(void){return &observation;}
