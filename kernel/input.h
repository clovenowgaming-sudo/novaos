#pragma once
#include "core.h"

#define K_UP 0x101
#define K_DOWN 0x102
#define K_LEFT 0x103
#define K_RIGHT 0x104
#define K_DEL 0x105
#define K_HOME 0x106
#define K_END 0x107
#define K_PGUP 0x108
#define K_PGDN 0x109
#define K_CTRL(c) (0x200 + (c))

static volatile int kq[64], kqh, kqt;
static int shift, ctrl, caps, e0;
/* mx,my,mb = posisi/tombol yang di-sample sekali per frame (dipakai UI).
   pcx,pcy,pmbtn = posisi mouse "hidup", diisi oleh IRQ12 (tidak pernah hilang walau render lambat). */
static int mx=512, my=384, mb, mclick;
static volatile int pcx=512, pcy=384, pmbtn, pclick, pclick_x, pclick_y;
static int mpi; static u8 mpk[3]; static u64 mlast_tsc;
#define MOUSE_GAP 20000000ULL   /* jeda antar byte dalam satu paket; lebih dari ini = paket nyasar, resync */

static void kq_push(int k){ int n=(kqt+1)&63; if(n!=kqh){ kq[kqt]=k; kqt=n; } }
static int kq_pop(void){ if(kqh==kqt) return 0; int k=kq[kqh]; kqh=(kqh+1)&63; return k; }

static const char km_n[] = "\0" "\033" "1234567890-=" "\b\t" "qwertyuiop[]" "\n" "\0" "asdfghjkl;'`" "\0" "\\" "zxcvbnm,./" "\0*\0 ";
static const char km_s[] = "\0" "\033" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n" "\0" "ASDFGHJKL:\"~" "\0" "|" "ZXCVBNM<>?" "\0*\0 ";

static void kbd_byte(u8 d){
    if(d==0xE0){ e0=1; return; }
    int rel=d&0x80; int sc=d&0x7F;
    if(sc==0x2A||sc==0x36){ shift=!rel; e0=0; return; }
    if(sc==0x1D){ ctrl=!rel; e0=0; return; }
    if(sc==0x3A && !rel){ caps=!caps; e0=0; return; }
    if(rel){ e0=0; return; }
    int k=0;
    if(e0){
        switch(sc){
        case 0x48: k=K_UP; break; case 0x50: k=K_DOWN; break; case 0x4B: k=K_LEFT; break;
        case 0x4D: k=K_RIGHT; break; case 0x53: k=K_DEL; break; case 0x47: k=K_HOME; break;
        case 0x4F: k=K_END; break; case 0x49: k=K_PGUP; break; case 0x51: k=K_PGDN; break;
        case 0x1C: k='\n'; break;
        }
        e0=0;
    } else if(sc<58){
        char c = shift ? km_s[sc] : km_n[sc];
        if(caps && c>='a'&&c<='z' && !shift) c-=32;
        else if(caps && c>='A'&&c<='Z' && shift) c+=32;
        if(ctrl && lower(c)>='a' && lower(c)<='z') k=K_CTRL(lower(c));
        else k=(u8)c;
    }
    if(k) kq_push(k);
}

static void mouse_byte(u8 d){
    u64 t=rdtsc();
    if(mpi && t-mlast_tsc>MOUSE_GAP) mpi=0;     /* paket setengah jalan kelamaan -> buang, sinkron ulang */
    mlast_tsc=t;
    if(mpi==0 && !(d&8)) return;                 /* byte pertama paket wajib punya bit 3 */
    mpk[mpi++]=d;
    if(mpi<3) return;
    mpi=0;
    int nb=mpk[0]&1;
    if(!(mpk[0]&0xC0)){                          /* bit 6/7 = overflow: delta sampah, jangan dipakai */
        int dx=mpk[1]-((mpk[0]<<4)&0x100), dy=mpk[2]-((mpk[0]<<3)&0x100);
        int x=pcx+dx, y=pcy-dy;
        if(x<0)x=0; if(y<0)y=0; if(x>=W)x=W-1; if(y>=H)y=H-1;
        pcx=x; pcy=y;
    }
    if(nb && !pmbtn){ pclick=1; pclick_x=pcx; pclick_y=pcy; }
    pmbtn=nb;
}

/* ---- IRQ handlers (dipanggil dari stub di boot.asm) ---- */
void irq1_handler(void){ u8 d=inb(0x60); kbd_byte(d); outb(0x20,0x20); }
void irq12_handler(void){ u8 d=inb(0x60); mouse_byte(d); outb(0xA0,0x20); outb(0x20,0x20); }

