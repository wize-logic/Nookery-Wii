#!/usr/bin/env python3
"""Converts a GameCube disc image (.iso/.gcm) to a compact CISO: the image's
blocks that hold only zeros are left out (a GameCube disc is mostly padding),
so the file fits on a small SD card. Nookery for the Wii reads .iso, .gcm
and .ciso images (wii/src/dvd.c).

    iso2ciso.py GAME.iso GAME.ciso [--block 0x200000] [--trim]

--trim also leaves out the blocks no file uses (the junk the disc's mastering
filled its gaps with): only the header, the apploader, main.dol, the file
table and the files are kept.

The format: "CISO", the block size (u32, little-endian), then one byte per
block (1: stored) to 0x8000, then the stored blocks in order.
"""
import argparse
import struct
import sys

HEADER = 0x8000


def used_blocks(f, bs: int, nblocks: int) -> set[int]:
    def be32(b, o):
        return struct.unpack_from(">I", b, o)[0]
    f.seek(0)
    head = f.read(0x2440 + 0x20)
    dol_off, fst_off, fst_size = be32(head, 0x420), be32(head, 0x424), be32(head, 0x428)
    loader = be32(head, 0x2440 + 0x14) + be32(head, 0x2440 + 0x18)
    ranges = [(0, 0x2440 + 0x20 + loader), (fst_off, fst_size)]
    f.seek(dol_off)
    dol = f.read(0x100)
    end = 0x100
    for i in range(18):
        off, size = be32(dol, i * 4), be32(dol, 0x90 + i * 4)
        if size:
            end = max(end, off + size)
    ranges.append((dol_off, end))
    f.seek(fst_off)
    fst = f.read(fst_size)
    count = be32(fst, 8)
    for i in range(1, count):
        e = fst[i * 12:(i + 1) * 12]
        if e[0] == 0:
            ranges.append((be32(e, 4), be32(e, 8)))
    used = set()
    for off, size in ranges:
        for b in range(off // bs, min(nblocks, (off + max(size, 1) - 1) // bs + 1)):
            used.add(b)
    return used


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("ciso")
    ap.add_argument("--block", type=lambda s: int(s, 0), default=0x200000)
    ap.add_argument("--trim", action="store_true")
    a = ap.parse_args()
    bs = a.block
    zero = bytes(bs)
    blocks = []
    with open(a.iso, "rb") as f, open(a.ciso, "wb") as out:
        f.seek(0, 2)
        nblocks = (f.tell() + bs - 1) // bs
        keep = used_blocks(f, bs, nblocks) if a.trim else None
        f.seek(0)
        out.write(bytes(HEADER))
        while True:
            chunk = f.read(bs)
            if not chunk:
                break
            if len(blocks) >= HEADER - 8:
                print("image too large for the block size", file=sys.stderr)
                return 1
            if (keep is not None and len(blocks) not in keep) or chunk.count(0) == len(chunk):
                blocks.append(0)
                continue
            blocks.append(1)
            out.write(chunk.ljust(bs, b"\0"))
        out.seek(0)
        out.write(b"CISO" + struct.pack("<I", bs) + bytes(blocks))
    kept = sum(blocks)
    print(f"{a.ciso}: {kept} of {len(blocks)} blocks of {bs:#x} kept ({kept * bs / 1e6:.0f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
