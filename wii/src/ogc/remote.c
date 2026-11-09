/* The Wii Remote's own buttons during the game (the Classic Controller's
 * are the game's pad, ogc/classic.c):
 *   1     a screenshot: the picture on the TV, as a PNG in
 *         sd:/apps/nookery/screenshots/ (the Remote rumbles once);
 *   HOME  held for a second (the Remote's or a Classic Controller's): back to
 *         the Homebrew Channel, as RESET does (the system watch thread).
 * Looked at once a frame, after the Remotes are read (nk_wii_classic_scan).
 *
 * The picture is copied out of the frame buffer being shown (vi.c) at once,
 * a couple of milliseconds; a thread of its own below the game's turns it
 * into RGB, compresses it (zlib) and writes the file, so the game doesn't
 * stop for the card. One at a time: a press while one is being written is
 * let go. The frame buffer is the game's 640-pixel frame (the TV's 16:9 is
 * the same frame stretched). */

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <zlib.h>

#include <wl/base/base.h>
#include <wl/platform/platform.h>

#include "nk_wii.h"

#define SHOT_DIR NK_WII_DIR "/screenshots"
#define HOME_HOLD_MS 1000
#define RUMBLE_MS 120
#define MAX_W 720
#define MAX_H 576

/* settings.c: tests */
extern int g_nk_screenshot_test;
extern int g_nk_home_test;

/* vi.c: the frame buffer on the TV, and its size */
void* nk_wii_vi_shown_fb(int* width, int* height);

static u64 s_home_since[WPAD_MAX_WIIMOTES];
static u64 s_rumble_until[WPAD_MAX_WIIMOTES];
static u8* s_yuv;
static int s_w, s_h;
static volatile int s_busy;
static lwp_t s_thread = LWP_THREAD_NULL;
static u8 s_stack[16 * 1024] __attribute__((aligned(32)));

