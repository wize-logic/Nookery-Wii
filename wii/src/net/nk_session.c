/* The online session (nk_net.h) over ENet: a thread of its own services the
 * ENet host, answers the lobby (nk_lobby.c) while hosting, and keeps the
 * latest of everyone's state for the game's thread to read.
 *
 * Channels: 0 reliable (handshake, town, looks, field changes), 1 unreliable
 * (player states). The host relays its guests' states and looks to one
 * another (a star: each guest connects to the host only). All the messages
 * are big-endian, the Wii's order (nk_net_proto.h). */

#include "nk_net.h"

#include <dirent.h>
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wl/base/base.h>
#include <wl/platform/platform.h>

#include "enet/enet.h"
#include "nk_lobby.h"
#include "nk_net_proto.h"
#include "nk_socket.h"

#define CH_RELIABLE 0
#define CH_STATE 1
#define STATE_INTERVAL_MS 100
#define TOWN_CHUNK 4096
#define FIELD_QUEUE 128
#define CONNECT_TIMEOUT_MS 8000
#define HANDSHAKE_TIMEOUT_MS 10000

/* nk_wii.h's (the town's folder on SD; slot B read again) */
#define NK_WII_DIR "sd:/apps/nookery"
void nk_wii_card_reload(int chan, const char* dir);
/* town_info.c: the town's name in a town save */
int nk_wii_town_name(const unsigned char* gci, unsigned int size, char* out, int out_size);

typedef struct Remote {
    int present;
    ENetPeer* peer;        /* host: the guest's peer */
    NkPlayerState state;
    uint32_t state_time;
    NkAppearance look;
    uint32_t look_seq;
    char name[NK_NET_NAME_LEN];
} Remote;

static mutex_t s_lock = LWP_MUTEX_NULL;
static lwp_t s_thread = LWP_THREAD_NULL;
static uint8_t s_stack[64 * 1024] __attribute__((aligned(32)));
static volatile int s_run;

static int s_mode;
static volatile int s_status;
static char s_error[96];
static int s_local_slot;
static ENetHost* s_host;
static ENetPeer* s_server_peer; /* guest: the host */
static Remote s_remote[NK_NET_MAX_PLAYERS];

static NkPlayerState s_local_state;
static int s_local_state_fresh;
static NkAppearance s_local_look;
static int s_local_look_fresh;
static uint32_t s_last_state_send;
/* the town's clock: host, its own (published by the game); guest, the host's */
#define CLOCK_INTERVAL_MS 30000
static NkClock s_clock;
static uint32_t s_clock_ms; /* nk_time_ms() when it was read */
static int s_clock_have, s_clock_fresh;
static uint32_t s_last_clock_send;
static void send_clock(ENetPeer* peer);

static NkFieldChange s_field_in[FIELD_QUEUE];
static uint32_t s_field_in_head, s_field_in_tail;
static NkFieldChange s_field_out[FIELD_QUEUE];
static uint32_t s_field_out_head, s_field_out_tail;
#define BLOCK_QUEUE 32
static NkFieldBlock s_block_in[BLOCK_QUEUE];
static uint32_t s_block_in_head, s_block_in_tail;
static NkMsgFieldBlock s_block_out[BLOCK_QUEUE];
static uint32_t s_block_out_head, s_block_out_tail;

/* Hosting */
static NkHostConfig s_config;
static uint8_t* s_town;
static uint32_t s_town_size, s_town_crc;
static char s_town_name_file[96];

/* Joining */
static uint32_t s_target_public_ip, s_target_local_ip;
static uint16_t s_target_public_port, s_target_local_port;
static char s_join_name[NK_NET_NAME_LEN];
static char s_join_password[32];
static char s_town_dir[96];
static uint8_t* s_town_rx;
static volatile uint32_t s_town_got, s_town_total;
static uint32_t s_phase_start;
static int s_tried_local;

static void lock(void)
{
    LWP_MutexLock(s_lock);
}

static void unlock(void)
{
    LWP_MutexUnlock(s_lock);
}

static void fail(const char* fmt, const char* detail)
{
    snprintf(s_error, sizeof(s_error), fmt, detail != NULL ? detail : "");
    WL_LOG_E("net", "%s", s_error);
    s_status = NK_NET_FAILED;
}

