#ifndef NETWORK_LAYER_ENET_H
#define NETWORK_LAYER_ENET_H

#include "network/NetworkLayer.h"

#include <enet/enet.h>

#include <stdint.h>     // uintN_t


class NetworkLayerENet : public NetworkLayer
{
    public:
        NetworkLayerENet();
        ~NetworkLayerENet();

        bool init() override;

        bool client_restart() override;
        void client_listen(NetworkEventHandler&) override;
        void client_shutdown() override;

        bool gamehost_restart() override;
        void gamehost_listen(NetworkEventHandler&) override;
        void gamehost_shutdown() override;

        void cleanup() override;

        bool connectToLobbyServer(const char* hostname, const uint16_t port) override;
        bool connectToForeignGameHost(const char* hostname, const uint16_t port) override;
        bool natPunch(uint32_t host, uint16_t port) override;

    private:
        ENetHost* local_client;
        ENetEvent last_client_event;

        ENetHost* local_gamehost;
        ENetEvent last_gamehost_event;

        bool openConnection(const char* hostname, const uint16_t port);
        void listen(ENetHost*, ENetEvent&, NetworkEventHandler&);
};

#endif // NETWORK_LAYER_ENET_H
