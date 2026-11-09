#!/usr/bin/env python3
"""Makes Nookery for the Wii's release zip: the SD card's files (the
Homebrew Channel's apps/nookery folder) and the lobby server, and beside it the build's symbols
(nookery-wii-vVERSION-BUILD.elf.xz, for wii/tools/crash_report.py; keep it with the release). No game data:
players put their own disc image on the card.

    wii/tools/make_release.py [--build build/wii] [--out build/release]

The zip unpacks onto the SD card's root:
    apps/nookery/boot.dol, meta.xml, icon.png, lobby_servers.txt
    nookery-lobby-server/nookery_lobby.py, README.md   (for whoever runs one)
"""

import argparse
import re
import lzma
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(ROOT / "build" / "wii"))
    ap.add_argument("--out", default=str(ROOT / "build" / "release"))
    a = ap.parse_args()
    dol = Path(a.build) / "boot.dol"
    if not dol.is_file():
        print(f"{dol} is missing: build first (cmake --build --preset wii)", file=sys.stderr)
        return 1
    meta = ROOT / "wii" / "release" / "apps" / "nookery" / "meta.xml"
    version = re.search(r"<version>([^<]+)</version>", meta.read_text()).group(1)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    zpath = out / f"nookery-wii-v{version}.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dol, "apps/nookery/boot.dol")
        z.write(meta, "apps/nookery/meta.xml")
        z.write(ROOT / "wii" / "release" / "apps" / "nookery" / "icon.png", "apps/nookery/icon.png")
        # the default lobby servers (wii/src/net/nk_servers.h's NK_SERVERS_DEFAULT)
        z.write(ROOT / "wii" / "release" / "apps" / "nookery" / "lobby_servers.txt",
                "apps/nookery/lobby_servers.txt")
        z.write(ROOT / "wii" / "server" / "nookery_lobby.py", "nookery-lobby-server/nookery_lobby.py")
        z.write(ROOT / "wii" / "server" / "README.md", "nookery-lobby-server/README.md")
        z.write(ROOT / "wii" / "README.md", "apps/nookery/README.md")
    print(zpath)

    # The build's symbols, kept beside the zip (not in it): what turns a crash report's addresses into names
    # (wii/tools/crash_report.py LOG --elf THIS). Named by the build id in the log's first line.
    elf = Path(a.build) / "nookery.elf"
    hdr = Path(a.build) / "generated" / "nk_build_id.h"
    build = re.search(r'NK_BUILD_ID "([^"]+)"', hdr.read_text()).group(1) if hdr.is_file() else "unknown"
    if build.endswith("-modified") or build == "unknown":
        print(f"note: build {build}: commit first, so a crash report can be matched to a commit", file=sys.stderr)
    spath = out / f"nookery-wii-v{version}-{build}.elf.xz"
    with elf.open("rb") as src, lzma.open(spath, "wb", preset=6) as dst:
        shutil.copyfileobj(src, dst)
    print(spath)
    return 0


if __name__ == "__main__":
    sys.exit(main())
