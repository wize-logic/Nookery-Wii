/* Nookery's online play, as the game side (wii/src/netgame, built with the
 * game's headers) and the launcher see it. Plain C types only: the session
 * (nk_session.c, over libogc and ENet) and the game include this alike.
 *
 * The model: a host plays their own town; up to three guests connect to it
 * (directly, peer to peer; the lobby server only introduces them). A guest
 * gets the host's town (its saved file, into memory card slot B) and visits
 * it the game's own way, by train. While both are in the town, each sees the
 * other walking, and what changes on the ground (items picked up, dropped,
 * planted, dug) is sent to the other, the host's town having the last word. */
#ifndef NK_NET_H
#define NK_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NK_NET_MAX_PLAYERS 4
#define NK_NET_NAME_LEN 16

enum { NK_NET_OFF = 0, NK_NET_HOST = 1, NK_NET_GUEST = 2 };

/* Where the session is (nk_net_status) */
enum {
    NK_NET_IDLE = 0,
    NK_NET_CONNECTING,   /* guest: reaching the host */
    NK_NET_HANDSHAKE,    /* guest: waiting for the host's answer */
    NK_NET_TOWN,         /* guest: receiving the host's town */
    NK_NET_READY,        /* in a session */
    NK_NET_FAILED,       /* see nk_net_error */
};

int nk_net_mode(void);
int nk_net_status(void);
const char* nk_net_error(void);
/* This console's player slot in the session (the host's is 0) */
int nk_net_local_slot(void);
/* Players in the session, the host included */
int nk_net_player_count(void);
/* The town received (guest): bytes so far and in all */
void nk_net_town_progress(uint32_t* got, uint32_t* total);

/* A player's state, sent ten times a second (unreliable) */
typedef struct __attribute__((packed)) NkPlayerState {
    uint8_t slot;
    uint8_t in_town;     /* on the town's field (not indoors, not elsewhere) */
    uint16_t scene;      /* the game's scene number */
    uint32_t land_id;    /* the town's id (its name hashed in, nk_netgame.c) */
    float x, y, z;
    int16_t angle;       /* shape angle y */
    uint16_t anim;       /* the player's animation (mPlayer_ANIM_*) */
    float speed;
    uint32_t seq;
} NkPlayerState;

/* A player's looks, sent when they change (reliable) */
typedef struct __attribute__((packed)) NkAppearance {
    uint8_t slot;
    uint8_t sex;
    uint8_t face;
    uint8_t sunburn;
    uint16_t cloth;      /* the clothing item */
    uint16_t pad;
    char name[NK_NET_NAME_LEN]; /* the game's own encoding */
} NkAppearance;

/* One tile of the town's field changed */
typedef struct __attribute__((packed)) NkFieldChange {
    uint8_t block_x, block_z; /* the acre */
    uint8_t ut_x, ut_z;       /* the tile in it, 0-15 */
    uint16_t item;
    uint16_t pad;
    uint32_t land_id;
} NkFieldChange;

/* A whole acre of the town's field (the host's answer to a guest arriving
 * on the town's streets: its copy is the host's last save) */
#define NK_FIELD_ACRE_TILES 256
typedef struct __attribute__((packed)) NkFieldBlock {
    uint8_t block_x, block_z;
    uint16_t pad;
    uint32_t land_id;
    uint16_t items[NK_FIELD_ACRE_TILES];
} NkFieldBlock;

/* NkFieldChange.block_x of a guest's request for the host's whole field */
#define NK_FIELD_REQUEST 0xFE

/* The game side's calls (from the game's thread, each frame) */
void nk_net_publish_state(const NkPlayerState* state);
void nk_net_publish_appearance(const NkAppearance* look);
/* The latest state and looks of each other player: 1 when the slot is in
 * the session; *state_age_ms is how old the state is */
int nk_net_remote(int slot, NkPlayerState* state, NkAppearance* look, uint32_t* state_age_ms,
                  uint32_t* look_seq);
void nk_net_send_field(const NkFieldChange* change);
/* The next field change from the others (host: from its guests; guest:
 * from the host); 1 when there was one */
int nk_net_take_field(NkFieldChange* change);
/* Host: an acre to the guests; guest: the next acre from the host */
void nk_net_send_block(const NkFieldBlock* block);
int nk_net_take_block(NkFieldBlock* block);

/* The town's clock (after ACGC-PC-Multiplayer's CLOCK_SYNC): the host's
 * game clock (the hardware clock plus its save's time_delta, in the
 * GameCube's ticks) and its town. A guest in that town runs its clock from
 * it, so the time of day, the day and what comes with them agree */
typedef struct __attribute__((packed)) NkClock {
    int64_t game_ticks; /* the host's game time when read */
    uint32_t land_id;   /* the host's town */
    uint32_t pad;
} NkClock;

/* Host, each frame: its clock now */
void nk_net_publish_clock(const NkClock* clock);
/* Guest: the host's latest clock and how many ms ago it was read; 1 when
 * one came */
int nk_net_host_clock(NkClock* clock, uint32_t* age_ms);

/* The launcher's calls (ogc side) */
typedef struct NkHostConfig {
    char lobby_name[32];
    char password[32];  /* "" for none */
    int private_lobby;  /* not listed: joined by its code */
    int max_players;    /* 2 to 4, the host included */
    char player_name[NK_NET_NAME_LEN];
    char town_name[NK_NET_NAME_LEN];
} NkHostConfig;

/* Starts hosting (the lobby's announcements and the session's thread) */
int nk_net_host_start(const NkHostConfig* config, const char* lobby_server);
/* The lobby's private code ("" while the server hasn't given one) */
const char* nk_net_host_code(void);

/* Joins the host at these addresses (from the lobby), sending `password`;
 * the session's thread connects, then fetches the town into `town_dir`
 * (nk_net_status follows it) */
int nk_net_join_start(uint32_t public_ip, uint16_t public_port, uint32_t local_ip, uint16_t local_port,
                      const char* player_name, const char* password, const char* town_dir);
void nk_net_stop(void);

#ifdef __cplusplus
}
#endif

#endif
