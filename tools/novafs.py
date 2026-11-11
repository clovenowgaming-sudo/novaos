#!/usr/bin/env python3
"""Edit partisi data Nova di image disk dari host (mis. disk.img untuk QEMU):
   novafs.py disk.img list
   novafs.py disk.img add  <file-host> </Path/di/Nova>      (mis. add hello.exe /Programs/hello.exe)
   novafs.py disk.img cat  </Path>"""
import struct, sys
SECT, SLOT, MAGIC = 512, 16384, b"NOVAFS01"
def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h
def find_part(f):
    f.seek(0); m = f.read(512)
    for i in range(4):
        e = m[0x1BE + 16 * i:0x1CE + 16 * i]
        if e[4] == 0xDA: return struct.unpack_from("<I", e, 8)[0]
    sys.exit("No Nova data partition (type 0xDA) - was the OS installed to this disk?")
def read_slot(f, base, s):
    f.seek((base + s * SLOT) * SECT); h = f.read(SECT)
    if h[:8] != MAGIC: return None
    _, seq, plen, crc, nn, user, pw = struct.unpack_from("<8s4I40s40s", h)
    if plen > 7500000: return None
    p = f.read(plen)
    return (seq, user, pw, p) if fnv(p) == crc else None
def parse(p):
    nodes = {0: dict(parent=-1, dir=1, name="", data=b"")}; o = 0
    while o + 48 <= len(p):
        idx, par, d, size, name = struct.unpack_from("<IIII32s", p, o); o += 48
        nodes[idx] = dict(parent=par, dir=d, name=name.split(b"\0")[0].decode(), data=p[o:o + size]); o += size
    return nodes
def path(n, i): return "/" if i == 0 else (path(n, n[i]["parent"]).rstrip("/") + "/" + n[i]["name"])
def resolve(n, pth):
    cur = 0
    for part in [x for x in pth.split("/") if x]:
        nxt = [i for i, v in n.items() if i and v["parent"] == cur and v["name"].lower() == part.lower()]
        if not nxt: return None
        cur = nxt[0]
    return cur
def main():
    img, cmd = sys.argv[1], sys.argv[2]
    with open(img, "r+b") as f:
        base = find_part(f); best = None
        for s in (0, 1):
            r = read_slot(f, base, s)
            if r and (best is None or r[0] >= best[1][0]): best = (s, r)
        if not best: sys.exit("No valid Nova filesystem found on the data partition")
        s, (seq, user, pw, p) = best; n = parse(p)
        if cmd == "list":
            for i in sorted(n):
                if i: print(("d " if n[i]["dir"] else "f ") + path(n, i), len(n[i]["data"]))
        elif cmd == "cat":
            i = resolve(n, sys.argv[3]); sys.stdout.buffer.write(n[i]["data"] if i else b"")
        elif cmd == "add":
            data = open(sys.argv[3], "rb").read(); dst = sys.argv[4]
            dirs, name = dst.rsplit("/", 1); cur = 0
            for part in [x for x in dirs.split("/") if x]:
                nx = [i for i, v in n.items() if i and v["parent"] == cur and v["name"].lower() == part.lower() and v["dir"]]
                if nx: cur = nx[0]
                else:
                    free = min(i for i in range(1, 192) if i not in n); n[free] = dict(parent=cur, dir=1, name=part, data=b""); cur = free
            ex = [i for i, v in n.items() if i and v["parent"] == cur and v["name"].lower() == name.lower()]
            if ex: n[ex[0]]["data"] = data
            else:
                free = [i for i in range(1, 192) if i not in n]
                if not free: sys.exit("Nova file table is full")
                n[free[0]] = dict(parent=cur, dir=0, name=name, data=data)
            pl = b"".join(struct.pack("<IIII32s", i, v["parent"], v["dir"], 0 if v["dir"] else len(v["data"]), v["name"].encode()) + (b"" if v["dir"] else v["data"]) for i, v in sorted(n.items()) if i)
            ns = s ^ 1; f.seek((base + ns * SLOT + 1) * SECT); f.write(pl + b"\0" * (-len(pl) % SECT))
            hdr = struct.pack("<8s4I40s40s", MAGIC, seq + 1, len(pl), fnv(pl), len(n), user, pw).ljust(SECT, b"\0")
            f.seek((base + ns * SLOT) * SECT); f.write(hdr); print("added", dst, len(data), "bytes")
        else: sys.exit(__doc__)
main()
