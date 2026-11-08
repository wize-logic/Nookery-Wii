/* The log over the network, for runs on a console with no cable to a
 * computer, which starts boot.dol with wiiload and the
 * arguments "net=listen:PORT run=NAME", connects to PORT and keeps every log
 * line. The protocol:
 *     HELLO WLNET1 <run name>\n   first on each connection
 *     <log line>\n                a line starting with HELLO, FILE, END or QUIT
 *                                 gets a space before it
 *     END\n                       the last thing sent
 * and "QUIT\n" from the PC, which leaves for the Homebrew Channel.
 *
 * The log's lines go into a ring (the sink may run with interrupts disabled);
 * one thread of low priority brings the network up (nk_socket.c's start),
 * waits for the PC and sends what the ring holds. A full ring drops new lines
 * and says how many. Off unless the program was started with net=. */
#include "nk_wii.h"
#include "../net/nk_socket.h"

#include <gccore.h>
#include <network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wl/platform/platform.h>

extern void (*g_wl_log_tee)(const char* text, int len);

#define RING_BYTES (256u * 1024u)

static char* s_ring;
static volatile uint32_t s_head, s_tail; /* bytes written, bytes sent */
static volatile uint32_t s_dropped;
static uint16_t s_port;
static char s_name[48];
static lwp_t s_thread = LWP_THREAD_NULL;
static uint8_t s_stack[32 * 1024] __attribute__((aligned(32)));

static void ring_put(const char* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        s_ring[(s_head + i) % RING_BYTES] = p[i];
    }
    s_head += n;
}

