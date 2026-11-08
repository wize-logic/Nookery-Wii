/* The launcher's online screens: the lobby browser (the lobby server's public
 * lobbies and those on the local network), hosting a lobby of your own town
 * (named, public or private, with or without a password, 2 to 4 players), and
 * joining one, from the list or a private lobby's code. Returns to the
 * launcher (1) once a session is set up: the game then starts, with the
 * session's thread running beside it. */

#include "ui.h"

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "nk_lobby.h"
#include "nk_net.h"
#include "nk_servers.h"
#include "nk_socket.h"

void nk_wii_settings_save(void);
extern char g_nk_online_name[16];
extern char g_nk_lobby_name[32];
extern int g_nk_lobby_private;
extern int g_nk_lobby_max_players;

#define TOWN_DIR "sd:/apps/nookery/net/town"
#define LIST_MAX 24

static NkLobbyInfo s_list[LIST_MAX];
static int s_list_count;

static void wait_screen(const char* heading, const char* text)
{
    ui_background(heading);
    gfx_round_rect(ui_left() + 40, 180, 560, 80, 12, UI_PANEL, 220);
    gfx_text_centered(&font_body, ui_left() + 320, 206, UI_WHITE, text);
    ui_footer("Please wait...");
    ui_present();
}

/* The network and the socket: 0, or a message shown and -1 */
static int start_network(int hosting)
{
    wait_screen("Online", "Connecting to the network...");
    if (nk_lobby_start(nk_servers_joined(), hosting, 20000) < 0) {
        ui_message("Online",
                   "The Wii could not connect to the network.\n\n"
                   "Check the connection in Wii Settings > Internet (a connection test there helps), "
                   "then try again.",
                   "A / B: Back");
        return -1;
    }
    return 0;
}

static void entry_line(const NkLobbyInfo* l, char* out, int size)
{
    snprintf(out, size, "%.32s", l->entry.name[0] != '\0' ? l->entry.name : "(unnamed lobby)");
}

static void entry_detail(const NkLobbyInfo* l, char* out, int size)
{
    snprintf(out, size, "%s%s %d/%d", l->lan ? "LAN  " : "", (l->entry.flags & NK_LOBBY_PASSWORD) ? "Locked " : "",
             (int) l->entry.players, (int) l->entry.max_players);
}

/* Connects to the host and fetches its town, with a progress screen; 1 when
 * the session is ready */
static int join_target(const NkJoinTarget* target)
{
    char password[32] = "";
    if (target->entry.flags & NK_LOBBY_PASSWORD) {
        if (!ui_keyboard("Join", "This lobby asks for a password:", password, sizeof(password), 1)) {
            return 0;
        }
    }
    if (nk_net_join_start(target->public_ip, target->public_port, target->local_ip, target->local_port,
                          g_nk_online_name, password, TOWN_DIR) < 0)
    {
        ui_message("Join", nk_net_error(), "A / B: Back");
        nk_net_stop();
        return 0;
    }
    for (;;) {
        unsigned int pressed = input_poll();
        int status = nk_net_status();
        char line[128];
        ui_background("Join");
        gfx_round_rect(ui_left() + 40, 120, 560, 240, 12, UI_PANEL, 220);
        snprintf(line, sizeof(line), "%.32s", target->entry.name);
        gfx_text(&font_body_bold, ui_left() + 62, 136, UI_SAND, line);
        snprintf(line, sizeof(line), "%.16s's town %.16s", target->entry.host, target->entry.town);
        gfx_text(&font_small, ui_left() + 62, 170, UI_GREY, line);
        const char* what = status == NK_NET_CONNECTING  ? "Reaching the host..."
                           : status == NK_NET_HANDSHAKE ? "Saying hello..."
                           : status == NK_NET_TOWN      ? "Receiving the town..."
                                                        : "";
        gfx_text(&font_body, ui_left() + 62, 214, UI_WHITE, what);
        if (status == NK_NET_TOWN) {
            uint32_t got, total;
            nk_net_town_progress(&got, &total);
            int w = total > 0 ? (int) ((uint64_t) got * 516 / total) : 0;
            gfx_round_rect(ui_left() + 62, 252, 516, 18, 9, UI_DARK, 255);
            gfx_round_rect(ui_left() + 62, 252, w > 18 ? w : 18, 18, 9, UI_LEAF, 255);
        }
        ui_footer("B: Cancel");
        ui_present();
        if (status == NK_NET_READY) {
            ui_message("Joined!",
                       "The host's town is ready in memory card slot B.\n\n"
                       "Start your own game, walk to your town's train station and tell the station "
                       "master you want to travel: the train takes you to the host's town, where you "
                       "will see each other.",
                       "A: Start the game");
            return 1;
        }
        if (status == NK_NET_FAILED) {
            ui_message("Join", nk_net_error(), "A / B: Back");
            nk_net_stop();
            return 0;
        }
        if (pressed & (IN_B | IN_HOME)) {
            nk_net_stop();
            return 0;
        }
    }
}

