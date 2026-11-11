/* The program's start: the console's services (wiilib's platform layer over
 * libogc), the player's disc image, the game's tables read out of it
 * (pc/src/pc_assets.c), then the game's own boot (src/static/boot.c, built
 * as boot_main), which runs src/main.c's entry (built as ac_entry). */

#include "nk_wii.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <wl/base/base.h>
#include <wl/platform/platform.h>
#include <wl/platform/stop.h>

#include "nk_build_id.h"

/* The build: the commit it comes from ("-modified" when files differed from it), in the log's first line and in
 * crash reports (stall.c), to find the nookery.elf that turns a report's addresses into names
 * (wii/tools/crash_report.py) */
const char nk_build_id[] = NK_BUILD_ID;
const char nk_build_date[] = NK_BUILD_DATE;

/* libogc's sbrk takes MEM2 for newlib's malloc while this is set */
extern u32 MALLOC_MEM2;

/* wii/nookery.ld: src/data's tables, in MEM2 */
extern u8 __mem2bss_start[];
extern u8 __mem2bss_end[];

/* The game (src/main.c's main, then src/static/boot.c's) */
void ac_entry(void);
int boot_main(int argc, const char** argv);
/* pc/src/pc_assets.c */
int pc_assets_init(void);
/* The SDK's DVD (dvd.c) */
void DVDInit(void);
/* The menus before the game (launcher.c): what the player chose */
void nk_wii_launcher(void);
/* settings.c */
void nk_wii_settings_load(void);

/* The boot's log, line by line until the game's first frames: the log's own
 * thread writes once a second, so a boot that stops would lose its last
 * lines */
static lwp_t s_main_thread = LWP_THREAD_NULL;
static volatile int s_log_sync = 1;
static volatile int s_log_writing;

void nk_wii_log_point(void)
{
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if (s_log_sync && (msr & 0x8000) && LWP_GetSelf() == s_main_thread) {
        s_log_writing = 1;
        if (s_log_sync) {
            wl_platform_log_flush();
        }
        s_log_writing = 0;
    }
}

void nk_wii_log_sync_end(void)
{
    nk_wii_log_point();
    s_log_sync = 0;
}

/* net/nk_socket.c, when the online play is built (this file is the
 * executable's own, compiled without its definitions) */
void nk_socket_shutdown(void) __attribute__((weak));

/* os_thread.c, vi.c, os_alarm.c, dvd.c, card.c, remote.c: their parts of leaving */
void nk_wii_threads_stop(void);
void nk_wii_vi_stop(void);
void nk_wii_alarms_stop(void);
void nk_wii_dvd_stop(void);
int nk_wii_card_sync(unsigned ms);
void nk_wii_remote_sync(unsigned ms);

/* wiilib's wl_platform_exit, before the system's shutdown (libogc's, then the Homebrew Channel's stub, which runs
 * IOS's): the game stopped, as a GameCube's OSResetSystem stops it. The network first, whose lock the game's
 * threads take (a Wii hung in the shutdown with the network thread running); then the game's threads, its retrace
 * callbacks (they showed its picture again) and its alarms; then the files: a save the game had just written
 * reaches its file before the card is unmounted. */
void wl_platform_exit_prepare(int to_loader)
{
    WL_LOG_I("nookery", "leaving (%s)", to_loader ? "to the loader" : "power off");
    AUDIO_StopDMA();
    if (nk_socket_shutdown != NULL) {
        nk_socket_shutdown();
    }
    /* (the game's main thread is not stopped while it writes the boot's log, holding the log's lock) */
    s_log_sync = 0;
    while (s_log_writing && LWP_GetSelf() != s_main_thread) {
        usleep(1000);
    }
    nk_wii_threads_stop();
    nk_wii_vi_stop();
    nk_wii_alarms_stop();
    AUDIO_StopDMA(); /* (again: the game's audio thread ran until here) */
    nk_wii_dvd_stop();
    if (!nk_wii_card_sync(5000)) {
        WL_LOG_W("nookery", "leaving: a save was still being written");
    }
    nk_wii_remote_sync(5000);
    WL_LOG_I("nookery", "leaving: the game, the audio and the network are stopped");
    wl_platform_log_flush();
}

/* By the system watch thread, as RESET is, wherever the call comes from: the game's threads' stacks are small, and
 * it stops them all */
void nk_wii_exit(void)
{
    if (wl_platform_exit_request(WL_EXIT_LOADER) == WL_OK) {
        for (;;) {
            usleep(100 * 1000);
        }
    }
    wl_platform_exit(WL_EXIT_LOADER);
}

void nk_wii_fatal(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    WL_LOG_E("nookery", "fatal: %s", msg);
    wl_platform_log_flush();
    wl_platform_stop_screen("Nookery\n\n%s", msg);
}

/* settings.c */
extern int g_nk_stub_test;

/* The GPU's command FIFO reset, as libogc's GX_AbortFrame does it, before the
 * game's GXInit, which takes the GPU as a GameCube's start leaves it: on a
 * Wii, the program before (the Homebrew Channel draws with GX) may not */
