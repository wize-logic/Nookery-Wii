#ifndef NK_WII_H
#define NK_WII_H

/* What the Wii port's shims share (wii/src): the GameCube SDK's contracts
 * over libogc. */

/* The program's folders on the SD card and a USB drive */
#define NK_WII_DIR "sd:/apps/nookery"
#define NK_WII_USB_DIR "usb:/apps/nookery"

/* os_thread.c: the main thread's OSThread; from OSInit */
void nk_wii_thread_init(void);

/* main.c: leaves for the loader (the Homebrew Channel) */
__attribute__((noreturn)) void nk_wii_exit(void);
/* main.c: a stop screen with the message, then leaves */
__attribute__((noreturn, format(printf, 1, 2))) void nk_wii_fatal(const char* fmt, ...);

/* Work deferred to interrupt context (a tick task on the decrementer, as
 * libogc's alarms are). The SDK delivers its DVD and ARAM completions from
 * interrupts, which a thread that disabled them holds off; libogc switches
 * to a woken thread at once, whatever the waker's interrupts. So the shims
 * wake their threads and call the game's callbacks from here: never while
 * the game's thread has interrupts disabled. Armed once until it runs; `fn`
 * runs with interrupts disabled. */
typedef struct NkWiiDefer {
    unsigned long long task[3]; /* tuxedo's KTickTask */
    void (*fn)(void* arg);
    void* arg;
    volatile int armed;
} NkWiiDefer;

void nk_wii_defer(NkWiiDefer* defer);

/* stall.c: a report in the log, by the system watch thread, when the game
 * shows no new frame for long, with where its main thread waits */
enum {
    NK_WII_WAIT_SLEEP = 1, /* OSSleepThread: `object` its queue */
    NK_WII_WAIT_RETRACE,   /* VIWaitForRetrace */
};
void nk_wii_wait_begin(int kind, const void* object, void* frame);
void nk_wii_wait_end(void);
void nk_wii_wait_suspend(unsigned int lwp, int suspended, void* by);
void nk_wii_stall_start(void);
void nk_wii_stall_frame(void);
void nk_wii_stall_game_thread(void);
/* The Wii Remote's 1 (a screenshot) and HOME held (back to the loader), once a frame (ogc/remote.c) */
void nk_wii_remote_frame(void);
void nk_wii_stall_tick(void* user);
void nk_wii_crash_install(void);

/* The priorities of the shims' worker threads (libogc's, 127 highest; the
 * main thread runs at 64): above the game's, like the interrupts they stand
 * for */
#define NK_WII_PRIO_IO 100

/* The floating-point unit as the GameCube's SDK runs the game: IEEE mode
 * (denormals kept; libogc starts with FPSCR[NI]), round to nearest, no
 * exceptions */
static inline void nk_wii_fpscr(void)
{
    double zero = 0.0;
    __asm__ volatile("mtfsf 255, %0" ::"f"(zero));
}

/* main.c: the boot's log written at once (until the game's first frames) */
void nk_wii_log_point(void);
void nk_wii_log_sync_end(void);

/* netlog.c: the log to a computer over TCP (spec
 * "listen:PORT", from the program's net= argument) */
void nk_wii_netlog_start(const char* spec, const char* run_name);

/* os.c: MEM1's and MEM2's free memory for the game's arena (main.c, first) */
void nk_wii_reserve_arena(void);

/* dvd.c: the player's disc image */
int nk_wii_disc_open(void);
const char* nk_wii_disc_path(void);
void nk_wii_disc_release_dol(void);
int nk_wii_disc_read(unsigned int offset, void* buf, unsigned int len);

/* ar.c: the 16 MiB of MEM2 that stand for ARAM */
unsigned char* nk_wii_aram(void);

/* card.c: memory card slot `chan`'s folder (sd:/apps/nookery/card_a, card_b) */
const char* nk_wii_card_get_dir(int chan);

/* ai.c: samples of the NES emulator's (pc/src/pc_nes_fixnes.c: 32 kHz
 * stereo, as the game's mixer gives the AI) mixed into what the AI plays
 * next, as the PC queues them to its audio device beside the game's */
void nk_wii_ai_mix_samples(const short* frames, unsigned int bytes);
unsigned int nk_wii_ai_mix_queued(void);

/* settings.c: sd:/apps/nookery/settings.ini into g_pc_settings */
void nk_wii_settings_load(void);
void nk_wii_settings_save(void);

#endif