static void tee(const char* text, int len)
{
    static const char* const words[] = {"HELLO", "FILE", "END", "QUIT"};
    int space = 0;
    for (int i = 0; i < 4; i++) {
        size_t w = strlen(words[i]);
        if (strncmp(text, words[i], w) == 0) {
            space = 1;
        }
    }
    u32 level = IRQ_Disable();
    if (s_head - s_tail + (uint32_t) len + 2u > RING_BYTES) {
        s_dropped++;
    } else {
        if (space) {
            ring_put(" ", 1);
        }
        ring_put(text, (uint32_t) len);
        ring_put("\n", 1);
    }
    IRQ_Restore(level);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = {ms / 1000, (long) (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static int send_all(int32_t s, const char* p, uint32_t n)
{
    while (n > 0) {
        int32_t r = net_send(s, p, n > 4096 ? 4096 : n, 0);
        if (r <= 0) {
            return -1;
        }
        p += r;
        n -= (uint32_t) r;
    }
    return 0;
}

/* Sends what the ring holds; -1 when the connection broke */
static int pump(int32_t s)
{
    static char out[8192];
    for (;;) {
        u32 level = IRQ_Disable();
        uint32_t n = s_head - s_tail;
        if (n > sizeof(out)) {
            n = sizeof(out);
        }
        for (uint32_t i = 0; i < n; i++) {
            out[i] = s_ring[(s_tail + i) % RING_BYTES];
        }
        uint32_t dropped = s_dropped;
        s_dropped = 0;
        IRQ_Restore(level);
        if (dropped != 0) {
            char note[64];
            int m = snprintf(note, sizeof(note), "[W][netlog] %u lines dropped\n", (unsigned) dropped);
            if (send_all(s, note, (uint32_t) m) < 0) {
                return -1;
            }
        }
        if (n == 0) {
            return 0;
        }
        if (send_all(s, out, n) < 0) {
            return -1;
        }
        level = IRQ_Disable();
        s_tail += n;
        IRQ_Restore(level);
    }
}

/* pad.c: controller 1 held from the PC for a number of reads */
void nk_wii_pad_remote(u16 buttons, int stick_x, int stick_y, int sub_x, int sub_y, u32 reads);

/* "PAD BUTTONS FRAMES [STICK_X STICK_Y [SUB_X SUB_Y]]" (BUTTONS the PAD_BUTTON_*
 * bits in hex, as input.txt's): controller 1 holds them for FRAMES reads, on
 * top of a real one. For playing a test console from the PC. */
static void command(const char* line)
{
    unsigned buttons, frames;
    int sx = 0, sy = 0, cx = 0, cy = 0;
    if (sscanf(line, "PAD %x %u %d %d %d %d", &buttons, &frames, &sx, &sy, &cx, &cy) >= 2) {
        nk_wii_pad_remote((u16) buttons, sx, sy, cx, cy, frames);
        WL_LOG_I("netlog", "PAD %04x for %u frames, stick %d %d", buttons, frames, sx, sy);
    }
}

static void* netlog_main(void* arg)
{
    (void) arg;
    nk_socket_init(); /* the network's start (DHCP), shared with online play */
    while (!nk_net_ready()) {
        sleep_ms(100);
    }
    char ip[16];
    nk_format_ipv4(nk_local_ip(), ip, sizeof(ip));
    WL_LOG_I("netlog", "waiting for the PC on %s:%u", ip, (unsigned) s_port);
    for (;;) {
        int32_t ls = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (ls < 0) {
            sleep_ms(1000);
            continue;
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_len = sizeof(addr);
        addr.sin_port = htons(s_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (net_bind(ls, (struct sockaddr*) &addr, sizeof(addr)) < 0 || net_listen(ls, 1) < 0) {
            net_close(ls);
            sleep_ms(1000);
            continue;
        }
        struct sockaddr_in from;
        u32 from_len = sizeof(from);
        int32_t s = net_accept(ls, (struct sockaddr*) &from, &from_len);
        net_close(ls);
        if (s < 0) {
            sleep_ms(500);
            continue;
        }
        char hello[96];
        int n = snprintf(hello, sizeof(hello), "HELLO WLNET1 %s\n", s_name);
        if (send_all(s, hello, (uint32_t) n) == 0) {
            u32 one = 1;
            net_ioctl(s, FIONBIO, &one);
            char in[256];
            uint32_t in_len = 0;
            for (;;) {
                if (pump(s) < 0) {
                    break;
                }
                int32_t r = net_recv(s, in + in_len, sizeof(in) - 1 - in_len, 0);
                if (r == 0) {
                    break; /* the PC closed */
                }
                if (r > 0) {
                    in_len += (uint32_t) r;
                    in[in_len] = '\0';
                    /* whole lines: QUIT, or PAD (controller 1 from the PC) */
                    char* nl;
                    while ((nl = strchr(in, '\n')) != NULL) {
                        *nl = '\0';
                        if (strncmp(in, "QUIT", 4) == 0) {
                            WL_LOG_I("netlog", "QUIT from the PC: back to the loader");
                            pump(s);
                            send_all(s, "END\n", 4);
                            net_close(s);
                            nk_wii_exit();
                        }
                        command(in);
                        uint32_t used = (uint32_t) (nl + 1 - in);
                        memmove(in, nl + 1, in_len - used + 1);
                        in_len -= used;
                    }
                    if (in_len >= sizeof(in) - 1) {
                        in_len = 0;
                    }
                }
                sleep_ms(20);
            }
        }
        net_close(s);
    }
    return NULL;
}

void nk_wii_netlog_start(const char* spec, const char* run_name)
{
    unsigned port;
    if (s_ring != NULL || sscanf(spec, "listen:%u", &port) != 1 || port == 0 || port > 65535) {
        WL_LOG_W("netlog", "net=%s: only net=listen:<port> is supported", spec);
        return;
    }
    s_ring = malloc(RING_BYTES);
    if (s_ring == NULL) {
        return;
    }
    s_port = (uint16_t) port;
    snprintf(s_name, sizeof(s_name), "%s", run_name != NULL && run_name[0] != '\0' ? run_name : "nookery");
    for (char* c = s_name; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
              *c == '_' || *c == '-')) {
            *c = '_';
        }
    }
    g_wl_log_tee = tee;
    LWP_CreateThread(&s_thread, netlog_main, NULL, s_stack, sizeof(s_stack), 40);
}
