/* The SDK's PAD on libogc's: the same serial interface, and PADStatus is
 * libogc's structure field for field.
 *
 * For testing without a controller (Dolphin run unattended): when
 * sd:/apps/nookery/input.txt exists, controller 1 plays it back. Each line is
 *     FRAME BUTTONS [STICK_X STICK_Y [SUBSTICK_X SUBSTICK_Y]]
 * (FRAME in video frames since the game first reads the controllers, so
 * that time in the launcher doesn't count; BUTTONS the PAD_BUTTON_* bits in
 * hex, the sticks -128 to 127): from that frame on, controller 1 holds them,
 * on top of what a real one does, until the next line. '#' starts a
 * comment. */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

#include <wl/platform/platform.h>

_Static_assert(sizeof(PADStatus) == 12, "PADStatus is libogc's");

#define SCRIPT_PATH NK_WII_DIR "/input.txt"
#define SCRIPT_MAX 1024

typedef struct Step {
    s32 scene; /* >= 0: wait for this scene, the rest unused */
    u32 frame;
    u16 buttons;
    s8 stick[4];
} Step;

static Step* s_script;
static u32 s_steps;
static u32 s_index;      /* the next step */
static u32 s_base;       /* the frame the frames count from */
static Step s_current;   /* what controller 1 holds */

/* src/graph.c's frames (the script's frames count video frames) */
static void load_script(void)
{
    static BOOL loaded;
    WlPlatformFile* file;
    if (loaded) {
        return;
    }
    loaded = TRUE;
    if (!wl_platform_fs_exists(SCRIPT_PATH) ||
        wl_platform_fs_open(SCRIPT_PATH, WL_FILE_READ, &file) != WL_OK)
    {
        return;
    }
    u32 size = (u32) wl_platform_fs_size(file);
    char* text = malloc(size + 1);
    s_script = malloc(sizeof(Step) * SCRIPT_MAX);
    if (text == NULL || s_script == NULL ||
        wl_platform_fs_read_at(file, 0, text, size) != WL_OK)
    {
        wl_platform_fs_close(file);
        free(text);
        return;
    }
    wl_platform_fs_close(file);
    text[size] = '\0';
    for (char* line = text; line != NULL && *line != '\0' && s_steps < SCRIPT_MAX;) {
        char* next = line;
        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next++ = '\0';
        }
        unsigned frame, buttons;
        int sx = 0, sy = 0, cx = 0, cy = 0;
        if (line[0] != '#' &&
            sscanf(line, "%u %x %d %d %d %d", &frame, &buttons, &sx, &sy, &cx, &cy) >= 2)
        {
            Step* step = &s_script[s_steps++];
            step->scene = -1;
            step->frame = frame;
            step->buttons = (u16) buttons;
            step->stick[0] = (s8) sx;
            step->stick[1] = (s8) sy;
            step->stick[2] = (s8) cx;
            step->stick[3] = (s8) cy;
        }
        line = next;
    }
    free(text);
    s_base = VIGetRetraceCount();
    OSReport("PAD: controller 1 plays %s (%u steps, from video frame %u)\n", SCRIPT_PATH, (unsigned) s_steps,
             (unsigned) s_base);
}

static void play_script(PADStatus* status)
{
    u32 frame = VIGetRetraceCount();
    while (s_index < s_steps) {
        const Step* next = &s_script[s_index];
        if (frame - s_base >= next->frame) {
            s_current = *next;
            s_index++;
        } else {
            break;
        }
    }
    const Step* step = &s_current;
    PADStatus* pad = &status[0];
    if (pad->err != PAD_ERR_NONE) {
        memset(pad, 0, sizeof(*pad));
        pad->err = PAD_ERR_NONE;
    }
    pad->button |= step->buttons;
    if (step->stick[0] != 0 || step->stick[1] != 0) {
        pad->stickX = step->stick[0];
        pad->stickY = step->stick[1];
    }
    if (step->stick[2] != 0 || step->stick[3] != 0) {
        pad->substickX = step->stick[2];
        pad->substickY = step->stick[3];
    }
    if (step->buttons & PAD_BUTTON_A) {
        pad->analogA = 255;
    }
    if (step->buttons & PAD_BUTTON_B) {
        pad->analogB = 255;
    }
    if (step->buttons & PAD_TRIGGER_L) {
        pad->triggerLeft = 255;
    }
    if (step->buttons & PAD_TRIGGER_R) {
        pad->triggerRight = 255;
    }
}

/* classic.c: Wii Remotes with Classic Controllers, as controllers */
void nk_wii_classic_init(void);
void nk_wii_classic_scan(void);
int nk_wii_classic_read(int chan, u16* buttons, s8 sticks[4], u8 triggers[2]);

BOOL PADInit(void)
{
    PAD_Init();
    nk_wii_classic_init();
    load_script();
    return TRUE;
}

/* A port without a GameCube controller takes the Classic Controller of the
 * Remote with its number, if there is one */