static int join_with_code(void)
{
    char code[8] = "";
    if (!ui_keyboard("Join a private lobby", "The lobby's code (six letters and digits):", code, sizeof(code), 0)) {
        return 0;
    }
    if (start_network(0) < 0) {
        return 0;
    }
    wait_screen("Join", "Looking for the lobby...");
    NkJoinTarget target;
    int r = nk_lobby_join(NULL, code, &target, 4000);
    if (r != 0) {
        ui_message("Join",
                   r == NK_JOIN_FULL ? "That lobby is full."
                   : r == NK_JOIN_NOT_FOUND
                       ? "No lobby has that code (on the lobby server or this network)."
                       : "The lobby server didn't answer.",
                   "A / B: Back");
        return 0;
    }
    return join_target(&target);
}

static int browse(void)
{
    int cursor = 0;
    if (start_network(0) < 0) {
        return 0;
    }
    wait_screen("Lobbies", "Looking for lobbies...");
    s_list_count = nk_lobby_refresh(s_list, LIST_MAX, 1500);
    for (;;) {
        unsigned int pressed = input_poll();
        int x = ui_left();
        ui_background("Lobbies");
        if (s_list_count == 0) {
            gfx_round_rect(x + 40, 120, 560, 120, 12, UI_PANEL, 220);
            gfx_text_centered(&font_body, x + 320, 142, UI_WHITE, "No lobbies right now.");
            gfx_text_centered(&font_small, x + 320, 178, UI_GREY,
                              nk_servers_count() > 0 ? "Host one, or look again in a moment."
                                                     : "No lobby servers on the list: only this network is searched.");
        } else {
            if (pressed & IN_UP) {
                cursor = (cursor + s_list_count - 1) % s_list_count;
            }
            if (pressed & IN_DOWN) {
                cursor = (cursor + 1) % s_list_count;
            }
            int first = cursor >= 6 ? cursor - 5 : 0;
            int pointed = ui_pointed_row(112, 50, 44, s_list_count - first < 6 ? s_list_count - first : 6, 40, 600);
            if (pointed >= 0) {
                cursor = first + pointed;
            }
            for (int i = first; i < s_list_count && i < first + 6; i++) {
                int y = 112 + (i - first) * 50, on = i == cursor;
                char name[64], detail[64], host[64];
                entry_line(&s_list[i], name, sizeof(name));
                entry_detail(&s_list[i], detail, sizeof(detail));
                snprintf(host, sizeof(host), "%.16s  \xC2\xB7  %.16s", s_list[i].entry.host, s_list[i].entry.town);
                gfx_round_rect(x + 40, y, 560, 44, 10, on ? UI_PANEL_LIGHT : UI_PANEL, on ? 255 : 200);
                gfx_text(&font_body_bold, x + 58, y + 4, on ? UI_WHITE : UI_GREY, name);
                gfx_text(&font_small, x + 58, y + 24, UI_GREY, host);
                int w = gfx_text_width(&font_small, detail);
                gfx_text(&font_small, x + 584 - w, y + 14, on ? UI_SAND : UI_GREY, detail);
            }
        }
        ui_footer("A: Join    X/+: Look again    B: Back");
        ui_present();
        if (pressed & (IN_B | IN_HOME)) {
            return 0;
        }
        if (pressed & IN_X) {
            wait_screen("Lobbies", "Looking for lobbies...");
            s_list_count = nk_lobby_refresh(s_list, LIST_MAX, 1500);
            cursor = 0;
        }
        if ((pressed & IN_A) && s_list_count > 0) {
            NkJoinTarget target;
            wait_screen("Join", "Asking the lobby server...");
            int r = nk_lobby_join(&s_list[cursor], NULL, &target, 4000);
            if (r != 0) {
                ui_message("Join", r == NK_JOIN_FULL ? "That lobby is full." : "That lobby is gone.", "A / B: Back");
                continue;
            }
            if (join_target(&target)) {
                return 1;
            }
        }
    }
}

