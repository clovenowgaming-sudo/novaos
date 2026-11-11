#include "core.h"
#include "input.h"
#include "fs.h"
#include "ui.h"
#include "ata.h"
#include "store.h"

#define OWN_TB 101
#define OWN_SM 102
#define OWN_MB 103
#define OWN_DESK 104
enum { ST_INSTALL, ST_LOCK, ST_LOGIN, ST_DESK };
u64 tsc_hz=3000000000ULL;
static int live, installed, disk_sel=-1, confirm_ok, inst_err, inst_mem;
static int state=ST_INSTALL, ipage, lic_ok, inst_pct, sm_open, dsel=-1, login_bad;
static char sm_q[32]; static int sm_ql;
typedef struct { char s[40]; int n; } Field;
static Field f_user, f_pass, f_login; static int ifocus;

static const char *strstr_(const char *h,const char *n){ int l=strlen(n); for(;*h;h++) if(!strncmp(h,n,l)) return h; return NULL; }
static void begin_install(void); static void install_tick(void);
static int wrap_text(int x,int y,int cols,const char *s,u32 c){
    char line[128]; int ll=0; const char *p=s;
    while(*p){
        int wl=0; while(p[wl]&&p[wl]!=' '&&p[wl]!='\n') wl++;
        if(ll && ll+1+wl>cols){ line[ll]=0; text(x,y,line,c,1); y+=20; ll=0; }
        if(ll) line[ll++]=' ';
        for(int i=0;i<wl&&ll<126;i++) line[ll++]=p[i];
        p+=wl;
        if(*p=='\n'){ line[ll]=0; text(x,y,line,c,1); y+=20; ll=0; p++; }
        else if(*p==' ') p++;
    }
    if(ll){ line[ll]=0; text(x,y,line,c,1); y+=20; }
    return y;
}
static int field(int x,int y,int w,Field *f,int mask,int foc){
    rrect(x,y,w,32,4,0x2B2B30,255); if(foc) rect(x+2,y+30,w-4,2,ACCENT);
    char buf[44]; for(int i=0;i<f->n;i++) buf[i]=mask?'*':f->s[i]; buf[f->n]=0;
    text(x+10,y+8,buf,0xFFFFFF,1);
    if(foc&&(rt_s&1)) rect(x+10+f->n*8,y+7,2,18,0xFFFFFF);
    return clicked(x,y,w,32);
}
static void field_key(Field *f,int k){
    if(k=='\b'){ if(f->n) f->s[--f->n]=0; }
    else if(k>=32&&k<127&&f->n<30){ f->s[f->n++]=k; f->s[f->n]=0; }
}
static int weekday(int y,int m,int d){ static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3) y--; return (y+y/4-y/100+y/400+t[m-1]+d)%7; }
static const char *wd_names[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static void backdrop(int dim){ memcpy(bb,wall,(u64)W*H*4); if(dim) rectA(0,0,W,H,0x000000,dim); }

/* ===================== installer ===================== */
static void start_install_done(void){
    strncpyz(username,f_user.s,40); strncpyz(password,f_pass.s,40);
    if(st_dev>=0){ want_power=1; return; }   /* instalasi ke disk: restart ke disk */
    state=ST_LOCK;
}
static void begin_install(void){
    strncpyz(username,f_user.s,40); strncpyz(password,f_pass.s,40);
    inst_pct=0; inst_err=0; inst_mem=0;
    if(disk_sel>=0){ int r=inst_begin(disk_sel); if(r){ inst_err=r; ipage=6; return; } }
    ipage=4;
}
static void install_tick(void){
    if(disk_sel<0){ inst_pct+=2; if(inst_pct>=100){ inst_pct=100; ipage=5; } return; }
    int r=inst_step();
    if(r<0){ inst_err=r; st_dev=-1; ipage=6; return; }
    if(r>=100){ int e=inst_finish(); if(e){ inst_err=e; st_dev=-1; ipage=6; } else { inst_pct=100; ipage=5; } }
    else inst_pct=r;
}
static int next_btn(int x,int y,const char *label,int enabled){
    if(!enabled){ rrect(x,y,120,34,4,0x333333,255); textc(x+60,y+9,label,0x707070,1); return 0; }
    return btn(x,y,120,34,label,1);
}
static void draw_installer(void){
    backdrop(100);
    int pw=820, ph=500; if(pw>W-20) pw=W-20; if(ph>H-20) ph=H-20;
    int px0=(W-pw)/2, py0=(H-ph)/2;
    rrect(px0,py0,pw,ph,10,0x1F1F23,255);
    rrect2(px0,py0,230,ph,10,0x0A4E8C,255,5);
    logo(px0+24,py0+24,28,0xFFFFFF);
    text(px0+62,py0+30,"Nova Setup",0xFFFFFF,1);
    const char *steps[]={"Welcome","License terms","Select drive","Your account","Installing","Finish"};
    for(int i=0;i<6;i++){
        int y=py0+100+i*34;
        if(i==ipage) rrect(px0+12,y-6,206,28,4,0x1A6BB5,255);
        circle(px0+30,y+8,5, i<ipage?0x7FE08A: (i==ipage?0xFFFFFF:0x6C9BC8));
        text(px0+46,y,steps[i],i<=ipage?0xFFFFFF:0xA8C4E0,1);
    }
    int cx=px0+260, cy=py0+36, cw=pw-290;
    int bx=px0+pw-150, by=py0+ph-60;
    curown=OWN_UI;
    switch(ipage){
    case 0:
        text(cx,cy,"Welcome to Nova",0xFFFFFF,3);
        wrap_text(cx,cy+80,(cw)/8,"Nova OS is a from-scratch 64-bit operating system written in C and assembly.\n\nLanguage to install: English (United States)\nTime and currency: English (United States)\nKeyboard: US\n\nClick Install now to begin.",0xD0D0D0);
        if(next_btn(bx,by,"Install now",1)) ipage=1;
        break;
    case 1:
        text(cx,cy,"License terms",0xFFFFFF,2);
        rrect(cx,cy+50,cw,230,6,0x2A2A2F,255);
        wrap_text(cx+12,cy+60,(cw-24)/8,"NOVA OS EVALUATION LICENSE\n\nThis software is provided as is, without warranty of any kind. You may use, copy and modify it for personal and educational purposes. The authors are not liable for any damages arising from its use.\n\nThis is an early development build. Do not use it for anything important.",0xC8C8C8);
        { int ck=lic_ok; rrect(cx,cy+296,22,22,4,ck?ACCENT:0x3A3A3F,255); if(ck) text(cx+7,cy+299,"v",0xFFFFFF,1);
          text(cx+34,cy+299,"I accept the license terms",0xFFFFFF,1);
          if(clicked(cx,cy+292,300,30)) lic_ok=!lic_ok; }
        if(next_btn(bx,by,"Next",lic_ok)) ipage=2;
        break;
    case 2: {
        text(cx,cy,"Where do you want to install Nova?",0xFFFFFF,2);
        int y=cy+50, rh=54;
        for(int r=-1;r<4;r++){
            char l1[90], l2[60];
            if(r<0){ strcpy(l1,"Memory only (live session)"); strcpy(l2,"Nothing is saved. Everything is lost on restart."); }
            else { if(!ata_dev[r].ok) continue; ksprintf(l1,"Disk %d - %s",r,ata_dev[r].model[0]?ata_dev[r].model:"ATA disk");
                   u32 mb=(u32)(ata_dev[r].sectors/2048);
                   if(!live) ksprintf(l2,"%u MB - installer image not available in this boot",mb);
                   else if(ata_dev[r].sectors<NFS_MIN_SECT) ksprintf(l2,"%u MB - too small (need 32 MB)",mb);
                   else ksprintf(l2,"%u MB - ALL DATA WILL BE ERASED",mb); }
            int ok = r<0 || (live && ata_dev[r].sectors>=NFS_MIN_SECT);
            int sel = disk_sel==r;
            rrect(cx,y,cw,rh-6,6,sel?0x2F3A4A:0x2A2A2F,255); if(sel) frame(cx,y,cw,rh-6,ACCENT);
            icon_file(cx+10,y+8,32);
            text(cx+54,y+8,l1,ok?0xFFFFFF:0x808080,1); text(cx+54,y+28,l2,r>=0&&ok?0xFF9A9A:0xA0A0A0,1);
            if(ok && clicked(cx,y,cw,rh-6)){ disk_sel=r; confirm_ok=0; }
            y+=rh;
        }
        if(disk_sel>=0){
            rrect(cx,y+6,22,22,4,confirm_ok?ACCENT:0x3A3A3F,255); if(confirm_ok) text(cx+7,y+9,"v",0xFFFFFF,1);
            text(cx+34,y+9,"I understand this disk will be completely erased",0xFFFFFF,1);
            if(clicked(cx,y+2,cw,30)) confirm_ok=!confirm_ok;
        }
        if(ata_n==0) wrap_text(cx,y+40,cw/8,"No IDE/ATA disk found. On real PCs set SATA mode to IDE/Compatibility. In QEMU add -hda disk.img.",0xB0B0B0);
        if(next_btn(bx,by,"Next",disk_sel<0||confirm_ok)) ipage=3;
        break; }
    case 4: {
        text(cx,cy,"Installing Nova",0xFFFFFF,2);
        const char *msg = disk_sel>=0 ? (inst_pct<95?"Writing system to disk...":"Saving your files...") : (inst_pct<35?"Copying files...": inst_pct<70?"Installing drivers...":"Finishing up...");
        text(cx,cy+80,msg,0xD0D0D0,1);
        rrect(cx,cy+110,cw,16,8,0x333338,255);
        if(inst_pct>0) rrect(cx,cy+110,cw*inst_pct/100<16?16:cw*inst_pct/100,16,8,ACCENT,255);
        char pc[16]; ksprintf(pc,"%d%%",inst_pct); text(cx,cy+136,pc,0xA0A0A0,1);
        text(cx,cy+180,"Do not turn off your computer.",0x909090,1);
        break; }
    case 3:
        text(cx,cy,"Create your account",0xFFFFFF,2);
        text(cx,cy+60,"User name",0xB0B0B0,1);
        if(field(cx,cy+82,320,&f_user,0,ifocus==0)) ifocus=0;
        text(cx,cy+134,"Password (optional)",0xB0B0B0,1);
        if(field(cx,cy+156,320,&f_pass,1,ifocus==1)) ifocus=1;
        text(cx,cy+210,"Press Tab to switch fields.",0x808080,1);
        if(next_btn(bx,by,disk_sel>=0?"Install":"Next",f_user.n>0)) begin_install();
        break;
    case 5:
        text(cx,cy,"All set!",0xFFFFFF,3);
        { char m[200]; if(disk_sel>=0) ksprintf(m,"Welcome, %s. Nova has been installed on the disk.\n\nRemove the installation media (or boot from the disk) and click Restart.",f_user.s);
          else ksprintf(m,"Welcome, %s. Nova is ready (memory only - not saved).\n\nClick Restart to sign in.",f_user.s);
          wrap_text(cx,cy+80,cw/8,m,0xD0D0D0); }
        if(next_btn(bx,by,"Restart",1)) start_install_done();
        break;
    case 6:
        text(cx,cy,"Installation failed",0xFF8A8A,2);
        { char m[120]; ksprintf(m,"Could not write to the disk (error %d). The disk may be read-only, too small, or unsupported.",inst_err); wrap_text(cx,cy+70,cw/8,m,0xD0D0D0); }
        if(next_btn(bx,by,"Back",1)){ ipage=2; inst_err=0; }
        break;
    }
}
static void installer_key(int k){
    if(ipage==3){
        if(k=='\t') ifocus=!ifocus;
        else if(k=='\n'){ if(ifocus==0) ifocus=1; else if(f_user.n>0) begin_install(); }
        else field_key(ifocus?&f_pass:&f_user,k);
    } else if(k=='\n'){
        if(ipage==0) ipage=1; else if(ipage==1){ if(lic_ok) ipage=2; else lic_ok=1; }
        else if(ipage==2){ if(disk_sel<0||confirm_ok) ipage=3; } else if(ipage==5) start_install_done();
    } else if(k==' ' && ipage==1) lic_ok=!lic_ok;
}

/* ===================== lock / login ===================== */
static void draw_lock(void){
    backdrop(40);
    char t[16]; ksprintf(t,"%02d:%02d",rt_h,rt_m);
    textc(W/2,H/5,t,0xFFFFFF,9);
    char d[64]; ksprintf(d,"%s, %s %d",wd_names[weekday(rt_y,rt_mo?rt_mo:1,rt_d?rt_d:1)],mon_names[rt_mo<13?rt_mo:0],rt_d);
    textc(W/2,H/5+9*16+16,d,0xFFFFFF,2);
    textc(W/2,H-90,"Press any key or click to sign in",0xE0E0E0,1);
    logo(24,H-48,20,0xFFFFFF); text(52,H-46,"Nova OS",0xFFFFFF,1);
}
static void try_login(void){
    if(!strcmp(f_login.s,password)){ state=ST_DESK; login_bad=0; f_login.n=0; f_login.s[0]=0; sm_open=0; }
    else { login_bad=1; f_login.n=0; f_login.s[0]=0; }
}
static void draw_login(void){
    backdrop(120);
    int cx=W/2, cy=H/2-60;
    circle(cx,cy,56,0x2A6FB8); circle(cx,cy,52,ACCENT);
    char ini[2]={username[0]>='a'&&username[0]<='z'?username[0]-32:username[0],0};
    textc(cx,cy-24,ini,0xFFFFFF,3);
    textc(cx,cy+76,username,0xFFFFFF,2);
    curown=OWN_UI;
    field(cx-130,cy+124,260,&f_login,1,1);
    if(!f_login.n) text(cx-120,cy+132,"Password",0x808080,1);
    if(btn(cx-50,cy+170,100,32,"Sign in",1)) try_login();
    if(login_bad) textc(cx,cy+216,"The password is incorrect. Try again.",0xFF8A8A,1);
    if(btn(W-130,H-60,110,34,"Shut down",0)) want_power=2;
}

/* ===================== desktop ===================== */
typedef struct { const char *name; int type; } AppEnt;
static const AppEnt apps[]={{"Terminal",T_TERM},{"Notepad",T_NOTE},{"File Explorer",T_EXPL},{"Browser",T_BROW},{"About Nova",100},{"Help",101}};
#define NAPPS 6
static void launch(int type){
    if(type==100) open_browser("nova://about"); else if(type==101) open_browser("nova://help"); else open_app(type);
}
static int app_match(int i){
    if(!sm_ql) return 1;
    char a[32], b[32]; int n=strlen(apps[i].name);
    for(int k=0;k<=n;k++) a[k]=lower(apps[i].name[k]);
    for(int k=0;k<=sm_ql;k++) b[k]=lower(sm_q[k]);
    for(int s=0;s+sm_ql<=n;s++) if(!strncmp(a+s,b,sm_ql)) return 1;
    return 0;
}
static void sm_geom(int *x,int *y,int *w,int *h){
    *w=560; *h=580; if(*h>H-48-24) *h=H-48-24; if(*w>W-20) *w=W-20;
    *x=(W-*w)/2; *y=H-48-8-*h;
}
static void draw_startmenu(void){
    int x,y,w,h; sm_geom(&x,&y,&w,&h);
    rrect(x-1,y-1,w+2,h+2,11,0x4A4A52,255);
    rrect(x,y,w,h,10,0x24242C,248);
    curown=OWN_SM;
    rrect(x+24,y+20,w-48,36,18,0x34343C,255);
    if(sm_ql) text(x+44,y+29,sm_q,0xFFFFFF,1); else text(x+44,y+29,"Type here to search",0x909090,1);
    if(rt_s&1) rect(x+44+sm_ql*8,y+28,2,18,0xFFFFFF);
    text(x+30,y+78,sm_ql?"Results":"Pinned",0xFFFFFF,1);
    int n=0;
    for(int i=0;i<NAPPS;i++){
        if(!app_match(i)) continue;
        int cx=x+24+(n%4)*128, cy=y+104+(n/4)*100;
        int hv=hover(cx,cy,120,90);
        if(hv) rrect(cx,cy,120,90,6,0x3A3A44,255);
        int ty=apps[i].type<100?apps[i].type:T_BROW;
        app_icon(ty,cx+42,cy+14,36);
        textc(cx+60,cy+62,apps[i].name,0xFFFFFF,1);
        if(clicked(cx,cy,120,90)){ launch(apps[i].type); sm_open=0; sm_ql=0; sm_q[0]=0; }
        n++;
    }
    int by=y+h-64;
    hline(x+1,by,w-2,0x3A3A44);
    circle(x+44,by+32,16,ACCENT);
    char ini[2]={username[0]>='a'&&username[0]<='z'?username[0]-32:username[0],0};
    textc(x+44,by+24,ini,0xFFFFFF,1);
    text(x+68,by+24,username,0xFFFFFF,1);
    if(btn(x+w-290,by+16,70,32,"Lock",0)){ want_lock=1; }
    if(btn(x+w-212,by+16,90,32,"Restart",0)){ want_power=1; }
    if(btn(x+w-114,by+16,100,32,"Shut down",1)){ want_power=2; }
}
static int win_of_type(int type){ for(int i=nz-1;i>=0;i--) if(wins[zord[i]].type==type) return zord[i]; return -1; }
static void draw_taskbar(void){
    int y=H-48;
    rectA(0,y,W,48,0x1C1C22,238); hline(0,y,W,0x3A3A42);
    curown=OWN_TB;
    int x0=(W-5*48)/2;
    int types[5]={0,T_TERM,T_NOTE,T_EXPL,T_BROW};
    for(int i=0;i<5;i++){
        int sx=x0+i*48;
        int hv=hover(sx+2,y+4,44,40);
        if(hv||(i==0&&sm_open)) rrect(sx+2,y+4,44,40,5,0x3A3A44,255);
        if(i==0) logo(sx+13,y+13,22,ACCENT); else app_icon(types[i],sx+12,y+12,24);
        if(i>0){
            int id=win_of_type(types[i]);
            if(id>=0){
                int act=(id==win_active());
                rrect(sx+24-(act?9:4),y+42,act?18:8,3,1,act?ACCENT:0x9A9AA2,255);
            }
        }
        if(clicked(sx+2,y+4,44,40)){
            if(i==0){ sm_open=!sm_open; sm_ql=0; sm_q[0]=0; }
            else {
                sm_open=0;
                int id=win_of_type(types[i]);
                if(id<0) open_app(types[i]);
                else if(wins[id].min||id!=win_active()) win_focus(id);
                else wins[id].min=1;
            }
        }
    }
    char t[16], d[24];
    ksprintf(t,"%02d:%02d",rt_h,rt_m); ksprintf(d,"%d/%d/%d",rt_mo,rt_d,rt_y);
    textc(W-56,y+6,t,0xFFFFFF,1); textc(W-56,y+24,d,0xD0D0D0,1);
    text(W-130,y+16,"ENG",0xD0D0D0,1);
}
static void draw_msgbox(void){
    rectA(0,0,W,H,0x000000,110);
    int w=460,h=200,x=(W-w)/2,y=(H-h)/2;
    rrect(x-1,y-1,w+2,h+2,9,0x555560,255);
    rrect(x,y,w,h,8,0x2B2B32,255);
    text(x+20,y+18,mb_title,0xFFFFFF,2>1?1:1);
    wrap_text(x+20,y+54,(w-40)/8,mb_text,0xD0D0D0);
    curown=OWN_MB;
    if(btn(x+w-110,y+h-52,90,34,"OK",1)) mb_on=0;
}
static int desk_icon_clicked;
static void draw_desktop(void){
    backdrop(0);
    curown=OWN_DESK;
    int dt[4]={T_EXPL,T_TERM,T_NOTE,T_BROW}; const char *dn[4]={"Files","Terminal","Notepad","Browser"};
    for(int i=0;i<4;i++){
        int x=16, y=16+i*96;
        if(dsel==i) rrect(x,y,84,88,6,0xFFFFFF,50); else if(hover(x,y,84,88)) rrect(x,y,84,88,6,0xFFFFFF,25);
        app_icon(dt[i],x+22,y+8,40);
        textc(x+42,y+58,dn[i],0x000000,1); textc(x+41,y+57,dn[i],0xFFFFFF,1);
        if(clicked(x,y,84,88)){ if(dbl && dsel==i) open_app(dt[i]); dsel=i; }
    }
    if(click && owner==OWN_DESK && mx>110) dsel=-1;
    win_draw_all();
    draw_taskbar();
    if(sm_open) draw_startmenu();
    if(mb_on) draw_msgbox();
}

/* ===================== cursor / splash ===================== */
static const char *cur_shape[17]={
"X           ","XX          ","X.X         ","X..X        ","X...X       ","X....X      ",
"X.....X     ","X......X    ","X.......X   ","X........X  ","X.....XXXXX ","X..X..X     ",
"X.X X..X    ","XX  X..X    ","X    X..X   ","     X..X   ","      XX    "};
static int cur_x=-1, cur_y=-1;   /* posisi pointer yang sedang tergambar di framebuffer (-1 = belum) */
/* gambar ulang sebagian framebuffer dari scene (bb), lalu tempel pointer di (nx,ny) kalau nx>=0 */
static void cursor_region(int rx,int ry,int rw,int rh,int nx,int ny){
    u32 row[64];
    for(int j=0;j<rh;j++){
        int y=ry+j; if(y<0||y>=H) continue;
        int xs=rx<0?0:rx, xe=rx+rw>W?W:rx+rw; if(xe<=xs) continue;
        int n=xe-xs;
        for(int i=0;i<n;i++) row[i]=bb[y*W+xs+i];
        if(nx>=0){
            int cj=y-ny;
            if(cj>=0&&cj<17) for(int i=0;i<n;i++){
                int ci=xs+i-nx;
                if(ci>=0&&ci<12){ char c=cur_shape[cj][ci]; if(c=='X') row[i]=0x000000; else if(c=='.') row[i]=0xFFFFFF; }
            }
        }
        memcpy(FB+(u64)y*PITCH+(u64)xs*4,row,(u64)n*4);
    }
}
static void cursor_show(int nx,int ny){
    if(cur_x==nx&&cur_y==ny) return;
    int dx=nx-cur_x, dy=ny-cur_y; if(dx<0)dx=-dx; if(dy<0)dy=-dy;
    if(cur_x>=0 && dx<=24 && dy<=34){
        int ux=cur_x<nx?cur_x:nx, uy=cur_y<ny?cur_y:ny;
        cursor_region(ux,uy,12+dx,17+dy,nx,ny);
    } else {
        if(cur_x>=0) cursor_region(cur_x,cur_y,12,17,-1,-1);
        cursor_region(nx,ny,12,17,nx,ny);
    }
    cur_x=nx; cur_y=ny;
}
void pe_refresh_main(void){
    click=0; dbl=0; owner=-1; clip_reset(); draw_desktop(); flip(); cur_x=-1; cursor_show(pcx,pcy);
}
static void splash(void){
    clip_reset(); backdrop(130);
    logo(W/2-36,H/2-90,72,ACCENT);
    textc(W/2,H/2+10,"Nova OS",0xFFFFFF,3);
    for(int i=0;i<5;i++) circle(W/2-48+i*24,H/2+100,5,0xFFFFFF);
    flip();
    rtc_get(); int s0=rt_s, changes=0; u64 t0=0;
    for(long n=0;n<400000000L && changes<2;n++){ if((n&0xFFFF)==0){ rtc_get(); if(rt_s!=s0){ s0=rt_s; changes++;
        u64 t=rdtsc(); if(changes==1) t0=t; else { u64 d=t-t0; if(d>200000000ULL && d<20000000000ULL) tsc_hz=d; } } } }
}
static void safe_screen(void){
    clip_reset(); rect(0,0,W,H,0x000000);
    textc(W/2,H/2-8,"It's now safe to turn off your computer.",0xFFFFFF,1);
    flip();
    for(;;) __asm__ volatile("hlt");
}

/* ===================== main ===================== */
void kmain(u32 mbi){
    u8 *p=(u8*)(u64)mbi+8, *end=(u8*)(u64)mbi+*(u32*)(u64)mbi;
    int ok=0;
    while(p<end){
        u32 type=*(u32*)p, size=*(u32*)(p+4);
        if(type==0) break;
        if(type==8){
            u64 addr=*(u64*)(p+8); u32 pitch=*(u32*)(p+16), w=*(u32*)(p+20), h=*(u32*)(p+24); u8 bpp=p[28];
            if(bpp==32 && addr){ FB=(u8*)addr; PITCH=pitch; W=w>1920?1920:w; H=h>1080?1080:h; ok=1; }
        }
        if(type==3){
            u32 ms=*(u32*)(p+8), me=*(u32*)(p+12); const char *cmd=(const char*)(p+16);
            if(strstr_(cmd,"novadisk")){ u32 n=me-ms; if(n<=sizeof diskimg){ memcpy(diskimg,(void*)(u64)ms,n); diskimg_len=n; live=1; } }
            else if(nmods<24){ const char *b=cmd; for(const char *q=cmd;*q&&*q!=' ';q++) if(*q=='/') b=q+1;
                int k=0; while(b[k]&&b[k]!=' '&&k<47){ mods[nmods].name[k]=b[k]; k++; } mods[nmods].name[k]=0; mods[nmods].start=(u8*)(u64)ms; mods[nmods].len=me-ms; nmods++; }
        }
        if(type==6 && mr_n<32){ u32 es=*(u32*)(p+8); for(u8 *e=p+16;e+es<=p+size&&mr_n<32;e+=es) if(*(u32*)(e+16)==1){ mr_s[mr_n]=*(u64*)e; mr_e[mr_n]=*(u64*)e+*(u64*)(e+8); mr_n++; } }
        p+=(size+7)&~7U;
    }
    if(!ok){
        volatile u16 *vga=(volatile u16*)0xB8000; const char *m="Nova OS: no 32-bit framebuffer available";
        for(int i=0;m[i];i++) vga[i]=0x0F00|m[i];
        for(;;) __asm__ volatile("hlt");
    }
    mx=pcx=W/2; my=pcy=H/2;
    clip_reset();
    fs_init(); input_init(); make_wallpaper(); rtc_get();
    ata_init();
    if(nmods){ int pg=fs_find(0,"Programs"); if(pg<0) pg=fs_new(0,"Programs",1);
        for(int i=0;i<nmods;i++){ int f=fs_find(pg,mods[i].name); if(f<0) f=fs_new(pg,mods[i].name,0); if(f>0) fs_write(f,(const char*)mods[i].start,mods[i].len); } }
    if(!live && nfs_load_any()){ installed=1; state=ST_LOCK; }
    splash();

    int last_s=-1, first=1, smx=-1, smy=-1, smb=0;
    u64 last_rtc=0, last_scene=0;
    for(;;){
        u64 now=rdtsc();
        int sec_changed=0;
        if(now-last_rtc>tsc_hz/20){ last_rtc=now; rtc_get(); if(rt_s!=last_s){ last_s=rt_s; sec_changed=1; } }
        input_sample();
        int moved=(mx!=smx||my!=smy);
        int need = first||sec_changed||mclick||(kqh!=kqt)||(mb!=smb)
                 ||(moved && (drag>=0 || now-last_scene>tsc_hz/60))
                 ||(state==ST_INSTALL && ipage==4);
        if(!need){
            /* tidak ada yang perlu digambar ulang: cukup geser pointer (murah, mulus) */
            int px_=pcx, py_=pcy; if(px_!=cur_x||py_!=cur_y) cursor_show(px_,py_);
            __asm__ volatile("pause"); continue;
        }
        first=0;
        click=0; dbl=0; owner=-1;

        if(state==ST_INSTALL && ipage==4) install_tick();
        if(st_dev>=0 && fs_dirty && state==ST_DESK){ static u64 last_save; if(now-last_save>tsc_hz*3){ last_save=now; nfs_save(); } }

        if(mclick){
            mclick=0; click=1;
            u64 t=rdtsc();
            int dxm=mx-last_click_x, dym=my-last_click_y; if(dxm<0)dxm=-dxm; if(dym<0)dym=-dym;
            dbl=(t-last_click_tsc<(tsc_hz*3/10) && dxm<8 && dym<8);
            last_click_tsc=dbl?0:t; last_click_x=mx; last_click_y=my;
            if(state!=ST_DESK) owner=OWN_UI;
            else if(mb_on) owner=OWN_MB;
            else {
                int x,y,w,h; sm_geom(&x,&y,&w,&h);
                if(sm_open && hover(x,y,w,h)) owner=OWN_SM;
                else if(my>=H-48) owner=OWN_TB;
                else {
                    if(sm_open) sm_open=0;
                    int r=win_hit();
                    if(r>=0) owner=r; else if(r==-2) owner=-1; else owner=OWN_DESK;
                }
            }
            if(state==ST_LOCK){ state=ST_LOGIN; owner=-1; }
        }
        if(drag>=0){
            if(mb){ wins[drag].x=mx-dragx; wins[drag].y=my-dragy; if(wins[drag].y<0) wins[drag].y=0; if(wins[drag].y>H-48-32) wins[drag].y=H-48-32; }
            else drag=-1;
        }

        int k;
        while((k=kq_pop())){
            if(state==ST_INSTALL) installer_key(k);
            else if(state==ST_LOCK) state=ST_LOGIN;
            else if(state==ST_LOGIN){ if(k=='\n') try_login(); else field_key(&f_login,k); }
            else if(mb_on){ if(k=='\n'||k==27) mb_on=0; }
            else if(sm_open){
                if(k==27){ sm_open=0; }
                else if(k=='\n'){ for(int i=0;i<NAPPS;i++) if(app_match(i)){ launch(apps[i].type); break; } sm_open=0; sm_ql=0; sm_q[0]=0; }
                else if(k=='\b'){ if(sm_ql) sm_q[--sm_ql]=0; }
                else if(k>=32&&k<127&&sm_ql<28){ sm_q[sm_ql++]=k; sm_q[sm_ql]=0; }
            }
            else win_key(k);
        }

        clip_reset();
        switch(state){
        case ST_INSTALL: draw_installer(); break;
        case ST_LOCK: draw_lock(); break;
        case ST_LOGIN: draw_login(); break;
        default: draw_desktop(); break;
        }
        if(want_lock){ want_lock=0; state=ST_LOCK; sm_open=0; mb_on=0; }
        if(want_power){
            int wp=want_power; want_power=0;
            if(st_dev>=0 && fs_dirty && state!=ST_INSTALL) nfs_save();
            if(wp==1) do_reboot(); else { do_poweroff(); safe_screen(); }
        }
        flip();
        cur_x=-1; cursor_show(pcx,pcy);
        last_scene=rdtsc(); smx=mx; smy=my; smb=mb;
        click=0;
    }
}
