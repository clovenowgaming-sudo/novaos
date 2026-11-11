#pragma once
#include "core.h"
#include "input.h"
#include "fs.h"

#define ACCENT 0x0078D4
#define OWN_UI 100
enum { T_TERM=1, T_NOTE, T_EXPL, T_BROW };

char username[40]="User", password[40]="";
static int click, dbl, owner, curown=OWN_UI;
static u64 last_click_tsc; static int last_click_x, last_click_y;

/* modal message box */
static int mb_on; static char mb_title[48], mb_text[240];
static void msgbox(const char *t, const char *s){ strncpyz(mb_title,t,48); strncpyz(mb_text,s,240); mb_on=1; }

static int hover(int x,int y,int w,int h){ return mx>=x&&my>=y&&mx<x+w&&my<y+h; }
static int clicked(int x,int y,int w,int h){ return click && owner==curown && hover(x,y,w,h); }
static int btn(int x,int y,int w,int h,const char *label,int style){
    int hv=hover(x,y,w,h) && (owner==curown || !click);
    u32 c = style==1 ? (hv?0x1A8AE6:ACCENT) : (hv?0x4A4A4A:0x3A3A3A);
    rrect(x,y,w,h,4,c,255);
    textc(x+w/2,y+(h-16)/2,label,0xFFFFFF,1);
    return clicked(x,y,w,h);
}

/* ---- window manager ---- */
typedef struct { int used,type,x,y,w,h,min,max,sx,sy,sw,sh; char title[48]; void *st; } Win;
static Win wins[8]; static int zord[8], nz;
static int drag=-1, dragx, dragy;

static int win_open(int type,const char *title,int w,int h,void *st){
    int id=-1; for(int i=0;i<8;i++) if(!wins[i].used){ id=i; break; }
    if(id<0){ msgbox("Too many windows","Close a window before opening another."); return -1; }
    if(w>W-20) w=W-20; if(h>H-48-20) h=H-48-20;
    Win *k=&wins[id]; memset(k,0,sizeof *k);
    k->used=1; k->type=type; k->w=w; k->h=h; k->st=st;
    k->x=60+(nz%6)*34; k->y=40+(nz%6)*30;
    if(k->x+w>W) k->x=W-w; if(k->y+h>H-48) k->y=H-48-h; if(k->x<0)k->x=0; if(k->y<0)k->y=0;
    strncpyz(k->title,title,48);
    zord[nz++]=id;
    return id;
}
static void win_focus(int id){
    int p=-1; for(int i=0;i<nz;i++) if(zord[i]==id) p=i;
    if(p<0) return;
    for(int i=p;i<nz-1;i++) zord[i]=zord[i+1];
    zord[nz-1]=id; wins[id].min=0;
}
static void win_close(int id){
    int p=-1; for(int i=0;i<nz;i++) if(zord[i]==id) p=i;
    if(p>=0){ for(int i=p;i<nz-1;i++) zord[i]=zord[i+1]; nz--; }
    wins[id].used=0;
}
static int win_active(void){ for(int i=nz-1;i>=0;i--) if(!wins[zord[i]].min) return zord[i]; return -1; }
static void win_toggle_max(int id){
    Win *w=&wins[id];
    if(!w->max){ w->sx=w->x; w->sy=w->y; w->sw=w->w; w->sh=w->h; w->x=0; w->y=0; w->w=W; w->h=H-48; w->max=1; }
    else { w->x=w->sx; w->y=w->sy; w->w=w->sw; w->h=w->sh; w->max=0; }
}

/* ---- icons ---- */
static void icon_folder(int x,int y,int s){
    rrect(x,y+s/6,s*2/5,s/4,2,0xE8A800,255);
    rrect(x,y+s/4,s,s*3/4-s/8,3,0xFFC83D,255);
}
static void icon_file(int x,int y,int s){
    rrect(x+s/6,y,s*2/3,s,3,0xF0F0F0,255);
    rect(x+s/4,y+s/4,s/2,2,0x909090); rect(x+s/4,y+s/2-2,s/2,2,0x909090); rect(x+s/4,y+s*3/4-4,s/3,2,0x909090);
}
static void app_icon(int type,int x,int y,int s){
    switch(type){
    case T_TERM: rrect(x,y+2,s,s-4,4,0x1E1E1E,255); frame(x,y+2,s,s-4,0x707070);
        text(x+s/2-10,y+s/2-8,">_",0x4EE07A,1); break;
    case T_NOTE: rrect(x+3,y,s-6,s,3,0xFFFFFF,255); rect(x+3,y,s-6,7,0x2B88D8);
        for(int i=0;i<3;i++) rect(x+8,y+12+i*5,s-16,2,0x8A8A8A); break;
    case T_EXPL: icon_folder(x,y+2,s); break;
    case T_BROW: circle(x+s/2,y+s/2,s/2,0x1C7FE0); circle(x+s/2,y+s/2,s/2-3,0x35B6F0);
        rect(x+3,y+s/2-1,s-6,2,0xFFFFFF); rect(x+s/2-1,y+3,2,s-6,0xFFFFFF); circle(x+s/2,y+s/2,s/4,0x1C7FE0); circle(x+s/2,y+s/2,s/4-2,0x35B6F0); break;
    }
}
static void logo(int x,int y,int s,u32 c){
    int g=s/10+1, q=(s-g)/2;
    rect(x,y,q,q,c); rect(x+q+g,y,q,q,c); rect(x,y+q+g,q,q,c); rect(x+q+g,y+q+g,q,q,c);
}

