#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#include <cstddef>

// define platform guards here
#ifdef NETWORK_DISABLED
    #include "platform/network/null/NetworkLayerNULL.h"
    #define NetworkHandler NetworkLayerNULL
#else
    #include "platform/network/enet/NetworkLayerENet.h"
    #define NetworkHandler NetworkLayerENet
#endif

// Declarations shared between the translation units that implement
// the networking code (net.cpp, NetClient*.cpp and NetGameHost.cpp).

// Defined in net.cpp
extern NetworkHandler networkHandler;
bool isPackageComplete(size_t dataLength, size_t expectedLength, const char* packageName);

#endif // NET_INTERNAL_H
