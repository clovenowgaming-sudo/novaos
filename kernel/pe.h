#pragma once
#include "core.h"
#include "input.h"
/* ===== PE loader (PE32 x86 + PE32+ x64) + lapisan Win32/CRT minimal =====
   Program jalan di ring 0 (tanpa proteksi). x64: langsung long mode. x86: compatibility mode (CS=0x18).
   Import diresolve per NAMA ke tabel API di bawah (semua DLL satu namespace). Import yang belum ada -> program
   berhenti dengan pesan "Unimplemented import: nama". */
extern u64 gdt64[], pml4[], pdpt[], exc_table[32];
extern u32 pe_ret_pop;
extern u64 pe_enter64(u64 entry,u64 stack_top,u64 arg);
extern void pe_longjmp(u64 code) __attribute__((noreturn));
extern u32 pe_cb32(u64 fn,u32 *args,u32 nargs,u64 new_stack);
extern void thunk32_common(void); extern void thunk64_common(void);
extern u64 tsc_hz;

static void (*pe_out_fn)(const char*,int);
static void (*pe_refresh_fn)(void);
static void (*pe_msg_fn)(const char*,const char*,int);

/* ---- memory map + arena ---- */
static u64 mr_s[32], mr_e[32]; static int mr_n;
#define PA_START 0x400000ULL
#define PA_MAX   0x3C00000ULL
static u64 pa_base, pa_cur, pa_end;
static int pe_arena_init(void){
    pa_base=pa_cur=PA_START; pa_end=PA_MAX;
    if(mr_n){ int ok=0; for(int i=0;i<mr_n;i++) if(mr_s[i]<=PA_START && mr_e[i]>PA_START){ ok=1; if(mr_e[i]<pa_end) pa_end=mr_e[i]; }
        if(!ok) return 0; }
    return pa_end>pa_cur+(8<<20);
}
static void *pa_alloc(u64 n,u64 al){ u64 p=(pa_cur+al-1)&~(al-1); if(p+n>pa_end) return NULL; pa_cur=p+n; return (void*)p; }
static void *pa_zalloc(u64 n,u64 al){ void *p=pa_alloc(n,al); if(p) memset(p,0,n); return p; }

/* ---- state ---- */
static int pe_run_flag, pe_is64, pe_crashed; static char pe_msg[160];
static u64 pe_image_base; static char pe_path[96], pe_cmd[200];
static u32 *pe_args32; static u32 pe_lasterr;
static u8 *pe_iob; static int pe_iob_stride;
static u64 pe_stdin_line[1]; static char pe_inbuf[256]; static int pe_inlen, pe_inpos;
static u64 pe_last_refresh;
static int pe_ptr(void){ return pe_is64?8:4; }

/* ---- hal: exception handler ---- */
void exc_handler(u64 vec,u64 err,u64 rip,u64 cr2){
    if(pe_run_flag){
        ksprintf(pe_msg,"Program crashed: exception %d err=%x at %x%08x cr2=%x",(int)vec,(u32)err,(u32)(rip>>32),(u32)rip,(u32)cr2);
        pe_crashed=1; pe_longjmp(0xC0000005);
    }
    clip_reset(); rect(0,0,W,H,0x300000);
    char b[120]; ksprintf(b,"KERNEL PANIC: exception %d err=%x rip=%x%08x cr2=%x",(int)vec,(u32)err,(u32)(rip>>32),(u32)rip,(u32)cr2);
    text(20,20,b,0xFFFFFF,1); flip(); for(;;) __asm__ volatile("cli;hlt");
}

/* ---- page mapping untuk ImageBase >= 4GB ---- */
static u64 pt_pool[64][512] __attribute__((aligned(4096))); static int pt_used;
static void tlb_flush(void){ u64 c; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(c)::"memory"); }
static int vmap(u64 va,u64 pa,u64 size){
    if((va>>30)<4||(va>>39)) return -1;
    size=(size+4095)&~4095ULL;
    for(u64 o=0;o<size;o+=4096){
        u64 v=va+o; int i3=(v>>30)&511, i2=(v>>21)&511, i1=(v>>12)&511;
        if(!(pdpt[i3]&1)){ if(pt_used>=64) return -1; u64 *n=pt_pool[pt_used++]; memset(n,0,4096); pdpt[i3]=(u64)n|3; }
        u64 *pdp=(u64*)(pdpt[i3]&~0xFFFULL);
        if(!(pdp[i2]&1)){ if(pt_used>=64) return -1; u64 *n=pt_pool[pt_used++]; memset(n,0,4096); pdp[i2]=(u64)n|3; }
        u64 *pt=(u64*)(pdp[i2]&~0xFFFULL); pt[i1]=(pa+o)|3;
    }
    tlb_flush(); return 0;
}
static void vunmap_all(void){ for(int i=4;i<512;i++) pdpt[i]=0; pt_used=0; tlb_flush(); }

/* ---- API plumbing ---- */
typedef u64 (*api_fn)(u64*);
typedef struct { const char *name; api_fn fn; u8 nargs, cd, data; void *var; } ApiEnt;
static const ApiEnt api_tab[];
static int api_n;
typedef struct { const ApiEnt *a; char name[40]; } Imp;
static Imp imps[768]; static int nimp; static u8 *pe_stubs;
static const ApiEnt *api_find(const char *n){ for(int i=0;i<api_n;i++) if(!strcmp(api_tab[i].name,n)) return &api_tab[i]; return NULL; }
static u64 imp_bind(const char *name){
    const ApiEnt *a=api_find(name);
    if(a && a->data) return (u64)a->var;
    if(nimp>=768) return 0;
    int idx=nimp++; imps[idx].a=a; strncpyz(imps[idx].name,name,40);
    u8 *s=pe_stubs+idx*32; memset(s,0x90,32);
    if(!pe_is64){ s[0]=0x68; *(u32*)(s+1)=idx; s[5]=0xE9; *(u32*)(s+6)=(u32)((u64)thunk32_common-(u64)(s+10)); }
    else { s[0]=0xB8; *(u32*)(s+1)=idx; s[5]=0x49; s[6]=0xBB; *(u64*)(s+7)=(u64)thunk64_common; s[15]=0x41; s[16]=0xFF; s[17]=0xE3; }
    return (u64)s;
}
static void pe_unimpl(int idx){
    ksprintf(pe_msg,"Unimplemented import: %s",imps[idx].name); pe_crashed=1; pe_longjmp(0xC0000139);
}
u64 w64_dispatch(u32 idx,u64 *a){
    if(!imps[idx].a) pe_unimpl(idx);
    return imps[idx].a->fn(a);
}
u64 w32_dispatch(u32 idx,u32 *w){
    u64 a[16]; for(int i=0;i<16;i++) a[i]=w[i];
    pe_args32=w;
    if(!imps[idx].a) pe_unimpl(idx);
    const ApiEnt *e=imps[idx].a;
    u64 r=e->fn(a); pe_ret_pop=e->cd?0:e->nargs*4u; return r;
}