static uint32_t crc32(const uint8_t* p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

static void send_msg(ENetPeer* peer, int channel, const void* data, size_t len, int reliable)
{
    ENetPacket* p = enet_packet_create(data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (p != NULL && enet_peer_send(peer, (enet_uint8) channel, p) < 0) {
        enet_packet_destroy(p);
    }
}

/* Host: to every guest but `except` */
static void broadcast(int channel, const void* data, size_t len, int reliable, int except)
{
    for (int i = 1; i < NK_NET_MAX_PLAYERS; i++) {
        if (i != except && s_remote[i].present && s_remote[i].peer != NULL) {
            send_msg(s_remote[i].peer, channel, data, len, reliable);
        }
    }
}

/* ---- the host's town file --------------------------------------------------- */

/* The newest town save in slot A's folder (the game's "...MURA" file) */
static int load_town_file(void)
{
    char dir[96], best[192] = "";
    snprintf(dir, sizeof(dir), "%s/card_a", NK_WII_DIR);
    DIR* d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcasecmp(e->d_name + n - 4, ".gci") == 0 && strstr(e->d_name, "MURA") != NULL) {
            snprintf(best, sizeof(best), "%s/%s", dir, e->d_name);
            snprintf(s_town_name_file, sizeof(s_town_name_file), "%s", e->d_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return -1;
    }
    FILE* f = fopen(best, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = size > 0 && size < 4 * 1024 * 1024 ? malloc(size) : NULL;
    if (data == NULL || fread(data, 1, size, f) != (size_t) size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    free(s_town);
    s_town = data;
    s_town_size = (uint32_t) size;
    s_town_crc = crc32(data, s_town_size);
    if (s_config.town_name[0] == '\0') {
        nk_wii_town_name(data, s_town_size, s_config.town_name, sizeof(s_config.town_name));
    }
    WL_LOG_I("net", "the town to share: %s (%u bytes), %s", best, (unsigned) s_town_size, s_config.town_name);
    return 0;
}

static void send_town(ENetPeer* peer)
{
    NkMsgTownBegin begin;
    memset(&begin, 0, sizeof(begin));
    begin.type = NK_MSG_TOWN_BEGIN;
    begin.size = s_town_size;
    begin.crc = s_town_crc;
    snprintf(begin.file_name, sizeof(begin.file_name), "%s", s_town_name_file);
    send_msg(peer, CH_RELIABLE, &begin, sizeof(begin), 1);
    for (uint32_t off = 0; off < s_town_size; off += TOWN_CHUNK) {
        uint32_t n = s_town_size - off < TOWN_CHUNK ? s_town_size - off : TOWN_CHUNK;
        ENetPacket* p = enet_packet_create(NULL, sizeof(NkMsgTownChunk) + n, ENET_PACKET_FLAG_RELIABLE);
        if (p == NULL) {
            break;
        }
        NkMsgTownChunk* c = (NkMsgTownChunk*) p->data;
        c->type = NK_MSG_TOWN_CHUNK;
        c->pad[0] = c->pad[1] = c->pad[2] = 0;
        c->offset = off;
        memcpy(p->data + sizeof(NkMsgTownChunk), s_town + off, n);
        enet_peer_send(peer, CH_RELIABLE, p);
    }
    uint8_t end = NK_MSG_TOWN_END;
    send_msg(peer, CH_RELIABLE, &end, 1, 1);
}

/* ---- messages --------------------------------------------------------------- */

static int count_players(void)
{
    int n = 1;
    for (int i = 1; i < NK_NET_MAX_PLAYERS; i++) {
        n += s_remote[i].present;
    }
    return n;
}

static void host_on_hello(ENetPeer* peer, const NkMsgHello* hello)
{
    NkMsgReject rej;
    memset(&rej, 0, sizeof(rej));
    rej.type = NK_MSG_REJECT;
    if (hello->proto != NK_NET_PROTO) {
        rej.reason = NK_REJECT_VERSION;
    } else if (s_config.password[0] != '\0' &&
               strncmp(hello->password, s_config.password, sizeof(hello->password)) != 0)
    {
        rej.reason = NK_REJECT_PASSWORD;
    } else if (s_town == NULL && load_town_file() < 0) {
        rej.reason = NK_REJECT_NO_TOWN;
    } else {
        int slot = -1;
        for (int i = 1; i < s_config.max_players && i < NK_NET_MAX_PLAYERS; i++) {
            if (!s_remote[i].present) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            rej.reason = NK_REJECT_FULL;
        } else {
            lock();
            memset(&s_remote[slot], 0, sizeof(Remote));
            s_remote[slot].present = 1;
            s_remote[slot].peer = peer;
            memcpy(s_remote[slot].name, hello->name, NK_NET_NAME_LEN);
            unlock();
            peer->data = (void*) (uintptr_t) slot;
            NkMsgWelcome w;
            memset(&w, 0, sizeof(w));
            w.type = NK_MSG_WELCOME;
            w.slot = (uint8_t) slot;
            w.max_players = (uint8_t) s_config.max_players;
            memcpy(w.host_name, s_config.player_name, NK_NET_NAME_LEN);
            memcpy(w.town_name, s_config.town_name, NK_NET_NAME_LEN);
            send_msg(peer, CH_RELIABLE, &w, sizeof(w), 1);
            send_town(peer);
            send_clock(peer);
            /* the others' looks */
            lock();
            if (s_local_look.slot == 0) {
                NkMsgAppearance a = {NK_MSG_APPEARANCE, {0}};
                a.look = s_local_look;
                send_msg(peer, CH_RELIABLE, &a, sizeof(a), 1);
            }
            for (int i = 1; i < NK_NET_MAX_PLAYERS; i++) {
                if (i != slot && s_remote[i].present && s_remote[i].look_seq != 0) {
                    NkMsgAppearance a = {NK_MSG_APPEARANCE, {0}};
                    a.look = s_remote[i].look;
                    send_msg(peer, CH_RELIABLE, &a, sizeof(a), 1);
                }
            }
            unlock();
            nk_lobby_host_set_players(count_players());
            WL_LOG_I("net", "guest %.16s joined as player %d", hello->name, slot);
            return;
        }
    }
    WL_LOG_I("net", "guest %.16s refused (%d)", hello->name, (int) rej.reason);
    send_msg(peer, CH_RELIABLE, &rej, sizeof(rej), 1);
    enet_peer_disconnect_later(peer, 0);
}

static void on_state(const NkPlayerState* st, int from_slot)
{
    int slot = s_mode == NK_NET_HOST ? from_slot : st->slot;
    if (slot <= 0 && s_mode == NK_NET_HOST) {
        return;
    }
    if (slot < 0 || slot >= NK_NET_MAX_PLAYERS || slot == s_local_slot) {
        return;
    }
    lock();
    Remote* r = &s_remote[slot];
    r->present = 1;
    if (st->seq >= r->state.seq || nk_time_ms() - r->state_time > 2000) {
        r->state = *st;
        r->state.slot = (uint8_t) slot;
        r->state_time = nk_time_ms();
    }
    unlock();
    if (s_mode == NK_NET_HOST) {
        NkMsgState m = {NK_MSG_STATE, {0}};
        m.state = *st;
        m.state.slot = (uint8_t) slot;
        broadcast(CH_STATE, &m, sizeof(m), 0, slot);
    }
}

static void on_appearance(const NkAppearance* look, int from_slot)
{
    int slot = s_mode == NK_NET_HOST ? from_slot : look->slot;
    if (slot < 0 || slot >= NK_NET_MAX_PLAYERS || slot == s_local_slot) {
        return;
    }
    lock();
    s_remote[slot].present = 1;
    s_remote[slot].look = *look;
    s_remote[slot].look.slot = (uint8_t) slot;
    s_remote[slot].look_seq++;
    unlock();
    if (s_mode == NK_NET_HOST) {
        NkMsgAppearance m = {NK_MSG_APPEARANCE, {0}};
        m.look = *look;
        m.look.slot = (uint8_t) slot;
        broadcast(CH_RELIABLE, &m, sizeof(m), 1, slot);
    }
}

static void on_field(const NkFieldChange* change)
{
    lock();
    if (s_field_in_head - s_field_in_tail < FIELD_QUEUE) {
        s_field_in[s_field_in_head++ % FIELD_QUEUE] = *change;
    }
    unlock();
}

static void guest_on_town(const uint8_t* data, size_t len)
{
    uint8_t type = data[0];
    if (type == NK_MSG_TOWN_BEGIN && len >= sizeof(NkMsgTownBegin)) {
        const NkMsgTownBegin* b = (const NkMsgTownBegin*) data;
        free(s_town_rx);
        s_town_rx = b->size < 4 * 1024 * 1024 ? malloc(b->size) : NULL;
        s_town_total = s_town_rx != NULL ? b->size : 0;
        s_town_got = 0;
        s_town_crc = b->crc;
        snprintf(s_town_name_file, sizeof(s_town_name_file), "%.63s", b->file_name);
        if (s_town_rx == NULL) {
            fail("No memory for the host's town", NULL);
        }
        s_status = NK_NET_TOWN;
    } else if (type == NK_MSG_TOWN_CHUNK && s_town_rx != NULL && len > sizeof(NkMsgTownChunk)) {
        const NkMsgTownChunk* c = (const NkMsgTownChunk*) data;
        size_t n = len - sizeof(NkMsgTownChunk);
        if (c->offset < s_town_total && c->offset + n <= s_town_total) {
            memcpy(s_town_rx + c->offset, data + sizeof(NkMsgTownChunk), n);
            s_town_got += n;
        }
    } else if (type == NK_MSG_TOWN_END && s_town_rx != NULL) {
        if (s_town_got != s_town_total || crc32(s_town_rx, s_town_total) != s_town_crc) {
            fail("The host's town arrived damaged", NULL);
            return;
        }
        /* slot B's folder for this session: the host's town alone */
        char path[192];
        wl_platform_fs_mkdir(NK_WII_DIR "/net");
        wl_platform_fs_mkdir(s_town_dir);
        DIR* d = opendir(s_town_dir);
        if (d != NULL) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] != '.') {
                    snprintf(path, sizeof(path), "%s/%s", s_town_dir, e->d_name);
                    remove(path);
                }
            }
            closedir(d);
        }
        snprintf(path, sizeof(path), "%s/%s", s_town_dir,
                 s_town_name_file[0] != '\0' ? s_town_name_file : "8P-GAFE-DobutsunomoriP_MURA.gci");
        FILE* f = fopen(path, "wb");
        if (f == NULL || fwrite(s_town_rx, 1, s_town_total, f) != s_town_total) {
            if (f != NULL) {
                fclose(f);
            }
            fail("The host's town could not be saved to the SD card", NULL);
            return;
        }
        fclose(f);
        free(s_town_rx);
        s_town_rx = NULL;
        nk_wii_card_reload(1, s_town_dir);
        WL_LOG_I("net", "the host's town is in slot B (%s)", path);
        s_status = NK_NET_READY;
    }
}