static void gx_reset(void)
{
    volatile u32* pi = (volatile u32*) 0xCC003000u;
    u64 start;
    pi[6] = 1;
    start = gettime();
    while (gettime() - start < 50) {
    }
    pi[6] = 0;
    start = gettime();
    while (gettime() - start < 5) {
    }
}

int main(int argc, char** argv)
{

    /* src/data's tables: zeros until the disc's are copied in */
    memset(__mem2bss_start, 0, __mem2bss_end - __mem2bss_start);
    DCFlushRange(__mem2bss_start, __mem2bss_end - __mem2bss_start);

    /* MEM1's free memory is the game's arena, as on the GameCube (os.c),
     * taken before anything allocates; newlib's malloc is in MEM2 */
    nk_wii_reserve_arena();
    MALLOC_MEM2 = 1;

    wl_platform_fs_ios_init(NULL);
    wl_platform_system_init();
    wl_platform_fs_mount(WL_DEVICE_SD);
    wl_platform_fs_mkdir(NK_WII_DIR);
    wl_platform_fs_mkdir(NK_WII_DIR "/logs");
    wl_platform_log_init(NK_WII_DIR "/logs");
    /* stdout and stderr (the PC port's shared code prints) to the log */
    SYS_STDIO_Report(true);
    s_main_thread = LWP_GetSelf();
    wl_platform_system_watch_start(16, nk_wii_stall_tick, NULL);
    nk_wii_crash_install();
    nk_wii_fpscr();

    /* The log over the network: "net=listen:PORT run=NAME" (netlog.c) */
    {
        const char* net = NULL;
        const char* run = NULL;
        for (int i = 1; i < argc; i++) {
            if (argv[i] != NULL && strncmp(argv[i], "net=", 4) == 0) {
                net = argv[i] + 4;
            } else if (argv[i] != NULL && strncmp(argv[i], "run=", 4) == 0) {
                run = argv[i] + 4;
            }
        }
        /* or the same words in sd:/apps/nookery/netrun.txt (Dolphin gives
         * a DOL no arguments) */
        static char file_words[2][64];
        FILE* f = net == NULL ? fopen(NK_WII_DIR "/netrun.txt", "r") : NULL;
        if (f != NULL) {
            char word[64];
            while (fscanf(f, "%63s", word) == 1) {
                if (strncmp(word, "net=", 4) == 0) {
                    snprintf(file_words[0], sizeof(file_words[0]), "%s", word + 4);
                    net = file_words[0];
                } else if (strncmp(word, "run=", 4) == 0) {
                    snprintf(file_words[1], sizeof(file_words[1]), "%s", word + 4);
                    run = file_words[1];
                }
            }
            fclose(f);
        }
        if (net != NULL) {
            nk_wii_netlog_start(net, run);
        }
    }
    WL_LOG_I("nookery", "Nookery for the Wii, build %s (%s) (MEM1 %p-%p free, MEM2 tables %p-%p)", nk_build_id,
             nk_build_date, SYS_GetArena1Lo(), SYS_GetArena1Hi(), __mem2bss_start, __mem2bss_end);
    nk_wii_settings_load();
    if (g_nk_stub_test) {
        /* Dolphin stops the emulation at the Homebrew Channel's stub's address but has no code there, which wiilib
         * takes for no stub (it powers off): with an instruction there, leaving takes the way to the loader, as on
         * a console, and Dolphin ends by itself when it gets there (its log may show an ISI exception at 0 and a
         * crash report's first line as it stops: the processor runs on a moment) */
        u32* stub = (u32*) 0x80001800u;
        *stub = 0x48000000u;
        DCFlushRange(stub, 4);
        ICInvalidateRange(stub, 4);
        wl_platform_loader_stub_keep();
    }

    /* The player's disc, then the game's tables out of its DOL and REL */
    if (nk_wii_disc_open() != 0) {
        nk_wii_fatal("No copy of Animal Crossing was found.\n\n"
                     "Put your own dump of the game (NTSC-U, GAFE01) on the SD\n"
                     "card or a USB drive as\n"
                     "  /apps/nookery/game.iso  or  /games/GAFE01.iso\n"
                     "(.iso, .gcm and .ciso images work)");
    }
    nk_wii_log_point();
    DVDInit();
    if (!pc_assets_init()) {
        nk_wii_fatal("The game's data could not be read from\n%s", nk_wii_disc_path());
    }
    nk_wii_disc_release_dol();
    WL_LOG_I("nookery", "the game's tables are loaded");
    nk_wii_log_point();

    /* The menus before the game: play, online lobbies, settings */
    /* (sd:/apps/nookery/autostart: straight to the game, for unattended
     * tests and for players who never want the menus) */
    if (!wl_platform_fs_exists(NK_WII_DIR "/autostart") || wl_platform_fs_exists(NK_WII_DIR "/nettest.txt")) {
        nk_wii_launcher();
    }

    gx_reset();
    nk_wii_stall_start();
    WL_LOG_I("nookery", "starting the game");
    nk_wii_log_point();
    ac_entry();
    boot_main(0, NULL);
    nk_wii_exit();
}