/* ---- callbacks ke kode PE ---- */
static u64 pe_call(u64 fn,u64 a0,u64 a1){
    if(pe_is64) return ((u64(__attribute__((ms_abi))*)(u64,u64))fn)(a0,a1);
    u32 ar[2]={(u32)a0,(u32)a1}; return pe_cb32(fn,ar,2,0);
}
static u64 pe_call0(u64 fn){ if(pe_is64) return ((u64(__attribute__((ms_abi))*)(void))fn)(); return pe_cb32(fn,NULL,0,0); }

/* ---- output / input ---- */
static void pe_write(const char *s,int n){
    char b[256]; int k=0;
    for(int i=0;i<n;i++){
        char c=s[i]; if(c=='\r') continue;
        b[k++]=c; if(k==255){ b[k]=0; if(pe_out_fn) pe_out_fn(b,k); k=0; }
    }
    if(k){ b[k]=0; if(pe_out_fn) pe_out_fn(b,k); }
    u64 t=rdtsc(); if(pe_refresh_fn && t-pe_last_refresh>tsc_hz/20){ pe_last_refresh=t; pe_refresh_fn(); }
}
static int pe_getkey(int echo){
    for(;;){
        int k=kq_pop();
        if(k){ if(k<256) return k; continue; }
        if(pe_refresh_fn){ pe_last_refresh=rdtsc(); pe_refresh_fn(); }
        for(int i=0;i<20000;i++) __asm__ volatile("pause");
    }
}
static void pe_fill_line(void){
    pe_inlen=0; pe_inpos=0;
    for(;;){
        int k=pe_getkey(1);
        if(k=='\n'){ pe_inbuf[pe_inlen++]='\n'; pe_write("\n",1); return; }
        if(k=='\b'){ if(pe_inlen>0){ pe_inlen--; pe_write("\b",1); } continue; }
        if(k>=32&&k<127&&pe_inlen<250){ pe_inbuf[pe_inlen++]=k; char c=k; pe_write(&c,1); }
    }
}
static int pe_stdin_getc(void){ if(pe_inpos>=pe_inlen) pe_fill_line(); return (u8)pe_inbuf[pe_inpos++]; }

/* ---- printf engine ---- */
typedef struct { const u8 *p; int st; } VA;
static VA mkva(u64 *a,int first){ VA v; if(pe_is64){ v.p=(u8*)(a+first); v.st=8; } else { v.p=(u8*)(pe_args32+first); v.st=4; } return v; }
static VA va_from(u64 ptr){ VA v; v.p=(u8*)ptr; v.st=pe_is64?8:4; return v; }
static u64 va_i(VA *v){ u64 r=v->st==4?*(u32*)v->p:*(u64*)v->p; v->p+=v->st; return r; }
static u64 va_ll(VA *v){ if(v->st==8) return va_i(v); u64 lo=va_i(v), hi=va_i(v); return lo|hi<<32; }
typedef unsigned __int128 u128;
static int fmt_dbl(char *o,u64 bits,int prec){
    char *s=o; int neg=bits>>63, e=(int)((bits>>52)&0x7FF); u64 m=bits&0xFFFFFFFFFFFFFULL;
    if(neg) *s++='-';
    if(e==0x7FF){ strcpy(s,m?"nan":"inf"); return strlen(o); }
    if(e) m|=1ULL<<52; e-=1023;
    u128 num; int k;
    if(e>=0){ if(e>63){ strcpy(s,"1e+huge"); return strlen(o); } num=(u128)m<<e; k=52; }
    else { num=m; k=52-e; if(k>120){ num=0; k=52; } }
    u64 ip=(u64)(num>>k); u128 fr=num&(((u128)1<<k)-1);
    char dg[40]; int nd=0;
    for(int i=0;i<prec&&i<30;i++){ fr=(fr<<3)+(fr<<1); dg[nd++]=(char)('0'+(int)(fr>>k)); fr&=(((u128)1<<k)-1); }
    if(fr>=((u128)1<<(k-1))){ int i=nd-1; for(;i>=0;i--){ if(dg[i]=='9') dg[i]='0'; else { dg[i]++; break; } } if(i<0) ip++; }
    char t[24]; int n=0; if(!ip) t[n++]='0'; while(ip){ t[n++]='0'+ip%10; ip/=10; } while(n) *s++=t[--n];
    if(nd){ *s++='.'; for(int i=0;i<nd;i++) *s++=dg[i]; }
    *s=0; return strlen(o);
}
static u32 pe_vfmt(char *o,u32 cap,const char *f,VA *v){
    u32 n=0;
#define PUT(c) do{ if(n+1<cap) o[n]=(c); n++; }while(0)
    while(*f){
        if(*f!='%'){ PUT(*f); f++; continue; }
        f++; int left=0,zero=0,plus=0,space=0,alt=0,w=0,prec=-1,lng=0;
        for(;;f++){ if(*f=='-') left=1; else if(*f=='0') zero=1; else if(*f=='+') plus=1; else if(*f==' ') space=1; else if(*f=='#') alt=1; else break; }
        if(*f=='*'){ w=(int)va_i(v); if(w<0){ left=1; w=-w; } f++; } else while(*f>='0'&&*f<='9') w=w*10+*f++-'0';
        if(*f=='.'){ f++; prec=0; if(*f=='*'){ prec=(int)va_i(v); f++; } else while(*f>='0'&&*f<='9') prec=prec*10+*f++-'0'; }
        for(;;f++){ if(*f=='l') lng++; else if(*f=='h'||*f=='L'||*f=='j'||*f=='t') { if(*f=='j') lng=2; } else if(*f=='z') lng=pe_is64?2:1; else if(*f=='I'){ if(f[1]=='6'&&f[2]=='4'){ lng=2; f+=2; } else if(f[1]=='3'&&f[2]=='2'){ f+=2; } else lng=pe_is64?2:1; } else break; }
        char tmp[80]; char *s=tmp; int len=0; char sign=0; int isnum=0;
        char c=*f; if(c) f++;
        switch(c){
        case '%': PUT('%'); continue;
        case 'c': tmp[0]=(char)va_i(v); len=1; s=tmp; break;
        case 's': case 'S': { u64 pv=va_i(v); const char *p=(const char*)pv; if(!p) p="(null)"; s=(char*)p;
            if(c=='S'||lng==1&&c=='s'&&0){ } len=0; while(p[len]&&(prec<0||len<prec)) len++; break; }
        case 'd': case 'i': { i64 x= lng>=2 ? (i64)va_ll(v) : (i64)(int)va_i(v); u64 ux=x<0?(u64)-x:(u64)x; if(x<0) sign='-'; else if(plus) sign='+'; else if(space) sign=' ';
            char t[24]; int k=0; if(!ux) t[k++]='0'; while(ux){ t[k++]='0'+ux%10; ux/=10; } while(k) tmp[len++]=t[--k]; isnum=1; break; }
        case 'u': case 'x': case 'X': case 'o': case 'p': {
            u64 ux; if(c=='p'){ ux=va_i(v); if(!pe_is64) ux&=0xFFFFFFFF; prec=pe_is64?16:8; } else ux= lng>=2 ? va_ll(v) : (u32)va_i(v);
            int base= c=='u'?10: c=='o'?8:16; const char *dgs= c=='x'?"0123456789abcdef":"0123456789ABCDEF";
            char t[24]; int k=0; if(!ux) t[k++]='0'; while(ux){ t[k++]=dgs[ux%base]; ux/=base; } while(k) tmp[len++]=t[--k];
            if(alt&&c!='u'&&c!='p'&&len&&!(len==1&&tmp[0]=='0')){ memmove(tmp+2,tmp,len); tmp[0]='0'; tmp[1]=c; len+=2; }
            isnum=1; break; }
        case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': {
            u64 bits=va_ll(v); if(prec<0) prec=6; len=fmt_dbl(tmp,bits,prec);
            if((c=='g'||c=='G')&&len>0){ char *dot=0; for(int i=0;i<len;i++) if(tmp[i]=='.') dot=&tmp[i]; if(dot){ while(len>0&&tmp[len-1]=='0') len--; if(tmp[len-1]=='.') len--; } }
            if(tmp[0]=='-'){ sign='-'; memmove(tmp,tmp+1,len); len--; } else if(plus) sign='+';
            prec=-1; isnum=2; break; }
        default: PUT('%'); if(c) PUT(c); continue;
        }
        if(isnum==1&&prec>=0){ zero=0; while(len<prec){ memmove(tmp+1,tmp,len); tmp[0]='0'; len++; } }
        int tot=len+(sign?1:0), pad=w>tot?w-tot:0;
        if(!left&&!zero) while(pad-->0) PUT(' ');
        if(sign) PUT(sign);
        if(!left&&zero&&isnum) while(pad-->0) PUT('0');
        for(int i=0;i<len;i++) PUT(s[i]);
        if(left) while(pad-->0) PUT(' ');
    }
    if(cap) o[n<cap?n:cap-1]=0;
    return n;
