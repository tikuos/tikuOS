/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs.c - Checksummed media transactions and copy-on-write extent indexes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mfs.h"
#include "tiku_mfs_query.h"
#include "tiku_mfs_checkpoint.h"
#include "tiku_mfs_mutation.h"
#include <string.h>
#include <limits.h>

#define SECTOR 512u
#define CRC_AT 508u
#define FANOUT 16u
#define REF_AT 64u
#define DEPTH 12u

/** @brief Decode an unaligned little-endian word. */
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
/** @brief Decode an unaligned little-endian double word. */
static uint64_t get64(const uint8_t *p)
{ return get32(p)|((uint64_t)get32(p+4)<<32); }
/** @brief Encode a little-endian word. */
static void put32(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
/** @brief Encode a little-endian double word. */
static void put64(uint8_t *p,uint64_t v)
{ put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32)); }
/** @brief Advance reflected CRC32C, with initial and final XOR supplied by the caller. */
static uint32_t crc_more(uint32_t c,const void *data,size_t n)
{
    const uint8_t *p=data;
    while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0x82f63b78u);}
    return c;
}
/** @brief Compute a complete CRC32C. */
static uint32_t crc(const void *p,size_t n){return ~crc_more(UINT32_MAX,p,n);}
/** @brief Seal a metadata sector. */
static void seal(uint8_t *p){put32(p+CRC_AT,crc(p,CRC_AT));}
/** @brief Check a metadata sector and volume binding. */
static int valid(tiku_mfs_t *f,const uint8_t *p,const char *magic)
{ return !memcmp(p,magic,4)&&!memcmp(p+8,f->uuid,16)&&get32(p+CRC_AT)==crc(p,CRC_AT); }
/** @brief Initialize a metadata sector with a volume binding. */
static void page(tiku_mfs_t *f,uint8_t *p,const char *magic)
{ memset(p,0,SECTOR);memcpy(p,magic,4);memcpy(p+8,f->uuid,16); }
/** @brief Read blocks with range validation and freeze after uncertain I/O. */
static int rd(tiku_mfs_t *f,uint64_t lba,uint32_t n,void *p)
{
    if(!n||lba>=f->block.sectors||n>f->block.sectors-lba)return TIKU_MFS_CORRUPT;
    if(f->block.read(f->block.context,lba,n,p)){f->failed=1;return TIKU_MFS_IO;}
    return 0;
}
/** @brief Write blocks with range validation and freeze after uncertain I/O. */
static int wr(tiku_mfs_t *f,uint64_t lba,uint32_t n,const void *p)
{
    if(f->failed)return TIKU_MFS_IO;
    if(!n||lba>=f->block.sectors||n>f->block.sectors-lba)return TIKU_MFS_CORRUPT;
    if(f->block.write(f->block.context,lba,n,p)){f->failed=1;return TIKU_MFS_IO;}
    return 0;
}
/** @brief Finish an ordered durability barrier. */
static int barrier(tiku_mfs_t *f)
{
    if(f->failed||f->block.sync(f->block.context)){f->failed=1;return TIKU_MFS_IO;}
    return 0;
}
/** @brief Read one segment allocation bit. */
static unsigned bit(const uint8_t *p,uint32_t n){return (p[n/8]>>(n%8))&1u;}
/** @brief Mark one allocated segment. */
static void set(uint8_t *p,uint32_t n){p[n/8]|=(uint8_t)(1u<<(n%8));}
/** @brief Test a nonempty canonical relative object name. */
static int name_ok(const char *s)
{
    if(!s||!*s||*s=='/')return 0;
    unsigned component=0;
    for(unsigned i=0;i<TIKU_MFS_NAME;i++){
        unsigned char c=(unsigned char)s[i];
        if(!c)return component&&!(component==1&&s[i-1]=='.')&&
            !(component==2&&s[i-1]=='.'&&s[i-2]=='.');
        if(c<' '||c==127||c=='\\')return 0;
        if(c=='/'){
            if(!component||(component==1&&s[i-1]=='.')||
                (component==2&&s[i-1]=='.'&&s[i-2]=='.'))return 0;
            component=0;
        }else component++;
    }
    return 0;
}
/** @brief Rotate a SHA-256 word. */
static uint32_t ror(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
/** @brief Compress one SHA-256 block. */
static void hash_block(tiku_mfs_hash_t *s,const uint8_t *p)
{
    static const uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64],v[8];
    for(unsigned i=0;i<16;i++)w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|
        ((uint32_t)p[i*4+2]<<8)|p[i*4+3];
    for(unsigned i=16;i<64;i++){
        uint32_t a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(ror(a,7)^ror(a,18)^(a>>3))+w[i-7]+(ror(b,17)^ror(b,19)^(b>>10));
    }
    memcpy(v,s->h,sizeof v);
    for(unsigned i=0;i<64;i++){
        uint32_t a=v[7]+(ror(v[4],6)^ror(v[4],11)^ror(v[4],25))+
            ((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
        uint32_t b=(ror(v[0],2)^ror(v[0],13)^ror(v[0],22))+
            ((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
        for(unsigned j=7;j;j--)v[j]=v[j-1];
        v[4]+=a;v[0]=a+b;
    }
    for(unsigned i=0;i<8;i++)s->h[i]+=v[i];
}
/** @brief Initialize the SHA-256 chaining words. */
static void hash_init(tiku_mfs_hash_t *s)
{
    static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memset(s,0,sizeof *s);memcpy(s->h,initial,sizeof initial);
}
/** @brief Hash accepted bytes using the object's existing byte count. */
static void hash_add(tiku_mfs_hash_t *s,uint64_t length,const uint8_t *p,uint32_t n)
{
    unsigned used=(unsigned)(length%64);
    while(n){unsigned take=64-used;if(take>n)take=n;
        memcpy(s->tail+used,p,take);p+=take;n-=take;used+=take;
        if(used==64){hash_block(s,s->tail);used=0;memset(s->tail,0,64);}
    }
}
/** @brief Finalize a copy of an incremental SHA-256 state. */
static void hash_end(tiku_mfs_hash_t s,uint64_t length,uint8_t digest[32])
{
    unsigned used=(unsigned)(length%64);s.tail[used++]=0x80;
    if(used>56){memset(s.tail+used,0,64-used);hash_block(&s,s.tail);used=0;}
    memset(s.tail+used,0,56-used);
    for(unsigned i=0;i<8;i++)s.tail[63-i]=(uint8_t)((length*8)>>(8*i));
    hash_block(&s,s.tail);
    for(unsigned i=0;i<32;i++)digest[i]=(uint8_t)(s.h[i/4]>>(24-8*(i%4)));
}
/** @brief Encode one catalog entry without native struct layout. */
static void object_encode(tiku_mfs_t *f,uint8_t *p,const tiku_mfs_object_t *o)
{
    page(f,p,"MFO1");put64(p+24,o->id);put64(p+32,o->length);put64(p+40,o->expected);
    put64(p+48,o->root.lba);put32(p+56,o->root.crc);
    p[60]=o->height;p[61]=o->policy;p[62]=o->complete;p[63]=o->visible;
    memcpy(p+64,o->name,64);memcpy(p+128,o->digest,32);memcpy(p+160,o->expected_digest,32);
    for(unsigned i=0;i<8;i++)put32(p+192+4*i,o->hash.h[i]);
    memcpy(p+224,o->hash.tail,64);seal(p);
}
/** @brief Validate a data/index reference within one allocated data segment. */
static int ref_ok(tiku_mfs_t *f,tiku_mfs_ref_t r)
{ return r.lba>=TIKU_MFS_SEGMENT&&r.lba<(uint64_t)f->segments*TIKU_MFS_SEGMENT; }
/** @brief Decode a checksummed catalog entry and its bounded fields. */
static int object_decode(tiku_mfs_t *f,const uint8_t *p,tiku_mfs_object_t *o)
{
    if(!valid(f,p,"MFO1"))return TIKU_MFS_CORRUPT;
    memset(o,0,sizeof *o);o->id=get64(p+24);o->length=get64(p+32);o->expected=get64(p+40);
    o->root.lba=get64(p+48);o->root.crc=get32(p+56);
    o->height=p[60];o->policy=p[61];o->complete=p[62];o->visible=p[63];
    memcpy(o->name,p+64,64);memcpy(o->digest,p+128,32);memcpy(o->expected_digest,p+160,32);
    for(unsigned i=0;i<8;i++)o->hash.h[i]=get32(p+192+4*i);
    memcpy(o->hash.tail,p+224,64);
    if(!o->id){memset(o,0,sizeof *o);return 0;}
    if(!name_ok(o->name)||(o->policy!=TIKU_MFS_STREAM&&o->policy!=TIKU_MFS_ATOMIC
#if TIKU_MEDIA_DIRECTORIES_ENABLE
       &&o->policy!=TIKU_MFS_DIRECTORY
#endif
       )||
       o->complete>1||o->visible>1||o->height>DEPTH||o->length>UINT64_MAX/8||
       (o->length&&(!o->height||!ref_ok(f,o->root)))||
       (!o->length&&(o->height||o->root.lba))||
       (o->policy==TIKU_MFS_STREAM&&!o->visible)||
       (o->policy==TIKU_MFS_ATOMIC&&(o->length>o->expected||o->visible!=o->complete)))
        return TIKU_MFS_CORRUPT;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(o->policy==TIKU_MFS_DIRECTORY&&(!(f->features&1u)||o->length||o->expected||
       !o->complete||!o->visible||o->root.crc))return TIKU_MFS_CORRUPT;
#endif
    if(o->length&&((o->length-1)/TIKU_MFS_CHUNK)>>(4*o->height))return TIKU_MFS_CORRUPT;
    return 0;
}

#if TIKU_MEDIA_NAMES_ENABLE
/** @brief Boot-scoped namespace versions remain distinct across context initialization. */
static uint64_t names_version;
#endif

size_t tiku_mfs_bitmap_bytes(uint64_t sectors)
{
    if(sectors<8ull*TIKU_MFS_SEGMENT||sectors>(1ull<<32))return 0;
    return (size_t)((sectors/TIKU_MFS_SEGMENT+7)/8);
}
int tiku_mfs_identify(const tiku_block_t *b,uint8_t uuid[16])
{
    if(!b||!b->read||!uuid||!tiku_mfs_bitmap_bytes(b->sectors))return TIKU_MFS_ARG;
    uint64_t bank=1+(tiku_mfs_bitmap_bytes(b->sectors)+511)/512+2*TIKU_MFS_JOURNAL;
    uint8_t p[512],found[16]={0};unsigned valid_count=0;
    for(unsigned i=0;i<2;i++){
        if(b->read(b->context,i*bank,1,p))return TIKU_MFS_IO;
        if(memcmp(p,"MFC2",4)||crc(p,508)!=get32(p+508)||get64(p+32)!=b->sectors||
           get32(p+84)!=2||
#if TIKU_MEDIA_DIRECTORIES_ENABLE
           (get32(p+88)&~1u)||
#else
           get32(p+88)||
#endif
           get32(p+92)!=TIKU_MFS_SEGMENT||!get64(p+40))continue;
        unsigned nonzero=0;for(unsigned j=0;j<16;j++)nonzero|=p[8+j];
        if(!nonzero)continue;
        if(valid_count&&memcmp(found,p+8,16))return TIKU_MFS_CORRUPT;
        memcpy(found,p+8,16);valid_count++;
    }
    if(!valid_count)return TIKU_MFS_NOFS;
    memcpy(uuid,found,16);return 0;
}
void tiku_mfs_init(tiku_mfs_t *f){if(f){memset(f,0,sizeof *f);f->next_token=1;}}
/** @brief Bind caller storage to supported volume geometry. */
static int setup(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t *uuid,
                 void *used,void *mark,size_t capacity)
{
    if(!f||!b||!uuid||!used||!mark||used==mark||!b->read||!b->write||!b->sync)return TIKU_MFS_ARG;
    size_t bytes=tiku_mfs_bitmap_bytes(b->sectors);unsigned nonzero=0;
    for(unsigned i=0;i<16;i++)nonzero|=uuid[i];
    if(!bytes||capacity<bytes||!nonzero||!f->next_token)return TIKU_MFS_ARG;
    f->mounted=f->failed=f->writing=f->collecting=0;
    if(f->generation==UINT64_MAX)return TIKU_MFS_LIMIT;
#if TIKU_MEDIA_NAMES_ENABLE
    if(names_version==UINT64_MAX)return TIKU_MFS_LIMIT;
    f->names_epoch=++names_version;
#endif
    f->generation++;f->catalog_write=0;
    memset(f->readers,0,sizeof f->readers);
    memset(&f->ids,0,sizeof f->ids);memset(&f->names,0,sizeof f->names);
    f->block=*b;f->used=used;f->mark=mark;f->bitmap_bytes=bytes;
    f->segments=(uint32_t)(b->sectors/TIKU_MFS_SEGMENT);
    f->bank_sectors=1+(uint32_t)((bytes+511)/512)+2*TIKU_MFS_JOURNAL;
    memcpy(f->uuid,uuid,16);return 0;
}
/** @brief Locate the first journal record in a checkpoint bank. */
static uint64_t journal_lba(tiku_mfs_t *f,unsigned bank)
{ return (uint64_t)bank*f->bank_sectors+1+(f->bitmap_bytes+511)/512; }
#define CATALOG_IDS 1u
#define CATALOG_NAMES 2u
#define CATALOG_RESERVE 2048u

/** @brief One immutable AVL catalog node with either an ID or name key. */
typedef struct {
    tiku_mfs_link_t child[2];
    uint64_t key, visible, pending;
    tiku_mfs_ref_t object;
    char name[TIKU_MFS_NAME];
    unsigned kind;
} catalog_node_t;

/** @brief Allocate one contiguous log run; catalog publication may use the reserve. */
static int allocate(tiku_mfs_t *f,unsigned count,uint64_t *lba)
{
    unsigned left=f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-(unsigned)(f->tail%TIKU_MFS_SEGMENT):0;
    if(!f->catalog_write&&(uint64_t)f->free_segments*TIKU_MFS_SEGMENT+left<count+CATALOG_RESERVE)
        return TIKU_MFS_SPACE;
    if(left>=count){*lba=f->tail;f->tail+=count;return 0;}
    if(f->allocations==UINT64_MAX)return TIKU_MFS_LIMIT;
    for(uint32_t i=0;i<f->segments-1;i++){
        uint32_t s=f->cursor;f->cursor++;if(f->cursor==f->segments)f->cursor=1;
        if(!bit(f->used,s)){
            set(f->used,s);f->allocations++;f->free_segments--;
            *lba=(uint64_t)s*TIKU_MFS_SEGMENT;f->tail=*lba+count;return 0;
        }
    }
    return TIKU_MFS_SPACE;
}
/** @brief Encode a catalog subtree reference and its rank summaries. */
static void link_put(uint8_t *p,tiku_mfs_link_t link)
{
    put64(p,link.ref.lba);put32(p+8,link.ref.crc);put32(p+12,link.height);
    put64(p+16,link.count);put64(p+24,link.pending);
}
/** @brief Decode a catalog subtree reference. */
static tiku_mfs_link_t link_get(const uint8_t *p)
{
    tiku_mfs_link_t link={{get64(p),get32(p+8)},get64(p+16),get64(p+24),0};
    uint32_t h=get32(p+12);link.height=(uint8_t)(h<=TIKU_MFS_CATALOG_DEPTH?h:255);return link;
}
/** @brief Validate bounded subtree summaries and empty-link encoding. */
static int link_ok(tiku_mfs_t *f,tiku_mfs_link_t link)
{
    if(!link.ref.lba)return !link.ref.crc&&!link.height&&!link.count&&!link.pending;
    return ref_ok(f,link.ref)&&link.height&&link.height<=TIKU_MFS_CATALOG_DEPTH&&
        link.count<=f->block.sectors&&link.pending<=f->block.sectors&&
        link.count+link.pending&&link.count+link.pending<=f->block.sectors;
}
/** @brief Derive a node's height and visible/private rank counts. */
static tiku_mfs_link_t node_link(const catalog_node_t *n)
{
    tiku_mfs_link_t r={{0,0},0,0,0};
    r.height=(uint8_t)(1+(n->child[0].height>n->child[1].height?n->child[0].height:n->child[1].height));
    r.count=n->child[0].count+n->child[1].count+(n->kind==CATALOG_IDS?1:!!n->visible);
    r.pending=n->child[0].pending+n->child[1].pending+(n->kind==CATALOG_NAMES&&n->pending!=0);
    return r;
}
/** @brief Validate a catalog node and its bound subtree summaries. */
#if TIKU_MEDIA_ASYNC_ENABLE
static int catalog_decode(tiku_mfs_t *f,tiku_mfs_link_t link,unsigned kind,catalog_node_t *n,const uint8_t p[512])
{
    if(!link_ok(f,link)||!link.ref.lba)return TIKU_MFS_CORRUPT;
#else
static int catalog_read(tiku_mfs_t *f,tiku_mfs_link_t link,unsigned kind,catalog_node_t *n)
{
    uint8_t p[512];if(!link_ok(f,link)||!link.ref.lba)return TIKU_MFS_CORRUPT;
    int rc=rd(f,link.ref.lba,1,p);if(rc)return rc;
#endif
    if(!valid(f,p,"MFI2")||get32(p+508)!=link.ref.crc||get32(p+24)!=kind)return TIKU_MFS_CORRUPT;
    memset(n,0,sizeof *n);n->kind=kind;n->child[0]=link_get(p+32);n->child[1]=link_get(p+64);
    n->key=get64(p+96);n->visible=get64(p+104);n->pending=get64(p+112);
    n->object=(tiku_mfs_ref_t){get64(p+120),get32(p+128)};memcpy(n->name,p+144,64);
    if(!link_ok(f,n->child[0])||!link_ok(f,n->child[1])||
       (kind==CATALOG_IDS&&(!n->key||!ref_ok(f,n->object)||n->child[0].pending||n->child[1].pending))||
       (kind==CATALOG_NAMES&&(!name_ok(n->name)||(!n->visible&&!n->pending)||
        (n->visible&&n->visible==n->pending))))return TIKU_MFS_CORRUPT;
    int balance=(int)n->child[0].height-(int)n->child[1].height;
    tiku_mfs_link_t actual=node_link(n);
    if(balance < -1||balance>1||actual.height!=link.height||actual.count!=link.count||
       actual.pending!=link.pending)return TIKU_MFS_CORRUPT;
    return 0;
}
#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Read and decode one catalog page through the synchronous block provider. */
static int catalog_read(tiku_mfs_t *f,tiku_mfs_link_t link,unsigned kind,catalog_node_t *n)
{
    uint8_t p[512];if(!link_ok(f,link)||!link.ref.lba)return TIKU_MFS_CORRUPT;
    int rc=rd(f,link.ref.lba,1,p);if(rc)return rc;
    return catalog_decode(f,link,kind,n,p);
}
#endif
/** @brief Compare catalog keys without reading the referenced object descriptor. */
static int catalog_compare(const catalog_node_t *a,const catalog_node_t *b)
{ return a->kind==CATALOG_NAMES?strcmp(a->name,b->name):a->key<b->key?-1:a->key>b->key; }
/** @brief Persist one balanced catalog node into unreferenced storage. */
static int catalog_write(tiku_mfs_t *f,const catalog_node_t *n,tiku_mfs_link_t *out)
{
    uint8_t p[512];tiku_mfs_link_t r=node_link(n);uint64_t lba;
    if(r.height>TIKU_MFS_CATALOG_DEPTH||r.count+r.pending>f->block.sectors)return TIKU_MFS_LIMIT;
    int rc=allocate(f,1,&lba);if(rc)return rc;
    page(f,p,"MFI2");put32(p+24,n->kind);link_put(p+32,n->child[0]);link_put(p+64,n->child[1]);
    put64(p+96,n->key);put64(p+104,n->visible);put64(p+112,n->pending);
    put64(p+120,n->object.lba);put32(p+128,n->object.crc);memcpy(p+144,n->name,64);seal(p);
    if((rc=wr(f,lba,1,p)))return rc;
    r.ref=(tiku_mfs_ref_t){lba,get32(p+508)};*out=r;return 0;
}
/** @brief Rebalance one changed path node with copy-on-write single or double rotation. */
static int catalog_balance(tiku_mfs_t *f,catalog_node_t *n,tiku_mfs_link_t *out)
{
    int b=(int)n->child[0].height-(int)n->child[1].height;
    if(b>=-1&&b<=1)return catalog_write(f,n,out);
    if(b < -2||b>2)return TIKU_MFS_CORRUPT;
    unsigned heavy=b>0?0:1,other=1-heavy;catalog_node_t child,middle;int rc;
    if((rc=catalog_read(f,n->child[heavy],n->kind,&child)))return rc;
    if(child.child[heavy].height>=child.child[other].height){
        n->child[heavy]=child.child[other];
        if((rc=catalog_write(f,n,&child.child[other])))return rc;
        return catalog_write(f,&child,out);
    }
    if((rc=catalog_read(f,child.child[other],n->kind,&middle)))return rc;
    child.child[other]=middle.child[heavy];n->child[heavy]=middle.child[other];
    if((rc=catalog_write(f,&child,&middle.child[heavy]))||
       (rc=catalog_write(f,n,&middle.child[other])))return rc;
    return catalog_write(f,&middle,out);
}
/** @brief Find an exact catalog key in bounded traversal storage. */
static int catalog_find(tiku_mfs_t *f,tiku_mfs_link_t root,const catalog_node_t *key,catalog_node_t *out)
{
    for(unsigned depth=0;root.ref.lba&&depth<TIKU_MFS_CATALOG_DEPTH;depth++){
        int rc=catalog_read(f,root,key->kind,out);if(rc)return rc;
        int cmp=catalog_compare(key,out);if(!cmp)return 0;
        root=out->child[cmp>0];
    }
    return root.ref.lba?TIKU_MFS_CORRUPT:TIKU_MFS_NOENT;
}
/** @brief Insert, replace or remove one key and return an unpublished catalog root. */
static int catalog_set(tiku_mfs_t *f,tiku_mfs_link_t root,const catalog_node_t *value,
                       int remove,tiku_mfs_link_t *out)
{
    tiku_mfs_link_t path[TIKU_MFS_CATALOG_DEPTH],r=root;
    uint8_t directions[TIKU_MFS_CATALOG_DEPTH];unsigned depth=0;
    catalog_node_t n,successor;unsigned replace_depth=TIKU_MFS_CATALOG_DEPTH;int rc,found=0;
    while(r.ref.lba){
        if(depth==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
        if((rc=catalog_read(f,r,value->kind,&n)))return rc;
        int cmp=catalog_compare(value,&n);if(!cmp){found=1;break;}
        path[depth]=r;directions[depth++]=(uint8_t)(cmp>0);r=n.child[cmp>0];
    }
    if(remove){
        if(!found)return TIKU_MFS_NOENT;
        if(!n.child[0].ref.lba||!n.child[1].ref.lba)r=n.child[n.child[0].ref.lba?0:1];
        else{
            replace_depth=depth;path[depth]=r;directions[depth++]=1;r=n.child[1];
            for(;;){
                if((rc=catalog_read(f,r,value->kind,&successor)))return rc;
                if(!successor.child[0].ref.lba){r=successor.child[1];break;}
                if(depth==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
                path[depth]=r;directions[depth++]=0;r=successor.child[0];
            }
        }
    }else{
        tiku_mfs_link_t left={0},right={0};if(found){left=n.child[0];right=n.child[1];}
        n=*value;n.child[0]=left;n.child[1]=right;
        if((rc=catalog_write(f,&n,&r)))return rc;
    }
    while(depth){
        depth--;if((rc=catalog_read(f,path[depth],value->kind,&n)))return rc;
        if(depth==replace_depth){
            n.key=successor.key;n.visible=successor.visible;n.pending=successor.pending;
            n.object=successor.object;memcpy(n.name,successor.name,64);
        }
        n.child[directions[depth]]=r;if((rc=catalog_balance(f,&n,&r)))return rc;
    }
    *out=r;return 0;
}
/** @brief Load an ID-indexed object and validate its descriptor binding. */
static int catalog_object(tiku_mfs_t *f,uint64_t id,tiku_mfs_object_t *out)
{
    catalog_node_t key={0},n;key.kind=CATALOG_IDS;key.key=id;
    int rc=catalog_find(f,f->ids,&key,&n);if(rc)return rc;
    uint8_t p[512];if((rc=rd(f,n.object.lba,1,p)))return rc;
    if(get32(p+508)!=n.object.crc)return TIKU_MFS_CORRUPT;
    if((rc=object_decode(f,p,out)))return rc;
    return out->id==id&&id<f->next_id?0:TIKU_MFS_CORRUPT;
}
/** @brief Find the name-index record containing visible and private IDs. */
static int catalog_name(tiku_mfs_t *f,const char *name,catalog_node_t *out)
{
    catalog_node_t key={0};key.kind=CATALOG_NAMES;memcpy(key.name,name,strlen(name)+1);
    return catalog_find(f,f->names,&key,out);
}
/** @brief Return the first catalog name at or above a byte prefix. */
static int catalog_lower(tiku_mfs_t *f,const char *name,catalog_node_t *out)
{
    tiku_mfs_link_t r=f->names;int found=0;
    while(r.ref.lba){
        catalog_node_t n;int rc=catalog_read(f,r,CATALOG_NAMES,&n);if(rc)return rc;
        int cmp=strcmp(n.name,name);if(cmp>=0){*out=n;found=1;r=n.child[0];}else r=n.child[1];
    }
    return found?0:TIKU_MFS_NOENT;
}
/** @brief Validate catalog roots without traversing per-file metadata. */
static int catalog_roots(tiku_mfs_t *f,tiku_mfs_link_t ids,tiku_mfs_link_t names)
{
    catalog_node_t n;int rc;
    if(!link_ok(f,ids)||!link_ok(f,names)||ids.pending||ids.count!=names.count+names.pending)return TIKU_MFS_CORRUPT;
    if(ids.ref.lba&&(rc=catalog_read(f,ids,CATALOG_IDS,&n)))return rc;
    if(names.ref.lba&&(rc=catalog_read(f,names,CATALOG_NAMES,&n)))return rc;
    return 0;
}
/** @brief Encode the two catalog roots into a checkpoint or journal root record. */
static void roots_put(uint8_t *p,tiku_mfs_link_t ids,tiku_mfs_link_t names)
{ link_put(p+112,ids);link_put(p+144,names); }
/** @brief Count free allocation segments from a recovered or collected bitmap. */
static void free_count(tiku_mfs_t *f)
{ f->free_segments=0;for(uint32_t i=1;i<f->segments;i++)f->free_segments+=!bit(f->used,i); }
/** @brief Check or load one checkpoint's bitmap and catalog roots. */
static int bank_load(tiku_mfs_t *f,unsigned bank,int load,uint64_t *epoch)
{
    uint8_t h[512],p[512];uint64_t base=(uint64_t)bank*f->bank_sectors;
    int rc=rd(f,base,1,h);if(rc)return rc;
    if(!valid(f,h,"MFC2")||get64(h+32)!=f->block.sectors||
#if TIKU_MEDIA_DIRECTORIES_ENABLE
       (get32(h+88)&~1u)||
#else
       get32(h+88)!=0||
#endif

       get32(h+92)!=TIKU_MFS_SEGMENT||get32(h+84)!=2)return TIKU_MFS_NOFS;
    *epoch=get64(h+40);
    if(!*epoch||!get64(h+56)||get32(h+72)<1||get32(h+72)>=f->segments||
       (get64(h+64)&&(get64(h+64)<TIKU_MFS_SEGMENT||
        get64(h+64)>(uint64_t)f->segments*TIKU_MFS_SEGMENT)))return TIKU_MFS_CORRUPT;
    tiku_mfs_link_t ids=link_get(h+112),names=link_get(h+144);
    if((rc=catalog_roots(f,ids,names)))return rc;
    uint32_t c=UINT32_MAX;
    for(size_t at=0;at<f->bitmap_bytes;at+=512){
        size_t n=f->bitmap_bytes-at;if(n>512)n=512;
        rc=rd(f,base+1+at/512,1,p);if(rc)return rc;
        c=crc_more(c,p,n);if(load)memcpy(f->used+at,p,n);
    }
    if(~c!=get32(h+76))return TIKU_MFS_CORRUPT;
    if(load){
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        f->features=get32(h+88);
#endif
        f->ids=ids;f->names=names;f->bank=(uint8_t)bank;f->epoch=*epoch;f->sequence=get64(h+48);
        f->next_id=get64(h+56);f->tail=get64(h+64);f->cursor=get32(h+72);
        f->allocations=get64(h+96);
        if(!bit(f->used,0)||(f->tail&&!bit(f->used,(uint32_t)((f->tail-1)/TIKU_MFS_SEGMENT))))
            return TIKU_MFS_CORRUPT;
    }
    return 0;
}
int tiku_mfs_checkpoint(tiku_mfs_t *f)
{
    if(!f||!f->mounted)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    if(f->epoch==UINT64_MAX)return TIKU_MFS_LIMIT;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(f->features&2u)return TIKU_MFS_BUSY;
#endif
    uint8_t h[512],p[512];unsigned bank=1u-f->bank;
    uint64_t base=(uint64_t)bank*f->bank_sectors;
    memset(p,0,512);int rc=wr(f,base,1,p);if(rc||(rc=barrier(f)))return rc;
    for(size_t at=0;at<f->bitmap_bytes;at+=512){
        size_t n=f->bitmap_bytes-at;if(n>512)n=512;
        memset(p,0,512);memcpy(p,f->used+at,n);
        rc=wr(f,base+1+at/512,1,p);if(rc)return rc;
    }
    if((rc=barrier(f)))return rc;
    page(f,h,"MFC2");put64(h+32,f->block.sectors);put64(h+40,f->epoch+1);
    put64(h+48,f->sequence);put64(h+56,f->next_id);put64(h+64,f->tail);
    put32(h+72,f->cursor);put32(h+76,crc(f->used,f->bitmap_bytes));
    put32(h+84,2);put32(h+92,TIKU_MFS_SEGMENT);put64(h+96,f->allocations);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    put32(h+88,f->features&1u);
#endif
    roots_put(h,f->ids,f->names);seal(h);
    if((rc=wr(f,base,1,h))||(rc=barrier(f)))return rc;
    f->bank=(uint8_t)bank;f->epoch++;f->journal=0;f->recovered=0;return 0;
}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
/** @brief Format one new volume with the selected mandatory feature word. */
static int format_features(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],
                           void *used,void *mark,size_t capacity,unsigned features)
#else
int tiku_mfs_format(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],
                    void *used,void *mark,size_t capacity)
#endif
{
    int rc=setup(f,b,uuid,used,mark,capacity);if(rc)return rc;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    f->features=features;
#endif
    uint8_t p[512];
    for(unsigned bank=0;bank<2;bank++){
        if((rc=rd(f,(uint64_t)bank*f->bank_sectors,1,p)))return rc;
        if(valid(f,p,"MFC2")||valid(f,p,"MFC1"))return TIKU_MFS_ARG;
    }
    memset(p,0,sizeof p);
    if((rc=wr(f,0,1,p))||(rc=wr(f,f->bank_sectors,1,p))||(rc=barrier(f)))return rc;
    memset(f->used,0,f->bitmap_bytes);set(f->used,0);free_count(f);
    f->sequence=f->epoch=f->tail=f->allocations=0;f->next_id=1;f->cursor=1;
    f->bank=1;f->mounted=1;
    if((rc=tiku_mfs_checkpoint(f)))return rc;
    return tiku_mfs_checkpoint(f);
}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
int tiku_mfs_format(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],void *used,void *mark,size_t capacity)
{return format_features(f,b,uuid,used,mark,capacity,0);}
int tiku_mfs_format_directories(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],void *used,void *mark,size_t capacity)
{return format_features(f,b,uuid,used,mark,capacity,1);}
int tiku_mfs_has_directories(const tiku_mfs_t *f)
{return f&&f->mounted&&!f->failed&&f->features==1;}
int tiku_mfs_directory_upgrade_pending(const tiku_mfs_t *f)
{return f&&f->mounted&&!f->failed&&(f->features&2u); }
#endif
/** @brief Replay segment allocations in their rotating search order. */
static int replay_alloc(tiku_mfs_t *f,uint64_t allocations,uint64_t tail,uint32_t cursor)
{
    if(allocations<f->allocations||allocations-f->allocations>=f->segments||
       cursor<1||cursor>=f->segments||
       (tail&&(tail<TIKU_MFS_SEGMENT||tail>(uint64_t)f->segments*TIKU_MFS_SEGMENT)))
        return TIKU_MFS_CORRUPT;
    uint32_t last=0;
    while(f->allocations<allocations){
        unsigned found=0;
        for(uint32_t i=0;i<f->segments-1;i++){
            uint32_t s=f->cursor;f->cursor++;if(f->cursor==f->segments)f->cursor=1;
            if(!bit(f->used,s)){set(f->used,s);last=s;found=1;break;}
        }
        if(!found)return TIKU_MFS_CORRUPT;
        f->allocations++;
    }
    if(cursor!=f->cursor||(tail&&!bit(f->used,(uint32_t)((tail-1)/TIKU_MFS_SEGMENT)))||
       (last&&(!tail||(tail-1)/TIKU_MFS_SEGMENT!=last))||
       (!last&&(tail<f->tail||(tail&&f->tail&&
        (tail-1)/TIKU_MFS_SEGMENT!=(f->tail-1)/TIKU_MFS_SEGMENT))))return TIKU_MFS_CORRUPT;
    f->tail=tail;return 0;
}
int tiku_mfs_mount(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],
                   void *used,void *mark,size_t capacity)
{
    int rc=setup(f,b,uuid,used,mark,capacity);if(rc)return rc;
    uint64_t epochs[2]={0,0};int good[2];
    for(unsigned i=0;i<2;i++){good[i]=bank_load(f,i,0,&epochs[i]);if(good[i]==TIKU_MFS_IO)return good[i];}
    if(good[0]&&good[1])return good[0]==TIKU_MFS_NOFS&&good[1]==TIKU_MFS_NOFS?TIKU_MFS_NOFS:TIKU_MFS_CORRUPT;
    unsigned bank=good[0]?1:good[1]?0:epochs[1]>epochs[0];
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    unsigned transition=0;
    if(!good[0]&&!good[1]){
        uint8_t header[512];unsigned features[2];
        for(unsigned i=0;i<2;++i){if((rc=rd(f,(uint64_t)i*f->bank_sectors,1,header)))return rc;features[i]=get32(header+88);}
        if(features[0]!=features[1]){bank=features[0]?1:0;transition=2;}
    }
#endif
    if((rc=bank_load(f,bank,1,&epochs[bank])))return rc;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    f->features|=transition;
#endif
    uint8_t p[512],c[512];f->journal=0;
    for(unsigned i=0;i<TIKU_MFS_JOURNAL;i++){
        if(f->sequence==UINT64_MAX)break;
        uint64_t at=journal_lba(f,bank)+2*i;
        if((rc=rd(f,at+1,1,c)))return rc;
        if(!valid(f,c,"MFT2")||get64(c+24)!=f->epoch||get64(c+32)!=f->sequence+1||
           get64(c+40)!=f->sequence)break;
        if((rc=rd(f,at,1,p)))return rc;
        if(!valid(f,p,"MFR2")||get32(c+48)!=get32(p+508))return TIKU_MFS_CORRUPT;
        tiku_mfs_link_t ids=link_get(p+112),names=link_get(p+144);
        if((rc=catalog_roots(f,ids,names)))return rc;
        if((rc=replay_alloc(f,get64(c+88),get64(c+64),get32(c+80))))return rc;
        if(!get64(c+72)||get64(c+72)<f->next_id)return TIKU_MFS_CORRUPT;
        f->next_id=get64(c+72);f->ids=ids;f->names=names;
        f->sequence++;f->journal++;
    }
    free_count(f);
    f->mounted=1;f->recovered=1;return 0;
}
/** @brief Publish immutable catalog roots through two ordered barriers. */
static int publish(tiku_mfs_t *f,tiku_mfs_link_t ids,tiku_mfs_link_t names)
{
    if(f->failed)return TIKU_MFS_IO;
    if(f->sequence==UINT64_MAX)return TIKU_MFS_LIMIT;
    int rc;if((f->recovered||f->journal==TIKU_MFS_JOURNAL)&&(rc=tiku_mfs_checkpoint(f)))return rc;
    uint8_t p[512],c[512];page(f,p,"MFR2");roots_put(p,ids,names);seal(p);
    uint64_t at=journal_lba(f,f->bank)+2*f->journal;
    page(f,c,"MFT2");put64(c+24,f->epoch);put64(c+32,f->sequence+1);put64(c+40,f->sequence);
    put32(c+48,get32(p+508));put64(c+64,f->tail);put64(c+72,f->next_id);put32(c+80,f->cursor);
    put64(c+88,f->allocations);seal(c);
    if((rc=wr(f,at,1,p))||(rc=barrier(f))||(rc=wr(f,at+1,1,c))||(rc=barrier(f)))return rc;
    f->ids=ids;f->names=names;f->sequence++;f->journal++;return 0;
}
/** @brief Build an updated ID tree and optional name transaction without publishing. */
static int change_catalog(tiku_mfs_t *f,const tiku_mfs_object_t *o,int remove)
{
    catalog_node_t name,key={0};int rc=catalog_name(f,o->name,&name);
    if(rc!=0&&rc!=TIKU_MFS_NOENT)return rc;
    if(rc){memset(&name,0,sizeof name);name.kind=CATALOG_NAMES;memcpy(name.name,o->name,64);}
    uint64_t old_visible=name.visible,old_pending=name.pending,replace=0;
    if(remove){
        if(name.visible==o->id)name.visible=0;
        else if(name.pending==o->id)name.pending=0;
        else return TIKU_MFS_CORRUPT;
    }else if(o->visible){
        if(name.pending&&name.pending!=o->id)return TIKU_MFS_CORRUPT;
        replace=name.visible!=o->id?name.visible:0;name.visible=o->id;
        if(name.pending==o->id)name.pending=0;
    }else{
        if(name.pending&&name.pending!=o->id)return TIKU_MFS_CORRUPT;
        name.pending=o->id;
    }
    tiku_mfs_link_t ids=f->ids,names=f->names;
    key.kind=CATALOG_IDS;key.key=o->id;
    if(!remove){
        uint8_t p[512];uint64_t lba;object_encode(f,p,o);
        if((rc=allocate(f,1,&lba))||(rc=wr(f,lba,1,p)))return rc;
        key.object=(tiku_mfs_ref_t){lba,get32(p+508)};
    }
    if((rc=catalog_set(f,ids,&key,remove,&ids)))return rc;
    if(replace){key.key=replace;if((rc=catalog_set(f,ids,&key,1,&ids)))return rc;}
    if(name.visible!=old_visible||name.pending!=old_pending){
        if((rc=catalog_set(f,names,&name,!name.visible&&!name.pending,&names)))return rc;
    }
#if TIKU_MEDIA_NAMES_ENABLE
    int changed=old_visible!=name.visible||(o->visible&&o->complete&&!remove);
    if(changed&&names_version==UINT64_MAX)return TIKU_MFS_LIMIT;
    rc=publish(f,ids,names);
    if(!rc&&changed)f->names_epoch=++names_version;
    return rc;
#else
    return publish(f,ids,names);
#endif
}
/** @brief Commit one object transaction using reserved metadata space. */
static int commit(tiku_mfs_t *f,const tiku_mfs_object_t *o,int remove)
{
    if(f->failed)return TIKU_MFS_IO;
    f->catalog_write=1;int rc=change_catalog(f,o,remove);f->catalog_write=0;return rc;
}
/** @brief Extract a checksummed child pointer. */
static tiku_mfs_ref_t ref_get(const uint8_t *p,unsigned index)
{
    tiku_mfs_ref_t r={get64(p+REF_AT+16*index),get32(p+REF_AT+16*index+8)};return r;
}
/** @brief Store a checksummed child pointer. */
static void ref_put(uint8_t *p,unsigned index,tiku_mfs_ref_t r)
{ put64(p+REF_AT+16*index,r.lba);put32(p+REF_AT+16*index+8,r.crc); }
/** @brief Bind an extent-index page to its object, level and range. */
#if TIKU_MEDIA_ASYNC_ENABLE
static int node_decode(tiku_mfs_t *f,const tiku_mfs_object_t *o,tiku_mfs_ref_t r,
                       unsigned level,uint64_t base,const uint8_t p[512])
{
    if(!ref_ok(f,r)||!level||level>DEPTH)return TIKU_MFS_CORRUPT;
#else
static int node_read(tiku_mfs_t *f,const tiku_mfs_object_t *o,tiku_mfs_ref_t r,
                     unsigned level,uint64_t base,uint8_t p[512])
{
    if(!ref_ok(f,r)||!level||level>DEPTH)return TIKU_MFS_CORRUPT;
    int rc=rd(f,r.lba,1,p);if(rc)return rc;
#endif
    if(!valid(f,p,"MFN1")||get32(p+508)!=r.crc||get64(p+24)!=o->id||
       get64(p+32)!=base||get32(p+40)!=level)return TIKU_MFS_CORRUPT;
    for(unsigned i=0;i<FANOUT;i++){
        tiku_mfs_ref_t child=ref_get(p,i);
        if(child.lba&&!ref_ok(f,child))return TIKU_MFS_CORRUPT;
    }
    return 0;
}
#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Read and decode one extent page through the synchronous block provider. */
static int node_read(tiku_mfs_t *f,const tiku_mfs_object_t *o,tiku_mfs_ref_t r,
                     unsigned level,uint64_t base,uint8_t p[512])
{
    if(!ref_ok(f,r)||!level||level>DEPTH)return TIKU_MFS_CORRUPT;
    int rc=rd(f,r.lba,1,p);if(rc)return rc;
    return node_decode(f,o,r,level,base,p);
}
#endif
/** @brief Look up one logical 64 KiB chunk through at most twelve index pages. */
static int lookup(tiku_mfs_t *f,const tiku_mfs_object_t *o,uint64_t chunk,tiku_mfs_ref_t *out)
{
    tiku_mfs_ref_t r=o->root;uint64_t base=0;uint8_t p[512];
    if(!o->height||o->height>DEPTH||chunk>>(4*o->height))return TIKU_MFS_CORRUPT;
    for(unsigned level=o->height;level;level--){
        int rc=node_read(f,o,r,level,base,p);if(rc)return rc;
        unsigned child=(unsigned)((chunk>>(4*(level-1)))&15u);
        r=ref_get(p,child);base+=(uint64_t)child<<(4*(level-1));
    }
    if(!ref_ok(f,r))return TIKU_MFS_CORRUPT;
    *out=r;return 0;
}
/** @brief Replace one index path with newly allocated, checksummed pages. */
static int index_set(tiku_mfs_t *f,tiku_mfs_object_t *o,uint64_t chunk,tiku_mfs_ref_t data)
{
    tiku_mfs_ref_t path[DEPTH];uint64_t bases[DEPTH];uint8_t p[512];
    unsigned height=o->height?o->height:1;
    while(chunk>>(4*height))height++;
    if(height>DEPTH)return TIKU_MFS_LIMIT;
    tiku_mfs_ref_t r=o->root;uint64_t base=0;
    for(unsigned level=height;level;level--){
        bases[level-1]=base;
        if(level>o->height){path[level-1]=(tiku_mfs_ref_t){0,0};
            r=((chunk>>(4*(level-1)))&15u)?(tiku_mfs_ref_t){0,0}:r;
        }else{
            path[level-1]=r;
            if(r.lba){int rc=node_read(f,o,r,level,base,p);if(rc)return rc;
                r=ref_get(p,(unsigned)((chunk>>(4*(level-1)))&15u));}
        }
        base+=((chunk>>(4*(level-1)))&15u)<<(4*(level-1));
    }
    for(unsigned level=1;level<=height;level++){
        if(path[level-1].lba){int rc=node_read(f,o,path[level-1],level,bases[level-1],p);if(rc)return rc;}
        else{
            page(f,p,"MFN1");put64(p+24,o->id);put64(p+32,bases[level-1]);put32(p+40,level);
            if(level==(unsigned)o->height+1&&o->root.lba)ref_put(p,0,o->root);
        }
        ref_put(p,(unsigned)((chunk>>(4*(level-1)))&15u),data);seal(p);
        uint64_t lba;int rc=allocate(f,1,&lba);if(rc)return rc;
        if((rc=wr(f,lba,1,p)))return rc;
        data=(tiku_mfs_ref_t){lba,get32(p+508)};
    }
    o->root=data;o->height=(uint8_t)height;return 0;
}
#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Validate a frame header, object binding and segment bounds. */
static int frame_header_decode(tiku_mfs_t *f,const tiku_mfs_object_t *o,uint64_t chunk,
                                tiku_mfs_ref_t r,const uint8_t header[512])
{
    uint32_t length=get32(header+40),sectors=(length+511)/512;
    return !ref_ok(f,r)||!valid(f,header,"MFD1")||get32(header+508)!=r.crc||get64(header+24)!=o->id||
        get64(header+32)!=chunk||!length||length>TIKU_MFS_CHUNK||
        r.lba%TIKU_MFS_SEGMENT+sectors+2>TIKU_MFS_SEGMENT?TIKU_MFS_CORRUPT:0;
}
/** @brief Validate a footer against its header checksum and logical payload. */
static int frame_footer_decode(tiku_mfs_t *f,const tiku_mfs_object_t *o,uint64_t chunk,
                                uint32_t length,uint32_t checksum,const uint8_t footer[512])
{
    return !valid(f,footer,"MFE1")||get32(footer+508)!=checksum||get64(footer+24)!=o->id||
        get64(footer+32)!=chunk||get32(footer+40)!=length?TIKU_MFS_CORRUPT:0;
}
#endif
/** @brief Load a chunk header and its bound payload-checksum footer. */
static int frame_read(tiku_mfs_t *f,const tiku_mfs_object_t *o,uint64_t chunk,
                      tiku_mfs_ref_t r,uint8_t header[512],uint8_t footer[512])
{
    if(!ref_ok(f,r))return TIKU_MFS_CORRUPT;
    int rc=rd(f,r.lba,1,header);if(rc)return rc;
    uint32_t length=get32(header+40),sectors=(length+511)/512;
#if TIKU_MEDIA_ASYNC_ENABLE
    if((rc=frame_header_decode(f,o,chunk,r,header)))return rc;
#else
    if(!valid(f,header,"MFD1")||get32(header+508)!=r.crc||get64(header+24)!=o->id||
       get64(header+32)!=chunk||!length||length>TIKU_MFS_CHUNK||
       r.lba%TIKU_MFS_SEGMENT+sectors+2>TIKU_MFS_SEGMENT)return TIKU_MFS_CORRUPT;
#endif
    if((rc=rd(f,r.lba+1+sectors,1,footer)))return rc;
#if TIKU_MEDIA_ASYNC_ENABLE
    if((rc=frame_footer_decode(f,o,chunk,length,get32(header+44),footer)))return rc;
#else
    if(!valid(f,footer,"MFE1")||get32(footer+508)!=get32(header+44)||
       get64(footer+24)!=o->id||get64(footer+32)!=chunk||get32(footer+40)!=length)return TIKU_MFS_CORRUPT;
#endif
    return 0;
}
/** @brief Read and verify one payload slice of up to 4096 bytes. */
static int slice_read(tiku_mfs_t *f,tiku_mfs_ref_t r,const uint8_t *footer,
                      unsigned slice,uint8_t p[4096])
{
    uint32_t length=get32(footer+40),at=slice*4096;
    if(at>=length)return TIKU_MFS_CORRUPT;
    unsigned bytes=length-at;if(bytes>4096)bytes=4096;
    int rc=rd(f,r.lba+1+slice*8,(bytes+511)/512,p);if(rc)return rc;
    return crc(p,bytes)==get32(footer+64+4*slice)?0:TIKU_MFS_CORRUPT;
}
/** @brief Write a new chunk, preserving and checking an existing partial prefix. */
static int frame_append(tiku_mfs_t *f,tiku_mfs_object_t *o,const uint8_t *data,unsigned n)
{
    uint64_t chunk=o->length/TIKU_MFS_CHUNK;unsigned prefix=(unsigned)(o->length%TIKU_MFS_CHUNK);
    uint8_t h[512],foot[512],oldfoot[512],buffer[4096];tiku_mfs_ref_t old={0,0};
    int rc;
    if(prefix){
        if((rc=lookup(f,o,chunk,&old))||(rc=frame_read(f,o,chunk,old,h,oldfoot)))return rc;
        if(get32(h+40)!=prefix)return TIKU_MFS_CORRUPT;
    }
    unsigned length=prefix+n,sectors=(length+511)/512;uint64_t lba;
    if((rc=allocate(f,sectors+2,&lba)))return rc;
    page(f,foot,"MFE1");put64(foot+24,o->id);put64(foot+32,chunk);put32(foot+40,length);
    for(unsigned at=0;at<length;at+=4096){
        unsigned bytes=length-at;if(bytes>4096)bytes=4096;
        memset(buffer,0,sizeof buffer);
        unsigned keep=at<prefix?prefix-at:0;if(keep>bytes)keep=bytes;
        if(keep&&(rc=slice_read(f,old,oldfoot,at/4096,buffer)))return rc;
        if(bytes>keep)memcpy(buffer+keep,data+(at+keep-prefix),bytes-keep);
        memset(buffer+bytes,0,((bytes+511)/512)*512-bytes);
        put32(foot+64+4*(at/4096),crc(buffer,bytes));
        if((rc=wr(f,lba+1+at/512,(bytes+511)/512,buffer)))return rc;
    }
    seal(foot);if((rc=wr(f,lba+1+sectors,1,foot)))return rc;
    page(f,h,"MFD1");put64(h+24,o->id);put64(h+32,chunk);put32(h+40,length);
    put32(h+44,get32(foot+508));seal(h);if((rc=wr(f,lba,1,h)))return rc;
    return index_set(f,o,chunk,(tiku_mfs_ref_t){lba,get32(h+508)});
}
/** @brief Refuse mutation while unmounted, failed, collecting or awaiting feature migration. */
static int mutable(tiku_mfs_t *f)
{ return !f||!f->mounted?TIKU_MFS_ARG:f->failed?TIKU_MFS_IO:
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    (f->features&2u)?TIKU_MFS_BUSY:
#endif
    f->collecting?TIKU_MFS_BUSY:0; }
/** @brief Check parent files and child prefixes through the name index. */
static int namespace_free(tiku_mfs_t *f,const char *name)
{
    char prefix[TIKU_MFS_NAME];size_t length=strlen(name);catalog_node_t n;int rc;
    memcpy(prefix,name,length+1);
    for(size_t i=0;i<length;i++)if(prefix[i]=='/'){
        prefix[i]=0;rc=catalog_name(f,prefix,&n);prefix[i]='/';
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(!rc&&n.visible&&!n.pending){tiku_mfs_object_t parent;
            int result=catalog_object(f,n.visible,&parent);if(result)return result;
            if(parent.policy==TIKU_MFS_DIRECTORY)continue;}
#endif
        if(!rc)return TIKU_MFS_BUSY;
        if(rc!=TIKU_MFS_NOENT)return rc;
    }
    if(length+1<TIKU_MFS_NAME){
        prefix[length]='/';prefix[length+1]=0;rc=catalog_lower(f,prefix,&n);
        if(!rc&&!strncmp(prefix,n.name,length+1))return TIKU_MFS_BUSY;
        if(rc&&rc!=TIKU_MFS_NOENT)return rc;
    }
    return 0;
}
int tiku_mfs_begin(tiku_mfs_t *f,const char *name,tiku_mfs_policy_t policy,
                   uint64_t expected,const uint8_t digest[32],uint64_t *id)
{
    int rc=mutable(f);if(rc)return rc;
    if(!id||!name_ok(name)||(policy!=TIKU_MFS_STREAM&&policy!=TIKU_MFS_ATOMIC)||
       (policy==TIKU_MFS_ATOMIC&&(!digest||expected>UINT64_MAX/8)))return TIKU_MFS_ARG;
    if(f->writing)return TIKU_MFS_BUSY;
    catalog_node_t n;rc=catalog_name(f,name,&n);
    if(!rc&&(policy==TIKU_MFS_STREAM||n.pending))return TIKU_MFS_BUSY;
    if(rc&&rc!=TIKU_MFS_NOENT)return rc;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(!rc&&n.visible){tiku_mfs_object_t existing;rc=catalog_object(f,n.visible,&existing);if(rc)return rc;
        if(existing.policy==TIKU_MFS_DIRECTORY)return TIKU_MFS_BUSY;}
#endif
    if((rc=namespace_free(f,name)))return rc;
    if(f->next_id==UINT64_MAX)return TIKU_MFS_LIMIT;
    uint64_t available=(uint64_t)f->free_segments*TIKU_MFS_SEGMENT+
        (f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-f->tail%TIKU_MFS_SEGMENT:0);
    if(available<CATALOG_RESERVE+3u*(f->ids.height+f->names.height+2u)+1u)return TIKU_MFS_SPACE;
    tiku_mfs_object_t o;memset(&o,0,sizeof o);o.id=f->next_id++;
    memcpy(o.name,name,strlen(name)+1);o.policy=(uint8_t)policy;o.visible=policy==TIKU_MFS_STREAM;
    o.expected=policy==TIKU_MFS_ATOMIC?expected:0;if(digest)memcpy(o.expected_digest,digest,32);
    hash_init(&o.hash);
    if((rc=commit(f,&o,0)))return rc;
    f->draft=o;f->writing=1;*id=o.id;return 0;
}
int tiku_mfs_resume(tiku_mfs_t *f,uint64_t id)
{
    int rc=mutable(f);if(rc)return rc;if(f->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;if((rc=catalog_object(f,id,&o)))return rc;
    if(o.complete)return TIKU_MFS_ARG;
    f->draft=o;f->writing=1;return 0;
}
int tiku_mfs_append(tiku_mfs_t *f,const void *data,uint32_t n)
{
    int rc=mutable(f);if(rc)return rc;
    if(!f->writing||!data||!n||n>TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    tiku_mfs_object_t o=f->draft;
    if(n>UINT64_MAX/8-o.length||(o.policy==TIKU_MFS_ATOMIC&&n>o.expected-o.length))return TIKU_MFS_LIMIT;
    const uint8_t *p=data;uint32_t remaining=n;
    while(remaining){
        unsigned take=TIKU_MFS_CHUNK-(unsigned)(o.length%TIKU_MFS_CHUNK);if(take>remaining)take=remaining;
        if((rc=frame_append(f,&o,p,take)))return rc;
        hash_add(&o.hash,o.length,p,take);o.length+=take;p+=take;remaining-=take;
    }
    f->draft=o;return 0;
}
int tiku_mfs_sync(tiku_mfs_t *f)
{
    int rc=mutable(f);if(rc)return rc;if(!f->writing)return TIKU_MFS_ARG;
    tiku_mfs_object_t old;if((rc=catalog_object(f,f->draft.id,&old)))return rc;
    if(old.length==f->draft.length)return 0;
    return commit(f,&f->draft,0);
}
int tiku_mfs_finalize(tiku_mfs_t *f)
{
    int rc=mutable(f);if(rc)return rc;if(!f->writing)return TIKU_MFS_ARG;
    tiku_mfs_object_t o=f->draft;hash_end(o.hash,o.length,o.digest);
    if(o.policy==TIKU_MFS_ATOMIC&&(o.length!=o.expected||memcmp(o.digest,o.expected_digest,32)))return TIKU_MFS_DIGEST;
    o.complete=o.visible=1;
    if((rc=commit(f,&o,0)))return rc;
    f->writing=0;return 0;
}
int tiku_mfs_release(tiku_mfs_t *f)
{ if(!f||!f->mounted)return TIKU_MFS_ARG;f->writing=0;return 0; }
#if TIKU_MEDIA_DIRECTORIES_ENABLE
/** @brief Refuse removing a directory that still has visible or pending children. */
static int directory_empty(tiku_mfs_t *f,const tiku_mfs_object_t *o)
{
    if(o->policy!=TIKU_MFS_DIRECTORY)return 0;
    size_t n=strlen(o->name);if(n+1>=64)return 0;
    char prefix[64];memcpy(prefix,o->name,n);prefix[n++]='/';prefix[n]=0;
    catalog_node_t child;int rc=catalog_lower(f,prefix,&child);
    return rc==TIKU_MFS_NOENT?0:rc?rc:!strncmp(prefix,child.name,n)?TIKU_MFS_BUSY:0;
}
#endif
int tiku_mfs_delete(tiku_mfs_t *f,uint64_t id)
{
    int rc=mutable(f);if(rc)return rc;if(f->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;if((rc=catalog_object(f,id,&o)))return rc;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if((rc=directory_empty(f,&o)))return rc;
#endif
    return commit(f,&o,1);
}
int tiku_mfs_delete_version(tiku_mfs_t *f,uint64_t id,const char *source)
{
    int rc=mutable(f);if(rc)return rc;
    if(!id||!name_ok(source))return TIKU_MFS_ARG;
    if(f->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;catalog_node_t name;
    if((rc=catalog_object(f,id,&o)))return rc;
    if(!o.visible||!o.complete||strcmp(o.name,source))return TIKU_MFS_STALE;
    if((rc=catalog_name(f,source,&name)))return rc;
    if(name.visible!=id)return TIKU_MFS_STALE;
    if(name.pending)return TIKU_MFS_BUSY;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if((rc=directory_empty(f,&o)))return rc;
#endif
    return commit(f,&o,1);
}
/** @brief Build renamed object and name roots, then publish their single transaction. */
static int rename_catalog(tiku_mfs_t *f,tiku_mfs_object_t *o,catalog_node_t *old,const char *target)
{
    tiku_mfs_link_t ids=f->ids,names=f->names;
    catalog_node_t key={0},name={0};uint8_t p[512];uint64_t lba;int rc;
    memset(o->name,0,sizeof o->name);memcpy(o->name,target,strlen(target));
    object_encode(f,p,o);
    if((rc=allocate(f,1,&lba))||(rc=wr(f,lba,1,p)))return rc;
    key.kind=CATALOG_IDS;key.key=o->id;key.object=(tiku_mfs_ref_t){lba,get32(p+508)};
    if((rc=catalog_set(f,ids,&key,0,&ids)))return rc;
    if((rc=catalog_set(f,names,old,1,&names)))return rc;
    name.kind=CATALOG_NAMES;name.visible=o->id;memcpy(name.name,o->name,sizeof name.name);
    if((rc=catalog_set(f,names,&name,0,&names)))return rc;
    rc=publish(f,ids,names);
#if TIKU_MEDIA_NAMES_ENABLE
    if(!rc)f->names_epoch=++names_version;
#endif
    return rc;
}
int tiku_mfs_rename(tiku_mfs_t *f,uint64_t id,const char *source,const char *target)
{
    int rc=mutable(f);if(rc)return rc;
    if(!id||!name_ok(source)||!name_ok(target))return TIKU_MFS_ARG;
    if(f->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;catalog_node_t old,destination;
    if((rc=catalog_object(f,id,&o)))return rc;
    if(!o.visible||!o.complete||strcmp(o.name,source))return TIKU_MFS_STALE;
    if((rc=catalog_name(f,source,&old)))return rc;
    if(old.visible!=id)return TIKU_MFS_STALE;
    if(old.pending)return TIKU_MFS_BUSY;
    if(!strcmp(source,target))return 0;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if((rc=directory_empty(f,&o)))return rc;
    size_t source_length=strlen(source);
    if(o.policy==TIKU_MFS_DIRECTORY&&!strncmp(source,target,source_length)&&target[source_length]=='/')return TIKU_MFS_BUSY;
#endif
    rc=catalog_name(f,target,&destination);
    if(!rc)return TIKU_MFS_BUSY;
    if(rc!=TIKU_MFS_NOENT)return rc;
    if((rc=namespace_free(f,target)))return rc;
#if TIKU_MEDIA_NAMES_ENABLE
    if(names_version==UINT64_MAX)return TIKU_MFS_LIMIT;
#endif
    f->catalog_write=1;rc=rename_catalog(f,&o,&old,target);f->catalog_write=0;
    return rc;
}
int tiku_mfs_stat(tiku_mfs_t *f,const char *name,tiku_mfs_object_t *out)
{
    if(!f||!f->mounted||!out||!name_ok(name))return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    catalog_node_t n;int rc=catalog_name(f,name,&n);if(rc)return rc;
    if(!n.visible)return TIKU_MFS_NOENT;
    if((rc=catalog_object(f,n.visible,out)))return rc;
    return out->visible&&!strcmp(name,out->name)?0:TIKU_MFS_CORRUPT;
}
/** @brief Select a ranked visible or pending name without scanning other objects. */
static int catalog_rank(tiku_mfs_t *f,uint64_t rank,int pending,catalog_node_t *out)
{
    tiku_mfs_link_t r=f->names;
    if(rank>=(pending?r.pending:r.count))return TIKU_MFS_NOENT;
    while(r.ref.lba){
        int rc=catalog_read(f,r,CATALOG_NAMES,out);if(rc)return rc;
        uint64_t left=pending?out->child[0].pending:out->child[0].count;
        unsigned here=pending?!!out->pending:!!out->visible;
        if(rank<left){r=out->child[0];continue;}
        rank-=left;if(!rank&&here)return 0;
        rank-=here;r=out->child[1];
    }
    return TIKU_MFS_CORRUPT;
}
/** @brief Initialize or validate a mutation-sensitive, mount-scoped listing cursor. */
static int listing_cursor(tiku_mfs_t *f,tiku_mfs_cursor_t *c,int pending)
{
    if(!f||!f->mounted||!c)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    if(!c->started){
        *c=(tiku_mfs_cursor_t){f->generation,f->sequence,0,1,(uint8_t)pending};return 0;
    }
    return c->generation==f->generation&&c->sequence==f->sequence&&c->pending==pending?
        0:TIKU_MFS_STALE;
}
/** @brief Enumerate a ranked namespace view and validate its object binding. */
static int list_object(tiku_mfs_t *f,tiku_mfs_cursor_t *c,tiku_mfs_object_t *out,int pending)
{
    if(!out)return TIKU_MFS_ARG;
    int rc=listing_cursor(f,c,pending);if(rc)return rc;
    catalog_node_t n;if((rc=catalog_rank(f,c->rank,pending,&n)))return rc;
    if((rc=catalog_object(f,pending?n.pending:n.visible,out)))return rc;
    if(strcmp(n.name,out->name)||out->visible==pending)return TIKU_MFS_CORRUPT;
    c->rank++;return 0;
}
int tiku_mfs_list(tiku_mfs_t *f,tiku_mfs_cursor_t *c,tiku_mfs_object_t *out)
{ return list_object(f,c,out,0); }
int tiku_mfs_list_pending(tiku_mfs_t *f,tiku_mfs_cursor_t *c,tiku_mfs_object_t *out)
{ return list_object(f,c,out,1); }
int tiku_mfs_list_seek(tiku_mfs_t *f,const char *prefix,tiku_mfs_cursor_t *c)
{
    if(!f||!f->mounted||!prefix||!c)return TIKU_MFS_ARG;
    unsigned len=0;while(len<TIKU_MFS_NAME&&prefix[len])len++;
    if(len==TIKU_MFS_NAME)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    uint64_t rank=0;tiku_mfs_link_t r=f->names;
    while(r.ref.lba){
        catalog_node_t n;int rc=catalog_read(f,r,CATALOG_NAMES,&n);if(rc)return rc;
        if(strcmp(n.name,prefix)<0){rank+=n.child[0].count+!!n.visible;r=n.child[1];}
        else r=n.child[0];
    }
    *c=(tiku_mfs_cursor_t){f->generation,f->sequence,rank,1,0};return 0;
}
#if TIKU_MEDIA_NAMES_ENABLE
uint64_t tiku_mfs_names_epoch(const tiku_mfs_t *f)
{ return f&&f->mounted&&!f->failed?f->names_epoch:0; }

int tiku_mfs_enumerate(tiku_mfs_t *f,const char *lower,int exclusive,
                       uint64_t expected,tiku_mfs_object_t *out,uint64_t *rank,uint64_t *epoch)
{
    if(!f||!out||!rank||!epoch||(exclusive!=0&&exclusive!=1))return TIKU_MFS_ARG;
    uint64_t version=tiku_mfs_names_epoch(f);
    if(!version)return TIKU_MFS_IO;
    if(expected&&expected!=version)return TIKU_MFS_STALE;
    tiku_mfs_cursor_t cursor={0};
    int rc=tiku_mfs_list_seek(f,lower,&cursor);if(rc)return rc;
    uint64_t position=cursor.rank;
    rc=tiku_mfs_list(f,&cursor,out);
    if(!rc&&exclusive&&!strcmp(out->name,lower)) {
        position=cursor.rank;
        rc=tiku_mfs_list(f,&cursor,out);
    }
    if(!rc||rc==TIKU_MFS_NOENT){*rank=position;*epoch=version;}
    return rc;
}

#endif
int tiku_mfs_open_read(tiku_mfs_t *f,const char *name,uint64_t *token)
{
    if(!f||!token)return TIKU_MFS_ARG;
    if(f->collecting)return TIKU_MFS_BUSY;
    if(f->next_token==UINT64_MAX)return TIKU_MFS_LIMIT;
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(!f->readers[i].token){
        int rc=tiku_mfs_stat(f,name,&f->readers[i].object);if(rc)return rc;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(f->readers[i].object.policy==TIKU_MFS_DIRECTORY)return TIKU_MFS_ARG;
#endif
        *token=f->readers[i].token=f->next_token++;return 0;
    }
    return TIKU_MFS_LIMIT;
}
/** @brief Find a live snapshot reader by token. */
static tiku_mfs_reader_t *reader(tiku_mfs_t *f,uint64_t token)
{
    if(f&&f->mounted&&token)for(unsigned i=0;i<TIKU_MFS_READERS;i++)
        if(f->readers[i].token==token)return &f->readers[i];
    return NULL;
}
int tiku_mfs_read_at(tiku_mfs_t *f,uint64_t token,uint64_t offset,void *data,uint32_t n)
{
    tiku_mfs_reader_t *r=reader(f,token);if(!r)return TIKU_MFS_STALE;
    if((!data&&n)||n>TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    const tiku_mfs_object_t *o=&r->object;
    if(offset>=o->length||!n)return 0;
    if(n>o->length-offset)n=(uint32_t)(o->length-offset);
    uint32_t total=n;uint8_t *out=data,h[512],foot[512],slice[4096];
    while(n){
        uint64_t chunk=offset/TIKU_MFS_CHUNK;tiku_mfs_ref_t ref;
        int rc=lookup(f,o,chunk,&ref);if(rc)return rc;
        if((rc=frame_read(f,o,chunk,ref,h,foot)))return rc;
        unsigned within=(unsigned)(offset%TIKU_MFS_CHUNK),length=get32(h+40);
        uint64_t expected=o->length-chunk*TIKU_MFS_CHUNK;if(expected>TIKU_MFS_CHUNK)expected=TIKU_MFS_CHUNK;
        if(length!=expected)return TIKU_MFS_CORRUPT;
        while(n&&within<length){
            unsigned s=within/4096,skip=within%4096,take=4096-skip;
            if(take>n)take=n;
            if(take>length-within)take=length-within;
            if((rc=slice_read(f,ref,foot,s,slice)))return rc;
            memcpy(out,slice+skip,take);out+=take;offset+=take;within+=take;n-=take;
        }
    }
    return (int)total;
}
int tiku_mfs_close_read(tiku_mfs_t *f,uint64_t token)
{ tiku_mfs_reader_t *r=reader(f,token);if(!r)return TIKU_MFS_STALE;r->token=0;return 0; }
int tiku_mfs_collect_begin(tiku_mfs_t *f)
{
    int rc=mutable(f);if(rc)return rc;if(f->writing)return TIKU_MFS_BUSY;
    if((rc=tiku_mfs_checkpoint(f))||(rc=tiku_mfs_checkpoint(f)))return rc;
    memset(f->mark,0,f->bitmap_bytes);set(f->mark,0);
    f->collecting=1;f->gc_phase=f->gc_active=0;
    f->gc_reader=f->gc_depth=f->catalog_depth=0;return 0;
}
/** @brief Start a bounded traversal of one object's extent tree. */
static void gc_start_object(tiku_mfs_t *f,const tiku_mfs_object_t *o)
{
    f->gc_value=*o;f->gc_depth=0;
    if(o->root.lba){f->walk[0]=(tiku_mfs_walk_t){o->root,0,o->height,0};f->gc_depth=1;}
}
/** @brief Visit one extent-index page and mark its referenced data segments. */
static int gc_extent_step(tiku_mfs_t *f)
{
    tiku_mfs_walk_t *w=&f->walk[f->gc_depth-1];uint8_t p[512];
    int rc=node_read(f,&f->gc_value,w->ref,w->level,w->base,p);if(rc)return rc;
    set(f->mark,(uint32_t)(w->ref.lba/TIKU_MFS_SEGMENT));
    if(w->level==1){
        for(unsigned i=0;i<FANOUT;i++){
            tiku_mfs_ref_t child=ref_get(p,i);
            if(child.lba)set(f->mark,(uint32_t)(child.lba/TIKU_MFS_SEGMENT));
        }
        f->gc_depth--;return 0;
    }
    while(w->child<FANOUT){
        unsigned i=w->child++;tiku_mfs_ref_t child=ref_get(p,i);if(!child.lba)continue;
        if(f->gc_depth==DEPTH)return TIKU_MFS_CORRUPT;
        f->walk[f->gc_depth++]=(tiku_mfs_walk_t){child,w->base+((uint64_t)i<<(4*(w->level-1))),
            (uint8_t)(w->level-1),0};return 0;
    }
    f->gc_depth--;return 0;
}
/** @brief Visit one catalog node, including the bound object descriptor for an ID node. */
static int gc_catalog_step(tiku_mfs_t *f)
{
    unsigned kind=f->gc_phase?CATALOG_IDS:CATALOG_NAMES;
    if(!f->gc_active){
        tiku_mfs_link_t root=f->gc_phase?f->ids:f->names;f->gc_active=1;
        if(root.ref.lba){f->catalog_walk[0]=(tiku_mfs_catalog_walk_t){root,0};f->catalog_depth=1;}
    }
    if(!f->catalog_depth){f->gc_phase++;f->gc_active=0;return 0;}
    tiku_mfs_catalog_walk_t *w=&f->catalog_walk[f->catalog_depth-1];catalog_node_t n;
    int rc=catalog_read(f,w->link,kind,&n);if(rc)return rc;
    if(!w->stage){
        set(f->mark,(uint32_t)(w->link.ref.lba/TIKU_MFS_SEGMENT));w->stage=1;
        if(kind==CATALOG_IDS){
            uint8_t p[512];tiku_mfs_object_t o;
            if((rc=rd(f,n.object.lba,1,p)))return rc;
            if(get32(p+508)!=n.object.crc)return TIKU_MFS_CORRUPT;
            if((rc=object_decode(f,p,&o)))return rc;
            if(o.id!=n.key||o.id>=f->next_id)return TIKU_MFS_CORRUPT;
            set(f->mark,(uint32_t)(n.object.lba/TIKU_MFS_SEGMENT));gc_start_object(f,&o);
            return 0;
        }
    }
    while(w->stage<3){
        unsigned side=w->stage++-1;if(!n.child[side].ref.lba)continue;
        if(f->catalog_depth==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
        f->catalog_walk[f->catalog_depth++]=(tiku_mfs_catalog_walk_t){n.child[side],0};return 0;
    }
    f->catalog_depth--;return 0;
}
int tiku_mfs_collect_step(tiku_mfs_t *f,unsigned budget)
{
    if(!f||!f->mounted||!f->collecting||!budget)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    while(budget--){
        int rc=0;
        if(f->gc_depth)rc=gc_extent_step(f);
        else if(f->gc_phase<2)rc=gc_catalog_step(f);
        else{
            if(f->gc_reader==TIKU_MFS_READERS)break;
            tiku_mfs_reader_t *r=&f->readers[f->gc_reader++];
            if(r->token)gc_start_object(f,&r->object);
        }
        if(rc){f->failed=1;return rc;}
    }
    if(f->gc_phase<2||f->gc_depth||f->gc_reader<TIKU_MFS_READERS)return 1;
    memcpy(f->used,f->mark,f->bitmap_bytes);f->tail=0;free_count(f);
    int rc=tiku_mfs_checkpoint(f);if(!rc)rc=tiku_mfs_checkpoint(f);
    if(!rc)f->collecting=0;
    return rc;
}

#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Resumable query phases, each consuming at most one supplied sector. */
enum { QUERY_SEEK=1, QUERY_RANK, QUERY_ID, QUERY_OBJECT, QUERY_INDEX, QUERY_HEADER, QUERY_FOOTER, QUERY_PAYLOAD };
/** @brief Publish a terminal result without retaining a sector request. */
static int query_done(tiku_mfs_query_t *q,int result)
{ q->result=result;q->done=1;q->waiting=q->have=0;return 0; }
/** @brief Check the serialized mount/catalog state captured by a query. */
static int query_current(tiku_mfs_t *f,const tiku_mfs_query_t *q)
{
    if(!f||!f->mounted||f->generation!=q->generation||f->sequence!=q->sequence)return TIKU_MFS_STALE;
    return f->failed?TIKU_MFS_IO:0;
}
/** @brief Initialize a query without touching the block provider. */
static int query_init(tiku_mfs_t *f,tiku_mfs_query_t *q)
{
    if(!f||!q||!f->mounted)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    memset(q,0,sizeof *q);q->generation=f->generation;q->sequence=f->sequence;
    q->names_epoch=f->names_epoch;return 0;
}
int tiku_mfs_query_enumerate(tiku_mfs_t *f,tiku_mfs_query_t *q,const char *lower,int exclusive,uint64_t expected)
{
    if(!lower||(exclusive!=0&&exclusive!=1))return TIKU_MFS_ARG;
    unsigned n=0;while(n<64&&lower[n])n++;
    if(n==64)return TIKU_MFS_ARG;
    int rc=query_init(f,q);if(rc)return rc;
    if(expected&&expected!=f->names_epoch)return TIKU_MFS_STALE;
    memcpy(q->lower,lower,n+1);q->exclusive=(uint8_t)exclusive;
    q->link=f->names;q->phase=QUERY_SEEK;return 0;
}
/** @brief Select the extent root for the next checked slice. */
static void query_index(tiku_mfs_query_t *q)
{
    q->chunk=q->offset/TIKU_MFS_CHUNK;q->base=0;
    q->ref=q->object.root;q->level=q->object.height;q->phase=QUERY_INDEX;
}
int tiku_mfs_query_read(tiku_mfs_t *f,tiku_mfs_query_t *q,uint64_t token,uint64_t offset,void *output,uint32_t length)
{
    if((!output&&length)||length>TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    tiku_mfs_reader_t *r=reader(f,token);if(!r)return TIKU_MFS_STALE;
    int rc=query_init(f,q);if(rc)return rc;
    q->mode=1;q->object=r->object;q->output=output;q->offset=offset;
    if(offset>=q->object.length||!length){query_done(q,0);return 0;}
    q->total=length;if(q->total>q->object.length-offset)q->total=(uint32_t)(q->object.length-offset);
    query_index(q);return 0;
}
int tiku_mfs_query_supply(tiku_mfs_query_t *q,int result)
{
    if(!q||q->done||!q->waiting||result>0)return TIKU_MFS_ARG;
    q->waiting=0;q->have=1;q->io_result=result;return 0;
}
/** @brief Ask the transport for one checked extent-relative sector. */
static int query_sector(tiku_mfs_t *f,tiku_mfs_query_t *q,uint64_t lba)
{
    if(lba>=f->block.sectors)return query_done(q,TIKU_MFS_CORRUPT);
    q->sector_lba=lba;q->waiting=1;return 1;
}
int tiku_mfs_query_step(tiku_mfs_t *f,tiku_mfs_query_t *q)
{
    if(!q)return TIKU_MFS_ARG;
    if(q->done)return 0;
    int rc=query_current(f,q);if(rc)return query_done(q,rc);
    if(q->waiting)return 1;
    if(q->have&&q->io_result){f->failed=1;return query_done(q,TIKU_MFS_IO);}
    if(q->phase==QUERY_SEEK&&!q->link.ref.lba){
        if(q->rank>=f->names.count)return query_done(q,TIKU_MFS_NOENT);
        q->phase=QUERY_RANK;q->link=f->names;q->position=q->rank;q->depth=0;return 1;
    }
    if(!q->have){
        uint64_t lba=q->phase<=QUERY_ID?q->link.ref.lba:q->ref.lba;
        if(q->phase==QUERY_FOOTER)lba+=1+(q->frame_length+511)/512;
        if(q->phase==QUERY_PAYLOAD)lba+=1+(q->offset%TIKU_MFS_CHUNK/4096)*8+q->slice_filled/512;
        return query_sector(f,q,lba);
    }
    q->have=0;
    if(q->phase<=QUERY_ID){
        catalog_node_t node;
        if(++q->depth>TIKU_MFS_CATALOG_DEPTH)return query_done(q,TIKU_MFS_CORRUPT);
        rc=catalog_decode(f,q->link,q->phase==QUERY_ID?CATALOG_IDS:CATALOG_NAMES,&node,q->sector);
        if(rc)return query_done(q,rc);
        if(q->phase==QUERY_SEEK){
            int cmp=strcmp(node.name,q->lower);
            unsigned right=cmp<0||(!cmp&&q->exclusive);
            if(right)q->rank+=node.child[0].count+!!node.visible;
            q->link=node.child[right];
        }else if(q->phase==QUERY_RANK){
            uint64_t left=node.child[0].count;
            if(q->position<left)q->link=node.child[0];
            else if(q->position==left&&node.visible){
                q->id=node.visible;memcpy(q->found,node.name,64);
                q->link=f->ids;q->phase=QUERY_ID;q->depth=0;
            }else {q->position-=left+!!node.visible;q->link=node.child[1];}
        }else if(node.key==q->id){q->ref=node.object;q->phase=QUERY_OBJECT;}
        else q->link=node.child[q->id>node.key];
        return 1;
    }
    if(q->phase==QUERY_OBJECT){
        if(get32(q->sector+508)!=q->ref.crc)return query_done(q,TIKU_MFS_CORRUPT);
        rc=object_decode(f,q->sector,&q->object);
        if(!rc&&(q->object.id!=q->id||q->id>=f->next_id||!q->object.visible||strcmp(q->object.name,q->found)))
            rc=TIKU_MFS_CORRUPT;
        return query_done(q,rc);
    }
    if(q->phase==QUERY_INDEX){
        rc=node_decode(f,&q->object,q->ref,q->level,q->base,q->sector);
        if(rc)return query_done(q,rc);
        unsigned child=(unsigned)((q->chunk>>(4*(q->level-1)))&15u);
        q->ref=ref_get(q->sector,child);q->base+=(uint64_t)child<<(4*(q->level-1));
        if(!ref_ok(f,q->ref))return query_done(q,TIKU_MFS_CORRUPT);
        if(!--q->level)q->phase=QUERY_HEADER;
        return 1;
    }
    if(q->phase==QUERY_HEADER){
        rc=frame_header_decode(f,&q->object,q->chunk,q->ref,q->sector);
        if(rc)return query_done(q,rc);
        q->frame_length=get32(q->sector+40);q->footer_crc=get32(q->sector+44);
        uint64_t expected=q->object.length-q->chunk*TIKU_MFS_CHUNK;
        if(expected>TIKU_MFS_CHUNK)expected=TIKU_MFS_CHUNK;
        if(q->frame_length!=expected)return query_done(q,TIKU_MFS_CORRUPT);
        q->phase=QUERY_FOOTER;return 1;
    }
    if(q->phase==QUERY_FOOTER){
        rc=frame_footer_decode(f,&q->object,q->chunk,q->frame_length,q->footer_crc,q->sector);
        if(rc)return query_done(q,rc);
        unsigned slice=(unsigned)(q->offset%TIKU_MFS_CHUNK)/4096;
        if(slice*4096>=q->frame_length)return query_done(q,TIKU_MFS_CORRUPT);
        q->expected_crc=get32(q->sector+64+4*slice);
        q->slice_length=q->frame_length-slice*4096;if(q->slice_length>4096)q->slice_length=4096;
        q->slice_filled=0;q->slice_crc=UINT32_MAX;q->phase=QUERY_PAYLOAD;return 1;
    }
    if(q->phase==QUERY_PAYLOAD){
        unsigned n=q->slice_length-q->slice_filled;if(n>512)n=512;
        memcpy(q->slice+q->slice_filled,q->sector,n);q->slice_crc=crc_more(q->slice_crc,q->sector,n);
        q->slice_filled+=n;
        if(q->slice_filled<q->slice_length)return 1;
        if(~q->slice_crc!=q->expected_crc)return query_done(q,TIKU_MFS_CORRUPT);
        unsigned skip=(unsigned)(q->offset%4096),take=q->slice_length-skip;
        if(take>q->total-q->copied)take=q->total-q->copied;
        memcpy(q->output+q->copied,q->slice+skip,take);q->copied+=take;q->offset+=take;
        if(q->copied==q->total)return query_done(q,(int)q->copied);
        query_index(q);return 1;
    }
    return query_done(q,TIKU_MFS_ARG);
}
int tiku_mfs_query_open(tiku_mfs_t *f,tiku_mfs_query_t *q,uint64_t *token)
{
    if(!q||!token||!q->done||q->result||q->mode||q->pinned)return TIKU_MFS_ARG;
    int rc=query_current(f,q);if(rc)return rc;
    if(f->collecting)return TIKU_MFS_BUSY;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(q->object.policy==TIKU_MFS_DIRECTORY)return TIKU_MFS_ARG;
#endif
    if(f->next_token==UINT64_MAX)return TIKU_MFS_LIMIT;
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(!f->readers[i].token){
        f->readers[i].object=q->object;*token=f->readers[i].token=f->next_token++;q->pinned=1;return 0;
    }
    return TIKU_MFS_LIMIT;
}
#endif

#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Complete a checkpoint without retaining transport output. */
static int checkpoint_done(tiku_mfs_checkpoint_t *c,int result)
{ c->result=result;c->done=1;c->waiting=c->have=0;return 0; }
int tiku_mfs_checkpoint_begin(tiku_mfs_t *f,tiku_mfs_checkpoint_t *c)
{
    int rc=mutable(f);if(rc)return rc;
    if(!c)return TIKU_MFS_ARG;
    if(f->epoch==UINT64_MAX)return TIKU_MFS_LIMIT;
    *c=(tiku_mfs_checkpoint_t){.generation=f->generation,.sequence=f->sequence,.epoch=f->epoch,
        .base=(uint64_t)(1u-f->bank)*f->bank_sectors,.checksum=UINT32_MAX,.phase=1};
    return 0;
}
int tiku_mfs_checkpoint_supply(tiku_mfs_checkpoint_t *c,int result)
{
    if(!c||c->done||!c->waiting||result>0)return TIKU_MFS_ARG;
    c->waiting=0;c->have=1;c->io_result=result;return 0;
}
int tiku_mfs_checkpoint_cancel(tiku_mfs_t *f,tiku_mfs_checkpoint_t *c)
{
    if(!f||!c)return TIKU_MFS_ARG;
    if(c->done)return 0;
    if(c->waiting)return TIKU_MFS_BUSY;
    if(c->changed)f->failed=1;
    checkpoint_done(c,TIKU_MFS_STALE);return 0;
}
int tiku_mfs_checkpoint_step(tiku_mfs_t *f,tiku_mfs_checkpoint_t *c)
{
    if(!c)return TIKU_MFS_ARG;
    if(c->done)return 0;
    if(!f||!f->mounted||f->generation!=c->generation||f->sequence!=c->sequence||f->epoch!=c->epoch)
        return checkpoint_done(c,TIKU_MFS_STALE);
    if(f->failed)return checkpoint_done(c,TIKU_MFS_IO);
    if(c->waiting)return 1;
    if(c->have){
        c->have=0;
        if(c->io_result){f->failed=1;return checkpoint_done(c,TIKU_MFS_IO);}
    }
    switch(c->phase){
    case 1:
        memset(c->sector,0,512);c->sector_lba=c->base;c->changed=1;
        c->waiting=TIKU_MFS_CHECKPOINT_WRITE;c->phase=2;return 1;
    case 2:c->waiting=TIKU_MFS_CHECKPOINT_SYNC;c->phase=3;return 1;
    case 3:
        if(c->offset<f->bitmap_bytes){
            size_t n=f->bitmap_bytes-c->offset;if(n>512)n=512;
            memset(c->sector,0,512);memcpy(c->sector,f->used+c->offset,n);
            c->checksum=crc_more(c->checksum,c->sector,n);
            c->sector_lba=c->base+1+c->offset/512;c->offset+=n;
            c->waiting=TIKU_MFS_CHECKPOINT_WRITE;return 1;
        }
        c->waiting=TIKU_MFS_CHECKPOINT_SYNC;c->phase=4;return 1;
    case 4:
        page(f,c->sector,"MFC2");put64(c->sector+32,f->block.sectors);put64(c->sector+40,f->epoch+1);
        put64(c->sector+48,f->sequence);put64(c->sector+56,f->next_id);put64(c->sector+64,f->tail);
        put32(c->sector+72,f->cursor);put32(c->sector+76,~c->checksum);
        put32(c->sector+84,2);put32(c->sector+92,TIKU_MFS_SEGMENT);put64(c->sector+96,f->allocations);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        put32(c->sector+88,f->features&1u);
#endif
        roots_put(c->sector,f->ids,f->names);seal(c->sector);
        c->sector_lba=c->base;c->waiting=TIKU_MFS_CHECKPOINT_WRITE;c->phase=5;return 1;
    case 5:c->waiting=TIKU_MFS_CHECKPOINT_SYNC;c->phase=6;return 1;
    case 6:
        f->bank=(uint8_t)(1u-f->bank);f->epoch++;f->journal=0;f->recovered=0;
        return checkpoint_done(c,0);
    default:return checkpoint_done(c,TIKU_MFS_ARG);
    }
}
#endif

#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Resumable copy-on-write AVL edit with bounded ancestor storage. */
typedef struct {
    tiku_mfs_link_t path[TIKU_MFS_CATALOG_DEPTH],root;
    catalog_node_t value,node,successor,child,middle;
    const catalog_node_t *write_node;
    uint32_t scanned;
    uint8_t directions[TIKU_MFS_CATALOG_DEPTH];
    unsigned depth,replace_depth,phase,after_write,remove,heavy;
} catalog_edit_t;
/** @brief Retained extent path and checksums while appending one chunk. */
typedef struct {
    tiku_mfs_ref_t path[DEPTH],ref,data,old;
    uint64_t bases[DEPTH],base,chunk;
    uint32_t checksums[16],old_crc,footer_crc;
    unsigned height,level,prefix,length,at,take;
} mutation_frame_t;
/** @brief Private state retained through the final barrier and transport retirement. */
struct tiku_mfs_mutation {
    union {
        struct { catalog_edit_t edit; tiku_mfs_checkpoint_t checkpoint; };
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        tiku_mfs_query_t copy_query;
#endif
        mutation_frame_t stream_frame;
    };
    tiku_mfs_object_t object;
    catalog_node_t seek_key,seek_node,old_name;
    tiku_mfs_link_t seek_link,ids,names;
    uint64_t generation,sequence,epoch,id,sector_lba;
    uint32_t root_crc,scanned,payload_size,payload_at,slice_crc;
    uint64_t frame_lba;
    const uint8_t *payload;
    uint8_t footer[512];
    char source[64],target[64],prefix[64];
    uint8_t sector[512];
    int result,io_result;
    unsigned phase,seek_phase,seek_depth,seek_lower,seek_found,prefix_at;
    uint8_t waiting,have,done,changed,remove,replace,stream;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    uint8_t directory,tree,tree_started,copy,copy_ready;
    uint8_t copy_digest[32];
    void *copy_buffer;
    uint64_t copy_id,copy_prepared,copy_total;
    uint64_t tree_id;
    uint32_t tree_count;
    char tree_source[64],tree_target[64];
#endif
};
/** @brief Transaction phases, including validation before any allocation. */
enum {
    MUT_ID=1,MUT_OBJECT,MUT_SOURCE,MUT_TARGET,MUT_PARENT,MUT_CHILD,
    MUT_PREPARE,MUT_CHECKPOINT,MUT_BUILD,MUT_DESCRIPTOR,MUT_IDS,MUT_OLD,
    MUT_NEW,MUT_ROOT,MUT_ROOT_SYNC,MUT_COMMIT,MUT_COMMIT_SYNC,MUT_PUBLISH,
    MUT_REPLACE_TARGET,MUT_FILE_ALLOC,MUT_FILE_HASH,MUT_FILE_DATA,MUT_FILE_FOOTER,
    MUT_FILE_HEADER,MUT_FILE_INDEX,MUT_FILE_DESCRIPTOR,MUT_REPLACED,
    MUT_PARENT_ID,MUT_PARENT_OBJECT,MUT_DIRECTORY_CHILD,
    MUT_TREE_IMPLICIT,MUT_TREE_START,MUT_TREE_NEXT,MUT_TREE_END,MUT_COPY_READ,
    MUT_STREAM_NAME,MUT_STREAM_FRAME,MUT_STREAM_DESCEND,MUT_STREAM_DECODE,
    MUT_STREAM_HEADER,MUT_STREAM_FOOTER,MUT_STREAM_ALLOC,MUT_STREAM_SECTOR,
    MUT_STREAM_HASH,MUT_STREAM_DATA,MUT_STREAM_END,MUT_STREAM_HEAD,
    MUT_STREAM_ASCEND,MUT_STREAM_NODE,MUT_STREAM_NODE_ALLOC,MUT_STREAM_NODE_WRITTEN,
    MUT_UPGRADE_INIT,MUT_UPGRADE_CHECKPOINT,MUT_UPGRADE_READ,MUT_UPGRADE_RETIRE,MUT_UPGRADE_SYNC,MUT_UPGRADE_DONE
};
size_t tiku_mfs_mutation_size(void){return sizeof(tiku_mfs_mutation_t);}
tiku_mfs_mutation_t *tiku_mfs_mutation_init(void *memory,size_t size)
{
    if(!memory||size<sizeof(tiku_mfs_mutation_t)||(uintptr_t)memory%_Alignof(tiku_mfs_mutation_t))return NULL;
    memset(memory,0,sizeof(tiku_mfs_mutation_t));return memory;
}
/** @brief Complete an operation, freezing an interrupted allocation/publication. */
static int mutation_done(tiku_mfs_t *f,tiku_mfs_mutation_t *m,int result)
{
    if(result&&(m->changed
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ||m->directory==2
#endif
        )&&f&&f->generation==m->generation)f->failed=1;
    m->result=result;m->done=1;m->waiting=m->have=0;return 0;
}
/** @brief Request one sector or barrier without invoking the block provider. */
static int mutation_io(tiku_mfs_mutation_t *m,unsigned kind,uint64_t lba)
{
    m->sector_lba=lba;m->waiting=(uint8_t)kind;
    if(kind!=TIKU_MFS_MUTATION_READ)m->changed=1;
    return 1;
}
/** @brief Initialize an exact or lower-bound catalog lookup. */
static void mutation_seek(tiku_mfs_mutation_t *m,tiku_mfs_link_t root,unsigned kind,
                          uint64_t id,const char *name,int lower)
{
    m->seek_key=(catalog_node_t){.kind=kind,.key=id};
    if(name)memcpy(m->seek_key.name,name,strlen(name)+1);
    m->seek_link=root;m->seek_phase=m->seek_depth=m->seek_found=0;m->seek_lower=(unsigned)lower;
}
/** @brief Advance one lookup read or decode; return 1 pending, zero found, negative absent/error. */
static int mutation_seek_step(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    if(!m->seek_phase){
        if(!m->seek_link.ref.lba)return m->seek_found?0:TIKU_MFS_NOENT;
        if(m->seek_depth++==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
        m->seek_phase=1;return mutation_io(m,TIKU_MFS_MUTATION_READ,m->seek_link.ref.lba);
    }
    catalog_node_t n;int rc=catalog_decode(f,m->seek_link,m->seek_key.kind,&n,m->sector);
    if(rc)return rc;
    int cmp=catalog_compare(&m->seek_key,&n);
    #if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(!cmp&&m->seek_lower==2){m->seek_link=n.child[1];m->seek_phase=0;return 1;}
#endif
    if(!cmp){m->seek_node=n;return 0;}
    if(m->seek_lower&&cmp<0){m->seek_node=n;m->seek_found=1;}
    m->seek_link=n.child[cmp>0];m->seek_phase=0;return 1;
}
/** @brief Reserve contiguous sectors, inspecting at most 64 segment bits per call. */
static int mutation_reserve(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint32_t *scanned,unsigned count,uint64_t *lba)
{
    m->changed=1;
    unsigned left=f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-(unsigned)(f->tail%TIKU_MFS_SEGMENT):0;
    if(left>=count){*lba=f->tail;f->tail+=count;return 0;}
    if(f->allocations==UINT64_MAX)return TIKU_MFS_LIMIT;
    for(unsigned n=0;n<64&&*scanned<f->segments-1;++n,++*scanned){
        uint32_t s=f->cursor;f->cursor++;if(f->cursor==f->segments)f->cursor=1;
        if(!bit(f->used,s)){
            set(f->used,s);f->allocations++;f->free_segments--;
            *lba=(uint64_t)s*TIKU_MFS_SEGMENT;f->tail=*lba+count;return 0;
        }
    }
    return *scanned==f->segments-1?TIKU_MFS_SPACE:1;
}
/** @brief Reserve one catalog sector through the bounded allocator. */
static int mutation_allocate(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint32_t *scanned,uint64_t *lba)
{ return mutation_reserve(f,m,scanned,1,lba); }
/** @brief Start an unpublished catalog edit. */
static void mutation_edit(tiku_mfs_mutation_t *m,tiku_mfs_link_t root,const catalog_node_t *value,int remove)
{
    m->edit=(catalog_edit_t){.root=root,.value=*value,.remove=(unsigned)remove,
        .replace_depth=TIKU_MFS_CATALOG_DEPTH,.phase=1};
}
/** @brief Schedule a node write and its continuation after I/O retirement. */
static int mutation_edit_write(catalog_edit_t *e,const catalog_node_t *n,unsigned after)
{ e->write_node=n;e->after_write=after;e->phase=20;e->scanned=0;return 1; }
/** @brief Advance one catalog traversal, rotation, allocation slice or sector operation. */
static int mutation_edit_step(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    catalog_edit_t *e=&m->edit;catalog_node_t *n=&e->node;int rc;
    switch(e->phase){
    case 1:
        if(!e->root.ref.lba){
            if(e->remove)return TIKU_MFS_NOENT;
            *n=e->value;n->child[0]=n->child[1]=(tiku_mfs_link_t){0};
            return mutation_edit_write(e,n,10);
        }
        if(e->depth==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
        e->phase=2;return mutation_io(m,TIKU_MFS_MUTATION_READ,e->root.ref.lba);
    case 2:{
        if((rc=catalog_decode(f,e->root,e->value.kind,n,m->sector)))return rc;
        int cmp=catalog_compare(&e->value,n);
        if(cmp){
            e->path[e->depth]=e->root;e->directions[e->depth++]=(uint8_t)(cmp>0);
            e->root=n->child[cmp>0];e->phase=1;return 1;
        }
        if(!e->remove){
            tiku_mfs_link_t left=n->child[0],right=n->child[1];*n=e->value;n->child[0]=left;n->child[1]=right;
            return mutation_edit_write(e,n,10);
        }
        if(!n->child[0].ref.lba||!n->child[1].ref.lba){
            e->root=n->child[n->child[0].ref.lba?0:1];e->phase=10;return 1;
        }
        e->replace_depth=e->depth;e->path[e->depth]=e->root;e->directions[e->depth++]=1;
        e->root=n->child[1];e->phase=3;return 1;
    }
    case 3:
        if(e->depth==TIKU_MFS_CATALOG_DEPTH)return TIKU_MFS_CORRUPT;
        e->phase=4;return mutation_io(m,TIKU_MFS_MUTATION_READ,e->root.ref.lba);
    case 4:
        if((rc=catalog_decode(f,e->root,e->value.kind,&e->successor,m->sector)))return rc;
        if(!e->successor.child[0].ref.lba){e->root=e->successor.child[1];e->phase=10;return 1;}
        e->path[e->depth]=e->root;e->directions[e->depth++]=0;e->root=e->successor.child[0];e->phase=3;return 1;
    case 10:
        if(!e->depth)return 0;
        --e->depth;e->phase=11;return mutation_io(m,TIKU_MFS_MUTATION_READ,e->path[e->depth].ref.lba);
    case 11:{
        if((rc=catalog_decode(f,e->path[e->depth],e->value.kind,n,m->sector)))return rc;
        if(e->depth==e->replace_depth){
            n->key=e->successor.key;n->visible=e->successor.visible;n->pending=e->successor.pending;
            n->object=e->successor.object;memcpy(n->name,e->successor.name,64);
        }
        n->child[e->directions[e->depth]]=e->root;
        int balance=(int)n->child[0].height-(int)n->child[1].height;
        if(balance>=-1&&balance<=1)return mutation_edit_write(e,n,10);
        if(balance < -2||balance>2)return TIKU_MFS_CORRUPT;
        e->heavy=balance>0?0:1;e->phase=12;
        return mutation_io(m,TIKU_MFS_MUTATION_READ,n->child[e->heavy].ref.lba);
    }
    case 12:
        if((rc=catalog_decode(f,n->child[e->heavy],e->value.kind,&e->child,m->sector)))return rc;
        if(e->child.child[e->heavy].height>=e->child.child[1-e->heavy].height){
            n->child[e->heavy]=e->child.child[1-e->heavy];return mutation_edit_write(e,n,13);
        }
        e->phase=14;return mutation_io(m,TIKU_MFS_MUTATION_READ,e->child.child[1-e->heavy].ref.lba);
    case 13:
        e->child.child[1-e->heavy]=e->root;return mutation_edit_write(e,&e->child,10);
    case 14:
        if((rc=catalog_decode(f,e->child.child[1-e->heavy],e->value.kind,&e->middle,m->sector)))return rc;
        e->child.child[1-e->heavy]=e->middle.child[e->heavy];n->child[e->heavy]=e->middle.child[1-e->heavy];
        return mutation_edit_write(e,&e->child,15);
    case 15:e->middle.child[e->heavy]=e->root;return mutation_edit_write(e,n,16);
    case 16:e->middle.child[1-e->heavy]=e->root;return mutation_edit_write(e,&e->middle,10);
    case 20:{
        tiku_mfs_link_t r=node_link(e->write_node);uint64_t lba;
        if(r.height>TIKU_MFS_CATALOG_DEPTH||r.count+r.pending>f->block.sectors)return TIKU_MFS_LIMIT;
        rc=mutation_allocate(f,m,&e->scanned,&lba);if(rc)return rc;
        const catalog_node_t *w=e->write_node;uint8_t *p=m->sector;
        page(f,p,"MFI2");put32(p+24,w->kind);link_put(p+32,w->child[0]);link_put(p+64,w->child[1]);
        put64(p+96,w->key);put64(p+104,w->visible);put64(p+112,w->pending);
        put64(p+120,w->object.lba);put32(p+128,w->object.crc);memcpy(p+144,w->name,64);seal(p);
        r.ref=(tiku_mfs_ref_t){lba,get32(p+508)};e->root=r;e->phase=21;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,lba);
    }
    case 21:e->phase=e->after_write;return 1;
    default:return TIKU_MFS_ARG;
    }
}
int tiku_mfs_mutation_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t id,const char *source,const char *target)
{
    int rc=mutable(f);if(rc)return rc;
    if(!m||!id||!name_ok(source)||(target&&!name_ok(target)))return TIKU_MFS_ARG;
    if(f->writing||(m->phase&&!m->done))return TIKU_MFS_BUSY;
    memset(m,0,sizeof *m);m->generation=f->generation;m->sequence=f->sequence;m->epoch=f->epoch;
    m->id=id;m->ids=f->ids;m->names=f->names;m->remove=target==NULL;
    memcpy(m->source,source,strlen(source)+1);if(target)memcpy(m->target,target,strlen(target)+1);
    mutation_seek(m,f->ids,CATALOG_IDS,id,NULL,0);m->phase=MUT_ID;return 0;
}
int tiku_mfs_mutation_replace_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t expected_id,
                                  const char *name,const void *payload,uint32_t bytes)
{
    int rc=mutable(f);if(rc)return rc;
    if(!m||!name_ok(name)||bytes>TIKU_MFS_CHUNK||(bytes&&!payload))return TIKU_MFS_ARG;
    if(f->writing||(m->phase&&!m->done))return TIKU_MFS_BUSY;
    memset(m,0,sizeof *m);m->generation=f->generation;m->sequence=f->sequence;m->epoch=f->epoch;
    m->id=expected_id;m->ids=f->ids;m->names=f->names;m->replace=1;m->payload=payload;m->payload_size=bytes;
    memcpy(m->source,name,strlen(name)+1);memcpy(m->target,m->source,64);
    mutation_seek(m,f->names,CATALOG_NAMES,0,name,0);m->phase=MUT_REPLACE_TARGET;return 0;
}
int tiku_mfs_mutation_upload_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t expected_id,
                                  const char *name,tiku_mfs_policy_t policy,uint64_t expected,const uint8_t digest[32])
{
    if((policy!=TIKU_MFS_STREAM&&policy!=TIKU_MFS_ATOMIC)||
       (policy==TIKU_MFS_STREAM&&expected_id)||
       (policy==TIKU_MFS_ATOMIC&&(!digest||expected>UINT64_MAX/8)))return TIKU_MFS_ARG;
    int rc=tiku_mfs_mutation_replace_begin(f,m,expected_id,name,NULL,0);if(rc)return rc;
    m->stream=1;m->footer[0]=(uint8_t)policy;put64(m->footer+8,expected);
    if(digest)memcpy(m->footer+16,digest,32);
    return 0;
}
/** @brief Initialize a serialized writer operation without filesystem access. */
static int mutation_writer_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,unsigned operation)
{
    int rc=mutable(f);if(rc)return rc;
    if(!m)return TIKU_MFS_ARG;
    if((m->phase&&!m->done)||(operation==2?f->writing:!f->writing))return TIKU_MFS_BUSY;
    memset(m,0,sizeof *m);m->generation=f->generation;m->sequence=f->sequence;m->epoch=f->epoch;
    m->ids=f->ids;m->names=f->names;m->stream=(uint8_t)operation;return 0;
}
int tiku_mfs_mutation_resume_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t id)
{
    if(!id)return TIKU_MFS_ARG;
    int rc=mutation_writer_begin(f,m,2);if(rc)return rc;
    m->id=id;mutation_seek(m,f->ids,CATALOG_IDS,id,NULL,0);m->phase=MUT_ID;return 0;
}
int tiku_mfs_mutation_append_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,const void *payload,uint32_t bytes)
{
    if(!payload||!bytes||bytes>TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    int rc=mutable(f);if(rc)return rc;
    if(!f->writing)return TIKU_MFS_ARG;
    if(f->draft.length>UINT64_MAX/8||bytes>UINT64_MAX/8-f->draft.length||
       (f->draft.policy==TIKU_MFS_ATOMIC&&(f->draft.length>f->draft.expected||bytes>f->draft.expected-f->draft.length)))return TIKU_MFS_LIMIT;
    rc=mutation_writer_begin(f,m,3);if(rc)return rc;
    m->object=f->draft;m->id=m->object.id;m->payload=payload;m->payload_size=bytes;
    m->phase=MUT_STREAM_FRAME;return 0;
}
int tiku_mfs_mutation_flush_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,int final)
{
    int rc=mutable(f);if(rc)return rc;
    if(!f->writing)return TIKU_MFS_ARG;
    rc=mutation_writer_begin(f,m,final?5:4);if(rc)return rc;
    m->object=f->draft;m->id=m->object.id;memcpy(m->target,m->object.name,64);
    if(final){
        hash_end(m->object.hash,m->object.length,m->object.digest);
        if(m->object.policy==TIKU_MFS_ATOMIC&&(m->object.length!=m->object.expected||
           memcmp(m->object.digest,m->object.expected_digest,32))){mutation_done(f,m,TIKU_MFS_DIGEST);return TIKU_MFS_DIGEST;}
        m->object.complete=m->object.visible=1;
    }
    mutation_seek(m,f->names,CATALOG_NAMES,0,m->target,0);m->phase=MUT_STREAM_NAME;return 0;
}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
int tiku_mfs_mutation_upgrade_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    if(!f||!f->mounted||!m)return TIKU_MFS_ARG;
    if(f->failed)return TIKU_MFS_IO;
    if(f->writing||f->collecting||(m->phase&&!m->done))return TIKU_MFS_BUSY;
    memset(m,0,sizeof *m);m->generation=f->generation;m->sequence=f->sequence;m->epoch=f->epoch;
    m->directory=2;m->phase=MUT_UPGRADE_INIT;return 0;
}
int tiku_mfs_mutation_tree_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t id,const char *source,const char *target)
{
    int rc=mutable(f);if(rc)return rc;
    if(!m||!name_ok(source)||(target&&!name_ok(target)))return TIKU_MFS_ARG;
    if(f->writing||(m->phase&&!m->done))return TIKU_MFS_BUSY;
    size_t n=strlen(source);
    if(target&&!strncmp(source,target,n)&&target[n]=='/')return TIKU_MFS_BUSY;
    if(id){rc=tiku_mfs_mutation_begin(f,m,id,source,target);if(rc)return rc;}
    else {
        memset(m,0,sizeof *m);m->generation=f->generation;m->sequence=f->sequence;m->epoch=f->epoch;
        m->ids=f->ids;m->names=f->names;m->remove=target==NULL;
        memcpy(m->source,source,n+1);if(target)memcpy(m->target,target,strlen(target)+1);
        mutation_seek(m,f->names,CATALOG_NAMES,0,source,0);m->phase=MUT_TREE_IMPLICIT;
    }
    m->tree=1;m->tree_id=id;memcpy(m->tree_source,m->source,64);memcpy(m->tree_target,m->target,64);return 0;
}
int tiku_mfs_mutation_tree_copy_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,uint64_t id,
                                      const char *source,const char *target,void *buffer,uint32_t capacity)
{
    if(!source||!target||!buffer||capacity<TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    if(!strcmp(source,target))return TIKU_MFS_BUSY;
    int rc=tiku_mfs_mutation_tree_begin(f,m,id,source,target);if(rc)return rc;
    m->copy=1;m->copy_buffer=buffer;return 0;
}
int tiku_mfs_mutation_progress(const tiku_mfs_mutation_t *m,uint64_t *prepared,uint64_t *total)
{
    if(!m||!prepared||!total||!m->copy)return TIKU_MFS_ARG;
    *prepared=m->copy_prepared;*total=m->tree==1?0:m->copy_total;return 0;
}
int tiku_mfs_mutation_mkdir_begin(tiku_mfs_t *f,tiku_mfs_mutation_t *m,const char *name)
{
    if(!f||!f->mounted)return TIKU_MFS_ARG;
    if(!tiku_mfs_has_directories(f))return TIKU_MFS_UNSUPPORTED;
    int rc=tiku_mfs_mutation_replace_begin(f,m,0,name,NULL,0);if(!rc)m->directory=1;return rc;
}
#endif
int tiku_mfs_mutation_io(tiku_mfs_mutation_t *m,uint64_t *lba,void **sector)
{
    if(!m||!lba||!sector||m->done)return 0;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(m->phase==MUT_COPY_READ){*lba=m->copy_query.sector_lba;*sector=m->copy_query.sector;
        return m->copy_query.waiting?TIKU_MFS_MUTATION_READ:0;}
#endif
    if((m->phase==MUT_CHECKPOINT
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ||m->phase==MUT_UPGRADE_CHECKPOINT
#endif
        )){
        *lba=m->checkpoint.sector_lba;*sector=m->checkpoint.sector;
        return m->checkpoint.waiting==TIKU_MFS_CHECKPOINT_WRITE?TIKU_MFS_MUTATION_WRITE:
            m->checkpoint.waiting==TIKU_MFS_CHECKPOINT_SYNC?TIKU_MFS_MUTATION_SYNC:0;
    }
    *lba=m->sector_lba;*sector=m->sector;return m->waiting;
}
int tiku_mfs_mutation_supply(tiku_mfs_mutation_t *m,int result)
{
    if(!m||m->done||result>0)return TIKU_MFS_ARG;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(m->phase==MUT_COPY_READ)return tiku_mfs_query_supply(&m->copy_query,result);
#endif
    if((m->phase==MUT_CHECKPOINT
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ||m->phase==MUT_UPGRADE_CHECKPOINT
#endif
        ))return tiku_mfs_checkpoint_supply(&m->checkpoint,result);
    if(!m->waiting)return TIKU_MFS_ARG;
    m->waiting=0;m->have=1;m->io_result=result;return 0;
}
int tiku_mfs_mutation_result(const tiku_mfs_mutation_t *m)
{ return !m?TIKU_MFS_ARG:m->done?m->result:TIKU_MFS_BUSY; }
uint64_t tiku_mfs_mutation_id(const tiku_mfs_mutation_t *m)
{ return m&&m->done&&!m->result&&!m->remove?m->id:0; }
int tiku_mfs_mutation_cancel(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    if(!f||!m)return TIKU_MFS_ARG;
    if(m->done)return 0;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if(m->phase==MUT_COPY_READ&&m->copy_query.waiting)return TIKU_MFS_BUSY;
#endif
    if(m->waiting||((m->phase==MUT_CHECKPOINT
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ||m->phase==MUT_UPGRADE_CHECKPOINT
#endif
        )&&m->checkpoint.waiting))return TIKU_MFS_BUSY;
    if((m->phase==MUT_CHECKPOINT
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ||m->phase==MUT_UPGRADE_CHECKPOINT
#endif
        )&&m->checkpoint.changed)m->changed=1;
    mutation_done(f,m,TIKU_MFS_STALE);return 0;
}
/** @brief Select the next parent-file check, then the target's first possible child. */
static void mutation_parent(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    size_t length=strlen(m->target);memcpy(m->prefix,m->target,length+1);
    for(unsigned i=m->prefix_at;i<length;++i)if(m->target[i]=='/'){
        m->prefix_at=i+1;m->prefix[i]=0;
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->prefix,0);m->phase=MUT_PARENT;return;
    }
    if(length+1<64){
        m->prefix[length]='/';m->prefix[length+1]=0;
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->prefix,1);m->phase=MUT_CHILD;
    }else m->phase=MUT_PREPARE;
}
/** @brief Start publication of the new visible name into the candidate catalog. */
static void mutation_new_name(tiku_mfs_mutation_t *m)
{
    catalog_node_t key={.kind=CATALOG_NAMES,.visible=m->id};memcpy(key.name,m->target,64);
    if(m->stream){
        key=m->old_name;key.kind=CATALOG_NAMES;memcpy(key.name,m->target,64);
        if(m->object.visible){key.visible=m->id;if(key.pending==m->id)key.pending=0;}
        else key.pending=m->id;
    }
    mutation_edit(m,m->names,&key,0);m->phase=MUT_NEW;
}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
/** @brief Seek the next original descendant without retaining a catalog-sized list. */
static void mutation_tree_next(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    size_t n=strlen(m->tree_source);
    if(!strcmp(m->source,m->tree_source)){
        if(n==63){m->phase=MUT_TREE_END;return;}
        memcpy(m->prefix,m->tree_source,n);m->prefix[n++]='/';m->prefix[n]=0;
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->prefix,1);
    }else mutation_seek(m,f->names,CATALOG_NAMES,0,m->source,2);
    m->phase=MUT_TREE_NEXT;
}
#endif
/** @brief Descend one extent level after selecting its child. */
static void mutation_frame_down(mutation_frame_t *c)
{
    c->base+=((c->chunk>>(4*(c->level-1)))&15u)<<(4*(c->level-1));
    --c->level;
}
/** @brief Advance an append by one extent phase, sector or 64 payload bytes. */
static int mutation_append_step(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    mutation_frame_t *c=&m->stream_frame;uint8_t *p=m->sector;int rc;
    switch(m->phase){
    case MUT_STREAM_FRAME:{
        memset(c,0,sizeof *c);c->chunk=m->object.length/TIKU_MFS_CHUNK;
        c->prefix=(unsigned)(m->object.length%TIKU_MFS_CHUNK);
        c->take=m->payload_size-m->payload_at;if(c->take>TIKU_MFS_CHUNK-c->prefix)c->take=TIKU_MFS_CHUNK-c->prefix;
        c->length=c->prefix+c->take;c->height=m->object.height?m->object.height:1;
        while(c->height<=DEPTH&&(c->chunk>>(4*c->height)))++c->height;
        if(c->height>DEPTH)return mutation_done(f,m,TIKU_MFS_LIMIT);
        unsigned left=f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-(unsigned)(f->tail%TIKU_MFS_SEGMENT):0;
        if((uint64_t)f->free_segments*TIKU_MFS_SEGMENT+left<
           (c->length+511)/512+2u+c->height+CATALOG_RESERVE)return mutation_done(f,m,TIKU_MFS_SPACE);
        c->ref=m->object.root;c->level=c->height;m->phase=MUT_STREAM_DESCEND;return 1;
    }
    case MUT_STREAM_DESCEND:
        if(!c->level){
            if(c->prefix){
                c->old=c->ref;if(!ref_ok(f,c->old))return mutation_done(f,m,TIKU_MFS_CORRUPT);
                m->phase=MUT_STREAM_HEADER;return mutation_io(m,TIKU_MFS_MUTATION_READ,c->old.lba);
            }
            m->phase=MUT_STREAM_ALLOC;return 1;
        }
        c->bases[c->level-1]=c->base;
        if(c->level>m->object.height){
            if((c->chunk>>(4*(c->level-1)))&15u)c->ref=(tiku_mfs_ref_t){0};
        }else {
            c->path[c->level-1]=c->ref;
            if(c->ref.lba){m->phase=MUT_STREAM_DECODE;return mutation_io(m,TIKU_MFS_MUTATION_READ,c->ref.lba);}
        }
        mutation_frame_down(c);return 1;
    case MUT_STREAM_DECODE:
        rc=node_decode(f,&m->object,c->ref,c->level,c->base,p);if(rc)return mutation_done(f,m,rc);
        c->ref=ref_get(p,(unsigned)((c->chunk>>(4*(c->level-1)))&15u));
        mutation_frame_down(c);m->phase=MUT_STREAM_DESCEND;return 1;
    case MUT_STREAM_HEADER:
        rc=frame_header_decode(f,&m->object,c->chunk,c->old,p);if(rc)return mutation_done(f,m,rc);
        if(get32(p+40)!=c->prefix)return mutation_done(f,m,TIKU_MFS_CORRUPT);
        c->footer_crc=get32(p+44);m->phase=MUT_STREAM_FOOTER;
        return mutation_io(m,TIKU_MFS_MUTATION_READ,c->old.lba+1+(c->prefix+511)/512);
    case MUT_STREAM_FOOTER:
        rc=frame_footer_decode(f,&m->object,c->chunk,c->prefix,c->footer_crc,p);if(rc)return mutation_done(f,m,rc);
        for(unsigned i=0;i<16;++i)c->checksums[i]=get32(p+64+4*i);
        m->phase=MUT_STREAM_ALLOC;return 1;
    case MUT_STREAM_ALLOC:
        rc=mutation_reserve(f,m,&m->scanned,(c->length+511)/512+2,&m->frame_lba);
        if(rc==1)return 1;
        if(rc)return mutation_done(f,m,rc);
        m->scanned=0;m->slice_crc=c->old_crc=UINT32_MAX;
        page(f,m->footer,"MFE1");put64(m->footer+24,m->id);put64(m->footer+32,c->chunk);put32(m->footer+40,c->length);
        m->phase=MUT_STREAM_SECTOR;return 1;
    case MUT_STREAM_SECTOR:
        m->phase=MUT_STREAM_HASH;
        if(c->at<c->prefix)return mutation_io(m,TIKU_MFS_MUTATION_READ,c->old.lba+1+c->at/512);
        memset(p,0,512);return 1;
    case MUT_STREAM_HASH:{
        unsigned n=c->length-c->at;if(n>64)n=64;
        if(n>512-c->at%512)n=512-c->at%512;
        uint8_t *bytes=p+c->at%512;
        if(c->at<c->prefix){
            if(n>c->prefix-c->at)n=c->prefix-c->at;
            c->old_crc=crc_more(c->old_crc,bytes,n);
            if(!((c->at+n)%4096)||c->at+n==c->prefix){
                if(~c->old_crc!=c->checksums[c->at/4096])return mutation_done(f,m,TIKU_MFS_CORRUPT);
                c->old_crc=UINT32_MAX;
            }
        }else {
            memcpy(bytes,m->payload+m->payload_at,n);
            hash_add(&m->object.hash,m->object.length+c->at-c->prefix,bytes,n);m->payload_at+=n;
        }
        m->slice_crc=crc_more(m->slice_crc,bytes,n);c->at+=n;
        if(!(c->at%4096)||c->at==c->length){
            put32(m->footer+64+4*((c->at-1)/4096),~m->slice_crc);m->slice_crc=UINT32_MAX;
        }
        if(!(c->at%512)||c->at==c->length){
            if(c->at%512)memset(p+c->at%512,0,512-c->at%512);
            m->phase=MUT_STREAM_DATA;return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba+1+(c->at-1)/512);
        }
        return 1;
    }
    case MUT_STREAM_DATA:m->phase=c->at<c->length?MUT_STREAM_SECTOR:MUT_STREAM_END;return 1;
    case MUT_STREAM_END:
        seal(m->footer);c->footer_crc=get32(m->footer+508);memcpy(p,m->footer,512);m->phase=MUT_STREAM_HEAD;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba+1+(c->length+511)/512);
    case MUT_STREAM_HEAD:
        page(f,p,"MFD1");put64(p+24,m->id);put64(p+32,c->chunk);put32(p+40,c->length);put32(p+44,c->footer_crc);seal(p);
        c->data=(tiku_mfs_ref_t){m->frame_lba,get32(p+508)};c->level=1;m->phase=MUT_STREAM_ASCEND;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba);
    case MUT_STREAM_ASCEND:
        if(c->level>c->height){
            m->object.root=c->data;m->object.height=(uint8_t)c->height;m->object.length+=c->take;
            if(m->payload_at<m->payload_size){m->phase=MUT_STREAM_FRAME;return 1;}
            f->draft=m->object;return mutation_done(f,m,0);
        }
        m->phase=MUT_STREAM_NODE;
        if(c->path[c->level-1].lba)return mutation_io(m,TIKU_MFS_MUTATION_READ,c->path[c->level-1].lba);
        page(f,p,"MFN1");put64(p+24,m->id);put64(p+32,c->bases[c->level-1]);put32(p+40,c->level);
        if(c->level==(unsigned)m->object.height+1&&m->object.root.lba)ref_put(p,0,m->object.root);
        return 1;
    case MUT_STREAM_NODE:
        if(c->path[c->level-1].lba){
            rc=node_decode(f,&m->object,c->path[c->level-1],c->level,c->bases[c->level-1],p);
            if(rc)return mutation_done(f,m,rc);
        }
        ref_put(p,(unsigned)((c->chunk>>(4*(c->level-1)))&15u),c->data);seal(p);
        m->phase=MUT_STREAM_NODE_ALLOC;return 1;
    case MUT_STREAM_NODE_ALLOC:{
        uint64_t lba;rc=mutation_allocate(f,m,&m->scanned,&lba);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->scanned=0;c->data=(tiku_mfs_ref_t){lba,get32(p+508)};m->phase=MUT_STREAM_NODE_WRITTEN;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,lba);
    }
    case MUT_STREAM_NODE_WRITTEN:++c->level;m->phase=MUT_STREAM_ASCEND;return 1;
    default:return mutation_done(f,m,TIKU_MFS_ARG);
    }
}
int tiku_mfs_mutation_step(tiku_mfs_t *f,tiku_mfs_mutation_t *m)
{
    if(!m)return TIKU_MFS_ARG;
    if(m->done)return 0;
    if(!f||!f->mounted||f->generation!=m->generation||f->sequence!=m->sequence||f->epoch!=m->epoch)
        return mutation_done(f,m,TIKU_MFS_STALE);
    if(f->failed)return mutation_done(f,m,TIKU_MFS_IO);
    if(m->waiting)return 1;
    if(m->have){m->have=0;if(m->io_result){f->failed=1;return mutation_done(f,m,TIKU_MFS_IO);}}
    int rc;uint8_t *p=m->sector;
    if(m->phase>=MUT_STREAM_FRAME&&m->phase<=MUT_STREAM_NODE_WRITTEN)return mutation_append_step(f,m);
    switch(m->phase){
    case MUT_STREAM_NAME:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->old_name=m->seek_node;
        if(m->object.policy==TIKU_MFS_ATOMIC?m->old_name.pending!=m->id:m->old_name.visible!=m->id)
            return mutation_done(f,m,TIKU_MFS_STALE);
        m->phase=MUT_PREPARE;return 1;
    case MUT_REPLACE_TARGET:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;
        if(rc==TIKU_MFS_NOENT){if(m->id)return mutation_done(f,m,TIKU_MFS_STALE);mutation_parent(f,m);return 1;}
        if(rc)return mutation_done(f,m,rc);
        if(m->seek_node.pending)return mutation_done(f,m,TIKU_MFS_BUSY);
        if(!m->id)return mutation_done(f,m,TIKU_MFS_BUSY);
        if(m->seek_node.visible!=m->id)return mutation_done(f,m,TIKU_MFS_STALE);
        mutation_seek(m,f->ids,CATALOG_IDS,m->id,NULL,0);m->phase=MUT_ID;return 1;
    case MUT_ID:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->phase=MUT_OBJECT;return mutation_io(m,TIKU_MFS_MUTATION_READ,m->seek_node.object.lba);
    case MUT_OBJECT:
        if(get32(p+508)!=m->seek_node.object.crc)return mutation_done(f,m,TIKU_MFS_CORRUPT);
        rc=object_decode(f,p,&m->object);if(rc)return mutation_done(f,m,rc);
        if(m->object.id!=m->id||m->id>=f->next_id)return mutation_done(f,m,TIKU_MFS_CORRUPT);
        if(m->stream==2){
            if(m->object.complete)return mutation_done(f,m,TIKU_MFS_ARG);
            f->draft=m->object;f->writing=1;return mutation_done(f,m,0);
        }
        if(!m->object.visible||!m->object.complete||strcmp(m->object.name,m->source))return mutation_done(f,m,TIKU_MFS_STALE);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->tree&&m->id==m->tree_id&&m->object.policy!=TIKU_MFS_DIRECTORY)return mutation_done(f,m,TIKU_MFS_ARG);
        if(m->replace&&m->object.policy==TIKU_MFS_DIRECTORY)return mutation_done(f,m,TIKU_MFS_BUSY);
#endif
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->source,0);m->phase=MUT_SOURCE;return 1;
    case MUT_SOURCE:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->old_name=m->seek_node;
        if(m->old_name.visible!=m->id)return mutation_done(f,m,TIKU_MFS_STALE);
        if(m->old_name.pending)return mutation_done(f,m,TIKU_MFS_BUSY);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->object.policy==TIKU_MFS_DIRECTORY&&!m->remove){size_t n=strlen(m->source);
            if(!strncmp(m->source,m->target,n)&&m->target[n]=='/')return mutation_done(f,m,TIKU_MFS_BUSY);}
        if(!m->tree&&m->object.policy==TIKU_MFS_DIRECTORY&&(m->remove||strcmp(m->source,m->target))){
            size_t n=strlen(m->source);if(n+1<64){memcpy(m->prefix,m->source,n);m->prefix[n++]='/';m->prefix[n]=0;
                mutation_seek(m,f->names,CATALOG_NAMES,0,m->prefix,1);m->phase=MUT_DIRECTORY_CHILD;return 1;}
        }
