#!/usr/bin/env python3
"""Bikin .exe contoh (PE32 x86 + PE32+ x64) dari assembly GAS: tulis "Hello from Nova" lewat kernel32 lalu ExitProcess."""
import struct, subprocess, sys, tempfile, os
SRC64 = """.intel_syntax noprefix
.text
.globl _start
_start:
    sub rsp, 56
    mov ecx, -11
    call [rip+IAT_GetStdHandle]
    mov rcx, rax
    lea rdx, [rip+msg]
    mov r8d, 31
    lea r9, [rsp+40]
    mov qword ptr [rsp+32], 0
    call [rip+IAT_WriteFile]
    xor ecx, ecx
    call [rip+IAT_ExitProcess]
msg: .ascii "Hello from a 64-bit Windows exe!\\n"
"""
SRC32 = """.intel_syntax noprefix
.code32
.text
.globl _start
_start:
    push -11
    call dword ptr ds:IAT_GetStdHandle
    sub esp, 4
    push 0
    lea ecx, [esp+4]
    push ecx
    push 31
    push offset msg
    push eax
    call dword ptr ds:IAT_WriteFile
    push 0
    call dword ptr ds:IAT_ExitProcess
msg: .ascii "Hello from a 32-bit Windows exe!\\n"
"""
NAMES = ["GetStdHandle", "WriteFile", "ExitProcess"]
def build(is64, out):
    base = 0x140000000 if is64 else 0x400000; ps = 8 if is64 else 4
    # .idata layout @ RVA 0x2000
    idata = 0x2000; desc = idata; ilt = idata + 40; iat = ilt + (len(NAMES) + 1) * ps; names = iat + (len(NAMES) + 1) * ps
    nb = b""; offs = []
    for n in NAMES: offs.append(names + len(nb)); nb += struct.pack("<H", 0) + n.encode() + b"\0" + (b"\0" if (len(n) + 1) % 2 else b"")
    dll = names + len(nb); nb += b"KERNEL32.dll\0"
    fmt = "<Q" if is64 else "<I"
    ilt_b = b"".join(struct.pack(fmt, o) for o in offs) + struct.pack(fmt, 0)
    d = struct.pack("<IIIII", ilt, 0, 0, dll, iat) + b"\0" * 20
    idb = d + ilt_b + ilt_b + nb
    defs = []
    for i, n in enumerate(NAMES): defs += ["--defsym", "IAT_%s=0x%x" % (n, base + iat + i * ps)]
    with tempfile.TemporaryDirectory() as td:
        s = os.path.join(td, "a.s"); open(s, "w").write(SRC64 if is64 else SRC32)
        o = os.path.join(td, "a.o"); subprocess.check_call(["as", "--64" if is64 else "--32", s, "-o", o])
        e = os.path.join(td, "a.elf")
        subprocess.check_call(["ld", "-m", "elf_x86_64" if is64 else "elf_i386", "-Ttext=0x%x" % (base + 0x1000), "-e", "_start", "-o", e, o] + defs)
        b = os.path.join(td, "a.bin"); subprocess.check_call(["objcopy", "-O", "binary", "-j", ".text", e, b]); text = open(b, "rb").read()
    fa = 0x200; hdr_sz = 0x200
    def pad(x): return x + b"\0" * (-len(x) % fa)
    t, i = pad(text), pad(idb)
    nsec = 2; opt = 240 if is64 else 224
    coff = struct.pack("<HHIIIHH", 0x8664 if is64 else 0x14c, nsec, 0, 0, 0, opt, 0x0022 if is64 else 0x0103)
    o = struct.pack("<HBBIIIII", 0x20b if is64 else 0x10b, 14, 0, len(t), len(i), 0, 0x1000, 0x1000)
    if not is64: o += struct.pack("<I", 0x2000)
    o += struct.pack("<Q" if is64 else "<I", base)
    o += struct.pack("<IIHHHHHHIIIIHH", 0x1000, fa, 6, 0, 0, 0, 6, 0, 0, 0x3000, hdr_sz, 0, 3, 0)
    o += struct.pack("<QQQQ" if is64 else "<IIII", 0x100000, 0x1000, 0x100000, 0x1000) + struct.pack("<II", 0, 16)
    o += struct.pack("<II", 0, 0) + struct.pack("<II", idata, len(idb)) + b"\0" * (14 * 8)
    assert len(o) == opt, (len(o), opt)
    secs = struct.pack("<8sIIIIIIHHI", b".text", len(text), 0x1000, len(t), hdr_sz, 0, 0, 0, 0, 0x60000020)
    secs += struct.pack("<8sIIIIIIHHI", b".idata", len(idb), 0x2000, len(i), hdr_sz + len(t), 0, 0, 0, 0, 0xC0000040)
    dos = b"MZ" + b"\0" * 58 + struct.pack("<I", 0x80) + b"\0" * 64
    img = dos + b"PE\0\0" + coff + o + secs
    open(out, "wb").write(pad(img) + t + i); print(out, os.path.getsize(out))
build(True, sys.argv[1]); build(False, sys.argv[2])
