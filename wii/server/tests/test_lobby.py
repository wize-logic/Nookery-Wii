#!/usr/bin/env python3
"""Tests nookery_lobby.py over loopback: a host registers (public, then a
private one), a browser lists, a guest joins by id and by code, and the host
gets the guest's address to punch to.

    python3 wii/server/tests/test_lobby.py
"""
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

SERVER = Path(__file__).resolve().parents[1] / "nookery_lobby.py"
PORT = 38910
MAGIC = 0x4E4B4C42


def hdr(kind):
    return struct.pack(">IBBH", MAGIC, 1, kind, 0)


def entry(name, flags=0, players=1, max_players=4):
    return struct.pack(">IBBBB32s16s16s", 0, flags, players, max_players, 1, name.encode(), b"Tumbleweed", b"Ann")


def sock():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(2)
    return s


def main():
    proc = subprocess.Popen([sys.executable, str(SERVER), "--bind", "127.0.0.1", "--port", str(PORT)])
    try:
        time.sleep(0.5)
        addr = ("127.0.0.1", PORT)
        host, private_host, guest = sock(), sock(), sock()

        host.sendto(hdr(1) + struct.pack(">IIHH", 0, 0x0A000002, 28920, 0) + entry("Public town"), addr)
        data, _ = host.recvfrom(1500)
        lobby_id, token, code = struct.unpack_from(">II8s", data, 8)
        assert data[5] == 2 and lobby_id != 0, "REGISTERED"

        private_host.sendto(hdr(1) + struct.pack(">IIHH", 0, 0, 28920, 0) + entry("Secret", flags=1), addr)
        data, _ = private_host.recvfrom(1500)
        _, _, private_code = struct.unpack_from(">II8s", data, 8)
        private_code = private_code.rstrip(b"\0").decode()
        assert len(private_code) == 6

        guest.sendto(hdr(4) + struct.pack(">HH", 0, 0), addr)
        data, _ = guest.recvfrom(1500)
        page, total, count = struct.unpack_from(">HHB", data, 8)
        assert data[5] == 5 and total == 1 and count == 1, f"LIST: total {total} count {count} (private hidden)"
        listed = struct.unpack_from(">IBBBB32s16s16s", data, 16)
        assert listed[0] == lobby_id and listed[5].rstrip(b"\0") == b"Public town"

        guest.sendto(hdr(6) + struct.pack(">I8sIHH", lobby_id, b"", 0xC0A80005, 41234, 0), addr)
        data, _ = guest.recvfrom(1500)
        result, public_ip, public_port, local_port, local_ip = struct.unpack_from(">B3xIHHI", data, 8)
        assert data[5] == 7 and result == 0 and public_port == host.getsockname()[1] and local_ip == 0x0A000002
        data, _ = host.recvfrom(1500)
        assert data[5] == 8, "PUNCH_REQUEST to the host"
        gip, gport, glocal_port, glocal_ip = struct.unpack_from(">IHHI", data, 8)
        assert gport == guest.getsockname()[1] and glocal_port == 41234 and glocal_ip == 0xC0A80005

        guest.sendto(hdr(6) + struct.pack(">I8sIHH", 0, private_code.lower().encode(), 0, 0, 0), addr)
        data, _ = guest.recvfrom(1500)
        assert data[5] == 7 and data[8] == 0, "join a private lobby by its code (any case)"
        private_host.recvfrom(1500)

        guest.sendto(hdr(6) + struct.pack(">I8sIHH", 0, b"ZZZZZZ", 0, 0, 0), addr)
        data, _ = guest.recvfrom(1500)
        assert data[8] == 1, "an unknown code: not found"

        full = sock()
        full.sendto(hdr(1) + struct.pack(">IIHH", 0, 0, 28920, 0) + entry("Full", players=4, max_players=4), addr)
        data, _ = full.recvfrom(1500)
        full_id = struct.unpack_from(">I", data, 8)[0]
        guest.sendto(hdr(6) + struct.pack(">I8sIHH", full_id, b"", 0, 0, 0), addr)
        data, _ = guest.recvfrom(1500)
        assert data[8] == 2, "a full lobby"

        host.sendto(hdr(3) + struct.pack(">II", lobby_id, token), addr)
        time.sleep(0.2)
        guest.sendto(hdr(4) + struct.pack(">HH", 0, 0), addr)
        data, _ = guest.recvfrom(1500)
        assert struct.unpack_from(">H", data, 10)[0] == 1, "closed: only the full one is listed"
        print("lobby server: all tests passed")
        return 0
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