#endif
        if(m->remove||m->replace){m->phase=MUT_PREPARE;return 1;}
        if(!strcmp(m->source,m->target))return mutation_done(f,m,0);
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->target,0);m->phase=MUT_TARGET;return 1;
    case MUT_TARGET:case MUT_PARENT:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(!rc&&m->phase==MUT_PARENT&&m->seek_node.visible&&!m->seek_node.pending){
            mutation_seek(m,f->ids,CATALOG_IDS,m->seek_node.visible,NULL,0);m->phase=MUT_PARENT_ID;return 1;}
#endif
        if(rc!=TIKU_MFS_NOENT)return mutation_done(f,m,rc?rc:TIKU_MFS_BUSY);
        mutation_parent(f,m);return 1;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    case MUT_PARENT_ID:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->phase=MUT_PARENT_OBJECT;return mutation_io(m,TIKU_MFS_MUTATION_READ,m->seek_node.object.lba);
    case MUT_PARENT_OBJECT:{
        tiku_mfs_object_t parent;if(get32(p+508)!=m->seek_node.object.crc)return mutation_done(f,m,TIKU_MFS_CORRUPT);
        rc=object_decode(f,p,&parent);if(rc)return mutation_done(f,m,rc);
        if(parent.id!=m->seek_node.key||strcmp(parent.name,m->prefix))return mutation_done(f,m,TIKU_MFS_CORRUPT);
        if(parent.policy!=TIKU_MFS_DIRECTORY)return mutation_done(f,m,TIKU_MFS_BUSY);
        mutation_parent(f,m);return 1;
    }
    case MUT_DIRECTORY_CHILD:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;if(rc&&rc!=TIKU_MFS_NOENT)return mutation_done(f,m,rc);
        if(!rc&&!strncmp(m->prefix,m->seek_node.name,strlen(m->prefix)))return mutation_done(f,m,TIKU_MFS_BUSY);
        if(m->remove){m->phase=MUT_PREPARE;return 1;}
        mutation_seek(m,f->names,CATALOG_NAMES,0,m->target,0);m->phase=MUT_TARGET;return 1;