#undef PUT
}
static int pe_file_fd(u64 f){ if(!pe_iob||f<(u64)pe_iob||f>=(u64)pe_iob+3*pe_iob_stride) return -1; return (int)((f-(u64)pe_iob)/pe_iob_stride); }
static char pe_fbuf[4096];
static int pe_printf_to(int fd,const char *f,VA *v){
    u32 n=pe_vfmt(pe_fbuf,sizeof pe_fbuf,f,v); if(n>=sizeof pe_fbuf) n=sizeof pe_fbuf-1;
    if(fd==1||fd==2||fd<0) pe_write(pe_fbuf,n); return (int)n;
}

/* ---- heap ---- */
static void *pe_malloc(u64 n){ n=(n+15)&~15ULL; u8 *p=pa_alloc(n+16,16); if(!p) return NULL; *(u64*)p=n; return p+16; }
static void pe_free(void *p){ if(!p) return; u8 *b=(u8*)p-16; u64 n=*(u64*)b; if(b+16+n==(u8*)pa_cur) pa_cur=(u64)b; }
static void *pe_realloc(void *p,u64 n){ if(!p) return pe_malloc(n); u64 o=*(u64*)((u8*)p-16); if(n<=o) return p; void *q=pe_malloc(n); if(!q) return NULL; memcpy(q,p,o); return q; }

/* ---- exit ---- */
static u64 pe_exit_code;
static void pe_exit(u64 c){ pe_exit_code=c; pe_longjmp(c); }

#define A(n) static u64 api_##n(u64 *a)
#define S(i) ((const char*)a[i])
#define PV(i) ((void*)a[i])
#define W16(p,v) (*(u16*)(p)=(v))
/* ---- kernel32 ---- */
A(GetStdHandle){ int id=(int)a[0]; return id==-10?0x10:id==-11?0x14:id==-12?0x18:(u64)-1; }
static int h_fd(u64 h){ return h==0x10?0:h==0x14?1:h==0x18?2:-1; }
A(WriteFile){ int fd=h_fd(a[0]); if(fd<1){ pe_lasterr=6; return 0; } pe_write(S(1),(int)a[2]); if(a[3]) *(u32*)a[3]=(u32)a[2]; return 1; }
A(WriteConsoleA){ pe_write(S(1),(int)a[2]); if(a[3]) *(u32*)a[3]=(u32)a[2]; return 1; }
A(WriteConsoleW){ const u16 *w=PV(1); char b[200]; int n=(int)a[2]; for(int o=0;o<n;){ int c=0; while(c<199&&o+c<n){ b[c]=w[o+c]<128?(char)w[o+c]:'?'; c++; } pe_write(b,c); o+=c; } if(a[3]) *(u32*)a[3]=(u32)a[2]; return 1; }
A(ReadFile){ if(h_fd(a[0])!=0){ if(a[3]) *(u32*)a[3]=0; return 1; } char *d=PV(1); u32 n=(u32)a[2],i=0; if(pe_inpos>=pe_inlen) pe_fill_line(); while(i<n&&pe_inpos<pe_inlen) d[i++]=pe_inbuf[pe_inpos++]; if(a[3]) *(u32*)a[3]=i; return 1; }
A(ExitProcess){ pe_exit(a[0]); return 0; }
A(TerminateProcess){ pe_exit(a[1]); return 0; }
A(Zero){ return 0; }
A(One){ return 1; }
A(GetCurrentProcess){ return (u64)-1; }
A(GetCurrentThread){ return (u64)-2; }
A(GetCurrentId){ return 4; }
A(GetModuleHandle){ if(!a[0]) return pe_image_base; return 0x70000000; }
A(GetModuleFileNameA){ u32 n=(u32)a[2]; char *d=PV(1); int l=strlen(pe_path); if(l>=(int)n) l=n-1; memcpy(d,pe_path,l); d[l]=0; return l; }
A(GetModuleFileNameW){ u32 n=(u32)a[2]; u16 *d=PV(1); int l=strlen(pe_path); if(l>=(int)n) l=n-1; for(int i=0;i<l;i++) d[i]=pe_path[i]; d[l]=0; return l; }
A(GetProcAddress){ if(a[1]<0x10000) return 0; return imp_bind(S(1)); }
A(LoadLibrary){ return 0x70000000; }
A(GetCommandLineA){ return (u64)pe_cmd; }
static u16 *pe_cmdw;
A(GetCommandLineW){ return (u64)pe_cmdw; }
static u8 pe_envw[8];
A(GetEnvironmentStringsW){ return (u64)pe_envw; }
A(GetEnvironmentStringsA){ return (u64)pe_envw; }
A(GetEnvVar){ pe_lasterr=203; return 0; }
A(GetLastError){ return pe_lasterr; }
A(SetLastError){ pe_lasterr=(u32)a[0]; return 0; }
A(GetProcessHeap){ return 0xA0000; }
A(HeapCreate){ return 0xA0000; }
A(HeapAlloc){ void *p=pe_malloc(a[2]); if(p&&(a[1]&8)) memset(p,0,a[2]); return (u64)p; }
A(HeapFree){ pe_free(PV(2)); return 1; }
A(HeapReAlloc){ return (u64)pe_realloc(PV(2),a[3]); }
A(HeapSize){ return a[2]?*(u64*)((u8*)a[2]-16):(u64)-1; }
A(LocalAlloc){ void *p=pe_malloc(a[1]); if(p&&(a[0]&0x40)) memset(p,0,a[1]); return (u64)p; }
A(LocalFree){ pe_free(PV(0)); return 0; }
A(VirtualAlloc){ u64 n=(a[1]+4095)&~4095ULL; if(a[0]&&a[0]>=pa_base&&a[0]<pa_end) return a[0]; return (u64)pa_zalloc(n,4096); }
A(VirtualFree){ return 1; }
A(VirtualQuery){ u8 *m=PV(1); memset(m,0,pe_is64?48:28); u64 base=a[0]&~4095ULL;
    if(pe_is64){ *(u64*)m=base; *(u64*)(m+8)=base; *(u32*)(m+16)=4; *(u64*)(m+24)=4096; *(u32*)(m+32)=0x1000; *(u32*)(m+36)=4; *(u32*)(m+40)=0x20000; return 48; }
    *(u32*)m=(u32)base; *(u32*)(m+4)=(u32)base; *(u32*)(m+8)=4; *(u32*)(m+12)=4096; *(u32*)(m+16)=0x1000; *(u32*)(m+20)=4; *(u32*)(m+24)=0x20000; return 28; }
