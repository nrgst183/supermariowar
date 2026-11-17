/*
 ENet's platform functions on the original Xbox (its Winsock, XNet).

 Adapted from ENet's win32.c (MIT, Copyright (c) 2002-2024 Lee Salzman). The
 differences: the network stack is started by the SDL layer (sdl_core.cpp,
 XNet without its security, so Xboxes and PCs talk plain UDP), the Xbox's
 Winsock has no host name lookups (addresses are numbers) and no inet_ntoa,
 and the time comes from GetTickCount.
*/
#include <xtl.h>
#include <winsockx.h>

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

#include <string.h>
#include <stdio.h>

/* The header's types are the Winsock ones (see enet/enet.h) */
_Static_assert (sizeof (ENetSocket) == sizeof (SOCKET), "ENetSocket is a SOCKET");
_Static_assert (sizeof (ENetBuffer) == sizeof (WSABUF), "ENetBuffer is a WSABUF");
_Static_assert (sizeof (ENetSocketSet) == sizeof (fd_set) && ENET_XBOX_SOCKETSET_SIZE == FD_SETSIZE, "ENetSocketSet is an fd_set");

/* sdl_core.cpp: starts XNet once (its DHCP runs while the menus are up) */
extern int SDL_XboxStartNetwork (void);

static enet_uint32 timeBase = 0;

int
enet_initialize (void)
{
    WSADATA wsaData;

    if (! SDL_XboxStartNetwork ())
      return -1;

    if (WSAStartup (MAKEWORD (2, 2), & wsaData))
      return -1;

    return 0;
}

void
enet_deinitialize (void)
{
    WSACleanup ();
}

enet_uint32
enet_host_random_seed (void)
{
    enet_uint32 seed = 0;
    if (XNetRandom ((BYTE *) & seed, sizeof (seed)) != 0)
      seed = (enet_uint32) GetTickCount ();
    return seed;
}

enet_uint32
enet_time_get (void)
{
    return (enet_uint32) GetTickCount () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = (enet_uint32) GetTickCount () - newTimeBase;
}

int
enet_address_set_host_ip (ENetAddress * address, const char * name)
{
    enet_uint8 vals [4] = { 0, 0, 0, 0 };
    int i;

    for (i = 0; i < 4; ++ i)
    {
        const char * next = name + 1;
        if (* name != '0')
        {
            long val = strtol (name, (char **) & next, 10);
            if (val < 0 || val > 255 || next == name || next - name > 3)
              return -1;
            vals [i] = (enet_uint8) val;
        }

        if (* next != (i < 3 ? '.' : '\0'))
          return -1;
        name = next + 1;
    }

    memcpy (& address -> host, vals, sizeof (enet_uint32));
    return 0;
}