#endif
    case MUT_CHILD:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;
        if(rc&&rc!=TIKU_MFS_NOENT)return mutation_done(f,m,rc);
        if(!rc&&!strncmp(m->prefix,m->seek_node.name,strlen(m->prefix)))return mutation_done(f,m,TIKU_MFS_BUSY);
        m->phase=MUT_PREPARE;return 1;
    case MUT_PREPARE:
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->tree==1){
            if(m->id){
                if(m->tree_count==UINT32_MAX)return mutation_done(f,m,TIKU_MFS_LIMIT);
                m->tree_count++;
                if(m->copy){
                    if(m->object.length>TIKU_MFS_CHUNK||UINT64_MAX-m->copy_total<m->object.length)
                        return mutation_done(f,m,TIKU_MFS_LIMIT);
                    m->copy_total+=m->object.length;
                }
            }
            mutation_tree_next(f,m);return 1;
        }
        if(m->tree==2&&m->tree_started){m->phase=MUT_BUILD;return 1;}
#endif
        if(m->replace){
            if(f->next_id==UINT64_MAX)return mutation_done(f,m,TIKU_MFS_LIMIT);
            unsigned left=f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-(unsigned)(f->tail%TIKU_MFS_SEGMENT):0;
            if((uint64_t)f->free_segments*TIKU_MFS_SEGMENT+left<
               (m->payload_size+511)/512+3u+CATALOG_RESERVE)return mutation_done(f,m,TIKU_MFS_SPACE);
        }
        if(f->sequence==UINT64_MAX||names_version==UINT64_MAX)return mutation_done(f,m,TIKU_MFS_LIMIT);
        if(f->recovered||f->journal==TIKU_MFS_JOURNAL){
            rc=tiku_mfs_checkpoint_begin(f,&m->checkpoint);if(rc)return mutation_done(f,m,rc);
            m->phase=MUT_CHECKPOINT;
        }else m->phase=MUT_BUILD;
        return 1;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    case MUT_UPGRADE_CHECKPOINT:
