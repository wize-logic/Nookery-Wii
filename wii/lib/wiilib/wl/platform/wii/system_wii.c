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
static bool g_sys_init;

static void on_reset(u32 irq, void *ctx) {
    (void)irq;
    (void)ctx;
    atomic_fetch_or(&g_events, (unsigned)WL_SYS_RESET);
}

static void on_power(void) {
    atomic_fetch_or(&g_events, (unsigned)WL_SYS_POWER);
}

void wl_platform_system_init(void) {
    if (g_sys_init) {
        return;
    }
    g_sys_init = true;
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
}

uint32_t wl_platform_system_events(void) {
    return atomic_exchange(&g_events, 0u);
}

// The Homebrew Channel (and the loaders that copy it) leave a reload stub at 0x80001800 with
// "STUBHAXX" after its first word; libogc's exit jumps there. Without one (Dolphin booting the DOL
// itself) there is nothing to go back to and the jump lands at 0, so leaving powers off instead.
// Dolphin writes the signature with no code before it (the first word 0): that is no stub either.
static bool loader_stub(void) {
    return memcmp((const void *)0x80001804, "STUBHAXX", 8) == 0 && *(const volatile uint32_t *)0x80001800 != 0u;
}

// The program's own wind-down before the system's (its threads that use IOS stopped, its audio
// stopped), when it defines one
void wl_platform_exit_prepare(int to_loader) __attribute__((weak));

WL_NORETURN void wl_platform_exit(WlExitTarget target) {
    static atomic_flag leaving = ATOMIC_FLAG_INIT;
    if (atomic_flag_test_and_set(&leaving)) {
        for (;;) {
            VIDEO_WaitVSync(); // the first caller leaves
        }
    }
    if (target == WL_EXIT_LOADER && !loader_stub()) {
        target = WL_EXIT_POWER_OFF;
    }
    if (wl_platform_exit_prepare != NULL) {
        wl_platform_exit_prepare(target == WL_EXIT_LOADER);
    }
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    wl_platform_log_flush();
    wl_platform_log_shutdown();
    wl_platform_fs_unmount(WL_DEVICE_SD);
    wl_platform_fs_unmount(WL_DEVICE_USB);
    switch (target) {
    case WL_EXIT_WII_MENU: SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0); break;
    case WL_EXIT_POWER_OFF: STM_ShutdownToStandby(); break; // also ends emulation under Dolphin --batch
    case WL_EXIT_LOADER:
    default: exit(0); break; // back to the Homebrew Channel or whatever loaded us
    }
    for (;;) {
        VIDEO_WaitVSync();
    }
}

// The watcher: a thread above the program's, so the buttons work while it loads or stalls
static lwp_t g_watch_thread = LWP_THREAD_NULL;
static uint32_t g_watch_period_ms;
static void (*g_watch_tick)(void *user);
static void *g_watch_user;
static uint8_t g_watch_stack[16 * 1024] __attribute__((aligned(32)));

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
