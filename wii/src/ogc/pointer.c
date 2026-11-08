/* The game's pointer (pc/include/game_pointer.h, the PC port's mouse) from
 * Wii Remote 1's pointer: where it points on the TV, A as the left button
 * (choose, walk to, pick up), B as the right one, the D-pad's Up and Down as
 * the scroll wheel. As the PC's GamePointer::begin_tick
 * (pc/src/acgc_game_pointer.cpp) does it: a GameCube controller's (or a
 * Classic Controller's) button or stick takes over and hides the hand, which
 * also hides after five seconds of a still Remote; a button pressed with the
 * Remote pointed off the screen does nothing until released.
 *
 * The Remotes are scanned with the controllers (classic.c, from PADRead),
 * their pointers on since classic.c's start. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "game_pointer.h"

/* widescreen.c: (4:3) / the screen's shape, 1 for 4:3 */
float pc_widescreen_get_aspect_factor(void);
/* pad.c: controller 1 as last read */
void nk_wii_pad_last(u16* buttons, s8 sticks[4]);

#define STICK_THRESHOLD_SQUARED 98.0f   /* the PC's: the game's STICK_MIN */
#define HAND_TIMEOUT_NS 5000000000ULL
#define JITTER_PIXELS 3.0f              /* a hand's tremor, in 640x480 pixels */

static GamePointerFrame s_frame = {.x = -1.0f, .y = -1.0f};
static int s_has_tick;
static int s_inside;
static GamePointerOwner s_owner = GAME_POINTER_NONE;
static u32 s_held_prev, s_suppressed;
static u16 s_pad_buttons_prev;
static u32 s_cbuttons;
static float s_ref_x, s_ref_y;           /* the stick's deflection when the pointer last moved */
static float s_anchor_x = -1000.0f, s_anchor_y = -1000.0f; /* where the Remote last moved, native */
static float s_last_x = -1.0f, s_last_y = -1.0f;
static u64 s_last_activity;

/* For tests in Dolphin (whose Remote points where its window's mouse is):
 * sd:/apps/nookery/pointer.txt, lines "FRAME X Y [A] [B]" (the Remote
 * pointed at X, Y of 640x480 from that frame on, A or B held) or "FRAME
 * off"; FRAME counts the game's input ticks from the first. With the file,
 * the real Remote's pointer is not read. */
#define SCRIPT_MAX 1024
static struct {
    u32 frame;
    float x, y;
    u32 buttons; /* WPAD_BUTTON_A / _B */
    int off;
} s_script[SCRIPT_MAX];
static int s_script_count, s_script_next, s_script_loaded;
static u32 s_script_ticks;
static float s_script_x = -1.0f, s_script_y = -1.0f;
static u32 s_script_buttons, s_script_buttons_prev;
static int s_script_off = 1;

static void script_load(void)
{
    s_script_loaded = 1;
    FILE* f = fopen("sd:/apps/nookery/pointer.txt", "r");
    if (f == NULL) {
        return;
    }
    char line[96];
    while (s_script_count < SCRIPT_MAX && fgets(line, sizeof(line), f) != NULL) {
        unsigned frame;
        char a[8] = "", b[8] = "", c[8] = "";
        float x, y;
        if (line[0] == '#') {
            continue;
        }
        if (sscanf(line, "%u %7s", &frame, a) == 2 && strcmp(a, "off") == 0) {
            s_script[s_script_count].frame = frame;
            s_script[s_script_count].off = 1;
            s_script_count++;
            continue;
        }
        int n = sscanf(line, "%u %f %f %7s %7s", &frame, &x, &y, b, c);
        if (n < 3) {
            continue;
        }
        s_script[s_script_count].frame = frame;
        s_script[s_script_count].x = x;
        s_script[s_script_count].y = y;
        s_script[s_script_count].off = 0;
        s_script[s_script_count].buttons = (strcmp(b, "A") == 0 || strcmp(c, "A") == 0 ? WPAD_BUTTON_A : 0) |
                                           (strcmp(b, "B") == 0 || strcmp(c, "B") == 0 ? WPAD_BUTTON_B : 0);
        s_script_count++;
    }
    fclose(f);
}

/* 1 when a script stands for the Remote: its pointer and buttons */
static int script_remote(int* valid, float* x, float* y, u32* held, u32* down)
{
    if (!s_script_loaded) {
        script_load();
    }
    if (s_script_count == 0) {
        return 0;
    }
    s_script_ticks++;
    while (s_script_next < s_script_count && s_script_ticks >= s_script[s_script_next].frame) {
        s_script_off = s_script[s_script_next].off;
        s_script_x = s_script[s_script_next].x;
        s_script_y = s_script[s_script_next].y;
        s_script_buttons = s_script[s_script_next].buttons;
        s_script_next++;
    }
    *valid = !s_script_off;
    *x = s_script_x;
    *y = s_script_y;
    *held = s_script_off ? 0 : s_script_buttons;
    *down = *held & ~s_script_buttons_prev;
    s_script_buttons_prev = *held;
    return 1;
}

const GamePointerFrame* acgc_pointer_frame(void)
{
    return &s_frame;
}