A(VirtualProtect){ if(a[3]) *(u32*)a[3]=4; return 1; }
A(Sleep){ u64 t=rdtsc()+a[0]*(tsc_hz/1000); while(rdtsc()<t){ if(pe_refresh_fn&&rdtsc()-pe_last_refresh>tsc_hz/20){ pe_last_refresh=rdtsc(); pe_refresh_fn(); } __asm__ volatile("pause"); } return 0; }
A(GetTickCount){ return (u32)(rdtsc()/(tsc_hz/1000)); }
A(GetTickCount64){ return rdtsc()/(tsc_hz/1000); }
A(QueryPerformanceCounter){ *(u64*)a[0]=rdtsc(); return 1; }
A(QueryPerformanceFrequency){ *(u64*)a[0]=tsc_hz; return 1; }
A(GetSystemTimeAsFileTime){ *(u64*)a[0]=132000000000000000ULL+rdtsc()/(tsc_hz/10000000); return 0; }
A(TlsAlloc){ static int n; return n<60?n++:(u64)-1; }
static u64 tlsv[64];
A(TlsGetValue){ return a[0]<64?tlsv[a[0]]:0; }
A(TlsSetValue){ if(a[0]<64) tlsv[a[0]]=a[1]; return 1; }
A(GetStartupInfo){ u8 *s=PV(0); int n=pe_is64?104:68; memset(s,0,n); *(u32*)s=n; return 0; }
A(GetSystemInfo){ u8 *s=PV(0); memset(s,0,48); *(u32*)(s+4)=4096; if(pe_is64){ *(u64*)(s+8)=0x10000; *(u64*)(s+16)=0x7FFFFFFEFFFFULL; *(u64*)(s+24)=1; *(u32*)(s+32)=1; *(u32*)(s+44)=65536; } else { *(u32*)(s+8)=0x10000; *(u32*)(s+12)=0x7FFEFFFF; *(u32*)(s+16)=1; *(u32*)(s+20)=1; *(u32*)(s+28)=65536; } return 0; }
A(GetFileType){ return h_fd(a[0])>=0?2:0; }
A(GetConsoleMode){ if(a[1]) *(u32*)a[1]=3; return 1; }
A(GetCP){ return 437; }
A(GetCPInfo){ u8 *p=PV(1); memset(p,0,20); *(u32*)p=1; return 1; }
A(MultiByteToWideChar){ const u8 *s=PV(2); int n=(int)a[3]; u16 *d=PV(4); if(n<0) n=strlen((const char*)s)+1; if(!a[5]) return n; for(int i=0;i<n&&i<(int)a[5];i++) d[i]=s[i]; return n; }
A(WideCharToMultiByte){ const u16 *s=PV(2); int n=(int)a[3]; char *d=PV(4); if(n<0){ n=0; while(s[n]) n++; n++; } if(!a[5]) return n; for(int i=0;i<n&&i<(int)a[5];i++) d[i]=s[i]<256?(char)s[i]:'?'; return n; }
A(InterlockedIncrement){ return ++*(volatile u32*)a[0]; }
A(InterlockedDecrement){ return --*(volatile u32*)a[0]; }
A(InterlockedExchange){ u32 o=*(u32*)a[0]; *(u32*)a[0]=(u32)a[1]; return o; }
A(InterlockedCompareExchange){ u32 o=*(u32*)a[0]; if(o==(u32)a[2]) *(u32*)a[0]=(u32)a[1]; return o; }
A(InterlockedExchangeAdd){ u32 o=*(u32*)a[0]; *(u32*)a[0]=o+(u32)a[1]; return o; }
A(RtlCaptureContext){ memset(PV(0),0,pe_is64?1232:716); return 0; }
A(GetVersion){ return 0x0A00; }
A(MessageBox){ const char *t=S(2),*c=S(1); char tb[48],cb[240]; if(a[1]==0) c=""; strncpyz(cb,c,240); strncpyz(tb,t?t:"Message",48);
    if(pe_msg_fn){ pe_msg_fn(tb,cb,1); for(;;){ int k=pe_getkey(0); if(k=='\n'||k==27||k==' ') break; } pe_msg_fn(tb,cb,0); } return 1; }
A(MessageBoxW){ char tb[48],cb[240]; const u16 *t=PV(2),*c=PV(1); int i; for(i=0;c&&c[i]&&i<239;i++) cb[i]=c[i]<128?c[i]:'?'; cb[i]=0; for(i=0;t&&t[i]&&i<47;i++) tb[i]=t[i]<128?t[i]:'?'; tb[i]=0;
    if(pe_msg_fn){ pe_msg_fn(tb,cb,1); for(;;){ int k=pe_getkey(0); if(k=='\n'||k==27||k==' ') break; } pe_msg_fn(tb,cb,0); } return 1; }
