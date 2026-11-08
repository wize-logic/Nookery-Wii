/* The SDK's OS on the Wii: libogc owns the processor (exceptions, interrupts,
 * threads, the decrementer); this keeps the SDK's contracts above it. */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <dolphin/os.h>

#include <wl/base/base.h>

/* The game's arena: all of MEM1 that libogc leaves (newlib's malloc is in
 * MEM2: main.c's MALLOC_MEM2) */
static void* s_arena_lo;
static void* s_arena_hi;

static BOOL s_initialized;

/* MEM1's free memory is the game's arena, as on the GameCube: taken from
 * libogc at the start (main.c), before anything (the launcher, the
 * network) allocates, so newlib's malloc goes to MEM2 instead */
void nk_wii_reserve_arena(void)
{
    if (s_arena_hi != NULL) {
        return;
    }
    s_arena_lo = (void*) OSRoundUp32B(SYS_GetArena1Lo());
    s_arena_hi = (void*) OSRoundDown32B(SYS_GetArena1Hi());
    /* libogc is not to give this memory to anyone else */
    SYS_SetArena1Lo(s_arena_hi);
}

/* The launcher's frame buffers: the top of the game's arena to be (which
 * OSInit clears before the game has it) */
void* nk_wii_arena_scratch(unsigned int size)
{
    nk_wii_reserve_arena();
    return (void*) (((u32) s_arena_hi - size) & ~31u);
}

void OSInit(void)
{
    if (s_initialized) {
        return;
    }
    s_initialized = TRUE;

    nk_wii_reserve_arena();
    /* As the GameCube's boot leaves it: zeros. The game takes fields of its
     * fresh heap memory for zero in places (a texture's or display list's
     * address with stray bits in the GX register the Wii reads more of), and
     * what is left here (the port's staging copy, libogc's) is not */
    memset(s_arena_lo, 0, (u8*) s_arena_hi - (u8*) s_arena_lo);
    DCFlushRange(s_arena_lo, (u8*) s_arena_hi - (u8*) s_arena_lo);

    nk_wii_thread_init();
    OSReport("OSInit: arena %p-%p (%u KiB), bus %u Hz\n", s_arena_lo,
             s_arena_hi,
             (unsigned) (((u8*) s_arena_hi - (u8*) s_arena_lo) / 1024),
             (unsigned) OS_BUS_CLOCK);
}

#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define ROUND_DOWN(x, a) ((x) & ~((a) - 1))

/* OSArena.c */
void* OSGetArenaHi(void)
{
    return s_arena_hi;
}

void* OSGetArenaLo(void)
{
    return s_arena_lo;
}

void OSSetArenaHi(void* addr)
{
    s_arena_hi = addr;
}

void OSSetArenaLo(void* addr)
{
    s_arena_lo = addr;
}

void* OSAllocFromArenaLo(u32 size, u32 align)
{
    u8* ptr = (u8*) ROUND_UP((u32) s_arena_lo, align);
    s_arena_lo = (void*) ROUND_UP((u32) (ptr + size), align);
    return ptr;
}

void* OSAllocFromArenaHi(u32 size, u32 align)
{
    u8* arena = (u8*) ROUND_DOWN((u32) s_arena_hi, align);
    u8* ptr = (u8*) ROUND_DOWN((u32) (arena - size), align);
    s_arena_hi = ptr;
    return ptr;
}

/* OSMemory.c: a retail console's 24 MiB, not a development kit's 48 */
u32 OSGetPhysicalMemSize(void)
{
    return 24 * 1024 * 1024;
}

u32 OSGetConsoleSimulatedMemSize(void)
{
    return 24 * 1024 * 1024;
}

/* OSError.c. Lines are assembled until their newline: the game reports a
 * line in pieces */
static char s_report[512];
static size_t s_report_len;

/* The game turns its reports off once it has started on a retail console
 * (src/static/boot.c), as their volume would slow it; the settings' verbose
 * keeps them */
extern int g_pc_verbose;
static BOOL s_report_off;

void OSReportDisable(void)
{
    s_report_off = TRUE;
}

void OSReportEnable(void)
{
    s_report_off = FALSE;
}

