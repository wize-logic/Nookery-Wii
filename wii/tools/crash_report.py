#!/usr/bin/env python3
"""Turns a Nookery crash or freeze report into function names and source lines.

The Wii build logs a crash (a CPU exception) and a freeze (no new frame for 20 s) to
sd:/apps/nookery/logs/log.txt (wii/src/ogc/stall.c): the exception, the registers and the
chain of return addresses. This finds those lines in a log, checks the log's build against
the ELF (the build id: the commit, "-modified" when files differed from it), and runs
addr2line on the addresses.

  wii/tools/crash_report.py log.txt
  wii/tools/crash_report.py log-1.txt --elf build/release/nookery-wii-v1.1.0-1a2b3c4d5e.elf.xz

The ELF must be the very build that made the log: build/wii/nookery.elf for a local build,
or the symbols file make_release.py keeps next to a release zip (an .elf.xz is unpacked to a
temporary file).
"""

import argparse
import lzma
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEVKITPPC = Path(os.environ.get("DEVKITPPC", "/opt/devkitpro/devkitPPC"))

HEX = r"([0-9a-fA-F]{8})"


def find_addr2line(given):
    if given:
        return given
    for cand in (DEVKITPPC / "bin" / "powerpc-eabi-addr2line", Path(shutil.which("powerpc-eabi-addr2line") or "")):
        if cand and Path(cand).is_file():
            return str(cand)
    sys.exit("powerpc-eabi-addr2line not found: install devkitPPC or pass --addr2line")


def code_address(a):
    return 0x80000000 <= a < 0x81800000


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", type=Path, help="the log (log.txt, log-1.txt, a Dolphin log...)")
    ap.add_argument("--elf", type=Path, default=ROOT / "build" / "wii" / "nookery.elf",
                    help="the build's ELF, or its .elf.xz (default: build/wii/nookery.elf)")
    ap.add_argument("--addr2line", help="powerpc-eabi-addr2line (default: devkitPPC's)")
    a = ap.parse_args()

    text = a.log.read_text(errors="replace")
    build = None
    m = re.search(r"Nookery for the Wii, build (\S+)", text) or re.search(r"(?:crash|stall): build (\S+)", text)
    if m:
        build = m.group(1)

    reports = []  # (kind, lines)
    for line in text.splitlines():
        body = re.sub(r"^.*?\b(crash|stall|OSPanic)", r"\1", line)
        if body.startswith("crash:") or body.startswith("stall:") or body.startswith("OSPanic"):
            kind = body.split(":", 1)[0]
            if not reports or reports[-1][0] != kind:
                reports.append((kind, []))
            reports[-1][1].append(body)
    if not reports:
        print(f"{a.log}: no crash or freeze report in it")
        if build:
            print(f"(build {build})")
        return 0

    elf = a.elf
    tmp = None
    if elf.suffix == ".xz":
        tmp = tempfile.NamedTemporaryFile(suffix=".elf", delete=False)
        with lzma.open(elf) as f:
            shutil.copyfileobj(f, tmp)
        tmp.close()
        elf = Path(tmp.name)
    if not elf.is_file():
        sys.exit(f"{elf}: no such ELF")

    print(f"Log {a.log}, build {build or 'unknown (no build line: a log from before builds were named)'}")
    if build:
        if (build.encode() + b"\0") in elf.read_bytes():
            print(f"ELF {a.elf}: the same build")
        else:
            print(f"ELF {a.elf}: NOT build {build}; the names below may be wrong")
    print()

    addr2line = find_addr2line(a.addr2line)

    def names(addrs):
        if not addrs:
            return []
        out = subprocess.run([addr2line, "-f", "-i", "-C", "-p", "-e", str(elf)] + [f"{x:08x}" for x in addrs],
                             capture_output=True, text=True).stdout.splitlines()
        # -i adds lines for inlined frames: keep them under their address
        res, i = [], 0
        for line in out:
            if line.startswith(" (inlined by)") and res:
                res[-1] += "\n" + " " * 14 + line.strip()
            else:
                res.append(line)
        return res

    for kind, lines in reports:
        for line in lines:
            print(line)
        print()
        where = []
        joined = "\n".join(lines)
        m = re.search(r"exception \(\d+\) at " + HEX + r", lr " + HEX, joined)
        if m:
            where.append(("crashed in", int(m.group(1), 16)))
            where.append(("called from (lr)", int(m.group(2), 16) - 4))
        m = re.search(r"its calls \(innermost first\):((?: [0-9a-fA-F]{8})+)", joined)
        if m:
            for i, h in enumerate(m.group(1).split()):
                where.append((f"call {i}", int(h, 16) - 4))  # the call before the return address
        where = [(label, x) for label, x in where if code_address(x)]
        if where:
            print("Where:")
            for (label, x), name in zip(where, names([x for _, x in where])):
                print(f"  {label:<17} {x:08x}  {name}")
            print()

    if tmp:
        os.unlink(tmp.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
