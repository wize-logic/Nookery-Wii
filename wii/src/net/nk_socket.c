/* nk_socket.h on libogc's sockets: one thread makes every IOS call (the
 * design Wi-Fi needs, see nk_socket.h). */

#include "nk_socket.h"

#include <gccore.h>
#include <network.h>
#include <ogc/cond.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>

#include <wl/base/base.h>

/* IOS's poll flag (BSD's; <poll.h>'s POLLIN is not it) */
#define IOS_POLLIN 0x0001

/* The length of an IPv4 address as IOS takes it: 8 (its length, family,
 * port, address), not libogc's sizeof(struct sockaddr_in), 16. A Wii's IOS
 * sends nothing to an address of 16 (Dolphin's takes either). */
#define IOS_SOCKADDR_LEN 8

#define RX_SLOTS 48
#define LOBBY_SLOTS 16
#define TX_SLOTS 32
#define POLL_MS 2

typedef struct {
    uint32_t addr;
    uint16_t port;
    uint16_t len;
    uint8_t data[NK_DATAGRAM_MAX];
} Datagram;

typedef struct {
    Datagram* slots;
    uint32_t count;
    volatile uint32_t head, tail;
} Ring;

static Datagram s_rx_slots[RX_SLOTS];
static Datagram s_lobby_slots[LOBBY_SLOTS];
static Datagram s_tx_slots[TX_SLOTS];
static Ring s_rx = {s_rx_slots, RX_SLOTS, 0, 0};
static Ring s_lobby = {s_lobby_slots, LOBBY_SLOTS, 0, 0};
static Ring s_tx = {s_tx_slots, TX_SLOTS, 0, 0};

static mutex_t s_lock = LWP_MUTEX_NULL;
static cond_t s_rx_cond = LWP_COND_NULL; /* a game datagram came */
static cond_t s_io_cond = LWP_COND_NULL; /* the I/O thread has work */

static volatile int s_net_ready;
static volatile int s_quit; /* nk_socket_shutdown: the threads end */
static volatile uint32_t s_ip;
static volatile int s_want_port = -1;
static int32_t s_sock = -1;
static volatile uint16_t s_port;
static lwp_t s_up_thread = LWP_THREAD_NULL, s_io_thread = LWP_THREAD_NULL;
static uint8_t s_up_stack[16 * 1024] __attribute__((aligned(32)));
static uint8_t s_io_stack[32 * 1024] __attribute__((aligned(32)));

uint32_t nk_time_ms(void)
{
    return (uint32_t) ticks_to_millisecs(gettime());
}

