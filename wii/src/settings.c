/* Nookery's gameplay settings (pc/include/pc_settings.h) on the Wii:
 * sd:/apps/nookery/settings.ini, "key = value" lines, read at the start and
 * written by the launcher's settings screen. The PC port keeps the same
 * fields in its config.json; the ones about windows, renderers and the mouse
 * don't apply here and keep their defaults. */

#include "nk_wii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_settings.h"

#define SETTINGS_PATH NK_WII_DIR "/settings.ini"

PCSettings g_pc_settings = {
    .window_width = 640,
    .window_height = 480,
    .vsync = 1,
    .max_fps = 60,
    .disable_resetti = 0,
    .disable_shop_visitor_req = 0,
    .borderless_acres = 0,
    .nes_aspect = 1,
    .master_volume = 100,
    .stick_deadzone = 12,
    .cstick_deadzone = 12,
    .fast_text = 0,
    .travel_move_out_chance = 100,
    .hud_at_edges = 0,
    .villager_life = 1,
    .town_life = 1,
};

/* The PC port's command-line overrides, here never set */
int g_pc_verbose = 0;
int g_pc_running = 1;
int g_pc_frame_limit_override = -1;
int g_pc_speedhack_enabled = 0;
int g_pc_time_override = -1;
int g_pc_min_override = -1;
int g_pc_sec_override = -1;
int g_pc_date_month = -1;
int g_pc_date_day = -1;
int g_pc_date_year = -1;
int g_pc_weather_override = -1;
int g_pc_weather_intensity_override = 2;

/* The online play's: the name others see, the lobby server ("host[:port]",
 * "" for the local network only), the last lobby hosted */
char g_nk_online_name[16] = "Villager";
char g_nk_lobby_server[96] = "";
char g_nk_lobby_name[32] = "";
int g_nk_lobby_private = 0;
int g_nk_lobby_max_players = 4;
/* A test: the local player shown again as a remote one, beside them */
int g_nk_net_echo = 0;
/* A test: the title screen's demo counts as the town (two consoles at the
 * title see each other) */
int g_nk_net_title_town = 0;
/* A test: each scene the game enters, in the log */
int g_nk_trace_scenes = 0;
/* A test: the NES furniture's game of this number (1 on) starts from the
 * town, as if played on the furniture */
int g_nk_nes_test = 0;
/* A test: two seconds on the town's streets, the station master's journey
 * to the town in slot B (his card check, the save, the train ride), as if
 * asked for at the station */
int g_nk_travel_test = 0;
/* life_test = 1, 2: villager friendships tests (nk_netgame.c life_test); 3, 4: town life (town_test) */
int g_nk_life_test = 0;
/* life_test_topic = N: only gossip topic N (99: each news, fan and moving topic in turn) */
int g_nk_life_test_topic = -1;
/* Online: the host's town clock, while a guest is in the host's town
 * (wii/src/netgame/nk_netgame.c; src/lb_rtc.c reads it): its game time is
 * the hardware clock plus g_nk_clock_offset, not the save's time_delta */
int g_nk_clock_active = 0;
long long g_nk_clock_offset = 0;
/* A test: this console's clock as if set this many seconds off */
int g_nk_clock_test_skew = 0;
/* A test: a guest tries the host's public address first, even on the same
 * network (the route two networks take) */
int g_nk_net_test_public = 0;
/* Widescreen: 1 follows Wii Settings (16:9 there: the game drawn wide, as
 * the PC port's hor+), 0 keeps 4:3 (wii/src/widescreen.c) */
int g_nk_widescreen = 1;
/* Wii Remote 1's pointer as the PC port's mouse in the game (ogc/pointer.c) */
int g_nk_wii_remote_pointer = 1;

typedef struct StrKey {
    const char* name;
    char* field;
    int size;
} StrKey;

static const StrKey s_str_keys[] = {
    {"online_name", g_nk_online_name, sizeof(g_nk_online_name)},
    {"lobby_server", g_nk_lobby_server, sizeof(g_nk_lobby_server)},
    {"lobby_name", g_nk_lobby_name, sizeof(g_nk_lobby_name)},
};

