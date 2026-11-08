/* The lobby client: see nk_lobby.h and nk_lobby_proto.h */

#include "nk_lobby.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wl/base/base.h>

#include "nk_servers.h"
#include "nk_socket.h"

/* The lobby servers (nk_servers.h's list), and a host's registration on each */
typedef struct Server {
    char name[NK_SERVER_NAME_LEN];
    uint32_t ip;
    uint16_t port;
    uint32_t lobby_id, token;
    char code[NK_LOBBY_CODE_LEN + 1];
} Server;
static Server s_servers[NK_SERVERS_MAX];
static int s_server_count;
static char s_server_names[NK_SERVERS_MAX * NK_SERVER_NAME_LEN];

/* Hosting */
static int s_hosting;
static NkLobbyEntry s_entry;
static char s_code[NK_LOBBY_CODE_LEN + 1]; /* the code shown: the first server's that gave one */
static char s_local_code[NK_LOBBY_CODE_LEN + 1]; /* with no server: the local network's own */
static uint32_t s_last_register;

/* The local network's broadcasts (finding and answering lobbies there) */
static int s_lan = 1;

void nk_lobby_set_lan(int on)
{
    s_lan = on;
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = {ms / 1000, (long) (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void header(NkLobbyHeader* h, uint8_t type)
{
    h->magic = NK_LOBBY_MAGIC;
    h->version = NK_LOBBY_VERSION;
    h->type = type;
    h->reserved = 0;
}

static int valid(const void* data, int len, int min, uint8_t type)
{
    const NkLobbyHeader* h = data;
    return len >= min && h->magic == NK_LOBBY_MAGIC && h->version == NK_LOBBY_VERSION && h->type == type;
}

const char* nk_lobby_server(void)
{
    return s_server_names;
}

/* The server a datagram came from, or -1 */
static int server_of(uint32_t addr, uint16_t port)
{
    for (int i = 0; i < s_server_count; i++) {
        if (s_servers[i].ip == addr && s_servers[i].port == port) {
            return i;
        }
    }
    return -1;
}

int nk_lobby_server_count(void)
{
    return s_server_count;
}

int nk_lobby_start(const char* servers, int hosting, uint32_t wait_ms)
{
    nk_socket_init();
    for (uint32_t waited = 0; !nk_net_ready(); waited += 50) {
        if (waited >= wait_ms) {
            return -1;
        }
        sleep_ms(50);
    }
    uint16_t port = hosting ? NK_GAME_PORT : (uint16_t) (41000 + nk_random() % 10000);
    if (!nk_socket_is_open() || (hosting && nk_socket_port() != NK_GAME_PORT)) {
        if (nk_socket_open(port) < 0) {
            WL_LOG_E("lobby", "no socket on port %u", (unsigned) port);
            return -2;
        }
    }
    /* the servers: one address a line (or comma separated) */
    s_server_count = 0;
    s_server_names[0] = '\0';
    const char* p = servers != NULL ? servers : "";
    while (*p != '\0' && s_server_count < NK_SERVERS_MAX) {
        char host[NK_SERVER_NAME_LEN];
        int n = 0;
        while (*p == '\n' || *p == ',' || *p == ' ' || *p == '\r') {
            p++;
        }
        while (*p != '\0' && *p != '\n' && *p != ',' && *p != ' ' && *p != '\r' && n < (int) sizeof(host) - 1) {
            host[n++] = *p++;
        }
        host[n] = '\0';
        if (n == 0) {
            continue;
        }
        Server* sv = &s_servers[s_server_count];
        memset(sv, 0, sizeof(*sv));
        snprintf(sv->name, sizeof(sv->name), "%s", host);
        char* colon = strchr(host, ':');
        sv->port = NK_LOBBY_SERVER_PORT;
        if (colon != NULL) {
            *colon = '\0';
            sv->port = (uint16_t) atoi(colon + 1);
        }
        if (sv->port == 0 || nk_resolve(host, &sv->ip) < 0) {
            WL_LOG_W("lobby", "the lobby server %s could not be found", sv->name);
            continue;
        }
        if (server_of(sv->ip, sv->port) >= 0) {
            continue; /* the same server twice */
        }
        if (s_server_names[0] != '\0') {
            strcat(s_server_names, ", ");
        }
        strcat(s_server_names, sv->name);
        s_server_count++;
    }
    WL_LOG_I("lobby", "%d lobby server(s): %s", s_server_count, s_server_names[0] != '\0' ? s_server_names : "none");
    return 0;
}

/* ---- the browser ------------------------------------------------------------ */

static void add(NkLobbyInfo* out, int* count, int max, const NkLobbyInfo* found)
{
    for (int i = 0; i < *count; i++) {
        int same = (found->lan && out[i].lan && out[i].addr == found->addr) ||
                   (!found->lan && !out[i].lan && found->server == out[i].server && found->entry.lobby_id != 0 &&
                    out[i].entry.lobby_id == found->entry.lobby_id) ||
                   /* one host announced on several servers, or on a server
                    * and this network: the same names */
                   (memcmp(found->entry.name, out[i].entry.name, sizeof(found->entry.name)) == 0 &&
                    memcmp(found->entry.town, out[i].entry.town, sizeof(found->entry.town)) == 0 &&
                    memcmp(found->entry.host, out[i].entry.host, sizeof(found->entry.host)) == 0);
        if (same) {
            /* the same lobby on the server and on this network: the local
             * answer (its address is the host's own) */
            if (found->lan || !out[i].lan) {
                out[i] = *found;
            }
            return;
        }
    }
    if (*count < max) {
        out[(*count)++] = *found;
    }
}

int nk_lobby_refresh(NkLobbyInfo* out, int max, uint32_t wait_ms)
{
    int count = 0;
    uint8_t buf[NK_DATAGRAM_MAX];
    uint32_t addr;
    uint16_t port;
    /* (what was waiting is old) */
    while (nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf)) > 0) {
    }
    for (int i = 0; i < s_server_count; i++) {
        NkLobbyListRequest req;
        memset(&req, 0, sizeof(req));
        header(&req.h, NK_LOBBY_LIST_REQUEST);
        nk_socket_send(s_servers[i].ip, s_servers[i].port, &req, sizeof(req));
    }
    if (s_lan) {
        NkLobbyLanDiscover disc;
        memset(&disc, 0, sizeof(disc));
        header(&disc.h, NK_LOBBY_LAN_DISCOVER);
        nk_socket_send(0xFFFFFFFFu, NK_GAME_PORT, &disc, sizeof(disc));
    }

    uint32_t start = nk_time_ms();
    while (nk_time_ms() - start < wait_ms) {
        int n = nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf));
        if (n <= 0) {
            sleep_ms(10);
            continue;
        }
        if (valid(buf, n, (int) sizeof(NkLobbyListReply) - (int) sizeof(((NkLobbyListReply*) 0)->entries),
                  NK_LOBBY_LIST_REPLY) &&
            server_of(addr, port) >= 0)
        {
            const NkLobbyListReply* r = (const NkLobbyListReply*) buf;
            int k = r->count <= NK_LOBBY_LIST_MAX ? r->count : NK_LOBBY_LIST_MAX;
            for (int i = 0; i < k && (int) (offsetof(NkLobbyListReply, entries) + (i + 1) * sizeof(NkLobbyEntry)) <= n; i++) {
                NkLobbyInfo info;
                memset(&info, 0, sizeof(info));
                info.entry = r->entries[i];
                info.server = server_of(addr, port);
                add(out, &count, max, &info);
            }
        } else if (s_lan && valid(buf, n, sizeof(NkLobbyLanInfo), NK_LOBBY_LAN_INFO)) {
            const NkLobbyLanInfo* r = (const NkLobbyLanInfo*) buf;
            NkLobbyInfo info;
            memset(&info, 0, sizeof(info));
            info.entry = r->entry;
            info.lan = 1;
            info.server = -1;
            info.addr = addr;
            info.port = r->game_port;
            add(out, &count, max, &info);
        }
    }
    return count;
}

