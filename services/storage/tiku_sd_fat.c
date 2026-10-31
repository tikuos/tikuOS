/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sd_fat.c - Per-operation FAT32 views with bounded SD controller ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sd_fat.h"
#include <arch/esp32p4/tiku_sdcard_arch.h>
#include <boot/tiku_boot_c6.h>
#include <string.h>

#define READ_BUDGET 64u
#define REQUEST_BUDGET 512u

/** @brief One command owns a checked partition view, a sector cache and bounded reads. */
typedef struct {
    tiku_p4_sdcard_t card;
    uint64_t first,sectors;
    uint32_t cached_lba,reads,requests;
    uint8_t cache[512],cached,limited;
} sd_view_t;

/** @brief Read a physical sector, reusing the last sector within this command. */
static int physical_read(uint32_t lba,void *data,void *context)
{
    sd_view_t *v=context;
    if(!data||(uint64_t)lba>=v->card.sectors)return -1;
    if(v->requests++>=REQUEST_BUDGET){v->limited=1;return -1;}
    if(!v->cached||v->cached_lba!=lba){
        if(v->reads>=READ_BUDGET){v->limited=1;return -1;}
        v->cached=0;v->reads++;
        if(tiku_p4_sdcard_read(lba,v->cache,&v->card))return -1;
        v->cached_lba=lba;v->cached=1;
    }
    memcpy(data,v->cache,512);return 0;
}
/** @brief Translate FAT volume-relative reads through the checked partition extent. */
static int volume_read(uint32_t lba,uint32_t n,void *data,void *context)
{
    sd_view_t *v=context;
    if(!data||!n||(uint64_t)lba>=v->sectors||n>v->sectors-lba||n>READ_BUDGET)return -1;
    for(uint32_t i=0;i<n;i++){
        uint64_t absolute=v->first+lba+i;
        if(absolute>UINT32_MAX||physical_read((uint32_t)absolute,(uint8_t *)data+512u*i,v))return -1;
    }
    return 0;
}

static uint8_t serving;

/** @brief Map parser failures to the public filesystem status vocabulary. */
static int status(tiku_fat_err_t error)
{
    switch(error){
    case TIKU_FAT_OK:return 0;
    case TIKU_FAT_ERR_NOENT:return TIKU_VFS_ENOENT;
    case TIKU_FAT_ERR_NOFS:case TIKU_FAT_ERR_NOT_FAT32:return TIKU_VFS_ENOTSUP;
    case TIKU_FAT_ERR_NOTDIR:return TIKU_VFS_EACCES;
    case TIKU_FAT_ERR_ARG:return TIKU_VFS_EINVAL;
    case TIKU_FAT_ERR_GEOM:case TIKU_FAT_ERR_CORRUPT:return TIKU_VFS_ECORRUPT;
    default:return TIKU_VFS_EIO;
    }
}
/** @brief Locate a checked FAT32 volume inside the identified medium. */
static int mount_volume(sd_view_t *v,tiku_fat_t *fs,tiku_sd_fat_info_t *info)
{
    tiku_media_probe_t media;
    info->probe_status=tiku_media_probe(&media,v->card.sectors,physical_read,v);
    if(info->probe_status)return info->probe_status==TIKU_MEDIA_IO?TIKU_VFS_EIO:TIKU_VFS_ECORRUPT;
    if(media.count)info->detected=media.parts[0].fs;
    for(unsigned i=0;i<media.count;i++){
        const tiku_media_partition_t *p=&media.parts[i];
        if(p->fs!=TIKU_MEDIA_FAT32||!p->geometry_valid)continue;
        v->first=p->first;v->sectors=p->volume_sectors;
        info->detected=p->fs;info->first=p->first;info->sectors=p->volume_sectors;
        info->cluster_bytes=p->cluster_bytes;
        info->fat_status=tiku_fat_mount(fs,volume_read,v);
        if(!info->fat_status&&fs->total_sec!=v->sectors)info->fat_status=TIKU_FAT_ERR_GEOM;
        return status(info->fat_status);
    }
    info->fat_status=TIKU_FAT_ERR_NOFS;return TIKU_VFS_ENOTSUP;
}
/** @brief Open one synchronous session, advancing pending C6 transfers through their owner. */
static int begin(sd_view_t *v,tiku_fat_t *fs,tiku_sd_fat_info_t *info)
{
    memset(v,0,sizeof *v);
    info->open_status=tiku_p4_sdcard_open(&v->card);
    for(unsigned n=0;info->open_status==TIKU_IPC_BUSY&&n<128;n++){
        tiku_ipc_sdio_t *link=tiku_boot_c6_owner();
        if(!link||link->state!=TIKU_SDIO_READY)break;
        tiku_ipc_sdio_step(link);info->open_status=tiku_p4_sdcard_open(&v->card);
    }
    info->card_sectors=v->card.sectors;
    if(info->open_status)return info->open_status==TIKU_IPC_BUSY?TIKU_VFS_EBUSY:TIKU_VFS_EIO;
    return mount_volume(v,fs,info);
}
/** @brief Restore controller ownership and give restoration failures precedence. */
static int finish(sd_view_t *v,tiku_sd_fat_info_t *info,int result)
{
    if(info->open_status!=TIKU_IPC_BUSY)info->close_status=tiku_p4_sdcard_close();
    info->limited=v->limited;
    if(info->limited)result=TIKU_VFS_E2BIG;
    if(info->close_status)result=TIKU_VFS_EIO;
    serving=0;return result;
}
/** @brief Start an operation with zeroed diagnostics and reject recursive access. */
static int enter(tiku_sd_fat_info_t *info)
{
    if(!info)return TIKU_VFS_EINVAL;
    memset(info,0,sizeof *info);
    if(serving){info->open_status=TIKU_IPC_BUSY;return TIKU_VFS_EBUSY;}
    serving=1;return 0;
}