/* ---- CRT: stdio ---- */
A(printf){ VA v=mkva(a,1); return pe_printf_to(1,S(0),&v); }
A(vprintf){ VA v=va_from(a[1]); return pe_printf_to(1,S(0),&v); }
A(fprintf){ VA v=mkva(a,2); return pe_printf_to(pe_file_fd(a[0]),S(1),&v); }
A(vfprintf){ VA v=va_from(a[2]); return pe_printf_to(pe_file_fd(a[0]),S(1),&v); }
A(sprintf){ VA v=mkva(a,2); return pe_vfmt(PV(0),0x7FFFFFFF,S(1),&v); }
A(vsprintf){ VA v=va_from(a[2]); return pe_vfmt(PV(0),0x7FFFFFFF,S(1),&v); }
A(snprintf){ VA v=mkva(a,3); u32 n=pe_vfmt(PV(0),(u32)a[1],S(2),&v); return n; }
A(vsnprintf){ VA v=va_from(a[3]); return pe_vfmt(PV(0),(u32)a[1],S(2),&v); }
A(stdio_vfprintf){ int o=pe_is64?0:1; VA v=va_from(a[4+o]); return pe_printf_to(pe_file_fd(a[1+o]),S(2+o),&v); }
A(stdio_vsprintf){ int o=pe_is64?0:1; VA v=va_from(a[5+o]); return pe_vfmt(PV(1+o),(u32)a[2+o]?(u32)a[2+o]:0x7FFFFFFF,S(3+o),&v); }
A(puts){ pe_write(S(0),strlen(S(0))); pe_write("\n",1); return 1; }
A(putchar){ char c=(char)a[0]; pe_write(&c,1); return a[0]&255; }
A(fputs){ if(pe_file_fd(a[1])>0) pe_write(S(0),strlen(S(0))); return 1; }
A(fputc){ char c=(char)a[0]; if(pe_file_fd(a[1])>0) pe_write(&c,1); return a[0]&255; }
A(fwrite){ if(pe_file_fd(a[3])>0) pe_write(S(0),(int)(a[1]*a[2])); return a[2]; }
A(write_){ if(a[0]==1||a[0]==2) pe_write(S(1),(int)a[2]); return a[2]; }
A(getchar){ return pe_stdin_getc(); }
A(fgets){ char *d=PV(0); int n=(int)a[1],i=0; if(pe_file_fd(a[2])!=0) return 0; while(i<n-1){ int c=pe_stdin_getc(); d[i++]=c; if(c=='\n') break; } d[i]=0; return (u64)d; }
A(getch){ return pe_getkey(0); }
A(kbhit){ return kqh!=kqt; }
A(iob_func){ return (u64)pe_iob; }
A(acrt_iob_func){ return (u64)pe_iob+a[0]*pe_iob_stride; }
A(fflush){ return 0; }
A(fileno){ return pe_file_fd(a[0]); }
A(fopen){ return 0; }
/* ---- CRT: memory/string ---- */
A(malloc){ return (u64)pe_malloc(a[0]); }
A(calloc){ void *p=pe_malloc(a[0]*a[1]); if(p) memset(p,0,a[0]*a[1]); return (u64)p; }
A(realloc){ return (u64)pe_realloc(PV(0),a[1]); }
A(free){ pe_free(PV(0)); return 0; }
A(memcpy){ memcpy(PV(0),PV(1),a[2]); return a[0]; }
A(memmove){ memmove(PV(0),PV(1),a[2]); return a[0]; }
A(memset){ memset(PV(0),(int)a[1],a[2]); return a[0]; }
A(memcmp){ return (u64)(i64)memcmp_(PV(0),PV(1),(int)a[2]); }
A(strlen){ return strlen(S(0)); }
A(strcmp){ return (u64)(i64)strcmp(S(0),S(1)); }
A(strncmp){ return (u64)(i64)strncmp(S(0),S(1),(int)a[2]); }
A(stricmp){ return (u64)(i64)strcasecmp(S(0),S(1)); }
A(strcpy){ strcpy(PV(0),S(1)); return a[0]; }
A(strncpy){ char *d=PV(0); const char *s=S(1); u64 i=0; for(;i<a[2]&&s[i];i++) d[i]=s[i]; for(;i<a[2];i++) d[i]=0; return a[0]; }
A(strcat){ strcat(PV(0),S(1)); return a[0]; }
A(strchr){ const char *s=S(0); for(;;s++){ if(*s==(char)a[1]) return (u64)s; if(!*s) return 0; } }
A(strrchr){ const char *s=S(0),*r=0; for(;;s++){ if(*s==(char)a[1]) r=s; if(!*s) return (u64)r; } }
A(strstr){ const char *h=S(0),*n=S(1); int l=strlen(n); for(;*h;h++) if(!strncmp(h,n,l)) return (u64)h; return l?0:(u64)h; }
A(toupper){ int c=(int)a[0]; return c>='a'&&c<='z'?c-32:c; }
A(tolower){ int c=(int)a[0]; return c>='A'&&c<='Z'?c+32:c; }
A(isdigit){ int c=(int)a[0]; return c>='0'&&c<='9'; }
A(isalpha){ int c=(int)a[0]|32; return c>='a'&&c<='z'; }
A(isspace){ int c=(int)a[0]; return c==' '||(c>=9&&c<=13); }
A(isalnum){ int c=(int)a[0]; return (c>='0'&&c<='9')||((c|32)>='a'&&(c|32)<='z'); }
A(strtol){ const char *s=S(0); int base=(int)a[2]; while(*s==' ') s++; int neg=0; if(*s=='-'){ neg=1; s++; } else if(*s=='+') s++;
    if((!base||base==16)&&s[0]=='0'&&(s[1]|32)=='x'){ s+=2; base=16; } else if(!base) base=s[0]=='0'?8:10;
    i64 r=0; for(;;s++){ int d=*s>='0'&&*s<='9'?*s-'0':(*s|32)>='a'&&(*s|32)<='z'?(*s|32)-'a'+10:99; if(d>=base) break; r=r*base+d; }
    if(a[1]) *(u64*)a[1]=(u64)s; if(neg) r=-r; return (u64)r; }
A(atoi){ u64 b[3]={a[0],0,10}; return api_strtol(b); }
A(abs){ int v=(int)a[0]; return v<0?-v:v; }
static u32 rnd=1;
A(srand){ rnd=(u32)a[0]; return 0; }
A(rand){ rnd=rnd*1103515245+12345; return (rnd>>16)&0x7FFF; }
A(time){ u64 t=1700000000+rdtsc()/tsc_hz; if(a[0]) *(u64*)a[0]=t; return t; }
A(clock){ return rdtsc()/(tsc_hz/1000); }
A(qsort){ u8 *b=PV(0); u64 n=a[1],sz=a[2]; u8 t[256]; if(sz>256) return 0;
    for(u64 i=1;i<n;i++){ memcpy(t,b+i*sz,sz); u64 j=i; while(j>0&&(int)pe_call(a[3],(u64)(b+(j-1)*sz),(u64)t)>0){ memcpy(b+j*sz,b+(j-1)*sz,sz); j--; } memcpy(b+j*sz,t,sz); }
    return 0; }
