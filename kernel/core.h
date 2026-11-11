#pragma once
#include <stdarg.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef unsigned long long u64; typedef long long i64;
#define NULL ((void*)0)

static inline void outb(u16 p, u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline u8 inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u64 rdtsc(void){ u32 lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((u64)hi<<32)|lo; }

/* ---- memory / string ---- */
void *memcpy(void *d, const void *s, u64 n){ void *o=d; __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); return o; }
void *memset(void *d, int c, u64 n){ void *o=d; __asm__ volatile("rep stosb":"+D"(d),"+c"(n):"a"(c):"memory"); return o; }
void *memmove(void *d, const void *s, u64 n){
    u8 *a=d; const u8 *b=s;
    if(a<b || a>=b+n){ for(u64 i=0;i<n;i++) a[i]=b[i]; }
    else { for(u64 i=n;i>0;i--) a[i-1]=b[i-1]; }
    return d;
}
static int memcmp_(const void *a,const void *b,int n){ const u8 *x=a,*y=b; while(n--){ if(*x!=*y) return *x-*y; x++; y++; } return 0; }
static int strlen(const char *s){ int n=0; while(s[n]) n++; return n; }
static int strcmp(const char *a, const char *b){ while(*a && *a==*b){a++;b++;} return (u8)*a-(u8)*b; }
static char lower(char c){ return (c>='A'&&c<='Z')? c+32 : c; }
static int strcasecmp(const char *a, const char *b){ while(*a && lower(*a)==lower(*b)){a++;b++;} return (u8)lower(*a)-(u8)lower(*b); }
static int strncmp(const char *a, const char *b, int n){ while(n>0 && *a && *a==*b){a++;b++;n--;} return n? (u8)*a-(u8)*b : 0; }
static void strcpy(char *d, const char *s){ while((*d++=*s++)); }
static void strncpyz(char *d, const char *s, int n){ int i=0; while(i<n-1 && s[i]){ d[i]=s[i]; i++; } d[i]=0; }
static void strcat(char *d, const char *s){ strcpy(d+strlen(d), s); }
static int starts(const char *s, const char *p){ return strncmp(s,p,strlen(p))==0; }
static int ends_ci(const char *s, const char *suf){ int a=strlen(s), b=strlen(suf); return a>=b && strcasecmp(s+a-b,suf)==0; }

static u8 heap[24<<20]; static u64 heap_pos;
static void *kmalloc(u64 n){ n=(n+15)&~15ULL; if(heap_pos+n>sizeof heap) return NULL; void *p=&heap[heap_pos]; heap_pos+=n; return p; }

static char *numstr(char *p, u64 v, int base, int wd, int zero){
    char t[24]; int n=0;
    if(!v) t[n++]='0';
    while(v){ int d=v%base; t[n++]= d<10? '0'+d : 'a'+d-10; v/=base; }
    while(n<wd) t[n++]= zero? '0':' ';
    while(n) *p++=t[--n];
    return p;
}
static int ksprintf(char *o, const char *f, ...){
    va_list a; va_start(a,f); char *p=o;
    while(*f){
        if(*f!='%'){ *p++=*f++; continue; }
        f++; int zero=0, wd=0;
        if(*f=='0'){ zero=1; f++; }
        while(*f>='0'&&*f<='9'){ wd=wd*10+*f-'0'; f++; }
        switch(*f){
        case 's': { const char *s=va_arg(a,const char*); if(!s) s="(null)"; while(*s) *p++=*s++; break; }
        case 'd': { int v=va_arg(a,int); if(v<0){ *p++='-'; v=-v; } p=numstr(p,(u64)v,10,wd,zero); break; }
        case 'u': { u32 v=va_arg(a,u32); p=numstr(p,v,10,wd,zero); break; }
        case 'x': { u32 v=va_arg(a,u32); p=numstr(p,v,16,wd,zero); break; }
        case 'c': { *p++=(char)va_arg(a,int); break; }
        case '%': *p++='%'; break;
        }
        if(*f) f++;
    }
    *p=0; va_end(a); return (int)(p-o);
}

