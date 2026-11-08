/* A game that stops: where its main thread is, and the hardware's state,
 * written to the log (nk_wii.h).
 *
 * The game shows each frame through VISetNextFrameBuffer (vi.c); the shims'
 * blocking calls mark the main thread's waits with their own stack frame
 * (its frames pace themselves by polling the retrace count, not by waiting,
 * so a wait that lasts is the news). The system watch thread
 * (wl_platform_system_watch_start, above every thread of the game's) checks
 * each tick: when no new frame has come for 20 s (from the game's start),
 * it logs what the main thread waits in, if anything, the call chain under
 * that wait (the stack's back chain, as the SDK's OSDumpContext walks it),
 * and the registers of the hardware a start-up or frame waits on (GX's FIFO,
 * the pixel engine, VI, the DSP, the audio interface), then writes the log
 * out at once. Once per stall: a new frame arms it again. It needs interrupts
 * on (a stop with them off leaves nothing, as before). */

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>

#include <wl/base/base.h>
#include <wl/platform/platform.h>

#include "nk_wii.h"

#define STALL_SECONDS 20
#define MAX_FRAMES 16

static volatile int s_armed;
static lwp_t s_main;
static volatile int s_kind;
static const void* volatile s_object;
static volatile u32 s_frame;
static volatile u64 s_since;
static volatile u64 s_last_end;
static volatile u32 s_waits;
static volatile u64 s_started;
static volatile u64 s_last_frame;
static volatile u32 s_frames;
static u32 s_reported = 0xFFFFFFFFu;

/* The VI's retraces the game has had (vi.c) */
u32 VIGetRetraceCount(void);

void nk_wii_stall_start(void)
{
    s_main = LWP_GetSelf();
    s_started = s_last_end = gettime();
    s_armed = 1;
}

void nk_wii_stall_frame(void)
{
    s_last_frame = gettime();
    s_frames++;
}

void nk_wii_wait_begin(int kind, const void* object, void* frame)
{
    if (!s_armed || LWP_GetSelf() != s_main) {
        return;
    }
    s_object = object;
    s_frame = (u32) frame;
    s_since = gettime();
    s_kind = kind;
}

void nk_wii_wait_end(void)
{
    if (!s_armed || LWP_GetSelf() != s_main) {
        return;
    }
    s_kind = 0;
    s_last_end = gettime();
    s_waits++;
}

#define WAIT_SUSPENDED 100

void nk_wii_wait_suspend(unsigned int lwp, int suspended, void* by)
{
    if (!s_armed || (lwp_t) lwp != s_main) {
        return;
    }
    if (suspended) {
        s_object = by;
        s_frame = 0;
        s_since = gettime();
        s_kind = WAIT_SUSPENDED;
    } else if (s_kind == WAIT_SUSPENDED) {
        s_kind = 0;
        s_last_end = gettime();
        s_waits++;
    }
}

/* A stack frame's address, as the game's and the port's stacks have them */
static int stack_addr(u32 p)
{
    return (p & 7) == 0 && ((p >= 0x80000000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x94000000u));
}

static void report_calls(u32 frame)
{
    char line[200];
    int n = 0, len = 0;
    len += snprintf(line + len, sizeof(line) - len, "stall: its calls (innermost first):");
    for (u32 p = frame; stack_addr(p) && n < MAX_FRAMES; n++) {
        u32 next = ((volatile u32*) p)[0];
        len += snprintf(line + len, sizeof(line) - len, " %08x", (unsigned) ((volatile u32*) p)[1]);
        if (len >= (int) sizeof(line) - 10 || next <= p) {
            break;
        }
        p = next;
    }
    WL_LOG_W("nookery", "%s", line);
}

static u32 cp32(u32 off)
{
    volatile u16* cp = (volatile u16*) 0xCC000000u;
    return ((u32) cp[(off + 2) / 2] << 16) | cp[off / 2];
}