int tiku_sd_fat_info(tiku_sd_fat_info_t *info)
{
    sd_view_t view;tiku_fat_t fs;
    int rc=enter(info);if(rc)return rc;
    rc=begin(&view,&fs,info);return finish(&view,info,rc);
}

int tiku_sd_fat_read_at(const char *path,uint32_t offset,void *data,
                       uint32_t max,uint32_t *total,tiku_sd_fat_info_t *info)
{
    sd_view_t view;tiku_fat_t fs;tiku_fat_file_t file;uint32_t size=0;
    if(total)*total=0;
    if(info)memset(info,0,sizeof *info);
    if(!path||!data||!total||!max||max>TIKU_SD_FAT_READ_MAX)return TIKU_VFS_EINVAL;
    int rc=enter(info);if(rc)return rc;
    rc=begin(&view,&fs,info);
    if(!rc){
        info->fat_status=tiku_fat_open(&fs,path,&file);rc=status(info->fat_status);
        if(!rc){
            size=file.size;
            if(offset<size){
                info->fat_status=tiku_fat_seek(&fs,&file,offset);rc=status(info->fat_status);
                if(!rc){
                    rc=(int)tiku_fat_read(&fs,&file,data,max);
                    if(rc<0){info->fat_status=(tiku_fat_err_t)-rc;rc=status(info->fat_status);}
                }
            }
        }
    }
    rc=finish(&view,info,rc);if(rc>=0)*total=size;return rc;
}
/** @brief Read the next entry other than the directory's dot references. */
static tiku_fat_err_t next(tiku_fat_t *fs,tiku_fat_dir_t *dir,tiku_fat_dirent_t *entry)
{
    tiku_fat_err_t rc;
    do{rc=tiku_fat_readdir(fs,dir,entry);}
    while(!rc&&(!strcmp(entry->name,".")||!strcmp(entry->name,"..")));
    return rc;
}

int tiku_sd_fat_list(const char *path,uint32_t skip,tiku_fat_dirent_t *entries,
                    uint32_t capacity,uint32_t *count,uint8_t *more,tiku_sd_fat_info_t *info)
{
    sd_view_t view;tiku_fat_t fs;tiku_fat_dir_t dir;tiku_fat_dirent_t entry;
    uint32_t found=0;uint8_t remains=0;
    if(count)*count=0;
    if(more)*more=0;
    if(info)memset(info,0,sizeof *info);
    if(!path||!entries||!capacity||capacity>TIKU_SD_FAT_LIST_MAX||
       skip>TIKU_SD_FAT_SKIP_MAX||!count||!more)return TIKU_VFS_EINVAL;
    int rc=enter(info);if(rc)return rc;
    rc=begin(&view,&fs,info);
    if(!rc){
        info->fat_status=tiku_fat_opendir(&fs,path,&dir);rc=status(info->fat_status);
        if(!rc){
            for(uint32_t i=0;i<skip&&!info->fat_status;i++)info->fat_status=next(&fs,&dir,&entry);
            while(!info->fat_status&&found<capacity){
                info->fat_status=next(&fs,&dir,&entry);
                if(!info->fat_status)entries[found++]=entry;
            }
            if(!info->fat_status){info->fat_status=next(&fs,&dir,&entry);remains=!info->fat_status;}
            if(info->fat_status==TIKU_FAT_ERR_NOENT)info->fat_status=TIKU_FAT_OK;
            rc=status(info->fat_status);
        }
    }
    rc=finish(&view,info,rc);if(!rc){*count=found;*more=remains;}return rc;
}

int tiku_sd_fat_is_dir(const char *path,tiku_sd_fat_info_t *info)
{
    sd_view_t view;tiku_fat_t fs;tiku_fat_dir_t dir;
    if(info)memset(info,0,sizeof *info);
    if(!path)return TIKU_VFS_EINVAL;
    int rc=enter(info);if(rc)return rc;
    rc=begin(&view,&fs,info);
    if(!rc){
        info->fat_status=tiku_fat_opendir(&fs,path,&dir);
        rc=!info->fat_status?1:info->fat_status==TIKU_FAT_ERR_NOTDIR?0:status(info->fat_status);
    }
    return finish(&view,info,rc);
}