/* ---- CRT: startup ---- */
static int pe_argc; static u64 pe_argv, pe_envp, pe_argv_var, pe_environ_var; static u32 pe_fmode, pe_commode;
A(exit){ pe_exit(a[0]); return 0; }
A(abort){ ksprintf(pe_msg,"abort() called"); pe_crashed=1; pe_longjmp(3); return 0; }
A(getmainargs){ *(int*)a[0]=pe_argc; if(pe_is64){ *(u64*)a[1]=pe_argv; *(u64*)a[2]=pe_envp; } else { *(u32*)a[1]=(u32)pe_argv; *(u32*)a[2]=(u32)pe_envp; } return 0; }
A(p_argc){ return (u64)&pe_argc; }
A(p_argv){ pe_argv_var=pe_argv; return (u64)&pe_argv_var; }
A(p_environ){ pe_environ_var=pe_envp; return (u64)&pe_environ_var; }
A(p_fmode){ return (u64)&pe_fmode; }
A(p_commode){ return (u64)&pe_commode; }
A(errno_){ static int e; return (u64)&e; }
A(initterm){ u64 *p64=PV(0),*e64=PV(1); u32 *p32=PV(0),*e32=PV(1);
    if(pe_is64){ for(;p64<e64;p64++) if(*p64) pe_call0(*p64); } else { for(;p32<e32;p32++) if(*p32) pe_call0(*p32); } return 0; }
A(initterm_e){ api_initterm(a); return 0; }
A(getenv){ return 0; }
A(signal){ return 0; }
A(system){ return (u64)-1; }

#define E(n,f,na,cd) { n, api_##f, na, cd, 0, 0 }
#define K(n,f,na) E(n,f,na,0)
#define C(n,f,na) E(n,f,na,1)
#define D(n,v) { n, 0, 0, 0, 1, (void*)&(v) }
static const ApiEnt api_tab[]={
 K("GetStdHandle",GetStdHandle,1),K("WriteFile",WriteFile,5),K("WriteConsoleA",WriteConsoleA,5),K("WriteConsoleW",WriteConsoleW,5),
 K("ReadFile",ReadFile,5),K("ReadConsoleA",ReadFile,5),K("ExitProcess",ExitProcess,1),K("TerminateProcess",TerminateProcess,2),
 K("GetCurrentProcess",GetCurrentProcess,0),K("GetCurrentThread",GetCurrentThread,0),K("GetCurrentProcessId",GetCurrentId,0),K("GetCurrentThreadId",GetCurrentId,0),
 K("GetModuleHandleA",GetModuleHandle,1),K("GetModuleHandleW",GetModuleHandle,1),K("GetModuleFileNameA",GetModuleFileNameA,3),K("GetModuleFileNameW",GetModuleFileNameW,3),
 K("GetProcAddress",GetProcAddress,2),K("LoadLibraryA",LoadLibrary,1),K("LoadLibraryW",LoadLibrary,1),K("LoadLibraryExA",LoadLibrary,3),K("LoadLibraryExW",LoadLibrary,3),K("FreeLibrary",One,1),
 K("GetCommandLineA",GetCommandLineA,0),K("GetCommandLineW",GetCommandLineW,0),K("GetEnvironmentStringsW",GetEnvironmentStringsW,0),K("GetEnvironmentStrings",GetEnvironmentStringsA,0),
 K("FreeEnvironmentStringsW",One,1),K("FreeEnvironmentStringsA",One,1),K("GetEnvironmentVariableA",GetEnvVar,3),K("GetEnvironmentVariableW",GetEnvVar,3),
 K("GetLastError",GetLastError,0),K("SetLastError",SetLastError,1),K("GetProcessHeap",GetProcessHeap,0),K("HeapCreate",HeapCreate,3),K("HeapDestroy",One,1),
 K("HeapAlloc",HeapAlloc,3),K("HeapFree",HeapFree,3),K("HeapReAlloc",HeapReAlloc,4),K("HeapSize",HeapSize,3),K("HeapSetInformation",One,4),
 K("LocalAlloc",LocalAlloc,2),K("LocalFree",LocalFree,1),K("GlobalAlloc",LocalAlloc,2),K("GlobalFree",LocalFree,1),
 K("VirtualAlloc",VirtualAlloc,4),K("VirtualFree",VirtualFree,3),K("VirtualQuery",VirtualQuery,3),K("VirtualProtect",VirtualProtect,4),
 K("Sleep",Sleep,1),K("GetTickCount",GetTickCount,0),K("GetTickCount64",GetTickCount64,0),K("QueryPerformanceCounter",QueryPerformanceCounter,1),K("QueryPerformanceFrequency",QueryPerformanceFrequency,1),
 K("GetSystemTimeAsFileTime",GetSystemTimeAsFileTime,1),K("GetSystemTimePreciseAsFileTime",GetSystemTimeAsFileTime,1),
 K("InitializeCriticalSection",Zero,1),K("InitializeCriticalSectionAndSpinCount",One,2),K("InitializeCriticalSectionEx",One,3),K("EnterCriticalSection",Zero,1),K("LeaveCriticalSection",Zero,1),K("DeleteCriticalSection",Zero,1),K("TryEnterCriticalSection",One,1),
 K("TlsAlloc",TlsAlloc,0),K("TlsGetValue",TlsGetValue,1),K("TlsSetValue",TlsSetValue,2),K("TlsFree",One,1),
 K("FlsAlloc",TlsAlloc,1),K("FlsGetValue",TlsGetValue,1),K("FlsSetValue",TlsSetValue,2),K("FlsFree",One,1),
 K("SetUnhandledExceptionFilter",Zero,1),K("UnhandledExceptionFilter",Zero,1),K("IsProcessorFeaturePresent",Zero,1),K("IsDebuggerPresent",Zero,0),
 K("GetStartupInfoA",GetStartupInfo,1),K("GetStartupInfoW",GetStartupInfo,1),K("GetFileType",GetFileType,1),K("SetConsoleCtrlHandler",One,2),
 K("GetConsoleMode",GetConsoleMode,2),K("SetConsoleMode",One,2),K("GetConsoleCP",GetCP,0),K("GetConsoleOutputCP",GetCP,0),K("SetConsoleOutputCP",One,1),K("SetConsoleTitleA",One,1),
 K("GetACP",GetCP,0),K("GetOEMCP",GetCP,0),K("GetCPInfo",GetCPInfo,2),K("MultiByteToWideChar",MultiByteToWideChar,6),K("WideCharToMultiByte",WideCharToMultiByte,8),
 K("InterlockedIncrement",InterlockedIncrement,1),K("InterlockedDecrement",InterlockedDecrement,1),K("InterlockedExchange",InterlockedExchange,2),
 K("InterlockedCompareExchange",InterlockedCompareExchange,3),K("InterlockedExchangeAdd",InterlockedExchangeAdd,2),K("InitializeSListHead",Zero,1),
 K("RtlCaptureContext",RtlCaptureContext,1),K("RtlLookupFunctionEntry",Zero,3),K("RtlVirtualUnwind",Zero,8),K("GetSystemInfo",GetSystemInfo,1),K("CloseHandle",One,1),
 K("FlushFileBuffers",One,1),K("GetVersion",GetVersion,0),K("SetEnvironmentVariableA",One,2),
 K("MessageBoxA",MessageBox,4),K("MessageBoxW",MessageBoxW,4),
 C("printf",printf,1),C("vprintf",vprintf,2),C("fprintf",fprintf,2),C("vfprintf",vfprintf,3),C("sprintf",sprintf,2),C("vsprintf",vsprintf,3),
 C("_snprintf",snprintf,3),C("snprintf",snprintf,3),C("_vsnprintf",vsnprintf,4),C("vsnprintf",vsnprintf,4),
 C("__stdio_common_vfprintf",stdio_vfprintf,0),C("__stdio_common_vsprintf",stdio_vsprintf,0),C("__stdio_common_vsnprintf_s",stdio_vsprintf,0),
 C("puts",puts,1),C("putchar",putchar,1),C("fputs",fputs,2),C("fputc",fputc,2),C("putc",fputc,2),C("fwrite",fwrite,4),C("_write",write_,3),
 C("getchar",getchar,0),C("fgets",fgets,3),C("_getch",getch,0),C("_getche",getch,0),C("_kbhit",kbhit,0),C("__iob_func",iob_func,0),C("__acrt_iob_func",acrt_iob_func,1),
 C("fflush",fflush,1),C("_fileno",fileno,1),C("fopen",fopen,2),C("_setmode",Zero,2),C("_isatty",One,1),
 C("malloc",malloc,1),C("calloc",calloc,2),C("realloc",realloc,2),C("free",free,1),
 C("memcpy",memcpy,3),C("memmove",memmove,3),C("memset",memset,3),C("memcmp",memcmp,3),C("strlen",strlen,1),C("strcmp",strcmp,2),C("strncmp",strncmp,3),
 C("_stricmp",stricmp,2),C("stricmp",stricmp,2),C("strcpy",strcpy,2),C("strncpy",strncpy,3),C("strcat",strcat,2),C("strchr",strchr,2),C("strrchr",strrchr,2),C("strstr",strstr,2),
 C("toupper",toupper,1),C("tolower",tolower,1),C("isdigit",isdigit,1),C("isalpha",isalpha,1),C("isspace",isspace,1),C("isalnum",isalnum,1),
 C("strtol",strtol,3),C("strtoul",strtol,3),C("atoi",atoi,1),C("atol",atoi,1),C("abs",abs,1),C("srand",srand,1),C("rand",rand,0),C("time",time,1),C("clock",clock,0),C("qsort",qsort,4),
 C("exit",exit,1),C("_exit",exit,1),C("abort",abort,0),C("_cexit",Zero,0),C("_c_exit",Zero,0),C("atexit",Zero,1),C("_onexit",Zero,1),C("_crt_atexit",Zero,1),C("_amsg_exit",exit,1),
 C("__getmainargs",getmainargs,5),C("__p___argc",p_argc,0),C("__p___argv",p_argv,0),C("__p__environ",p_environ,0),C("__p__fmode",p_fmode,0),C("__p__commode",p_commode,0),
 C("_errno",errno_,0),C("_initterm",initterm,2),C("_initterm_e",initterm_e,2),
 C("__set_app_type",Zero,1),C("_set_app_type",Zero,1),C("__setusermatherr",Zero,1),C("_set_invalid_parameter_handler",Zero,1),C("_controlfp_s",Zero,3),C("_configure_narrow_argv",Zero,1),
 C("_initialize_narrow_environment",Zero,0),C("_set_new_mode",Zero,1),C("_set_fmode",Zero,1),C("_register_onexit_function",Zero,2),C("_initialize_onexit_table",Zero,1),
 C("_seh_filter_exe",Zero,2),C("_XcptFilter",Zero,2),C("__C_specific_handler",Zero,4),C("signal",signal,2),C("getenv",getenv,1),C("system",system,1),
 D("_fmode",pe_fmode),D("_commode",pe_commode),D("__imp__fmode",pe_fmode),D("__imp__commode",pe_commode),
};
#define API_COUNT ((int)(sizeof api_tab/sizeof api_tab[0]))