static void put32(u8* p, u32 v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static int chunk(FILE* f, const char* type, const u8* data, u32 len)
{
    u8 head[8];
    u8 tail[4];
    uLong crc = crc32(0, (const Bytef*) type, 4);
    put32(head, len);
    memcpy(head + 4, type, 4);
    if (len) {
        crc = crc32(crc, data, len);
    }
    put32(tail, (u32) crc);
    return fwrite(head, 8, 1, f) == 1 && (len == 0 || fwrite(data, len, 1, f) == 1) && fwrite(tail, 4, 1, f) == 1;
}

static u8 clamp(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (u8) v;
}

/* One row of the YUY2 frame buffer (Y0 U Y1 V for each two pixels) as RGB, after PNG's filter byte (none) */
static void row_rgb(const u8* yuv, u8* out, int w)
{
    *out++ = 0;
    for (int x = 0; x < w; x += 2, yuv += 4) {
        int u = yuv[1] - 128, v = yuv[3] - 128;
        int rv = (359 * v) >> 8, gu = (88 * u + 183 * v) >> 8, bu = (454 * u) >> 8;
        for (int i = 0; i < 2; i++) {
            int y = yuv[i * 2];
            *out++ = clamp(y + rv);
            *out++ = clamp(y - gu);
            *out++ = clamp(y + bu);
        }
    }
}

static int write_png(const char* path, const u8* yuv, int w, int h)
{
    static u8 row[1 + MAX_W * 3];
    static u8 out[32 * 1024];
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    u8 ihdr[13];
    z_stream z;
    int ok;
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    ok = fwrite(sig, 8, 1, f) == 1 && chunk(f, "IHDR", ihdr, 13);

    memset(&z, 0, sizeof(z));
    ok = ok && deflateInit(&z, 6) == Z_OK;
    for (int y = 0; ok && y <= h; y++) {
        int flush = y == h ? Z_FINISH : Z_NO_FLUSH;
        if (y < h) {
            row_rgb(yuv + y * w * 2, row, w);
            z.next_in = row;
            z.avail_in = 1 + w * 3;
        }
        do {
            int r;
            z.next_out = out;
            z.avail_out = sizeof(out);
            r = deflate(&z, flush);
            if (r == Z_STREAM_ERROR) {
                ok = 0;
                break;
            }
            if (sizeof(out) - z.avail_out > 0) {
                ok = chunk(f, "IDAT", out, sizeof(out) - z.avail_out);
            }
            if (r == Z_STREAM_END) {
                break;
            }
        } while (ok && (z.avail_out == 0 || (flush == Z_FINISH)));
    }
    deflateEnd(&z);
    ok = ok && chunk(f, "IEND", NULL, 0);
    ok = fclose(f) == 0 && ok;
    return ok;
}

static void* writer(void* arg)
{
    (void) arg;
    for (;;) {
        char path[96];
        time_t now;
        struct tm* t;
        /* (a look ten times a second: a wakeup sent before the thread first slept would be lost) */
        while (!s_busy) {
            usleep(100 * 1000);
        }
        wl_platform_fs_mkdir(SHOT_DIR);
        now = time(NULL);
        t = localtime(&now);
        snprintf(path, sizeof(path), SHOT_DIR "/shot-%04d%02d%02d-%02d%02d%02d.png", t->tm_year + 1900,
                 t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        if (wl_platform_fs_exists(path)) {
            /* (two in the same second) */
            snprintf(path, sizeof(path), SHOT_DIR "/shot-%04d%02d%02d-%02d%02d%02d-%u.png", t->tm_year + 1900,
                     t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, (unsigned) (gettime() & 0xFFF));
        }
        if (write_png(path, s_yuv, s_w, s_h)) {
            WL_LOG_I("nookery", "screenshot: %s (%dx%d)", path, s_w, s_h);
        } else {
            WL_LOG_W("nookery", "screenshot: couldn't write %s (no SD card?)", path);
        }
        s_busy = 0;
    }
    return NULL;
}

/* A screenshot: the picture copied now, written by the writer thread */
static void screenshot(int chan)
{
    int w, h;
    void* fb;
    if (s_busy) {
        return;
    }
    fb = nk_wii_vi_shown_fb(&w, &h);
    if (fb == NULL || w <= 0 || h <= 0 || w > MAX_W || h > MAX_H) {
        WL_LOG_W("nookery", "screenshot: no picture (frame buffer %p, %dx%d)", fb, w, h);
        return;
    }
    if (s_yuv == NULL) {
        s_yuv = memalign(32, MAX_W * MAX_H * 2); /* (newlib's heap: MEM2) */
        if (s_yuv == NULL) {
            return;
        }
    }
    if (s_thread == LWP_THREAD_NULL) {
        /* below the game's threads: the card's writes wait for the game, not the other way round */
        if (LWP_CreateThread(&s_thread, writer, NULL, s_stack, sizeof(s_stack), 20) < 0) {
            s_thread = LWP_THREAD_NULL;
            return;
        }
    }
    DCInvalidateRange(fb, w * h * 2);
    memcpy(s_yuv, fb, w * h * 2);
    s_w = w;
    s_h = h;
    s_busy = 1;
    if (chan >= 0) {
        WPAD_Rumble(chan, 1);
        s_rumble_until[chan] = gettime() + millisecs_to_ticks(RUMBLE_MS);
    }
}

void nk_wii_remote_frame(void)
{
    static u32 frames;
    u64 now = gettime();
    frames++;

    if (g_nk_screenshot_test && frames % 1800 == 0) {
        /* settings screenshot_test: one every 30 s, as if 1 were pressed */
        WL_LOG_I("nookery", "SHOTTEST: a screenshot");
        screenshot(-1);
    }
    if (g_nk_home_test && frames == (u32) g_nk_home_test * 60) {
        /* settings home_test = N: HOME held from N seconds on */
        WL_LOG_I("nookery", "HOMETEST: HOME held");
        s_home_since[0] = now;
    }

    for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
        const WPADData* d = WPAD_Data(chan);
        int home = g_nk_home_test && chan == 0 && s_home_since[0] != 0;
        if (s_rumble_until[chan] != 0 && now >= s_rumble_until[chan]) {
            WPAD_Rumble(chan, 0);
            s_rumble_until[chan] = 0;
        }
        if (d != NULL && d->err == WPAD_ERR_NONE) {
            if (d->btns_d & WPAD_BUTTON_1) {
                screenshot(chan);
            }
            home |= (d->btns_h & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) != 0;
        }
        if (!home) {
            s_home_since[chan] = 0;
        } else if (s_home_since[chan] == 0) {
            s_home_since[chan] = now;
        } else if (ticks_to_millisecs(now - s_home_since[chan]) >= HOME_HOLD_MS) {
            WL_LOG_I("SYS", "HOME held: leaving");
            wl_platform_exit(WL_EXIT_LOADER);
        }
    }
}