static void handle(ENetPeer* peer, const uint8_t* data, size_t len)
{
    if (len < 1) {
        return;
    }
    int from = (int) (uintptr_t) peer->data;
    switch (data[0]) {
    case NK_MSG_HELLO:
        if (s_mode == NK_NET_HOST && len >= sizeof(NkMsgHello)) {
            host_on_hello(peer, (const NkMsgHello*) data);
        }
        break;
    case NK_MSG_WELCOME:
        if (s_mode == NK_NET_GUEST && len >= sizeof(NkMsgWelcome)) {
            const NkMsgWelcome* w = (const NkMsgWelcome*) data;
            s_local_slot = w->slot;
            s_local_state.slot = w->slot;
            s_local_look.slot = w->slot;
            s_status = NK_NET_TOWN;
            WL_LOG_I("net", "welcomed by %.16s as player %d", w->host_name, (int) w->slot);
        }
        break;
    case NK_MSG_REJECT:
        if (s_mode == NK_NET_GUEST && len >= sizeof(NkMsgReject)) {
            static const char* const reasons[] = {"", "Another version of Nookery", "Wrong password",
                                                  "The lobby is full", "The host has no saved town yet"};
            uint8_t r = ((const NkMsgReject*) data)->reason;
            fail("The host refused: %s", r < sizeof(reasons) / sizeof(reasons[0]) ? reasons[r] : "?");
        }
        break;
    case NK_MSG_TOWN_BEGIN:
    case NK_MSG_TOWN_CHUNK:
    case NK_MSG_TOWN_END:
        if (s_mode == NK_NET_GUEST) {
            guest_on_town(data, len);
        }
        break;
    case NK_MSG_STATE:
        if (len >= sizeof(NkMsgState)) {
            on_state(&((const NkMsgState*) data)->state, from);
        }
        break;
    case NK_MSG_APPEARANCE:
        if (len >= sizeof(NkMsgAppearance)) {
            on_appearance(&((const NkMsgAppearance*) data)->look, from);
        }
        break;
    case NK_MSG_FIELD:
        if (len >= sizeof(NkMsgField)) {
            on_field(&((const NkMsgField*) data)->change);
        }
        break;
    case NK_MSG_FIELD_BLOCK:
        if (s_mode == NK_NET_GUEST && len >= sizeof(NkMsgFieldBlock)) {
            lock();
            if (s_block_in_head - s_block_in_tail < BLOCK_QUEUE) {
                s_block_in[s_block_in_head++ % BLOCK_QUEUE] = ((const NkMsgFieldBlock*) data)->block;
            }
            unlock();
        }
        break;
    case NK_MSG_CLOCK:
        if (s_mode == NK_NET_GUEST && len >= sizeof(NkMsgClock)) {
            const NkMsgClock* c = (const NkMsgClock*) data;
            lock();
            s_clock = c->clock;
            s_clock_ms = nk_time_ms() - c->age_ms;
            s_clock_have = 1;
            s_clock_fresh = 1;
            unlock();
        }
        break;
    case NK_MSG_LEAVE:
        if (s_mode == NK_NET_GUEST && len >= sizeof(NkMsgLeave)) {
            uint8_t slot = ((const NkMsgLeave*) data)->slot;
            if (slot < NK_NET_MAX_PLAYERS) {
                lock();
                s_remote[slot].present = 0;
                unlock();
            }
        }
        break;
    }
}