/* ---- IDT + PIC ---- */
struct __attribute__((packed)) IdtE { u16 lo, sel; u8 ist, attr; u16 mid; u32 hi, zero; };
struct __attribute__((packed)) IdtPtr { u16 limit; u64 base; };
static struct IdtE idt[48]; static struct IdtPtr idtp;
extern void isr_kbd(void), isr_mouse(void), isr_ignore(void);
static void idt_set(int n, void (*h)(void)){
    u64 a=(u64)h;
    idt[n].lo=a&0xFFFF; idt[n].sel=0x08; idt[n].ist=0; idt[n].attr=0x8E;
    idt[n].mid=(a>>16)&0xFFFF; idt[n].hi=(u32)(a>>32); idt[n].zero=0;
}
static void io_wait(void){ outb(0x80,0); }
extern u64 exc_table[32];
static void irq_init(void){
    for(int i=0;i<32;i++) idt_set(i,(void(*)(void))exc_table[i]);
    for(int i=0x20;i<48;i++) idt_set(i,isr_ignore);
    idt_set(0x21,isr_kbd); idt_set(0x2C,isr_mouse);
    idtp.limit=sizeof idt-1; idtp.base=(u64)idt;
    __asm__ volatile("lidt %0"::"m"(idtp));
    outb(0x20,0x11); io_wait(); outb(0xA0,0x11); io_wait();
    outb(0x21,0x20); io_wait(); outb(0xA1,0x28); io_wait();
    outb(0x21,0x04); io_wait(); outb(0xA1,0x02); io_wait();
    outb(0x21,0x01); io_wait(); outb(0xA1,0x01); io_wait();
    outb(0x21,0xF9);   /* buka IRQ1 (keyboard) + IRQ2 (cascade) */
    outb(0xA1,0xEF);   /* buka IRQ12 (mouse) */
    while(inb(0x64)&1) inb(0x60);
    __asm__ volatile("sti");
}

/* ambil snapshot input sekali per frame */
static int ev_moved, ev_btn;
static void input_sample(void){
    __asm__ volatile("cli");
    int nx=pcx, ny=pcy, nb=pmbtn;
    if(pclick){ pclick=0; mclick=1; nx=pclick_x; ny=pclick_y; }  /* klik dievaluasi di posisi saat klik terjadi */
    __asm__ volatile("sti");
    ev_moved=(nx!=mx||ny!=my); ev_btn=(nb!=mb);
    mx=nx; my=ny; mb=nb;
}

static void kb_wait_in(void){ for(int i=0;i<200000;i++) if(!(inb(0x64)&2)) return; }
static void kb_wait_out(void){ for(int i=0;i<200000;i++) if(inb(0x64)&1) return; }
static void mouse_cmd(u8 c){ kb_wait_in(); outb(0x64,0xD4); kb_wait_in(); outb(0x60,c); kb_wait_out(); if(inb(0x64)&1) inb(0x60); }
static void input_init(void){
    while(inb(0x64)&1) inb(0x60);
    kb_wait_in(); outb(0x64,0xA8);
    kb_wait_in(); outb(0x64,0x20); kb_wait_out(); u8 s=inb(0x60);
    s|=3; s&=~0x20;
    kb_wait_in(); outb(0x64,0x60); kb_wait_in(); outb(0x60,s);
    mouse_cmd(0xF6); mouse_cmd(0xF4);
    irq_init();
}

/* ---- RTC ---- */
static int rtc_read(int r){ outb(0x70,r); return inb(0x71); }
static int bcd(int v){ return (v&15)+(v>>4)*10; }
static int rt_h, rt_m, rt_s, rt_d, rt_mo, rt_y;
static void rtc_get(void){
    for(int i=0;i<100000 && (rtc_read(0x0A)&0x80);i++);
    int b=rtc_read(0x0B);
    rt_s=rtc_read(0); rt_m=rtc_read(2); rt_h=rtc_read(4);
    rt_d=rtc_read(7); rt_mo=rtc_read(8); rt_y=rtc_read(9);
    if(!(b&4)){ rt_s=bcd(rt_s); rt_m=bcd(rt_m); rt_h=bcd(rt_h&0x7F)+((rt_h&0x80)?12:0); rt_d=bcd(rt_d); rt_mo=bcd(rt_mo); rt_y=bcd(rt_y); }
    rt_y+=2000;
}
static const char *mon_names[]={"","January","February","March","April","May","June","July","August","September","October","November","December"};

/* ---- power ---- */
static void do_reboot(void){
    outb(0x64,0xFE);
    for(volatile int i=0;i<1000000;i++);
    __asm__ volatile("lidt 0\n int3");
}
static void do_poweroff(void){
    outw(0x604,0x2000); outw(0xB004,0x2000); outw(0x4004,0x3400);
}