static u32 classic_ports(PADStatus* status)
{
    u32 present = 0;
    nk_wii_classic_scan();
    for (int chan = 0; chan < 4; chan++) {
        PADStatus* pad = &status[chan];
        u16 buttons;
        s8 sticks[4];
        u8 triggers[2];
        if (pad->err != PAD_ERR_NO_CONTROLLER ||
            !nk_wii_classic_read(chan, &buttons, sticks, triggers))
        {
            continue;
        }
        memset(pad, 0, sizeof(*pad));
        pad->button = buttons;
        pad->stickX = sticks[0];
        pad->stickY = sticks[1];
        pad->substickX = sticks[2];
        pad->substickY = sticks[3];
        pad->triggerLeft = triggers[0];
        pad->triggerRight = triggers[1];
        pad->err = PAD_ERR_NONE;
        present |= 0x80000000u >> chan;
    }
    return present;
}

static PADStatus s_last0; /* controller 1 as last read */

/* The analog triggers from where each one rests. The game takes any trigger
 * above 0 (after PADClamp's 30) for L or R held (contreaddata.c), and L or R
 * held runs: a Classic Controller's triggers (0-31, scaled to 255) and a
 * worn or third-party GameCube controller's rest above that, and the player
 * always ran on a real Wii. A GameCube measures each controller's rest when
 * it is connected (its origin) and reads from there; this keeps the lowest
 * value each trigger has shown since its controller came, and reads from it. */
static u8 s_trigger_rest[4][2];
static u8 s_trigger_seen[4];

static void triggers_from_rest(PADStatus* status)
{
    for (int chan = 0; chan < 4; chan++) {
        PADStatus* pad = &status[chan];
        if (pad->err != PAD_ERR_NONE) {
            if (pad->err == PAD_ERR_NO_CONTROLLER) {
                s_trigger_seen[chan] = 0;
            }
            continue;
        }
        u8* value[2] = {&pad->triggerLeft, &pad->triggerRight};
        for (int t = 0; t < 2; t++) {
            u8* rest = &s_trigger_rest[chan][t];
            if (!s_trigger_seen[chan] || *value[t] < *rest) {
                *rest = *value[t];
            }
            /* (a trigger clicked all the way is 255 from the Classic
             * Controller: kept, whatever the rest) */
            if (*value[t] != 255) {
                int v = (int) *value[t] - (int) *rest;
                *value[t] = (u8) (v < 0 ? 0 : v);
            }
        }
        s_trigger_seen[chan] = 1;
    }
}

/* ogc/netlog.c's PAD command: controller 1 from the PC */
static volatile u16 s_remote_buttons;
static volatile s8 s_remote_sticks[4];
static volatile u32 s_remote_reads;

void nk_wii_pad_remote(u16 buttons, int stick_x, int stick_y, int sub_x, int sub_y, u32 reads)
{
    s_remote_reads = 0;
    s_remote_buttons = buttons;
    s_remote_sticks[0] = (s8) stick_x;
    s_remote_sticks[1] = (s8) stick_y;
    s_remote_sticks[2] = (s8) sub_x;
    s_remote_sticks[3] = (s8) sub_y;
    s_remote_reads = reads;
}

static void play_remote(PADStatus* pad)
{
    if (s_remote_reads == 0) {
        return;
    }
    s_remote_reads--;
    pad->err = PAD_ERR_NONE;
    pad->button |= s_remote_buttons;
    if (s_remote_buttons & PAD_TRIGGER_L) {
        pad->triggerLeft = 255;
    }
    if (s_remote_buttons & PAD_TRIGGER_R) {
        pad->triggerRight = 255;
    }
    if (s_remote_sticks[0] != 0 || s_remote_sticks[1] != 0) {
        pad->stickX = s_remote_sticks[0];
        pad->stickY = s_remote_sticks[1];
    }
    if (s_remote_sticks[2] != 0 || s_remote_sticks[3] != 0) {
        pad->substickX = s_remote_sticks[2];
        pad->substickY = s_remote_sticks[3];
    }
}

u32 PADRead(PADStatus* status)
{
    u32 r = PAD_Read(status);
    r |= classic_ports(status);
    triggers_from_rest(status);
    if (s_steps != 0) {
        play_script(status);
    }
    play_remote(&status[0]);
    s_last0 = status[0];
    return r;
}

/* ogc/pointer.c: controller 1's buttons and sticks (main x, y, C x, y) as
 * last read, to hand control back from the Wii Remote's pointer */
void nk_wii_pad_last(u16* buttons, s8 sticks[4])
{
    int ok = s_last0.err == PAD_ERR_NONE;
    *buttons = ok ? s_last0.button : 0;
    sticks[0] = ok ? s_last0.stickX : 0;
    sticks[1] = ok ? s_last0.stickY : 0;
    sticks[2] = ok ? s_last0.substickX : 0;
    sticks[3] = ok ? s_last0.substickY : 0;
}

int PADReset(unsigned long mask)
{
    return PAD_Reset(mask) != 0;
}

BOOL PADRecalibrate(u32 mask)
{
    return PAD_Recalibrate(mask) != 0;
}

void PADControlMotor(u32 chan, u32 command)
{
    PAD_ControlMotor(chan, command);
}

/* libogc samples at its own rate; the game asks for the SDK's default */
void PADSetSamplingRate(unsigned long msec)
{
    (void) msec;
}

void PADSetSpec(u32 spec)
{
    (void) spec;
}