/* ===================== Terminal ===================== */
#define TL 300
#define TC 110
typedef struct { char l[TL][TC]; int n, cx, il, cwd, scroll; char in[160]; } Term;

static int open_notepad(int node);
static int open_terminal(void);
static int open_browser(const char *url);
static int open_explorer(int dir);
static int want_lock, want_power; /* 1=reboot 2=shutdown */

static void t_nl(Term *t){
    if(t->n<TL){ t->n++; }
    else { memmove(t->l[0],t->l[1],(TL-1)*TC); }
    t->l[t->n-1][0]=0; t->cx=0;
}
static void t_puts(Term *t,const char *s){
    for(;*s;s++){
        if(*s=='\n') t_nl(t);
        else { if(t->cx>=TC-1) t_nl(t); t->l[t->n-1][t->cx++]=*s; t->l[t->n-1][t->cx]=0; }
    }
    t->scroll=0;
}
#define tp(t,...) do{ char _b[512]; ksprintf(_b,__VA_ARGS__); t_puts(t,_b); }while(0)

#include "pe.h"
static Term *pe_term;
static void pe_out_cb(const char *s,int n){ if(pe_term) t_puts(pe_term,s); }
static void pe_msg_cb(const char *t,const char *s,int show){ if(show){ strncpyz(mb_title,t,48); strncpyz(mb_text,s,240); mb_on=1; } else mb_on=0; }
void pe_refresh_main(void);
static int term_run_node(Term *t,int n,const char *args){
    char path[200], w[220]; fs_path(n,path); ksprintf(w,"C:%s",path); for(char *q=w;*q;q++) if(*q=='/') *q='\\';
    pe_term=t; pe_out_fn=pe_out_cb; pe_msg_fn=pe_msg_cb; pe_refresh_fn=pe_refresh_main;
    u32 code=0; const char *e=pe_run((const u8*)fs[n].data,fs[n].size,w,args,&code);
    if(e) tp(t,"\n[%s]\n",e); else tp(t,"\n[Program exited with code %d]\n",(int)code);
    pe_term=NULL; return 1;
}
static int term_try_exe(Term *t,const char *name,int argc,char **argv,int first){
    char nm[80]; int n=fs_resolve(t->cwd,name); int hasexe=ends_ci(name,".exe");
    if((n<0||fs[n].dir)&&!hasexe){ ksprintf(nm,"%s.exe",name); n=fs_resolve(t->cwd,nm); }
    if(n<0||fs[n].dir){ int pg=fs_find(0,"Programs"); if(pg>=0){ n=fs_find(pg,name); if(n<0&&!hasexe){ ksprintf(nm,"%s.exe",name); n=fs_find(pg,nm); } } }
    if(n<=0||fs[n].dir||!ends_ci(fs[n].name,".exe")) return 0;
    char args[160]; args[0]=0; for(int i=first;i<argc;i++){ if(i>first) strcat(args," "); strcat(args,argv[i]); }
    return term_run_node(t,n,args);
}

