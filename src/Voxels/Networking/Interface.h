#pragma once
#include "../ClassImplMacros.h"
#include "../Reflection.h"

#include "entt/meta/meta.hpp"

#include <vector>

class World;

namespace Networking
{
  struct RpcInfo
  {
    Core::Reflection::RpcTraits traits;
    entt::id_type funcId;
    std::vector<entt::meta_any> args;
  };

  class Interface
  {
  public:
    Interface() = default;
    NO_COPY_NO_MOVE(Interface);
    virtual ~Interface() = default;

    virtual void ProcessMessages(World&) = 0;
    virtual void SendMessages(World&)    = 0;
    virtual void EnqueueRPC(RpcInfo rpc) = 0;
  };

  enum class ClientStatus
  {
    Resolving,
    Joining,
    Connected,
    Disconnected,
  };

  enum class PacketType : uint8_t
  {
    // std::vector<entt::entity> + std::vector<char>
    EntityBundle,

    // entt::id_type + serialized args
    Rpc,

    RemovedEntity,
    InputState,
    TwoLevelGrid,
  };
}
