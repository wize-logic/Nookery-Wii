# ENet 1.3.13 (vendored)

Upstream: <https://github.com/lsalzman/enet>, tag `v1.3.13`. MIT licence: see
`LICENSE`.

## Contents

| File | Status |
| --- | --- |
| `callbacks.c`, `host.c`, `list.c`, `packet.c`, `peer.c`, `protocol.c` | upstream, unmodified |
| `include/enet/{callbacks,list,protocol,time,types,utility}.h` | upstream, unmodified |
| `include/enet/enet.h` | upstream, plus one change: it includes `enet/ios.h` when `ENET_IOS` is defined |
| `include/enet/ios.h`, `ios.c` | new: the Wii platform layer over Nookery's socket (`../nk_socket.h`) (replaces `unix.c`/`win32.c`) |

Line endings are LF, as upstream.

## Wii platform layer

`ios.c` implements ENet's platform API on top of `../nk_socket.h`: one UDP
socket, shared with the lobby protocol (its datagrams start with "NKLB" and
never reach ENet), sends queued to the socket's I/O thread, receives taken
from its ring. Time and randomness come from `nk_socket.h`, memory from
newlib's malloc (MEM2). `compress.c` is left out (Nookery doesn't use it).
Call `enet_ios_initialize` instead of `enet_initialize`.
