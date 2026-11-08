#!/usr/bin/env python3
"""Nookery's lobby server: the list of online towns, and the introductions
that let a guest reach a host behind a home router (UDP hole punching).

The game itself is peer to peer: a host's Wii runs its town and its guests
connect to it directly. This server only keeps the lobbies hosts announce,
answers the lobby browser, and, when a guest picks a lobby (from the list, or
a private one by its code), tells each side the other's address so both can
send to it at once. It holds no towns and sees no game traffic.

    python3 nookery_lobby.py [--port 28910] [--bind 0.0.0.0] [--verbose]

Then set `lobby_server = your.host.name` in sd:/apps/nookery/settings.ini on
each Wii (or in the launcher's online settings). The protocol is in
wii/src/net/nk_lobby_proto.h; UDP port 28910 must be reachable.
"""

import argparse
import asyncio
import logging
import random
import socket
import struct
import time

MAGIC = 0x4E4B4C42  # "NKLB"
VERSION = 1

HOST_REGISTER, REGISTERED, HOST_CLOSE, LIST_REQUEST, LIST_REPLY = 1, 2, 3, 4, 5
JOIN_REQUEST, JOIN_INFO, PUNCH_REQUEST = 6, 7, 8

FLAG_PRIVATE = 0x01
JOIN_OK, JOIN_NOT_FOUND, JOIN_FULL = 0, 1, 2

HEADER = struct.Struct(">IBBH")
ENTRY = struct.Struct(">IBBBB32s16s16s")  # 72 bytes
REGISTER = struct.Struct(">IIHH")  # token, local_ip, game_port, pad (then an entry)
LIST_MAX = 8
EXPIRE_S = 20.0
CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no 0/O or 1/I

log = logging.getLogger("nookery-lobby")


def ip_to_int(ip: str) -> int:
    return struct.unpack(">I", socket.inet_aton(ip))[0]


def header(kind: int) -> bytes:
    return HEADER.pack(MAGIC, VERSION, kind, 0)


class Lobby:
    def __init__(self, lobby_id: int, token: int, code: str):
        self.lobby_id = lobby_id
        self.token = token
        self.code = code
        self.flags = 0
        self.players = 1
        self.max_players = 4
        self.game_version = 0
        self.name = b""
        self.town = b""
        self.host = b""
        self.public = ("0.0.0.0", 0)
        self.local_ip = 0
        self.local_port = 0
        self.seen = time.monotonic()

    def entry(self) -> bytes:
        return ENTRY.pack(self.lobby_id, self.flags, self.players, self.max_players, self.game_version,
                          self.name, self.town, self.host)


class LobbyServer(asyncio.DatagramProtocol):
    def __init__(self):
        self.lobbies: dict[int, Lobby] = {}
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def new_code(self) -> str:
        while True:
            code = "".join(random.choice(CODE_CHARS) for _ in range(6))
            if all(l.code != code for l in self.lobbies.values()):
                return code

    def expire(self):
        now = time.monotonic()
        for lid in [lid for lid, l in self.lobbies.items() if now - l.seen > EXPIRE_S]:
            log.info("lobby %d (%s) expired", lid, self.lobbies[lid].name.rstrip(b"\0").decode(errors="replace"))
            del self.lobbies[lid]

    def send(self, data: bytes, addr):
        self.transport.sendto(data, addr)

    def datagram_received(self, data: bytes, addr):
        if len(data) < HEADER.size:
            return
        magic, version, kind, _ = HEADER.unpack_from(data)
        if magic != MAGIC or version != VERSION:
            return
        body = data[HEADER.size:]
        self.expire()
        try:
            if kind == HOST_REGISTER:
                self.on_register(body, addr)
            elif kind == HOST_CLOSE:
                lid, token = struct.unpack_from(">II", body)
                lobby = self.lobbies.get(lid)
                if lobby is not None and lobby.token == token:
                    del self.lobbies[lid]
                    log.info("lobby %d closed", lid)
            elif kind == LIST_REQUEST:
                self.on_list(body, addr)
            elif kind == JOIN_REQUEST:
                self.on_join(body, addr)
        except struct.error:
            log.debug("short datagram from %s", addr)

    def on_register(self, body: bytes, addr):
        token, local_ip, game_port, _ = REGISTER.unpack_from(body)
        lid, flags, players, max_players, game_version, name, town, host = ENTRY.unpack_from(body, REGISTER.size)
        lobby = self.lobbies.get(lid)
        if lobby is None or lobby.token != token:
            lid = random.randrange(1, 0x7FFFFFFF)
            while lid in self.lobbies:
                lid = random.randrange(1, 0x7FFFFFFF)
            lobby = Lobby(lid, random.randrange(1, 0xFFFFFFFF), self.new_code())
            self.lobbies[lid] = lobby
            log.info("lobby %d registered from %s:%d: %s (%s's %s), code %s", lid, addr[0], addr[1],
                     name.rstrip(b"\0").decode(errors="replace"), host.rstrip(b"\0").decode(errors="replace"),
                     town.rstrip(b"\0").decode(errors="replace"), lobby.code)
        lobby.flags, lobby.players, lobby.max_players, lobby.game_version = flags, players, max_players, game_version
        lobby.name, lobby.town, lobby.host = name, town, host
        lobby.public = addr
        lobby.local_ip, lobby.local_port = local_ip, game_port
        lobby.seen = time.monotonic()
        self.send(header(REGISTERED) + struct.pack(">II8s", lobby.lobby_id, lobby.token,
                                                   lobby.code.encode()), addr)

    def on_list(self, body: bytes, addr):
        (page,) = struct.unpack_from(">H", body)
        public = sorted((l for l in self.lobbies.values() if not l.flags & FLAG_PRIVATE),
                        key=lambda l: (l.players >= l.max_players, l.name))
        chunk = public[page * LIST_MAX:(page + 1) * LIST_MAX]
        reply = header(LIST_REPLY) + struct.pack(">HHB3x", page, len(public), len(chunk))
        reply += b"".join(l.entry() for l in chunk)
        self.send(reply, addr)

    def on_join(self, body: bytes, addr):
        lid, code, local_ip, local_port, _ = struct.unpack_from(">I8sIHH", body)
        code = code.rstrip(b"\0").decode(errors="replace").upper()
        lobby = self.lobbies.get(lid) if lid else None
        if lobby is None and code:
            lobby = next((l for l in self.lobbies.values() if l.code == code), None)
        if lobby is None:
            self.send(header(JOIN_INFO) + struct.pack(">B3xIHHI", JOIN_NOT_FOUND, 0, 0, 0, 0) + bytes(ENTRY.size),
                      addr)
            return
        result = JOIN_FULL if lobby.players >= lobby.max_players else JOIN_OK
        host_ip = ip_to_int(lobby.public[0])
        self.send(header(JOIN_INFO) + struct.pack(">B3xIHHI", result, host_ip, lobby.public[1], lobby.local_port,
                                                  lobby.local_ip) + lobby.entry(), addr)
        if result == JOIN_OK:
            # the host sends to the guest at once: its router then lets the
            # guest's datagrams in
            self.send(header(PUNCH_REQUEST) + struct.pack(">IHHI", ip_to_int(addr[0]), addr[1], local_port,
                                                          local_ip), lobby.public)
            log.info("guest %s:%d joins lobby %d", addr[0], addr[1], lobby.lobby_id)


async def main_async(bind: str, port: int):
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(LobbyServer, local_addr=(bind, port))
    log.info("Nookery lobby server on udp %s:%d", bind, port)
    try:
        await asyncio.Event().wait()
    finally:
        transport.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=28910)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(main_async(a.bind, a.port))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