static void report(const char* msg, va_list args)
{
    if (s_report_off && !g_pc_verbose) {
        return;
    }
    BOOL enabled = OSDisableInterrupts();
    BOOL logged = FALSE;
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), msg, args);
    if (n < 0) {
        n = 0;
    } else if (n >= (int) sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n' || s_report_len == sizeof(s_report) - 1) {
            s_report[s_report_len] = '\0';
            if (strncmp(s_report, "SYNC", 4) == 0) {
                /* the determinism test's lines (gm/synctest.inc): every one,
                 * straight out (the log drops lines when they come faster
                 * than its writer) */
                SYS_Report("%s\n", s_report);
            } else {
                wl_log_write(WL_LOG_LEVEL_INFO, "nookery", "%s", s_report);
                logged = TRUE;
            }
            s_report_len = 0;
            if (c == '\n') {
                continue;
            }
        }
        s_report[s_report_len++] = c;
    }
    OSRestoreInterrupts(enabled);
    /* (on the card at once while the game starts: with interrupts on again,
     * so a boot that stops leaves its last line) */
    if (logged) {
        nk_wii_log_point();
    }
}

void OSReport(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    report(msg, args);
    va_end(args);
}

/* The port's own lines (the online play's), which the game's switch leaves on */
void nk_wii_log(const char* msg, ...)
{
    BOOL off = s_report_off;
    va_list args;
    s_report_off = FALSE;
    va_start(args, msg);
    report(msg, args);
    va_end(args);
    s_report_off = off;
}

void OSVReport(const char* msg, va_list args)
{
    report(msg, args);
}

void OSPanic(const char* file, int line, const char* msg, ...)
{
    char buf[256];
    va_list args;
    OSDisableInterrupts();
    va_start(args, msg);
    vsnprintf(buf, sizeof(buf), msg, args);
    va_end(args);
    wl_log_write(WL_LOG_LEVEL_ERROR, "nookery", "OSPanic in \"%s\" on line %d: %s",
                 file, line, buf);
    nk_wii_fatal("OSPanic in \"%s\" on line %d:\n%s", file, line, buf);
}

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler)
{
    /* libogc's exception handlers keep them */
    (void) error;
    (void) handler;
    return NULL;
}

/* OSInterrupt.c: MSR[EE] */
BOOL OSDisableInterrupts(void)
{
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" ::"r"(msr & ~0x8000u) : "memory");
    return (msr >> 15) & 1;
}

BOOL OSEnableInterrupts(void)
{
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" ::"r"(msr | 0x8000u) : "memory");
    return (msr >> 15) & 1;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if (level) {
        __asm__ volatile("mtmsr %0" ::"r"(msr | 0x8000u) : "memory");
    } else {
        __asm__ volatile("mtmsr %0" ::"r"(msr & ~0x8000u) : "memory");
    }
    return (msr >> 15) & 1;
}

/* OSTime.c's assembly: the time base. The game counts time in the
 * GameCube's ticks (OS_TIMER_CLOCK, 40.5 MHz: Aurora's headers, and the
 * SDK's as wii/... builds them), the Wii's time base runs at 60.75 MHz: its
 * value is scaled by 2/3, which keeps the RTC's epoch (2000) and seconds as
 * they are. */
OSTime OSGetTime(void)
{
    u32 hi, lo, hi2;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    u64 tb = ((u64) hi << 32) | lo;
    return (OSTime) ((tb / 3) * 2 + ((tb % 3) * 2) / 3);
}

OSTick OSGetTick(void)
{
    return (OSTick) OSGetTime();
}

/* The SDK's low memory base (Aurora's os.h: OS_BASE_CACHED) */
uintptr_t OSBaseAddress = 0x80000000;

/* OSContext.c: the contexts the SDK switches its floating-point registers
 * with. libogc saves a thread's registers itself; these only keep the
 * pointers the SDK's callers hand back. */
static OSContext s_context;
static OSContext* s_current_context = &s_context;

OSContext* OSGetCurrentContext(void)
{
    return s_current_context;
}

void OSSetCurrentContext(OSContext* context)
{
    s_current_context = context;
}