#endif
    case MUT_CHECKPOINT:
        rc=tiku_mfs_checkpoint_step(f,&m->checkpoint);if(m->checkpoint.changed)m->changed=1;
        if(rc)return rc;
        if(m->checkpoint.result)return mutation_done(f,m,m->checkpoint.result);
        m->epoch=f->epoch;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->phase==MUT_UPGRADE_CHECKPOINT)m->phase=MUT_UPGRADE_READ;else
#endif
        m->phase=MUT_BUILD;
        return 1;
    case MUT_BUILD:
        if(m->stream==1){
            m->changed=1;m->id=f->next_id++;
            m->object=(tiku_mfs_object_t){.id=m->id,.policy=m->footer[0],.expected=get64(m->footer+8)};
            m->object.visible=m->object.policy==TIKU_MFS_STREAM;
            if(m->object.visible)m->object.expected=0;
            memcpy(m->object.expected_digest,m->footer+16,32);hash_init(&m->object.hash);
            m->phase=MUT_FILE_DESCRIPTOR;return 1;
        }
        if(m->stream){m->phase=MUT_FILE_DESCRIPTOR;return 1;}
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->tree)m->tree_started=1;
        if(m->copy&&!m->copy_ready){
            unsigned left=f->tail%TIKU_MFS_SEGMENT?TIKU_MFS_SEGMENT-(unsigned)(f->tail%TIKU_MFS_SEGMENT):0;
            if((uint64_t)f->free_segments*TIKU_MFS_SEGMENT+left<
               (m->object.length+511)/512+3u+CATALOG_RESERVE)return mutation_done(f,m,TIKU_MFS_SPACE);
            memcpy(m->copy_digest,m->object.digest,32);
            m->directory=m->object.policy==TIKU_MFS_DIRECTORY;
            m->payload=m->copy_buffer;m->payload_size=(uint32_t)m->object.length;m->payload_at=0;
            if(m->payload_size){
                rc=query_init(f,&m->copy_query);if(rc)return mutation_done(f,m,rc);
                m->copy_query.mode=1;m->copy_query.object=m->object;m->copy_query.output=m->copy_buffer;
                m->copy_query.total=m->payload_size;query_index(&m->copy_query);m->phase=MUT_COPY_READ;return 1;
            }
            m->copy_ready=1;
        }