typedef struct Key {
    const char* name;
    int* field;
    int min, max;
} Key;

static const Key s_keys[] = {
    {"fast_text", &g_pc_settings.fast_text, 0, 1},
    {"disable_resetti", &g_pc_settings.disable_resetti, 0, 1},
    {"villager_life", &g_pc_settings.villager_life, 0, 1},
    {"town_life", &g_pc_settings.town_life, 0, 1},
    {"disable_shop_visitor_req", &g_pc_settings.disable_shop_visitor_req, 0, 1},
    {"travel_move_out_chance", &g_pc_settings.travel_move_out_chance, 10, 100},
    {"nes_aspect", &g_pc_settings.nes_aspect, 0, 1},
    {"widescreen", &g_nk_widescreen, 0, 1},
    {"wii_remote_pointer", &g_nk_wii_remote_pointer, 0, 1},
    {"hud_at_edges", &g_pc_settings.hud_at_edges, 0, 1},
    {"master_volume", &g_pc_settings.master_volume, 0, 100},
    {"stick_deadzone", &g_pc_settings.stick_deadzone, 0, 40},
    {"cstick_deadzone", &g_pc_settings.cstick_deadzone, 0, 40},
    {"verbose", &g_pc_verbose, 0, 1},
    {"lobby_private", &g_nk_lobby_private, 0, 1},
    {"lobby_max_players", &g_nk_lobby_max_players, 2, 4},
    {"net_echo", &g_nk_net_echo, 0, 1},
    {"net_title_town", &g_nk_net_title_town, 0, 1},
    {"trace_scenes", &g_nk_trace_scenes, 0, 1},
    {"nes_test", &g_nk_nes_test, 0, 30},
    {"travel_test", &g_nk_travel_test, 0, 1},
    {"life_test", &g_nk_life_test, 0, 6},
    {"life_test_topic", &g_nk_life_test_topic, -1, 99},
    {"clock_test_skew", &g_nk_clock_test_skew, -864000, 864000},
    {"net_test_public", &g_nk_net_test_public, 0, 1},
};

void nk_wii_settings_load(void)
{
    FILE* f = fopen(SETTINGS_PATH, "r");
    char line[160];
    if (f == NULL) {
        nk_wii_settings_save();
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[64];
        char text[128];
        int value;
        if (line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (sscanf(line, " %63[a-z_] = %127[^\r\n]", key, text) == 2) {
            for (size_t i = 0; i < sizeof(s_str_keys) / sizeof(s_str_keys[0]); i++) {
                if (strcmp(key, s_str_keys[i].name) == 0) {
                    snprintf(s_str_keys[i].field, s_str_keys[i].size, "%s", text);
                }
            }
        }
        if (sscanf(line, " %63[a-z_] = %d", key, &value) != 2) {
            continue;
        }
        for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++) {
            if (strcmp(key, s_keys[i].name) == 0 && value >= s_keys[i].min && value <= s_keys[i].max) {
                *s_keys[i].field = value;
            }
        }
    }
    fclose(f);
    if (g_pc_settings.travel_move_out_chance != 10 && g_pc_settings.travel_move_out_chance != 50) {
        g_pc_settings.travel_move_out_chance = 100;
    }
}

void nk_wii_settings_save(void)
{
    FILE* f = fopen(SETTINGS_PATH, "w");
    if (f == NULL) {
        return;
    }
    fputs("# Nookery for the Wii: gameplay settings (the launcher's Settings screen writes them)\n", f);
    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++) {
        fprintf(f, "%s = %d\n", s_keys[i].name, *s_keys[i].field);
    }
    for (size_t i = 0; i < sizeof(s_str_keys) / sizeof(s_str_keys[0]); i++) {
        fprintf(f, "%s = %s\n", s_str_keys[i].name, s_str_keys[i].field);
    }
    fclose(f);
}

/* The PC's hooks the shared code calls */
void pc_settings_load(void) { nk_wii_settings_load(); }
void pc_settings_save(void) { nk_wii_settings_save(); }
void pc_settings_apply(void) {}
