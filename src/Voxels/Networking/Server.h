#pragma once
#include "Interface.h"
#include "../ClassImplMacros.h"
#include "EnetFwd.h"

#include "entt/fwd.hpp"

#include <unordered_map>

class World;

namespace Networking
{
  class Server : public Interface
  {
  public:
    explicit Server(World& world);
    ~Server() override;
    NO_COPY_NO_MOVE(Server);

    void ProcessMessages(World& world) final;
    void SendMessages(World& world) final;

  private:
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    void HandlePacket(World& world, ENetPeer* peer, const ENetPacket& packet);

    ENetHost* localHost_;

    struct ClientInfo
    {
      entt::entity entity;
      ClientStatus status;
    };
    std::unordered_map<ENetPeer*, ClientInfo> connections_;
    World* world_;
  };
} // namespace Networking
