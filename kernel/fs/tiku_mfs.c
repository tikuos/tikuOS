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
    if(!name_ok(o->name)||(o->policy!=TIKU_MFS_STREAM&&o->policy!=TIKU_MFS_ATOMIC)||
       o->complete>1||o->visible>1||o->height>DEPTH||o->length>UINT64_MAX/8||
       (o->length&&(!o->height||!ref_ok(f,o->root)))||
       (!o->length&&(o->height||o->root.lba))||
       (o->policy==TIKU_MFS_STREAM&&!o->visible)||
       (o->policy==TIKU_MFS_ATOMIC&&(o->length>o->expected||o->visible!=o->complete)))
        return TIKU_MFS_CORRUPT;
    if(o->length&&((o->length-1)/TIKU_MFS_CHUNK)>>(4*o->height))return TIKU_MFS_CORRUPT;
    return 0;
}

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
           get32(p+84)!=2||get32(p+88)||get32(p+92)!=TIKU_MFS_SEGMENT||!get64(p+40))continue;
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
/** @brief Read one catalog node and validate its bound subtree summaries. */
static int catalog_read(tiku_mfs_t *f,tiku_mfs_link_t link,unsigned kind,catalog_node_t *n)
{
    uint8_t p[512];if(!link_ok(f,link)||!link.ref.lba)return TIKU_MFS_CORRUPT;
    int rc=rd(f,link.ref.lba,1,p);if(rc)return rc;
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
    if(!valid(f,h,"MFC2")||get64(h+32)!=f->block.sectors||get32(h+88)!=0||
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
    roots_put(h,f->ids,f->names);seal(h);
    if((rc=wr(f,base,1,h))||(rc=barrier(f)))return rc;
    f->bank=(uint8_t)bank;f->epoch++;f->journal=0;f->recovered=0;return 0;
}
int tiku_mfs_format(tiku_mfs_t *f,const tiku_block_t *b,const uint8_t uuid[16],
                    void *used,void *mark,size_t capacity)
{
    int rc=setup(f,b,uuid,used,mark,capacity);if(rc)return rc;
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
    if((rc=bank_load(f,bank,1,&epochs[bank])))return rc;
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
    return publish(f,ids,names);
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
/** @brief Read and bind an extent-index page to its object, level and range. */
static int node_read(tiku_mfs_t *f,const tiku_mfs_object_t *o,tiku_mfs_ref_t r,
                     unsigned level,uint64_t base,uint8_t p[512])
{
    if(!ref_ok(f,r)||!level||level>DEPTH)return TIKU_MFS_CORRUPT;
    int rc=rd(f,r.lba,1,p);if(rc)return rc;
    if(!valid(f,p,"MFN1")||get32(p+508)!=r.crc||get64(p+24)!=o->id||
       get64(p+32)!=base||get32(p+40)!=level)return TIKU_MFS_CORRUPT;
    for(unsigned i=0;i<FANOUT;i++){
        tiku_mfs_ref_t child=ref_get(p,i);
        if(child.lba&&!ref_ok(f,child))return TIKU_MFS_CORRUPT;
    }
    return 0;
}
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
/** @brief Load a chunk header and its bound payload-checksum footer. */
static int frame_read(tiku_mfs_t *f,const tiku_mfs_object_t *o,uint64_t chunk,
                      tiku_mfs_ref_t r,uint8_t header[512],uint8_t footer[512])
{
    if(!ref_ok(f,r))return TIKU_MFS_CORRUPT;
    int rc=rd(f,r.lba,1,header);if(rc)return rc;
    uint32_t length=get32(header+40),sectors=(length+511)/512;
    if(!valid(f,header,"MFD1")||get32(header+508)!=r.crc||get64(header+24)!=o->id||
       get64(header+32)!=chunk||!length||length>TIKU_MFS_CHUNK||
       r.lba%TIKU_MFS_SEGMENT+sectors+2>TIKU_MFS_SEGMENT)return TIKU_MFS_CORRUPT;
    if((rc=rd(f,r.lba+1+sectors,1,footer)))return rc;
    if(!valid(f,footer,"MFE1")||get32(footer+508)!=get32(header+44)||
       get64(footer+24)!=o->id||get64(footer+32)!=chunk||get32(footer+40)!=length)return TIKU_MFS_CORRUPT;
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
/** @brief Refuse mutation while unmounted, failed or collecting. */
static int mutable(tiku_mfs_t *f)
{ return !f||!f->mounted?TIKU_MFS_ARG:f->failed?TIKU_MFS_IO:f->collecting?TIKU_MFS_BUSY:0; }
/** @brief Check parent files and child prefixes through the name index. */
static int namespace_free(tiku_mfs_t *f,const char *name)
{
    char prefix[TIKU_MFS_NAME];size_t length=strlen(name);catalog_node_t n;int rc;
    memcpy(prefix,name,length+1);
    for(size_t i=0;i<length;i++)if(prefix[i]=='/'){
        prefix[i]=0;rc=catalog_name(f,prefix,&n);prefix[i]='/';
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
int tiku_mfs_delete(tiku_mfs_t *f,uint64_t id)
{
    int rc=mutable(f);if(rc)return rc;if(f->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;if((rc=catalog_object(f,id,&o)))return rc;
    return commit(f,&o,1);
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
int tiku_mfs_open_read(tiku_mfs_t *f,const char *name,uint64_t *token)
{
    if(!f||!token)return TIKU_MFS_ARG;
    if(f->collecting)return TIKU_MFS_BUSY;
    if(f->next_token==UINT64_MAX)return TIKU_MFS_LIMIT;
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(!f->readers[i].token){
        int rc=tiku_mfs_stat(f,name,&f->readers[i].object);if(rc)return rc;
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
