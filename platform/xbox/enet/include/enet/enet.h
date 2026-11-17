/*
 ENet on the original Xbox: its public header, with the platform part for the
 Xbox's Winsock (XNet).

 ENet's own enet.h picks its platform header with _WIN32, which the Xbox build
 doesn't define (the game's code is built without it). This header comes
 first on the include path of everything that uses ENet (the enet target puts
 this folder before ENet's): it supplies the platform types itself, without
 the Xbox headers (so the game's code doesn't get xtl.h's min/max macros and
 the like), then includes ENet's enet.h.

 The types match winsockx.h: a SOCKET is a 32-bit handle, an ENetBuffer is laid
 out like a WSABUF, an ENetSocketSet like an fd_set (enet_xbox.c checks it).
*/
#ifndef SMW_XBOX_ENET_H
#define SMW_XBOX_ENET_H

#include <stddef.h>
#include <stdlib.h>

/* ENet's Win32 header is replaced by this part (its include guard) */
#define __ENET_WIN32_H__

typedef unsigned int ENetSocket;

#define ENET_SOCKET_NULL ((ENetSocket) ~0u)

/* The Xbox is little-endian: network order is the bytes swapped */
#define ENET_HOST_TO_NET_16(value) ((unsigned short) __builtin_bswap16 ((unsigned short) (value)))
#define ENET_HOST_TO_NET_32(value) ((unsigned int) __builtin_bswap32 ((unsigned int) (value)))
#define ENET_NET_TO_HOST_16(value) ((unsigned short) __builtin_bswap16 ((unsigned short) (value)))
#define ENET_NET_TO_HOST_32(value) ((unsigned int) __builtin_bswap32 ((unsigned int) (value)))

typedef struct
{
    size_t dataLength;
    void * data;
} ENetBuffer;

#define ENET_CALLBACK __cdecl
#define ENET_API extern

#define ENET_XBOX_SOCKETSET_SIZE 64

typedef struct
{
    unsigned int fd_count;
    ENetSocket fd_array [ENET_XBOX_SOCKETSET_SIZE];
} ENetSocketSet;

static inline void enet_xbox_socketset_add (ENetSocketSet * set, ENetSocket socket)
{
    unsigned int i;
    for (i = 0; i < set -> fd_count; ++ i)
      if (set -> fd_array [i] == socket)
        return;
    if (set -> fd_count < ENET_XBOX_SOCKETSET_SIZE)
      set -> fd_array [set -> fd_count ++] = socket;
}

static inline void enet_xbox_socketset_remove (ENetSocketSet * set, ENetSocket socket)
{
    unsigned int i;
    for (i = 0; i < set -> fd_count; ++ i)
      if (set -> fd_array [i] == socket)
      {
          for (; i + 1 < set -> fd_count; ++ i)
            set -> fd_array [i] = set -> fd_array [i + 1];
          -- set -> fd_count;
          return;
      }
}

static inline int enet_xbox_socketset_check (const ENetSocketSet * set, ENetSocket socket)
{
    unsigned int i;
    for (i = 0; i < set -> fd_count; ++ i)
      if (set -> fd_array [i] == socket)
        return 1;
    return 0;
}

#define ENET_SOCKETSET_EMPTY(sockset)          ((sockset).fd_count = 0)
#define ENET_SOCKETSET_ADD(sockset, socket)    enet_xbox_socketset_add (& (sockset), (socket))
#define ENET_SOCKETSET_REMOVE(sockset, socket) enet_xbox_socketset_remove (& (sockset), (socket))
#define ENET_SOCKETSET_CHECK(sockset, socket)  enet_xbox_socketset_check (& (sockset), (socket))

/* ENet's enet.h, with the Win32 platform part (already supplied above) */
#ifdef _WIN32
#include_next <enet/enet.h>
#else
#define _WIN32 1
#include_next <enet/enet.h>
#undef _WIN32
#endif

#endif /* SMW_XBOX_ENET_H */
