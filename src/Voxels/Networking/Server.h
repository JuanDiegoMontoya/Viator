#pragma once
#include "Interface.h"
#include "ThreadSafeQueue.h"
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
    void EnqueueRPC(RpcInfo rpc) final;

    auto GetNumberOfConnections() const
    {
      return connections_.size();
    }

  private:
    void FlushRPCs();
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    void HandlePacket(World& world, ENetPeer* peer, const ENetPacket& packet);

    ENetHost* localHost_;

    struct ClientInfo
    {
      entt::entity entity;
      ClientStatus status;
    };
    std::unordered_map<ENetPeer*, ClientInfo> connections_;
    std::unordered_map<entt::entity, ENetPeer*> entityToConnection_;
    World* world_;
    ThreadSafeQueue<RpcInfo> rpcs_;
  };
} // namespace Networking
