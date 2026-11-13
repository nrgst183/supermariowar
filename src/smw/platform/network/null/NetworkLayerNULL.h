#ifndef NETWORK_LAYER_NULL_H
#define NETWORK_LAYER_NULL_H

#include "network/NetworkLayer.h"

class NetworkLayerNULL : public NetworkLayer
{
    public:
        bool init() override { return false; }
        void cleanup() override {}

        bool client_restart() override { return false; }
        void client_listen(NetworkEventHandler&) override {}
        void client_shutdown() override {}

        bool gamehost_restart() override { return false; }
        void gamehost_listen(NetworkEventHandler&) override {}
        void gamehost_shutdown() override {}

        bool connectToLobbyServer(const char*, uint16_t) override { return false; }
        bool connectToForeignGameHost(const char*, uint16_t) override { return false; }
        bool natPunch(uint32_t host, uint16_t port) override { return false; };
};

#endif // NETWORK_LAYER_NULL_H