static void t_prompt(Term *t,char *out){ char p[160]; fs_path(t->cwd,p); ksprintf(out,"%s@nova:%s> ",username,p); }
static void term_exec(Term *t,char *line){
    char *argv[8]; int argc=0; char *p=line;
    while(*p&&argc<8){ while(*p==' ')p++; if(!*p)break; argv[argc++]=p; while(*p&&*p!=' ')p++; if(*p)*p++=0; }
    if(!argc) return;
    char *c=argv[0];
    if(!strcasecmp(c,"help")){
        t_puts(t,"Commands:\n  help  clear  echo  ver  date  time  whoami  hostname  mem  neofetch\n"
                 "  pwd  ls [dir]  cd <dir>  cat <file>  mkdir <dir>  touch <file>  rm <path>\n"
                 "  write <file> <text>  edit [file]  explorer  browser [url]\n  run <file.exe> [args]   (or just type the .exe name; Programs folder is searched too)\n"
                 "  ping  ipconfig  wget  curl  download  (network - not available yet)\n"
                 "  lock  reboot  shutdown  exit\n");
    } else if(!strcasecmp(c,"clear")||!strcasecmp(c,"cls")){ t->n=1; t->l[0][0]=0; t->cx=0; }
    else if(!strcasecmp(c,"echo")){ for(int i=1;i<argc;i++){ t_puts(t,argv[i]); t_puts(t,i<argc-1?" ":""); } t_puts(t,"\n"); }
    else if(!strcasecmp(c,"ver")) tp(t,"Nova OS [Version 0.1.0] (x86_64)\n");
    else if(!strcasecmp(c,"date")){ rtc_get(); tp(t,"%s %d, %d\n",mon_names[rt_mo<13?rt_mo:0],rt_d,rt_y); }
    else if(!strcasecmp(c,"time")){ rtc_get(); tp(t,"%02d:%02d:%02d\n",rt_h,rt_m,rt_s); }
    else if(!strcasecmp(c,"whoami")) tp(t,"%s\n",username);
    else if(!strcasecmp(c,"hostname")) tp(t,"NOVA-PC\n");
    else if(!strcasecmp(c,"pwd")){ char b[160]; fs_path(t->cwd,b); tp(t,"%s\n",b); }
    else if(!strcasecmp(c,"mem")) tp(t,"Heap: %u KB used of %u KB\n",(u32)(heap_pos>>10),(u32)(sizeof heap>>10));
    else if(!strcasecmp(c,"neofetch")){
        rtc_get();
        tp(t,"      ____        %s@NOVA-PC\n",username);
        t_puts(t,"     /    \\       ------------\n");
        tp(t,"    | NOVA |      OS: Nova OS 0.1 x86_64\n");
        tp(t,"     \\____/       Kernel: nova-kernel (C + asm)\n");
        tp(t,"                  Resolution: %dx%d\n",W,H);
        tp(t,"                  Shell: nsh\n");
    }
    else if(!strcasecmp(c,"ls")||!strcasecmp(c,"dir")){
        int d = argc>1 ? fs_resolve(t->cwd,argv[1]) : t->cwd;
        if(d<0||!fs[d].dir){ tp(t,"Cannot find directory.\n"); return; }
        int ch[64], n=fs_children(d,ch,64);
        for(int i=0;i<n;i++){ if(fs[ch[i]].dir) tp(t,"  <DIR>   %s\n",fs[ch[i]].name); else tp(t,"%7d   %s\n",fs[ch[i]].size,fs[ch[i]].name); }
        if(!n) t_puts(t,"  (empty)\n");
    }
    else if(!strcasecmp(c,"cd")){
        if(argc<2){ t->cwd=0; return; }
        int d=fs_resolve(t->cwd,argv[1]);
        if(d<0||!fs[d].dir) tp(t,"The system cannot find the path specified.\n"); else t->cwd=d;
    }
    else if(!strcasecmp(c,"cat")||!strcasecmp(c,"type")){
        if(argc<2){ tp(t,"Usage: cat <file>\n"); return; }
        int f=fs_resolve(t->cwd,argv[1]);
        if(f<0||fs[f].dir){ tp(t,"File not found.\n"); return; }
        t_puts(t,fs[f].data?fs[f].data:""); t_puts(t,"\n");
    }
    else if(!strcasecmp(c,"mkdir")){
        if(argc<2){ tp(t,"Usage: mkdir <name>\n"); return; }
        if(fs_find(t->cwd,argv[1])>=0) tp(t,"Already exists.\n"); else if(fs_new(t->cwd,argv[1],1)<0) tp(t,"Disk full.\n");
    }
    else if(!strcasecmp(c,"touch")){
        if(argc<2){ tp(t,"Usage: touch <name>\n"); return; }
        if(fs_find(t->cwd,argv[1])<0 && fs_mkfile(t->cwd,argv[1],"")<0) tp(t,"Disk full.\n");
    }
    else if(!strcasecmp(c,"rm")||!strcasecmp(c,"del")){
        if(argc<2){ tp(t,"Usage: rm <path>\n"); return; }
        int f=fs_resolve(t->cwd,argv[1]);
        if(f<=0) tp(t,"File not found.\n"); else { if(f==t->cwd) t->cwd=fs[f].parent; fs_del(f); }
    }
    else if(!strcasecmp(c,"write")){
        if(argc<3){ tp(t,"Usage: write <file> <text>\n"); return; }
        char buf[400]; buf[0]=0;
        for(int i=2;i<argc;i++){ strcat(buf,argv[i]); if(i<argc-1) strcat(buf," "); }
        int f=fs_find(t->cwd,argv[1]); if(f<0) f=fs_mkfile(t->cwd,argv[1],"");
        if(f>=0) fs_write(f,buf,strlen(buf));
    }
    else if(!strcasecmp(c,"edit")||!strcasecmp(c,"notepad")){
        int f=-1;
        if(argc>1){ f=fs_resolve(t->cwd,argv[1]); if(f<0) f=fs_mkfile(t->cwd,argv[1],""); }
        open_notepad(f);
    }
    else if(!strcasecmp(c,"explorer")) open_explorer(t->cwd);
    else if(!strcasecmp(c,"browser")||!strcasecmp(c,"web")) open_browser(argc>1?argv[1]:"nova://home");
    else if(!strcasecmp(c,"ping")||!strcasecmp(c,"ipconfig")||!strcasecmp(c,"wget")||!strcasecmp(c,"curl")||!strcasecmp(c,"download")||!strcasecmp(c,"nslookup"))
        tp(t,"%s: no network adapter driver is loaded yet.\nThe network stack (Ethernet, TCP/IP, DNS, TLS) is planned for the next build.\n",c);
    else if(!strcasecmp(c,"lock")) want_lock=1;
    else if(!strcasecmp(c,"reboot")) want_power=1;
    else if(!strcasecmp(c,"shutdown")) want_power=2;
    else if(!strcasecmp(c,"exit")) t->il=-1;
    else if(!strcasecmp(c,"run")||!strcasecmp(c,"start")){ if(argc<2) tp(t,"Usage: run <file.exe> [args]\n"); else if(!term_try_exe(t,argv[1],argc,argv,2)) tp(t,"Cannot find %s\n",argv[1]); }
    else if(term_try_exe(t,c,argc,argv,1)){}
    else tp(t,"'%s' is not recognized as a command. Type help.\n",c);
}

