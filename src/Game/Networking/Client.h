#pragma once
#include "Interface.h"
#include "ThreadSafeQueue.h"
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
    void EnqueueRPC(RpcInfo rpc) final;
    bool IsEntityOwnedByRemote(entt::entity entity) override;

    [[nodiscard]] ClientStatus GetStatus() const
    {
      return status_;
    }

    [[nodiscard]] const auto& GetRemoteToLocalEntityMap() const
    {
      return remoteToLocalEntity_;
    }

  private:
    void FlushRPCs();
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    int32_t HandlePacket(World& world, const ENetPacket& packet);

    World* world_{};
    ENetHost* localHost_{};
    ENetPeer* remotePeer_{};
    ClientStatus status_ = ClientStatus::Resolving;
    std::unordered_map<entt::entity, entt::entity> remoteToLocalEntity_;
    std::unordered_map<entt::entity, entt::entity> localToRemoteEntity_;
    ThreadSafeQueue<RpcInfo> rpcs_;
  };
}