void OSClearContext(OSContext* context)
{
    context->mode = 0;
    context->state = 0;
}

u32 OSSaveContext(OSContext* context)
{
    memset(context, 0, sizeof(*context));
    __asm__ volatile("mflr %0" : "=r"(context->lr));
    __asm__ volatile("mr %0, 1" : "=r"(context->gpr[1]));
    __asm__ volatile("mr %0, 2" : "=r"(context->gpr[2]));
    __asm__ volatile("mr %0, 13" : "=r"(context->gpr[13]));
    context->srr0 = context->lr;
    return 0;
}

void OSSaveFPUContext(OSContext* context)
{
    (void) context;
}

void OSLoadFPUContext(OSContext* context)
{
    (void) context;
}

/* OSReset.c: the reset and power buttons are wiilib's watcher thread's */
unsigned long OSGetResetCode(void)
{
    return 0;
}

BOOL OSGetResetSwitchState(void)
{
    return FALSE;
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu)
{
    OSReport("OSResetSystem(%d, 0x%x, %d): leaving\n", reset,
             (unsigned) resetCode, forceMenu);
    nk_wii_exit();
}

/* OSRtc.c: the console's settings stand for the GameCube's SRAM */
static u32 s_progressive = (u32) -1;
static u32 s_sound_mode = (u32) -1;

u32 OSGetProgressiveMode(void)
{
    if (s_progressive == (u32) -1) {
        s_progressive = CONF_GetProgressiveScan() > 0 ? 1 : 0;
    }
    return s_progressive;
}

void OSSetProgressiveMode(u32 on)
{
    s_progressive = on ? 1 : 0;
}

u32 OSGetSoundMode(void)
{
    if (s_sound_mode == (u32) -1) {
        /* CONF_SOUND_MONO is 0 */
        s_sound_mode = CONF_GetSoundMode() == 0 ? 0 : 1;
    }
    return s_sound_mode;
}

void OSSetSoundMode(u32 mode)
{
    s_sound_mode = mode ? 1 : 0;
}

/* base/PPCArch.c */
u32 PPCMfmsr(void)
{
    u32 v;
    __asm__ volatile("mfmsr %0" : "=r"(v));
    return v;
}

void PPCMtmsr(u32 v)
{
    __asm__ volatile("mtmsr %0" ::"r"(v) : "memory");
}

u32 PPCMfhid2(void)
{
    u32 v;
    __asm__ volatile("mfspr %0, 920" : "=r"(v));
    return v;
}

void PPCMthid2(u32 v)
{
    __asm__ volatile("mtspr 920, %0" ::"r"(v) : "memory");
}

u32 PPCMfwpar(void)
{
    u32 v;
    __asm__ volatile("sync; mfspr %0, 921" : "=r"(v)::"memory");
    return v;
}

void PPCMtwpar(u32 v)
{
    __asm__ volatile("mtspr 921, %0" ::"r"(v) : "memory");
}

/* The SDK's is a system call whose handler syncs; libogc's system call is
 * its own */
void PPCSync(void)
{
    __asm__ volatile("sync; isync" ::: "memory");
}

/* db.c */
BOOL DBIsDebuggerPresent(void)
{
    return FALSE;
}

/* OSExi.c: GXInit's reset of the EXI probes */
void EXIProbeReset(void) {}


/* OSError.c's: the game turns the reports off on a retail console
 * (boot.c); the port keeps its log */

/* OS.c: a retail console's */
u32 OSGetConsoleType(void)
{
    return OS_CONSOLE_RETAIL1;
}

/* __ppc_eabi_init.c's: code written at run time made fetchable */
void __flush_cache(void* address, unsigned int size)
{
    DCStoreRange(address, size);
    ICInvalidateRange(address, size);
}

/* OSStopwatch.c, OSTime.c's calendar are the SDK's; the system time is the
 * time base as libogc keeps it (seconds since 2000, the console's clock) */

/* libforest/osreport.c's (left out: the port's OSReport is above) */
void OSChangeBootMode(u32 mode)
{
    (void) mode;
}

void OSDVDFatalError(void)
{
    nk_wii_fatal("The game's disc image could not be read.\n\n%s", nk_wii_disc_path());
}
