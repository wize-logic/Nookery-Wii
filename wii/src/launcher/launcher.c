/* The launcher (main.c's nk_wii_launcher): the screens before the game.
 * Play (your own town, as the PC port starts it), Online (the lobby browser:
 * wii/src/launcher/ui_online.c), Settings, Exit. On the first frames the game
 * shows, the launcher's frame buffers (at the top of the game's arena) are
 * cleared with the rest of it. */

#include "ui.h"

#include <gccore.h>
#include <stdio.h>
#include <string.h>

#include "pc_settings.h"

void nk_wii_settings_save(void);
void nk_wii_exit(void);
const char* nk_wii_disc_path(void);
/* ui_online.c: 1 when an online session was set up (hosting or joined) */
#ifdef NOOKERY_WII_NET
int ui_online(void);
int ui_online_autotest(void);
#define HAVE_ONLINE 1
#else
#define HAVE_ONLINE 0
static int ui_online(void)
{
    return 0;
}
#endif

/* A held B (GameCube) as the program starts skips nothing; held Z goes
 * straight to the game */
static int skip_requested(void)
{
    PAD_ScanPads();
    for (int i = 0; i < 4; i++) {
        if (PAD_ButtonsHeld(i) & PAD_TRIGGER_Z) {
            return 1;
        }
    }
    return 0;
}

/* ---- settings -------------------------------------------------------------- */

typedef struct Toggle {
    const char* label;
    int* field;
    const int* values;
    const char* const* names;
    int count;
} Toggle;

static const int s_onoff[] = {0, 1};
static const char* const s_onoff_names[] = {"Off", "On"};
static const int s_move_out[] = {100, 50, 10};
static const char* const s_move_out_names[] = {"Default", "50%", "10%"};
static const int s_volume[] = {100, 75, 50, 25, 0};
static const char* const s_volume_names[] = {"100", "75", "50", "25", "Muted"};
static const int s_wide[] = {1, 0};
static const char* const s_wide_names[] = {"As Wii Settings", "Off (4:3)"};
/* settings.c: widescreen = 1 follows Wii Settings' 16:9 (wii/src/widescreen.c) */
extern int g_nk_widescreen;
/* settings.c: Wii Remote 1's pointer in the game (wii/src/ogc/pointer.c) */
extern int g_nk_wii_remote_pointer;

static Toggle s_toggles[] = {
    {"Fast text", &g_pc_settings.fast_text, s_onoff, s_onoff_names, 2},
    {"No Resetti", &g_pc_settings.disable_resetti, s_onoff, s_onoff_names, 2},
    {"Single-player shop upgrades", &g_pc_settings.disable_shop_visitor_req, s_onoff, s_onoff_names, 2},
    {"Villager follows traveler", &g_pc_settings.travel_move_out_chance, s_move_out, s_move_out_names, 3},
    {"Widescreen", &g_nk_widescreen, s_wide, s_wide_names, 2},
    {"Widescreen HUD at the edges", &g_pc_settings.hud_at_edges, s_onoff, s_onoff_names, 2},
    {"Wii Remote pointer in the game", &g_nk_wii_remote_pointer, s_onoff, s_onoff_names, 2},
    {"NES games in 4:3", &g_pc_settings.nes_aspect, s_onoff, s_onoff_names, 2},
    {"Volume", &g_pc_settings.master_volume, s_volume, s_volume_names, 5},
};
#define TOGGLE_COUNT ((int) (sizeof(s_toggles) / sizeof(s_toggles[0])))

static int toggle_index(const Toggle* t)
{
    for (int i = 0; i < t->count; i++) {
        if (*t->field == t->values[i]) {
            return i;
        }
    }
    return 0;
}

static const char* toggle_detail(int item, void* user)
{
    (void) user;
    if (item >= TOGGLE_COUNT) {
        return NULL;
    }
    return s_toggles[item].names[toggle_index(&s_toggles[item])];
}

void ui_settings(void)
{
    const char* items[TOGGLE_COUNT + 1];
    int cursor = 0;
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        items[i] = s_toggles[i].label;
    }
    items[TOGGLE_COUNT] = "Save and go back";
    for (;;) {
        unsigned int pressed = input_poll();
        ui_background("Settings");
        int chosen = ui_menu_rows(items, TOGGLE_COUNT + 1, &cursor, 104, 33, toggle_detail, NULL, pressed);
        ui_footer("A / Left / Right: Change    B: Back");
        ui_present();
        int step = (pressed & IN_LEFT) ? -1 : (pressed & IN_RIGHT) ? 1 : (chosen >= 0 ? 1 : 0);
        if (cursor < TOGGLE_COUNT && step != 0 && (chosen >= 0 || (pressed & (IN_LEFT | IN_RIGHT)))) {
            Toggle* t = &s_toggles[cursor];
            int i = (toggle_index(t) + step + t->count) % t->count;
            *t->field = t->values[i];
        }
        if (chosen == TOGGLE_COUNT || chosen == -1) {
            nk_wii_settings_save();
            return;
        }
    }
}

/* ---- the main screen ------------------------------------------------------- */

enum { ITEM_PLAY, ITEM_ONLINE, ITEM_SETTINGS, ITEM_EXIT, ITEM_COUNT };

static const char* main_detail(int item, void* user)
{
    (void) user;
    switch (item) {
    case ITEM_PLAY:
        return "Your town";
    case ITEM_ONLINE:
        return HAVE_ONLINE ? "Lobbies" : "Not in this build";
    default:
        return NULL;
    }
}

void nk_wii_launcher(void)
{
    static const char* const items[ITEM_COUNT] = {"Play", "Online", "Settings", "Exit"};
    int cursor = 0;
#ifdef NOOKERY_WII_NET
    if (ui_online_autotest()) {
        return;
    }
#endif
    if (skip_requested()) {
        return;
    }
    gfx_init();
    input_init();
    for (;;) {
        unsigned int pressed = input_poll();
        ui_background(NULL);
        int chosen = ui_menu(items, ITEM_COUNT, &cursor, 128, main_detail, NULL, pressed);
        {
            const char* disc = nk_wii_disc_path();
            char line[200];
            snprintf(line, sizeof(line), "Game: %s", disc != NULL ? disc : "(none)");
            gfx_text(&font_small, ui_left() + 42, 330, UI_GREY, line);
            gfx_text(&font_small, ui_left() + 42, 352, UI_GREY,
                     "Saves: sd:/apps/nookery/card_a (your town), card_b (towns to visit)");
        }
        ui_footer("A: Choose    HOME: Exit    (hold Z at start to skip this screen)");
        ui_present();
        if ((pressed & IN_HOME) || chosen == ITEM_EXIT) {
            gfx_shutdown();
            nk_wii_exit();
        }
        if (chosen == ITEM_PLAY) {
            break;
        }
        if (chosen == ITEM_SETTINGS) {
            ui_settings();
        }
        if (chosen == ITEM_ONLINE && HAVE_ONLINE && ui_online()) {
            break;
        }
    }
    gfx_shutdown();
    input_hand_over();
}