/* ---- joining -------------------------------------------------------------- */

static void upper_code(char* out, const char* code)
{
    memset(out, 0, NK_LOBBY_CODE_LEN);
    for (int i = 0; code != NULL && code[i] != '\0' && i < NK_LOBBY_CODE_LEN - 1; i++) {
        out[i] = (char) toupper((unsigned char) code[i]);
    }
}

int nk_lobby_join(const NkLobbyInfo* lobby, const char* code, NkJoinTarget* target, uint32_t wait_ms)
{
    uint8_t buf[NK_DATAGRAM_MAX];
    uint32_t addr;
    uint16_t port;
    memset(target, 0, sizeof(*target));
    if (lobby != NULL && lobby->lan) {
        /* the host is on this network: straight to it */
        target->entry = lobby->entry;
        target->local_ip = target->public_ip = lobby->addr;
        target->local_port = target->public_port = lobby->port;
        return 0;
    }
    while (nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf)) > 0) {
    }
    NkLobbyJoinRequest req;
    memset(&req, 0, sizeof(req));
    header(&req.h, NK_LOBBY_JOIN_REQUEST);
    req.lobby_id = lobby != NULL ? lobby->entry.lobby_id : 0;
    upper_code(req.code, code);
    req.local_ip = nk_local_ip();
    req.local_port = nk_socket_port();

    /* A private lobby on this network answers its code too */
    if (s_lan && lobby == NULL && code != NULL && code[0] != '\0') {
        NkLobbyLanDiscover disc;
        memset(&disc, 0, sizeof(disc));
        header(&disc.h, NK_LOBBY_LAN_DISCOVER);
        upper_code(disc.code, code);
        nk_socket_send(0xFFFFFFFFu, NK_GAME_PORT, &disc, sizeof(disc));
    }
    /* A listed lobby: the server that listed it. A code: every server (only
     * the one that gave the code knows it) */
    uint8_t asked[NK_SERVERS_MAX] = {0};
    int asking = 0;
    for (int i = 0; i < s_server_count; i++) {
        if (lobby == NULL || lobby->server == i) {
            asked[i] = 1;
            asking++;
        }
    }
    int refused = 0, last_refusal = NK_JOIN_NOT_FOUND;
    uint32_t start = nk_time_ms(), last_send = 0;
    int sends = 0;
    while (nk_time_ms() - start < wait_ms) {
        if (asking > 0 && (sends == 0 || (nk_time_ms() - last_send > 700 && sends < 4))) {
            for (int i = 0; i < s_server_count; i++) {
                if (asked[i] == 1) {
                    nk_socket_send(s_servers[i].ip, s_servers[i].port, &req, sizeof(req));
                }
            }
            last_send = nk_time_ms();
            sends++;
        }
        int n = nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf));
        if (n <= 0) {
            sleep_ms(10);
            continue;
        }
        int from = server_of(addr, port);
        if (valid(buf, n, sizeof(NkLobbyJoinInfo), NK_LOBBY_JOIN_INFO) && from >= 0 && asked[from] == 1) {
            const NkLobbyJoinInfo* r = (const NkLobbyJoinInfo*) buf;
            if (r->result != NK_JOIN_OK) {
                /* this server doesn't have it; another may */
                asked[from] = 2;
                last_refusal = r->result;
                if (++refused == asking) {
                    return last_refusal;
                }
                continue;
            }
            target->entry = r->entry;
            target->public_ip = r->public_ip;
            target->public_port = r->public_port;
            target->local_ip = r->local_ip;
            target->local_port = r->local_port;
            return 0;
        }
        if (s_lan && valid(buf, n, sizeof(NkLobbyLanInfo), NK_LOBBY_LAN_INFO)) {
            const NkLobbyLanInfo* r = (const NkLobbyLanInfo*) buf;
            target->entry = r->entry;
            target->public_ip = target->local_ip = addr;
            target->public_port = target->local_port = r->game_port;
            return 0;
        }
    }
    return asking > refused ? -1 : last_refusal;
}

