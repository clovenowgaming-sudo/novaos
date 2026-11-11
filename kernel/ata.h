#pragma once
#include "core.h"
/* ATA PIO (IDE legacy ports). Jalan di QEMU (-hda / IDE) dan PC dengan SATA mode IDE/Compatibility. AHCI/NVMe belum ada. */
typedef struct { int ok, lba48, base, ctrl, slave; u64 sectors; char model[41]; } AtaDev;
static AtaDev ata_dev[4]; static int ata_n;
static inline void insw_(u16 p, void *b, u32 n){ __asm__ volatile("rep insw":"+D"(b),"+c"(n):"d"(p):"memory"); }
static inline void outsw_(u16 p, const void *b, u32 n){ __asm__ volatile("rep outsw":"+S"(b),"+c"(n):"d"(p):"memory"); }
static inline u16 inw_(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static void ata_delay(AtaDev *d){ for(int i=0;i<4;i++) inb(d->ctrl); }
static int ata_poll(AtaDev *d, int need_drq){
    ata_delay(d);
    for(long i=0;i<4000000;i++){
        u8 s=inb(d->base+7);
        if(s&1) return -1;            /* ERR */
        if(s&0x20) return -1;         /* DF */
        if(!(s&0x80)){ if(!need_drq) return 0; if(s&8) return 0; }
    }
    return -2;                        /* timeout */
}
static void ata_select(AtaDev *d, u8 extra){ outb(d->base+6, 0xA0|(d->slave<<4)|extra); ata_delay(d); }
static void ata_identify(AtaDev *d){
    d->ok=0;
    ata_select(d,0);
    if(inb(d->base+7)==0xFF) return;
    outb(d->ctrl,2);                  /* nIEN: tanpa interrupt */
    outb(d->base+2,0); outb(d->base+3,0); outb(d->base+4,0); outb(d->base+5,0);
    outb(d->base+7,0xEC); ata_delay(d);
    if(inb(d->base+7)==0) return;
    for(long i=0;i<4000000 && (inb(d->base+7)&0x80);i++);
    if(inb(d->base+4)||inb(d->base+5)) return;     /* ATAPI / SATA signature: lewati */
    if(ata_poll(d,1)) return;
    u16 id[256]; insw_(d->base,id,256);
    for(int i=0;i<20;i++){ d->model[i*2]=id[27+i]>>8; d->model[i*2+1]=id[27+i]&255; }
    d->model[40]=0; for(int i=39;i>=0&&d->model[i]==' ';i--) d->model[i]=0;
    d->lba48=(id[83]>>10)&1;
    d->sectors = d->lba48 ? ((u64)id[103]<<48|(u64)id[102]<<32|(u64)id[101]<<16|id[100]) : ((u32)id[61]<<16|id[60]);
    d->ok = d->sectors>0;
}
static void ata_init(void){
    static const int bases[2]={0x1F0,0x170}, ctrls[2]={0x3F6,0x376};
    ata_n=0;
    for(int b=0;b<2;b++) for(int s=0;s<2;s++){
        AtaDev *d=&ata_dev[b*2+s]; memset(d,0,sizeof *d);
        d->base=bases[b]; d->ctrl=ctrls[b]; d->slave=s;
        ata_identify(d); if(d->ok) ata_n++;
    }
}
/* n: 1..256 sektor per panggilan */
static int ata_xfer(AtaDev *d, u64 lba, int n, void *buf, int wr){
    int ext = d->lba48 || lba+n>0x0FFFFFFF;
    if(ext && !d->lba48) return -3;
    u8 cnt = (u8)(n==256?0:n);
    if(ext){
        ata_select(d,0x40);
        outb(d->base+2,0); outb(d->base+3,lba>>24); outb(d->base+4,lba>>32); outb(d->base+5,lba>>40);
        outb(d->base+2,cnt); outb(d->base+3,lba); outb(d->base+4,lba>>8); outb(d->base+5,lba>>16);
        outb(d->base+7, wr?0x34:0x24);
    } else {
        ata_select(d,0x40|((lba>>24)&0x0F));
        outb(d->base+2,cnt); outb(d->base+3,lba); outb(d->base+4,lba>>8); outb(d->base+5,lba>>16);
        outb(d->base+7, wr?0x30:0x20);
    }
    u8 *p=buf;
    for(int i=0;i<n;i++){
        int r=ata_poll(d,1); if(r) return r;
        if(wr) outsw_(d->base,p,256); else insw_(d->base,p,256);
        p+=512;
    }
    if(wr){ outb(d->base+7, ext?0xEA:0xE7); ata_poll(d,0); }
    return 0;
}
static int ata_read(int dev,u64 lba,int n,void *buf){ return ata_xfer(&ata_dev[dev],lba,n,buf,0); }
static int ata_write(int dev,u64 lba,int n,const void *buf){ return ata_xfer(&ata_dev[dev],lba,n,(void*)buf,1); }
