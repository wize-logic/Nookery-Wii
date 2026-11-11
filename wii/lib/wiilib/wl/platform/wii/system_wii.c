// SPDX-License-Identifier: MIT
// System buttons, their watcher thread and exit (platform.h). The reset and power callbacks run in interrupt context: they
// only latch a bit, collected by wl_platform_system_events or the watcher thread.
#include <gccore.h>
#include <ogc/stm.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wl/platform/platform.h"

static atomic_uint g_events;
static atomic_uint g_exit_request; // wl_platform_exit_request: a WlExitTarget + 1
static bool g_sys_init;
static lwp_t g_watch_thread = LWP_THREAD_NULL;

static void on_reset(u32 irq, void *ctx) {
    (void)irq;
    (void)ctx;
    atomic_fetch_or(&g_events, (unsigned)WL_SYS_RESET);
}

static void on_power(void) {
    atomic_fetch_or(&g_events, (unsigned)WL_SYS_POWER);
}

// The loader's stub as the program found it (0x80001800 up to 0x80003000), put back before leaving should
// anything have written over it since
#define LOADER_STUB 0x80001800u
#define LOADER_STUB_SIZE 0x1800u
static uint8_t g_loader_stub[LOADER_STUB_SIZE] __attribute__((aligned(32)));
static bool g_have_loader_stub;

static void loader_stub_restore(void) {
    if (g_have_loader_stub && memcmp((const void *)LOADER_STUB, g_loader_stub, LOADER_STUB_SIZE) != 0) {
        WL_LOG_W("SYS", "the loader's stub was written over: put back");
        memcpy((void *)LOADER_STUB, g_loader_stub, LOADER_STUB_SIZE);
        DCFlushRange((void *)LOADER_STUB, LOADER_STUB_SIZE);
        ICInvalidateRange((void *)LOADER_STUB, LOADER_STUB_SIZE);
    }
}

void wl_platform_loader_stub_keep(void) {
    if (memcmp((const void *)(LOADER_STUB + 4u), "STUBHAXX", 8) == 0) {
        memcpy(g_loader_stub, (const void *)LOADER_STUB, LOADER_STUB_SIZE);
        g_have_loader_stub = true;
    }
}

void wl_platform_system_init(void) {
    if (g_sys_init) {
        return;
    }
    g_sys_init = true;
    wl_platform_loader_stub_keep();
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
}

uint32_t wl_platform_system_events(void) {
    return atomic_exchange(&g_events, 0u);
}

// The Homebrew Channel (and the loaders that copy it) leave a reload stub at 0x80001800 with "STUBHAXX" after its
// first word; libogc's exit jumps there. Without one there is nothing to go back to, so leaving powers off
// instead. Dolphin booting the DOL itself writes the signature with no code before it (the first word 0; it
// stops the emulation when the address is reached): that is no stub either, and powering off ends it as well.
static bool loader_stub(void) {
    return memcmp((const void *)(LOADER_STUB + 4u), "STUBHAXX", 8) == 0 && *(const volatile uint32_t *)LOADER_STUB != 0u;
}

// The program's own wind-down before the system's (its threads that use IOS stopped, its audio
// stopped), when it defines one
void wl_platform_exit_prepare(int to_loader) __attribute__((weak));

WL_NORETURN void wl_platform_exit(WlExitTarget target) {
    static atomic_flag leaving = ATOMIC_FLAG_INIT;
    if (atomic_flag_test_and_set(&leaving)) {
        for (;;) {
            usleep(100u * 1000u); // the first caller leaves
        }
    }
    loader_stub_restore();
    if (target == WL_EXIT_LOADER && !loader_stub()) {
        target = WL_EXIT_POWER_OFF;
    }
    if (wl_platform_exit_prepare != NULL) {
        wl_platform_exit_prepare(target == WL_EXIT_LOADER);
    }
    // black for the TV before the video stops (two fields' time: no retrace is waited for, which a program that
    // never started the video would not get)
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    usleep(34u * 1000u);
    wl_platform_log_flush();
    wl_platform_log_shutdown();
    wl_platform_fs_unmount(WL_DEVICE_SD);
    wl_platform_fs_unmount(WL_DEVICE_USB);
    switch (target) {
    case WL_EXIT_WII_MENU: SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0); break;
    case WL_EXIT_POWER_OFF: STM_ShutdownToStandby(); break; // also ends emulation under Dolphin --batch
    case WL_EXIT_LOADER:
    default:
        // back to the Homebrew Channel or whatever loaded us, as libogc's exit ends: the system shut down, then the
        // loader's stub. Not exit() itself: it would first run the program's atexit functions and static
        // destructors (with its other threads still there) and have stdio close every FILE left open, the ones of
        // the devices unmounted above too (a crash, no return)
        loader_stub_restore();
        SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
        ((void (*)(void))LOADER_STUB)();
        break;
    }
    for (;;) {
        usleep(100u * 1000u);
    }
}

WlResult wl_platform_exit_request(WlExitTarget target) {
    if (g_watch_thread == LWP_THREAD_NULL) {
        return WL_ERR_STATE;
    }
    unsigned none = 0u;
    atomic_compare_exchange_strong(&g_exit_request, &none, (unsigned)target + 1u); // the first request stays
    return WL_OK;
}

// The watcher: a thread above the program's, so the buttons work while it loads or stalls
static uint32_t g_watch_period_ms;
static void (*g_watch_tick)(void *user);
static void *g_watch_user;
static uint8_t g_watch_stack[32 * 1024] __attribute__((aligned(32))); // (leaving runs on it)

static void *watch_main(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t events = wl_platform_system_events();
        if (events & WL_SYS_POWER) {
            WL_LOG_I("SYS", "power button: leaving");
            wl_platform_exit(WL_EXIT_POWER_OFF);
        }
        if (events & WL_SYS_RESET) {
            WL_LOG_I("SYS", "reset button: leaving");
            wl_platform_exit(WL_EXIT_LOADER);
        }
        unsigned request = atomic_load(&g_exit_request);
        if (request != 0u) {
            wl_platform_exit((WlExitTarget)(request - 1u));
        }
        if (g_watch_tick != NULL) {
            g_watch_tick(g_watch_user);
        }
        usleep(g_watch_period_ms * 1000u);
    }
    return NULL;
}

WlResult wl_platform_system_watch_start(uint32_t period_ms, void (*tick)(void *user), void *user) {
    if (g_watch_thread != LWP_THREAD_NULL) {
        return WL_ERR_STATE;
    }
    wl_platform_system_init();
    g_watch_period_ms = period_ms != 0u ? period_ms : 16u;
    g_watch_tick = tick;
    g_watch_user = user;
    if (LWP_CreateThread(&g_watch_thread, watch_main, NULL, g_watch_stack, sizeof g_watch_stack, 110) < 0) {
        g_watch_thread = LWP_THREAD_NULL;
        return WL_ERR_FAILED;
    }
    return WL_OK;
}