static void term_key(int id,int k){
    Term *t=wins[id].st;
    if(k==K_PGUP){ t->scroll+=8; if(t->scroll>t->n-2) t->scroll=t->n-2; if(t->scroll<0)t->scroll=0; return; }
    if(k==K_PGDN){ t->scroll-=8; if(t->scroll<0) t->scroll=0; return; }
    if(k==K_CTRL('l')){ t->n=1; t->l[0][0]=0; t->cx=0; return; }
    t->scroll=0;
    if(k=='\n'){
        char pr[200]; t_prompt(t,pr); t_puts(t,pr); t_puts(t,t->in); t_puts(t,"\n");
        char line[160]; strcpy(line,t->in); t->in[0]=0; t->il=0;
        term_exec(t,line);
        if(t->il==-1){ win_close(id); return; }
        if(want_lock||want_power) return;
    } else if(k=='\b'){ if(t->il>0) t->in[--t->il]=0; }
    else if(k>=32&&k<127&&t->il<150){ t->in[t->il++]=k; t->in[t->il]=0; }
}
static void term_draw(int id,int cx,int cy,int cw,int ch){
    Term *t=wins[id].st;
    rect(cx,cy,cw,ch,0x0C0C0C);
    int rows=(ch-8)/16, end=t->n-1-t->scroll, start=end-rows+1;
    if(start<0) start=0;
    for(int li=start;li<=end && li-start<rows;li++){
        int y=cy+4+(li-start)*16;
        if(li==t->n-1){
            char pr[200]; t_prompt(t,pr);
            text(cx+6,y,pr,0x4EE07A,1);
            int x=cx+6+textw(pr,1);
            text(x,y,t->in,0xE0E0E0,1);
            if(rt_s&1) rect(x+textw(t->in,1),y+13,8,2,0xE0E0E0);
        } else text(cx+6,y,t->l[li],0xCCCCCC,1);
    }
}

/* ===================== Notepad ===================== */
#define NCAP 16384
typedef struct { char *buf; int len, cur, top, node, modified; char msg[40]; } Note;
static int np_ls(Note *n,int p){ while(p>0&&n->buf[p-1]!='\n') p--; return p; }
static int np_le(Note *n,int p){ while(p<n->len&&n->buf[p]!='\n') p++; return p; }
static void np_title(int id){
    Note *n=wins[id].st; char nm[32]; strcpy(nm, n->node>0? fs[n->node].name : "Untitled");
    ksprintf(wins[id].title,"%s%s - Notepad",n->modified?"*":"",nm);
}
static void np_save(int id){
    Note *n=wins[id].st;
    if(n->node<=0){
        char nm[32]; int docs=fs_find(0,"Documents"); if(docs<0) docs=0;
        fs_unique(docs,"Untitled",".txt",nm);
        n->node=fs_mkfile(docs,nm,"");
        if(n->node<0){ n->node=0; strcpy(n->msg,"Disk full"); return; }
    }
    fs_write(n->node,n->buf,n->len); n->modified=0; strcpy(n->msg,"Saved"); np_title(id);
}
static void np_ins(int id,char c){
    Note *n=wins[id].st; if(n->len>=NCAP-2) return;
    memmove(n->buf+n->cur+1,n->buf+n->cur,n->len-n->cur); n->buf[n->cur++]=c; n->len++; n->modified=1; n->msg[0]=0; np_title(id);
}
static void np_new(int id){
    Note *n=wins[id].st; n->len=0; n->cur=0; n->top=0; n->node=-1; n->modified=0; n->buf[0]=0; n->msg[0]=0; np_title(id);
}
static void np_key(int id,int k){
    Note *n=wins[id].st;
    if(k==K_CTRL('s')){ np_save(id); return; }
    if(k==K_CTRL('n')){ np_new(id); return; }
    if(k==K_LEFT){ if(n->cur>0) n->cur--; }
    else if(k==K_RIGHT){ if(n->cur<n->len) n->cur++; }
    else if(k==K_HOME) n->cur=np_ls(n,n->cur);
    else if(k==K_END) n->cur=np_le(n,n->cur);
    else if(k==K_UP){ int ls=np_ls(n,n->cur), col=n->cur-ls; if(ls>0){ int pls=np_ls(n,ls-1), pl=ls-1-pls; n->cur=pls+(col<pl?col:pl); } }
    else if(k==K_DOWN){ int le=np_le(n,n->cur); if(le<n->len){ int col=n->cur-np_ls(n,n->cur), nls=le+1, nle=np_le(n,nls); n->cur=nls+(col<nle-nls?col:nle-nls); } }
    else if(k==K_PGUP){ for(int i=0;i<10;i++) np_key(id,K_UP); }
    else if(k==K_PGDN){ for(int i=0;i<10;i++) np_key(id,K_DOWN); }
    else if(k=='\b'){ if(n->cur>0){ memmove(n->buf+n->cur-1,n->buf+n->cur,n->len-n->cur); n->cur--; n->len--; n->buf[n->len]=0; n->modified=1; np_title(id); } }
    else if(k==K_DEL){ if(n->cur<n->len){ memmove(n->buf+n->cur,n->buf+n->cur+1,n->len-n->cur-1); n->len--; n->buf[n->len]=0; n->modified=1; np_title(id); } }
    else if(k=='\t'){ for(int i=0;i<4;i++) np_ins(id,' '); }
    else if(k=='\n'||(k>=32&&k<127)) np_ins(id,k);
    n->buf[n->len]=0;
}
static void note_draw(int id,int cx,int cy,int cw,int ch){
    Note *n=wins[id].st;
    rect(cx,cy,cw,ch,0x1E1E1E);
    rect(cx,cy,cw,34,0x2B2B2B);
    if(btn(cx+6,cy+4,60,26,"New",0)) np_new(id);
    if(btn(cx+72,cy+4,60,26,"Save",1)) np_save(id);
    text(cx+144,cy+9,"Ctrl+S save   Ctrl+N new",0x909090,1);
    int ax=cx+8, ay=cy+40, aw=cw-16, ah=ch-34-26-10, rows=ah/16, cols=aw/8;
    /* cursor line */
    int cl=0; for(int i=0;i<n->cur;i++) if(n->buf[i]=='\n') cl++;
    int col=n->cur-np_ls(n,n->cur);
    if(cl<n->top) n->top=cl; if(cl>=n->top+rows) n->top=cl-rows+1;
    int p=0; for(int l=0;l<n->top&&p<n->len;l++){ p=np_le(n,p)+1; }
    for(int r=0;r<rows && p<=n->len;r++){
        int le=np_le(n,p); char ln[200]; int m=le-p; if(m>cols) m=cols; if(m>190) m=190;
        memcpy(ln,n->buf+p,m); ln[m]=0;
        text(ax,ay+r*16,ln,0xE6E6E6,1);
        if(n->top+r==cl && (rt_s&1)) rect(ax+col*8,ay+r*16,2,16,0xFFFFFF);
        if(le>=n->len) break;
        p=le+1;
    }
    int sy=cy+ch-24; rect(cx,sy,cw,24,0x2B2B2B);
    char st[100]; ksprintf(st,"Ln %d, Col %d    %d chars    %s",cl+1,col+1,n->len,n->msg);
    text(cx+8,sy+4,st,0xB0B0B0,1);
}