/* ---- the thread --------------------------------------------------------------- */

static void guest_connect(uint32_t ip, uint16_t port)
{
    ENetAddress a;
    a.host = ip;
    a.port = port;
    for (int i = 0; i < 3; i++) {
        NkJoinTarget t;
        memset(&t, 0, sizeof(t));
        t.public_ip = t.local_ip = ip;
        t.public_port = t.local_port = port;
        nk_lobby_punch(&t);
    }
    s_server_peer = enet_host_connect(s_host, &a, 2, NK_NET_PROTO);
    s_phase_start = nk_time_ms();
    char text[16];
    nk_format_ipv4(ip, text, sizeof(text));
    WL_LOG_I("net", "connecting to %s:%u", text, (unsigned) port);
}

/* Host: the clock to one guest (peer) or all (NULL) */
static void send_clock(ENetPeer* peer)
{
    NkMsgClock m;
    memset(&m, 0, sizeof(m));
    m.type = NK_MSG_CLOCK;
    lock();
    int have = s_clock_have;
    m.clock = s_clock;
    m.age_ms = nk_time_ms() - s_clock_ms;
    unlock();
    if (!have) {
        return;
    }
    if (peer != NULL) {
        send_msg(peer, CH_RELIABLE, &m, sizeof(m), 1);
    } else {
        broadcast(CH_RELIABLE, &m, sizeof(m), 1, -1);
    }
}