#endif
        if(m->replace
#if TIKU_MEDIA_DIRECTORIES_ENABLE
           ||m->copy
#endif
           ){
#if TIKU_MEDIA_DIRECTORIES_ENABLE
            if(m->copy&&f->next_id==UINT64_MAX)return mutation_done(f,m,TIKU_MFS_LIMIT);
#endif
            m->changed=1;m->id=f->next_id++;m->object=(tiku_mfs_object_t){.id=m->id,
                .length=m->payload_size,.expected=m->payload_size,.policy=TIKU_MFS_ATOMIC,.visible=1,.complete=1};
#if TIKU_MEDIA_DIRECTORIES_ENABLE
            if(m->directory)m->object.policy=TIKU_MFS_DIRECTORY;
            if(m->copy&&!strcmp(m->source,m->tree_source))m->copy_id=m->id;
#endif
            memcpy(m->object.name,m->target,64);hash_init(&m->object.hash);
            if(m->payload_size)m->phase=MUT_FILE_ALLOC;
            else {hash_end(m->object.hash,0,m->object.digest);memcpy(m->object.expected_digest,m->object.digest,32);m->phase=MUT_FILE_DESCRIPTOR;}
            return 1;
        }
        /* Namespace-only operations retain their existing object metadata. */
        m->phase=MUT_FILE_DESCRIPTOR;return 1;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    case MUT_COPY_READ:
        rc=tiku_mfs_query_step(f,&m->copy_query);if(rc)return rc;
        if(m->copy_query.result!=(int)m->payload_size)
            return mutation_done(f,m,m->copy_query.result<0?m->copy_query.result:TIKU_MFS_CORRUPT);
        m->copy_ready=1;m->phase=MUT_BUILD;return 1;