void nk_lobby_punch(const NkJoinTarget* target)
{
    NkLobbyPunch p;
    memset(&p, 0, sizeof(p));
    header(&p.h, NK_LOBBY_PUNCH);
    p.lobby_id = target->entry.lobby_id;
    nk_socket_send(target->public_ip, target->public_port, &p, sizeof(p));
    if (target->local_ip != target->public_ip && target->local_ip != 0) {
        nk_socket_send(target->local_ip, target->local_port, &p, sizeof(p));
    }
}

/* ---- hosting -------------------------------------------------------------- */

void nk_lobby_host_begin(const NkLobbyEntry* entry)
{
    s_entry = *entry;
    s_hosting = 1;
    s_code[0] = '\0';
    s_local_code[0] = '\0';
    for (int i = 0; i < s_server_count; i++) {
        s_servers[i].lobby_id = 0;
        s_servers[i].token = 0;
        s_servers[i].code[0] = '\0';
    }
    if (s_server_count == 0) {
        /* no server to give one: the local network's own code */
        static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        for (int i = 0; i < 6; i++) {
            s_local_code[i] = chars[nk_random() % (sizeof(chars) - 1)];
        }
        s_local_code[6] = '\0';
        memcpy(s_code, s_local_code, sizeof(s_code));
    }
    s_last_register = nk_time_ms() - NK_LOBBY_HEARTBEAT_MS;
}

void nk_lobby_host_set_players(int players)
{
    s_entry.players = (uint8_t) players;
    /* tell the server at the next poll */
    s_last_register = nk_time_ms() - NK_LOBBY_HEARTBEAT_MS;
}

const char* nk_lobby_host_code(void)
{
    return s_code;
}

int nk_lobby_host_registered(void)
{
    for (int i = 0; i < s_server_count; i++) {
        if (s_servers[i].lobby_id != 0) {
            return 1;
        }
    }
    return 0;
}

/* The lobby's code on a server, or the local one */
static int is_our_code(const char* code)
{
    if (code[0] == '\0') {
        return 0;
    }
    if (strcmp(code, s_local_code) == 0) {
        return 1;
    }
    for (int i = 0; i < s_server_count; i++) {
        if (strcmp(code, s_servers[i].code) == 0) {
            return 1;
        }
    }
    return 0;
}

