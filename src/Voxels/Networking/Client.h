#pragma once
#include "Interface.h"
#include "../ClassImplMacros.h"
#include "EnetFwd.h"

#include "entt/fwd.hpp"

#include <unordered_map>

namespace Networking
{
  class Client : public Interface
  {
  public:
    explicit Client(World& world, const char* hostName);
    ~Client() override;
    NO_COPY_NO_MOVE(Client);

    void ProcessMessages(World& world) final;
    void SendMessages(World& world) final;

    [[nodiscard]] ClientStatus GetStatus() const
    {
      return status_;
    }

  private:
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    void HandlePacket(World& world, const ENetPacket& packet);

    World* world_{};
    ENetHost* localHost_{};
    ENetPeer* remotePeer_{};
    ClientStatus status_ = ClientStatus::Resolving;
    std::unordered_map<entt::entity, entt::entity> remoteToLocalEntity_;
    std::unordered_map<entt::entity, entt::entity> localToRemoteEntity_;
  };
}
