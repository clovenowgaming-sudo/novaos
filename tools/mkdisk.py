#!/usr/bin/env python3
"""Bikin image disk Nova (dipakai installer): MBR+GRUB BIOS (core.img di LBA1), ESP FAT16 dengan BOOTX64.EFI,
dan partisi data Nova (kosong). Kernel + grub.cfg di-embed ke core.img/BOOTX64.EFI lewat memdisk (tanpa FS tambahan).
Butuh: grub-mkimage, /usr/lib/grub/i386-pc/boot.img (paket grub-pc-bin, grub-efi-amd64-bin)."""
import argparse, io, os, struct, subprocess, sys, tarfile, tempfile

SECT = 512
ESP_LBA, ESP_SECT = 2048, 16384
NFS_LBA, NFS_SECT = 18432, 32768          # harus sama dengan kernel/store.h (IMG_SECT, 2*NFS_SLOT)
CFG = 'set timeout=0\nset default=0\nset gfxpayload=keep\nmenuentry "Nova OS" {\n    multiboot2 (memdisk)/boot/kernel.elf\n    boot\n}\n'
BIOS_MODS = "biosdisk part_msdos part_gpt fat memdisk tar normal configfile boot echo test multiboot2 all_video video video_fb vbe"
EFI_MODS = "part_msdos part_gpt fat memdisk tar normal configfile boot echo test multiboot2 all_video video video_fb efi_gop efi_uga"

def make_tar(kernel):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w", format=tarfile.USTAR_FORMAT) as t:
        for name, data in (("boot/grub/grub.cfg", CFG.encode()), ("boot/kernel.elf", open(kernel, "rb").read())):
            ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = 0o644; t.addfile(ti, io.BytesIO(data))
    return b.getvalue()

def mkimage(tool, target, out, memdisk, mods):
    cmd = [tool, "-O", target, "-o", out, "-p", "(memdisk)/boot/grub", "-m", memdisk] + mods.split()
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit("grub-mkimage failed (%s):\n%s%s" % (target, r.stdout, r.stderr))
    return open(out, "rb").read()

def fat16_esp(efi):
    spc, rsv, nfat, rootent, fatsz = 2, 1, 2, 512, 32
    img = bytearray(ESP_SECT * SECT)
    bpb = struct.pack("<3s8sHBHBHHBHHHIIBBBI11s8s", b"\xEB\x3C\x90", b"NOVA 1.0", SECT, spc, rsv, nfat, rootent, ESP_SECT, 0xF8, fatsz, 63, 255,
                      ESP_LBA, 0, 0x80, 0, 0x29, 0x4E4F5641, b"NOVA ESP   ", b"FAT16   ")
    img[:len(bpb)] = bpb; img[510:512] = b"\x55\xAA"
    fat = bytearray(fatsz * SECT); fat[0:4] = b"\xF8\xFF\xFF\xFF"
    root_off = (rsv + nfat * fatsz) * SECT; data_off = root_off + rootent * 32; csz = spc * SECT
    nclus = (ESP_SECT - rsv - nfat * fatsz - rootent * 32 // SECT) // spc
    nxt = [2]
    def alloc(n):
        cl = list(range(nxt[0], nxt[0] + n)); nxt[0] += n
        for i, c in enumerate(cl): struct.pack_into("<H", fat, c * 2, cl[i + 1] if i + 1 < len(cl) else 0xFFFF)
        assert nxt[0] <= nclus + 2, "ESP too small"; return cl
    def ent(name, attr, cl, size): return struct.pack("<11sB10xHHHI", name, attr, 0, 0x21, cl, size)
    def put(cl, data):
        for i, c in enumerate(cl):
            o = data_off + (c - 2) * csz; chunk = data[i * csz:(i + 1) * csz]; img[o:o + len(chunk)] = chunk
    c_efi, c_boot = alloc(1)[0], alloc(1)[0]
    fcl = alloc((len(efi) + csz - 1) // csz)
    img[root_off:root_off + 32] = ent(b"EFI        ", 0x10, c_efi, 0)
    put([c_efi], ent(b".          ", 0x10, c_efi, 0) + ent(b"..         ", 0x10, 0, 0) + ent(b"BOOT       ", 0x10, c_boot, 0))
    put([c_boot], ent(b".          ", 0x10, c_boot, 0) + ent(b"..         ", 0x10, c_efi, 0) + ent(b"BOOTX64 EFI", 0x20, fcl[0], len(efi)))
    put(fcl, efi)
    for i in range(nfat): img[(rsv + i * fatsz) * SECT:(rsv + (i + 1) * fatsz) * SECT] = fat
    return bytes(img)

def part(boot, ptype, lba, n):
    return struct.pack("<B3sB3sII", 0x80 if boot else 0, b"\xFE\xFF\xFF", ptype, b"\xFE\xFF\xFF", lba, n)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--grub-mkimage", default=os.environ.get("GRUB_MKIMAGE", "grub-mkimage"))
    ap.add_argument("--boot-img", default="/usr/lib/grub/i386-pc/boot.img")
    a = ap.parse_args()
    tarb = make_tar(a.kernel)
    with tempfile.TemporaryDirectory() as td:
        tp = os.path.join(td, "memdisk.tar"); open(tp, "wb").write(tarb)
        core = mkimage(a.grub_mkimage, "i386-pc", os.path.join(td, "core.img"), tp, BIOS_MODS)
        efi = mkimage(a.grub_mkimage, "x86_64-efi", os.path.join(td, "BOOTX64.EFI"), tp, EFI_MODS)
    if len(core) > 2046 * SECT: sys.exit("core.img too big (%d bytes) for the 1MB embedding gap" % len(core))
    if len(efi) > (ESP_SECT - 200) * SECT // 2: sys.exit("BOOTX64.EFI too big for ESP")
    core = bytearray(core + b"\0" * (-len(core) % SECT)); ncs = len(core) // SECT
    core[SECT - 12:SECT] = struct.pack("<QHH", 2, ncs - 1, 0x820)      # blocklist: sektor core.img ke-2 dst. mulai LBA 2 (seperti grub-setup)
    mbr = bytearray(open(a.boot_img, "rb").read(SECT)); assert len(mbr) == SECT
    struct.pack_into("<Q", mbr, 0x5C, 1)                                # kernel_sector = LBA 1
    mbr[0x1BE:0x1FE] = part(1, 0xEF, ESP_LBA, ESP_SECT) + part(0, 0xDA, NFS_LBA, NFS_SECT) + b"\0" * 32
    mbr[510:512] = b"\x55\xAA"
    out = bytearray(NFS_LBA * SECT); out[:SECT] = mbr; out[SECT:SECT + len(core)] = core
    out[ESP_LBA * SECT:(ESP_LBA + ESP_SECT) * SECT] = fat16_esp(efi)
    open(a.out, "wb").write(out)
    print("novadisk.img: %d sectors (core.img %d B, BOOTX64.EFI %d B)" % (len(out) // SECT, len(core), len(efi)))
main()