void acgc_pointer_begin_tick(uint32_t tick)
{
    if (s_has_tick && tick == s_frame.tick) {
        return;
    }
    s_has_tick = 1;
    u64 now = ticks_to_nanosecs(gettime());

    /* the Remote */
    const WPADData* d = WPAD_Data(0);
    u32 wbuttons = 0, wdown = 0;
    int was_inside = s_inside;
    float native_x = -1.0f, native_y = -1.0f;
    int valid = 0;
    s_inside = 0;
    extern int g_nk_wii_remote_pointer; /* settings.c */
    if (!script_remote(&valid, &native_x, &native_y, &wbuttons, &wdown) && g_nk_wii_remote_pointer && d != NULL &&
        d->err == WPAD_ERR_NONE)
    {
        wbuttons = d->btns_h;
        wdown = d->btns_d;
        valid = d->ir.valid;
        native_x = d->ir.x;
        native_y = d->ir.y;
    }
    if (valid) {
        s_inside = native_x >= 0.0f && native_x < 640.0f && native_y >= 0.0f && native_y < 480.0f;
    }
    float aspect = pc_widescreen_get_aspect_factor();
    float x = -1.0f, y = -1.0f;
    if (s_inside) {
        x = (native_x - (1.0f - aspect) * 320.0f) / (2.0f * aspect);
        y = native_y * 0.5f;
    }
    u32 held = ((wbuttons & WPAD_BUTTON_A) ? GAME_POINTER_LEFT : 0) |
               ((wbuttons & WPAD_BUTTON_B) ? GAME_POINTER_RIGHT : 0);
    u32 down = held & ~s_held_prev;
    int cancelled = 0, activity = 0, moved = 0;
    int scroll_steps = 0;

    if (s_inside) {
        moved = x != s_last_x || y != s_last_y;
        float dx = native_x - s_anchor_x, dy = native_y - s_anchor_y;
        if (!was_inside || dx * dx + dy * dy > JITTER_PIXELS * JITTER_PIXELS) {
            s_anchor_x = native_x;
            s_anchor_y = native_y;
            activity = 1;
        }
        /* a button pressed while pointing counts; one pressed off the
         * screen stays quiet until released */
        if (down & ~s_suppressed) {
            activity = 1;
        }
        if (wdown & WPAD_BUTTON_UP) {
            scroll_steps -= 1;
            activity = 1;
        }
        if (wdown & WPAD_BUTTON_DOWN) {
            scroll_steps += 1;
            activity = 1;
        }
    } else {
        cancelled = was_inside;
        s_suppressed |= held | down;
        s_owner = GAME_POINTER_NONE;
    }
    u32 pressed = s_inside ? down & ~s_suppressed : 0;

    /* the controller taking over, as the PC's begin_tick */
    u16 pad_buttons;
    s8 sticks[4];
    nk_wii_pad_last(&pad_buttons, sticks);
    u32 cbuttons = (sticks[2] >= 29 ? 1u : 0u) | (sticks[2] <= -29 ? 2u : 0u) | (sticks[3] >= 29 ? 4u : 0u) |
                   (sticks[3] <= -29 ? 8u : 0u);
    int pad_activity = (pad_buttons & ~s_pad_buttons_prev) != 0 || (cbuttons & ~s_cbuttons) != 0;
    s_pad_buttons_prev = pad_buttons;
    s_cbuttons = cbuttons;
    float sx = sticks[0], sy = sticks[1];
    int deflected = sx * sx + sy * sy > STICK_THRESHOLD_SQUARED;
    if (!deflected) {
        s_ref_x = s_ref_y = 0.0f;
    } else if (activity) {
        s_ref_x = sx;
        s_ref_y = sy;
    }
    float rdx = sx - s_ref_x, rdy = sy - s_ref_y;
    int takeover = pad_activity || (deflected && rdx * rdx + rdy * rdy > STICK_THRESHOLD_SQUARED);
    if (takeover) {
        s_owner = GAME_POINTER_PAD;
        s_suppressed |= held;
        pressed = 0;
        scroll_steps = 0;
        activity = 0;
    } else if (activity) {
        s_owner = GAME_POINTER_MOUSE;
    }
    u32 held_out = s_inside ? held & ~s_suppressed : 0;
    if (activity || (s_owner == GAME_POINTER_MOUSE && held_out)) {
        s_last_activity = now;
    }
    int visible = s_inside && s_owner == GAME_POINTER_MOUSE && now - s_last_activity < HAND_TIMEOUT_NS;

    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.tick = tick;
    s_frame.x = x;
    s_frame.y = y;
    s_frame.held = held_out;
    s_frame.pressed = pressed;
    s_frame.scroll_y = (float) scroll_steps;
    s_frame.scroll_steps_y = scroll_steps;
    s_frame.inside = s_inside;
    s_frame.moved = moved;
    s_frame.active = activity;
    s_frame.visible = visible;
    s_frame.cancelled = cancelled;
    s_frame.owner = s_owner;

    /* released buttons are no longer held back */
    s_suppressed &= held;
    s_held_prev = held;
    s_last_x = x;
    s_last_y = y;
}