static void send_states(void)
{
    if (s_mode == NK_NET_HOST && nk_time_ms() - s_last_clock_send >= CLOCK_INTERVAL_MS) {
        s_last_clock_send = nk_time_ms();
        send_clock(NULL);
    }
    if (nk_time_ms() - s_last_state_send < STATE_INTERVAL_MS) {
        return;
    }
    s_last_state_send = nk_time_ms();
    NkMsgState m = {NK_MSG_STATE, {0}};
    NkMsgAppearance a = {NK_MSG_APPEARANCE, {0}};
    int have_state, have_look;
    lock();
    have_state = s_local_state_fresh;
    have_look = s_local_look_fresh;
    m.state = s_local_state;
    a.look = s_local_look;
    s_local_state_fresh = 0;
    s_local_look_fresh = 0;
    unlock();
    if (s_mode == NK_NET_HOST) {
        if (have_state) {
            broadcast(CH_STATE, &m, sizeof(m), 0, -1);
        }
        if (have_look) {
            broadcast(CH_RELIABLE, &a, sizeof(a), 1, -1);
        }
    } else if (s_server_peer != NULL && s_status == NK_NET_READY) {
        if (have_state) {
            send_msg(s_server_peer, CH_STATE, &m, sizeof(m), 0);
        }
        if (have_look) {
            send_msg(s_server_peer, CH_RELIABLE, &a, sizeof(a), 1);
        }
    }
    /* whole acres out (host) */
    for (;;) {
        NkMsgFieldBlock b;
        lock();
        if (s_block_out_tail == s_block_out_head) {
            unlock();
            break;
        }
        b = s_block_out[s_block_out_tail++ % BLOCK_QUEUE];
        unlock();
        broadcast(CH_RELIABLE, &b, sizeof(b), 1, -1);
    }
    /* field changes out */
    for (;;) {
        NkMsgField f = {NK_MSG_FIELD, {0}};
        lock();
        if (s_field_out_tail == s_field_out_head) {
            unlock();
            break;
        }
        f.change = s_field_out[s_field_out_tail++ % FIELD_QUEUE];
        unlock();
        if (s_mode == NK_NET_HOST) {
            broadcast(CH_RELIABLE, &f, sizeof(f), 1, -1);
        } else if (s_server_peer != NULL) {
            send_msg(s_server_peer, CH_RELIABLE, &f, sizeof(f), 1);
        }
    }
}

