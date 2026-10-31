/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_image.c - Regular-file media images and binary import/export.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <kernel/fs/tiku_mfs.h>
#include <kernel/fs/tiku_mfs_copy.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

/** @brief Transfer exactly n sectors at a 64-bit regular-file offset. */
static int disk_io(int fd,uint64_t lba,uint32_t n,void *data,int writing)
{
    if(lba>INT64_MAX/512||n>128u)return -1;
    size_t bytes=(size_t)n*512;uint8_t *p=data;off_t at=(off_t)(lba*512);
    while(bytes){
        ssize_t done=writing?pwrite(fd,p,bytes,at):pread(fd,p,bytes,at);
        if(done<0&&errno==EINTR)continue;
        if(done<=0)return -1;
        p+=done;bytes-=(size_t)done;at+=done;
    }
    return 0;
}
/** @brief Read image sectors. */
static int image_read(void *ctx,uint64_t lba,uint32_t n,void *p)
{ return disk_io(*(int *)ctx,lba,n,p,0); }
/** @brief Write image sectors. */
static int image_write(void *ctx,uint64_t lba,uint32_t n,const void *p)
{ return disk_io(*(int *)ctx,lba,n,(void *)p,1); }
/** @brief Flush the regular-file image to its host backend. */
static int image_sync(void *ctx){return fsync(*(int *)ctx);}
/** @brief Decode exact-length hexadecimal UUIDs and digests. */
static int unhex(const char *s,uint8_t *p,unsigned n)
{
    if(strlen(s)!=n*2)return -1;
    for(unsigned i=0;i<n;i++){
        unsigned value=0;
        for(unsigned j=0;j<2;j++){
            unsigned char c=(unsigned char)s[i*2+j];unsigned v;
            if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;
            else if(c>='A'&&c<='F')v=c-'A'+10;else return -1;
            value=value*16+v;
        }
        p[i]=(uint8_t)value;
    }
    return 0;
}
/** @brief Import a file with an externally computed expected SHA-256. */
static int import(tiku_mfs_t *fs,const char *name,const char *path,const char *hash)
{
    uint8_t digest[32],buffer[TIKU_MFS_CHUNK];if(unhex(hash,digest,32))return TIKU_MFS_ARG;
    FILE *input=fopen(path,"rb");if(!input)return TIKU_MFS_IO;
    struct stat st;int rc=0;
    if(fstat(fileno(input),&st)||!S_ISREG(st.st_mode)||st.st_size<0)rc=TIKU_MFS_ARG;
    uint64_t id=0;
    if(!rc)rc=tiku_mfs_begin(fs,name,TIKU_MFS_ATOMIC,(uint64_t)st.st_size,digest,&id);
    while(!rc){
        size_t n=fread(buffer,1,sizeof buffer,input);
        if(n){rc=tiku_mfs_append(fs,buffer,(uint32_t)n);if(!rc)rc=tiku_mfs_sync(fs);}
        if(n<sizeof buffer){if(ferror(input))rc=TIKU_MFS_IO;break;}
    }
    if(!rc)rc=tiku_mfs_finalize(fs);
    if(rc&&id&&!fs->failed){(void)tiku_mfs_release(fs);(void)tiku_mfs_delete(fs,id);}
    fclose(input);return rc;
}
/** @brief Export a committed snapshot to a newly created regular file. */
static int export(tiku_mfs_t *fs,const char *name,const char *path)
{
    uint64_t token,offset=0;int rc=tiku_mfs_open_read(fs,name,&token);if(rc)return rc;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){(void)tiku_mfs_close_read(fs,token);return TIKU_MFS_IO;}
    uint8_t buffer[TIKU_MFS_CHUNK];
    for(;;){
        int n=tiku_mfs_read_at(fs,token,offset,buffer,sizeof buffer);
        if(n<=0){rc=n;break;}
        unsigned done=0;
        while(done<(unsigned)n){ssize_t k=write(fd,buffer+done,(unsigned)n-done);
            if(k<0&&errno==EINTR)continue;
            if(k<=0){rc=TIKU_MFS_IO;break;}done+=(unsigned)k;}
        if(rc)break;
        offset+=(unsigned)n;
    }
    if(!rc&&fsync(fd))rc=TIKU_MFS_IO;
    if(close(fd)&&!rc)rc=TIKU_MFS_IO;
    (void)tiku_mfs_close_read(fs,token);return rc;
}
/** @brief Dispatch explicit image mutations and snapshot reads. */
int main(int argc,char **argv)
{
    if(argc<4){fprintf(stderr,"usage: %s COMMAND IMAGE UUID [ARGS]\n"
        "format: BYTES; put: NAME INPUT SHA256; get: NAME OUTPUT; delete/copy: NAME; abort: ID; list; uploads; collect\n",argv[0]);return 2;}
    uint8_t uuid[16];if(unhex(argv[3],uuid,16))return 2;
    int formatting=!strcmp(argv[1],"format");
    int fd=open(argv[2],O_RDWR|(formatting?O_CREAT|O_EXCL:0),0600);
    if(fd<0){perror(argv[2]);return 1;}
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return 2;}
    if(formatting){
        if(argc!=5){close(fd);return 2;}
        char *end;errno=0;uint64_t size=strtoull(argv[4],&end,10);
        if(errno||*end||size%512||!tiku_mfs_bitmap_bytes(size/512)||ftruncate(fd,(off_t)size)){close(fd);return 2;}
        st.st_size=(off_t)size;
    }
    tiku_block_t block={&fd,(uint64_t)st.st_size/512,image_read,image_write,image_sync};
    size_t bytes=tiku_mfs_bitmap_bytes(block.sectors);
    tiku_mfs_t *fs=calloc(1,sizeof *fs);uint8_t *bits=calloc(2,bytes);
    if(!bytes||!fs||!bits){close(fd);free(fs);free(bits);return 1;}
    tiku_mfs_init(fs);
    int rc=formatting?tiku_mfs_format(fs,&block,uuid,bits,bits+bytes,bytes):
        tiku_mfs_mount(fs,&block,uuid,bits,bits+bytes,bytes);
    if(!rc&&!formatting){
        if(!strcmp(argv[1],"put")&&argc==7)rc=import(fs,argv[4],argv[5],argv[6]);
        else if(!strcmp(argv[1],"get")&&argc==6)rc=export(fs,argv[4],argv[5]);
        else if((!strcmp(argv[1],"list")||!strcmp(argv[1],"uploads"))&&argc==4){
            tiku_mfs_cursor_t cursor={0};tiku_mfs_object_t o;
            while(!(rc=!strcmp(argv[1],"uploads")?tiku_mfs_list_pending(fs,&cursor,&o):tiku_mfs_list(fs,&cursor,&o)))printf("%" PRIu64 " %" PRIu64 " %u %s\n",o.id,o.length,o.complete,o.name);
            if(rc==TIKU_MFS_NOENT)rc=0;
        }else if(!strcmp(argv[1],"abort")&&argc==5){
            char *end;errno=0;uint64_t id=strtoull(argv[4],&end,10);
            if(errno||*end||!id)rc=TIKU_MFS_ARG;
            else{
                tiku_mfs_cursor_t cursor={0};tiku_mfs_object_t o;rc=TIKU_MFS_NOENT;
                while(!tiku_mfs_list_pending(fs,&cursor,&o))if(o.id==id){rc=tiku_mfs_delete(fs,id);break;}
            }
        }else if(!strcmp(argv[1],"delete")&&argc==5){
            tiku_mfs_object_t o;rc=tiku_mfs_stat(fs,argv[4],&o);if(!rc)rc=tiku_mfs_delete(fs,o.id);
        }else if(!strcmp(argv[1],"copy")&&argc==5){
            tiku_mfs_copy_t job;uint8_t buffer[TIKU_MFS_CHUNK];rc=tiku_mfs_copy_begin(fs,&job,argv[4]);
            if(!rc)do{rc=tiku_mfs_copy_step(fs,&job,buffer,sizeof buffer);}while(rc==1);
        }else if(!strcmp(argv[1],"collect")&&argc==4){
            rc=tiku_mfs_collect_begin(fs);if(!rc)do{rc=tiku_mfs_collect_step(fs,16);}while(rc==1);
        }else rc=TIKU_MFS_ARG;
    }
    if(rc)fprintf(stderr,"media filesystem error %d\n",rc);
    close(fd);free(bits);free(fs);return rc?1:0;
}
