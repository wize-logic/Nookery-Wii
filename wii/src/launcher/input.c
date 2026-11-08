/* The launcher's controls: any GameCube controller, and Wii Remotes (with or
 * without a Classic Controller), as presses: the directions repeat while
 * held. The Wii Remote's stack is shut down when the launcher is done: the
 * game reads GameCube controllers only. */

#include "input.h"

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include <wl/base/base.h>

#define REPEAT_FIRST 22 /* frames before a held direction repeats */
#define REPEAT_NEXT 6

static u32 s_held_prev;
static int s_repeat;
static int s_wpad;

void input_init(void)
{
    PAD_Init();
    s_wpad = WPAD_Init() == WPAD_ERR_NONE;
    if (s_wpad) {
        /* the pointer, in a 640x480 screen's pixels */
        WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
        WPAD_SetVRes(WPAD_CHAN_ALL, 640, 480);
    }
}

/* Where the first Wii Remote that points at the screen points (640x480; the
 * last input_poll's), or 0 when none does */
int input_pointer(int* x, int* y)
{
    if (!s_wpad) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        WPADData* d = WPAD_Data(i);
        if (d != NULL && d->err == WPAD_ERR_NONE && d->ir.valid) {
            *x = (int) d->ir.x;
            *y = (int) d->ir.y;
            return 1;
        }
    }
    return 0;
}

/* The game starts: the Wii Remotes stay connected, for the Classic
 * Controllers the game takes as controllers (wii/src/classic.c) */
void input_hand_over(void)
{
}

void input_shutdown(void)
{
    if (s_wpad) {
        WPAD_Shutdown();
        s_wpad = 0;
    }
}

/* For tests in Dolphin: sd:/apps/nookery/launcher_input.txt, lines
 * "FRAME BUTTON" (A, B, UP, DOWN, LEFT, RIGHT, X, Z, START, HOME; FRAME
 * counts input_poll's calls), each pressed for 4 frames, or "FRAME log TEXT"
 * (a log line, for a test runner to stop at and take a picture) */
#define SCRIPT_MAX 128
static struct {
    u32 frame;
    u32 buttons;
    char text[48];
} s_script[SCRIPT_MAX];
static int s_script_count, s_script_next, s_script_loaded;
static u32 s_frame, s_script_held, s_script_until;

static void script_load(void)
{
    static const struct {
        const char* name;
        u32 bit;
    } names[] = {{"A", IN_A},         {"B", IN_B},         {"UP", IN_UP}, {"DOWN", IN_DOWN},
                 {"LEFT", IN_LEFT},   {"RIGHT", IN_RIGHT}, {"X", IN_X},   {"Z", IN_Z},
                 {"START", IN_START}, {"HOME", IN_HOME}};
    s_script_loaded = 1;
    FILE* f = fopen("sd:/apps/nookery/launcher_input.txt", "r");
    if (f == NULL) {
        return;
    }
    char line[96];
    while (s_script_count < SCRIPT_MAX && fgets(line, sizeof(line), f) != NULL) {
        unsigned frame;
        char word[16], text[48] = "";
        if (line[0] == '#' || sscanf(line, "%u %15s %47[^\n]", &frame, word, text) < 2) {
            continue;
        }
        s_script[s_script_count].frame = frame;
        s_script[s_script_count].buttons = 0;
        s_script[s_script_count].text[0] = '\0';
        if (strcmp(word, "log") == 0) {
            snprintf(s_script[s_script_count].text, sizeof(s_script[0].text), "%s", text);
        }
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strcmp(word, names[i].name) == 0) {
                s_script[s_script_count].buttons = names[i].bit;
            }
        }
        s_script_count++;
    }
    fclose(f);
}

static u32 script_held(void)
{
    if (!s_script_loaded) {
        script_load();
    }
    s_frame++;
    while (s_script_next < s_script_count && s_frame >= s_script[s_script_next].frame) {
        if (s_script[s_script_next].text[0] != '\0') {
            WL_LOG_I("launcher", "script: %s", s_script[s_script_next].text);
        }
        if (s_script[s_script_next].buttons != 0) {
            s_script_held = s_script[s_script_next].buttons;
            s_script_until = s_frame + 4;
        }
        s_script_next++;
    }
    return s_frame < s_script_until ? s_script_held : 0;
}

static u32 held_now(void)
{
    u32 held = script_held();
    PAD_ScanPads();
    for (int i = 0; i < 4; i++) {
        u16 b = PAD_ButtonsHeld(i);
        s8 sx = PAD_StickX(i), sy = PAD_StickY(i);
        if ((b & PAD_BUTTON_UP) || sy > 50) held |= IN_UP;
        if ((b & PAD_BUTTON_DOWN) || sy < -50) held |= IN_DOWN;
        if ((b & PAD_BUTTON_LEFT) || sx < -50) held |= IN_LEFT;
        if ((b & PAD_BUTTON_RIGHT) || sx > 50) held |= IN_RIGHT;
        if (b & PAD_BUTTON_A) held |= IN_A;
        if (b & PAD_BUTTON_B) held |= IN_B;
        if (b & PAD_BUTTON_X) held |= IN_X;
        if (b & PAD_TRIGGER_Z) held |= IN_Z;
        if (b & PAD_BUTTON_START) held |= IN_START;
    }
    if (s_wpad) {
        WPAD_ScanPads();
        for (int i = 0; i < 4; i++) {
            u32 b = WPAD_ButtonsHeld(i);
            /* the Wii Remote held sideways: its D-pad turned */
            if (b & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_UP)) held |= IN_UP;
            if (b & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_DOWN)) held |= IN_DOWN;
            if (b & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_LEFT)) held |= IN_LEFT;
            if (b & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_RIGHT)) held |= IN_RIGHT;
            if (b & (WPAD_BUTTON_2 | WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) held |= IN_A;
            if (b & (WPAD_BUTTON_1 | WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) held |= IN_B;
            if (b & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS)) held |= IN_X;
            if (b & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS)) held |= IN_Z;
            if (b & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) held |= IN_HOME;
        }
    }
    return held;
}

u32 input_held(void)
{
    return s_held_prev;
}

u32 input_poll(void)
{
    u32 held = held_now();
    u32 pressed = held & ~s_held_prev;
    u32 dirs = held & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT);
    if (dirs && (held & s_held_prev & dirs)) {
        if (++s_repeat >= REPEAT_FIRST) {
            pressed |= dirs;
            s_repeat = REPEAT_FIRST - REPEAT_NEXT;
        }
    } else {
        s_repeat = 0;
    }
    s_held_prev = held;
    return pressed;
}
