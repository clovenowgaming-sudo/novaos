#pragma once
#include "core.h"

typedef struct { char name[32]; int parent, dir, used, size, cap; char *data; } Node;
static Node fs[192]; static int fs_dirty;

static int fs_new(int parent, const char *name, int dir){
    for(int i=1;i<192;i++) if(!fs[i].used){
        memset(&fs[i],0,sizeof fs[i]);
        fs[i].used=1; fs[i].parent=parent; fs[i].dir=dir; strncpyz(fs[i].name,name,32); fs_dirty=1;
        return i;
    }
    return -1;
}
static int fs_find(int parent, const char *name){
    for(int i=1;i<192;i++) if(fs[i].used && fs[i].parent==parent && !strcasecmp(fs[i].name,name)) return i;
    return -1;
}
static void fs_write(int i, const char *d, int len){
    if(len+1>fs[i].cap){
        int cap=(len+256)&~255; char *n=kmalloc(cap); if(!n) return;
        if(fs[i].data) memcpy(n,fs[i].data,fs[i].size);
        fs[i].data=n; fs[i].cap=cap;
    }
    memcpy(fs[i].data,d,len); fs[i].data[len]=0; fs[i].size=len; fs_dirty=1;
}
static int fs_mkfile(int parent, const char *name, const char *content){
    int i=fs_new(parent,name,0); if(i<0) return -1;
    fs_write(i, content? content : "", content? strlen(content):0);
    return i;
}
static void fs_unique(int parent, const char *base, const char *ext, char *out){
    ksprintf(out,"%s%s",base,ext);
    for(int n=2;fs_find(parent,out)>=0 && n<100;n++) ksprintf(out,"%s %d%s",base,n,ext);
}
static void fs_path(int i, char *out){
    if(i<=0){ strcpy(out,"/"); return; }
    char tmp[200]; fs_path(fs[i].parent,tmp);
    if(tmp[1]) ksprintf(out,"%s/%s",tmp,fs[i].name); else ksprintf(out,"/%s",fs[i].name);
}
static int fs_resolve(int cwd, const char *path){
    int cur = path[0]=='/' ? 0 : cwd;
    char tok[32];
    while(*path){
        while(*path=='/') path++;
        if(!*path) break;
        int n=0; while(*path && *path!='/' && n<31) tok[n++]=*path++;
        tok[n]=0;
        if(!strcmp(tok,".")) continue;
        if(!strcmp(tok,"..")){ if(cur>0) cur=fs[cur].parent; continue; }
        if(!fs[cur].dir) return -1;
        cur=fs_find(cur,tok);
        if(cur<0) return -1;
    }
    return cur;
}
static void fs_del(int i){
    if(i<=0) return;
    if(fs[i].dir) for(int k=1;k<192;k++) if(fs[k].used && fs[k].parent==i) fs_del(k);
    fs[i].used=0; fs_dirty=1;
}
static int fs_children(int dir, int *out, int max){
    int n=0;
    for(int pass=0;pass<2;pass++)
        for(int i=1;i<192 && n<max;i++) if(fs[i].used && fs[i].parent==dir && fs[i].dir==(pass==0)) out[n++]=i;
    return n;
}

static void fs_init(void){
    memset(fs,0,sizeof fs);
    fs[0].used=1; fs[0].dir=1; fs[0].parent=-1;
    int desk=fs_new(0,"Desktop",1), docs=fs_new(0,"Documents",1), dl=fs_new(0,"Downloads",1);
    int sys=fs_new(0,"System",1), www=fs_new(sys,"www",1);
    (void)desk; (void)dl;
    fs_mkfile(docs,"Welcome.txt",
        "Welcome to Nova OS!\n\n"
        "This is a from-scratch x86_64 operating system written in C and assembly.\n"
        "Try the Terminal (type 'help'), edit this file in Notepad,\n"
        "or browse the built-in pages in the Browser.\n");
    fs_mkfile(sys,"nova.cfg","version=0.1\ntheme=dark\nlanguage=en-US\n");
    fs_mkfile(www,"home.html",
        "<h1>Welcome to Nova Browser</h1>"
        "<p>A tiny web browser written from scratch in C. It renders simple HTML from the local file system.</p>"
        "<hr><h2>Quick links</h2>"
        "<li><a href=\"nova://about\">About Nova OS</a></li>"
        "<li><a href=\"nova://help\">Help and shortcuts</a></li>"
        "<li><a href=\"nova://roadmap\">Roadmap</a></li>"
        "<hr><p>Type an address in the bar above. nova://home works. http:// and https:// pages need the network stack, which is planned for the next build.</p>");
    fs_mkfile(www,"about.html",
        "<h1>About Nova OS</h1>"
        "<p>Nova OS 0.1 - 64-bit kernel written from scratch in C and x86_64 assembly.</p>"
        "<h2>What works</h2>"
        "<li>Multiboot2 boot, long mode, framebuffer graphics</li>"
        "<li>PS/2 keyboard and mouse</li>"
        "<li>Lock screen, sign-in, start menu, taskbar, window manager</li>"
        "<li>Terminal, Notepad, File Explorer, Browser</li>"
        "<li>In-memory file system</li>"
        "<p><a href=\"nova://home\">Back to home</a></p>");
    fs_mkfile(www,"help.html",
        "<h1>Help</h1>"
        "<li>Click the Start button or press the Windows logo on the taskbar.</li>"
        "<li>Drag windows by the title bar.</li>"
        "<li>Notepad: Ctrl+S saves, Ctrl+N creates a new file.</li>"
        "<li>Explorer: double-click or press Enter to open, Delete removes.</li>"
        "<li>Terminal: type help for a list of commands.</li>"
        "<p><a href=\"nova://home\">Back to home</a></p>");
    fs_mkfile(www,"roadmap.html",
        "<h1>Roadmap</h1>"
        "<h2>Next</h2>"
        "<li>USB (xHCI) keyboard and mouse for real hardware</li>"
        "<li>Ethernet driver, TCP/IP, DNS, download command</li>"
        "<li>TLS for https:// pages</li>"
        "<li>Real disk install (AHCI / NVMe) and persistent file system</li>"
        "<li>PE loader and x86 emulation layer for .exe files</li>"
        "<h2>Later</h2><li>WiFi and Bluetooth</li>"
        "<p><a href=\"nova://home\">Back to home</a></p>");
}
