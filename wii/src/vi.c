/* The SDK's VI on libogc's VIDEO: libogc programs the Wii's video encoder,
 * which the GameCube's VI library knows nothing of. GXRenderModeObj and
 * libogc's GXRModeObj are the same structure. */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <dolphin/gx/GXStruct.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>

static BOOL s_initialized;
static volatile u32 s_retrace_count;
static VIRetraceCallback s_pre_retrace;

/* wii/src/gecko.c: the player's cheats, each frame (when there) */
void nk_wii_gecko_frame(void) __attribute__((weak));
/* wii/src/diag_dirty.c, in test builds */
void nk_wii_diag_frame(void) __attribute__((weak));

/* libogc 3.1 counts no retraces: the count is kept here, before the game's
 * own pre-retrace callback */
/* The PC port's frame number (src/game.c's pointer tick): the retraces */
u32 pc_frame_counter;

static void pre_retrace(u32 count)
{
    (void) count;
    s_retrace_count++;
    pc_frame_counter++;
    if (nk_wii_gecko_frame != NULL) {
        nk_wii_gecko_frame();
    }
    if (nk_wii_diag_frame != NULL) {
        nk_wii_diag_frame();
    }
    if (s_pre_retrace != NULL) {
        s_pre_retrace(s_retrace_count);
    }
}

void VIInit(void)
{
    if (s_initialized) {
        return;
    }
    s_initialized = TRUE;
    VIDEO_Init();
    VIDEO_SetPreRetraceCallback((ogc_VIRetraceCallback) pre_retrace);
}

/* widescreen.c: (4:3) / the screen's shape, below 1 when the game draws 16:9 */
float pc_widescreen_get_aspect_factor(void);

/* The game's modes (jsyswrap.cpp) put its 640-pixel frame on 660 of a line's
 * 720 (viXOrigin 30): a GameCube's TV hid the rest in its overscan. A Wii
 * set to 16:9 is on a flat widescreen TV, which shows the whole line, so the
 * picture had black bars at its sides, and was 6% narrower than the 16:9 the
 * game draws for. With the game in widescreen the frame spans the line. */
/* The frame buffer on the TV and the frame's size, for screenshots (ogc/remote.c) */
static void* s_shown_fb;
static int s_fb_width, s_fb_height;

void* nk_wii_vi_shown_fb(int* width, int* height)
{
    *width = s_fb_width;
    *height = s_fb_height;
    return s_shown_fb;
}

void VIConfigure(const GXRenderModeObj* rm)
{
    static GXRenderModeObj wide;
    s_fb_width = rm->fbWidth;
    s_fb_height = rm->xfbHeight;
    if (pc_widescreen_get_aspect_factor() < 1.0f && rm->viWidth < 720) {
        wide = *rm;
        wide.viXOrigin = 0;
        wide.viWidth = 720; /* (VI_MAX_WIDTH_NTSC, PAL60 the same) */
        rm = &wide;
    }
    VIDEO_Configure((void*) rm);
}

void VIFlush(void)
{
    VIDEO_Flush();
}

void VISetNextFrameBuffer(void* fb)
{
    VIDEO_SetNextFramebuffer(fb);
    /* a new frame for the freeze watch (stall.c) only when the picture changes: JUTVideo's retrace callback
     * gives the frame buffer shown again at every retrace, a frozen game's too */
    if (fb != s_shown_fb) {
        s_shown_fb = fb;
        nk_wii_stall_frame();
    }
}

void VISetBlack(BOOL black)
{
    VIDEO_SetBlack(black != FALSE);
}

void VIWaitForRetrace(void)
{
    nk_wii_wait_begin(NK_WII_WAIT_RETRACE, NULL, __builtin_frame_address(0));
    VIDEO_WaitVSync();
    nk_wii_wait_end();
}

u32 VIGetRetraceCount(void)
{
    return s_retrace_count;
}

u32 VIGetNextField(void)
{
    return VIDEO_GetNextField();
}

u32 VIGetTvFormat(void)
{
    /* The game is the NTSC release: VI_NTSC, VI_PAL, VI_MPAL are numbered
     * alike in libogc; a Wii set to EURGB60 shows the NTSC timing */
    u32 mode = VIDEO_GetCurrentTvMode();
    return mode <= 2 ? mode : 0;
}

u32 VIGetDTVStatus(void)
{
    return VIDEO_HaveComponentCable();
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    BOOL enabled = OSDisableInterrupts();
    VIRetraceCallback old = s_pre_retrace;
    s_pre_retrace = cb;
    OSRestoreInterrupts(enabled);
    return old;
}

/* Leaving (ogc/main.c): the game's retrace callbacks off. JUTVideo's shows the game's frame again at every retrace,
 * over the black of the way out. */
void nk_wii_vi_stop(void)
{
    VISetPreRetraceCallback(NULL);
    VIDEO_SetPostRetraceCallback(NULL);
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    return (VIRetraceCallback) VIDEO_SetPostRetraceCallback(
        (ogc_VIRetraceCallback) cb);
}