static void report_hardware(void)
{
    volatile u32* pi = (volatile u32*) 0xCC003000u;
    volatile u16* cp = (volatile u16*) 0xCC000000u;
    volatile u16* pe = (volatile u16*) 0xCC001000u;
    volatile u16* vi = (volatile u16*) 0xCC002000u;
    volatile u16* dsp = (volatile u16*) 0xCC005000u;
    volatile u32* ai = (volatile u32*) 0xCD006C00u;
    WL_LOG_W("nookery", "stall: PI cause %08x mask %08x, its FIFO %08x-%08x write %08x",
             (unsigned) pi[0], (unsigned) pi[1], (unsigned) pi[3], (unsigned) pi[4], (unsigned) pi[5]);
    WL_LOG_W("nookery",
             "stall: CP status %04x control %04x, FIFO %08x-%08x, marks %08x/%08x, distance %08x, "
             "write %08x read %08x break %08x",
             (unsigned) cp[0], (unsigned) cp[1], (unsigned) cp32(0x20), (unsigned) cp32(0x24),
             (unsigned) cp32(0x28), (unsigned) cp32(0x2C), (unsigned) cp32(0x30), (unsigned) cp32(0x34),
             (unsigned) cp32(0x38), (unsigned) cp32(0x3C));
    /* (the mailboxes' high halves only: reading a low half takes the mail) */
    WL_LOG_W("nookery",
             "stall: PE interrupts %04x token %04x; VI config %04x, interrupts %04x%04x %04x%04x; "
             "DSP control %04x, mail to %04x from %04x; AI control %08x count %08x",
             (unsigned) pe[5], (unsigned) pe[7], (unsigned) vi[1], (unsigned) vi[0x18], (unsigned) vi[0x19],
             (unsigned) vi[0x1A], (unsigned) vi[0x1B], (unsigned) dsp[5], (unsigned) dsp[0], (unsigned) dsp[2],
             (unsigned) ai[0], (unsigned) ai[2]);
}

void nk_wii_stall_tick(void* user)
{
    u64 now, last;
    u32 seconds;
    int kind;
    (void) user;
    if (!s_armed || s_reported == s_frames) {
        return;
    }
    now = gettime();
    last = s_frames != 0 ? s_last_frame : s_started;
    seconds = ticks_to_secs(now - last);
    if (seconds < STALL_SECONDS) {
        return;
    }
    s_reported = s_frames;
    kind = s_kind;
    WL_LOG_W("nookery", "stall: no new frame for %u s (%u frames shown so far)", (unsigned) seconds,
             (unsigned) s_frames);
    seconds = ticks_to_secs(now - (kind != 0 ? s_since : s_last_end));
    switch (kind) {
    case 0:
        WL_LOG_W("nookery", "stall: the game's main thread is in no wait (its last ended %u s ago)",
                 (unsigned) seconds);
        break;
    case NK_WII_WAIT_SLEEP:
        WL_LOG_W("nookery", "stall: the game's main thread has waited %u s in OSSleepThread (queue %p)",
                 (unsigned) seconds, s_object);
        break;
    case NK_WII_WAIT_RETRACE:
        WL_LOG_W("nookery", "stall: the game's main thread has waited %u s for the VI's retrace",
                 (unsigned) seconds);
        break;
    case WAIT_SUSPENDED:
        WL_LOG_W("nookery", "stall: the game's main thread has been suspended %u s (by %p)", (unsigned) seconds,
                 s_object);
        break;
    default:
        break;
    }
    if (kind != 0 && kind != WAIT_SUSPENDED) {
        report_calls(s_frame);
    }
    report_hardware();
    WL_LOG_W("nookery", "stall: %u waits so far, %u retraces", (unsigned) s_waits, (unsigned) VIGetRetraceCount());
    wl_platform_log_flush();
}

/* ---- a crash ------------------------------------------------------------ */

/* An exception nothing handles goes to libogc's panic function (tuxedo's
 * PPCExcptCurPanicFn), whose screen shows the registers and waits for
 * RESET; while the game starts, the screen is black (the launcher's last
 * VIDEO_SetBlack), so a crash there looked like a hang. This one keeps the
 * crash, then has the crashed thread go on in crash_report, on a stack of its
 * own with interrupts on (the exception's return takes the context it is
 * given): the crash in the log, written out, then libogc's screen, shown. */

#include <string.h>
#include <tuxedo/ppc/exception.h>

extern char _SDA_BASE_[];
extern char _SDA2_BASE_[];

static PPCExcptPanicFn s_ogc_panic;
static volatile u32 s_crashes;
static unsigned s_crash_id;
static PPCContext s_crash;
static u32 s_crash_dar, s_crash_dsisr, s_crash_thread;
static u8 s_crash_stack[32 * 1024] __attribute__((aligned(32)));

static int code_addr(u32 p)
{
    return (p & 3) == 0 && ((p >= 0x80000000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x94000000u));
}

/* The game's arena's top (os.c): MEM1, which the video interface reads */
void* nk_wii_arena_scratch(unsigned int size);

/* A screen with the crash, in the launcher's video mode (a TV took that one),
 * on a frame buffer in MEM1, with interrupts on (libogc's own crash screen
 * programs the video interface with them off: a Wii lost the TV's signal);
 * RESET or POWER (the system watch thread) leaves */
