/**
 @file  ios.c
 @brief ENet platform functions for the Wii (libogc), on Nookery's socket

 Not part of upstream ENet; see README.md. Sockets map onto the single
 non-blocking UDP socket of wii/src/net/nk_socket.h, so servicing a host
 never waits on IOS.
*/
#define ENET_BUILDING_LIB 1
#include <string.h>
#include "enet/enet.h"
#include "../nk_socket.h"

#define IOS_SOCKET 0

static enet_uint32 timeBase;
static int socketCreated;

static void
ios_no_memory (void)
{
    /* ENet copes with a failed allocation; the default handler would abort. */
}

int
enet_ios_initialize (void)
{
    ENetCallbacks callbacks;

    callbacks.malloc = malloc;
    callbacks.free = free;
    callbacks.no_memory = ios_no_memory;

    return enet_initialize_with_callbacks (ENET_VERSION, & callbacks);
}

int
enet_initialize (void)
{
    return 0;
}

void
enet_deinitialize (void)
{
}

enet_uint32
enet_host_random_seed (void)
{
    return nk_random ();
}

enet_uint32
enet_time_get (void)
{
    return nk_time_ms () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = nk_time_ms () - newTimeBase;
}

int
enet_address_set_host (ENetAddress * address, const char * name)
{
    uint32_t host;

    if (nk_resolve (name, & host) < 0)
      return -1;

    address -> host = host;

    return 0;
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    char text [16];

    nk_format_ipv4 (address -> host, text, sizeof (text));

    if (strlen (text) >= nameLength)
      return -1;

    memcpy (name, text, strlen (text) + 1);

    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    return enet_address_get_host_ip (address, name, nameLength);
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    if (type != ENET_SOCKET_TYPE_DATAGRAM || socketCreated)
      return ENET_SOCKET_NULL;

    socketCreated = 1;

    return IOS_SOCKET;
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    uint16_t port = address != NULL ? address -> port : 0;

    if (socket != IOS_SOCKET)
      return -1;

    /* IOS cannot report the port it picks for 0, so pick one here; a socket
       the lobby opened already stays on its port. */
    if (port == 0)
      port = nk_socket_is_open () ? nk_socket_port () : (uint16_t) (41000 + nk_random () % 10000);

    return nk_socket_open (port) < 0 ? -1 : 0;
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    if (socket != IOS_SOCKET || ! nk_socket_is_open ())
      return -1;

    address -> host = nk_local_ip ();
    address -> port = nk_socket_port ();

    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    return -1;
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    return socket == IOS_SOCKET ? 0 : -1;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    if (socket != IOS_SOCKET)
      return -1;

    * value = 0;

    return 0;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    return -1;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    return ENET_SOCKET_NULL;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    return -1;
}

void
enet_socket_destroy (ENetSocket socket)
{
    /* The socket stays open for the lobby and the next host on its port. */
    if (socket == IOS_SOCKET)
      socketCreated = 0;
}

int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    NkSlice slices [ENET_BUFFER_MAXIMUM];
    size_t i;
    int sent;

    if (socket != IOS_SOCKET || address == NULL || bufferCount > ENET_BUFFER_MAXIMUM)
      return -1;

    if (! nk_socket_is_open () && enet_socket_bind (socket, NULL) < 0)
      return -1;

    for (i = 0; i < bufferCount; ++ i)
    {
        slices [i].data = buffers [i].data;
        slices [i].len = (uint32_t) buffers [i].dataLength;
    }

    sent = nk_socket_sendv (address -> host, address -> port, slices, (int) bufferCount);

    /* -2: every send buffer is in flight: as a full socket buffer */
    if (sent == -2)
      return 0;

    return sent;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    uint32_t host;
    uint16_t port;
    int length;

    if (socket != IOS_SOCKET || bufferCount != 1)
      return -1;

    length = nk_socket_receive (& host, & port, buffers [0].data, (uint32_t) buffers [0].dataLength);

    if (length > 0 && address != NULL)
    {
        address -> host = host;
        address -> port = port;
    }

    return length;
}

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    int ready = 0;

    if (readSet != NULL && ENET_SOCKETSET_CHECK (* readSet, IOS_SOCKET))
    {
        if (nk_socket_wait (timeout * 1000))
          ready = 1;
        else
          ENET_SOCKETSET_EMPTY (* readSet);
    }

    if (writeSet != NULL && ENET_SOCKETSET_CHECK (* writeSet, IOS_SOCKET))
      ready = 1;

    return ready;
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    enet_uint32 wanted = * condition;

    if (socket != IOS_SOCKET)
      return -1;

    * condition = ENET_SOCKET_WAIT_NONE;

    if (wanted & ENET_SOCKET_WAIT_SEND)
      * condition |= ENET_SOCKET_WAIT_SEND;
    else if ((wanted & ENET_SOCKET_WAIT_RECEIVE) && nk_socket_wait (timeout * 1000))
      * condition |= ENET_SOCKET_WAIT_RECEIVE;

    return 0;
}
