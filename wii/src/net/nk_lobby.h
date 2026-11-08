/* The lobby client (nk_lobby_proto.h): the browser's list (the lobby server's
 * public lobbies and those on the local network), joining one (by the list or
 * a private lobby's code), and a host's announcements. */
#ifndef NK_LOBBY_H
#define NK_LOBBY_H

#include <stdint.h>

#include "nk_lobby_proto.h"

/* A lobby found */
typedef struct NkLobbyInfo {
    NkLobbyEntry entry;
    int lan;           /* answered a LAN_DISCOVER: addr is the host's */
    int server;        /* not lan: the lobby server that listed it (nk_lobby_start's order) */
    uint32_t addr;     /* LAN: the host's address and game port */
    uint16_t port;
} NkLobbyInfo;

/* Where to reach a host (JOIN_INFO, or a LAN reply) */
typedef struct NkJoinTarget {
    NkLobbyEntry entry;
    uint32_t public_ip, local_ip;
    uint16_t public_port, local_port;
} NkJoinTarget;

/* The socket and the network (the guest's on a port of its own, the host's
 * on NK_GAME_PORT); `servers` the lobby servers, "host[:port]" one a line
 * (nk_servers_joined), "" for the local network only.
 * Returns 0, or < 0 when the network doesn't come up within `wait_ms`. */
int nk_lobby_start(const char* servers, int hosting, uint32_t wait_ms);
/* The servers found (by name or address) at the start */
int nk_lobby_server_count(void);
/* Whether the local network's broadcasts are used (on by default; off in a
 * test of the servers alone) */
void nk_lobby_set_lan(int on);

/* The browser: asks the server (when there is one) and the network, waits
 * `wait_ms` for answers; fills up to `max`, returns the count */
int nk_lobby_refresh(NkLobbyInfo* out, int max, uint32_t wait_ms);

/* Joining: by a listed lobby (its id, or its LAN address) or by a private
 * lobby's code. 0 with *target filled; NK_JOIN_NOT_FOUND, NK_JOIN_FULL, or
 * -1 when the server didn't answer */
int nk_lobby_join(const NkLobbyInfo* lobby, const char* code, NkJoinTarget* target, uint32_t wait_ms);
/* Sends PUNCH datagrams to the host's addresses (the guest's side) */
void nk_lobby_punch(const NkJoinTarget* target);

/* Hosting: what to announce (lobby_id ignored), then nk_lobby_host_poll
 * every frame or so (heartbeats, LAN answers, introductions) */
void nk_lobby_host_begin(const NkLobbyEntry* entry);
void nk_lobby_host_set_players(int players);
void nk_lobby_host_poll(void);
void nk_lobby_host_end(void);
/* The private code the server gave the lobby ("" until it has answered) */
const char* nk_lobby_host_code(void);
/* The server answered a registration (0 when there is no server or none yet) */
int nk_lobby_host_registered(void);

/* The servers in use, "a, b" ("" for none) */
const char* nk_lobby_server(void);

#endif