/* ---- hosting ------------------------------------------------------------------ */

enum { H_NAME, H_VISIBILITY, H_PASSWORD, H_PLAYERS, H_START, H_COUNT };

typedef struct HostForm {
    char password[32];
} HostForm;

static const char* host_detail(int item, void* user)
{
    static char buf[64];
    HostForm* f = user;
    switch (item) {
    case H_NAME:
        return g_nk_lobby_name[0] != '\0' ? g_nk_lobby_name : "(your name's town)";
    case H_VISIBILITY:
        return g_nk_lobby_private ? "Private (by code)" : "Public (listed)";
    case H_PASSWORD:
        return f->password[0] != '\0' ? "Set" : "None";
    case H_PLAYERS:
        snprintf(buf, sizeof(buf), "%d", g_nk_lobby_max_players);
        return buf;
    default:
        return NULL;
    }
}

static int host(void)
{
    static const char* const items[H_COUNT] = {"Lobby name", "Who can see it", "Password", "Players (with you)",
                                               "Open the lobby"};
    HostForm form;
    int cursor = 0;
    memset(&form, 0, sizeof(form));
    for (;;) {
        unsigned int pressed = input_poll();
        ui_background("Host your town");
        int chosen = ui_menu(items, H_COUNT, &cursor, 112, host_detail, &form, pressed);
        gfx_text(&font_small, ui_left() + 42, 352, UI_GREY,
                 "Guests get your town as you last saved it, and visit it by train.");
        ui_footer("A: Change    B: Back");
        ui_present();
        if (chosen == -1) {
            return 0;
        }
        if (chosen == H_NAME) {
            ui_keyboard("Lobby name", "What your lobby is called in the list:", g_nk_lobby_name,
                        sizeof(g_nk_lobby_name), 0);
        } else if (chosen == H_VISIBILITY) {
            g_nk_lobby_private ^= 1;
        } else if (chosen == H_PASSWORD) {
            ui_keyboard("Password", "A password for guests (empty: none):", form.password, sizeof(form.password), 1);
        } else if (chosen == H_PLAYERS) {
            g_nk_lobby_max_players = g_nk_lobby_max_players >= 4 ? 2 : g_nk_lobby_max_players + 1;
        } else if (chosen == H_START) {
            nk_wii_settings_save();
            if (start_network(1) < 0) {
                continue;
            }
            NkHostConfig c;
            memset(&c, 0, sizeof(c));
            snprintf(c.lobby_name, sizeof(c.lobby_name), "%s",
                     g_nk_lobby_name[0] != '\0' ? g_nk_lobby_name : g_nk_online_name);
            snprintf(c.password, sizeof(c.password), "%s", form.password);
            c.private_lobby = g_nk_lobby_private;
            c.max_players = g_nk_lobby_max_players;
            strncpy(c.player_name, g_nk_online_name, sizeof(c.player_name));
            if (nk_net_host_start(&c, nk_servers_joined()) < 0) {
                ui_message("Host your town", nk_net_error(), "A / B: Back");
                nk_net_stop();
                continue;
            }
            /* the server's code, when there is a server (a few seconds) */
            uint32_t start = nk_time_ms();
            while (nk_servers_count() > 0 && nk_net_host_code()[0] == '\0' && nk_time_ms() - start < 4000) {
                wait_screen("Host your town", "Announcing your lobby...");
            }
            char text[400];
            const char* code = nk_net_host_code();
            snprintf(text, sizeof(text),
                     "Your lobby is open.\n\n%s%s%s\n\n"
                     "It stays open while you play. Guests join from their own Wii's Online screen%s, "
                     "then come to your town by train.",
                     code[0] != '\0' ? "Its code: " : "", code[0] != '\0' ? code : "",
                     code[0] != '\0' ? "  (for a private lobby, give it to your friends)"
                                     : (nk_servers_count() > 0 ? "No lobby server answered: "
                                                                 "only this network can see it."
                                                               : "No lobby servers on the list: only this "
                                                                 "network can see it."),
                     g_nk_lobby_private ? " (Join with a code)" : "");
            ui_message("Host your town", text, "A: Start the game");
            return 1;
        }
    }
}

/* ---- the lobby servers ---------------------------------------------------------
 * The list in sd:/apps/nookery/lobby_servers.txt (nk_servers.h): every server
 * on it is searched, and a hosted town is announced on every one. */

