// SPDX-License-Identifier: MIT
// The log's sink (platform.h): each line to the EXI UART
// through SYS_Report (Dolphin's OSREPORT log; harmless on hardware), and to a file on the SD card.
// The sink can be called with interrupts disabled, from an interrupt even: the file's lines go
// to a buffer, which a thread of its own writes out.
#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wl/platform/platform.h"

#define LOG_BUFFER_BYTES (8u * 1024u)

static bool g_installed;
static WlPlatformFile *g_file;
static char g_buffer[LOG_BUFFER_BYTES];
static uint32_t g_used;
static uint32_t g_dropped;
static mutex_t g_write_mutex = LWP_MUTEX_NULL;
static lwp_t g_thread = LWP_THREAD_NULL;
static uint8_t g_stack[8 * 1024] __attribute__((aligned(32)));

// Another taker of each formatted line (no newline), when set: Nookery's network log
// (wii/src/ogc/netlog.c). Called as the sink is: interrupts may be disabled.
void (*g_wl_log_tee)(const char *text, int len);

static void sink(void *user, WlLogLevel level, const char *tag, const char *line) {
    static const char letters[] = "EWID";
    (void)user;
    char text[600];
    int n = snprintf(text, sizeof text, "[%c][%s] %s", letters[level], tag, line);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof text) {
        n = (int)sizeof text - 1;
    }
    SYS_Report("%s\n", text);
    if (g_wl_log_tee != NULL) {
        g_wl_log_tee(text, n);
    }
    if (g_file == NULL) {
        return;
    }
    u32 level_irq = IRQ_Disable();
    if (g_used + (uint32_t)n + 1u <= LOG_BUFFER_BYTES) {
        memcpy(g_buffer + g_used, text, (size_t)n);
        g_buffer[g_used + (uint32_t)n] = '\n';
        g_used += (uint32_t)n + 1u;
    } else {
        g_dropped++;
    }
    IRQ_Restore(level_irq);
}

void wl_platform_log_flush(void) {
    static char out[LOG_BUFFER_BYTES];
    if (g_file == NULL || g_write_mutex == LWP_MUTEX_NULL) {
        return;
    }
    LWP_MutexLock(g_write_mutex);
    u32 level_irq = IRQ_Disable();
    uint32_t n = g_used;
    uint32_t dropped = g_dropped;
    memcpy(out, g_buffer, n);
    g_used = 0;
    g_dropped = 0;
    IRQ_Restore(level_irq);
    if (g_file != NULL && n != 0u) {
        wl_platform_fs_write(g_file, out, n);
        if (dropped != 0u) {
            char note[64];
            int m = snprintf(note, sizeof note, "[W][LOG] %u lines dropped\n", (unsigned)dropped);
            wl_platform_fs_write(g_file, note, (uint32_t)m);
        }
        wl_platform_fs_flush(g_file);
    }
    LWP_MutexUnlock(g_write_mutex);
}

static void *writer_main(void *arg) {
    (void)arg;
    for (;;) {
        usleep(1000u * 1000u);
        wl_platform_log_flush();
    }
    return NULL;
}

void wl_platform_log_init(const char *sd_dir) {
    if (!g_installed) {
        g_installed = true;
        wl_log_set_sink(sink, NULL);
    }
    if (sd_dir == NULL || g_file != NULL || !wl_platform_fs_mounted(WL_DEVICE_SD)) {
        return;
    }
    // each level of the folder made in turn
    char path[192];
    size_t len = strlen(sd_dir);
    if (len + sizeof "/log.txt" > sizeof path) {
        return;
    }
    memcpy(path, sd_dir, len + 1);
    for (size_t i = sizeof "sd:/"; i <= len; i++) {
        if (path[i] == '/' || path[i] == '\0') {
            char c = path[i];
            path[i] = '\0';
            wl_platform_fs_mkdir(path);
            path[i] = c;
        }
    }
    memcpy(path + len, "/log.txt", sizeof "/log.txt");
    {
        // the last two runs' logs kept: log.txt -> log-1.txt -> log-2.txt (so starting again after a crash
        // doesn't lose its report)
        char older[sizeof path + 8];
        char old[sizeof path + 8];
        snprintf(older, sizeof older, "%.*s/log-2.txt", (int)len, sd_dir);
        snprintf(old, sizeof old, "%.*s/log-1.txt", (int)len, sd_dir);
        remove(older);
        rename(old, older);
        rename(path, old);
    }
    if (wl_platform_fs_open(path, WL_FILE_WRITE_NEW, &g_file) != WL_OK) {
        g_file = NULL;
        return;
    }
    LWP_MutexInit(&g_write_mutex, false);
    LWP_CreateThread(&g_thread, writer_main, NULL, g_stack, sizeof g_stack, 20);
}

void wl_platform_log_shutdown(void) {
    wl_platform_log_flush();
    if (g_file != NULL && g_write_mutex != LWP_MUTEX_NULL) {
        LWP_MutexLock(g_write_mutex);
        WlPlatformFile *file = g_file;
        g_file = NULL;
        wl_platform_fs_close(file);
        LWP_MutexUnlock(g_write_mutex);
    }
}