#endif
    case MUT_FILE_DESCRIPTOR:{
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->copy&&memcmp(m->object.digest,m->copy_digest,32))return mutation_done(f,m,TIKU_MFS_CORRUPT);
#endif
        catalog_node_t key={.kind=CATALOG_IDS,.key=m->id};
        if(m->remove){mutation_edit(m,m->ids,&key,1);m->phase=MUT_IDS;return 1;}
        uint64_t lba;rc=mutation_allocate(f,m,&m->scanned,&lba);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        memset(m->object.name,0,64);memcpy(m->object.name,m->target,strlen(m->target));object_encode(f,p,&m->object);
        key.object=(tiku_mfs_ref_t){lba,get32(p+508)};mutation_edit(m,m->ids,&key,0);
        m->phase=MUT_DESCRIPTOR;return mutation_io(m,TIKU_MFS_MUTATION_WRITE,lba);
    }
    case MUT_FILE_ALLOC:
        rc=mutation_reserve(f,m,&m->scanned,(m->payload_size+511)/512+2,&m->frame_lba);
        if(rc==1)return 1;
        if(rc)return mutation_done(f,m,rc);
        m->scanned=0;m->slice_crc=UINT32_MAX;
        page(f,m->footer,"MFE1");put64(m->footer+24,m->id);put32(m->footer+40,m->payload_size);
        m->phase=MUT_FILE_HASH;return 1;
    case MUT_FILE_HASH:{
        unsigned bytes=m->payload_size-m->payload_at;if(bytes>64)bytes=64;
        if(!(m->payload_at%512))memset(p,0,512);
        memcpy(p+m->payload_at%512,m->payload+m->payload_at,bytes);
        hash_add(&m->object.hash,m->payload_at,m->payload+m->payload_at,bytes);
        m->slice_crc=crc_more(m->slice_crc,m->payload+m->payload_at,bytes);m->payload_at+=bytes;
        if(!(m->payload_at%4096)||m->payload_at==m->payload_size){
            put32(m->footer+64+4*((m->payload_at-1)/4096),~m->slice_crc);m->slice_crc=UINT32_MAX;
        }
        if(!(m->payload_at%512)||m->payload_at==m->payload_size){
            m->phase=MUT_FILE_DATA;return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba+1+(m->payload_at-1)/512);
        }
        return 1;
    }
    case MUT_FILE_DATA:
        if(m->payload_at<m->payload_size)m->phase=MUT_FILE_HASH;
        else {hash_end(m->object.hash,m->payload_size,m->object.digest);memcpy(m->object.expected_digest,m->object.digest,32);m->phase=MUT_FILE_FOOTER;}
        return 1;
    case MUT_FILE_FOOTER:
        seal(m->footer);m->root_crc=get32(m->footer+508);memcpy(p,m->footer,512);m->phase=MUT_FILE_HEADER;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba+1+(m->payload_size+511)/512);
    case MUT_FILE_HEADER:
        page(f,p,"MFD1");put64(p+24,m->id);put32(p+40,m->payload_size);put32(p+44,m->root_crc);seal(p);
        m->object.root=(tiku_mfs_ref_t){m->frame_lba,get32(p+508)};m->phase=MUT_FILE_INDEX;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,m->frame_lba);
    case MUT_FILE_INDEX:{
        uint64_t lba;rc=mutation_allocate(f,m,&m->scanned,&lba);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->scanned=0;page(f,p,"MFN1");put64(p+24,m->id);put32(p+40,1);ref_put(p,0,m->object.root);seal(p);
        m->object.root=(tiku_mfs_ref_t){lba,get32(p+508)};m->object.height=1;m->phase=MUT_FILE_DESCRIPTOR;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,lba);
    }
    case MUT_DESCRIPTOR:m->phase=MUT_IDS;return 1;
    case MUT_IDS:
        rc=mutation_edit_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->ids=m->edit.root;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->copy){mutation_new_name(m);return 1;}
