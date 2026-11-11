#pragma once
#include "core.h"
#include "ata.h"
#include "fs.h"
/* Layout disk terpasang:
   LBA 0        MBR (GRUB boot.img) + core.img GRUB (BIOS) di LBA 1..2047
   LBA 2048..   partisi ESP FAT16 8MB (EFI/BOOT/BOOTX64.EFI)
   LBA NFS_LBA  partisi data Nova: 2 slot (A/B) @ NFS_SLOT sektor: header 1 sektor + payload */
#define IMG_SECT 18432
#define NFS_LBA IMG_SECT
#define NFS_SLOT 16384
#define NFS_MIN_SECT (NFS_LBA+2*NFS_SLOT)
typedef struct { char name[48]; u8 *start; u32 len; } Mod;
static Mod mods[24]; static int nmods;
static u8 diskimg[IMG_SECT*512]; static u32 diskimg_len;
static int st_dev=-1, st_slot=1; static u32 st_seq;
static char st_err[64];
typedef struct { char magic[8]; u32 seq, plen, crc, nn; char user[40], pass[40]; u8 pad[512-8-16-80]; } __attribute__((packed)) NfsHdr;
typedef struct { u32 idx,parent,dir,size; char name[32]; } __attribute__((packed)) NfsRec;
extern char username[40], password[40];

static u32 fnv(u32 h,const u8 *p,u32 n){ while(n--){ h^=*p++; h*=16777619u; } return h; }
typedef void (*sink_t)(const void*,u32);
static void nfs_emit(sink_t sk){
    for(int i=1;i<192;i++) if(fs[i].used){
        NfsRec r; memset(&r,0,sizeof r); r.idx=i; r.parent=fs[i].parent; r.dir=fs[i].dir; r.size=fs[i].dir?0:fs[i].size; strncpyz(r.name,fs[i].name,32);
        sk(&r,sizeof r); if(r.size) sk(fs[i].data,r.size);
    }
}
static u32 nw_len, nw_crc; static u64 nw_lba; static u32 nw_fill; static int nw_err; static u8 nw_buf[512*64];
static void sink_count(const void *d,u32 n){ nw_len+=n; nw_crc=fnv(nw_crc,d,n); }
static void nw_flush(int force){
    if(!nw_fill) return;
    u32 secs=(nw_fill+511)/512; if(!force && nw_fill<sizeof nw_buf) return;
    memset(nw_buf+nw_fill,0,secs*512-nw_fill);
    for(u32 o=0;o<secs;){ int c=secs-o>128?128:secs-o; if(ata_write(st_dev,nw_lba,c,nw_buf+o*512)) nw_err=1; nw_lba+=c; o+=c; }
    nw_fill=0;
}
static void sink_write(const void *d,u32 n){
    const u8 *p=d;
    while(n){ u32 c=sizeof nw_buf-nw_fill; if(c>n) c=n; memcpy(nw_buf+nw_fill,p,c); nw_fill+=c; p+=c; n-=c; if(nw_fill==sizeof nw_buf) nw_flush(1); }
}
/* tulis snapshot FS ke slot yang sedang tidak aktif; header ditulis terakhir */
static int nfs_save(void){
    if(st_dev<0) return 0;
    nw_len=0; nw_crc=2166136261u; nfs_emit(sink_count);
    if(nw_len>7500000) return -1;
    int slot=st_slot^1; u64 base=(u64)NFS_LBA+(u64)slot*NFS_SLOT;
    nw_lba=base+1; nw_fill=0; nw_err=0; nfs_emit(sink_write); nw_flush(1);
    if(nw_err) return -2;
    NfsHdr h; memset(&h,0,sizeof h); memcpy(h.magic,"NOVAFS01",8);
    h.seq=++st_seq; h.plen=nw_len; h.crc=nw_crc; strncpyz(h.user,username,40); strncpyz(h.pass,password,40);
    if(ata_write(st_dev,base,1,&h)) return -3;
    st_slot=slot; fs_dirty=0; return 0;
}
/* baca slot ke bb (scratch); return 1 kalau valid. */
static int nfs_read_slot(int dev,int slot,NfsHdr *h){
    u64 base=(u64)NFS_LBA+(u64)slot*NFS_SLOT;
    if(ata_read(dev,base,1,h)) return 0;
    if(memcmp_(h->magic,"NOVAFS01",8)||h->plen>7500000) return 0;
    u8 *buf=(u8*)bb; u32 secs=(h->plen+511)/512;
    for(u32 o=0;o<secs;){ int c=secs-o>128?128:secs-o; if(ata_read(dev,base+1+o,c,buf+o*512)) return 0; o+=c; }
    return fnv(2166136261u,buf,h->plen)==h->crc;
}
static void nfs_apply(NfsHdr *h){
    u8 *p=(u8*)bb, *e=p+h->plen;
    memset(fs,0,sizeof fs); fs[0].used=1; fs[0].dir=1; fs[0].parent=-1;
    while(p+sizeof(NfsRec)<=e){
        NfsRec r; memcpy(&r,p,sizeof r); p+=sizeof r;
        if(r.idx>=192||p+r.size>e) break;
        Node *n=&fs[r.idx]; n->used=1; n->parent=r.parent; n->dir=r.dir; strncpyz(n->name,r.name,32);
        if(!r.dir) fs_write(r.idx,(const char*)p,r.size);
        p+=r.size;
    }
    strncpyz(username,h->user,40); strncpyz(password,h->pass,40); fs_dirty=0;
}
static int nfs_load_any(void){
    int bd=-1,bs=-1; u32 bseq=0; NfsHdr h;
    for(int d=0;d<4;d++){ if(!ata_dev[d].ok||ata_dev[d].sectors<NFS_MIN_SECT) continue;
        for(int s=0;s<2;s++) if(nfs_read_slot(d,s,&h) && h.seq>=bseq){ bseq=h.seq; bd=d; bs=s; } }
    if(bd<0) return 0;
    nfs_read_slot(bd,bs,&h); nfs_apply(&h);
    st_dev=bd; st_slot=bs; st_seq=bseq; return 1;
}
/* ---- installer: tulis image (module "novadisk") ke disk, bertahap per frame ---- */
static u32 inst_pos;
static int inst_begin(int dev){
    if(!diskimg_len||dev<0||!ata_dev[dev].ok) return -1;
    if(ata_dev[dev].sectors<NFS_MIN_SECT) return -2;
    inst_pos=0; st_dev=dev; return 0;
}
/* return persen 0..100, <0 = error */
static int inst_step(void){
    if(inst_pos<IMG_SECT){
        int c=IMG_SECT-inst_pos>128?128:IMG_SECT-inst_pos;
        if(ata_write(st_dev,inst_pos,c,diskimg+(u64)inst_pos*512)){ st_dev=-1; return -3; }
        inst_pos+=c; return inst_pos*95/IMG_SECT;
    }
    return 100;
}
static int inst_finish(void){   /* kosongkan header slot, lalu simpan FS awal + akun */
    u8 z[512]; memset(z,0,512);
    if(ata_write(st_dev,NFS_LBA,1,z)||ata_write(st_dev,NFS_LBA+NFS_SLOT,1,z)) return -1;
    st_slot=1; st_seq=0; return nfs_save();
}