int
enet_address_set_host (ENetAddress * address, const char * name)
{
    /* (no name lookups on the Xbox's Winsock: addresses are numbers) */
    return enet_address_set_host_ip (address, name);
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    const enet_uint8 * b = (const enet_uint8 *) & address -> host;
    char text [16];
    int length = snprintf (text, sizeof (text), "%u.%u.%u.%u", b [0], b [1], b [2], b [3]);
    if (length < 0 || (size_t) length >= nameLength)
      return -1;
    memcpy (name, text, (size_t) length + 1);
    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    return enet_address_get_host_ip (address, name, nameLength);
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;

    memset (& sin, 0, sizeof (struct sockaddr_in));

    sin.sin_family = AF_INET;

    if (address != NULL)
    {
       sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
       sin.sin_addr.s_addr = address -> host;
    }
    else
    {
       sin.sin_port = 0;
       sin.sin_addr.s_addr = INADDR_ANY;
    }

    return bind (socket,
                 (struct sockaddr *) & sin,
                 sizeof (struct sockaddr_in)) == SOCKET_ERROR ? -1 : 0;
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    struct sockaddr_in sin;
    int sinLength = sizeof (struct sockaddr_in);

    if (getsockname (socket, (struct sockaddr *) & sin, & sinLength) == SOCKET_ERROR)
      return -1;

    address -> host = (enet_uint32) sin.sin_addr.s_addr;
    address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);

    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    return listen (socket, backlog < 0 ? SOMAXCONN : backlog) == SOCKET_ERROR ? -1 : 0;
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    SOCKET result = socket (AF_INET, type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
    return result == INVALID_SOCKET ? ENET_SOCKET_NULL : (ENetSocket) result;
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    int result = SOCKET_ERROR;
    switch (option)
    {
        case ENET_SOCKOPT_NONBLOCK:
        {
            u_long nonBlocking = (u_long) value;
            result = ioctlsocket (socket, FIONBIO, & nonBlocking);
            break;
        }

        case ENET_SOCKOPT_BROADCAST:
            result = setsockopt (socket, SOL_SOCKET, SO_BROADCAST, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_REUSEADDR:
            result = setsockopt (socket, SOL_SOCKET, SO_REUSEADDR, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_RCVBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_RCVBUF, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_SNDBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_SNDBUF, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_RCVTIMEO:
            result = setsockopt (socket, SOL_SOCKET, SO_RCVTIMEO, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_SNDTIMEO:
            result = setsockopt (socket, SOL_SOCKET, SO_SNDTIMEO, (char *) & value, sizeof (int));
            break;

        case ENET_SOCKOPT_NODELAY:
            result = setsockopt (socket, IPPROTO_TCP, TCP_NODELAY, (char *) & value, sizeof (int));
            break;

        default:
            /* (no IP_TTL on the Xbox) */
            break;
    }
    return result == SOCKET_ERROR ? -1 : 0;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    /* (the Xbox's Winsock has neither SO_ERROR nor IP_TTL) */
    (void) socket;
    (void) option;
    (void) value;
    return -1;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;
    int result;

    memset (& sin, 0, sizeof (struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
    sin.sin_addr.s_addr = address -> host;

    result = connect (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    if (result == SOCKET_ERROR && WSAGetLastError () != WSAEWOULDBLOCK)
      return -1;

    return 0;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    SOCKET result;
    struct sockaddr_in sin;
    int sinLength = sizeof (struct sockaddr_in);

    result = accept (socket,
                     address != NULL ? (struct sockaddr *) & sin : NULL,
                     address != NULL ? & sinLength : NULL);

    if (result == INVALID_SOCKET)
      return ENET_SOCKET_NULL;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return (ENetSocket) result;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    return shutdown (socket, (int) how) == SOCKET_ERROR ? -1 : 0;
}

void
enet_socket_destroy (ENetSocket socket)
{
    if (socket != ENET_SOCKET_NULL)
      closesocket (socket);
}

int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    struct sockaddr_in sin;
    DWORD sentLength = 0;

    if (address != NULL)
    {
        memset (& sin, 0, sizeof (struct sockaddr_in));

        sin.sin_family = AF_INET;
        sin.sin_port = ENET_HOST_TO_NET_16 (address -> port);
        sin.sin_addr.s_addr = address -> host;
    }

    if (WSASendTo (socket,
                   (LPWSABUF) buffers,
                   (DWORD) bufferCount,
                   & sentLength,
                   0,
                   address != NULL ? (struct sockaddr *) & sin : NULL,
                   address != NULL ? sizeof (struct sockaddr_in) : 0,
                   NULL,
                   NULL) == SOCKET_ERROR)
    {
       if (WSAGetLastError () == WSAEWOULDBLOCK)
         return 0;

       return -1;
    }

    return (int) sentLength;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    INT sinLength = sizeof (struct sockaddr_in);
    DWORD flags = 0,
          recvLength = 0;
    struct sockaddr_in sin;

    if (WSARecvFrom (socket,
                     (LPWSABUF) buffers,
                     (DWORD) bufferCount,
                     & recvLength,
                     & flags,
                     address != NULL ? (struct sockaddr *) & sin : NULL,
                     address != NULL ? & sinLength : NULL,
                     NULL,
                     NULL) == SOCKET_ERROR)
    {
       switch (WSAGetLastError ())
       {
       case WSAEWOULDBLOCK:
       case WSAECONNRESET:
          return 0;
       case WSAEINTR:
       case WSAEMSGSIZE:
          return -2;
       default:
          return -1;
       }
    }

    if (flags & MSG_PARTIAL)
      return -2;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return (int) recvLength;
}

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    struct timeval timeVal;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    return select ((int) maxSocket + 1, (fd_set *) readSet, (fd_set *) writeSet, NULL, & timeVal);
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    fd_set readSet, writeSet;
    struct timeval timeVal;
    int selectCount;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO (& readSet);
    FD_ZERO (& writeSet);

    if (* condition & ENET_SOCKET_WAIT_SEND)
      FD_SET (socket, & writeSet);

    if (* condition & ENET_SOCKET_WAIT_RECEIVE)
      FD_SET (socket, & readSet);

    selectCount = select ((int) socket + 1, & readSet, & writeSet, NULL, & timeVal);

    if (selectCount < 0)
      return -1;

    * condition = ENET_SOCKET_WAIT_NONE;

    if (selectCount == 0)
      return 0;

    if (FD_ISSET (socket, & writeSet))
      * condition |= ENET_SOCKET_WAIT_SEND;

    if (FD_ISSET (socket, & readSet))
      * condition |= ENET_SOCKET_WAIT_RECEIVE;

    return 0;
}