/* ===================== Explorer ===================== */
typedef struct { int cwd, sel, scroll; } Expl;
static void ex_open(int id,int node){
    Expl *e=wins[id].st;
    if(fs[node].dir){ e->cwd=node; e->sel=-1; e->scroll=0; return; }
    char p[200]; fs_path(node,p);
    if(ends_ci(fs[node].name,".html")||ends_ci(fs[node].name,".htm")){ char u[220]; ksprintf(u,"file://%s",p); open_browser(u); }
    else if(ends_ci(fs[node].name,".exe")){ int tid=open_terminal(); if(tid>=0){ Term *tt=wins[tid].st; tp(tt,"> %s\n",fs[node].name); term_run_node(tt,node,""); } }
    else open_notepad(node);
}
static void expl_key(int id,int k){
    Expl *e=wins[id].st; int ch[64], n=fs_children(e->cwd,ch,64);
    int pos=-1; for(int i=0;i<n;i++) if(ch[i]==e->sel) pos=i;
    if(k==K_DOWN){ if(pos<n-1) e->sel=ch[pos+1]; }
    else if(k==K_UP){ if(pos>0) e->sel=ch[pos-1]; else if(pos<0&&n) e->sel=ch[0]; }
    else if(k=='\n'){ if(e->sel>0) ex_open(id,e->sel); }
    else if(k=='\b'){ if(e->cwd>0){ e->sel=e->cwd; e->cwd=fs[e->cwd].parent; } }
    else if(k==K_DEL){ if(e->sel>0){ fs_del(e->sel); e->sel=-1; } }
}
static void expl_draw(int id,int cx,int cy,int cw,int ch){
    Expl *e=wins[id].st;
    rect(cx,cy,cw,ch,0x272727);
    rect(cx,cy,cw,38,0x2F2F2F);
    if(btn(cx+6,cy+5,32,28,"<",0)){ if(e->cwd>0){ e->sel=e->cwd; e->cwd=fs[e->cwd].parent; } }
    char path[200]; fs_path(e->cwd,path);
    int pw=cw-44-190; rrect(cx+44,cy+5,pw,28,4,0x1F1F1F,255);
    text(cx+54,cy+11,path,0xE0E0E0,1);
    if(btn(cx+cw-140,cy+5,64,28,"Folder",0)){ char nm[32]; fs_unique(e->cwd,"New folder","",nm); e->sel=fs_new(e->cwd,nm,1); }
    if(btn(cx+cw-72,cy+5,64,28,"File",0)){ char nm[32]; fs_unique(e->cwd,"New file",".txt",nm); e->sel=fs_mkfile(e->cwd,nm,""); }
    /* sidebar */
    const char *sb[]={"Desktop","Documents","Downloads","System"};
    int sy=cy+46;
    text(cx+10,sy,"Quick access",0x909090,1); sy+=22;
    for(int i=0;i<4;i++){
        int nd=fs_find(0,sb[i]);
        int hv=hover(cx+4,sy,128,26);
        if(hv) rrect(cx+4,sy,128,26,4,0x383838,255);
        if(nd>=0 && e->cwd==nd) rrect(cx+4,sy,128,26,4,0x3A3A3A,255);
        icon_folder(cx+10,sy+5,16); text(cx+34,sy+5,sb[i],0xE0E0E0,1);
        if(clicked(cx+4,sy,128,26) && nd>=0){ e->cwd=nd; e->sel=-1; }
        sy+=28;
    }
    vline(cx+138,cy+38,ch-38,0x3A3A3A);
    int lx=cx+140, lw=cw-140, ly=cy+40;
    text(lx+40,ly+2,"Name",0x909090,1); text(lx+lw-100,ly+2,"Size",0x909090,1);
    int items[64], n=fs_children(e->cwd,items,64), y=ly+24;
    for(int i=0;i<n;i++,y+=28){
        if(y+28>cy+ch) break;
        int node=items[i];
        int hv=hover(lx,y,lw,28);
        if(node==e->sel) rect(lx+4,y,lw-8,28,0x3A4A60); else if(hv) rect(lx+4,y,lw-8,28,0x333333);
        if(fs[node].dir) icon_folder(lx+12,y+5,18); else icon_file(lx+12,y+4,20);
        text(lx+40,y+6,fs[node].name,0xF0F0F0,1);
        char sz[24]; if(fs[node].dir) strcpy(sz,"Folder"); else ksprintf(sz,"%d B",fs[node].size);
        text(lx+lw-100,y+6,sz,0xA0A0A0,1);
        if(clicked(lx,y,lw,28)){ if(dbl && e->sel==node) ex_open(id,node); else e->sel=node; }
    }
    if(!n) text(lx+40,ly+40,"This folder is empty.",0x909090,1);
    char st[60]; ksprintf(st,"%d items",n); text(cx+8,cy+ch-18,st,0x909090,1);
}