#endif
        if(m->stream){
            if(m->object.visible&&m->old_name.visible&&m->old_name.visible!=m->id){
                catalog_node_t key={.kind=CATALOG_IDS,.key=m->old_name.visible};
                mutation_edit(m,m->ids,&key,1);m->phase=MUT_REPLACED;
            }else if(m->stream==4)m->phase=MUT_ROOT;
            else mutation_new_name(m);
            return 1;
        }
        if(m->replace){
            if(m->old_name.visible){catalog_node_t key={.kind=CATALOG_IDS,.key=m->old_name.visible};mutation_edit(m,m->ids,&key,1);m->phase=MUT_REPLACED;}
            else mutation_new_name(m);
        }else {mutation_edit(m,m->names,&m->old_name,1);m->phase=MUT_OLD;}
        return 1;
    case MUT_REPLACED:
        rc=mutation_edit_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->ids=m->edit.root;mutation_new_name(m);return 1;
    case MUT_OLD:
        rc=mutation_edit_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->names=m->edit.root;
        if(m->remove){
#if TIKU_MEDIA_DIRECTORIES_ENABLE
            if(m->tree)mutation_tree_next(f,m);else
#endif
            m->phase=MUT_ROOT;
        }else mutation_new_name(m);
        return 1;
    case MUT_NEW:
        rc=mutation_edit_step(f,m);if(rc==1)return 1;if(rc)return mutation_done(f,m,rc);
        m->names=m->edit.root;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(m->copy)m->copy_prepared+=m->payload_size;
        if(m->tree)mutation_tree_next(f,m);else