static const char* servers_detail(int item, void* user)
{
    return item < *(const int*) user ? "Remove" : NULL;
}

static void servers_screen(void)
{
    int cursor = 0;
    for (;;) {
        int n = nk_servers_count();
        const char* items[NK_SERVERS_MAX + 3];
        for (int i = 0; i < n; i++) {
            items[i] = nk_servers_get(i);
        }
        items[n] = "Add a server";
        items[n + 1] = "Reset to the default list";
        items[n + 2] = "Back";
        if (cursor > n + 2) {
            cursor = n + 2;
        }
        unsigned int pressed = input_poll();
        ui_background("Lobby servers");
        int chosen = ui_menu_rows(items, n + 3, &cursor, 104, 30, servers_detail, &n, pressed);
        int hint_y = 104 + (n + 3) * 30 + 6;
        if (hint_y + 16 < 444) {
            gfx_text(&font_small, ui_left() + 46, hint_y, UI_GREY,
                     n == 0 ? "No servers: only lobbies on this network are found."
                            : "Every server here is searched, and a town you host is announced on all.");
        }
        ui_footer("A: Choose    B: Back    (sd:/apps/nookery/lobby_servers.txt)");
        ui_present();
        if (chosen == -2) {
            continue;
        }
        if (chosen == -1 || chosen == n + 2) {
            return;
        }
        if (chosen < n) {
            char text[200];
            snprintf(text, sizeof(text), "Remove %s from the list of lobby servers?", nk_servers_get(chosen));
            if (ui_message("Lobby servers", text, "A: Remove    B: Keep it")) {
                nk_servers_remove(chosen);
            }
        } else if (chosen == n) {
            if (n >= NK_SERVERS_MAX) {
                ui_message("Lobby servers", "The list is full: remove a server first.", "A / B: Back");
                continue;
            }
            char address[NK_SERVER_NAME_LEN] = "";
            if (ui_keyboard("Add a lobby server", "Its address (a name or an IP address, :port when not 28910):",
                            address, sizeof(address), 0) &&
                address[0] != '\0' && nk_servers_add(address) < 0)
            {
                ui_message("Lobby servers", "That server is already on the list.", "A / B: Back");
            }
        } else if (chosen == n + 1) {
            if (ui_message("Lobby servers",
                           "Put back the default list of lobby servers? The servers you added are removed.",
                           "A: Reset    B: Back"))
            {
                nk_servers_reset();
                cursor = 0;
            }
        }
    }
}

/* ---- the online menu ---------------------------------------------------------- */

enum { O_BROWSE, O_HOST, O_CODE, O_NAME, O_SERVER, O_BACK, O_COUNT };

static const char* online_detail(int item, void* user)
{
    (void) user;
    switch (item) {
    case O_NAME:
        return g_nk_online_name;
    case O_SERVER: {
        static char text[48];
        int n = nk_servers_count();
        if (n == 0) {
            return "None (this network only)";
        }
        if (n == 1) {
            snprintf(text, sizeof(text), "%.40s", nk_servers_get(0));
        } else {
            snprintf(text, sizeof(text), "%d servers", n);
        }
        return text;
    }
    default:
        return NULL;
    }
}

int ui_online(void)
{
    static const char* const items[O_COUNT] = {"Browse lobbies", "Host your town", "Join with a code",
                                               "Your online name", "Lobby servers", "Back"};
    int cursor = 0;
    for (;;) {
        unsigned int pressed = input_poll();
        ui_background("Online");
        int chosen = ui_menu(items, O_COUNT, &cursor, 112, online_detail, NULL, pressed);
        ui_footer("A: Choose    B: Back");
        ui_present();
        switch (chosen) {
        case O_BROWSE:
            if (browse()) {
                return 1;
            }
            break;
        case O_HOST:
            if (host()) {
                return 1;
            }
            break;
        case O_CODE:
            if (join_with_code()) {
                return 1;
            }
            break;
        case O_NAME:
            if (ui_keyboard("Online name", "The name other players see:", g_nk_online_name,
                            sizeof(g_nk_online_name), 0))
            {
                nk_wii_settings_save();
            }
            break;
        case O_SERVER:
            servers_screen();
            break;
        case O_BACK:
        case -1:
            return 0;
        }
    }
}