/* ===================== Browser ===================== */
typedef struct { int x,y,w,h; char href[80]; } Link;
typedef struct {
    char url[100], edit[100]; const char *html; int scroll, pageh, nlinks, hn;
    char hist[12][100]; Link links[48];
} Brow;
static char errhtml[500];
static void br_go(int id,const char *url,int push){
    Brow *b=wins[id].st; if(!url[0]) return;
    char u[100]; strncpyz(u,url,100);
    int node=-1; b->html=NULL;
    if(starts(u,"nova://")){ char p[140]; ksprintf(p,"/System/www/%s.html",u+7); node=fs_resolve(0,p); }
    else if(starts(u,"file://")) node=fs_resolve(0,u+7);
    else if(starts(u,"http://")||starts(u,"https://")||strlen(u)>3){
        ksprintf(errhtml,"<h1>Can't reach this page</h1><p>%s</p><p>Nova OS does not have a network driver yet, so http:// and https:// pages cannot be loaded in this build. The Ethernet driver, TCP/IP stack and TLS are next on the roadmap.</p><p><a href=\"nova://home\">Go to home page</a></p>",u);
        b->html=errhtml;
    }
    if(!b->html){
        if(node>=0 && !fs[node].dir && fs[node].data) b->html=fs[node].data;
        else { ksprintf(errhtml,"<h1>Page not found</h1><p>%s</p><p><a href=\"nova://home\">Go to home page</a></p>",u); b->html=errhtml; }
    }
    if(push && b->url[0]){ if(b->hn==12){ memmove(b->hist[0],b->hist[1],11*100); b->hn=11; } strcpy(b->hist[b->hn++],b->url); }
    strcpy(b->url,u); strcpy(b->edit,u); b->scroll=0;
    ksprintf(wins[id].title,"%s - Browser",u);
}
static void br_key(int id,int k){
    Brow *b=wins[id].st; int l=strlen(b->edit);
    if(k=='\n') br_go(id,b->edit,1);
    else if(k=='\b'){ if(l>0) b->edit[l-1]=0; }
    else if(k==27) strcpy(b->edit,b->url);
    else if(k==K_DOWN) b->scroll+=40;
    else if(k==K_UP) b->scroll-=40;
    else if(k==K_PGDN) b->scroll+=300;
    else if(k==K_PGUP) b->scroll-=300;
    else if(k>=32&&k<127&&l<96){ b->edit[l]=k; b->edit[l+1]=0; }
    if(b->scroll<0) b->scroll=0;
}
static void attr_href(const char *tag,char *out){
    out[0]=0;
    for(const char *p=tag;*p;p++) if(starts(p,"href=\"")){ p+=6; int n=0; while(*p&&*p!='"'&&n<79) out[n++]=*p++; out[n]=0; return; }
}
#define NEWLINE(gap) do{ if(x>left+ind||(gap)){ py+=16*sc+4+(gap); } x=left+ind; pend=0; }while(0)
static void br_render(int id,int cx,int cy,int cw,int ch){
    Brow *b=wins[id].st; const char *h=b->html;
    rect(cx,cy,cw,ch,0xFAFAFA);
    clip_set(cx,cy,cw,ch);
    int left=cx+24, right=cx+cw-24, x=left, py=16, sc=1, link=0, pend=0, ind=0;
    u32 col=0x202020; char href[80]; href[0]=0; b->nlinks=0;
    while(h && *h){
        if(*h=='<'){
            char tag[120]; int n=0; h++;
            while(*h && *h!='>' && n<119) tag[n++]=*h++;
            tag[n]=0; if(*h=='>') h++;
            int closing=(tag[0]=='/'); char *tn=tag+(closing?1:0);
            char name[12]; int m=0; while(tn[m]&&tn[m]!=' '&&m<11){ name[m]=lower(tn[m]); m++; } name[m]=0;
            if(!strcmp(name,"h1")){ if(!closing){ NEWLINE(10); sc=2; col=0x0B5CAD; } else { NEWLINE(0); sc=1; col=0x202020; py+=8; } }
            else if(!strcmp(name,"h2")){ if(!closing){ NEWLINE(8); col=0x0B5CAD; } else { NEWLINE(0); col=0x202020; py+=4; } }
            else if(!strcmp(name,"p")){ NEWLINE(closing?0:6); }
            else if(!strcmp(name,"br")){ NEWLINE(0); }
            else if(!strcmp(name,"li")){
                if(!closing){ NEWLINE(0); ind=16; x=left+ind; text(left,cy+py-b->scroll,"*",0x0B5CAD,1); }
                else { NEWLINE(0); ind=0; x=left; }
            }
            else if(!strcmp(name,"hr")){ NEWLINE(0); rect(left,cy+py-b->scroll+4,right-left,1,0xC8C8C8); py+=14; }
            else if(!strcmp(name,"a")){ if(!closing){ link=1; attr_href(tn,href); } else link=0; }
            continue;
        }
        if(*h==' '||*h=='\n'||*h=='\t'||*h=='\r'){ pend=1; h++; continue; }
        char wd[64]; int n=0;
        while(*h && *h!='<' && *h!=' ' && *h!='\n' && *h!='\t' && *h!='\r' && n<62){
            if(*h=='&'){
                if(starts(h,"&amp;")){ wd[n++]='&'; h+=5; continue; }
                if(starts(h,"&lt;")){ wd[n++]='<'; h+=4; continue; }
                if(starts(h,"&gt;")){ wd[n++]='>'; h+=4; continue; }
                if(starts(h,"&nbsp;")){ wd[n++]=' '; h+=6; continue; }
            }
            wd[n++]=*h++;
        }
        wd[n]=0; if(!n){ h++; continue; }
        int ww=n*8*sc, sp=(pend&&x>left+ind)?8*sc:0;
        if(x+sp+ww>right && x>left+ind){ py+=16*sc+4; x=left+ind; sp=0; }
        x+=sp; pend=0;
        int yy=cy+py-b->scroll;
        text(x,yy,wd,link?0x0B5CAD:col,sc);
        if(link){
            rect(x,yy+16*sc-2,ww,1,0x0B5CAD);
            if(b->nlinks<48){ Link *L=&b->links[b->nlinks++]; L->x=x; L->y=yy; L->w=ww; L->h=16*sc; strcpy(L->href,href); }
        }
        x+=ww;
    }
    b->pageh=py+40;
    int mxs=b->pageh-ch; if(mxs<0) mxs=0; if(b->scroll>mxs) b->scroll=mxs;
    clip_reset();
}
static void brow_draw(int id,int cx,int cy,int cw,int ch){
    Brow *b=wins[id].st;
    if(btn(cx+6,cy+5,32,28,"<",0)){ if(b->hn>0){ char u[100]; strcpy(u,b->hist[--b->hn]); br_go(id,u,0); } }
    if(btn(cx+42,cy+5,52,28,"Home",0)) br_go(id,"nova://home",1);
    rrect(cx+100,cy+5,cw-108,28,14,0x1F1F1F,255);
    text(cx+114,cy+11,b->edit,0xF0F0F0,1);
    if(rt_s&1) rect(cx+114+textw(b->edit,1),cy+10,2,18,0xF0F0F0);
    b=wins[id].st;
    br_render(id,cx,cy+38,cw,ch-38);
    for(int i=0;i<b->nlinks;i++) if(clicked(b->links[i].x,b->links[i].y,b->links[i].w,b->links[i].h)){
        char hr[80]; strcpy(hr,b->links[i].href); br_go(id,hr,1); break;
    }
}

