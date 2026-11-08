/* Nookery's UDP socket on libogc (IOS's network stack): one socket, kept for
 * the whole online session, shared by ENet (the game session) and the lobby
 * protocol (nk_lobby.c), whose datagrams start with NK_LOBBY_MAGIC.
 *
 * IOS socket calls block for milliseconds and stall when several threads
 * call at once on Wi-Fi, so one
 * thread of its own makes every call in turn: the queued sends, then a short
 * poll and a receive. Nothing that calls in here waits on IOS. Addresses are
 * IPv4 in host order (the Wii's is network order). */
#ifndef NK_SOCKET_H
#define NK_SOCKET_H

#include <stdint.h>

#define NK_DATAGRAM_MAX 1500
/* The lobby protocol's datagrams begin with these four bytes */
#define NK_LOBBY_MAGIC 0x4E4B4C42u /* "NKLB" */

/* Starts the network (DHCP in the background) and the I/O thread */
int nk_socket_init(void);
/* Before leaving the program: the network thread stopped, the network down */
void nk_socket_shutdown(void);
/* The network is up (net_init done); the console's address, 0 before */
int nk_net_ready(void);
uint32_t nk_local_ip(void);

/* Opens (or reopens) the socket on `port` (broadcast allowed); waits for the
 * I/O thread up to 2 s. 0, or < 0 */
int nk_socket_open(uint16_t port);
void nk_socket_close(void);
int nk_socket_is_open(void);
uint16_t nk_socket_port(void);

typedef struct NkSlice {
    const void* data;
    uint32_t len;
} NkSlice;

/* Queues a datagram: its length, -2 when the queue is full (dropped, as UDP
 * may), -1 on error. addr 0xFFFFFFFF broadcasts. */
int nk_socket_send(uint32_t addr, uint16_t port, const void* data, uint32_t len);
int nk_socket_sendv(uint32_t addr, uint16_t port, const NkSlice* slices, int count);

/* The oldest game (ENet) datagram, or the oldest lobby one: its length, 0
 * when none is waiting */
int nk_socket_receive(uint32_t* addr, uint16_t* port, void* buf, uint32_t size);
int nk_socket_receive_lobby(uint32_t* addr, uint16_t* port, void* buf, uint32_t size);

/* Waits up to `timeout_us` for a game datagram; 1 when one is there */
int nk_socket_wait(uint32_t timeout_us);

/* Resolves a host name (or dotted address); 0, or < 0. Blocking: not on the
 * game's thread. */
int nk_resolve(const char* name, uint32_t* addr);
void nk_format_ipv4(uint32_t addr, char* out, int size);

uint32_t nk_time_ms(void);
uint32_t nk_random(void);

#endif
