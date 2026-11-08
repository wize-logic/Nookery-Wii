# Nookery lobby server

Lists the online lobbies of Nookery for the Wii and introduces guests to hosts behind home routers
(UDP hole punching). Games are console to console: the server never sees game traffic or passwords,
and keeps nothing on disk.

```sh
python3 nookery_lobby.py [--port 28910] [--bind 0.0.0.0] [--verbose]
```

It needs Python 3.7 or newer and nothing else. Its UDP port must be reachable from the internet.
Players add the server's address on each Wii under Online > Lobby servers, or in
`sd:/apps/nookery/lobby_servers.txt` (`host` or `host:port`, one per line). A Wii searches and
announces on every server on its list.

- Hosts announce every 5 s; a lobby silent for 20 s is dropped.
- Private lobbies are unlisted and joined by a six-character code.
- A list has up to 8 public lobbies, those with room first.
- The protocol is in `wii/src/net/nk_lobby_proto.h`; keep the two in step.

In the repository, `python3 wii/server/tests/test_lobby.py` tests it over loopback.