/* ---- graphics ---- */
#include "font.h"
static u8 *FB; static int W=1024, H=768, PITCH=4096;
static u32 bb[1920*1080], wall[1920*1080];
static int cl0, ct0, cr0, cb0;
static void clip_reset(void){ cl0=0; ct0=0; cr0=W; cb0=H; }
static void clip_set(int x,int y,int w,int h){
    cl0=x<0?0:x; ct0=y<0?0:y; cr0=x+w>W?W:x+w; cb0=y+h>H?H:y+h;
}
static inline u32 mix(u32 d, u32 s, int a){
    int dr=(d>>16)&255, dg=(d>>8)&255, db=d&255, sr=(s>>16)&255, sg=(s>>8)&255, sb=s&255;
    return ((dr*(255-a)+sr*a)/255)<<16 | ((dg*(255-a)+sg*a)/255)<<8 | ((db*(255-a)+sb*a)/255);
}
static inline void px(int x,int y,u32 c){ if(x<cl0||y<ct0||x>=cr0||y>=cb0) return; bb[y*W+x]=c; }
static inline void pxA(int x,int y,u32 c,int a){ if(x<cl0||y<ct0||x>=cr0||y>=cb0) return; bb[y*W+x]=mix(bb[y*W+x],c,a); }
static void rectA(int x,int y,int w,int h,u32 c,int a){
    int x0=x<cl0?cl0:x, y0=y<ct0?ct0:y, x1=x+w>cr0?cr0:x+w, y1=y+h>cb0?cb0:y+h;
    for(int j=y0;j<y1;j++){
        u32 *row=&bb[j*W];
        if(a>=255) for(int i=x0;i<x1;i++) row[i]=c;
        else for(int i=x0;i<x1;i++) row[i]=mix(row[i],c,a);
    }
}
static void rect(int x,int y,int w,int h,u32 c){ rectA(x,y,w,h,c,255); }
/* corners bitmask: 1=TL 2=TR 4=BL 8=BR */
static void rrect2(int x,int y,int w,int h,int r,u32 c,int a,int corners){
    if(r*2>w) r=w/2; if(r*2>h) r=h/2; if(r<0) r=0;
    for(int j=0;j<h;j++){
        int yy=y+j; if(yy<ct0||yy>=cb0) continue;
        int il=0, ir=0;
        if(r>0 && (j<r || j>=h-r)){
            int top=j<r, cy=top?r:h-r, dy=2*j+1-2*cy;
            int lbit=top?1:4, rbit=top?2:8;
            int ins=0;
            for(int i=0;i<r;i++){ int dx=2*i+1-2*r; if(dx*dx+dy*dy<=4*r*r){ ins=i; break; } ins=i+1; }
            if(corners&lbit) il=ins;
            if(corners&rbit) ir=ins;
        }
        int x0=x+il, x1=x+w-ir;
        if(x0<cl0) x0=cl0; if(x1>cr0) x1=cr0;
        if(x1<=x0) continue;
        u32 *row=&bb[yy*W];
        if(a>=255) for(int i=x0;i<x1;i++) row[i]=c;
        else for(int i=x0;i<x1;i++) row[i]=mix(row[i],c,a);
    }
}
static void rrect(int x,int y,int w,int h,int r,u32 c,int a){ rrect2(x,y,w,h,r,c,a,15); }
static void hline(int x,int y,int w,u32 c){ rect(x,y,w,1,c); }
static void vline(int x,int y,int h,u32 c){ rect(x,y,1,h,c); }
static void frame(int x,int y,int w,int h,u32 c){ hline(x,y,w,c); hline(x,y+h-1,w,c); vline(x,y,h,c); vline(x+w-1,y,h,c); }
static void gchar(int x,int y,int ch,u32 c,int s){
    if(ch<32||ch>126) return;
    const u8 *g=font8x16[ch-32];
    for(int r=0;r<16;r++) for(int b=0;b<8;b++) if(g[r]&(0x80>>b)){
        if(s==1) px(x+b,y+r,c); else rect(x+b*s,y+r*s,s,s,c);
    }
}
static void text(int x,int y,const char *s,u32 c,int sc){ for(;*s;s++,x+=8*sc) gchar(x,y,*s,c,sc); }
static int textw(const char *s,int sc){ return strlen(s)*8*sc; }
static void textc(int cx,int y,const char *s,u32 c,int sc){ text(cx-textw(s,sc)/2,y,s,c,sc); }
static void circle(int cx,int cy,int r,u32 c){
    for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r) px(cx+x,cy+y,c);
}

static void flip(void){
    for(int y=0;y<H;y++) memcpy(FB+(u64)y*PITCH, &bb[y*W], (u64)W*4);
}

static void make_wallpaper(void){
    struct { int cx,cy,r; int R,G,B; } bl[4] = {
        { W*55/100, H*50/100, W*55/100, 0, 120, 215 },
        { W*30/100, H*65/100, W*45/100, 120, 60, 220 },
        { W*80/100, H*20/100, W*40/100, 0, 190, 230 },
        { W*10/100, H*10/100, W*35/100, 10, 40, 140 },
    };
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int R=4, G=10, B=34;
        for(int k=0;k<4;k++){
            i64 dx=x-bl[k].cx, dy=y-bl[k].cy, d2=dx*dx+dy*dy, r2=(i64)bl[k].r*bl[k].r;
            if(d2<r2){ i64 t=(r2-d2)*256/r2; t=t*t>>8; R+=bl[k].R*t/300; G+=bl[k].G*t/300; B+=bl[k].B*t/300; }
        }
        if(R>255)R=255; if(G>255)G=255; if(B>255)B=255;
        wall[y*W+x]=R<<16|G<<8|B;
    }
}
