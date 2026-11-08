/* The session's messages on ENet (nk_session.c): big-endian, the first byte
 * the type. A change here changes NK_NET_PROTO: consoles of different
 * protocols refuse each other. */
#ifndef NK_NET_PROTO_H
#define NK_NET_PROTO_H

#include <stdint.h>

#include "nk_net.h"

#define NK_NET_PROTO 2

enum {
    NK_MSG_HELLO = 1,      /* guest -> host */
    NK_MSG_WELCOME = 2,    /* host -> guest */
    NK_MSG_REJECT = 3,     /* host -> guest */
    NK_MSG_TOWN_BEGIN = 4, /* host -> guest: the town file */
    NK_MSG_TOWN_CHUNK = 5,
    NK_MSG_TOWN_END = 6,
    NK_MSG_STATE = 7,      /* both (the host relays), unreliable */
    NK_MSG_APPEARANCE = 8, /* both (relayed) */
    NK_MSG_FIELD = 9,      /* both: guest -> host, host -> all */
    NK_MSG_LEAVE = 10,     /* host -> guests */
    NK_MSG_FIELD_BLOCK = 11, /* host -> guests: a whole acre */
    NK_MSG_CLOCK = 12,     /* host -> guests: the town's clock (on joining, then every 30 s) */
};

enum {
    NK_REJECT_VERSION = 1,
    NK_REJECT_PASSWORD = 2,
    NK_REJECT_FULL = 3,
    NK_REJECT_NO_TOWN = 4,
};

typedef struct __attribute__((packed)) NkMsgHello {
    uint8_t type;
    uint8_t proto;
    uint16_t pad;
    char name[NK_NET_NAME_LEN];
    char password[32];
} NkMsgHello;

typedef struct __attribute__((packed)) NkMsgWelcome {
    uint8_t type;
    uint8_t slot;
    uint8_t max_players;
    uint8_t pad;
    char host_name[NK_NET_NAME_LEN];
    char town_name[NK_NET_NAME_LEN];
} NkMsgWelcome;

typedef struct __attribute__((packed)) NkMsgReject {
    uint8_t type;
    uint8_t reason;
    uint16_t pad;
} NkMsgReject;

typedef struct __attribute__((packed)) NkMsgTownBegin {
    uint8_t type;
    uint8_t pad[3];
    uint32_t size;
    uint32_t crc;
    char file_name[64]; /* the GCI's name, as slot A's folder has it */
} NkMsgTownBegin;

typedef struct __attribute__((packed)) NkMsgTownChunk {
    uint8_t type;
    uint8_t pad[3];
    uint32_t offset; /* the bytes follow */
} NkMsgTownChunk;

typedef struct __attribute__((packed)) NkMsgState {
    uint8_t type;
    uint8_t pad[3];
    NkPlayerState state;
} NkMsgState;

typedef struct __attribute__((packed)) NkMsgAppearance {
    uint8_t type;
    uint8_t pad[3];
    NkAppearance look;
} NkMsgAppearance;

typedef struct __attribute__((packed)) NkMsgField {
    uint8_t type;
    uint8_t pad[3];
    NkFieldChange change;
} NkMsgField;

typedef struct __attribute__((packed)) NkMsgFieldBlock {
    uint8_t type;
    uint8_t pad[3];
    NkFieldBlock block;
} NkMsgFieldBlock;

typedef struct __attribute__((packed)) NkMsgClock {
    uint8_t type;
    uint8_t pad[3];
    NkClock clock;
    uint32_t age_ms; /* how long before sending the clock was read */
} NkMsgClock;

typedef struct __attribute__((packed)) NkMsgLeave {
    uint8_t type;
    uint8_t slot;
    uint8_t pad[2];
} NkMsgLeave;

#endif
