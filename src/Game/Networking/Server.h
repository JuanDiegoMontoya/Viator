#pragma once
#include "Interface.h"
#include "ThreadSafeQueue.h"
#include "Core/ClassImplMacros.h"
#include "EnetFwd.h"

#include "entt/fwd.hpp"

#include <unordered_map>
#include <vector>

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
    void EnqueueRPC(RpcInfo rpc) final;

    auto GetNumberOfConnections() const
    {
      return connections_.size();
    }

  private:
    void FlushRPCs();
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    int32_t HandlePacket(World& world, ENetPeer* peer, const ENetPacket& packet);
    bool IsEntityOwnedByRemote(entt::entity entity) override;

    ENetHost* localHost_;

    struct ClientInfo
    {
      entt::entity entity;
      ClientStatus status;
    };
    std::unordered_map<ENetPeer*, ClientInfo> connections_;
    std::unordered_map<entt::entity, ENetPeer*> entityToConnection_;
    std::vector<entt::entity> removedEntities_;
    World* world_;
    ThreadSafeQueue<RpcInfo> rpcs_;

    // Number of ticks before broadcasting client network info.
    uint32_t networkInfoFlushInterval = 50;
    uint32_t networkInfoAccumulator   = 0;
  };
} // namespace Networking