/* ---- unattended tests ----------------------------------------------------------
 * sd:/apps/nookery/nettest.txt, one line: "host [private] [password]" opens a
 * lobby; "join first [password]" joins the first lobby the browser lists;
 * "join server [password]" the first a lobby server lists, with the local
 * network's broadcasts off (a test of the servers' introduction); "join CODE
 * [password]" joins by code. The launcher then starts the game
 * (the results are in the log). */
#define NETTEST_PATH "sd:/apps/nookery/nettest.txt"

int ui_online_autotest(void)
{
    char line[96] = "", what[16] = "", arg[32] = "", password[32] = "";
    FILE* f = fopen(NETTEST_PATH, "r");
    if (f == NULL) {
        return 0;
    }
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);
    sscanf(line, "%15s %31s %31s", what, arg, password);
    if (strcmp(what, "host") == 0) {
        int priv = strcmp(arg, "private") == 0;
        if (!priv && arg[0] != '\0') {
            snprintf(password, sizeof(password), "%s", arg);
        }
        if (nk_lobby_start(nk_servers_joined(), 1, 20000) < 0) {
            return 0;
        }
        NkHostConfig c;
        memset(&c, 0, sizeof(c));
        snprintf(c.lobby_name, sizeof(c.lobby_name), "Test lobby");
        snprintf(c.password, sizeof(c.password), "%s", password);
        c.private_lobby = priv;
        c.max_players = 4;
        strncpy(c.player_name, g_nk_online_name, sizeof(c.player_name));
        if (nk_net_host_start(&c, nk_servers_joined()) < 0) {
            return 0;
        }
        uint32_t start = nk_time_ms();
        while (nk_net_host_code()[0] == '\0' && nk_time_ms() - start < 5000) {
            nk_lobby_host_poll();
        }
        printf("NETTEST host: lobby open, code %s\n", nk_net_host_code());
        return 1;
    }
    if (strcmp(what, "join") == 0) {
        NkJoinTarget target;
        int r;
        if (nk_lobby_start(nk_servers_joined(), 0, 20000) < 0) {
            printf("NETTEST join: no network\n");
            return 0;
        }
        if (strcmp(arg, "server") == 0) {
            nk_lobby_set_lan(0); /* the lobby servers alone */
        }
        if (strcmp(arg, "first") == 0 || strcmp(arg, "server") == 0) {
            /* "server": the first lobby the lobby server lists, never one
             * found on this network (a test of the server's introduction) */
            int only_server = strcmp(arg, "server") == 0;
            int n = 0, pick = -1;
            for (int tries = 0; tries < 20 && pick < 0; tries++) {
                n = nk_lobby_refresh(s_list, LIST_MAX, 1500);
                for (int i = 0; i < n && pick < 0; i++) {
                    if (!only_server || !s_list[i].lan) {
                        pick = i;
                    }
                }
            }
            printf("NETTEST join: %d lobbies listed\n", n);
            if (pick < 0) {
                return 0;
            }
            printf("NETTEST join: lobby %d (%s)\n", pick, s_list[pick].lan ? "this network" : "the lobby server");
            r = nk_lobby_join(&s_list[pick], NULL, &target, 4000);
        } else {
            r = nk_lobby_join(NULL, arg, &target, 4000);
        }
        printf("NETTEST join: lobby answer %d (%.32s): host at %u.%u.%u.%u:%u, locally %u.%u.%u.%u:%u\n", r,
               target.entry.name, (unsigned) (target.public_ip >> 24), (unsigned) (target.public_ip >> 16) & 255,
               (unsigned) (target.public_ip >> 8) & 255, (unsigned) target.public_ip & 255,
               (unsigned) target.public_port, (unsigned) (target.local_ip >> 24),
               (unsigned) (target.local_ip >> 16) & 255, (unsigned) (target.local_ip >> 8) & 255,
               (unsigned) target.local_ip & 255, (unsigned) target.local_port);
        if (r != 0 || nk_net_join_start(target.public_ip, target.public_port, target.local_ip, target.local_port,
                                        g_nk_online_name, password, TOWN_DIR) < 0)
        {
            return 0;
        }
        uint32_t start = nk_time_ms();
        while (nk_net_status() != NK_NET_READY && nk_net_status() != NK_NET_FAILED &&
               nk_time_ms() - start < 60000)
        {
            struct timespec ts = {0, 50 * 1000000L};
            nanosleep(&ts, NULL);
        }
        printf("NETTEST join: %s %s\n", nk_net_status() == NK_NET_READY ? "READY" : "FAILED", nk_net_error());
        return nk_net_status() == NK_NET_READY;
    }
    return 0;
}