#endif
        m->phase=MUT_ROOT;
        return 1;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    case MUT_UPGRADE_INIT:{
        if(f->features==1)return mutation_done(f,m,0);
        unsigned features=f->features;f->features&=1u;
        rc=tiku_mfs_checkpoint_begin(f,&m->checkpoint);f->features=(uint8_t)features;
        if(rc)return mutation_done(f,m,rc);
        f->features=3;m->phase=MUT_UPGRADE_CHECKPOINT;return 1;
    }
    case MUT_UPGRADE_READ:
        m->phase=MUT_UPGRADE_RETIRE;return mutation_io(m,TIKU_MFS_MUTATION_READ,(uint64_t)(1u-f->bank)*f->bank_sectors);
    case MUT_UPGRADE_RETIRE:
        if(valid(f,p,"MFC2")&&!get32(p+88)){
            put32(p+88,1);seal(p);m->phase=MUT_UPGRADE_SYNC;
            return mutation_io(m,TIKU_MFS_MUTATION_WRITE,(uint64_t)(1u-f->bank)*f->bank_sectors);
        }
        m->phase=MUT_UPGRADE_DONE;return 1;
    case MUT_UPGRADE_SYNC:m->phase=MUT_UPGRADE_DONE;return mutation_io(m,TIKU_MFS_MUTATION_SYNC,0);
    case MUT_UPGRADE_DONE:f->features=1;return mutation_done(f,m,0);
    case MUT_TREE_IMPLICIT:
        rc=mutation_seek_step(f,m);if(rc==1)return 1;
        if(rc!=TIKU_MFS_NOENT)return mutation_done(f,m,rc?rc:TIKU_MFS_STALE);
        m->phase=MUT_TREE_START;return 1;
    case MUT_TREE_START:
        if(m->remove||!strcmp(m->source,m->target))mutation_tree_next(f,m);
        else {mutation_seek(m,f->names,CATALOG_NAMES,0,m->target,0);m->phase=MUT_TARGET;}
        return 1;
    case MUT_TREE_NEXT:{
        rc=mutation_seek_step(f,m);if(rc==1)return 1;
        if(rc&&rc!=TIKU_MFS_NOENT)return mutation_done(f,m,rc);
        size_t n=strlen(m->tree_source);
        if(rc==TIKU_MFS_NOENT||strncmp(m->seek_node.name,m->tree_source,n)||m->seek_node.name[n]!='/'){
            m->phase=MUT_TREE_END;return 1;
        }
        if(m->seek_node.pending||!m->seek_node.visible)return mutation_done(f,m,TIKU_MFS_BUSY);
        size_t tail=strlen(m->seek_node.name+n),target=strlen(m->tree_target);
        if(!m->remove&&target+tail>=64)return mutation_done(f,m,TIKU_MFS_LIMIT);
        m->copy_ready=0;m->directory=0;
        m->id=m->seek_node.visible;memcpy(m->source,m->seek_node.name,64);
        memset(m->target,0,64);if(!m->remove){memcpy(m->target,m->tree_target,target);memcpy(m->target+target,m->source+n,tail+1);}
        m->scanned=m->prefix_at=0;mutation_seek(m,f->ids,CATALOG_IDS,m->id,NULL,0);m->phase=MUT_ID;return 1;
    }
    case MUT_TREE_END:
        if(m->tree==1){
            if(!m->tree_count)return mutation_done(f,m,TIKU_MFS_NOENT);
            if(m->copy&&UINT64_MAX-f->next_id<m->tree_count)return mutation_done(f,m,TIKU_MFS_LIMIT);
            if(!m->remove&&!strcmp(m->tree_source,m->tree_target))return mutation_done(f,m,0);
            m->tree=2;m->id=m->tree_id;m->scanned=m->prefix_at=0;
            memcpy(m->source,m->tree_source,64);memcpy(m->target,m->tree_target,64);
            if(m->tree_id){mutation_seek(m,f->ids,CATALOG_IDS,m->id,NULL,0);m->phase=MUT_ID;}
            else mutation_tree_next(f,m);
        }else {m->id=m->copy?m->copy_id:m->tree_id;m->phase=MUT_ROOT;}
        return 1;
#endif
    case MUT_ROOT:
        page(f,p,"MFR2");roots_put(p,m->ids,m->names);seal(p);m->root_crc=get32(p+508);
        m->phase=MUT_ROOT_SYNC;return mutation_io(m,TIKU_MFS_MUTATION_WRITE,journal_lba(f,f->bank)+2*f->journal);
    case MUT_ROOT_SYNC:m->phase=MUT_COMMIT;return mutation_io(m,TIKU_MFS_MUTATION_SYNC,0);
    case MUT_COMMIT:
        page(f,p,"MFT2");put64(p+24,f->epoch);put64(p+32,f->sequence+1);put64(p+40,f->sequence);
        put32(p+48,m->root_crc);put64(p+64,f->tail);put64(p+72,f->next_id);put32(p+80,f->cursor);
        put64(p+88,f->allocations);seal(p);m->phase=MUT_COMMIT_SYNC;
        return mutation_io(m,TIKU_MFS_MUTATION_WRITE,journal_lba(f,f->bank)+2*f->journal+1);
    case MUT_COMMIT_SYNC:m->phase=MUT_PUBLISH;return mutation_io(m,TIKU_MFS_MUTATION_SYNC,0);
    case MUT_PUBLISH:
        f->ids=m->ids;f->names=m->names;f->sequence++;f->journal++;
        if(!m->stream||m->stream==5||(m->stream==1&&m->object.visible))f->names_epoch=++names_version;
        if(m->stream){f->draft=m->object;f->writing=m->stream!=5;}
        return mutation_done(f,m,0);
    default:return mutation_done(f,m,TIKU_MFS_ARG);
    }
}
#endif