static void* session_main(void* arg)
{
    (void) arg;
    while (s_run) {
        ENetEvent ev;
        if (s_mode == NK_NET_HOST) {
            nk_lobby_host_poll();
        } else {
            nk_lobby_host_poll(); /* (drops stray lobby datagrams) */
            if (s_status == NK_NET_CONNECTING && nk_time_ms() - s_phase_start > CONNECT_TIMEOUT_MS) {
                if (!s_tried_local && s_target_local_ip != 0 && s_target_local_ip != s_target_public_ip) {
                    /* the host may be behind this console's own router */
                    s_tried_local = 1;
                    if (s_server_peer != NULL) {
                        enet_peer_reset(s_server_peer);
                    }
                    guest_connect(s_target_local_ip, s_target_local_port);
                } else {
                    fail("The host could not be reached%s", "");
                }
            }
            if (s_status == NK_NET_HANDSHAKE && nk_time_ms() - s_phase_start > HANDSHAKE_TIMEOUT_MS) {
                fail("The host didn't answer%s", "");
            }
        }
        while (s_host != NULL && enet_host_service(s_host, &ev, 5) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (s_mode == NK_NET_GUEST) {
                    NkMsgHello h;
                    memset(&h, 0, sizeof(h));
                    h.type = NK_MSG_HELLO;
                    h.proto = NK_NET_PROTO;
                    memcpy(h.name, s_join_name, NK_NET_NAME_LEN);
                    snprintf(h.password, sizeof(h.password), "%s", s_join_password);
                    send_msg(ev.peer, CH_RELIABLE, &h, sizeof(h), 1);
                    s_status = NK_NET_HANDSHAKE;
                    s_phase_start = nk_time_ms();
                } else {
                    ev.peer->data = (void*) (uintptr_t) 0xFF;
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                handle(ev.peer, ev.packet->data, ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (s_mode == NK_NET_HOST) {
                    int slot = (int) (uintptr_t) ev.peer->data;
                    if (slot > 0 && slot < NK_NET_MAX_PLAYERS) {
                        lock();
                        s_remote[slot].present = 0;
                        s_remote[slot].peer = NULL;
                        unlock();
                        NkMsgLeave l = {NK_MSG_LEAVE, (uint8_t) slot, {0}};
                        broadcast(CH_RELIABLE, &l, sizeof(l), 1, slot);
                        nk_lobby_host_set_players(count_players());
                        WL_LOG_I("net", "player %d left", slot);
                    }
                } else if (s_status != NK_NET_FAILED) {
                    fail(s_status == NK_NET_READY ? "The host left the session%s"
                                                  : "The host closed the connection%s",
                         "");
                    lock();
                    memset(s_remote, 0, sizeof(s_remote));
                    unlock();
                }
                break;
            default:
                break;
            }
        }
        send_states();
        if (s_host == NULL) {
            struct timespec ts = {0, 10 * 1000000L};
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

static int start_thread(void)
{
    if (s_lock == LWP_MUTEX_NULL) {
        LWP_MutexInit(&s_lock, false);
    }
    if (enet_ios_initialize() != 0) {
        fail("The network library could not start%s", "");
        return -1;
    }
    s_run = 1;
    /* below the game's own threads (libogc's 64 is the game's main), above
     * idle: ENet is serviced every 5 ms */
    if (LWP_CreateThread(&s_thread, session_main, NULL, s_stack, sizeof(s_stack), 50) < 0) {
        fail("No thread for the session%s", "");
        return -1;
    }
    return 0;
}

/* ---- nk_net.h ---------------------------------------------------------------- */

int nk_net_host_start(const NkHostConfig* config, const char* lobby_server)
{
    memset(s_remote, 0, sizeof(s_remote));
    s_clock_have = s_clock_fresh = 0;
    s_config = *config;
    if (s_config.max_players < 2 || s_config.max_players > NK_NET_MAX_PLAYERS) {
        s_config.max_players = NK_NET_MAX_PLAYERS;
    }
    s_mode = NK_NET_HOST;
    s_local_slot = 0;
    s_local_state.slot = 0;
    s_local_look.slot = 0;
    if (nk_lobby_start(lobby_server, 1, 15000) < 0) {
        fail("The network is not available (Wii Settings > Internet)%s", "");
        return -1;
    }
    if (load_town_file() < 0) {
        WL_LOG_W("net", "no saved town yet: guests are refused until the first save");
    }
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = NK_GAME_PORT;
    s_host = enet_host_create(&a, NK_NET_MAX_PLAYERS - 1, 2, 0, 0);
    if (s_host == NULL) {
        fail("The session could not start%s", "");
        return -1;
    }
    NkLobbyEntry e;
    memset(&e, 0, sizeof(e));
    e.flags = (uint8_t) ((s_config.private_lobby ? NK_LOBBY_PRIVATE : 0) |
                         (s_config.password[0] != '\0' ? NK_LOBBY_PASSWORD : 0));
    e.players = 1;
    e.max_players = (uint8_t) s_config.max_players;
    e.game_version = NK_NET_PROTO;
    strncpy(e.name, s_config.lobby_name, sizeof(e.name));
    strncpy(e.town, s_config.town_name, sizeof(e.town));
    strncpy(e.host, s_config.player_name, sizeof(e.host));
    nk_lobby_host_begin(&e);
    s_status = NK_NET_READY;
    return start_thread();
}

const char* nk_net_host_code(void)
{
    return nk_lobby_host_code();
}

int nk_net_join_start(uint32_t public_ip, uint16_t public_port, uint32_t local_ip, uint16_t local_port,
                      const char* player_name, const char* password, const char* town_dir)
{
    memset(s_remote, 0, sizeof(s_remote));
    s_clock_have = s_clock_fresh = 0;
    s_mode = NK_NET_GUEST;
    s_target_public_ip = public_ip;
    s_target_public_port = public_port;
    s_target_local_ip = local_ip;
    s_target_local_port = local_port;
    s_tried_local = 0;
    memset(s_join_name, 0, sizeof(s_join_name));
    memcpy(s_join_name, player_name, strnlen(player_name, NK_NET_NAME_LEN));
    snprintf(s_join_password, sizeof(s_join_password), "%s", password != NULL ? password : "");
    snprintf(s_town_dir, sizeof(s_town_dir), "%s", town_dir);
    s_town_got = s_town_total = 0;
    s_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (s_host == NULL) {
        fail("The session could not start%s", "");
        return -1;
    }
    s_status = NK_NET_CONNECTING;
    /* the same network: straight to the host's own address (unless a test,
     * settings.ini net_test_public = 1, wants the public one first) */
    extern int g_nk_net_test_public;
    if (!g_nk_net_test_public && local_ip != 0 && (local_ip & 0xFFFFFF00u) == (nk_local_ip() & 0xFFFFFF00u)) {
        s_tried_local = 1;
        guest_connect(local_ip, local_port);
    } else {
        guest_connect(public_ip, public_port);
    }
    return start_thread();
}

void nk_net_stop(void)
{
    if (s_host != NULL) {
        for (size_t i = 0; i < s_host->peerCount; i++) {
            enet_peer_disconnect(&s_host->peers[i], 0);
        }
        enet_host_flush(s_host);
    }
    s_run = 0;
    if (s_thread != LWP_THREAD_NULL) {
        LWP_JoinThread(s_thread, NULL);
        s_thread = LWP_THREAD_NULL;
    }
    if (s_mode == NK_NET_HOST) {
        nk_lobby_host_end();
    }
    if (s_host != NULL) {
        enet_host_destroy(s_host);
        s_host = NULL;
    }
    s_server_peer = NULL;
    s_mode = NK_NET_OFF;
    s_status = NK_NET_IDLE;
}

int nk_net_mode(void)
{
    return s_mode;
}

int nk_net_status(void)
{
    return s_status;
}

const char* nk_net_error(void)
{
    return s_error;
}

int nk_net_local_slot(void)
{
    return s_local_slot;
}

int nk_net_player_count(void)
{
    if (s_mode == NK_NET_OFF) {
        return 1;
    }
    lock();
    int n = 1;
    for (int i = 0; i < NK_NET_MAX_PLAYERS; i++) {
        n += i != s_local_slot && s_remote[i].present;
    }
    unlock();
    return n;
}

void nk_net_town_progress(uint32_t* got, uint32_t* total)
{
    *got = s_town_got;
    *total = s_town_total;
}

void nk_net_publish_state(const NkPlayerState* state)
{
    if (s_mode == NK_NET_OFF) {
        return;
    }
    lock();
    uint32_t seq = s_local_state.seq + 1;
    s_local_state = *state;
    s_local_state.slot = (uint8_t) s_local_slot;
    s_local_state.seq = seq;
    s_local_state_fresh = 1;
    unlock();
}

void nk_net_publish_appearance(const NkAppearance* look)
{
    if (s_mode == NK_NET_OFF) {
        return;
    }
    lock();
    s_local_look = *look;
    s_local_look.slot = (uint8_t) s_local_slot;
    s_local_look_fresh = 1;
    unlock();
}

int nk_net_remote(int slot, NkPlayerState* state, NkAppearance* look, uint32_t* state_age_ms,
                  uint32_t* look_seq)
{
    if (s_mode == NK_NET_OFF || slot < 0 || slot >= NK_NET_MAX_PLAYERS || slot == s_local_slot) {
        return 0;
    }
    lock();
    Remote* r = &s_remote[slot];
    int present = r->present && r->state_time != 0;
    if (present) {
        *state = r->state;
        *look = r->look;
        *state_age_ms = nk_time_ms() - r->state_time;
        *look_seq = r->look_seq;
    }
    unlock();
    return present;
}

void nk_net_send_field(const NkFieldChange* change)
{
    if (s_mode == NK_NET_OFF) {
        return;
    }
    lock();
    if (s_field_out_head - s_field_out_tail < FIELD_QUEUE) {
        s_field_out[s_field_out_head++ % FIELD_QUEUE] = *change;
    }
    unlock();
}

int nk_net_take_field(NkFieldChange* change)
{
    int got = 0;
    if (s_mode == NK_NET_OFF) {
        return 0;
    }
    lock();
    if (s_field_in_tail != s_field_in_head) {
        *change = s_field_in[s_field_in_tail++ % FIELD_QUEUE];
        got = 1;
    }
    unlock();
    return got;
}

void nk_net_send_block(const NkFieldBlock* block)
{
    if (s_mode != NK_NET_HOST || s_host == NULL) {
        return;
    }
    NkMsgFieldBlock m;
    memset(&m, 0, sizeof(m));
    m.type = NK_MSG_FIELD_BLOCK;
    m.block = *block;
    /* (from the game's thread: ENet's host is the session thread's; the
     * packet is queued under the lock and sent by that thread) */
    lock();
    if (s_block_out_head - s_block_out_tail < BLOCK_QUEUE) {
        s_block_out[s_block_out_head++ % BLOCK_QUEUE] = m;
    }
    unlock();
}

int nk_net_take_block(NkFieldBlock* block)
{
    int got = 0;
    lock();
    if (s_block_in_tail != s_block_in_head) {
        *block = s_block_in[s_block_in_tail++ % BLOCK_QUEUE];
        got = 1;
    }
    unlock();
    return got;
}

void nk_net_publish_clock(const NkClock* clock)
{
    lock();
    s_clock = *clock;
    s_clock_ms = nk_time_ms();
    s_clock_have = 1;
    unlock();
}

int nk_net_host_clock(NkClock* clock, uint32_t* age_ms)
{
    int fresh;
    lock();
    fresh = s_clock_fresh && s_mode == NK_NET_GUEST;
    if (fresh) {
        *clock = s_clock;
        *age_ms = nk_time_ms() - s_clock_ms;
        s_clock_fresh = 0;
    }
    unlock();
    return fresh;
}