static void lan_answer(uint32_t addr, uint16_t port, const NkLobbyLanDiscover* d)
{
    char code[NK_LOBBY_CODE_LEN + 1];
    memcpy(code, d->code, NK_LOBBY_CODE_LEN);
    code[NK_LOBBY_CODE_LEN] = '\0';
    /* a private lobby answers its own code only (and before the server gave
     * one, nothing: it has no code yet) */
    if (!s_lan || ((s_entry.flags & NK_LOBBY_PRIVATE) && !is_our_code(code))) {
        return;
    }
    NkLobbyLanInfo info;
    memset(&info, 0, sizeof(info));
    header(&info.h, NK_LOBBY_LAN_INFO);
    info.game_port = NK_GAME_PORT;
    info.entry = s_entry;
    info.entry.lobby_id = 0;
    nk_socket_send(addr, port, &info, sizeof(info));
}

void nk_lobby_host_poll(void)
{
    uint8_t buf[NK_DATAGRAM_MAX];
    uint32_t addr;
    uint16_t port;
    int n;
    if (!s_hosting) {
        /* (a guest drops the host's punches) */
        while (nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf)) > 0) {
        }
        return;
    }
    while ((n = nk_socket_receive_lobby(&addr, &port, buf, sizeof(buf))) > 0) {
        int from = server_of(addr, port);
        if (valid(buf, n, sizeof(NkLobbyRegistered), NK_LOBBY_REGISTERED) && from >= 0) {
            const NkLobbyRegistered* r = (const NkLobbyRegistered*) buf;
            Server* sv = &s_servers[from];
            if (sv->lobby_id != r->lobby_id) {
                WL_LOG_I("lobby", "registered on %s as lobby %u, code %.6s", sv->name, (unsigned) r->lobby_id,
                         r->code);
            }
            sv->lobby_id = r->lobby_id;
            sv->token = r->token;
            memcpy(sv->code, r->code, NK_LOBBY_CODE_LEN);
            sv->code[NK_LOBBY_CODE_LEN] = '\0';
            /* the code shown: the first server's in the list that gave one */
            for (int i = 0; i < s_server_count; i++) {
                if (s_servers[i].code[0] != '\0') {
                    memcpy(s_code, s_servers[i].code, sizeof(s_code));
                    break;
                }
            }
        } else if (valid(buf, n, sizeof(NkLobbyPunchRequest), NK_LOBBY_PUNCH_REQUEST) && from >= 0) {
            const NkLobbyPunchRequest* r = (const NkLobbyPunchRequest*) buf;
            NkJoinTarget guest;
            memset(&guest, 0, sizeof(guest));
            guest.entry.lobby_id = s_servers[from].lobby_id;
            guest.public_ip = r->public_ip;
            guest.public_port = r->public_port;
            guest.local_ip = r->local_ip;
            guest.local_port = r->local_port;
            /* a few, as the first ones may be eaten by the guest's router */
            for (int i = 0; i < 3; i++) {
                nk_lobby_punch(&guest);
            }
        } else if (valid(buf, n, sizeof(NkLobbyLanDiscover), NK_LOBBY_LAN_DISCOVER)) {
            lan_answer(addr, port, (const NkLobbyLanDiscover*) buf);
        }
    }
    if (s_server_count > 0 && nk_time_ms() - s_last_register >= NK_LOBBY_HEARTBEAT_MS) {
        for (int i = 0; i < s_server_count; i++) {
            NkLobbyRegister reg;
            memset(&reg, 0, sizeof(reg));
            header(&reg.h, NK_LOBBY_HOST_REGISTER);
            reg.token = s_servers[i].token;
            reg.local_ip = nk_local_ip();
            reg.game_port = NK_GAME_PORT;
            reg.entry = s_entry;
            reg.entry.lobby_id = s_servers[i].lobby_id;
            nk_socket_send(s_servers[i].ip, s_servers[i].port, &reg, sizeof(reg));
        }
        s_last_register = nk_time_ms();
    }
}

void nk_lobby_host_end(void)
{
    for (int i = 0; s_hosting && i < s_server_count; i++) {
        if (s_servers[i].lobby_id == 0) {
            continue;
        }
        NkLobbyClose c;
        memset(&c, 0, sizeof(c));
        header(&c.h, NK_LOBBY_HOST_CLOSE);
        c.lobby_id = s_servers[i].lobby_id;
        c.token = s_servers[i].token;
        nk_socket_send(s_servers[i].ip, s_servers[i].port, &c, sizeof(c));
    }
    s_hosting = 0;
}
