/* The lobby protocol (wii/server/nookery_lobby.py keeps the same layouts):
 * UDP datagrams, big-endian, each starting with NkLobbyHeader. They travel
 * on the game's own socket (nk_socket.h routes them by their magic), so the
 * lobby server sees the address and port the host's and guest's NATs give
 * that socket, and can introduce the two (UDP hole punching).
 *
 *   host  -> server  HOST_REGISTER (every NK_LOBBY_HEARTBEAT_MS; the first
 *                    with lobby_id 0)          -> REGISTERED (id, token, code)
 *   host  -> server  HOST_CLOSE
 *   guest -> server  LIST_REQUEST              -> LIST_REPLY (public lobbies)
 *   guest -> server  JOIN_REQUEST (id or code) -> JOIN_INFO (the host's
 *                    addresses), and the server sends the host PUNCH_REQUEST
 *                    (the guest's): both send PUNCH to the other's
 *   guest -> broadcast LAN_DISCOVER (port NK_GAME_PORT) -> LAN_INFO from each
 *                    host on the network (private lobbies only for their code)
 */
#ifndef NK_LOBBY_PROTO_H
#define NK_LOBBY_PROTO_H

#include <stdint.h>

#define NK_LOBBY_VERSION 1
/* The host's game socket (ENet and the LAN replies); the lobby server's */
#define NK_GAME_PORT 28920
#define NK_LOBBY_SERVER_PORT 28910
#define NK_LOBBY_HEARTBEAT_MS 5000
#define NK_LOBBY_EXPIRE_MS 20000

#define NK_LOBBY_NAME_LEN 32
#define NK_LOBBY_TOWN_LEN 16
#define NK_LOBBY_HOST_LEN 16
#define NK_LOBBY_CODE_LEN 8 /* six characters, NUL-padded */
#define NK_LOBBY_LIST_MAX 8 /* entries in one LIST_REPLY */
#define NK_LOBBY_MAX_PLAYERS 4

enum {
    NK_LOBBY_HOST_REGISTER = 1,
    NK_LOBBY_REGISTERED = 2,
    NK_LOBBY_HOST_CLOSE = 3,
    NK_LOBBY_LIST_REQUEST = 4,
    NK_LOBBY_LIST_REPLY = 5,
    NK_LOBBY_JOIN_REQUEST = 6,
    NK_LOBBY_JOIN_INFO = 7,
    NK_LOBBY_PUNCH_REQUEST = 8,
    NK_LOBBY_PUNCH = 9,
    NK_LOBBY_LAN_DISCOVER = 10,
    NK_LOBBY_LAN_INFO = 11,
};

/* NkLobbyEntry.flags */
#define NK_LOBBY_PRIVATE 0x01  /* not listed: joined by its code */
#define NK_LOBBY_PASSWORD 0x02 /* the host asks for a password */

/* NkLobbyJoinInfo.result */
enum {
    NK_JOIN_OK = 0,
    NK_JOIN_NOT_FOUND = 1,
    NK_JOIN_FULL = 2,
};

typedef struct __attribute__((packed)) NkLobbyHeader {
    uint32_t magic; /* NK_LOBBY_MAGIC */
    uint8_t version;
    uint8_t type;
    uint16_t reserved;
} NkLobbyHeader;

/* A lobby as the browser lists it */
typedef struct __attribute__((packed)) NkLobbyEntry {
    uint32_t lobby_id;
    uint8_t flags;
    uint8_t players;
    uint8_t max_players;
    uint8_t game_version;
    char name[NK_LOBBY_NAME_LEN];
    char town[NK_LOBBY_TOWN_LEN];
    char host[NK_LOBBY_HOST_LEN];
} NkLobbyEntry; /* 72 bytes */

typedef struct __attribute__((packed)) NkLobbyRegister {
    NkLobbyHeader h;
    uint32_t token; /* the server's, from REGISTERED; 0 the first time */
    uint32_t local_ip;
    uint16_t game_port;
    uint16_t pad;
    NkLobbyEntry entry; /* entry.lobby_id 0 the first time */
} NkLobbyRegister;

typedef struct __attribute__((packed)) NkLobbyRegistered {
    NkLobbyHeader h;
    uint32_t lobby_id;
    uint32_t token;
    char code[NK_LOBBY_CODE_LEN];
} NkLobbyRegistered;

typedef struct __attribute__((packed)) NkLobbyClose {
    NkLobbyHeader h;
    uint32_t lobby_id;
    uint32_t token;
} NkLobbyClose;

typedef struct __attribute__((packed)) NkLobbyListRequest {
    NkLobbyHeader h;
    uint16_t page;
    uint16_t pad;
} NkLobbyListRequest;

typedef struct __attribute__((packed)) NkLobbyListReply {
    NkLobbyHeader h;
    uint16_t page;
    uint16_t total; /* public lobbies on the server */
    uint8_t count;
    uint8_t pad[3];
    NkLobbyEntry entries[NK_LOBBY_LIST_MAX];
} NkLobbyListReply;

typedef struct __attribute__((packed)) NkLobbyJoinRequest {
    NkLobbyHeader h;
    uint32_t lobby_id; /* 0: by code */
    char code[NK_LOBBY_CODE_LEN];
    uint32_t local_ip;
    uint16_t local_port;
    uint16_t pad;
} NkLobbyJoinRequest;

typedef struct __attribute__((packed)) NkLobbyJoinInfo {
    NkLobbyHeader h;
    uint8_t result;
    uint8_t pad[3];
    uint32_t public_ip;
    uint16_t public_port;
    uint16_t local_port;
    uint32_t local_ip;
    NkLobbyEntry entry;
} NkLobbyJoinInfo;

typedef struct __attribute__((packed)) NkLobbyPunchRequest {
    NkLobbyHeader h;
    uint32_t public_ip;
    uint16_t public_port;
    uint16_t local_port;
    uint32_t local_ip;
} NkLobbyPunchRequest;

typedef struct __attribute__((packed)) NkLobbyPunch {
    NkLobbyHeader h;
    uint32_t lobby_id;
} NkLobbyPunch;

typedef struct __attribute__((packed)) NkLobbyLanDiscover {
    NkLobbyHeader h;
    char code[NK_LOBBY_CODE_LEN]; /* "": the public lobbies */
} NkLobbyLanDiscover;

typedef struct __attribute__((packed)) NkLobbyLanInfo {
    NkLobbyHeader h;
    uint16_t game_port;
    uint16_t pad;
    NkLobbyEntry entry;
} NkLobbyLanInfo;

#endif