/* ===================== open apps / window frame ===================== */
static int open_terminal(void){
    Term *t=kmalloc(sizeof(Term)); if(!t) return -1; memset(t,0,sizeof *t); t->n=1;
    t->cwd=fs_find(0,"Documents"); if(t->cwd<0) t->cwd=0;
    tp(t,"Nova OS [Version 0.1.0]\nType 'help' to list commands.\n\n");
    return win_open(T_TERM,"Terminal",660,400,t);
}
static int open_notepad(int node){
    Note *n=kmalloc(sizeof(Note)); if(!n) return -1; memset(n,0,sizeof *n);
    n->buf=kmalloc(NCAP); if(!n->buf) return -1; n->buf[0]=0; n->node=-1;
    if(node>0 && !fs[node].dir){ n->node=node; n->len=fs[node].size<NCAP-2?fs[node].size:NCAP-2; if(n->len>0) memcpy(n->buf,fs[node].data,n->len); n->buf[n->len]=0; }
    int id=win_open(T_NOTE,"Notepad",660,440,n); if(id>=0) np_title(id);
    return id;
}
static int open_explorer(int dir){
    Expl *e=kmalloc(sizeof(Expl)); if(!e) return -1; e->sel=-1; e->scroll=0;
    e->cwd = dir>=0 ? dir : fs_find(0,"Documents"); if(e->cwd<0) e->cwd=0;
    return win_open(T_EXPL,"File Explorer",780,480,e);
}
static int open_browser(const char *url){
    Brow *b=kmalloc(sizeof(Brow)); if(!b) return -1; memset(b,0,sizeof *b);
    int id=win_open(T_BROW,"Browser",880,540,b); if(id>=0) br_go(id,url,0);
    return id;
}
static void open_app(int type){
    if(type==T_TERM) open_terminal(); else if(type==T_NOTE) open_notepad(-1);
    else if(type==T_EXPL) open_explorer(-1); else if(type==T_BROW) open_browser("nova://home");
}