static void crash_screen(const char* name, const PPCContext* c)
{
    GXRModeObj* mode = VIDEO_GetPreferredMode(NULL);
    u32 size = VIDEO_GetFrameBufferSize(mode);
    void* fb = MEM_K0_TO_K1(nk_wii_arena_scratch(size));
    VIDEO_Configure(mode);
    VIDEO_ClearFrameBuffer(mode, fb, COLOR_BLACK);
    console_init(fb, 40, 40, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    printf("\n Nookery stopped: a crash.\n\n");
    printf(" %s exception at %08x (lr %08x)\n", name, (unsigned) c->pc, (unsigned) c->lr);
    printf(" data address %08x\n\n", (unsigned) s_crash_dar);
    printf(" The details are in apps/nookery/logs/log.txt on the SD card.\n\n");
    printf(" Press RESET to go back to the Homebrew Channel.\n");
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    for (;;) {
        VIDEO_WaitVSync();
    }
}

static void crash_report(void)
{
    static const char* const names[] = {
        "?", "reset", "machine check", "DSI (data access)", "ISI (instruction fetch)", "external interrupt",
        "alignment", "program (illegal instruction, trap or privileged)", "FPU unavailable", "decrementer",
        "?", "?", "system call", "trace", "?", "performance monitor",
    };
    const PPCContext* c = &s_crash;
    const char* name = s_crash_id < sizeof(names) / sizeof(names[0]) ? names[s_crash_id] : "?";
    WL_LOG_E("nookery", "crash: %s exception (%u) at %08x, lr %08x, srr1 %08x, in %s", name, s_crash_id,
             (unsigned) c->pc, (unsigned) c->lr, (unsigned) c->msr,
             s_armed && s_crash_thread == (u32) s_main ? "the game's main thread" : "another thread");
    WL_LOG_E("nookery", "crash: data address %08x, DSISR %08x", (unsigned) s_crash_dar, (unsigned) s_crash_dsisr);
    if (code_addr(c->pc - 8) && code_addr(c->pc + 8)) {
        const volatile u32* p = (const volatile u32*) (c->pc - 8);
        WL_LOG_E("nookery", "crash: code there: %08x %08x [%08x] %08x %08x", (unsigned) p[0], (unsigned) p[1],
                 (unsigned) p[2], (unsigned) p[3], (unsigned) p[4]);
    }
    for (int i = 0; i < 32; i += 8) {
        WL_LOG_E("nookery", "crash: r%-2d %08x %08x %08x %08x %08x %08x %08x %08x", i, (unsigned) c->gpr[i],
                 (unsigned) c->gpr[i + 1], (unsigned) c->gpr[i + 2], (unsigned) c->gpr[i + 3],
                 (unsigned) c->gpr[i + 4], (unsigned) c->gpr[i + 5], (unsigned) c->gpr[i + 6],
                 (unsigned) c->gpr[i + 7]);
    }
    WL_LOG_E("nookery", "crash: cr %08x ctr %08x xer %08x", (unsigned) c->cr, (unsigned) c->ctr, (unsigned) c->xer);
    report_calls(c->gpr[1]);
    report_hardware();
    wl_platform_log_flush();
    crash_screen(name, c);
}

static void panic(unsigned exid, PPCContext* ctx)
{
    if (s_crashes++ != 0 || s_ogc_panic == NULL) {
        if (s_ogc_panic != NULL) {
            s_ogc_panic(exid, ctx);
        }
        return;
    }
    __asm__ volatile("mfspr %0, 19" : "=r"(s_crash_dar));
    __asm__ volatile("mfspr %0, 18" : "=r"(s_crash_dsisr));
    s_crash_id = exid;
    s_crash_thread = (u32) LWP_GetSelf();
    /* (the exception's frame has the integer registers only) */
    memcpy(&s_crash, ctx, offsetof(PPCContext, fpscr));
    ctx->pc = (u32) crash_report;
    ctx->lr = 0;
    ctx->gpr[1] = ((u32) s_crash_stack + sizeof(s_crash_stack) - 64) & ~15u;
    ctx->gpr[2] = (u32) _SDA2_BASE_;
    ctx->gpr[13] = (u32) _SDA_BASE_;
    ctx->msr |= 0x8000u | 0x2000u | 0x2u; /* EE, FP, RI */
}

void nk_wii_crash_install(void)
{
    if (s_ogc_panic == NULL) {
        s_ogc_panic = PPCExcptCurPanicFn;
        PPCExcptCurPanicFn = panic;
    }
}