/* ---- loader ---- */
static u16 rd16(const u8 *p){ return *(const u16*)p; }
static u32 rd32(const u8 *p){ return *(const u32*)p; }
static u64 rd64(const u8 *p){ return *(const u64*)p; }
typedef struct { u8 *phys; u64 vbase, ibase; u32 size; u64 entry; int is64, vm; } PeImg;
static const char *pe_load(const u8 *f,u32 len,PeImg *im){
    if(len<0x200||f[0]!='M'||f[1]!='Z') return "Not a valid Windows program (no MZ header)";
    u32 lf=rd32(f+0x3C); if(lf+0x108>len||rd32(f+lf)!=0x4550) return "Not a valid PE file";
    u16 mach=rd16(f+lf+4), nsec=rd16(f+lf+6), osz=rd16(f+lf+20); const u8 *o=f+lf+24; u16 mg=rd16(o);
    int is64; if(mg==0x20b&&mach==0x8664) is64=1; else if(mg==0x10b&&mach==0x14c) is64=0; else return "Unsupported PE (need x86 or x64 .exe)";
    if(rd16(f+lf+22)&0x2000) return "This is a DLL, not an .exe";
    u32 entry=rd32(o+16), size=rd32(o+56), hsz=rd32(o+60); u64 ib=is64?rd64(o+24):rd32(o+28);
    u32 nd=rd32(o+(is64?108:92)); const u8 *dd=o+(is64?112:96); const u8 *sec=o+osz;
    if(!size||size>(40u<<20)) return "Image too large or invalid";
    size=(size+4095)&~4095u; im->is64=is64; im->size=size; im->ibase=ib; im->vm=0;
    u32 reloc_rva=nd>5?rd32(dd+5*8):0, reloc_sz=nd>5?rd32(dd+5*8+4):0;
    u64 delta=0;
    if(ib>=0x100000000ULL){ if(!is64) return "Bad ImageBase"; im->phys=pa_alloc(size,4096); if(!im->phys) return "Out of memory"; im->vbase=ib; im->vm=1; }
    else if(ib>=pa_cur && ib+size<=pa_end && !(ib&4095)){ im->phys=(u8*)ib; pa_cur=ib+size; im->vbase=ib; }
    else { if(!reloc_sz) return "Program must load at a fixed address and has no relocation data"; im->phys=pa_alloc(size,4096); if(!im->phys) return "Out of memory"; im->vbase=(u64)im->phys; delta=im->vbase-ib; }
    memset(im->phys,0,size);
    if(hsz>len) hsz=len; if(hsz>size) hsz=size; memcpy(im->phys,f,hsz);
    for(int i=0;i<nsec;i++){ const u8 *s=sec+i*40; u32 va=rd32(s+12), rs=rd32(s+16), rp=rd32(s+20), vs=rd32(s+8);
        if(!rs) continue; if(rs>vs&&vs) rs=vs; if(rp+rs>len||va+rs>size) return "Corrupt section table"; memcpy(im->phys+va,f+rp,rs); }
    if(delta&&reloc_sz){
        u32 off=0; while(off+8<=reloc_sz){ u32 pg=rd32(im->phys+reloc_rva+off), bs=rd32(im->phys+reloc_rva+off+4); if(bs<8) break;
            for(u32 k=8;k+2<=bs;k+=2){ u16 e=rd16(im->phys+reloc_rva+off+k); u32 t=e>>12, ro=pg+(e&0xFFF); if(ro+8>size) continue;
                if(t==3) *(u32*)(im->phys+ro)+=(u32)delta; else if(t==10) *(u64*)(im->phys+ro)+=delta; }
            off+=bs; }
    }
    /* imports */
    u32 imp_rva=nd>1?rd32(dd+8):0;
    if(imp_rva){ int ps=is64?8:4;
        for(const u8 *d=im->phys+imp_rva; rd32(d+12); d+=20){
            u32 ot=rd32(d), ft=rd32(d+16); if(!ot) ot=ft; if(ot>=size||ft>=size) return "Corrupt import table";
            for(int j=0;;j++){
                u64 ent= is64? rd64(im->phys+ot+j*ps) : rd32(im->phys+ot+j*ps); if(!ent) break;
                u64 addr;
                if(ent>>(is64?63:31)) addr=imp_bind("(ordinal)");
                else { if(ent+2>=size) return "Corrupt import name"; addr=imp_bind((const char*)im->phys+ent+2); }
                if(!addr) return "Too many imports";
                if(is64) *(u64*)(im->phys+ft+j*ps)=addr; else *(u32*)(im->phys+ft+j*ps)=(u32)addr;
            } } }
    im->entry=im->vbase+entry;
    /* TLS */
    u32 tls_rva=nd>9?rd32(dd+9*8):0;
    if(tls_rva&&tls_rva<size){ const u8 *t=im->phys+tls_rva; u64 st,en,idx; u32 zf;
        if(is64){ st=rd64(t); en=rd64(t+8); idx=rd64(t+16); zf=rd32(t+32); } else { st=rd32(t); en=rd32(t+4); idx=rd32(t+8); zf=rd32(t+16); }
        u64 tsz=(en-st)+zf; u8 *blk=pa_zalloc(tsz+16,16); if(!blk) return "Out of memory (TLS)";
        if(en>st&&st-im->vbase<size) memcpy(blk,im->phys+(st-im->vbase),en-st);
        if(idx-im->vbase<size) *(u32*)(im->phys+(idx-im->vbase))=0;
        u64 *arr=pa_zalloc(64,16); arr[0]=(u64)blk; (void)arr; im->vm|=0; pe_stdin_line[0]=(u64)arr;
    } else pe_stdin_line[0]=0;
    return NULL;
}
static void pe_cpu_init(void){
    u64 cr0,cr4; __asm__ volatile("clts; mov %%cr0,%0":"=r"(cr0));
    cr0&=~4ULL; cr0|=2|32; __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4)); cr4|=(1<<9)|(1<<10); __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
    __asm__ volatile("fninit");
}
static void wrmsr_(u32 m,u64 v){ __asm__ volatile("wrmsr"::"c"(m),"a"((u32)v),"d"((u32)(v>>32))); }
/* jalankan program. return NULL kalau selesai normal (kode di *code), atau pesan error */
static const char *pe_run(const u8 *file,u32 len,const char *path,const char *args,u32 *code){
    static PeImg im; *code=0; pe_crashed=0; pe_msg[0]=0;
    if(!pe_arena_init()) return "Not enough free memory below 60MB to run programs";
    pe_path[0]=0; strncpyz(pe_path,path,96); pe_image_base=0; nimp=0; api_n=API_COUNT;
    /* deteksi bitness dulu (imp_bind butuh pe_is64) */
    if(len>0x200&&file[0]=='M'){ u32 lf=rd32(file+0x3C); if(lf+8<len) pe_is64=(rd16(file+lf+4)==0x8664); }
    pa_end=(pa_end-768*32)&~4095ULL; pe_stubs=(u8*)pa_end; memset(pe_stubs,0,768*32);
    const char *err=pe_load(file,len,&im); if(err){ vunmap_all(); return err; }
    pe_is64=im.is64; pe_image_base=im.vbase;
    if(im.vm && vmap(im.vbase,(u64)im.phys,im.size)){ return "Cannot map image at its base address"; }
    /* TEB/PEB, stack, iob, cmdline, argv */
    u8 *teb=pa_zalloc(0x2000,4096); u8 *peb=teb+0x1000; u64 stk_sz=1<<20; u8 *stk=pa_zalloc(stk_sz,4096);
    if(!teb||!stk) return "Out of memory";
    u64 top=((u64)stk+stk_sz-512)&~15ULL;
    pe_iob_stride=pe_is64?48:32; pe_iob=pa_zalloc(pe_iob_stride*3,16);
    char cl[200]; ksprintf(cl,"\"%s\"%s%s",path,args&&*args?" ":"",args?args:""); strncpyz(pe_cmd,cl,200);
    pe_cmdw=pa_zalloc(402,16); for(int i=0;cl[i]&&i<199;i++) pe_cmdw[i]=cl[i];
    memset(pe_envw,0,8);
    { int ps=pe_ptr(); char *argvs[16]; int n=0; static char abuf[200]; strncpyz(abuf,args?args:"",200);
      argvs[n++]=pe_path; char *p=abuf; while(*p&&n<14){ while(*p==' ') p++; if(!*p) break; argvs[n++]=p; while(*p&&*p!=' ') p++; if(*p) *p++=0; }
      u8 *arr=pa_zalloc((n+2)*8+16,16); for(int i=0;i<n;i++){ if(ps==8) ((u64*)arr)[i]=(u64)argvs[i]; else ((u32*)arr)[i]=(u32)(u64)argvs[i]; }
      pe_argc=n; pe_argv=(u64)arr; u8 *env=pa_zalloc(16,16); pe_envp=(u64)env; }
    if(pe_is64){
        *(u64*)(teb+8)=top+512; *(u64*)(teb+0x10)=(u64)stk; *(u64*)(teb+0x30)=(u64)teb; *(u64*)(teb+0x58)=pe_stdin_line[0]; *(u64*)(teb+0x60)=(u64)peb;
        *(u64*)(peb+0x10)=im.vbase; *(u32*)(peb+0x118)=10;
        wrmsr_(0xC0000101,(u64)teb);
    } else {
        *(u32*)(teb)=0xFFFFFFFF; *(u32*)(teb+4)=(u32)(top+512); *(u32*)(teb+8)=(u32)(u64)stk; *(u32*)(teb+0x18)=(u32)(u64)teb; *(u32*)(teb+0x2C)=(u32)pe_stdin_line[0]; *(u32*)(teb+0x30)=(u32)(u64)peb;
        *(u32*)(peb+8)=(u32)im.vbase; *(u32*)(peb+0xA4)=10;
        u64 b=(u64)teb; gdt64[4]=0xFFFULL|((b&0xFFFFFF)<<16)|(0x92ULL<<40)|(0x4ULL<<52)|((b>>24)<<56);
    }
    pe_cpu_init(); pe_last_refresh=rdtsc(); pe_inlen=pe_inpos=0;
    pe_run_flag=1;
    u64 r= pe_is64 ? pe_enter64(im.entry,top,im.vbase) : pe_cb32(im.entry,NULL,0,top);
    pe_run_flag=0; vunmap_all();
    if(pe_crashed){ return pe_msg; }
    *code=(u32)r; return NULL;
}