static void win_draw(int id,int active){
    Win *w=&wins[id];
    if(!w->max) for(int i=3;i>=1;i--){
        int sx=w->x-i*3, sy=w->y-i*2+4, sw=w->w+i*6, sh=w->h+i*6;
        int wx=w->x, wy=w->y, wr=w->x+w->w, wb=w->y+w->h;
        if(sy<wy) rectA(sx,sy,sw,wy-sy,0x000000,10);                       /* atas */
        if(sy+sh>wb) rectA(sx,wb,sw,sy+sh-wb,0x000000,10);                  /* bawah */
        int my0=sy>wy?sy:wy, my1=sy+sh<wb?sy+sh:wb;
        if(my1>my0){
            if(sx<wx) rectA(sx,my0,wx-sx,my1-my0,0x000000,10);              /* kiri */
            if(sx+sw>wr) rectA(wr,my0,sx+sw-wr,my1-my0,0x000000,10);        /* kanan */
        }
    }
    int r=w->max?0:8;
    rrect(w->x,w->y,w->w,w->h,r,active?0x2B2B30:0x242428,255);
    app_icon(w->type,w->x+10,w->y+8,16);
    text(w->x+34,w->y+8,w->title,active?0xFFFFFF:0xA0A0A0,1);
    int bx=w->x+w->w-138;
    for(int k=0;k<3;k++){
        int x=bx+k*46;
        if(hover(x,w->y,46,32)){
            if(k==2) rrect2(x,w->y,46,32,r?r:1,0xC42B1C,255,2); else rect(x,w->y,46,32,0x3E3E44);
        }
        int cx=x+23, cy=w->y+16;
        if(k==0) hline(cx-5,cy,10,0xFFFFFF);
        else if(k==1) frame(cx-5,cy-5,10,10,0xFFFFFF);
        else for(int d=-5;d<=5;d++){ px(cx+d,cy+d,0xFFFFFF); px(cx+d,cy-d,0xFFFFFF); }
    }
    int cx=w->x+6, cy=w->y+32, cw=w->w-12, ch=w->h-38;
    clip_set(cx,cy,cw,ch);
    curown=id;
    switch(w->type){
    case T_TERM: term_draw(id,cx,cy,cw,ch); break;
    case T_NOTE: note_draw(id,cx,cy,cw,ch); break;
    case T_EXPL: expl_draw(id,cx,cy,cw,ch); break;
    case T_BROW: rect(cx,cy,cw,38,0x2F2F2F); brow_draw(id,cx,cy,cw,ch); break;
    }
    curown=OWN_UI;
    clip_reset();
}
static void win_draw_all(void){
    int act=win_active();
    for(int i=0;i<nz;i++){ int id=zord[i]; if(!wins[id].min) win_draw(id,id==act); }
}
/* returns window id hit, -2 if the click was consumed by the frame, -1 if no window */
static int win_hit(void){
    for(int i=nz-1;i>=0;i--){
        int id=zord[i]; Win *w=&wins[id];
        if(w->min||!hover(w->x,w->y,w->w,w->h)) continue;
        win_focus(id);
        if(my<w->y+32){
            int bx=w->x+w->w-138;
            if(mx>=bx+92){ win_close(id); return -2; }
            if(mx>=bx+46){ win_toggle_max(id); return -2; }
            if(mx>=bx){ w->min=1; return -2; }
            if(!w->max){ drag=id; dragx=mx-w->x; dragy=my-w->y; }
            return -2;
        }
        return id;
    }
    return -1;
}
static void win_key(int k){
    int id=win_active(); if(id<0) return;
    switch(wins[id].type){
    case T_TERM: term_key(id,k); break;
    case T_NOTE: np_key(id,k); break;
    case T_EXPL: expl_key(id,k); break;
    case T_BROW: br_key(id,k); break;
    }
}