uint32_t nk_random(void)
{
    static uint32_t state;
    if (state == 0) {
        state = (uint32_t) gettime() ^ 0x9E3779B9u ^ s_ip;
        if (state == 0) {
            state = 1;
        }
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = {ms / 1000, (long) (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void* net_up(void* arg)
{
    (void) arg;
    /* libogc's if_config, which waits for DHCP and tries again itself,
     * rather than net_init alone (what works on consoles) */
    for (;;) {
        char ip_text[16] = {0};
        if (s_quit) {
            return NULL;
        }
        int32_t res = if_config(ip_text, NULL, NULL, true, 20);
        if (res >= 0) {
            break;
        }
        WL_LOG_W("net", "the network is not up (%d); trying again", (int) res);
        sleep_ms(2000);
    }
    s_ip = net_gethostip();
    s_net_ready = 1;
    char ip[16];
    nk_format_ipv4(s_ip, ip, sizeof(ip));
    WL_LOG_I("net", "the network is up: %s", ip);
    LWP_MutexLock(s_lock);
    LWP_CondSignal(s_io_cond);
    LWP_MutexUnlock(s_lock);
    return NULL;
}

static int32_t open_udp(uint16_t port)
{
    struct sockaddr_in name;
    int32_t s = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) {
        return s;
    }
    int one = 1;
    net_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_len = IOS_SOCKADDR_LEN;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = INADDR_ANY;
    if (net_bind(s, (struct sockaddr*) &name, IOS_SOCKADDR_LEN) < 0) {
        net_close(s);
        return -2;
    }
    return s;
}

static void send_one(const Datagram* d)
{
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_len = IOS_SOCKADDR_LEN;
    to.sin_addr.s_addr = htonl(d->addr);
    to.sin_port = htons(d->port);
    int32_t n = net_sendto(s_sock, d->data, d->len, 0, (struct sockaddr*) &to, IOS_SOCKADDR_LEN);
    if (n < 0) {
        static int logged;
        if (logged++ < 8) {
            WL_LOG_W("net", "send to %u.%u.%u.%u:%u failed (%d)", (unsigned) (d->addr >> 24),
                     (unsigned) (d->addr >> 16) & 255, (unsigned) (d->addr >> 8) & 255, (unsigned) d->addr & 255,
                     (unsigned) d->port, (int) n);
        }
    }
}

static void receive_one(void)
{
    static uint8_t buf[NK_DATAGRAM_MAX] __attribute__((aligned(32)));
    struct sockaddr_in from;
    uint32_t from_len = sizeof(from);
    int32_t n = net_recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr*) &from, &from_len);
    if (n <= 0) {
        return;
    }
    uint32_t magic = n >= 4 ? ((uint32_t) buf[0] << 24 | (uint32_t) buf[1] << 16 | (uint32_t) buf[2] << 8 | buf[3]) : 0;
    Ring* ring = magic == NK_LOBBY_MAGIC ? &s_lobby : &s_rx;
    LWP_MutexLock(s_lock);
    if (ring->head - ring->tail < ring->count) {
        Datagram* d = &ring->slots[ring->head % ring->count];
        d->addr = ntohl(from.sin_addr.s_addr);
        d->port = ntohs(from.sin_port);
        d->len = (uint16_t) n;
        memcpy(d->data, buf, n);
        ring->head++;
        if (ring == &s_rx) {
            LWP_CondBroadcast(s_rx_cond);
        }
    }
    LWP_MutexUnlock(s_lock);
}

static void* io_main(void* arg)
{
    (void) arg;
    while (!s_quit) {
        if (!s_net_ready) {
            sleep_ms(50);
            continue;
        }
        /* open, reopen or close the socket, as asked */
        int want = s_want_port;
        if (want >= 0 && (s_sock < 0 || s_port != (uint16_t) want)) {
            if (s_sock >= 0) {
                net_close(s_sock);
            }
            s_sock = open_udp((uint16_t) want);
            if (s_sock < 0) {
                WL_LOG_W("net", "no socket on port %u (%d)", (unsigned) want, (int) s_sock);
                s_want_port = -1;
            } else {
                s_port = (uint16_t) want;
            }
        } else if (want < 0 && s_sock >= 0) {
            net_close(s_sock);
            s_sock = -1;
        }
        if (s_sock < 0) {
            LWP_MutexLock(s_lock);
            struct timespec ts = {0, 20 * 1000000L};
            LWP_CondTimedWait(s_io_cond, s_lock, &ts);
            LWP_MutexUnlock(s_lock);
            continue;
        }
        /* what is queued */
        Datagram out;
        int have = 0;
        LWP_MutexLock(s_lock);
        if (s_tx.tail != s_tx.head) {
            out = s_tx.slots[s_tx.tail % s_tx.count];
            s_tx.tail++;
            have = 1;
        }
        LWP_MutexUnlock(s_lock);
        if (have) {
            send_one(&out);
            continue;
        }
        /* then what has come */
        struct pollsd pfd;
        pfd.socket = s_sock;
        pfd.events = IOS_POLLIN;
        pfd.revents = 0;
        int32_t polled = net_poll(&pfd, 1, POLL_MS);
        if (polled > 0 && (pfd.revents & IOS_POLLIN)) {
            receive_one();
        } else if (polled < 0) {
            /* (an error comes back at once: never spin above the game) */
            sleep_ms(10);
        }
    }
    return NULL;
}

/* Before leaving the program: the I/O thread stopped, the socket closed and
 * the network taken down, so that the system's own shutdown (libogc's exit
 * into the Homebrew Channel's stub) finds nothing of ours running */
void nk_socket_shutdown(void)
{
    if (s_io_thread == LWP_THREAD_NULL) {
        return;
    }
    s_quit = 1;
    LWP_MutexLock(s_lock);
    LWP_CondSignal(s_io_cond);
    LWP_MutexUnlock(s_lock);
    LWP_JoinThread(s_io_thread, NULL);
    s_io_thread = LWP_THREAD_NULL;
    if (s_sock >= 0) {
        net_close(s_sock);
        s_sock = -1;
    }
    if (s_net_ready) {
        LWP_JoinThread(s_up_thread, NULL);
        s_net_ready = 0;
        net_deinit();
    }
    WL_LOG_I("net", "the network is down");
}

int nk_socket_init(void)
{
    static int started;
    if (started) {
        return 0;
    }
    LWP_MutexInit(&s_lock, false);
    LWP_CondInit(&s_rx_cond);
    LWP_CondInit(&s_io_cond);
    if (LWP_CreateThread(&s_up_thread, net_up, NULL, s_up_stack, sizeof(s_up_stack), 40) < 0 ||
        LWP_CreateThread(&s_io_thread, io_main, NULL, s_io_stack, sizeof(s_io_stack), 90) < 0)
    {
        return -1;
    }
    started = 1;
    return 0;
}

int nk_net_ready(void)
{
    return s_net_ready;
}

uint32_t nk_local_ip(void)
{
    return s_ip;
}

int nk_socket_open(uint16_t port)
{
    LWP_MutexLock(s_lock);
    s_rx.tail = s_rx.head;
    s_lobby.tail = s_lobby.head;
    s_want_port = port;
    LWP_CondSignal(s_io_cond);
    LWP_MutexUnlock(s_lock);
    for (int waited = 0; waited < 2000; waited += 5) {
        if (s_sock >= 0 && s_port == port) {
            return 0;
        }
        if (s_want_port < 0 && s_net_ready) {
            return -1;
        }
        sleep_ms(5);
    }
    return s_sock >= 0 && s_port == port ? 0 : -1;
}

void nk_socket_close(void)
{
    s_want_port = -1;
}

int nk_socket_is_open(void)
{
    return s_sock >= 0 && s_want_port >= 0;
}

uint16_t nk_socket_port(void)
{
    return s_port;
}

int nk_socket_sendv(uint32_t addr, uint16_t port, const NkSlice* slices, int count)
{
    uint32_t len = 0;
    LWP_MutexLock(s_lock);
    if (s_tx.head - s_tx.tail >= s_tx.count) {
        LWP_MutexUnlock(s_lock);
        return -2;
    }
    Datagram* d = &s_tx.slots[s_tx.head % s_tx.count];
    for (int i = 0; i < count; i++) {
        if (len + slices[i].len > NK_DATAGRAM_MAX) {
            LWP_MutexUnlock(s_lock);
            return -1;
        }
        memcpy(d->data + len, slices[i].data, slices[i].len);
        len += slices[i].len;
    }
    d->addr = addr;
    d->port = port;
    d->len = (uint16_t) len;
    s_tx.head++;
    LWP_CondSignal(s_io_cond);
    LWP_MutexUnlock(s_lock);
    return (int) len;
}

int nk_socket_send(uint32_t addr, uint16_t port, const void* data, uint32_t len)
{
    NkSlice slice = {data, len};
    return nk_socket_sendv(addr, port, &slice, 1);
}

static int take(Ring* ring, uint32_t* addr, uint16_t* port, void* buf, uint32_t size)
{
    int n = 0;
    LWP_MutexLock(s_lock);
    if (ring->tail != ring->head) {
        Datagram* d = &ring->slots[ring->tail % ring->count];
        n = d->len < size ? d->len : (int) size;
        memcpy(buf, d->data, n);
        if (addr != NULL) {
            *addr = d->addr;
        }
        if (port != NULL) {
            *port = d->port;
        }
        ring->tail++;
    }
    LWP_MutexUnlock(s_lock);
    return n;
}

int nk_socket_receive(uint32_t* addr, uint16_t* port, void* buf, uint32_t size)
{
    return take(&s_rx, addr, port, buf, size);
}

int nk_socket_receive_lobby(uint32_t* addr, uint16_t* port, void* buf, uint32_t size)
{
    return take(&s_lobby, addr, port, buf, size);
}

int nk_socket_wait(uint32_t timeout_us)
{
    int ready;
    LWP_MutexLock(s_lock);
    if (s_rx.tail == s_rx.head && timeout_us > 0) {
        struct timespec ts = {timeout_us / 1000000, (long) (timeout_us % 1000000) * 1000L};
        LWP_CondTimedWait(s_rx_cond, s_lock, &ts);
    }
    ready = s_rx.tail != s_rx.head;
    LWP_MutexUnlock(s_lock);
    return ready;
}

int nk_resolve(const char* name, uint32_t* addr)
{
    unsigned a, b, c, d;
    char tail;
    if (sscanf(name, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
        *addr = (a << 24) | (b << 16) | (c << 8) | d;
        return 0;
    }
    if (!s_net_ready) {
        return -1;
    }
    /* net_gethostbyname's answer is one static hostent: one lookup at a
     * time, and only an IPv4 one taken */
    static mutex_t lookup_lock = LWP_MUTEX_NULL;
    u32 level = IRQ_Disable();
    if (lookup_lock == LWP_MUTEX_NULL) {
        LWP_MutexInit(&lookup_lock, false);
    }
    IRQ_Restore(level);
    LWP_MutexLock(lookup_lock);
    struct hostent* h = net_gethostbyname(name);
    int ok = h != NULL && h->h_addrtype == AF_INET && h->h_addr_list != NULL && h->h_addr_list[0] != NULL;
    uint32_t v = 0;
    if (ok) {
        memcpy(&v, h->h_addr_list[0], 4);
    }
    LWP_MutexUnlock(lookup_lock);
    if (!ok) {
        return -1;
    }
    *addr = ntohl(v);
    return 0;
}

void nk_format_ipv4(uint32_t addr, char* out, int size)
{
    snprintf(out, size, "%u.%u.%u.%u", (unsigned) (addr >> 24), (unsigned) (addr >> 16) & 0xFF,
             (unsigned) (addr >> 8) & 0xFF, (unsigned) addr & 0xFF);
}
